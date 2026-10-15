#include "core/text/SearchEngine.h"

#include "core/doc/AnnotationStore.h"

#include <QChar>

#include <algorithm>
#include <iterator>
#include <set>
#include <tuple>

namespace rawpage::core::text {
namespace {

// T9 (D6-11): what a comment is, for a search, is `core::isCommentSubtype` (`core/doc/Fdf.h`, since T13 -- the same
// set an exchange carries) -- the subtypes `pdf/render/PdfiumDocument`'s reader calls comments, so a document's
// comments are the same set whichever of the two readers gave them.

// A worker takes this many pages at a time, so the first search of a long document -- every page laid
// out and normalized -- spreads over every worker rather than landing on one.
constexpr std::size_t kPagesPerJob = 16;

// ---- Whole words (the header's rule) -------------------------------------------------------------

char32_t codePointBefore(const QString& text, qsizetype at)
{
    if (at <= 0 || at > text.size()) return 0;
    const QChar low = text.at(at - 1);
    if (low.isLowSurrogate() && at >= 2 && text.at(at - 2).isHighSurrogate())
        return QChar::surrogateToUcs4(text.at(at - 2), low);
    return low.unicode();
}

char32_t codePointAt(const QString& text, qsizetype at)
{
    if (at < 0 || at >= text.size()) return 0;
    const QChar high = text.at(at);
    if (high.isHighSurrogate() && at + 1 < text.size() && text.at(at + 1).isLowSurrogate())
        return QChar::surrogateToUcs4(high, text.at(at + 1));
    return high.unicode();
}

bool isWordCharacter(char32_t cp)
{
    switch (QChar::category(cp)) {
    case QChar::Letter_Uppercase:
    case QChar::Letter_Lowercase:
    case QChar::Letter_Titlecase:
    case QChar::Letter_Modifier:
    case QChar::Letter_Other:
    case QChar::Number_DecimalDigit:
    case QChar::Number_Letter:
    case QChar::Number_Other:
    case QChar::Mark_NonSpacing:
    case QChar::Mark_SpacingCombining:
    case QChar::Mark_Enclosing:
    case QChar::Punctuation_Connector:
        return true;
    default:
        return false;
    }
}

// A character that is a WORD OF ITS OWN with no dictionary: an ideograph, and hiragana -- UAX #29 gives
// both the break class that joins with nothing.
bool isStandaloneWord(char32_t cp)
{
    const QChar::Script script = QChar::script(cp);
    return script == QChar::Script_Han || script == QChar::Script_Hiragana;
}

bool isKatakana(char32_t cp)
{
    return QChar::script(cp) == QChar::Script_Katakana;
}

bool isWordBoundary(const QString& text, qsizetype at)
{
    if (at <= 0 || at >= text.size()) return true;
    const char32_t before = codePointBefore(text, at);
    const char32_t after = codePointAt(text, at);
    if (!isWordCharacter(before) || !isWordCharacter(after)) return true;
    if (isStandaloneWord(before) || isStandaloneWord(after)) return true;
    // Katakana joins katakana and nothing else (UAX #29 WB13).
    return isKatakana(before) != isKatakana(after);
}

QString stripMarks(const QString& text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString stripped;
    stripped.reserve(decomposed.size());
    for (const QChar c : decomposed)
        if (c.category() != QChar::Mark_NonSpacing) stripped.append(c);
    return stripped.normalized(QString::NormalizationForm_C);
}

// What a candidate match came to.
enum class Verdict { Refused, Kept, Enough };

// EVERY CANDIDATE MATCH IN ONE NORMALIZED TEXT, in order -- the loop a page's block and a search unit share,
// so the two cannot come to differ. `judge(begin, end)` decides each; `Enough` stops the loop.
template <typename Judge>
void forEachMatch(const CompiledQuery& query, const QString& text, Judge&& judge)
{
    if (query.isRegex()) {
        QRegularExpressionMatchIterator it = query.expression().globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const qsizetype begin = match.capturedStart();
            const qsizetype end = match.capturedEnd();
            if (end <= begin) continue;   // a match of nothing is never a hit
            if (judge(begin, end) == Verdict::Enough) return;
        }
        return;
    }
    const QString& needle = query.literal();
    qsizetype at = 0;
    while ((at = text.indexOf(needle, at, Qt::CaseSensitive)) >= 0) {
        const qsizetype end = at + needle.size();
        const Verdict verdict = judge(at, end);
        if (verdict == Verdict::Enough) return;
        // NON-OVERLAPPING when kept; one unit on when refused, so a whole-word refusal of `lorem` at
        // `xlorem` still finds a `lorem` that starts inside it.
        at = verdict == Verdict::Kept ? end : at + 1;
    }
}

} // namespace

// =====================================================================================================
// The match
// =====================================================================================================

Result<CompiledQuery> SearchEngine::compile(const SearchQuery& query)
{
    CompiledQuery compiled;
    compiled.query_ = query;
    const NormalizeOptions options = query.flags.normalizeOptions();

    if (!query.flags.useRegex) {
        compiled.literal_ = TextNormalizer(options).normalizeQuery(query.pattern);
        if (compiled.literal_.isEmpty())
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.search.emptyPattern"), query.pattern};
        return compiled;
    }

    if (query.pattern.isEmpty())
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.search.emptyPattern"), query.pattern};

    // THE PAGE'S STEPS THAT ARE SAFE ON A PATTERN (the header): NFKC and mark stripping touch nothing in
    // ASCII, which is where every metacharacter is; case folding would turn `\S` into `\s`, so case is an
    // option instead.
    QString pattern = query.pattern.normalized(QString::NormalizationForm_KC);
    if (options.stripDiacritics) pattern = stripMarks(pattern);
    QRegularExpression::PatternOptions patternOptions = QRegularExpression::UseUnicodePropertiesOption;
    if (!query.flags.caseSensitive) patternOptions |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression expression(pattern, patternOptions);
    if (!expression.isValid())
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.search.invalidPattern"), query.pattern};
    expression.optimize();
    compiled.expression_ = std::move(expression);
    return compiled;
}

std::vector<SearchHit> SearchEngine::findOnPage(const CompiledQuery& query, const SearchablePage& page,
                                                const PageReading& reading, int limit)
{
    std::vector<SearchHit> hits;
    const SearchQuery& q = query.query();

    std::vector<Span> inside;
    const bool bySelection = q.scope.kind == SearchScope::Kind::Selection;
    if (bySelection) {
        if (!q.scope.selection.covers(page.pageIndex())) return hits;
        inside = q.scope.selection.spansOn(reading);
        if (inside.empty()) return hits;
    }

    bool enough = false;
    const auto judge = [&](const SearchableBlock& block, qsizetype begin, qsizetype end) -> Verdict {
        if (q.flags.wholeWords && !(isWordBoundary(block.text, begin) && isWordBoundary(block.text, end)))
            return Verdict::Refused;
        int from = -1;
        for (qsizetype u = begin; u < end && from < 0; ++u) from = block.firstPos[static_cast<std::size_t>(u)];
        int to = -1;
        for (qsizetype u = end - 1; u >= begin && to < 0; --u) to = block.lastPos[static_cast<std::size_t>(u)];
        if (from < 0 || to < from) return Verdict::Refused;
        const Span span{from, to + 1};
        if (bySelection
            && std::none_of(inside.begin(), inside.end(),
                            [&](const Span& s) { return s.begin <= span.begin && span.end <= s.end; }))
            return Verdict::Refused;
        hits.push_back(SearchHit{page.pageIndex(), span, nullptr, Span{}});
        enough = limit > 0 && static_cast<int>(hits.size()) >= limit;
        return enough ? Verdict::Enough : Verdict::Kept;
    };

    for (const SearchableBlock& block : page.blocks()) {
        forEachMatch(query, block.text, [&](qsizetype begin, qsizetype end) { return judge(block, begin, end); });
        if (enough) break;
    }
    return hits;
}

std::vector<SearchHit> SearchEngine::findInUnit(const CompiledQuery& query, std::shared_ptr<const SearchUnit> unit,
                                                int limit)
{
    std::vector<SearchHit> hits;
    if (!unit || unit->text.isEmpty()) return hits;

    // THE PAGE'S PIPELINE, over the unit's string (`TextNormalizer::normalizeString`): a comment that says
    // `café` is found by `cafe` with diacritics ignored exactly as a page that says it is (D5-5).
    const NormalizedText normalized = TextNormalizer(query.options()).normalizeString(unit->text);
    const bool wholeWords = query.query().flags.wholeWords;
    forEachMatch(query, normalized.text, [&](qsizetype begin, qsizetype end) -> Verdict {
        if (wholeWords && !(isWordBoundary(normalized.text, begin) && isWordBoundary(normalized.text, end)))
            return Verdict::Refused;
        const auto [from, to] = normalized.sourceRange(begin, end);
        if (from < 0 || to < from) return Verdict::Refused;
        hits.push_back(SearchHit{unit->page, Span{}, unit, Span{from, to + 1}});
        return limit > 0 && static_cast<int>(hits.size()) >= limit ? Verdict::Enough : Verdict::Kept;
    });
    return hits;
}

// =====================================================================================================
// The run
// =====================================================================================================

SearchEngine::SearchEngine(DocumentIndex& index, Config config) : index_(index)
{
    int workers = config.workerCount;
    if (workers <= 0) {
        const unsigned hardware = std::thread::hardware_concurrency();
        workers = std::clamp(static_cast<int>(hardware / 2), 1, 4);
    }
    workers_.reserve(static_cast<std::size_t>(workers));
    for (int i = 0; i < workers; ++i) workers_.emplace_back([this] { workerLoop(); });

    indexToken_ = index_.subscribe([this](const DocumentIndex::Event& event) { onIndexEvent(event); });
}

SearchEngine::~SearchEngine()
{
    // Convention 10's threaded form, as the scheduler does it: the subscription goes by token, then
    // every run lets go of its pages, then the workers stop and are JOINED before anything they read is
    // destroyed.
    index_.unsubscribe(indexToken_);
    for (auto& [id, run] : runs_) {
        (void)id;
        run.cancelled->store(true);
        index_.release(run.owner);
    }
    runs_.clear();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) worker.join();
}

std::shared_ptr<const SearchEngine::CommentUnits> SearchEngine::commentUnits(const AnnotationStore& store)
{
    auto out = std::make_shared<CommentUnits>();
    if (!store.status()) return out;
    for (int page = 0; page < store.pageCount(); ++page) {
        auto part = std::make_shared<SearchUnitPart>();
        part->part = kStoreCommentsPart;
        for (const std::size_t index : store.onPage(page)) {
            const Annotation& a = store.at(index);
            if (!isCommentSubtype(a.subtype) || a.isStateReply()) continue;
            SearchUnit base;
            base.contentClass = ContentClass::Comments;
            base.page = page;
            base.rect = a.rect;
            base.hasRect = a.rectFinite;
            base.locator = a.id.toString();
            for (const auto& [field, text] : {std::pair{SearchUnit::Field::CommentContents, &a.contents},
                                              std::pair{SearchUnit::Field::CommentSubject, &a.subject}}) {
                if (text->isEmpty()) continue;
                SearchUnit unit = base;
                unit.field = field;
                unit.text = *text;
                part->units.push_back(std::move(unit));
            }
        }
        if (!part->units.empty()) out->emplace(page, std::move(part));
    }
    return out;
}

Result<SearchEngine::SearchId> SearchEngine::start(DocumentIndex::EntryId entry, const SearchQuery& query,
                                                   Handlers handlers, std::shared_ptr<const CommentUnits> comments)
{
    Result<CompiledQuery> compiled = compile(query);
    if (!compiled) {
        ++stats_.refused;
        return compiled.error();
    }
    if (!index_.isAttached(entry)) {
        ++stats_.refused;
        return Error{ErrorCode::Unavailable, QLatin1String("error.search.noDocument")};
    }

    const int pageCount = index_.pageCount(entry);
    int first = 0;
    int last = pageCount - 1;
    switch (query.scope.kind) {
    case SearchScope::Kind::WholeDocument:
        break;
    case SearchScope::Kind::PageRange:
        first = std::max(0, query.scope.firstPage);
        last = std::min(pageCount - 1, query.scope.lastPage);
        break;
    case SearchScope::Kind::Selection:
        if (query.scope.selection.isEmpty()) {
            last = -1;
        } else {
            first = std::max(0, query.scope.selection.firstPage());
            last = std::min(pageCount - 1, query.scope.selection.lastPage());
        }
        break;
    }

    Run run;
    run.id = nextRun_++;
    run.entry = entry;
    run.owner = index_.newOwner();
    run.query = std::make_shared<const CompiledQuery>(std::move(compiled.value()));
    run.cancelled = std::make_shared<std::atomic<bool>>(false);
    run.handlers = std::move(handlers);
    run.firstPage = first;
    run.pagesInScope = std::max(0, last - first + 1);

    // WHICH UNIT PARTS THIS QUERY NEEDS (SearchQuery's rule): a page's comments and fields for a page range
    // or the whole document; the outline's and the attachments' for the whole document only; none for a
    // selection. A class switched off is never read.
    const SearchClasses& classes = query.classes;
    const bool selection = query.scope.kind == SearchScope::Kind::Selection;
    const bool wholeDocument = query.scope.kind == SearchScope::Kind::WholeDocument;
    // T9 (D6-11): comments from the store, when given, and the engine's page parts then only for fields.
    if (!selection && classes.comments && comments) run.comments = std::move(comments);
    run.pageUnits = !selection && (classes.fields || (classes.comments && !run.comments));
    if (wholeDocument && classes.bookmarks) run.docParts.push_back(kOutlinePart);
    if (wholeDocument && classes.attachments) run.docParts.push_back(kAttachmentsPart);
    run.docSlots = run.docParts.empty() ? 0 : 1;
    run.state.assign(static_cast<std::size_t>(run.slotCount()), Run::Untouched);
    const SearchId id = run.id;

    index_.markSearched(entry);
    ++stats_.started;
    Run& stored = runs_.emplace(id, std::move(run)).first->second;
    advance(stored);

    if (stored.slotCount() == 0) {
        // NOTHING TO SEARCH STILL FINISHES, and from the pump like everything else: a caller must not
        // have two paths to handle depending on whether its range was empty.
        WakeHandler wake;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            completions_.push_back(Completion{id, {}});
            wake = wakeHandler_;
        }
        if (wake) wake();
    }
    return id;
}

std::optional<SearchEngine::Piece> SearchEngine::collect(Run& run, int slot, bool ask)
{
    Piece piece;
    piece.slot = slot;
    bool complete = true;
    const auto needPart = [&](int part) {
        if (std::shared_ptr<const SearchUnitPart> held = index_.units(run.entry, part)) {
            piece.units.push_back(std::move(held));
            return;
        }
        complete = false;
        if (ask) (void)index_.requestUnits(run.owner, run.entry, part, render::RenderPriority::Background);
    };

    const int page = run.pageOfSlot(slot);
    if (page >= 0) {
        piece.page = index_.page(run.entry, page);
        if (!piece.page) {
            complete = false;
            // NOT HELD: asked at `Background`, for this run (D5-3).
            if (ask) (void)index_.request(run.owner, run.entry, page, render::RenderPriority::Background);
        }
        if (run.pageUnits) needPart(page);
        if (run.comments) {
            if (const auto held = run.comments->find(page); held != run.comments->end()) piece.units.push_back(held->second);
        }
    } else {
        for (const int part : run.docParts) needPart(part);
    }
    if (!complete) return std::nullopt;
    return piece;
}

void SearchEngine::advance(Run& run)
{
    std::vector<Piece> ready;
    while (run.nextToAsk < run.slotCount() && run.waiting < kExtractionWindow) {
        const int slot = run.nextToAsk++;
        if (std::optional<Piece> piece = collect(run, slot, true)) {
            run.state[static_cast<std::size_t>(slot)] = Run::Submitted;
            ready.push_back(std::move(*piece));
            continue;
        }
        run.state[static_cast<std::size_t>(slot)] = Run::Waiting;
        ++run.waiting;
    }
    if (!ready.empty()) submit(run, std::move(ready));
}

void SearchEngine::submit(Run& run, std::vector<Piece> pieces)
{
    const int maxResults = run.query->query().maxResults;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t at = 0; at < pieces.size(); at += kPagesPerJob) {
            Job job;
            job.run = run.id;
            job.cancelled = run.cancelled;
            job.query = run.query;
            // One more than the bound, per page: a page cannot put more than that into the first N+1 hits,
            // and N+1 is what says there were more than N.
            job.limitPerPage = maxResults > 0 ? maxResults + 1 : 0;
            job.storeComments = static_cast<bool>(run.comments);
            const std::size_t end = std::min(pieces.size(), at + kPagesPerJob);
            job.pieces.assign(std::make_move_iterator(pieces.begin() + static_cast<std::ptrdiff_t>(at)),
                              std::make_move_iterator(pieces.begin() + static_cast<std::ptrdiff_t>(end)));
            queue_.push_back(std::move(job));
        }
    }
    wake_.notify_all();
}

void SearchEngine::onIndexEvent(const DocumentIndex::Event& event)
{
    std::vector<SearchId> ids;
    for (const auto& [id, run] : runs_)
        if (run.entry == event.entry) ids.push_back(id);

    for (const SearchId id : ids) {
        const auto found = runs_.find(id);
        if (found == runs_.end()) continue;
        Run& run = found->second;

        // The slot an arrival completes, if it is one of this run's and is waiting.
        int slot = -1;
        switch (event.kind) {
        case DocumentIndex::Event::Kind::PageArrived:
            if (event.page >= run.firstPage && event.page < run.firstPage + run.pagesInScope)
                slot = run.slotOfPage(event.page);
            break;
        case DocumentIndex::Event::Kind::UnitsArrived:
            if (event.page >= 0) {
                if (run.pageUnits && event.page >= run.firstPage && event.page < run.firstPage + run.pagesInScope)
                    slot = run.slotOfPage(event.page);
            } else if (run.docSlots > 0
                       && std::find(run.docParts.begin(), run.docParts.end(), event.page) != run.docParts.end()) {
                slot = 0;
            }
            break;
        case DocumentIndex::Event::Kind::GeometryArrived:
            break;   // S6 T12: a page's paths -- nothing a search waits for
        case DocumentIndex::Event::Kind::EntryDropped:
            // THE DOCUMENT CLOSED UNDER THE SEARCH: nobody is there to be answered.
            ++stats_.cancelled;
            retire(id);
            continue;
        case DocumentIndex::Event::Kind::PagesInvalidated:
        case DocumentIndex::Event::Kind::PagesEvicted:
            // A SUBMITTED slot is held by its job. A WAITING one may have had one of its pieces taken while
            // it waited for another -- a page's text evicted before its unit part arrived -- and would then
            // wait for ever: each is asked again for whatever it now lacks (asking for a held piece is a
            // no-op, so the slots that lost nothing cost a lookup). One found complete is submitted here
            // rather than left for an arrival that has already happened.
            {
                std::vector<Piece> ready;
                for (int s = 0; s < run.nextToAsk; ++s) {
                    if (run.state[static_cast<std::size_t>(s)] != Run::Waiting) continue;
                    if (std::optional<Piece> piece = collect(run, s, true)) {
                        run.state[static_cast<std::size_t>(s)] = Run::Submitted;
                        --run.waiting;
                        ready.push_back(std::move(*piece));
                    }
                }
                if (!ready.empty()) {
                    submit(run, std::move(ready));
                    advance(run);
                }
            }
            continue;
        }
        if (slot < 0 || run.state[static_cast<std::size_t>(slot)] != Run::Waiting) continue;
        std::optional<Piece> piece = collect(run, slot, false);
        if (!piece) continue;   // its other piece is still coming
        run.state[static_cast<std::size_t>(slot)] = Run::Submitted;
        --run.waiting;
        std::vector<Piece> one;
        one.push_back(std::move(*piece));
        submit(run, std::move(one));
        advance(run);
    }
}

void SearchEngine::workerLoop()
{
    for (;;) {
        Job job;
        MatchObserver observer;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            ++running_;
            observer = matchObserver_;
        }

        // A REGULAR EXPRESSION OF THIS JOB'S OWN: `QRegularExpression` is documented reentrant, not
        // thread-safe, and a job-local copy made from the pattern shares nothing with another worker's.
        CompiledQuery local = *job.query;
        if (local.isRegex())
            local.expression_ = QRegularExpression(local.expression_.pattern(), local.expression_.patternOptions());

        Completion done;
        done.run = job.run;
        const SearchClasses& classes = local.query().classes;
        for (const Piece& piece : job.pieces) {
            if (job.cancelled->load()) break;
            if (observer) observer(piece.page ? piece.page->pageIndex() : -1);
            PageResult result;
            result.slot = piece.slot;
            if (const std::shared_ptr<const IndexedPage>& page = piece.page) {
                result.page = page->pageIndex();
                result.failed = page->failed();
                result.unmapped = static_cast<int>(page->model().unmappedCount());
                if (!result.failed) {
                    const std::shared_ptr<const PageReading> reading = page->reading();
                    const std::shared_ptr<const SearchablePage> searchable = page->searchable(local.options());
                    result.hits = findOnPage(local, *searchable, *reading, job.limitPerPage);
                }
            }
            // THE SLOT'S UNITS (T8), after its text, in the order the reader gave them -- only the classes
            // the query includes: a page's part holds its comments AND its fields, and either may be off.
            for (const std::shared_ptr<const SearchUnitPart>& part : piece.units) {
                if (part->failed) {
                    ++result.failedUnitParts;
                    continue;
                }
                // T9: with the store's comments in the job, an engine part's are the file's -- not what is open.
                const bool engineComments = job.storeComments && part->part != kStoreCommentsPart;
                for (std::size_t i = 0; i < part->units.size(); ++i) {
                    if (!classes.includes(part->units[i].contentClass)) continue;
                    if (engineComments && part->units[i].contentClass == ContentClass::Comments) continue;
                    const int room = job.limitPerPage > 0
                                         ? job.limitPerPage - static_cast<int>(result.hits.size())
                                         : 0;
                    if (job.limitPerPage > 0 && room <= 0) break;
                    // An ALIAS of the part: the hit keeps the part alive, and the part is never copied.
                    std::shared_ptr<const SearchUnit> unit(part, &part->units[i]);
                    std::vector<SearchHit> found = findInUnit(local, std::move(unit), room);
                    result.hits.insert(result.hits.end(), std::make_move_iterator(found.begin()),
                                       std::make_move_iterator(found.end()));
                }
            }
            done.pages.push_back(std::move(result));
        }

        WakeHandler wake;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            completions_.push_back(std::move(done));
            wake = wakeHandler_;
        }
        if (wake) wake();
        {
            // "Running" lasts until the wake has been made, as the scheduler's does: a caller that returns
            // from `waitForIdle` has seen every wake its work produced.
            const std::lock_guard<std::mutex> lock(mutex_);
            --running_;
            if (queue_.empty() && running_ == 0) idle_.notify_all();
        }
    }
}

int SearchEngine::deliverReady()
{
    std::deque<Completion> ready;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(completions_);
    }

    std::vector<SearchId> touched;
    for (Completion& completion : ready) {
        const auto found = runs_.find(completion.run);
        if (found == runs_.end()) continue;   // cancelled: it delivers nothing
        Run& run = found->second;
        for (PageResult& result : completion.pages) {
            const int slot = result.slot;
            if (slot < 0 || slot >= run.slotCount()) continue;
            char& state = run.state[static_cast<std::size_t>(slot)];
            if (state == Run::Done) continue;
            state = Run::Done;
            ++run.doneCount;
            if (slot >= run.docSlots) {
                ++run.pagesDone;
                ++stats_.pagesMatched;
            }
            run.results[slot] = std::move(result);
        }
        if (std::find(touched.begin(), touched.end(), completion.run) == touched.end())
            touched.push_back(completion.run);
    }

    // Looked up again for each: a handler may cancel or start runs.
    for (const SearchId id : touched) {
        if (maybeFinish(id)) continue;
        const auto found = runs_.find(id);
        if (found == runs_.end() || !found->second.handlers.progress) continue;
        const Run& run = found->second;
        Progress progress;
        progress.pagesSearched = run.pagesDone;
        progress.pagesInScope = run.pagesInScope;
        for (const auto& [page, result] : run.results) {
            (void)page;
            progress.hitsSoFar += static_cast<int>(result.hits.size());
        }
        const auto handler = run.handlers.progress;
        handler(progress);
    }
    return static_cast<int>(ready.size());
}

bool SearchEngine::maybeFinish(SearchId id)
{
    const auto found = runs_.find(id);
    if (found == runs_.end()) return false;
    Run& run = found->second;

    if (run.doneCount == run.slotCount()) {
        std::size_t total = 0;
        for (const auto& [slot, result] : run.results) {
            (void)slot;
            total += result.hits.size();
        }
        const int maxResults = run.query->query().maxResults;
        finish(run, maxResults > 0 && total > static_cast<std::size_t>(maxResults));
        return true;
    }

    // THE BOUND SETTLED EARLY: once the slots done from the start of the scope hold more than N hits, no
    // later slot can enter the first N, so the rest is neither extracted nor matched.
    const int maxResults = run.query->query().maxResults;
    if (maxResults <= 0) return false;
    std::size_t prefix = 0;
    for (int slot = 0; slot < run.slotCount() && run.state[static_cast<std::size_t>(slot)] == Run::Done; ++slot) {
        const auto result = run.results.find(slot);
        if (result != run.results.end()) prefix += result->second.hits.size();
        if (prefix > static_cast<std::size_t>(maxResults)) {
            finish(run, true);
            return true;
        }
    }
    return false;
}

void SearchEngine::finish(Run& run, bool limitReached)
{
    Outcome outcome;
    outcome.limitReached = limitReached;
    outcome.pagesSearched = run.pagesDone;
    outcome.pagesInScope = run.pagesInScope;
    const int maxResults = run.query->query().maxResults;
    // A FIELD IS ONE FIELD HOWEVER MANY WIDGETS SHOW IT (the header): its name and value, at one place in
    // the text, are kept at the first widget in document order.
    std::set<std::tuple<QString, int, int>> fieldsSeen;
    for (int slot = 0; slot < run.slotCount(); ++slot) {
        const auto result = run.results.find(slot);
        if (result == run.results.end()) {
            // Not done: only an early finish leaves one, and nothing after it may enter the list.
            if (limitReached) break;
            continue;
        }
        const PageResult& page = result->second;
        if (page.page >= 0) {
            if (page.failed) outcome.failedPages.push_back(page.page);
            if (page.unmapped > 0) {
                outcome.unmappedPages.push_back(page.page);
                outcome.unmappedGlyphs += page.unmapped;
            }
        }
        outcome.failedUnitParts += page.failedUnitParts;
        for (const SearchHit& hit : page.hits) {
            if (maxResults > 0 && static_cast<int>(outcome.hits.size()) >= maxResults) break;
            if (hit.unit && hit.unit->contentClass == ContentClass::Fields
                && !fieldsSeen.emplace(hit.unit->locator, static_cast<int>(hit.unit->field), hit.within.begin).second)
                continue;
            outcome.hits.push_back(hit);
        }
    }

    const DocumentIndex::EntryId entry = run.entry;
    const auto finished = std::move(run.handlers.finished);
    ++stats_.finished;
    retire(run.id);   // `run` is gone after this line
    index_.enforceBudget(entry);
    if (finished) finished(outcome);
}

void SearchEngine::retire(SearchId id)
{
    const auto found = runs_.find(id);
    if (found == runs_.end()) return;
    found->second.cancelled->store(true);
    const DocumentIndex::Owner owner = found->second.owner;
    runs_.erase(found);
    index_.release(owner);
}

void SearchEngine::cancel(SearchId id)
{
    if (runs_.find(id) == runs_.end()) return;
    ++stats_.cancelled;
    retire(id);
}

bool SearchEngine::isRunning(SearchId id) const
{
    return runs_.find(id) != runs_.end();
}

void SearchEngine::setWakeHandler(WakeHandler handler)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    wakeHandler_ = std::move(handler);
}

void SearchEngine::setMatchObserver(MatchObserver observer)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    matchObserver_ = std::move(observer);
}

bool SearchEngine::waitForIdle(int milliseconds)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return idle_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                          [this] { return queue_.empty() && running_ == 0; });
}

// =====================================================================================================
// SearchSession
// =====================================================================================================

Status SearchSession::start(DocumentIndex::EntryId entry, const SearchQuery& query, SearchEngine::Handlers handlers,
                            std::shared_ptr<const SearchEngine::CommentUnits> comments)
{
    // THE NEW RUN ASKS FIRST, THEN THE OLD ONE LETS GO (the class comment): the pages both want keep their
    // tickets instead of being cancelled and asked again.
    const SearchEngine::SearchId previous = current_;
    const Result<SearchEngine::SearchId> started = engine_.start(entry, query, std::move(handlers), std::move(comments));
    current_ = started ? started.value() : 0;
    if (previous != 0) engine_.cancel(previous);
    if (!started) return started.error();
    return {};
}

void SearchSession::cancel()
{
    if (current_ != 0) engine_.cancel(current_);
    current_ = 0;
}

} // namespace rawpage::core::text
