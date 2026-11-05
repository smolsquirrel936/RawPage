// core/doc/SplitRunner -- implementation. Stage 7 T7. The header says where each fact comes from.

#include "core/doc/SplitRunner.h"

#include "core/Log.h"
#include "core/doc/Outline.h"
#include "core/doc/PageTree.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace rawpage::core {

// One run's state. A blank page's answer is written on the render thread into its own slot and read on the pumping
// thread only after the scheduler says the task ran -- the scheduler's delivery is the hand-off.
struct SplitRunner::Run {
    DocumentHandle document;
    SplitRequest request;
    Done done;
    std::unique_ptr<AssemblySource> source;
    int pageCount = 0;

    // BlankPages
    struct Answers {
        std::mutex mutex;
        std::vector<int> state;   // -1 unread, 0 has content, 1 blank, 2 unreadable
    };
    std::shared_ptr<Answers> answers;
    std::map<std::uint64_t, int> tickets;   // in flight: ticket -> page
    int nextPage = 0;
    int answered = 0;

    // TextPattern
    std::optional<text::SearchEngine::SearchId> search;
};

SplitRunner::SplitRunner(IDocumentBackend& backend, render::RenderScheduler& scheduler, text::SearchEngine& search)
    : backend_(backend), scheduler_(scheduler), search_(search)
{
    taskSubscription_ = scheduler_.subscribeTasks([this](render::RenderTicket ticket) { onTaskDone(ticket); });
}

SplitRunner::~SplitRunner()
{
    cancel();
    scheduler_.unsubscribeTasks(taskSubscription_);
}

DocumentHandle SplitRunner::runningFor() const noexcept
{
    return run_ ? run_->document : DocumentHandle{};
}

void SplitRunner::cancel()
{
    if (!run_) return;
    for (const auto& [ticket, page] : run_->tickets) scheduler_.cancel(render::RenderTicket{ticket});
    if (run_->search) search_.cancel(*run_->search);
    run_.reset();
}

std::vector<QString> SplitRunner::fileNames(const SplitRequest& request, std::size_t count)
{
    std::vector<int> numbers;
    for (std::size_t i = 0; i < count; ++i) numbers.push_back(static_cast<int>(i) + 1);
    return split::numberedNames(request.namePattern, request.stem, numbers, static_cast<int>(count));
}

Status SplitRunner::start(DocumentHandle document, text::DocumentIndex::EntryId entry, SplitRequest request, Done done)
{
    if (run_) return Error{ErrorCode::Unavailable, QLatin1String("error.split.busy"), QString()};
    if (!done) return Error{ErrorCode::InvalidArgument, QLatin1String("error.split.busy"), QString()};
    // The request's own arguments, before anything is snapshotted.
    switch (request.method) {
    case SplitMethod::EveryN:
        if (request.pagesPerFile < 1) return Error{ErrorCode::InvalidArgument, QLatin1String("error.split.badCount"), QString::number(request.pagesPerFile)};
        break;
    case SplitMethod::BookmarkLevel:
        if (request.level < 1) return Error{ErrorCode::InvalidArgument, QLatin1String("error.split.badLevel"), QString::number(request.level)};
        break;
    case SplitMethod::MaxSize:
        if (request.maxBytes <= 0) return Error{ErrorCode::InvalidArgument, QLatin1String("error.split.badSize"), QString::number(request.maxBytes)};
        break;
    case SplitMethod::TextPattern:
        if (const auto compiled = text::SearchEngine::compile(request.pattern); !compiled) return compiled.error();
        break;
    case SplitMethod::TopLevelBookmarks:
    case SplitMethod::BlankPages:
        break;
    }
    if (request.folder.isEmpty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.assembly.badName"), QString()};

    Result<std::unique_ptr<AssemblySource>> source = backend_.assemblySource(document);
    if (!source) return source.error();

    auto run = std::make_unique<Run>();
    run->document = document;
    run->request = std::move(request);
    run->done = std::move(done);
    run->source = std::move(source).value();
    run->pageCount = run->source->pageCount();
    run_ = std::move(run);
    Run& current = *run_;

    switch (current.request.method) {
    case SplitMethod::EveryN:
        finish(split::everyN(current.pageCount, current.request.pagesPerFile));
        return {};
    case SplitMethod::TopLevelBookmarks:
    case SplitMethod::BookmarkLevel: {
        const Outline* outline = backend_.outline(document);
        const int level = current.request.method == SplitMethod::TopLevelBookmarks ? 1 : current.request.level;
        finish(split::atBookmarks(current.pageCount, outline ? *outline : Outline{}, level));
        return {};
    }
    case SplitMethod::MaxSize: {
        AssemblySource* snapshot = current.source.get();
        finish(split::bySize(current.pageCount, current.request.maxBytes, [snapshot](const std::vector<int>& pages) -> Result<qint64> {
            Result<AssembledDocument> made = snapshot->assemble(pages);
            if (!made) return made.error();
            return static_cast<qint64>(made.value().bytes.size());
        }));
        return {};
    }
    case SplitMethod::BlankPages:
        current.answers = std::make_shared<Run::Answers>();
        current.answers->state.assign(static_cast<std::size_t>(current.pageCount), -1);
        queueBlankTasks();
        return {};
    case SplitMethod::TextPattern: {
        text::SearchQuery query = current.request.pattern;
        query.scope = text::SearchScope::wholeDocument();
        query.classes = text::SearchClasses{};
        query.maxResults = 0;   // every page: a bound would stop the split short
        text::SearchEngine::Handlers handlers;
        handlers.finished = [this](const text::SearchEngine::Outcome& outcome) {
            if (!run_) return;
            run_->search.reset();
            if (!outcome.failedPages.empty()) {
                finish(Result<std::vector<SplitPart>>(Error{ErrorCode::IoFailure, QLatin1String("error.split.unreadable"),
                                                            QString::number(outcome.failedPages.front() + 1)}));
                return;
            }
            std::vector<int> starts;
            for (const text::SearchHit& hit : outcome.hits) starts.push_back(hit.page);
            finish(split::atStarts(run_->pageCount, starts));
        };
        Result<text::SearchEngine::SearchId> started = search_.start(entry, query, std::move(handlers));
        if (!started) {
            run_.reset();
            return started.error();
        }
        current.search = started.value();
        return {};
    }
    }
    return {};
}

void SplitRunner::queueBlankTasks()
{
    Run& run = *run_;
    while (run.nextPage < run.pageCount && static_cast<int>(run.tickets.size()) < kTaskWindow) {
        const int page = run.nextPage++;
        std::shared_ptr<Run::Answers> answers = run.answers;
        IDocumentBackend* backend = &backend_;
        const DocumentHandle document = run.document;
        const render::RenderTicket ticket = scheduler_.enqueueTask(document, render::RenderPriority::Visible, [backend, document, page, answers] {
            const Result<PdfRect> box = backend->contentBox(document, page);
            const int state = box ? 0 : (box.error().code == ErrorCode::NotFound ? 1 : 2);
            const std::lock_guard<std::mutex> guard(answers->mutex);
            answers->state[static_cast<std::size_t>(page)] = state;
        });
        if (!ticket.isValid()) {
            finish(Result<std::vector<SplitPart>>(Error{ErrorCode::Unavailable, QLatin1String("error.split.unreadable"), QString::number(page + 1)}));
            return;
        }
        run.tickets.emplace(ticket.id, page);
    }
}

void SplitRunner::onTaskDone(render::RenderTicket ticket)
{
    if (!run_) return;
    const auto found = run_->tickets.find(ticket.id);
    if (found == run_->tickets.end()) return;
    run_->tickets.erase(found);
    ++run_->answered;
    if (run_->answered < run_->pageCount) {
        queueBlankTasks();
        return;
    }
    std::vector<bool> blank;
    {
        const std::lock_guard<std::mutex> guard(run_->answers->mutex);
        for (std::size_t page = 0; page < run_->answers->state.size(); ++page) {
            const int state = run_->answers->state[page];
            if (state != 0 && state != 1) {
                finish(Result<std::vector<SplitPart>>(Error{ErrorCode::IoFailure, QLatin1String("error.split.unreadable"),
                                                            QString::number(static_cast<int>(page) + 1)}));
                return;
            }
            blank.push_back(state == 1);
        }
    }
    finish(split::atBlankPages(blank));
}

void SplitRunner::finish(std::optional<Result<std::vector<SplitPart>>> plan)
{
    if (!run_ || !plan) return;
    std::unique_ptr<Run> run = std::move(run_);   // the run ends here, whatever `done` does next
    if (!*plan) {
        run->done(plan->error());
        return;
    }
    // THE DOCUMENT MUST STILL BE THE SNAPSHOT'S: a page command since `start` makes the facts another document's.
    const PageTree* pages = backend_.pages(run->document);
    if (pages == nullptr || pages->count() != run->pageCount) {
        run->done(Error{ErrorCode::Unavailable, QLatin1String("error.split.documentChanged"), QString()});
        return;
    }
    SplitOutcome outcome;
    outcome.parts = std::move(plan->value());
    if (run->request.method == SplitMethod::BlankPages) {
        int kept = 0;
        for (const SplitPart& part : outcome.parts) kept += static_cast<int>(part.pages.size());
        outcome.blankPagesLeftOut = run->pageCount - kept;
    }
    const std::vector<QString> names = fileNames(run->request, outcome.parts.size());
    std::vector<AssemblyOutput> outputs;
    for (std::size_t at = 0; at < outcome.parts.size(); ++at) outputs.push_back(AssemblyOutput{outcome.parts[at].pages, names[at]});
    Result<std::vector<WrittenDocument>> written = run->source->write(outputs, run->request.folder);
    if (!written) {
        run->done(written.error());
        return;
    }
    outcome.written = std::move(written).value();
    RAWPAGE_LOG_INFO(LogSubsystem::Save) << "split: wrote" << static_cast<int>(outcome.written.size()) << "file(s) into"
                                         << run->request.folder;
    run->done(std::move(outcome));
}

} // namespace rawpage::core
