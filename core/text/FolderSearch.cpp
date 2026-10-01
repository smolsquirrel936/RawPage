#include "core/text/FolderSearch.h"

#include "core/Log.h"
#include "core/doc/PageTree.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>
#include <chrono>

namespace rawpage::core::text {

FolderSearch::FolderSearch(IDocumentBackend& backend, render::RenderScheduler& scheduler, DocumentIndex& index,
                           SearchEngine& engine)
    : backend_(backend), scheduler_(scheduler), index_(index), engine_(engine), session_(engine)
{
    // Removed by TOKEN in the destructor -- convention 10's threaded form, as every scheduler client does.
    taskToken_ = scheduler_.subscribeTasks([this](render::RenderTicket ticket) { onTask(ticket); });
    helper_ = std::thread([this] { helperLoop(); });
}

FolderSearch::~FolderSearch()
{
    // A RUN IN PROGRESS STOPS, and what it had in flight cleans up after itself (the header). Then the
    // helper DRAINS its queue -- a piece queued there owns an opening, and running it is how that opening
    // is released -- and is joined before anything it reads is destroyed.
    cancel();
    scheduler_.unsubscribeTasks(taskToken_);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    helper_.join();
}

// =====================================================================================================
// The helper thread
// =====================================================================================================

void FolderSearch::runHelper(std::function<void()> work, std::function<void()> deliver)
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(HelperJob{std::move(work), std::move(deliver)});
    }
    wake_.notify_all();
}

void FolderSearch::helperLoop()
{
    for (;;) {
        HelperJob job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;   // stopping, and drained
            job = std::move(queue_.front());
            queue_.pop_front();
            helperBusy_ = true;
        }
        if (job.work) job.work();
        WakeHandler wake;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (!stopping_) completions_.push_back(std::move(job.deliver));
            wake = wakeHandler_;
        }
        if (wake) wake();
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            helperBusy_ = false;
            if (queue_.empty()) idle_.notify_all();
        }
    }
}

int FolderSearch::deliverReady()
{
    std::deque<std::function<void()>> ready;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(completions_);
    }
    for (const std::function<void()>& deliver : ready)
        if (deliver) deliver();
    return static_cast<int>(ready.size());
}

void FolderSearch::setWakeHandler(WakeHandler handler)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    wakeHandler_ = std::move(handler);
}

void FolderSearch::setPieceObserver(PieceObserver observer)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    pieceObserver_ = std::move(observer);
}

bool FolderSearch::waitForIdle(int milliseconds)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return idle_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                          [this] { return queue_.empty() && !helperBusy_; });
}

// =====================================================================================================
// The run
// =====================================================================================================

Status FolderSearch::start(Request request, Handlers handlers)
{
    cancel();
    // THE FIND BAR'S REFUSALS, before anything is listed or opened: an empty pattern and a regular
    // expression that does not compile search nothing and say so (guide A-57's rule).
    if (const Result<CompiledQuery> compiled = SearchEngine::compile(request.query); !compiled)
        return compiled.error();
    if (request.files.isEmpty() && !QFileInfo(request.folder).isDir())
        return Error{ErrorCode::NotFound, QLatin1String("error.search.noFolder"), request.folder};

    const std::uint64_t run = ++run_;
    running_ = true;
    request_ = std::move(request);
    handlers_ = std::move(handlers);
    files_.clear();
    nextFile_ = 0;
    hits_ = 0;
    summary_ = Summary{};

    // LISTED ON THE HELPER, which is the only thread here that touches the file system: a folder of
    // thousands is a wait the UI thread must not take. Sorted, so a run is reproducible.
    auto out = std::make_shared<QStringList>();
    if (!request_.files.isEmpty()) {
        *out = request_.files;
        runHelper({}, [this, run, out] { listed(run, *out); });
        return {};
    }
    runHelper(
        [out, folder = request_.folder, recursive = request_.recursive] {
            QDirIterator walk(folder, QStringList{QStringLiteral("*.pdf")}, QDir::Files,
                              recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
            while (walk.hasNext()) out->push_back(walk.next());
            std::sort(out->begin(), out->end(), [](const QString& a, const QString& b) {
                const int folded = QString::compare(a, b, Qt::CaseInsensitive);
                return folded != 0 ? folded < 0 : a < b;
            });
        },
        [this, run, out] { listed(run, *out); });
    return {};
}

void FolderSearch::cancel()
{
    if (!running_) return;
    running_ = false;
    ++run_;   // everything still to arrive for the run is now an older run's, and dropped
    session_.cancel();
    if (flight_) {
        // THE PIECE IN FLIGHT: whichever of it and us finishes second cleans up (`Flight`).
        bool finished = false;
        {
            const std::lock_guard<std::mutex> lock(flight_->mutex);
            finished = flight_->finished;
            if (!finished) flight_->orphaned = true;
        }
        if (finished) cleanUp(flight_->result, flight_->opening);
        flight_.reset();
    }
    closeCurrent();
}

void FolderSearch::listed(std::uint64_t run, QStringList files)
{
    if (run != run_ || !running_) return;
    files_ = std::move(files);
    if (handlers_.progress) handlers_.progress(Progress{0, static_cast<int>(files_.size()), 0});
    nextFile();
}

void FolderSearch::nextFile()
{
    if (!running_) return;
    const int maxResults = request_.query.maxResults;
    if (nextFile_ >= files_.size() || summary_.limitReached || (maxResults > 0 && hits_ >= maxResults)) {
        finishRun();
        return;
    }
    currentPath_ = files_.at(nextFile_++);
    // NO PASSWORD CALLBACK: a folder search never prompts (D5-13); an encrypted file is refused by the
    // engine with `PermissionDenied`, and listed as skipped.
    const Result<OpenProgress> begun = backend_.beginOpen(currentPath_, PasswordCallback{});
    if (!begun) {
        refused(begun.error());
        return;
    }
    step(std::make_shared<Flight>(), begun.value());
}

void FolderSearch::step(std::shared_ptr<Flight> flight, const OpenProgress& progress)
{
    if (progress.done) {
        flight_.reset();
        opened(progress.handle);
        return;
    }
    flight->opening = progress.opening;
    flight_ = flight;

    PieceObserver observer;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        observer = pieceObserver_;
    }
    const bool engine = progress.nextNeedsEngine;
    // THE PIECE, run wherever it belongs. It holds the backend and its flight, never `this`: a run that is
    // cancelled while it runs is cleaned up by the piece itself.
    auto piece = [&backend = backend_, flight, observer, engine] {
        const auto began = std::chrono::steady_clock::now();
        Result<OpenProgress> result = backend.continueOpen(flight->opening);
        if (observer)
            observer(engine, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
        const std::lock_guard<std::mutex> lock(flight->mutex);
        flight->result = result;
        flight->finished = true;
        if (!flight->orphaned || !result) return;   // a refusal was already dropped by the backend
        if (result.value().done)
            (void)backend.close(result.value().handle);   // never attached: nothing else holds it
        else
            backend.abandonOpen(flight->opening);
    };

    const std::uint64_t run = run_;
    if (engine) {
        // AN ENGINE PIECE, as a task of no document -- the opening has no handle yet -- at `Background`,
        // behind every tile anybody is looking at.
        const render::RenderTicket ticket =
            scheduler_.enqueueTask(DocumentHandle{}, render::RenderPriority::Background, std::move(piece));
        engineFlights_[ticket.id] = flight;
        (void)run;
    } else {
        runHelper(std::move(piece), [this, run, flight] { pieceDone(run, flight); });
    }
}

void FolderSearch::onTask(render::RenderTicket ticket)
{
    const auto found = engineFlights_.find(ticket.id);
    if (found == engineFlights_.end()) return;   // a close or a clean-up, or not ours
    const std::shared_ptr<Flight> flight = found->second;
    engineFlights_.erase(found);
    pieceDone(run_, flight);
}

void FolderSearch::pieceDone(std::uint64_t run, const std::shared_ptr<Flight>& flight)
{
    // AN OLDER RUN'S PIECE: `cancel()` and the piece itself have already cleaned it up between them.
    if (run != run_ || flight != flight_) return;
    flight_.reset();
    const Result<OpenProgress> result = flight->result;
    if (!result) {
        refused(result.error());
        return;
    }
    step(std::make_shared<Flight>(), result.value());
}

void FolderSearch::opened(DocumentHandle handle)
{
    handle_ = handle;
    const PageTree* pages = backend_.pages(handle);
    const int pageCount = pages != nullptr ? pages->count() : 0;
    // ONE IDENTITY, ONE ENTRY: a file also open in a tab shares that tab's text rather than extracting it
    // twice (D5-4).
    entry_ = index_.attach(backend_.documentId(handle), handle, pageCount);

    SearchQuery query = request_.query;
    if (query.maxResults > 0) query.maxResults = std::max(1, query.maxResults - hits_);
    const std::uint64_t run = run_;
    SearchEngine::Handlers handlers;
    handlers.finished = [this, run](const SearchEngine::Outcome& outcome) {
        if (run == run_ && running_) searched(outcome);
    };
    if (const Status started = session_.start(entry_, query, std::move(handlers)); !started) {
        closeCurrent();
        refused(started.error());
    }
}

void FolderSearch::searched(const SearchEngine::Outcome& outcome)
{
    FileResult file;
    file.path = currentPath_;
    if (const Metadata* metadata = backend_.metadata(handle_)) file.metadata = *metadata;
    if (const PageTree* pages = backend_.pages(handle_)) file.pageCount = pages->count();
    int order = 0;
    for (const SearchHit& hit : outcome.hits) {
        ResultHit result;
        result.hit = hit;
        result.order = order++;
        if (hit.unit) {
            result.snippet = SearchResults::snippetOf(hit.unit->text, hit.within);
        } else if (const std::shared_ptr<const IndexedPage> page = index_.page(entry_, hit.page)) {
            result.snippet = SearchResults::snippetOf(*page->reading(), hit.reading);
        }
        file.hits.push_back(std::move(result));
    }
    hits_ += static_cast<int>(file.hits.size());
    ++summary_.filesSearched;
    summary_.hits = hits_;
    const int maxResults = request_.query.maxResults;
    // THE BOUND ACROSS FILES (the header): this file had more than was left, or it used the last of it with
    // files still unsearched -- either way the list may not be every hit there is, and it says so.
    if (outcome.limitReached || (maxResults > 0 && hits_ >= maxResults && nextFile_ < files_.size()))
        summary_.limitReached = true;

    closeCurrent();
    const std::uint64_t run = run_;
    if (handlers_.file) handlers_.file(std::move(file));
    if (run != run_ || !running_) return;   // the handler cancelled or started another search
    if (handlers_.progress)
        handlers_.progress(Progress{nextFile_, static_cast<int>(files_.size()), hits_});
    if (run != run_ || !running_) return;
    nextFile();
}

void FolderSearch::refused(const Error& error)
{
    RAWPAGE_LOG_INFO(LogSubsystem::Render)
        << "folder search: not searched" << currentPath_ << "--" << QString(error.messageKey);
    FileResult file;
    file.path = currentPath_;
    file.refused = error;
    ++summary_.filesRefused;
    const std::uint64_t run = run_;
    if (handlers_.file) handlers_.file(std::move(file));
    if (run != run_ || !running_) return;
    if (handlers_.progress)
        handlers_.progress(Progress{nextFile_, static_cast<int>(files_.size()), hits_});
    if (run != run_ || !running_) return;
    nextFile();
}

void FolderSearch::closeCurrent()
{
    if (!handle_.isValid()) return;
    const DocumentHandle handle = handle_;
    handle_ = {};
    entry_ = 0;
    // CANCEL, THEN AN EXCLUSIVE CLOSE ON THE RENDER THREAD (the header): what was already inside the engine
    // for this file finishes first, and the UI thread drains nothing.
    index_.detach(handle);
    scheduler_.cancelDocument(handle);
    (void)scheduler_.enqueueTask(
        handle, render::RenderPriority::Background, [&backend = backend_, handle] { (void)backend.close(handle); },
        true);
}

void FolderSearch::cleanUp(const Result<OpenProgress>& result, OpeningId opening)
{
    if (!result) return;   // the backend dropped a refused opening itself
    if (result.value().done) {
        const DocumentHandle handle = result.value().handle;
        (void)scheduler_.enqueueTask(
            handle, render::RenderPriority::Background, [&backend = backend_, handle] { (void)backend.close(handle); },
            true);
        return;
    }
    (void)scheduler_.enqueueTask(DocumentHandle{}, render::RenderPriority::Background,
                                 [&backend = backend_, opening] { backend.abandonOpen(opening); });
}

void FolderSearch::finishRun()
{
    running_ = false;
    if (handlers_.finished) handlers_.finished(summary_);
}

} // namespace rawpage::core::text
