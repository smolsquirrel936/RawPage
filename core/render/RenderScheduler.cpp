#include "core/render/RenderScheduler.h"

#include "core/Log.h"
#include "core/render/RenderCache.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace rawpage::core::render {

const char* renderPriorityName(RenderPriority priority) noexcept
{
    switch (priority) {
    case RenderPriority::Visible:     return "visible";
    case RenderPriority::Prefetch:    return "prefetch";
    case RenderPriority::Speculative: return "speculative";
    }
    return "?";
}

RenderScheduler::RenderScheduler(RasterizerFactory factory, RenderCache* cache, Config config)
    : factory_(std::move(factory)), cache_(cache), config_(config)
{
    if (config_.workerCount < 1) config_.workerCount = 1;
    if (config_.maxPending < 1) config_.maxPending = 1;
    activeWorkerLimit_ = config_.workerCount;

    workers_.reserve(static_cast<std::size_t>(config_.workerCount));
    for (int i = 0; i < config_.workerCount; ++i)
        workers_.emplace_back([this, i] { workerLoop(i); });
}

RenderScheduler::~RenderScheduler()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        // Everything queued is abandoned. What is RUNNING cannot be stopped -- there is no
        // cancellation inside `FPDF_RenderPageBitmap` -- so the join below waits for it, which is
        // the whole reason a tile is 256x256 rather than a page.
        pending_.clear();
    }
    workAvailable_.notify_all();
    // JOIN BEFORE ANY MEMBER IS DESTROYED. Convention 10's threaded form: a worker still holding
    // `this` while the members it reads are being destroyed is a use-after-free that reproduces
    // once in fifty closes, which is exactly the shape of Stage 1's I-12 with a thread in it.
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

RenderTicket RenderScheduler::enqueue(const RenderRequest& request, RenderPriority priority)
{
    // D2-12: a request with no real `Revision` cannot be a cache key, so rasterizing it would spend
    // the work to produce a tile nothing can ever find again. Refused by ticket rather than
    // silently ignored, so the caller can tell.
    if (!request.isValid()) return RenderTicket{};

    // The cache is consulted on THIS thread -- see `RenderCache.h`. A hit still travels back
    // through `deliverReady()` rather than being handed over here: a subscriber called from inside
    // `enqueue()` would re-enter a paint handler that has not finished asking for its tiles.
    if (cache_ != nullptr) {
        if (auto hit = cache_->lookup(request)) {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::uint64_t ticket = nextTicketId_++;
            ++stats_.enqueued;
            ++stats_.servedFromCache;
            completed_.push_back(Completion{request, std::move(*hit), true, true});
            return RenderTicket{ticket};
        }
    }

    std::unique_lock<std::mutex> lock(mutex_);
    ++stats_.enqueued;
    const std::uint64_t ticket = nextTicketId_++;

    const auto existing = byRequest_.find(request);
    if (existing != byRequest_.end()) {
        // COALESCED. Two viewports over one tile is one rasterization (plan 5.3's second property).
        Job& job = jobs_[existing->second];
        job.tickets.push_back(ticket);
        ticketToJob_[ticket] = existing->second;
        ++stats_.coalesced;
        // A visible request that lands on a queued prefetch RAISES it. Without this, a tile that
        // scrolled into view waits behind every other prefetch already in the queue -- the tile is
        // rendered, so nothing looks broken, and the page simply fills in from the wrong end.
        if (!job.started && static_cast<int>(priority) < static_cast<int>(job.priority)) {
            job.priority = priority;
            ++stats_.priorityRaised;
        }
        return RenderTicket{ticket};
    }

    if (pending_.size() >= config_.maxPending) dropLowestPriorityLocked();

    const std::uint64_t id = nextJobId_++;
    Job job;
    job.request = request;
    job.priority = priority;
    job.sequence = nextSequence_++;
    job.tickets.push_back(ticket);
    jobs_.emplace(id, std::move(job));
    byRequest_.emplace(request, id);
    ticketToJob_[ticket] = id;
    pending_.push_back(id);

    lock.unlock();
    workAvailable_.notify_one();
    return RenderTicket{ticket};
}

void RenderScheduler::dropLowestPriorityLocked()
{
    if (pending_.empty()) return;
    // The LOWEST priority and, among equals, the OLDEST -- which is the most speculative thing in
    // the queue. Refusing the incoming request instead would mean a full queue of prefetches
    // refusing a visible tile, and the user would watch the page they are looking at stay blank
    // while the machine rendered pages they are not.
    auto worst = pending_.end();
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        const auto candidate = jobs_.find(*it);
        if (candidate == jobs_.end()) continue;
        if (worst == pending_.end()) {
            worst = it;
            continue;
        }
        const Job& a = candidate->second;
        const Job& b = jobs_.find(*worst)->second;
        if (static_cast<int>(a.priority) > static_cast<int>(b.priority)
            || (a.priority == b.priority && a.sequence < b.sequence))
            worst = it;
    }
    if (worst == pending_.end()) return;

    const auto victim = jobs_.find(*worst);
    if (victim != jobs_.end()) {
        for (const std::uint64_t ticket : victim->second.tickets) ticketToJob_.erase(ticket);
        byRequest_.erase(victim->second.request);
        jobs_.erase(victim);
    }
    pending_.erase(worst);
    ++stats_.droppedOverflow;
}

void RenderScheduler::cancel(RenderTicket ticket)
{
    if (!ticket.isValid()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = ticketToJob_.find(ticket.id);
    if (found == ticketToJob_.end()) return;
    const std::uint64_t id = found->second;
    ticketToJob_.erase(found);

    const auto jobIt = jobs_.find(id);
    if (jobIt == jobs_.end()) return;
    Job& job = jobIt->second;
    job.tickets.erase(std::remove(job.tickets.begin(), job.tickets.end(), ticket.id),
                      job.tickets.end());
    // ANOTHER CALLER STILL WANTS IT. Two viewports over one tile share a job, and one of them
    // scrolling away must not take the other's pixels with it.
    if (!job.tickets.empty()) return;

    if (job.started) {
        // Cannot be stopped; marked, and its result is dropped on delivery rather than cached
        // (plan 5.3). Caching it would be the tempting optimization and is wrong twice over: it
        // spends memory on a tile nobody asked for, and after a `documentChanged` it is a stale
        // tile that a later lookup would happily serve.
        job.cancelled = true;
        return;
    }

    pending_.erase(std::remove(pending_.begin(), pending_.end(), id), pending_.end());
    byRequest_.erase(job.request);
    jobs_.erase(jobIt);
    ++stats_.cancelledBeforeStart;
    idle_.notify_all();
}

void RenderScheduler::cancelAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const std::uint64_t id : pending_) {
        const auto found = jobs_.find(id);
        if (found == jobs_.end()) continue;
        for (const std::uint64_t ticket : found->second.tickets) ticketToJob_.erase(ticket);
        byRequest_.erase(found->second.request);
        jobs_.erase(found);
        ++stats_.cancelledBeforeStart;
    }
    pending_.clear();
    for (auto& [id, job] : jobs_) {
        (void)id;
        if (job.started) job.cancelled = true;
    }
    idle_.notify_all();
}

void RenderScheduler::cancelDocument(DocumentHandle document)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::uint64_t> keep;
    keep.reserve(pending_.size());
    for (const std::uint64_t id : pending_) {
        const auto found = jobs_.find(id);
        if (found == jobs_.end()) continue;
        if (found->second.request.document != document) {
            keep.push_back(id);
            continue;
        }
        for (const std::uint64_t ticket : found->second.tickets) ticketToJob_.erase(ticket);
        byRequest_.erase(found->second.request);
        jobs_.erase(found);
        ++stats_.cancelledBeforeStart;
    }
    pending_ = std::move(keep);
    for (auto& [id, job] : jobs_) {
        (void)id;
        if (job.started && job.request.document == document) job.cancelled = true;
    }
    idle_.notify_all();
}

void RenderScheduler::documentChanged(DocumentHandle document, const Scope& scope,
                                      Revision newRevision)
{
    // The cache first, on this thread, for `RenderCache.h`'s reason.
    if (cache_ != nullptr) cache_->invalidate(document, scope, newRevision);

    std::lock_guard<std::mutex> lock(mutex_);
    if (newRevision.isValid()) revisions_[document.id] = newRevision;

    std::vector<std::uint64_t> keep;
    keep.reserve(pending_.size());
    for (const std::uint64_t id : pending_) {
        const auto found = jobs_.find(id);
        if (found == jobs_.end()) continue;
        if (!isSupersededLocked(found->second.request)) {
            keep.push_back(id);
            continue;
        }
        for (const std::uint64_t ticket : found->second.tickets) ticketToJob_.erase(ticket);
        byRequest_.erase(found->second.request);
        jobs_.erase(found);
        ++stats_.cancelledBeforeStart;
    }
    pending_ = std::move(keep);

    for (auto& [id, job] : jobs_) {
        (void)id;
        if (job.started && isSupersededLocked(job.request)) job.superseded = true;
    }

    // A completion already sitting in the queue for a request the mutation superseded must go too.
    // This is the narrowest window in the whole class -- the tile finished while the mutation was
    // being applied -- and it is exactly the one that puts a stale pixel on screen.
    completed_.erase(std::remove_if(completed_.begin(), completed_.end(),
                                    [this](const Completion& completion) {
                                        return isSupersededLocked(completion.request);
                                    }),
                     completed_.end());
    idle_.notify_all();
}

bool RenderScheduler::isSupersededLocked(const RenderRequest& request) const
{
    const auto found = revisions_.find(request.document.id);
    if (found == revisions_.end()) return false;
    return request.revision != found->second;
}

RenderScheduler::SubscriptionToken RenderScheduler::subscribeReady(ReadyHandler handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const SubscriptionToken token = nextToken_++;
    subscribers_.emplace_back(token, std::move(handler));
    return token;
}

void RenderScheduler::unsubscribeReady(SubscriptionToken token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
                                      [token](const auto& entry) { return entry.first == token; }),
                       subscribers_.end());
}

void RenderScheduler::setWakeHandler(WakeHandler handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    wake_ = std::move(handler);
}

int RenderScheduler::deliverReady()
{
    std::deque<Completion> batch;
    std::vector<std::pair<SubscriptionToken, ReadyHandler>> subscribers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(completed_);
        subscribers = subscribers_;
    }

    int delivered = 0;
    for (Completion& completion : batch) {
        if (!completion.usable) continue;
        // Inserted here rather than by the worker, which is what makes `RenderCache` single-
        // threaded by construction rather than by a lock. A completion that CAME from the cache is
        // not written back: re-inserting it would count as an insertion, and the eviction rate O7
        // is measured on would become a function of how often tiles were looked at.
        if (cache_ != nullptr && !completion.fromCache)
            cache_->put(completion.request, completion.image);
        for (const auto& [token, handler] : subscribers) {
            (void)token;
            if (handler) handler(completion.request, completion.image);
        }
        ++delivered;
    }

    if (delivered > 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.delivered += delivered;
    }
    return delivered;
}

void RenderScheduler::setActiveWorkerLimit(int limit)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        activeWorkerLimit_ = std::clamp(limit, 1, config_.workerCount);
    }
    workAvailable_.notify_all();
}

int RenderScheduler::activeWorkerLimit() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return activeWorkerLimit_;
}

void RenderScheduler::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = true;
}

void RenderScheduler::resume()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = false;
    }
    workAvailable_.notify_all();
}

bool RenderScheduler::isPaused() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_;
}

bool RenderScheduler::waitForIdle(int milliseconds)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return idle_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                          [this] { return pending_.empty() && running_ == 0; });
}

std::size_t RenderScheduler::pendingCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

std::size_t RenderScheduler::runningCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

RenderScheduler::Stats RenderScheduler::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

std::uint64_t RenderScheduler::selectNextLocked(int workerIndex) const
{
    if (paused_ || stopping_) return 0;
    if (workerIndex >= activeWorkerLimit_) return 0;
    if (pending_.empty()) return 0;

    // Strict priority, FIFO within a priority. A linear scan rather than a heap: the queue holds a
    // viewport's worth of tiles -- tens, not thousands -- and a heap whose keys change when a
    // prefetch is raised to visible is a data structure with a second, subtler correctness problem.
    std::uint64_t best = 0;
    const Job* bestJob = nullptr;
    for (const std::uint64_t id : pending_) {
        const auto found = jobs_.find(id);
        if (found == jobs_.end()) continue;
        const Job& job = found->second;
        if (bestJob == nullptr || static_cast<int>(job.priority) < static_cast<int>(bestJob->priority)
            || (job.priority == bestJob->priority && job.sequence < bestJob->sequence)) {
            best = id;
            bestJob = &job;
        }
    }
    return best;
}

void RenderScheduler::workerLoop(int index)
{
    // ONE RASTERIZER PER WORKER, created ON the worker thread. Creating them all up front on the
    // constructing thread would be the natural shape and is the wrong one: `pdf/render/`'s engine
    // objects belong to whoever will call them, and I-05's finding is precisely that PDFium is
    // fussier about threads than it looks.
    std::unique_ptr<IPageRasterizer> rasterizer = factory_ ? factory_() : nullptr;

    for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);
        std::uint64_t id = 0;
        workAvailable_.wait(lock, [this, index, &id] {
            if (stopping_) return true;
            id = selectNextLocked(index);
            return id != 0;
        });
        if (stopping_) return;

        auto jobIt = jobs_.find(id);
        if (jobIt == jobs_.end()) continue;
        jobIt->second.started = true;
        pending_.erase(std::remove(pending_.begin(), pending_.end(), id), pending_.end());
        ++running_;
        ++stats_.started;
        const RenderRequest request = jobIt->second.request;
        lock.unlock();

        Result<RasterImage> result = rasterizer != nullptr
                                         ? rasterizer->raster(request)
                                         : Result<RasterImage>(Error{ErrorCode::Unavailable,
                                                                     kBackendNotImplementedKey,
                                                                     QStringLiteral("IPageRasterizer")});

        lock.lock();
        --running_;
        ++stats_.completed;
        auto finished = jobs_.find(id);
        const bool cancelled = finished != jobs_.end() && finished->second.cancelled;
        const bool superseded =
            (finished != jobs_.end() && finished->second.superseded) || isSupersededLocked(request);

        if (finished != jobs_.end()) {
            for (const std::uint64_t ticket : finished->second.tickets) ticketToJob_.erase(ticket);
            byRequest_.erase(finished->second.request);
            jobs_.erase(finished);
        }

        if (!result) {
            ++stats_.failed;
        } else if (cancelled) {
            ++stats_.droppedCancelled;
        } else if (superseded) {
            ++stats_.droppedSuperseded;
        } else {
            completed_.push_back(Completion{request, std::move(result.value()), true, false});
        }

        const bool nowIdle = pending_.empty() && running_ == 0;
        WakeHandler wake = wake_;
        lock.unlock();

        if (nowIdle) idle_.notify_all();
        // Called OUTSIDE the lock and carrying nothing: it is the only outward call a worker makes,
        // and a handler that took data or reached back into the scheduler would put a UI object on
        // this thread -- which is the bug this whole arrangement exists to make impossible.
        if (wake) wake();
    }
}

} // namespace rawpage::core::render
