#include "core/text/DocumentIndex.h"

#include "core/Log.h"
#include "core/text/LayoutAnalyzer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>

namespace rawpage::core::text {
namespace {

// ESTIMATES, from the containers' own sizes. The budget is a promise about the order of magnitude a
// person set, not an accounting of every allocator header; what matters is that the estimate grows with
// what is kept and is the same number every time for the same page (guide A-65 asserts on it).
std::size_t modelBytes(const PageTextModel& model)
{
    std::size_t bytes = sizeof(PageTextModel) + model.chars().capacity() * sizeof(TextChar)
                        + model.rules().capacity() * sizeof(PdfRect);
    for (const FontIdentity& font : model.fonts())
        bytes += sizeof(FontIdentity) + static_cast<std::size_t>(font.name.capacity()) * sizeof(QChar);
    return bytes;
}

std::size_t readingBytes(const PageReading& reading)
{
    // Per position: the model index, the break before it, its line and its block.
    std::size_t bytes = sizeof(PageReading)
                        + static_cast<std::size_t>(reading.size())
                              * (3 * sizeof(int) + sizeof(ReadingItem::Kind));
    const PageLayout& layout = reading.layout();
    for (const TextLine& line : layout.lines)
        bytes += sizeof(TextLine) + line.chars.capacity() * sizeof(int) + line.gapBefore.capacity() / 8
                 + sizeof(Span);
    for (const TextBlock& block : layout.blocks)
        bytes += sizeof(TextBlock) + block.lines.capacity() * sizeof(int) + sizeof(Span);
    bytes += layout.order.capacity() * sizeof(int) + layout.tables.capacity() * sizeof(TableGrid)
             + layout.unplaced.capacity() * sizeof(int);
    return bytes;
}

std::size_t unitPartBytes(const SearchUnitPart& part)
{
    std::size_t bytes = sizeof(SearchUnitPart) + part.units.capacity() * sizeof(SearchUnit);
    for (const SearchUnit& unit : part.units)
        bytes += static_cast<std::size_t>(unit.text.capacity() + unit.locator.capacity()) * sizeof(QChar);
    return bytes;
}

} // namespace

// =====================================================================================================
// SearchablePage
// =====================================================================================================

std::shared_ptr<const SearchablePage> SearchablePage::build(const PageReading& reading, NormalizeOptions options)
{
    auto page = std::shared_ptr<SearchablePage>(new SearchablePage);
    page->model_ = reading.sharedModel();   // S7 T3: the index is the model's, which follows its page
    page->options_ = options;

    // The normalizer maps to MODEL indices (T4's contract); a hit becomes a span of READING positions, so
    // the map is turned round once here, per page, rather than once per hit.
    const PageTextModel& model = reading.model();
    std::vector<int> positionOf(model.size(), -1);
    for (int position = 0; position < reading.size(); ++position) {
        const int index = reading.modelIndex(position);
        if (index >= 0 && static_cast<std::size_t>(index) < positionOf.size())
            positionOf[static_cast<std::size_t>(index)] = position;
    }
    const auto toPosition = [&](int index) {
        return index >= 0 && static_cast<std::size_t>(index) < positionOf.size()
                   ? positionOf[static_cast<std::size_t>(index)]
                   : -1;
    };

    // ONE NORMALIZATION PER BLOCK, which is what keeps a match inside one (the header). A block's lines
    // are still one stream to the normalizer, so D5-5's hyphen join and its line-break space both apply.
    const TextNormalizer normalizer(options);
    const std::vector<ReadingItem> stream = LayoutAnalyzer::readingStream(reading.layout());
    std::vector<ReadingItem> block;
    const auto flush = [&] {
        if (block.empty()) return;
        NormalizedText normalized = normalizer.normalize(model, block);
        block.clear();
        if (normalized.text.isEmpty()) return;
        SearchableBlock out;
        out.firstPos.resize(normalized.first.size());
        out.lastPos.resize(normalized.last.size());
        for (std::size_t u = 0; u < normalized.first.size(); ++u) {
            out.firstPos[u] = toPosition(normalized.first[u]);
            out.lastPos[u] = toPosition(normalized.last[u]);
        }
        out.text = std::move(normalized.text);
        page->bytes_ += sizeof(SearchableBlock) + static_cast<std::size_t>(out.text.capacity()) * sizeof(QChar)
                        + (out.firstPos.capacity() + out.lastPos.capacity()) * sizeof(int);
        page->blocks_.push_back(std::move(out));
    };
    for (const ReadingItem& item : stream) {
        if (item.kind == ReadingItem::Kind::BlockBreak) {
            flush();
            continue;
        }
        block.push_back(item);
    }
    flush();
    page->bytes_ += sizeof(SearchablePage);
    return page;
}

// =====================================================================================================
// IndexedPage
// =====================================================================================================

IndexedPage::IndexedPage(std::shared_ptr<const PageTextModel> model, bool failed)
    : model_(model ? std::move(model) : std::make_shared<const PageTextModel>()), failed_(failed)
{
    bytes_.store(sizeof(IndexedPage) + modelBytes(*model_), std::memory_order_relaxed);
}

std::shared_ptr<const PageReading> IndexedPage::reading() const
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (reading_) return reading_;
    }
    // BUILT OUTSIDE THE LOCK (the class comment): the analyzer runs for milliseconds on a dense page, and
    // a selection on the UI thread must not queue behind a worker doing the same thing.
    std::shared_ptr<const PageReading> built = PageReading::build(model_);
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!reading_) {
        reading_ = std::move(built);
        bytes_.fetch_add(readingBytes(*reading_), std::memory_order_relaxed);
    }
    return reading_;
}

std::shared_ptr<const SearchablePage> IndexedPage::searchable(NormalizeOptions options) const
{
    const std::size_t at = slot(options);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (searchable_[at]) return searchable_[at];
    }
    const std::shared_ptr<const PageReading> page = reading();
    std::shared_ptr<const SearchablePage> built = SearchablePage::build(*page, options);
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!searchable_[at]) {
        searchable_[at] = std::move(built);
        bytes_.fetch_add(searchable_[at]->approxBytes(), std::memory_order_relaxed);
    }
    return searchable_[at];
}

bool IndexedPage::hasReading() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return reading_ != nullptr;
}

bool IndexedPage::hasSearchable(NormalizeOptions options) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return searchable_[slot(options)] != nullptr;
}

// =====================================================================================================
// The releaser (Stage 5 guide I-13)
// =====================================================================================================

// ONE THREAD, A QUEUE OF BATCHES, AND NOTHING ELSE: it holds the last reference to what the index let go,
// and lets go of it there. It shares nothing with the index but the batches it is handed, so it needs no
// lock of the index's -- the shared pointers' own counts are what make a page outlive it when something
// else still holds it.
class DocumentIndex::Releaser {
public:
    Releaser() : thread_([this] { loop(); }) {}

    ~Releaser()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        thread_.join();   // drains the queue first, held or not
    }

    void push(std::vector<std::shared_ptr<const void>> batch)
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(batch));
        }
        wake_.notify_all();
    }

    void hold(bool on)
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            held_ = on;
        }
        wake_.notify_all();
    }

    bool waitIdle(int milliseconds)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return idle_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                              [this] { return queue_.empty() && !busy_; });
    }

private:
    void loop()
    {
        for (;;) {
            std::vector<std::shared_ptr<const void>> batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || (!held_ && !queue_.empty()); });
                if (queue_.empty()) return;   // stopping, and drained
                batch = std::move(queue_.front());
                queue_.pop_front();
                busy_ = true;
            }
            batch.clear();   // THE FREEING, here and not on the thread that dropped the document
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                busy_ = false;
                if (queue_.empty()) idle_.notify_all();
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<std::vector<std::shared_ptr<const void>>> queue_;
    bool stopping_ = false;
    bool held_ = false;
    bool busy_ = false;
    std::thread thread_;   // LAST: it starts in the constructor and reads everything above
};

void DocumentIndex::release(std::vector<std::shared_ptr<const void>> held)
{
    if (held.empty()) return;
    stats_.releasedAway += static_cast<long long>(held.size());
    if (!releaser_) releaser_ = std::make_unique<Releaser>();
    releaser_->push(std::move(held));
}

void DocumentIndex::holdReleasesForTesting(bool hold)
{
    if (!releaser_) releaser_ = std::make_unique<Releaser>();
    releaser_->hold(hold);
}

bool DocumentIndex::waitForReleases(int milliseconds)
{
    return !releaser_ || releaser_->waitIdle(milliseconds);
}

// =====================================================================================================
// DocumentIndex
// =====================================================================================================

DocumentIndex::DocumentIndex(render::RenderScheduler& scheduler) : scheduler_(scheduler)
{
    // Removed by TOKEN in the destructor -- convention 10's threaded form, as every scheduler client does.
    schedulerToken_ = scheduler_.subscribeText(
        [this](const render::TextRequest& request, const render::TextResult& result) { onText(request, result); });
    taskToken_ = scheduler_.subscribeTasks([this](render::RenderTicket ticket) { onTask(ticket); });
}

DocumentIndex::~DocumentIndex()
{
    scheduler_.unsubscribeText(schedulerToken_);
    scheduler_.unsubscribeTasks(taskToken_);
    for (auto& [id, entry] : entries_) {
        (void)id;
        for (auto& [page, pending] : entry.pending) {
            (void)page;
            if (pending.ticket.isValid()) scheduler_.cancel(pending.ticket);
        }
    }
}

DocumentIndex::Entry* DocumentIndex::find(EntryId entry)
{
    const auto found = entries_.find(entry);
    return found == entries_.end() ? nullptr : &found->second;
}

const DocumentIndex::Entry* DocumentIndex::find(EntryId entry) const
{
    const auto found = entries_.find(entry);
    return found == entries_.end() ? nullptr : &found->second;
}

DocumentIndex::Entry* DocumentIndex::entryForHandle(DocumentHandle handle)
{
    const auto found = byHandle_.find(handle.id);
    return found == byHandle_.end() ? nullptr : find(found->second);
}

DocumentIndex::EntryId DocumentIndex::attach(const DocumentId* identity, DocumentHandle handle, int pageCount)
{
    if (!handle.isValid()) return 0;
    if (const auto known = byHandle_.find(handle.id); known != byHandle_.end()) return known->second;

    // ONE ENTRY PER IDENTITY: the second window over a file joins the first (the header, guide A-66).
    EntryId id = 0;
    if (identity != nullptr && identity->isValid()) {
        for (const auto& [existing, entry] : entries_) {
            if (entry.identity.isValid() && entry.identity == *identity) {
                id = existing;
                break;
            }
        }
    }
    if (id == 0) {
        id = nextEntry_++;
        Entry entry;
        entry.id = id;
        if (identity != nullptr && identity->isValid()) entry.identity = *identity;
        entry.pageCount = std::max(0, pageCount);
        entry.pages.resize(static_cast<std::size_t>(entry.pageCount));
        entries_.emplace(id, std::move(entry));
    }
    entries_[id].handles.push_back(handle);
    byHandle_[handle.id] = id;
    return id;
}

void DocumentIndex::detach(DocumentHandle handle)
{
    const auto known = byHandle_.find(handle.id);
    if (known == byHandle_.end()) return;
    const EntryId id = known->second;
    byHandle_.erase(known);

    Entry* entry = find(id);
    if (entry == nullptr) return;
    entry->handles.erase(std::remove(entry->handles.begin(), entry->handles.end(), handle), entry->handles.end());
    if (entry->handles.empty()) {
        dropEntry(id);
        return;
    }

    // RE-SENT THROUGH THE HANDLE THAT STAYS, before the closing window cancels its handle's jobs -- or a
    // selection in the companion would wait for a page the scheduler has just thrown away.
    // A page's text and a unit part alike: `send` sends the kind of job its key names.
    for (auto& [key, pending] : entry->pending) {
        if (pending.sentThrough != handle) continue;
        if (pending.ticket.isValid()) cancelTicket(pending.ticket);
        pending.ticket = {};
        send(*entry, key, pending, pending.lane);
    }
}

DocumentIndex::EntryId DocumentIndex::entryOf(DocumentHandle handle) const
{
    const auto found = byHandle_.find(handle.id);
    return found == byHandle_.end() ? 0 : found->second;
}

bool DocumentIndex::isAttached(EntryId entry) const
{
    return find(entry) != nullptr;
}

int DocumentIndex::pageCount(EntryId entry) const
{
    const Entry* found = find(entry);
    return found == nullptr ? 0 : found->pageCount;
}

int DocumentIndex::handleCount(EntryId entry) const
{
    const Entry* found = find(entry);
    return found == nullptr ? 0 : static_cast<int>(found->handles.size());
}

std::shared_ptr<const IndexedPage> DocumentIndex::page(EntryId entry, int page) const
{
    const Entry* found = find(entry);
    if (found == nullptr || page < 0 || page >= found->pageCount) return nullptr;
    return found->pages[static_cast<std::size_t>(page)];
}

int DocumentIndex::pagesHeld(EntryId entry) const
{
    const Entry* found = find(entry);
    if (found == nullptr) return 0;
    return static_cast<int>(std::count_if(found->pages.begin(), found->pages.end(),
                                          [](const auto& page) { return page != nullptr; }));
}

void DocumentIndex::send(Entry& entry, int key, Pending& pending, render::RenderPriority lane)
{
    if (entry.handles.empty()) return;
    const DocumentHandle through = entry.handles.front();

    if (isGeometryKey(key)) {
        // A PAGE'S PATHS, as an engine task, exactly as a unit part is below (S6 T12).
        const int page = key - kGeometryBase;
        auto result = std::make_shared<PageGeometry>();
        auto failed = std::make_shared<bool>(false);
        const render::RenderTicket ticket = scheduler_.enqueueTask(
            through, lane, [reader = geometryReader_, through, page, result, failed] {
                if (!reader || !reader(through, page, *result)) *failed = true;
            });
        if (!ticket.isValid()) return;
        if (pending.ticket.isValid()) cancelTicket(pending.ticket);
        geometryReads_[ticket.id] = GeometryRead{entry.id, page, std::move(result), std::move(failed)};
        pending.ticket = ticket;
        pending.lane = lane;
        pending.sentThrough = through;
        ++stats_.geometryRequested;
        ++entry.geometryRequested;
        return;
    }

    if (isUnitKey(key)) {
        // A SEARCH-UNIT PART, as an engine task (the header). The task holds a COPY of the reader and the
        // result it fills, never `this`: it runs on the worker, and the index may be gone by then.
        const int part = partOfKey(key);
        auto result = std::make_shared<SearchUnitPart>();
        result->part = part;
        const render::RenderTicket ticket = scheduler_.enqueueTask(
            through, lane, [reader = unitReader_, through, part, result] {
                if (!reader) {
                    result->failed = true;
                    return;
                }
                std::vector<SearchUnit> units;
                if (reader(through, part, units)) {
                    result->units = std::move(units);
                } else {
                    result->failed = true;
                }
            });
        if (!ticket.isValid()) return;
        // Not coalesced by the scheduler, so a raise is a new task and the old one is cancelled -- after
        // the new one is queued, as for a page.
        if (pending.ticket.isValid()) cancelTicket(pending.ticket);
        unitReads_[ticket.id] = UnitRead{entry.id, part, std::move(result)};
        pending.ticket = ticket;
        pending.lane = lane;
        pending.sentThrough = through;
        ++stats_.unitsRequested;
        ++entry.unitsRequested;
        return;
    }

    const render::RenderTicket ticket = scheduler_.enqueueText({through, key, entry.epoch}, lane);
    if (!ticket.isValid()) return;
    // The new ticket FIRST, then the old one cancelled: the scheduler raised the queued job to the new
    // lane, and cancelling first could remove the job before the raise reached it.
    if (pending.ticket.isValid()) cancelTicket(pending.ticket);
    pending.ticket = ticket;
    pending.lane = lane;
    pending.sentThrough = through;
    ++stats_.requested;
    ++entry.requested;
}

void DocumentIndex::cancelTicket(render::RenderTicket ticket)
{
    if (!ticket.isValid()) return;
    scheduler_.cancel(ticket);
    // A task cancelled before it ran is never reported, so its record goes here or never.
    unitReads_.erase(ticket.id);
    geometryReads_.erase(ticket.id);
}

void DocumentIndex::setGeometryReader(GeometryReader reader)
{
    geometryReader_ = std::move(reader);
}

std::shared_ptr<const PageGeometry> DocumentIndex::geometry(EntryId entryId, int page) const
{
    const Entry* entry = find(entryId);
    if (entry == nullptr) return nullptr;
    const auto found = entry->geometry.find(page);
    return found == entry->geometry.end() ? nullptr : found->second;
}

bool DocumentIndex::requestGeometry(Owner owner, EntryId entryId, int page, render::RenderPriority lane)
{
    Entry* entry = find(entryId);
    if (entry == nullptr || owner == 0 || page < 0 || page >= entry->pageCount) return false;
    if (entry->geometry.count(page) != 0) return true;

    const int key = geometryKey(page);
    Pending& pending = entry->pending[key];
    pending.owners.insert(owner);
    owners_[owner].insert({entryId, key});
    if (!pending.ticket.isValid() || static_cast<int>(lane) < static_cast<int>(pending.lane))
        send(*entry, key, pending, lane);
    return false;
}

long long DocumentIndex::geometryRequestedFor(EntryId entryId) const
{
    const Entry* entry = find(entryId);
    return entry == nullptr ? 0 : entry->geometryRequested;
}

void DocumentIndex::onGeometry(GeometryRead read)
{
    Entry* entry = find(read.entry);
    if (entry == nullptr) return;
    settle(*entry, geometryKey(read.page));
    if (entry->geometry.count(read.page) != 0) return;   // a raise delivered it twice; the first stays

    std::shared_ptr<const PageGeometry> geometry = read.result;
    const bool failed = !read.result || (read.failed && *read.failed);
    if (failed) {
        // STORED EMPTY rather than retried: asked again it would fail again (a page's text has the same rule).
        geometry = std::make_shared<const PageGeometry>(read.page, std::vector<PathSegment>{});
        ++stats_.geometryFailed;
        RAWPAGE_LOG_WARNING(LogSubsystem::Render) << "index: page" << read.page << "paths could not be read";
    }
    entry->geometry[read.page] = std::move(geometry);
    ++stats_.geometryStored;

    Event arrived;
    arrived.kind = Event::Kind::GeometryArrived;
    arrived.entry = read.entry;
    arrived.page = read.page;
    notify(arrived);
    enforceBudget(read.entry);
}

void DocumentIndex::setUnitReader(UnitReader reader)
{
    unitReader_ = std::move(reader);
}

std::shared_ptr<const SearchUnitPart> DocumentIndex::units(EntryId entryId, int part) const
{
    const Entry* entry = find(entryId);
    if (entry == nullptr) return nullptr;
    const auto found = entry->units.find(part);
    return found == entry->units.end() ? nullptr : found->second;
}

bool DocumentIndex::requestUnits(Owner owner, EntryId entryId, int part, render::RenderPriority lane)
{
    Entry* entry = find(entryId);
    if (entry == nullptr || owner == 0) return false;
    if (part != kOutlinePart && part != kAttachmentsPart && (part < 0 || part >= entry->pageCount)) return false;
    if (entry->units.count(part) != 0) return true;

    const int key = unitKey(part);
    Pending& pending = entry->pending[key];
    pending.owners.insert(owner);
    owners_[owner].insert({entryId, key});
    if (!pending.ticket.isValid() || static_cast<int>(lane) < static_cast<int>(pending.lane))
        send(*entry, key, pending, lane);
    return false;
}

long long DocumentIndex::unitsRequestedFor(EntryId entryId) const
{
    const Entry* entry = find(entryId);
    return entry == nullptr ? 0 : entry->unitsRequested;
}

void DocumentIndex::settle(Entry& entry, int key)
{
    const auto pending = entry.pending.find(key);
    if (pending == entry.pending.end()) return;
    for (const Owner owner : pending->second.owners) {
        const auto asked = owners_.find(owner);
        if (asked == owners_.end()) continue;
        asked->second.erase({entry.id, key});
        if (asked->second.empty()) owners_.erase(asked);
    }
    if (pending->second.ticket.isValid()) cancelTicket(pending->second.ticket);
    entry.pending.erase(pending);
}

void DocumentIndex::onTask(render::RenderTicket ticket)
{
    if (const auto paths = geometryReads_.find(ticket.id); paths != geometryReads_.end()) {
        GeometryRead read = std::move(paths->second);
        geometryReads_.erase(paths);
        onGeometry(std::move(read));
        return;
    }
    const auto found = unitReads_.find(ticket.id);
    if (found == unitReads_.end()) return;   // not ours, or cancelled
    const UnitRead read = std::move(found->second);
    unitReads_.erase(found);

    Entry* entry = find(read.entry);
    if (entry == nullptr) return;
    settle(*entry, unitKey(read.part));
    if (entry->units.count(read.part) != 0) return;   // a raise delivered it twice; the first stays

    std::shared_ptr<const SearchUnitPart> part = read.result;
    if (!part) {
        auto failed = std::make_shared<SearchUnitPart>();
        failed->part = read.part;
        failed->failed = true;
        part = std::move(failed);
    }
    if (part->failed) {
        ++stats_.unitsFailed;
        RAWPAGE_LOG_WARNING(LogSubsystem::Render) << "index: search-unit part" << read.part << "could not be read";
    }
    entry->units[read.part] = part;
    ++stats_.unitsStored;

    Event arrived;
    arrived.kind = Event::Kind::UnitsArrived;
    arrived.entry = read.entry;
    arrived.page = read.part;
    notify(arrived);
    enforceBudget(read.entry);
}

bool DocumentIndex::request(Owner owner, EntryId entryId, int page, render::RenderPriority lane)
{
    Entry* entry = find(entryId);
    if (entry == nullptr || owner == 0 || page < 0 || page >= entry->pageCount) return false;
    if (entry->pages[static_cast<std::size_t>(page)]) return true;

    Pending& pending = entry->pending[page];
    pending.owners.insert(owner);
    owners_[owner].insert({entryId, page});
    // ONCE PER PAGE, RAISED BY A MORE URGENT ASK: the person who starts selecting on a page the index
    // build queued at `Background` must not wait behind the other 899 (D5-3).
    if (!pending.ticket.isValid() || static_cast<int>(lane) < static_cast<int>(pending.lane))
        send(*entry, page, pending, lane);
    return false;
}

void DocumentIndex::release(Owner owner)
{
    const auto found = owners_.find(owner);
    if (found == owners_.end()) return;
    const std::set<std::pair<EntryId, int>> asked = std::move(found->second);
    owners_.erase(found);
    for (const auto& [entryId, page] : asked) {
        Entry* entry = find(entryId);
        if (entry == nullptr) continue;
        const auto pending = entry->pending.find(page);
        if (pending == entry->pending.end()) continue;
        pending->second.owners.erase(owner);
        // A PAGE SOMEBODY ELSE STILL WANTS KEEPS ITS TICKET (the header): a cancelled search must not
        // take a selection's page with it.
        if (!pending->second.owners.empty()) continue;
        if (pending->second.ticket.isValid()) cancelTicket(pending->second.ticket);
        entry->pending.erase(pending);
    }
}

std::size_t DocumentIndex::outstanding(Owner owner) const
{
    const auto found = owners_.find(owner);
    return found == owners_.end() ? 0 : found->second.size();
}

void DocumentIndex::onText(const render::TextRequest& request, const render::TextResult& result)
{
    Entry* entry = entryForHandle(request.document);
    if (entry == nullptr) return;
    const int page = request.pageIndex;
    if (page < 0 || page >= entry->pageCount) return;
    // ASKED BEFORE A PAGE COMMAND (S7 T3): the number named another page then, and the read was asked again.
    if (request.epoch != entry->epoch) return;
    const EntryId id = entry->id;

    // WHOEVER WAS WAITING HAS IT NOW -- whichever handle it came through.
    settle(*entry, page);
    if (entry->pages[static_cast<std::size_t>(page)]) return;   // two handles delivered it; the first stays

    std::shared_ptr<const PageTextModel> model;
    bool failed = false;
    if (result && result.value() != nullptr && result.value()->pageIndex() == page) {
        model = result.value();
    } else {
        failed = true;
        model = std::make_shared<const PageTextModel>(page);
        RAWPAGE_LOG_WARNING(LogSubsystem::Render)
            << "index: page" << page << "has no text --"
            << (result ? QStringLiteral("a model for another page") : QString(result.error().messageKey));
    }
    entry->pages[static_cast<std::size_t>(page)] = std::make_shared<const IndexedPage>(std::move(model), failed);
    ++stats_.stored;
    if (failed) ++stats_.failed;

    Event arrived;
    arrived.kind = Event::Kind::PageArrived;
    arrived.entry = id;
    arrived.page = page;
    notify(arrived);
    enforceBudget(id);
}

void DocumentIndex::remap(Entry& entry, const PageMap& map)
{
    std::vector<std::shared_ptr<const void>> released;   // freed off this thread (I-13)

    // THE TEXT FOLLOWS ITS PAGE: kept, re-filed at its new index, and told where it is now -- the model's own
    // index is what a hit, a reading and a selection name (`PageTextModel::movedTo`). A page the command
    // removed goes; an inserted page has no text yet and is read when someone asks.
    std::vector<std::shared_ptr<const IndexedPage>> pages(static_cast<std::size_t>(map.newCount()));
    for (int to = 0; to < map.newCount(); ++to) {
        const int from = map.oldIndexOf(to);
        if (from < 0 || from >= entry.pageCount) continue;
        std::shared_ptr<const IndexedPage>& held = entry.pages[static_cast<std::size_t>(from)];
        if (!held) continue;
        held->model().movedTo(to);
        pages[static_cast<std::size_t>(to)] = std::move(held);
        ++stats_.pagesRemapped;
    }
    for (std::shared_ptr<const IndexedPage>& gone : entry.pages)
        if (gone) {
            released.push_back(std::move(gone));
            ++stats_.pagesDropped;
        }
    entry.pages = std::move(pages);

    // A PAGE'S UNITS AND PATHS name their page inside them (`SearchUnit::page`, `PageGeometry`'s page), so they
    // are kept only where the page did not move, and read again where it did -- one engine task a page, and
    // only for a search or a measurement that asks. The attachments' part names no page; the OUTLINE's does -- each
    // bookmark carries its destination, which a page command moves -- so it is read again too.
    for (auto it = entry.units.begin(); it != entry.units.end();) {
        const int part = it->first;
        if (part == kAttachmentsPart || (part >= 0 && map.newIndexOf(part) == part)) {
            ++it;
            continue;
        }
        if (it->second) released.push_back(std::move(it->second));
        it = entry.units.erase(it);
    }
    for (auto it = entry.geometry.begin(); it != entry.geometry.end();) {
        if (map.newIndexOf(it->first) == it->first) {
            ++it;
            continue;
        }
        if (it->second) released.push_back(std::move(it->second));
        it = entry.geometry.erase(it);
    }

    // WHAT IS STILL BEING READ is read again at the page's new index: a job already queued or running names the
    // OLD index, and the engine it would run on may be the reloaded one, where that index is another page. Its
    // ticket is cancelled, and a text job that ran anyway is refused when it arrives -- it carries the entry's
    // old EPOCH (`onText`); a task cancelled here is not reported. Whoever waited keeps waiting, for the same
    // page. A read of a page the command removed is let go -- its owners are told by `PagesRemapped`.
    // TWO PASSES over the owners' ledger -- every old key out, then every new one in -- because two pages may
    // trade places, and one pass would take out the key it had just put in.
    std::map<int, Pending> moved;
    std::vector<int> resend;
    for (auto& [key, pending] : entry.pending) {
        const bool geometry = isGeometryKey(key);
        const bool unit = !geometry && isUnitKey(key);
        const int page = geometry ? key - kGeometryBase : unit ? partOfKey(key) : key;
        if (unit && page == kAttachmentsPart) {   // the attachments' part: no page, nothing to move
            moved.emplace(key, std::move(pending));
            continue;
        }
        if (pending.ticket.isValid()) cancelTicket(pending.ticket);
        pending.ticket = render::RenderTicket{};
        for (const Owner owner : pending.owners) {
            const auto asked = owners_.find(owner);
            if (asked == owners_.end()) continue;
            asked->second.erase({entry.id, key});
            if (asked->second.empty()) owners_.erase(asked);
        }
        // The outline's part is read again under its own key: its bookmarks name pages, the part none.
        const int to = (unit && page == kOutlinePart) ? page : map.newIndexOf(page);
        if (to == PageMap::kNone) continue;
        const int newKey = geometry ? geometryKey(to) : unit ? unitKey(to) : to;
        moved.emplace(newKey, std::move(pending));
        resend.push_back(newKey);
    }
    entry.pending = std::move(moved);
    entry.pageCount = map.newCount();
    ++entry.epoch;   // what was asked before this command is refused when it arrives (`onText`)
    for (const int key : resend) {
        Pending& pending = entry.pending.at(key);
        for (const Owner owner : pending.owners) owners_[owner].insert({entry.id, key});
        send(entry, key, pending, pending.lane);
    }

    release(std::move(released));
}

void DocumentIndex::invalidate(EntryId entryId, const Scope& scope)
{
    Entry* entry = find(entryId);
    if (entry == nullptr || scope.isNothing()) return;

    // A PAGE COMMAND (S7 T3, D7-3): where every page went, rather than what changed on them.
    if (const PageMap* map = scope.pageMap(); map != nullptr && map->oldCount() == entry->pageCount) {
        remap(*entry, *map);
        Event remapped;
        remapped.kind = Event::Kind::PagesRemapped;
        remapped.entry = entryId;
        remapped.map = map;
        notify(remapped);
        return;
    }

    Event dropped;
    dropped.kind = Event::Kind::PagesInvalidated;
    dropped.entry = entryId;
    // Pages whose UNITS went and whose text stayed (D6-10) -- told as an eviction: their text is what it
    // was, so a selection or a reflow holding it keeps it, and a waiting search asks again for the units.
    Event unitsOnly;
    unitsOnly.kind = Event::Kind::PagesEvicted;
    unitsOnly.entry = entryId;
    std::vector<std::shared_ptr<const void>> released;   // freed off this thread (I-13)
    for (int page = 0; page < entry->pageCount; ++page) {
        if (!scope.touchesPage(page)) continue;
        auto& held = entry->pages[static_cast<std::size_t>(page)];
        // A PAGE'S UNITS GO WITH ITS TEXT (T8): a command that changes a page may change its annotations.
        bool hadUnits = false;
        if (const auto unit = entry->units.find(page); unit != entry->units.end()) {
            if (unit->second) released.push_back(std::move(unit->second));
            entry->units.erase(unit);
            hadUnits = true;
        }
        // THE TEXT STAYS when only the page's annotations changed (S6 T4, D6-10): no annotation can
        // change what the page's content says, and re-extracting it would cost a whole text job per edit.
        if (!scope.touchesText(page)) {
            if (hadUnits) unitsOnly.pages.push_back(page);
            continue;
        }
        // S6 T12: a page's PATHS are its content too, and go with its text.
        bool hadGeometry = false;
        if (const auto paths = entry->geometry.find(page); paths != entry->geometry.end()) {
            if (paths->second) released.push_back(std::move(paths->second));
            entry->geometry.erase(paths);
            hadGeometry = true;
        }
        if (!held && !hadUnits && !hadGeometry) continue;
        if (held) {
            released.push_back(std::move(held));
            ++stats_.pagesDropped;
        }
        dropped.pages.push_back(page);
    }
    release(std::move(released));
    if (!unitsOnly.pages.empty()) notify(unitsOnly);
    if (dropped.pages.empty()) return;
    notify(dropped);
}

void DocumentIndex::markSearched(EntryId entryId)
{
    if (Entry* entry = find(entryId)) entry->lastSearched = ++searchTick_;
}

void DocumentIndex::setBudgetBytes(std::size_t bytes)
{
    budgetBytes_ = bytes;
    enforceBudget();
}

std::size_t DocumentIndex::bytesHeld(EntryId entryId) const
{
    const Entry* entry = find(entryId);
    if (entry == nullptr) return 0;
    std::size_t bytes = 0;
    for (const auto& page : entry->pages)
        if (page) bytes += page->approxBytes();
    for (const auto& [part, units] : entry->units) {
        (void)part;
        if (units) bytes += unitPartBytes(*units);
    }
    for (const auto& [page, paths] : entry->geometry) {
        (void)page;
        if (paths) bytes += paths->approxBytes();
    }
    return bytes;
}

std::size_t DocumentIndex::bytesHeld() const
{
    std::size_t bytes = 0;
    for (const auto& [id, entry] : entries_) {
        (void)entry;
        bytes += bytesHeld(id);
    }
    return bytes;
}

void DocumentIndex::evict(Entry& entry)
{
    Event dropped;
    dropped.kind = Event::Kind::PagesEvicted;
    dropped.entry = entry.id;
    std::vector<std::shared_ptr<const void>> released;   // freed off this thread (I-13)
    for (int page = 0; page < entry.pageCount; ++page) {
        auto& held = entry.pages[static_cast<std::size_t>(page)];
        if (!held) continue;
        released.push_back(std::move(held));
        dropped.pages.push_back(page);
    }
    // The units go with the document: WHOLE documents (the header). A part is small, but an entry holding
    // only parts would otherwise count against the budget for ever and evict nothing.
    const bool hadUnits = !entry.units.empty() || !entry.geometry.empty();
    for (auto& [part, unit] : entry.units) {
        (void)part;
        if (unit) released.push_back(std::move(unit));
    }
    entry.units.clear();
    for (auto& [page, paths] : entry.geometry) {
        (void)page;
        if (paths) released.push_back(std::move(paths));
    }
    entry.geometry.clear();
    release(std::move(released));
    if (dropped.pages.empty() && !hadUnits) return;
    ++stats_.documentsEvicted;
    // Said even when only parts went: a search waiting on this document must learn that what it held is gone.
    notify(dropped);
}

void DocumentIndex::enforceBudget(EntryId keep)
{
    std::size_t held = bytesHeld();
    if (held <= budgetBytes_) {
        overBudget_ = false;
        return;
    }

    // LEAST RECENTLY SEARCHED FIRST; never searched before anything searched; the older entry first on a
    // tie. Whole documents: half an index is a first search with extra steps.
    std::vector<Entry*> order;
    for (auto& [id, entry] : entries_)
        if (id != keep) order.push_back(&entry);
    std::sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) {
        return a->lastSearched != b->lastSearched ? a->lastSearched < b->lastSearched : a->id < b->id;
    });
    for (Entry* candidate : order) {
        if (held <= budgetBytes_) break;
        const std::size_t bytes = bytesHeld(candidate->id);
        if (bytes == 0) continue;
        evict(*candidate);
        held -= std::min(held, bytes);
    }

    if (held > budgetBytes_) {
        // THE KEPT DOCUMENT ALONE IS OVER THE BUDGET, and it stays (the header). Said once per episode,
        // not once per page of a 900-page build.
        if (!overBudget_) {
            ++stats_.overBudgetKept;
            RAWPAGE_LOG_INFO(LogSubsystem::Render)
                << "index: the document being searched holds" << static_cast<qint64>(held / (1024 * 1024))
                << "MB, over the" << static_cast<qint64>(budgetBytes_ / (1024 * 1024))
                << "MB budget (search/indexCacheMB) -- kept, because evicting it would make every search of it a"
                   " first search";
        }
        overBudget_ = true;
    } else {
        overBudget_ = false;
    }
}

void DocumentIndex::dropEntry(EntryId entryId)
{
    Entry* entry = find(entryId);
    if (entry == nullptr) return;
    for (auto& [page, pending] : entry->pending) {
        if (pending.ticket.isValid()) cancelTicket(pending.ticket);
        for (const Owner owner : pending.owners) {
            const auto asked = owners_.find(owner);
            if (asked == owners_.end()) continue;
            asked->second.erase({entryId, page});
            if (asked->second.empty()) owners_.erase(asked);
        }
    }
    for (const DocumentHandle handle : entry->handles) byHandle_.erase(handle.id);
    // ITS PAGES AND PARTS ARE FREED OFF THIS THREAD (I-13): moved out, then the entry -- now light -- erased.
    std::vector<std::shared_ptr<const void>> released;
    released.reserve(entry->pages.size() + entry->units.size());
    for (auto& page : entry->pages)
        if (page) released.push_back(std::move(page));
    for (auto& [part, unit] : entry->units) {
        (void)part;
        if (unit) released.push_back(std::move(unit));
    }
    for (auto& [page, paths] : entry->geometry) {
        (void)page;
        if (paths) released.push_back(std::move(paths));
    }
    entries_.erase(entryId);
    release(std::move(released));

    Event dropped;
    dropped.kind = Event::Kind::EntryDropped;
    dropped.entry = entryId;
    notify(dropped);
}

long long DocumentIndex::requestedFor(EntryId entryId) const
{
    const Entry* entry = find(entryId);
    return entry == nullptr ? 0 : entry->requested;
}

DocumentIndex::SubscriptionToken DocumentIndex::subscribe(Listener listener)
{
    const SubscriptionToken token = nextToken_++;
    listeners_.emplace(token, std::move(listener));
    return token;
}

void DocumentIndex::unsubscribe(SubscriptionToken token)
{
    listeners_.erase(token);
}

void DocumentIndex::notify(const Event& event)
{
    // BY TOKEN, looked up at each call: a listener that unsubscribes another -- a window closing in
    // response to an event -- must not be called after it went.
    std::vector<SubscriptionToken> tokens;
    tokens.reserve(listeners_.size());
    for (const auto& [token, listener] : listeners_) {
        (void)listener;
        tokens.push_back(token);
    }
    for (const SubscriptionToken token : tokens) {
        const auto found = listeners_.find(token);
        if (found == listeners_.end() || !found->second) continue;
        const Listener listener = found->second;   // a copy: the listener may unsubscribe itself
        listener(event);
    }
}

} // namespace rawpage::core::text
