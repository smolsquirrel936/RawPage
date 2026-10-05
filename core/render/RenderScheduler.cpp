#include "core/render/RenderScheduler.h"

#include "core/Log.h"
#include "core/render/RenderCache.h"
#include "core/text/PageTextModel.h"

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
    case RenderPriority::Background:  return "background";
    }
    return "?";
}

RenderScheduler::RenderScheduler(RasterizerFactory factory, TextExtractorFactory textFactory,
                                 RenderCache* cache, Config config)
    : factory_(std::move(factory)), textFactory_(std::move(textFactory)), cache_(cache),
      config_(config)
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

namespace {

// One place both `enqueue` paths -- the cache hit and the real queue -- count the lane. A cache hit
// is still a request made AT a priority, and leaving it out would make the counters disagree with
// `enqueued` for exactly the workload (a second window over an already-rendered document) the
// counters were added to measure.
void noteLaneLocked(RenderScheduler::Stats& stats, RenderPriority priority)
{
    switch (priority) {
    case RenderPriority::Visible: ++stats.enqueuedVisible; break;
    case RenderPriority::Prefetch: ++stats.enqueuedPrefetch; break;
    case RenderPriority::Speculative: ++stats.enqueuedSpeculative; break;
    case RenderPriority::Background: ++stats.enqueuedBackground; break;
    }
}

} // namespace

RenderTicket RenderScheduler::enqueue(const RenderRequest& request, RenderPriority priority,
                                      CachePolicy cachePolicy)
{
    // D2-12: a request with no real `Revision` cannot be a cache key, so rasterizing it would spend
    // the work to produce a tile nothing can ever find again. Refused by ticket rather than
    // silently ignored, so the caller can tell.
    if (!request.isValid()) return RenderTicket{};

    // The cache is consulted on THIS thread -- see `RenderCache.h`. A hit still travels back
    // through `deliverReady()` rather than being handed over here: a subscriber called from inside
    // `enqueue()` would re-enter a paint handler that has not finished asking for its tiles.
    // A BYPASSING tile is not looked up: a comparison's raster that happened to hit would still be
    // right, but the lookup is itself a statistic O7 is read from (D5-4), and a consumer that does
    // not keep tiles should not appear in the cache's figures at all.
    if (cache_ != nullptr && cachePolicy == CachePolicy::Use) {
        if (auto hit = cache_->lookup(request)) {
            std::uint64_t ticket = 0;
            WakeHandler wake;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ticket = nextTicketId_++;
                ++stats_.enqueued;
                noteLaneLocked(stats_, priority);
                ++stats_.servedFromCache;
                // Stamped even though the lookup already succeeded: a hit still travels back
                // through `deliverReady()`, so it has a real queued latency (a short one), and a
                // probe that skipped hits would report the pipeline as slower than it is.
                Completion completion;
                completion.request = request;
                completion.image = std::move(*hit);
                completion.usable = true;
                completion.fromCache = true;
                completion.priority = priority;
                completion.enqueuedAt = std::chrono::steady_clock::now();
                completed_.push_back(std::move(completion));
                wake = wake_;
            }

            // AND WAKE, because this is the second producer of a completion and the worker loop is
            // the other. `completed_` is drained only by `deliverReady()`, and in the running
            // product the only thing that posts `deliverReady()` is this handler -- so a batch of
            // requests that are ALL cache hits starts no worker, wakes nobody, and strands its
            // tiles in the queue. That is issue I-19: the mixed-DPI drag re-keys every tile on
            // `deviceScale`, dragging BACK finds them all cached, and the page stays blank until
            // some unrelated MISS happens to wake the pump and drain them.
            //
            // Waking is not delivering, so the rule above still holds: the handler posts, and the
            // queued call runs after the caller's `requestTiles()` has finished asking.
            if (wake) wake();
            return RenderTicket{ticket};
        }
    }

    std::unique_lock<std::mutex> lock(mutex_);
    ++stats_.enqueued;
    noteLaneLocked(stats_, priority);
    if (cachePolicy == CachePolicy::Bypass) ++stats_.enqueuedBypassingCache;
    const std::uint64_t ticket = nextTicketId_++;

    const auto existing = byRequest_.find(request);
    if (existing != byRequest_.end()) {
        // COALESCED. Two viewports over one tile is one rasterization (plan 5.3's second property).
        Job& job = jobs_[existing->second];
        job.tickets.push_back(ticket);
        // One holder that wants the tile KEPT is enough to keep it.
        if (cachePolicy == CachePolicy::Use) job.cacheable = true;
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
    job.cacheable = cachePolicy == CachePolicy::Use;
    job.sequence = nextSequence_++;
    job.enqueuedAt = std::chrono::steady_clock::now();
    job.tickets.push_back(ticket);
    jobs_.emplace(id, std::move(job));
    byRequest_.emplace(request, id);
    ticketToJob_[ticket] = id;
    pending_.push_back(id);

    lock.unlock();
    workAvailable_.notify_one();
    return RenderTicket{ticket};
}

RenderTicket RenderScheduler::enqueueText(const TextRequest& request, RenderPriority priority)
{
    // Refused by an invalid ticket, as `enqueue` refuses an invalid tile: a caller can tell.
    if (!request.isValid()) return RenderTicket{};

    std::unique_lock<std::mutex> lock(mutex_);
    ++stats_.enqueued;
    ++stats_.textEnqueued;
    noteLaneLocked(stats_, priority);
    const std::uint64_t ticket = nextTicketId_++;

    const auto existing = byText_.find(request);
    if (existing != byText_.end()) {
        // COALESCED, and RAISED when the new ask is more urgent -- `enqueue`'s rule, and here it is
        // the whole of D5-3's promise to a selection: the page a person is dragging over, asked at
        // `Visible`, must not wait behind an index build that happened to queue it at `Background`.
        Job& job = jobs_[existing->second];
        job.tickets.push_back(ticket);
        ticketToJob_[ticket] = existing->second;
        ++stats_.coalesced;
        if (!job.started && static_cast<int>(priority) < static_cast<int>(job.priority)) {
            job.priority = priority;
            ++stats_.priorityRaised;
        }
        return RenderTicket{ticket};
    }

    if (pending_.size() >= config_.maxPending) dropLowestPriorityLocked();

    const std::uint64_t id = nextJobId_++;
    Job job;
    job.kind = JobKind::Text;
    job.text = request;
    job.priority = priority;
    job.sequence = nextSequence_++;
    job.enqueuedAt = std::chrono::steady_clock::now();
    job.tickets.push_back(ticket);
    jobs_.emplace(id, std::move(job));
    byText_.emplace(request, id);
    ticketToJob_[ticket] = id;
    pending_.push_back(id);

    lock.unlock();
    workAvailable_.notify_one();
    return RenderTicket{ticket};
}

RenderTicket RenderScheduler::enqueueTask(DocumentHandle document, RenderPriority priority, EngineTask task,
                                          bool exclusive)
{
    // An INVALID document is a task of no document (S5 T9, the header); an exclusive one needs a document
    // to be exclusive against.
    if (!task || (exclusive && !document.isValid())) return RenderTicket{};

    std::unique_lock<std::mutex> lock(mutex_);
    ++stats_.enqueued;
    ++stats_.taskEnqueued;
    noteLaneLocked(stats_, priority);
    const std::uint64_t ticket = nextTicketId_++;

    // NEVER COALESCED (the header): a callable has no key, and its caller keeps the record of what it
    // asked. Otherwise exactly a text job's path.
    if (pending_.size() >= config_.maxPending) dropLowestPriorityLocked();

    const std::uint64_t id = nextJobId_++;
    Job job;
    job.kind = JobKind::Task;
    job.taskDocument = document;
    job.task = std::move(task);
    job.exclusive = exclusive;
    job.priority = priority;
    job.sequence = nextSequence_++;
    job.enqueuedAt = std::chrono::steady_clock::now();
    job.tickets.push_back(ticket);
    jobs_.emplace(id, std::move(job));
    ticketToJob_[ticket] = id;
    pending_.push_back(id);

    lock.unlock();
    workAvailable_.notify_one();
    return RenderTicket{ticket};
}

void RenderScheduler::forgetJobLocked(std::unordered_map<std::uint64_t, Job>::iterator job)
{
    for (const std::uint64_t ticket : job->second.tickets) ticketToJob_.erase(ticket);
    // BY KIND. A text job's coalescing key is in `byText_`; erasing its default-constructed
    // `request` from `byRequest_` instead would leave the text key behind, and the next ask for that
    // page would coalesce onto a job that no longer exists. A task has no key.
    if (job->second.kind == JobKind::Text) {
        byText_.erase(job->second.text);
    } else if (job->second.kind == JobKind::Raster) {
        byRequest_.erase(job->second.request);
    }
    jobs_.erase(job);
}

namespace {

template <typename JobOrCompletion>
DocumentHandle documentOf(const JobOrCompletion& item, bool isText, bool isTask)
{
    if (isTask) return item.taskDocument;
    return isText ? item.text.document : item.request.document;
}

} // namespace

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
    if (victim != jobs_.end()) forgetJobLocked(victim);
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
    forgetJobLocked(jobIt);
    ++stats_.cancelledBeforeStart;
    idle_.notify_all();
}

void RenderScheduler::cancelAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const std::uint64_t id : pending_) {
        const auto found = jobs_.find(id);
        if (found == jobs_.end()) continue;
        forgetJobLocked(found);
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
        const Job& job = found->second;
        if (documentOf(job, job.kind == JobKind::Text, job.kind == JobKind::Task) != document) {
            keep.push_back(id);
            continue;
        }
        forgetJobLocked(found);
        ++stats_.cancelledBeforeStart;
    }
    pending_ = std::move(keep);
    // TEXT JOBS AND TASKS TOO, and for the reason that makes this method exist: `close()`'s protocol is
    // cancel, drain, close, and a text job or a search-units task left running after it would be inside
    // the engine on a document the close is about to destroy.
    for (auto& [id, job] : jobs_) {
        (void)id;
        if (job.started && documentOf(job, job.kind == JobKind::Text, job.kind == JobKind::Task) == document)
            job.cancelled = true;
    }

    // AND WHAT HAS ALREADY FINISHED. The two steps above cover a tile that is queued and a tile
    // that is running; a tile that FINISHED and is waiting for the pump is the third state, and
    // until Stage 2 issue I-35 nothing covered it. The caller closing a tab runs cancel, drain,
    // `RenderCache::dropDocument()`, `close()` -- and the next `deliverReady()` then wrote these
    // completions into the cache under a handle nothing would ask for again, where they held the
    // memory budget until eviction. Measured in the product at ~4 MB per round of ten documents.
    completed_.erase(std::remove_if(completed_.begin(), completed_.end(),
                                    [this, document](const Completion& completion) {
                                        if (documentOf(completion, completion.kind == JobKind::Text,
                                                       completion.kind == JobKind::Task)
                                            != document)
                                            return false;
                                        // A cache hit was never rasterized, so it is not a
                                        // finished job being dropped -- `droppedCancelled` stays
                                        // a count of work the engine did for nothing.
                                        if (!completion.fromCache) ++stats_.droppedCancelled;
                                        return true;
                                    }),
                     completed_.end());
    idle_.notify_all();
}

bool RenderScheduler::rekeyLocked(std::unordered_map<std::uint64_t, Job>::iterator job, Revision revision)
{
    RenderRequest moved = job->second.request;
    moved.revision = revision;
    if (byRequest_.count(moved) != 0) return false;
    byRequest_.erase(job->second.request);
    job->second.request = moved;
    byRequest_.emplace(moved, job->first);
    ++stats_.rekeyed;
    return true;
}

void RenderScheduler::documentChanged(DocumentHandle document, const Scope& scope,
                                      Revision newRevision, const PageTree* pages)
{
    // The cache first, on this thread, for `RenderCache.h`'s reason.
    if (cache_ != nullptr) cache_->invalidate(document, scope, newRevision, pages);

    std::lock_guard<std::mutex> lock(mutex_);
    if (newRevision.isValid()) revisions_[document.id] = newRevision;

    // KEPT AT THE NEW REVISION (S6 T4): a tile of this document the scope does not reach. Its pixels
    // are the same either side of the mutation -- the engine may draw it from the old document or the
    // reloaded one, and both draw this tile alike.
    const auto unreached = [&](const RenderRequest& request) {
        return newRevision.isValid() && request.document == document
               && !tileTouchedBy(request, document, scope, pages);
    };

    std::vector<std::uint64_t> keep;
    keep.reserve(pending_.size());
    // TILES ONLY. A text job carries no revision because the text it reads cannot be changed by a
    // command at this stage (`TextRequest`'s header note); superseding it would throw away work that
    // is still right. A task likewise: its caller decides what a mutation makes stale.
    for (const std::uint64_t id : pending_) {
        const auto found = jobs_.find(id);
        if (found == jobs_.end()) continue;
        if (found->second.kind != JobKind::Raster || !isSupersededLocked(found->second.request)) {
            keep.push_back(id);
            continue;
        }
        if (unreached(found->second.request)) {
            if (rekeyLocked(found, newRevision)) {
                keep.push_back(id);
                continue;
            }
            // AN EQUAL REQUEST IS ALREADY QUEUED at the new revision: this job's holders join it, and the
            // more urgent lane and the wish to keep the tile go with them.
            RenderRequest moved = found->second.request;
            moved.revision = newRevision;
            Job& other = jobs_[byRequest_.at(moved)];
            for (const std::uint64_t ticket : found->second.tickets) {
                other.tickets.push_back(ticket);
                ticketToJob_[ticket] = byRequest_.at(moved);
            }
            found->second.tickets.clear();
            other.cacheable = other.cacheable || found->second.cacheable;
            if (!other.started && static_cast<int>(found->second.priority) < static_cast<int>(other.priority))
                other.priority = found->second.priority;
            forgetJobLocked(found);
            ++stats_.coalesced;
            continue;
        }
        forgetJobLocked(found);
        ++stats_.cancelledBeforeStart;
    }
    pending_ = std::move(keep);

    for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
        Job& job = it->second;
        if (!job.started || job.kind != JobKind::Raster || !isSupersededLocked(job.request)) continue;
        // A RUNNING tile the scope does not reach is delivered at the new revision; one it reaches, or
        // one an equal request already waits for, is dropped on delivery as before.
        if (unreached(job.request) && rekeyLocked(it, newRevision)) continue;
        job.superseded = true;
    }

    // A completion already sitting in the queue for a request the mutation superseded must go too.
    // This is the narrowest window in the whole class -- the tile finished while the mutation was
    // being applied -- and it is exactly the one that puts a stale pixel on screen. One the scope does
    // not reach is kept, at the new revision.
    for (Completion& completion : completed_) {
        if (completion.kind == JobKind::Raster && isSupersededLocked(completion.request)
            && unreached(completion.request)) {
            completion.request.revision = newRevision;
            ++stats_.rekeyed;
        }
    }
    completed_.erase(std::remove_if(completed_.begin(), completed_.end(),
                                    [this](const Completion& completion) {
                                        return completion.kind == JobKind::Raster
                                               && isSupersededLocked(completion.request);
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

RenderScheduler::SubscriptionToken RenderScheduler::subscribeText(TextReadyHandler handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const SubscriptionToken token = nextToken_++;
    textSubscribers_.emplace_back(token, std::move(handler));
    return token;
}

void RenderScheduler::unsubscribeText(SubscriptionToken token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    textSubscribers_.erase(std::remove_if(textSubscribers_.begin(), textSubscribers_.end(),
                                          [token](const auto& entry) { return entry.first == token; }),
                           textSubscribers_.end());
}

RenderScheduler::SubscriptionToken RenderScheduler::subscribeTasks(TaskDoneHandler handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const SubscriptionToken token = nextToken_++;
    taskSubscribers_.emplace_back(token, std::move(handler));
    return token;
}

void RenderScheduler::unsubscribeTasks(SubscriptionToken token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    taskSubscribers_.erase(std::remove_if(taskSubscribers_.begin(), taskSubscribers_.end(),
                                          [token](const auto& entry) { return entry.first == token; }),
                           taskSubscribers_.end());
}

void RenderScheduler::setWakeHandler(WakeHandler handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    wake_ = std::move(handler);
}

void RenderScheduler::setLatencyProbe(LatencyProbe probe)
{
    std::lock_guard<std::mutex> lock(mutex_);
    latencyProbe_ = std::move(probe);
}

int RenderScheduler::deliverReady()
{
    std::deque<Completion> batch;
    std::vector<std::pair<SubscriptionToken, ReadyHandler>> subscribers;
    std::vector<std::pair<SubscriptionToken, TextReadyHandler>> textSubscribers;
    std::vector<std::pair<SubscriptionToken, TaskDoneHandler>> taskSubscribers;
    LatencyProbe probe;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(completed_);
        subscribers = subscribers_;
        textSubscribers = textSubscribers_;
        taskSubscribers = taskSubscribers_;
        probe = latencyProbe_;
    }

    int delivered = 0;
    int textDelivered = 0;
    int taskDelivered = 0;
    for (Completion& completion : batch) {
        if (!completion.usable) continue;

        // A TASK RAN: its caller holds the result and is told which ticket. Not the cache, not the probe
        // -- the text job's rule, for the text job's reason.
        if (completion.kind == JobKind::Task) {
            for (const auto& [token, handler] : taskSubscribers) {
                (void)token;
                if (handler) handler(completion.taskTicket);
            }
            ++delivered;
            ++taskDelivered;
            continue;
        }

        // A PAGE'S TEXT: to the text subscribers and nowhere else -- not the cache (D5-4), and not
        // the latency probe, whose quantity is the TILE's queued latency (A2's) and would be
        // diluted by work nobody is watching.
        if (completion.kind == JobKind::Text) {
            if (completion.textResult) {
                for (const auto& [token, handler] : textSubscribers) {
                    (void)token;
                    if (handler) handler(completion.text, *completion.textResult);
                }
            }
            ++delivered;
            ++textDelivered;
            continue;
        }

        // BEFORE the subscribers, so a slow repaint is not counted as queueing. The subscriber is
        // what draws the tile, and folding its cost in here would make the number a function of
        // how expensive the painting is -- a different quantity, and not the one A2 is graded on.
        if (probe) {
            const std::chrono::duration<double, std::milli> queued =
                std::chrono::steady_clock::now() - completion.enqueuedAt;
            probe(completion.priority, queued.count());
        }
        // Inserted here rather than by the worker, which is what makes `RenderCache` single-
        // threaded by construction rather than by a lock. A completion that CAME from the cache is
        // not written back: re-inserting it would count as an insertion, and the eviction rate O7
        // is measured on would become a function of how often tiles were looked at.
        if (cache_ != nullptr && !completion.fromCache && completion.cacheable)
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
        stats_.textDelivered += textDelivered;
        stats_.taskDelivered += taskDelivered;
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
                          [this] { return pending_.empty() && running_ == 0 && handingOff_ == 0; });
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

    // EXCLUSIVE TASKS (S5 T9): which documents have a job running, and which have an exclusive one. A
    // job is running from `started` until its result is handed off, which is when it leaves `jobs_`.
    std::vector<DocumentHandle> runningFor;
    std::vector<DocumentHandle> exclusiveFor;
    for (const auto& [id, job] : jobs_) {
        (void)id;
        if (!job.started) continue;
        const DocumentHandle document = documentOf(job, job.kind == JobKind::Text, job.kind == JobKind::Task);
        runningFor.push_back(document);
        if (job.exclusive) exclusiveFor.push_back(document);
    }
    const auto among = [](const std::vector<DocumentHandle>& set, DocumentHandle document) {
        return std::find(set.begin(), set.end(), document) != set.end();
    };

    // Strict priority, FIFO within a priority. A linear scan rather than a heap: the queue holds a
    // viewport's worth of tiles -- tens, not thousands -- and a heap whose keys change when a
    // prefetch is raised to visible is a data structure with a second, subtler correctness problem.
    std::uint64_t best = 0;
    const Job* bestJob = nullptr;
    for (const std::uint64_t id : pending_) {
        const auto found = jobs_.find(id);
        if (found == jobs_.end()) continue;
        const Job& job = found->second;
        if (!exclusiveFor.empty() || job.exclusive) {
            const DocumentHandle document = documentOf(job, job.kind == JobKind::Text, job.kind == JobKind::Task);
            // Nothing of a document starts while its exclusive task runs, and an exclusive task does not
            // start while anything of its document runs.
            if (document.isValid() && among(exclusiveFor, document)) continue;
            if (job.exclusive && among(runningFor, document)) continue;
        }
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
    // Stage 5 T2 (D5-3): the text extractor, likewise one per worker and made on it.
    std::unique_ptr<ITextExtractor> extractor = textFactory_ ? textFactory_() : nullptr;

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
        // PRIORITY INVERSION, decided here and nowhere else (Stats, I-12): a more urgent job still pending.
        for (const std::uint64_t other : pending_) {
            if (other == id) continue;
            const auto waiting = jobs_.find(other);
            if (waiting != jobs_.end()
                && static_cast<int>(waiting->second.priority) < static_cast<int>(jobIt->second.priority)) {
                ++stats_.startedWhileMoreUrgentWaited;
                break;
            }
        }
        pending_.erase(std::remove(pending_.begin(), pending_.end(), id), pending_.end());
        ++running_;
        ++stats_.started;
        const JobKind kind = jobIt->second.kind;
        const RenderRequest request = jobIt->second.request;
        const TextRequest textRequest = jobIt->second.text;
        // Moved out: a task runs once, and its callable may hold what it reads into.
        EngineTask task = kind == JobKind::Task ? std::move(jobIt->second.task) : EngineTask{};
        const DocumentHandle taskDocument = jobIt->second.taskDocument;
        const RenderTicket taskTicket{jobIt->second.tickets.empty() ? 0 : jobIt->second.tickets.front()};
        lock.unlock();

        if (kind == JobKind::Task) {
            // ON THIS THREAD AND NO OTHER, like a page's text (D5-3): the task takes the engine lock itself.
            task();
            task = {};   // released here, on the worker, before anything is reported

            lock.lock();
            --running_;
            ++stats_.completed;
            ++stats_.taskCompleted;
            auto finished = jobs_.find(id);
            const bool cancelled = finished != jobs_.end() && finished->second.cancelled;
            if (finished != jobs_.end()) forgetJobLocked(finished);
            if (cancelled) {
                ++stats_.droppedCancelled;
            } else {
                Completion completion;
                completion.kind = JobKind::Task;
                completion.taskTicket = taskTicket;
                completion.taskDocument = taskDocument;
                completion.usable = true;
                completed_.push_back(std::move(completion));
            }

            WakeHandler wake = wake_;
            ++handingOff_;
            lock.unlock();
            if (wake) wake();
            lock.lock();
            --handingOff_;
            const bool nowIdle = pending_.empty() && running_ == 0 && handingOff_ == 0;
            lock.unlock();
            if (nowIdle) idle_.notify_all();
            continue;
        }

        if (kind == JobKind::Text) {
            // A PAGE'S TEXT, on this thread and no other (D5-3, guide case A-24). The model is built
            // here and handed over SHARED and const: from the moment it leaves this block nothing
            // may write to it, because the index and a selection may both be reading it.
            auto result = std::make_shared<TextResult>(Error{ErrorCode::Unavailable,
                                                             kBackendNotImplementedKey,
                                                             QStringLiteral("ITextExtractor")});
            if (extractor != nullptr) {
                auto model = std::make_shared<text::PageTextModel>(textRequest.pageIndex);
                const Status status =
                    extractor->extractPage(textRequest.document, textRequest.pageIndex, *model);
                if (status) {
                    *result = TextResult(std::shared_ptr<const text::PageTextModel>(std::move(model)));
                } else {
                    *result = TextResult(status.error());
                }
            }

            lock.lock();
            --running_;
            ++stats_.completed;
            ++stats_.textCompleted;
            if (!*result) {
                ++stats_.failed;
                ++stats_.textFailed;
            }
            auto finished = jobs_.find(id);
            const bool cancelled = finished != jobs_.end() && finished->second.cancelled;
            if (finished != jobs_.end()) forgetJobLocked(finished);
            if (cancelled) {
                ++stats_.droppedCancelled;
            } else {
                Completion completion;
                completion.kind = JobKind::Text;
                completion.text = textRequest;
                completion.textResult = std::move(result);
                completion.usable = true;
                completed_.push_back(std::move(completion));
            }

            WakeHandler wake = wake_;
            ++handingOff_;
            lock.unlock();
            if (wake) wake();
            lock.lock();
            --handingOff_;
            const bool nowIdle = pending_.empty() && running_ == 0 && handingOff_ == 0;
            lock.unlock();
            if (nowIdle) idle_.notify_all();
            continue;
        }

        Result<RasterImage> result = rasterizer != nullptr
                                         ? rasterizer->raster(request)
                                         : Result<RasterImage>(Error{ErrorCode::Unavailable,
                                                                     kBackendNotImplementedKey,
                                                                     QStringLiteral("IPageRasterizer")});

        lock.lock();
        --running_;
        ++stats_.completed;
        auto finished = jobs_.find(id);
        // READ BEFORE THE ERASE BELOW, which is the only reason these two lines are here rather
        // than at the push: the job is gone by the time the completion is built.
        const RenderPriority lane =
            finished != jobs_.end() ? finished->second.priority : RenderPriority::Visible;
        const std::chrono::steady_clock::time_point enqueuedAt =
            finished != jobs_.end() ? finished->second.enqueuedAt
                                    : std::chrono::steady_clock::now();
        const bool cancelled = finished != jobs_.end() && finished->second.cancelled;
        const bool cacheable = finished == jobs_.end() || finished->second.cacheable;
        // THE JOB'S REQUEST AS IT IS NOW, not as it was when the worker took it: a mutation the tile did
        // not reach re-keyed it to the new revision while it ran (S6 T4), and it is delivered under that.
        const RenderRequest delivered = finished != jobs_.end() ? finished->second.request : request;
        const bool superseded =
            (finished != jobs_.end() && finished->second.superseded) || isSupersededLocked(delivered);

        if (finished != jobs_.end()) forgetJobLocked(finished);

        if (!result) {
            ++stats_.failed;
        } else if (cancelled) {
            ++stats_.droppedCancelled;
        } else if (superseded) {
            ++stats_.droppedSuperseded;
        } else {
            Completion completion;
            completion.request = delivered;
            completion.image = std::move(result.value());
            completion.usable = true;
            completion.priority = lane;
            completion.enqueuedAt = enqueuedAt;
            completion.cacheable = cacheable;
            completed_.push_back(std::move(completion));
        }

        WakeHandler wake = wake_;
        ++handingOff_;
        lock.unlock();

        // Called OUTSIDE the lock and carrying nothing: it is the only outward call a worker makes,
        // and a handler that took data or reached back into the scheduler would put a UI object on
        // this thread -- which is the bug this whole arrangement exists to make impossible.
        if (wake) wake();

        // IDLE IS ANNOUNCED AFTER THE WAKE, not before it. Until 2026-09-14 the worker decremented
        // `running_`, released the lock and only then called the handler, so `waitForIdle()` could
        // return in that gap and its caller find no wake yet. The ASan build is slow enough to land
        // in it: CI run 34772420519 failed "render scheduler WAKES for a tile it served from the
        // cache" on its control, `wakes > 0`, with 0.
        lock.lock();
        --handingOff_;
        const bool nowIdle = pending_.empty() && running_ == 0 && handingOff_ == 0;
        lock.unlock();

        if (nowIdle) idle_.notify_all();
    }
}

} // namespace rawpage::core::render
