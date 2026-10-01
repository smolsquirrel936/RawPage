#include "core/text/DocumentIndex.h"

#include "core/Log.h"
#include "core/text/LayoutAnalyzer.h"

#include <algorithm>

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
    page->pageIndex_ = reading.pageIndex();
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

    const render::RenderTicket ticket = scheduler_.enqueueText({through, key}, lane);
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

void DocumentIndex::invalidate(EntryId entryId, const Scope& scope)
{
    Entry* entry = find(entryId);
    if (entry == nullptr || scope.isNothing()) return;
    Event dropped;
    dropped.kind = Event::Kind::PagesInvalidated;
    dropped.entry = entryId;
    for (int page = 0; page < entry->pageCount; ++page) {
        if (!scope.touchesPage(page)) continue;
        auto& held = entry->pages[static_cast<std::size_t>(page)];
        // A PAGE'S UNITS GO WITH ITS TEXT (T8): a command that changes a page may change its annotations.
        const bool hadUnits = entry->units.erase(page) != 0;
        if (!held && !hadUnits) continue;
        if (held) {
            held.reset();
            ++stats_.pagesDropped;
        }
        dropped.pages.push_back(page);
    }
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
    for (int page = 0; page < entry.pageCount; ++page) {
        auto& held = entry.pages[static_cast<std::size_t>(page)];
        if (!held) continue;
        held.reset();
        dropped.pages.push_back(page);
    }
    // The units go with the document: WHOLE documents (the header). A part is small, but an entry holding
    // only parts would otherwise count against the budget for ever and evict nothing.
    const bool hadUnits = !entry.units.empty();
    entry.units.clear();
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
    entries_.erase(entryId);

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
