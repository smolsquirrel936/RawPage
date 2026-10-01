#include "core/text/SearchEngine.h"

#include <QChar>

#include <algorithm>
#include <set>

namespace rawpage::core::text {
namespace {

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

    // True when the hit was kept and the page's bound is reached.
    const auto accept = [&](const SearchableBlock& block, qsizetype begin, qsizetype end) -> bool {
        if (q.flags.wholeWords && !(isWordBoundary(block.text, begin) && isWordBoundary(block.text, end)))
            return false;
        int from = -1;
        for (qsizetype u = begin; u < end && from < 0; ++u) from = block.firstPos[static_cast<std::size_t>(u)];
        int to = -1;
        for (qsizetype u = end - 1; u >= begin && to < 0; --u) to = block.lastPos[static_cast<std::size_t>(u)];
        if (from < 0 || to < from) return false;
        const Span span{from, to + 1};
        if (bySelection
            && std::none_of(inside.begin(), inside.end(),
                            [&](const Span& s) { return s.begin <= span.begin && span.end <= s.end; }))
            return false;
        hits.push_back(SearchHit{page.pageIndex(), span});
        return limit > 0 && static_cast<int>(hits.size()) >= limit;
    };

    for (const SearchableBlock& block : page.blocks()) {
        if (query.isRegex()) {
            QRegularExpressionMatchIterator it = query.expression().globalMatch(block.text);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                const qsizetype begin = match.capturedStart();
                const qsizetype end = match.capturedEnd();
                if (end <= begin) continue;   // a match of nothing is never a hit
                if (accept(block, begin, end)) return hits;
            }
            continue;
        }
        const QString& needle = query.literal();
        qsizetype at = 0;
        while ((at = block.text.indexOf(needle, at, Qt::CaseSensitive)) >= 0) {
            const qsizetype end = at + needle.size();
            const std::size_t before = hits.size();
            if (accept(block, at, end)) return hits;
            // NON-OVERLAPPING when kept; one unit on when refused, so a whole-word refusal of `lorem` at
            // `xlorem` still finds a `lorem` that starts inside it.
            at = hits.size() > before ? end : at + 1;
        }
    }
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

Result<SearchEngine::SearchId> SearchEngine::start(DocumentIndex::EntryId entry, const SearchQuery& query,
                                                   Handlers handlers)
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
    run.state.assign(static_cast<std::size_t>(run.pagesInScope), Run::Untouched);
    const SearchId id = run.id;

    index_.markSearched(entry);
    ++stats_.started;
    Run& stored = runs_.emplace(id, std::move(run)).first->second;
    advance(stored);

    if (stored.pagesInScope == 0) {
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

void SearchEngine::advance(Run& run)
{
    std::vector<std::shared_ptr<const IndexedPage>> ready;
    while (run.nextToAsk < run.pagesInScope && run.waiting < kExtractionWindow) {
        const int offset = run.nextToAsk++;
        const int page = run.firstPage + offset;
        if (std::shared_ptr<const IndexedPage> held = index_.page(run.entry, page)) {
            run.state[static_cast<std::size_t>(offset)] = Run::Submitted;
            ready.push_back(std::move(held));
            continue;
        }
        // NOT HELD: asked at `Background`, for this run (D5-3). The index answers `true` only for a page it
        // holds, which the line above has just ruled out.
        run.state[static_cast<std::size_t>(offset)] = Run::Waiting;
        ++run.waiting;
        (void)index_.request(run.owner, run.entry, page, render::RenderPriority::Background);
    }
    if (!ready.empty()) submit(run, std::move(ready));
}

void SearchEngine::submit(Run& run, std::vector<std::shared_ptr<const IndexedPage>> pages)
{
    const int maxResults = run.query->query().maxResults;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t at = 0; at < pages.size(); at += kPagesPerJob) {
            Job job;
            job.run = run.id;
            job.cancelled = run.cancelled;
            job.query = run.query;
            // One more than the bound, per page: a page cannot put more than that into the first N+1 hits,
            // and N+1 is what says there were more than N.
            job.limitPerPage = maxResults > 0 ? maxResults + 1 : 0;
            const std::size_t end = std::min(pages.size(), at + kPagesPerJob);
            job.pages.assign(pages.begin() + static_cast<std::ptrdiff_t>(at),
                             pages.begin() + static_cast<std::ptrdiff_t>(end));
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
        switch (event.kind) {
        case DocumentIndex::Event::Kind::PageArrived: {
            const int offset = event.page - run.firstPage;
            if (offset < 0 || offset >= run.pagesInScope) break;
            if (run.state[static_cast<std::size_t>(offset)] != Run::Waiting) break;
            std::shared_ptr<const IndexedPage> held = index_.page(run.entry, event.page);
            if (!held) break;
            run.state[static_cast<std::size_t>(offset)] = Run::Submitted;
            --run.waiting;
            submit(run, {std::move(held)});
            advance(run);
            break;
        }
        case DocumentIndex::Event::Kind::EntryDropped:
            // THE DOCUMENT CLOSED UNDER THE SEARCH: nobody is there to be answered.
            ++stats_.cancelled;
            retire(id);
            break;
        case DocumentIndex::Event::Kind::PagesInvalidated:
        case DocumentIndex::Event::Kind::PagesEvicted:
            break;   // a submitted page is held by its job; a waited-for page is still asked for
        }
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
        for (const std::shared_ptr<const IndexedPage>& page : job.pages) {
            if (job.cancelled->load()) break;
            if (observer) observer(page->pageIndex());
            PageResult result;
            result.page = page->pageIndex();
            result.failed = page->failed();
            result.unmapped = static_cast<int>(page->model().unmappedCount());
            if (!result.failed) {
                const std::shared_ptr<const PageReading> reading = page->reading();
                const std::shared_ptr<const SearchablePage> searchable = page->searchable(local.options());
                result.hits = findOnPage(local, *searchable, *reading, job.limitPerPage);
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
            const int offset = result.page - run.firstPage;
            if (offset < 0 || offset >= run.pagesInScope) continue;
            char& state = run.state[static_cast<std::size_t>(offset)];
            if (state == Run::Done) continue;
            state = Run::Done;
            ++run.doneCount;
            ++stats_.pagesMatched;
            run.results[result.page] = std::move(result);
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
        progress.pagesSearched = run.doneCount;
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

    if (run.doneCount == run.pagesInScope) {
        std::size_t total = 0;
        for (const auto& [page, result] : run.results) {
            (void)page;
            total += result.hits.size();
        }
        const int maxResults = run.query->query().maxResults;
        finish(run, maxResults > 0 && total > static_cast<std::size_t>(maxResults));
        return true;
    }

    // THE BOUND SETTLED EARLY: once the pages done from the start of the scope hold more than N hits, no
    // later page can enter the first N, so the rest is neither extracted nor matched.
    const int maxResults = run.query->query().maxResults;
    if (maxResults <= 0) return false;
    std::size_t prefix = 0;
    for (int offset = 0; offset < run.pagesInScope && run.state[static_cast<std::size_t>(offset)] == Run::Done; ++offset) {
        const auto result = run.results.find(run.firstPage + offset);
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
    outcome.pagesSearched = run.doneCount;
    outcome.pagesInScope = run.pagesInScope;
    const int maxResults = run.query->query().maxResults;
    for (int offset = 0; offset < run.pagesInScope; ++offset) {
        const auto result = run.results.find(run.firstPage + offset);
        if (result == run.results.end()) {
            // Not done: only an early finish leaves one, and nothing after it may enter the list.
            if (limitReached) break;
            continue;
        }
        const PageResult& page = result->second;
        if (page.failed) outcome.failedPages.push_back(page.page);
        if (page.unmapped > 0) {
            outcome.unmappedPages.push_back(page.page);
            outcome.unmappedGlyphs += page.unmapped;
        }
        for (const SearchHit& hit : page.hits) {
            if (maxResults > 0 && static_cast<int>(outcome.hits.size()) >= maxResults) break;
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

Status SearchSession::start(DocumentIndex::EntryId entry, const SearchQuery& query, SearchEngine::Handlers handlers)
{
    // THE NEW RUN ASKS FIRST, THEN THE OLD ONE LETS GO (the class comment): the pages both want keep their
    // tickets instead of being cancelled and asked again.
    const SearchEngine::SearchId previous = current_;
    const Result<SearchEngine::SearchId> started = engine_.start(entry, query, std::move(handlers));
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
