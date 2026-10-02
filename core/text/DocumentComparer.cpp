#include "core/text/DocumentComparer.h"

#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"
#include "core/text/LayoutAnalyzer.h"
#include "core/text/TextNormalizer.h"

#include <QHash>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace rawpage::core::text {
namespace {

using Edit = DocumentComparer::Edit;

// =========================================================================================================
// Myers' diff, linear space -- the middle snake as diff-match-patch's `diff_bisect` writes it
// =========================================================================================================

class Differ {
public:
    Differ(const std::vector<int>& a, const std::vector<int>& b, long long* explored)
        : a_(a), b_(b), explored_(explored)
    {
    }

    // False when the TOP-LEVEL forward search passes `dLimit` (-1: no limit). Sub-problems are smaller than
    // the distance already found and are never limited.
    bool run(int a0, int a1, int b0, int b1, int dLimit)
    {
        int prefix = 0;
        while (a0 + prefix < a1 && b0 + prefix < b1 && a_[a0 + prefix] == b_[b0 + prefix]) ++prefix;
        int suffix = 0;
        while (a1 - suffix > a0 + prefix && b1 - suffix > b0 + prefix && a_[a1 - 1 - suffix] == b_[b1 - 1 - suffix])
            ++suffix;
        push(Edit::Op::Equal, prefix);
        const int x0 = a0 + prefix, x1 = a1 - suffix, y0 = b0 + prefix, y1 = b1 - suffix;
        if (!middle(x0, x1, y0, y1, dLimit)) return false;
        push(Edit::Op::Equal, suffix);
        return true;
    }

    std::vector<Edit> take() { return std::move(out_); }

private:
    bool middle(int a0, int a1, int b0, int b1, int dLimit)
    {
        const int n = a1 - a0;
        const int m = b1 - b0;
        if (n == 0) {
            push(Edit::Op::Insert, m);
            return true;
        }
        if (m == 0) {
            push(Edit::Op::Delete, n);
            return true;
        }
        bool capped = false;
        const std::optional<std::pair<int, int>> split = bisect(a0, n, b0, m, dLimit, capped);
        if (capped) return false;
        if (!split) {
            // NOTHING IN COMMON: the distance is n + m, which a limited search has to check itself.
            if (dLimit >= 0 && (n + m + 1) / 2 > dLimit) return false;
            push(Edit::Op::Delete, n);
            push(Edit::Op::Insert, m);
            return true;
        }
        const int x = split->first;
        const int y = split->second;
        run(a0, a0 + x, b0, b0 + y, -1);
        run(a0 + x, a1, b0 + y, b1, -1);
        return true;
    }

    std::optional<std::pair<int, int>> bisect(int a0, int n, int b0, int m, int dLimit, bool& capped)
    {
        const int maxD = (n + m + 1) / 2;
        const int vOffset = maxD;
        const int vLength = 2 * maxD + 2;   // diff-match-patch's 2 * maxD, padded: maxD 1 reaches index 2
        std::vector<int> v1(static_cast<std::size_t>(vLength), -1);
        std::vector<int> v2(static_cast<std::size_t>(vLength), -1);
        v1[static_cast<std::size_t>(vOffset + 1)] = 0;
        v2[static_cast<std::size_t>(vOffset + 1)] = 0;
        const int delta = n - m;
        const bool front = (delta % 2 != 0);
        int k1start = 0, k1end = 0, k2start = 0, k2end = 0;
        const auto A = [&](int i) { return a_[static_cast<std::size_t>(a0 + i)]; };
        const auto B = [&](int j) { return b_[static_cast<std::size_t>(b0 + j)]; };
        const auto at = [](std::vector<int>& v, int i) -> int& { return v[static_cast<std::size_t>(i)]; };

        for (int d = 0; d < maxD; ++d) {
            if (dLimit >= 0 && d > dLimit) {
                capped = true;
                return std::nullopt;
            }
            for (int k1 = -d + k1start; k1 <= d - k1end; k1 += 2) {
                if (explored_ != nullptr) ++*explored_;
                const int k1o = vOffset + k1;
                int x1 = (k1 == -d || (k1 != d && at(v1, k1o - 1) < at(v1, k1o + 1))) ? at(v1, k1o + 1)
                                                                                     : at(v1, k1o - 1) + 1;
                int y1 = x1 - k1;
                while (x1 < n && y1 < m && A(x1) == B(y1)) {
                    ++x1;
                    ++y1;
                }
                at(v1, k1o) = x1;
                if (x1 > n) {
                    k1end += 2;
                } else if (y1 > m) {
                    k1start += 2;
                } else if (front) {
                    const int k2o = vOffset + delta - k1;
                    if (k2o >= 0 && k2o < vLength && at(v2, k2o) != -1) {
                        const int x2 = n - at(v2, k2o);
                        if (x1 >= x2) return std::make_pair(x1, y1);
                    }
                }
            }
            for (int k2 = -d + k2start; k2 <= d - k2end; k2 += 2) {
                if (explored_ != nullptr) ++*explored_;
                const int k2o = vOffset + k2;
                int x2 = (k2 == -d || (k2 != d && at(v2, k2o - 1) < at(v2, k2o + 1))) ? at(v2, k2o + 1)
                                                                                     : at(v2, k2o - 1) + 1;
                int y2 = x2 - k2;
                while (x2 < n && y2 < m && A(n - x2 - 1) == B(m - y2 - 1)) {
                    ++x2;
                    ++y2;
                }
                at(v2, k2o) = x2;
                if (x2 > n) {
                    k2end += 2;
                } else if (y2 > m) {
                    k2start += 2;
                } else if (!front) {
                    const int k1o = vOffset + delta - k2;
                    if (k1o >= 0 && k1o < vLength && at(v1, k1o) != -1) {
                        const int x1 = at(v1, k1o);
                        const int y1 = vOffset + x1 - k1o;
                        if (x1 >= n - x2) return std::make_pair(x1, y1);
                    }
                }
            }
        }
        return std::nullopt;
    }

    void push(Edit::Op op, int count)
    {
        if (count <= 0) return;
        if (!out_.empty() && out_.back().op == op) {
            out_.back().count += count;
            return;
        }
        out_.push_back(Edit{op, count});
    }

    const std::vector<int>& a_;
    const std::vector<int>& b_;
    long long* explored_;
    std::vector<Edit> out_;
};

int distanceOf(const std::vector<Edit>& edits)
{
    int d = 0;
    for (const Edit& e : edits)
        if (e.op != Edit::Op::Equal) d += e.count;
    return d;
}

// =========================================================================================================
// Words
// =========================================================================================================

struct Token {
    int id = -1;               // interned text
    QString text;
    PdfRect box;
    int line = -1;             // into the page's layout lines
    int spacesBefore = -1;     // spaces drawn before it on its own line; -1 after a line break or first
    Region region = Region::Body;
    bool number = false;       // digits only
};

struct PageWords {
    std::vector<Token> tokens;     // every word, band included, in reading order
    std::vector<int> kept;         // indices into `tokens` the options keep, in order
    std::vector<bool> ignored;     // per token: an ignore-list dropped it
    std::vector<int> keptIds;      // ... their ids: the sequence layer 2 diffs
    std::vector<int> bodyIds;      // the body's kept ids: what layer 1 aligns by
    std::vector<int> sortedBody;   // ... sorted, for the similarity
    std::uint64_t bodyHash = 0;
};

class Interner {
public:
    int id(const QString& text)
    {
        const auto it = ids_.constFind(text);
        if (it != ids_.constEnd()) return it.value();
        const int next = static_cast<int>(ids_.size());
        ids_.insert(text, next);
        return next;
    }

private:
    QHash<QString, int> ids_;
};

bool isDigits(const QString& text)
{
    if (text.isEmpty()) return false;
    for (const QChar c : text)
        if (!c.isDigit()) return false;
    return true;
}

PdfRect unite(const PdfRect& a, const PdfRect& b)
{
    if (a.size.width <= 0 && a.size.height <= 0 && a.origin.x == 0 && a.origin.y == 0) return b;
    const double left = std::min(a.left(), b.left());
    const double bottom = std::min(a.bottom(), b.bottom());
    const double right = std::max(a.right(), b.right());
    const double top = std::max(a.top(), b.top());
    return PdfRect{PdfPoint{left, bottom}, PdfSize{right - left, top - bottom}};
}

bool isDrawnSpace(char32_t cp)
{
    return cp == U' ' || cp == 0x00A0 || cp == U'\t' || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x3000;
}

// The page's words through D5-5's one normalizer, case kept -- a change of case is a change.
std::vector<Token> wordsOf(const PageReading& reading, Interner& interner)
{
    std::vector<Token> out;
    if (reading.isEmpty()) return out;
    const PageTextModel& model = reading.model();
    const TextNormalizer normalizer(NormalizeOptions{false, false});
    const NormalizedText normalized = normalizer.normalize(model, LayoutAnalyzer::readingStream(reading.layout()));

    std::vector<int> positionOf(model.size(), -1);
    for (int p = 0; p < reading.size(); ++p) positionOf[static_cast<std::size_t>(reading.modelIndex(p))] = p;

    int previousLast = -1;
    const qsizetype length = normalized.text.size();
    for (qsizetype i = 0; i < length;) {
        if (normalized.text.at(i) == QLatin1Char(' ')) {
            ++i;
            continue;
        }
        qsizetype end = i;
        while (end < length && normalized.text.at(end) != QLatin1Char(' ')) ++end;
        int first = -1, last = -1;
        for (qsizetype u = i; u < end; ++u) {
            const int f = normalized.first[static_cast<std::size_t>(u)];
            const int l = normalized.last[static_cast<std::size_t>(u)];
            if (f < 0) continue;
            const int pf = positionOf[static_cast<std::size_t>(f)];
            const int pl = positionOf[static_cast<std::size_t>(l)];
            if (pf < 0 || pl < 0) continue;
            first = first < 0 ? std::min(pf, pl) : std::min({first, pf, pl});
            last = std::max({last, pf, pl});
        }
        Token token;
        token.text = normalized.text.mid(i, end - i);
        token.id = interner.id(token.text);
        token.number = isDigits(token.text);
        if (first >= 0) {
            for (int p = first; p <= last; ++p) token.box = unite(token.box, reading.charAt(p).box);
            token.line = reading.lineOf(first);
            if (previousLast >= 0 && reading.lineOf(previousLast) == token.line && !out.empty()) {
                // THE SPACES BETWEEN TWO WORDS ARE MEASURED, NOT COUNTED: PDFium reports a run of drawn
                // spaces as ONE space character (measured at T12 on `whitespace-only.pdf`, where
                // `boundary  page` extracts as one space 2.78 pt wide before a gap of 5.56). So the gap
                // between the two words is divided by the width of the space glyph between them -- or a
                // quarter of the font size where none was drawn -- and rounded.
                double spaceWidth = 0.0;
                for (int p = previousLast + 1; p < first; ++p) {
                    const TextChar& c = reading.charAt(p);
                    if (isDrawnSpace(c.codepoint) && c.box.size.width > 0.0) spaceWidth = std::max(spaceWidth, c.box.size.width);
                }
                if (spaceWidth <= 0.0) spaceWidth = 0.25 * std::max(1.0, reading.charAt(first).fontSize);
                const PdfRect& before = out.back().box;
                const double gap = std::max(token.box.left() - before.right(), before.left() - token.box.right());
                token.spacesBefore = std::max(1, static_cast<int>(std::lround(gap / spaceWidth)));
            }
            previousLast = last;
        }
        out.push_back(std::move(token));
        i = end;
    }
    return out;
}

// A line's text with whitespace collapsed and every digit made `#`: the key a running header or footer
// keeps from page to page while its page number and date change.
QString bandKey(const PageReading& reading, int line)
{
    const Span span = reading.lineSpan(line);
    QString out;
    bool space = false;
    for (int p = span.begin; p < span.end; ++p) {
        const char32_t cp = reading.charAt(p).codepoint;
        if (isDrawnSpace(cp) || reading.breakBefore(p) == ReadingItem::Kind::WordGap) space = !out.isEmpty();
        if (isDrawnSpace(cp)) continue;
        if (space) out += QLatin1Char(' ');
        space = false;
        const QString s = QString::fromUcs4(&cp, 1);
        out += (s.size() == 1 && s.at(0).isDigit()) ? QStringLiteral("#") : s;
    }
    return out;
}

// Where on its page, as DRAWN, a page-space point is: 0 at the top edge, 1 at the bottom.
double drawnDepth(const ComparePage& page, PdfPoint point)
{
    const PageTransform transform(page.cropBox.size, 1.0, 72.0, page.rotation);
    const DevicePoint device = transform.toDevice(PdfPoint{point.x - page.cropBox.left(), point.y - page.cropBox.bottom()});
    const double height = transform.deviceSize().height;
    return height > 0.0 ? device.y / height : 0.5;
}

QString labelOf(const ComparePage& page, int index)
{
    return page.label.isEmpty() ? QString::number(index + 1) : page.label;
}

std::uint64_t hashOf(const std::vector<int>& ids)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (const int id : ids) {
        h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(id)) + 0x9E3779B97F4A7C15ULL;
        h *= 1099511628211ULL;
    }
    return h ^ static_cast<std::uint64_t>(ids.size());
}

// Dice of two sorted multisets: 2 |A ∩ B| / (|A| + |B|). Two empty pages are the same page.
double similarity(const std::vector<int>& a, const std::vector<int>& b)
{
    if (a.empty() && b.empty()) return 1.0;
    std::size_t i = 0, j = 0, common = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) {
            ++common;
            ++i;
            ++j;
        } else if (a[i] < b[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    return 2.0 * static_cast<double>(common) / static_cast<double>(a.size() + b.size());
}

// =========================================================================================================
// The comparison
// =========================================================================================================

struct Pair {
    int base = -1;
    int revised = -1;
    PageMatch::Kind kind = PageMatch::Kind::Modified;   // Identical, Modified or Moved until diffed
    double similarity = 0.0;
    std::vector<Edit> script;   // the words' edit script; a move's slide is applied to it (`findMoves`)
};

struct Hunk {
    int pair = -1;                 // into the pairs
    std::vector<int> deleted;      // token indices into the base page's `tokens`
    std::vector<int> inserted;     // ... into the revised page's
    // Where it sits in the two KEPT sequences, and how many equal words stand either side of it -- what
    // bounds the positions an insertion or a deletion could equally have been put at (its slides).
    int aBegin = 0, aEnd = 0, bBegin = 0, bEnd = 0;
    int equalBefore = 0, equalAfter = 0;
    // The script edit a PURE hunk is -- one insertion or one deletion between two equal runs, the only
    // kind that can slide; -1 for any other.
    int edit = -1;
    bool moved = false;
};

class Comparison {
public:
    Comparison(const CompareDocument& base, const CompareDocument& revised, const CompareOptions& options,
               const RasterProvider& rasters, const std::atomic<bool>* cancel, const DocumentComparer::Progress& progress)
        : base_(base), revised_(revised), options_(options), rasters_(rasters), cancel_(cancel), progress_(progress)
    {
        const int b = static_cast<int>(base.size());
        const int r = static_cast<int>(revised.size());
        total_ = b + r + 1 + std::max(b, r);
    }

    ComparisonResult run()
    {
        report();
        if (!readWords()) return finish();
        findBands();
        applyOptions();
        align();
        step();
        if (cancelled()) return finish();
        for (std::size_t p = 0; p < pairs_.size(); ++p) {
            if (cancelled()) return finish();
            diffPair(static_cast<int>(p));
            ++result_.pairsCompared;
            step();
        }
        findMoves();
        // WHITESPACE AND LAYOUT over the words each pair matched -- read from the scripts AFTER the moves'
        // slides, so a word is paired with the word it actually faces.
        for (const Pair& pair : pairs_)
            if (pair.kind != PageMatch::Kind::Replaced) matchedDifferences(pair, matchedOf(pair));
        emitHunks();
        if (options_.sensitivity == CompareSensitivity::Appearance) {
            for (std::size_t p = 0; p < pairs_.size(); ++p) {
                if (cancelled()) return finish();
                appearance(static_cast<int>(p));
            }
        }
        settleKinds();
        done_ = total_;
        report();
        return finish();
    }

private:
    bool cancelled() const { return cancel_ != nullptr && cancel_->load(); }

    void step()
    {
        ++done_;
        report();
    }

    void report()
    {
        const int done = std::min(done_, total_);
        if (progress_ && done != reported_) {
            reported_ = done;
            progress_(done, total_);
        }
    }

    ComparisonResult finish()
    {
        result_.cancelled = cancelled();
        orderPages();
        return std::move(result_);
    }

    // ---- Layer 0: each page's words ----------------------------------------------------------------------

    bool readWords()
    {
        const auto read = [&](const CompareDocument& document, std::vector<PageWords>& into) {
            into.resize(document.size());
            for (std::size_t i = 0; i < document.size(); ++i) {
                if (cancelled()) return false;
                if (document[i].reading) into[i].tokens = wordsOf(*document[i].reading, interner_);
                step();
            }
            return true;
        };
        return read(base_, baseWords_) && read(revised_, revisedWords_);
    }

    // THE HEADER/FOOTER BAND, over both documents' pages together (see the header).
    void findBands()
    {
        struct Seen {
            std::set<std::pair<int, int>> pages;   // (side, page)
        };
        std::map<std::pair<int, QString>, Seen> seen;   // (0 header / 1 footer, key)
        const int totalPages = static_cast<int>(base_.size() + revised_.size());
        const auto zoneOf = [&](const ComparePage& page, int line) -> int {
            const TextLine& l = page.reading->layout().lines[static_cast<std::size_t>(line)];
            const double depth = drawnDepth(page, PdfPoint{l.box.left() + l.box.size.width / 2,
                                                            l.box.bottom() + l.box.size.height / 2});
            if (depth < 0.1) return 0;
            if (depth > 0.9) return 1;
            return -1;
        };
        // A BAND LINE STANDS APART: its whole BLOCK is in the top or the bottom tenth. A paragraph's first
        // lines can reach into the top tenth and, digits normalized, look alike from page to page -- and a
        // band that took them would take them on one page and not on the next the moment one word changed,
        // putting the same words in two places (found at T12 by `tst_compare`'s synthetic pages).
        const auto lineBand = [&](const ComparePage& page, int line) -> int {
            const int zone = zoneOf(page, line);
            if (zone < 0) return -1;
            const PageReading& reading = *page.reading;
            const Span span = reading.lineSpan(line);
            if (span.isEmpty()) return -1;
            const TextBlock& block = reading.layout().blocks[static_cast<std::size_t>(reading.blockOf(span.begin))];
            for (const int other : block.lines)
                if (zoneOf(page, other) != zone) return -1;
            return zone;
        };
        const auto survey = [&](const CompareDocument& document, int side) {
            for (std::size_t i = 0; i < document.size(); ++i) {
                if (!document[i].reading) continue;
                const int lines = static_cast<int>(document[i].reading->layout().lines.size());
                for (int line = 0; line < lines; ++line) {
                    const int band = lineBand(document[i], line);
                    if (band < 0) continue;
                    seen[{band, bandKey(*document[i].reading, line)}].pages.insert({side, static_cast<int>(i)});
                }
            }
        };
        survey(base_, 0);
        survey(revised_, 1);
        const int needed = std::max(3, (totalPages + 1) / 2);
        const auto mark = [&](const CompareDocument& document, std::vector<PageWords>& words) {
            for (std::size_t i = 0; i < document.size(); ++i) {
                if (!document[i].reading) continue;
                for (Token& token : words[i].tokens) {
                    if (token.line < 0) continue;
                    const int band = lineBand(document[i], token.line);
                    if (band < 0) continue;
                    const auto it = seen.find({band, bandKey(*document[i].reading, token.line)});
                    if (it != seen.end() && static_cast<int>(it->second.pages.size()) >= needed)
                        token.region = band == 0 ? Region::Header : Region::Footer;
                }
            }
        };
        mark(base_, baseWords_);
        mark(revised_, revisedWords_);
    }

    void applyOptions()
    {
        const auto apply = [&](const CompareDocument& document, std::vector<PageWords>& words) {
            for (std::size_t i = 0; i < words.size(); ++i) {
                PageWords& page = words[i];
                const QString label = labelOf(document[i], static_cast<int>(i));
                page.ignored.assign(page.tokens.size(), true);
                for (std::size_t t = 0; t < page.tokens.size(); ++t) {
                    const Token& token = page.tokens[t];
                    if (options_.ignoreHeaderFooter && token.region != Region::Body) continue;
                    if (options_.ignorePageNumbers && token.number && (token.region != Region::Body || token.text == label))
                        continue;
                    page.kept.push_back(static_cast<int>(t));
                    page.ignored[t] = false;
                    page.keptIds.push_back(token.id);
                    if (token.region == Region::Body) page.bodyIds.push_back(token.id);
                }
                page.sortedBody = page.bodyIds;
                std::sort(page.sortedBody.begin(), page.sortedBody.end());
                page.bodyHash = hashOf(page.bodyIds);
            }
        };
        apply(base_, baseWords_);
        apply(revised_, revisedWords_);
    }

    // ---- Layer 1: the pages ------------------------------------------------------------------------------

    void align()
    {
        const int nb = static_cast<int>(baseWords_.size());
        const int nr = static_cast<int>(revisedWords_.size());
        // ANCHORS: the page sequences diffed by their body's hash.
        std::unordered_map<std::uint64_t, int> hashIds;
        const auto hashId = [&](std::uint64_t h) { return hashIds.emplace(h, static_cast<int>(hashIds.size())).first->second; };
        std::vector<int> a, b;
        for (const PageWords& p : baseWords_) a.push_back(hashId(p.bodyHash));
        for (const PageWords& p : revisedWords_) b.push_back(hashId(p.bodyHash));
        const std::vector<Edit> pages = *DocumentComparer::diff(a, b, -1);

        std::vector<int> baseOf(static_cast<std::size_t>(nr), -1);
        std::vector<int> revisedOf(static_cast<std::size_t>(nb), -1);
        struct Gap {
            int b0, b1, r0, r1;
        };
        std::vector<Gap> gaps;
        int ia = 0, ib = 0;
        for (std::size_t e = 0; e < pages.size();) {
            if (pages[e].op == Edit::Op::Equal) {
                for (int k = 0; k < pages[e].count; ++k) {
                    pairs_.push_back(Pair{ia + k, ib + k, PageMatch::Kind::Identical, 1.0, {}});
                    revisedOf[static_cast<std::size_t>(ia + k)] = ib + k;
                    baseOf[static_cast<std::size_t>(ib + k)] = ia + k;
                }
                ia += pages[e].count;
                ib += pages[e].count;
                ++e;
                continue;
            }
            Gap gap{ia, ia, ib, ib};
            for (; e < pages.size() && pages[e].op != Edit::Op::Equal; ++e) {
                if (pages[e].op == Edit::Op::Delete) ia += pages[e].count;
                else ib += pages[e].count;
            }
            gap.b1 = ia;
            gap.r1 = ib;
            gaps.push_back(gap);
        }

        // BETWEEN ANCHORS: the leftovers paired in order by similarity, a weighted longest common subsequence.
        for (const Gap& gap : gaps) {
            const int n = gap.b1 - gap.b0;
            const int m = gap.r1 - gap.r0;
            if (n == 0 || m == 0 || static_cast<long long>(n) * m > 4'000'000) continue;
            std::vector<double> sim(static_cast<std::size_t>(n) * m);
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < m; ++j)
                    sim[static_cast<std::size_t>(i) * m + j] = similarity(baseWords_[gap.b0 + i].sortedBody,
                                                                          revisedWords_[gap.r0 + j].sortedBody);
            std::vector<double> score(static_cast<std::size_t>(n + 1) * (m + 1), 0.0);
            const auto S = [&](int i, int j) -> double& { return score[static_cast<std::size_t>(i) * (m + 1) + j]; };
            for (int i = 1; i <= n; ++i)
                for (int j = 1; j <= m; ++j) {
                    const double s = sim[static_cast<std::size_t>(i - 1) * m + (j - 1)];
                    S(i, j) = std::max({S(i - 1, j), S(i, j - 1),
                                        s >= options_.pairSimilarity ? S(i - 1, j - 1) + s : -1.0});
                }
            for (int i = n, j = m; i > 0 && j > 0;) {
                const double s = sim[static_cast<std::size_t>(i - 1) * m + (j - 1)];
                if (s >= options_.pairSimilarity && S(i, j) == S(i - 1, j - 1) + s) {
                    pairs_.push_back(Pair{gap.b0 + i - 1, gap.r0 + j - 1, PageMatch::Kind::Modified, s, {}});
                    revisedOf[static_cast<std::size_t>(gap.b0 + i - 1)] = gap.r0 + j - 1;
                    baseOf[static_cast<std::size_t>(gap.r0 + j - 1)] = gap.b0 + i - 1;
                    --i;
                    --j;
                } else if (S(i, j) == S(i - 1, j)) {
                    --i;
                } else {
                    --j;
                }
            }
        }

        // MOVED: a page left over anywhere and one left over anywhere else, best first.
        std::vector<int> leftBase, leftRevised;
        for (int i = 0; i < nb; ++i)
            if (revisedOf[static_cast<std::size_t>(i)] < 0) leftBase.push_back(i);
        for (int j = 0; j < nr; ++j)
            if (baseOf[static_cast<std::size_t>(j)] < 0) leftRevised.push_back(j);
        if (!leftBase.empty() && !leftRevised.empty()
            && static_cast<long long>(leftBase.size()) * static_cast<long long>(leftRevised.size()) <= 1'000'000) {
            struct Candidate {
                double s;
                int i, j;
            };
            std::vector<Candidate> candidates;
            for (const int i : leftBase)
                for (const int j : leftRevised) {
                    const double s = similarity(baseWords_[static_cast<std::size_t>(i)].sortedBody,
                                                revisedWords_[static_cast<std::size_t>(j)].sortedBody);
                    if (s >= options_.moveSimilarity) candidates.push_back(Candidate{s, i, j});
                }
            std::stable_sort(candidates.begin(), candidates.end(),
                             [](const Candidate& x, const Candidate& y) { return x.s > y.s; });
            for (const Candidate& c : candidates) {
                if (revisedOf[static_cast<std::size_t>(c.i)] >= 0 || baseOf[static_cast<std::size_t>(c.j)] >= 0) continue;
                pairs_.push_back(Pair{c.i, c.j, PageMatch::Kind::Moved, c.s, {}});
                revisedOf[static_cast<std::size_t>(c.i)] = c.j;
                baseOf[static_cast<std::size_t>(c.j)] = c.i;
            }
        }

        // STILL LEFT IN ONE GAP: paired by position, and the cap decides whether that is a modification or a
        // replacement. Whatever has no partner was inserted or deleted.
        for (const Gap& gap : gaps) {
            std::vector<int> lb, lr;
            for (int i = gap.b0; i < gap.b1; ++i)
                if (revisedOf[static_cast<std::size_t>(i)] < 0) lb.push_back(i);
            for (int j = gap.r0; j < gap.r1; ++j)
                if (baseOf[static_cast<std::size_t>(j)] < 0) lr.push_back(j);
            for (std::size_t k = 0; k < std::min(lb.size(), lr.size()); ++k) {
                pairs_.push_back(Pair{lb[k], lr[k], PageMatch::Kind::Modified,
                                      similarity(baseWords_[static_cast<std::size_t>(lb[k])].sortedBody,
                                                 revisedWords_[static_cast<std::size_t>(lr[k])].sortedBody), {}});
                revisedOf[static_cast<std::size_t>(lb[k])] = lr[k];
                baseOf[static_cast<std::size_t>(lr[k])] = lb[k];
            }
        }
        for (int i = 0; i < nb; ++i)
            if (revisedOf[static_cast<std::size_t>(i)] < 0) {
                result_.pages.push_back(PageMatch{PageMatch::Kind::Deleted, i, -1, 0.0});
                Difference d;
                d.kind = DifferenceKind::PageDeleted;
                d.basePage = i;
                d.tokensBefore = static_cast<int>(baseWords_[static_cast<std::size_t>(i)].kept.size());
                pageDifferences_.push_back(std::move(d));
            }
        for (int j = 0; j < nr; ++j)
            if (baseOf[static_cast<std::size_t>(j)] < 0) {
                result_.pages.push_back(PageMatch{PageMatch::Kind::Inserted, -1, j, 0.0});
                Difference d;
                d.kind = DifferenceKind::PageInserted;
                d.revisedPage = j;
                d.tokensAfter = static_cast<int>(revisedWords_[static_cast<std::size_t>(j)].kept.size());
                pageDifferences_.push_back(std::move(d));
            }
        std::sort(pairs_.begin(), pairs_.end(), [](const Pair& x, const Pair& y) { return x.revised < y.revised; });
    }

    // ---- Layer 2: each pair's words ----------------------------------------------------------------------

    void diffPair(int index)
    {
        Pair& pair = pairs_[static_cast<std::size_t>(index)];
        const PageWords& a = baseWords_[static_cast<std::size_t>(pair.base)];
        const PageWords& b = revisedWords_[static_cast<std::size_t>(pair.revised)];
        const int words = static_cast<int>(a.keptIds.size() + b.keptIds.size());
        const int cap = std::min(options_.maxEdits,
                                 static_cast<int>(std::floor(options_.replaceShare * static_cast<double>(words))));
        const std::optional<std::vector<Edit>> edits =
            DocumentComparer::diff(a.keptIds, b.keptIds, std::max(cap, 0), &result_.editsExplored);

        if (!edits) {
            // OVER THE CAP: replaced, never diffed (D5-17's bound).
            pair.kind = PageMatch::Kind::Replaced;
            ++result_.pairsReplaced;
            Difference d;
            d.kind = DifferenceKind::PageReplaced;
            d.basePage = pair.base;
            d.revisedPage = pair.revised;
            d.tokensBefore = static_cast<int>(a.kept.size());
            d.tokensAfter = static_cast<int>(b.kept.size());
            pageDifferences_.push_back(std::move(d));
            return;
        }

        // PADDED with an empty equal run at each end, so every pure edit has two equal neighbours to slide
        // between (`slide`); an empty run splits no hunk.
        pair.script.clear();
        pair.script.push_back(Edit{Edit::Op::Equal, 0});
        pair.script.insert(pair.script.end(), edits->begin(), edits->end());
        pair.script.push_back(Edit{Edit::Op::Equal, 0});
        int ia = 0, ib = 0;
        int lastEqual = 0;
        int firstEdit = -1, editsInHunk = 0;
        Hunk hunk;
        bool open = false;
        const auto closeHunk = [&](int equalAfter) {
            if (!open) return;
            hunk.aEnd = ia;
            hunk.bEnd = ib;
            hunk.equalAfter = equalAfter;
            open = false;
            // ONE HUNK PER REGION, made here and not only when reported: the body's last paragraph deleted
            // next to the footer's date replaced is one hunk of the script (found at T12 on `report-v2.pdf`),
            // and a move must be matched against the paragraph alone. A part's word lists are its own; only a
            // hunk that is ONE edit and ONE region keeps its range in the kept sequence, which is what a
            // slide moves (`edit`, below).
            const auto regionOf = [&](const PageWords& page, int kept) {
                return page.tokens[static_cast<std::size_t>(page.kept[static_cast<std::size_t>(kept)])].region;
            };
            std::vector<Hunk> parts;
            for (const Region region : {Region::Header, Region::Body, Region::Footer}) {
                Hunk part;
                part.pair = index;
                part.aBegin = part.aEnd = -1;
                part.bBegin = part.bEnd = -1;
                for (int k = hunk.aBegin; k < hunk.aEnd; ++k) {
                    if (regionOf(a, k) != region) continue;
                    if (part.aBegin < 0) part.aBegin = k;
                    part.aEnd = k + 1;
                    part.deleted.push_back(a.kept[static_cast<std::size_t>(k)]);
                }
                for (int k = hunk.bBegin; k < hunk.bEnd; ++k) {
                    if (regionOf(b, k) != region) continue;
                    if (part.bBegin < 0) part.bBegin = k;
                    part.bEnd = k + 1;
                    part.inserted.push_back(b.kept[static_cast<std::size_t>(k)]);
                }
                if (part.deleted.empty() && part.inserted.empty()) continue;
                // A side this part has nothing on is an empty range: a pure insertion or deletion.
                if (part.aBegin < 0) part.aBegin = part.aEnd = hunk.aBegin;
                if (part.bBegin < 0) part.bBegin = part.bEnd = hunk.bBegin;
                parts.push_back(std::move(part));
            }
            for (std::size_t k = 0; k < parts.size(); ++k) {
                parts[k].equalBefore = k == 0 ? hunk.equalBefore : 0;
                parts[k].equalAfter = k + 1 == parts.size() ? hunk.equalAfter : 0;
                parts[k].edit = (parts.size() == 1 && editsInHunk == 1) ? firstEdit : -1;
                hunks_.push_back(std::move(parts[k]));
            }
        };
        const auto openHunk = [&](int at) {
            if (open) return;
            hunk = Hunk{};
            hunk.pair = index;
            hunk.aBegin = ia;
            hunk.bBegin = ib;
            hunk.equalBefore = lastEqual;
            firstEdit = at;
            editsInHunk = 0;
            open = true;
        };
        for (std::size_t e = 0; e < pair.script.size(); ++e) {
            const Edit& edit = pair.script[e];
            switch (edit.op) {
            case Edit::Op::Equal:
                closeHunk(edit.count);
                ia += edit.count;
                ib += edit.count;
                lastEqual = edit.count;
                break;
            case Edit::Op::Delete:
                openHunk(static_cast<int>(e));
                ++editsInHunk;
                for (int k = 0; k < edit.count; ++k) hunk.deleted.push_back(a.kept[static_cast<std::size_t>(ia + k)]);
                ia += edit.count;
                break;
            case Edit::Op::Insert:
                openHunk(static_cast<int>(e));
                ++editsInHunk;
                for (int k = 0; k < edit.count; ++k) hunk.inserted.push_back(b.kept[static_cast<std::size_t>(ib + k)]);
                ib += edit.count;
                break;
            }
        }
        closeHunk(0);
        if (pair.kind == PageMatch::Kind::Moved) {
            Difference d;
            d.kind = DifferenceKind::PageMoved;
            d.basePage = pair.base;
            d.revisedPage = pair.revised;
            pageDifferences_.push_back(std::move(d));
        }
    }

    // The (base token, revised token) pairs a pair's script keeps equal.
    std::vector<std::pair<int, int>> matchedOf(const Pair& pair) const
    {
        const PageWords& a = baseWords_[static_cast<std::size_t>(pair.base)];
        const PageWords& b = revisedWords_[static_cast<std::size_t>(pair.revised)];
        std::vector<std::pair<int, int>> matched;
        int ia = 0, ib = 0;
        for (const Edit& e : pair.script) {
            if (e.op == Edit::Op::Equal) {
                for (int k = 0; k < e.count; ++k)
                    matched.emplace_back(a.kept[static_cast<std::size_t>(ia + k)], b.kept[static_cast<std::size_t>(ib + k)]);
                ia += e.count;
                ib += e.count;
            } else if (e.op == Edit::Op::Delete) {
                ia += e.count;
            } else {
                ib += e.count;
            }
        }
        return matched;
    }

    // A PURE HUNK'S SLIDE, applied to its script: `k` words of the equal run after it move before it (or,
    // for a negative `k`, from before it to after) -- the same words, now faced by the words they equal.
    void slide(const Hunk& hunk, int k)
    {
        if (k == 0 || hunk.edit < 0) return;
        std::vector<Edit>& script = pairs_[static_cast<std::size_t>(hunk.pair)].script;
        const std::size_t e = static_cast<std::size_t>(hunk.edit);
        // A script is padded with an equal run at each end (`diffPair`), so a pure edit has both neighbours.
        if (e == 0 || e + 1 >= script.size()) return;
        Edit& before = script[e - 1];
        Edit& after = script[e + 1];
        if (before.op != Edit::Op::Equal || after.op != Edit::Op::Equal) return;
        if ((k > 0 && after.count < k) || (k < 0 && before.count < -k)) return;
        before.count += k;
        after.count -= k;
    }

    // A PAIR'S KIND, once every layer has spoken: identical unless something was reported on it.
    void settleKinds()
    {
        const auto touches = [](const Difference& d, const Pair& pair) {
            return d.basePage == pair.base || d.revisedPage == pair.revised;
        };
        for (Pair& pair : pairs_) {
            if (pair.kind != PageMatch::Kind::Identical && pair.kind != PageMatch::Kind::Modified) continue;
            bool changed = false;
            for (const auto* list : {&pairDifferences_, &moves_, &appearance_})
                for (const Difference& d : *list) changed = changed || touches(d, pair);
            pair.kind = changed ? PageMatch::Kind::Modified : PageMatch::Kind::Identical;
        }
    }

    // WHITESPACE and LAYOUT, over the words both sides kept.
    void matchedDifferences(const Pair& pair, const std::vector<std::pair<int, int>>& matched)
    {
        const std::vector<Token>& ta = baseWords_[static_cast<std::size_t>(pair.base)].tokens;
        const std::vector<Token>& tb = revisedWords_[static_cast<std::size_t>(pair.revised)].tokens;

        if (!options_.ignoreWhitespace) {
            Difference current;
            const auto flush = [&] {
                if (current.tokensAfter > 0) pairDifferences_.push_back(std::move(current));
                current = Difference{};
            };
            int previous = -2;
            for (std::size_t k = 0; k < matched.size(); ++k) {
                const Token& x = ta[static_cast<std::size_t>(matched[k].first)];
                const Token& y = tb[static_cast<std::size_t>(matched[k].second)];
                const bool differs = x.spacesBefore > 0 && y.spacesBefore > 0 && x.spacesBefore != y.spacesBefore;
                if (!differs) continue;
                if (previous != static_cast<int>(k) - 1) flush();
                if (current.tokensAfter == 0) {
                    current.kind = DifferenceKind::Whitespace;
                    current.region = y.region;
                    current.basePage = pair.base;
                    current.revisedPage = pair.revised;
                }
                current.tokensBefore += 1;
                current.tokensAfter += 1;
                current.before += (current.before.isEmpty() ? QString() : QStringLiteral(" ")) + x.text;
                current.after += (current.after.isEmpty() ? QString() : QStringLiteral(" ")) + y.text;
                current.baseRects.push_back(x.box);
                current.revisedRects.push_back(y.box);
                previous = static_cast<int>(k);
            }
            flush();
        }

        if (options_.sensitivity == CompareSensitivity::TextOnly) return;
        Difference current;
        double dx0 = 0.0, dy0 = 0.0;
        const auto flush = [&] {
            if (current.tokensAfter > 0) {
                current.baseRects = byLine(current.baseRects);
                current.revisedRects = byLine(current.revisedRects);
                pairDifferences_.push_back(std::move(current));
            }
            current = Difference{};
        };
        for (const auto& [ia, ib] : matched) {
            const Token& x = ta[static_cast<std::size_t>(ia)];
            const Token& y = tb[static_cast<std::size_t>(ib)];
            const double dx = y.box.left() - x.box.left();
            const double dy = y.box.bottom() - x.box.bottom();
            if (std::hypot(dx, dy) <= options_.layoutShiftPoints) {
                flush();
                continue;
            }
            if (current.tokensAfter > 0 && std::hypot(dx - dx0, dy - dy0) > 0.5) flush();
            if (current.tokensAfter == 0) {
                current.kind = DifferenceKind::Layout;
                current.region = y.region;
                current.basePage = pair.base;
                current.revisedPage = pair.revised;
                dx0 = dx;
                dy0 = dy;
            }
            current.tokensBefore += 1;
            current.tokensAfter += 1;
            current.before += (current.before.isEmpty() ? QString() : QStringLiteral(" ")) + x.text;
            current.after += (current.after.isEmpty() ? QString() : QStringLiteral(" ")) + y.text;
            current.baseRects.push_back(x.box);
            current.revisedRects.push_back(y.box);
        }
        flush();
    }

    // RECTANGLES ONE PER LINE: word boxes that share a baseline band are united.
    static std::vector<PdfRect> byLine(const std::vector<PdfRect>& boxes)
    {
        std::vector<PdfRect> out;
        for (const PdfRect& box : boxes) {
            if (!out.empty()) {
                PdfRect& last = out.back();
                const double overlap = std::min(last.top(), box.top()) - std::max(last.bottom(), box.bottom());
                if (overlap > 0.5 * std::min(last.size.height, box.size.height)) {
                    last = unite(last, box);
                    continue;
                }
            }
            out.push_back(box);
        }
        return out;
    }

    // A MOVE: a deleted run and an inserted run of the same words, anywhere in the document (A-100).
    //
    // TRIED AT EVERY POSITION THE RUN COULD EQUALLY HAVE TAKEN. A pure insertion between two equal stretches
    // can SLIDE while the word leaving one end equals the word arriving at the other, and a diff picks one
    // of those positions arbitrarily: `report-v2.pdf` moves a paragraph ending `baseline object schedule.`
    // after another ending the same way, and the insertion came out three words early -- the same words as
    // the deletion, rotated (found at T12). So each pure run is tried at each of its slides, and the slide
    // that matches is the one reported.
    struct Run {
        int hunk = -1;
        int offset = 0;
    };

    std::vector<int> slides(const std::vector<int>& sequence, int begin, int end, int before, int after) const
    {
        std::vector<int> out{0};
        for (int t = 1; t <= before && begin - t >= 0; ++t) {
            if (sequence[static_cast<std::size_t>(begin - t)] != sequence[static_cast<std::size_t>(end - t)]) break;
            out.push_back(-t);
        }
        for (int t = 1; t <= after && end + t - 1 < static_cast<int>(sequence.size()); ++t) {
            if (sequence[static_cast<std::size_t>(begin + t - 1)] != sequence[static_cast<std::size_t>(end + t - 1)]) break;
            out.push_back(t);
        }
        return out;
    }

    // A RUN'S WORDS at a slide: a hunk that is one edit in one region reads its range in the kept sequence,
    // moved by `offset`; any other hunk reads its own word list, and does not slide.
    std::vector<int> runOf(const Hunk& hunk, bool deleted, int offset) const
    {
        if (hunk.edit < 0) return deleted ? hunk.deleted : hunk.inserted;
        const Pair& pair = pairs_[static_cast<std::size_t>(hunk.pair)];
        const PageWords& page = deleted ? baseWords_[static_cast<std::size_t>(pair.base)]
                                        : revisedWords_[static_cast<std::size_t>(pair.revised)];
        const int begin = (deleted ? hunk.aBegin : hunk.bBegin) + offset;
        const int end = (deleted ? hunk.aEnd : hunk.bEnd) + offset;
        return std::vector<int>(page.kept.begin() + begin, page.kept.begin() + end);
    }

    std::vector<int> idsOf(const Hunk& hunk, bool deleted, const std::vector<int>& run) const
    {
        const Pair& pair = pairs_[static_cast<std::size_t>(hunk.pair)];
        const std::vector<Token>& tokens = deleted ? baseWords_[static_cast<std::size_t>(pair.base)].tokens
                                                   : revisedWords_[static_cast<std::size_t>(pair.revised)].tokens;
        std::vector<int> ids;
        for (const int t : run) ids.push_back(tokens[static_cast<std::size_t>(t)].id);
        return ids;
    }

    std::vector<int> slidesOf(const Hunk& hunk, bool deleted) const
    {
        const bool pure = hunk.edit >= 0 && (deleted ? hunk.inserted.empty() : hunk.deleted.empty());
        if (!pure) return {0};
        const Pair& pair = pairs_[static_cast<std::size_t>(hunk.pair)];
        const std::vector<int>& ids = deleted ? baseWords_[static_cast<std::size_t>(pair.base)].keptIds
                                              : revisedWords_[static_cast<std::size_t>(pair.revised)].keptIds;
        return deleted ? slides(ids, hunk.aBegin, hunk.aEnd, hunk.equalBefore, hunk.equalAfter)
                       : slides(ids, hunk.bBegin, hunk.bEnd, hunk.equalBefore, hunk.equalAfter);
    }

    void findMoves()
    {
        std::map<std::vector<int>, std::vector<Run>> deletedRuns;   // ids -> (hunk, slide)
        for (std::size_t h = 0; h < hunks_.size(); ++h) {
            const Hunk& hunk = hunks_[h];
            if (static_cast<int>(hunk.deleted.size()) < options_.minMoveTokens) continue;
            for (const int k : slidesOf(hunk, true))
                deletedRuns[idsOf(hunk, true, runOf(hunk, true, k))].push_back(Run{static_cast<int>(h), k});
        }
        for (std::size_t h = 0; h < hunks_.size(); ++h) {
            Hunk& in = hunks_[h];
            if (in.moved || static_cast<int>(in.inserted.size()) < options_.minMoveTokens) continue;
            bool found = false;
            for (const int k : slidesOf(in, false)) {
                const std::vector<int> inserted = runOf(in, false, k);
                const auto it = deletedRuns.find(idsOf(in, false, inserted));
                if (it == deletedRuns.end()) continue;
                for (const Run& run : it->second) {
                    Hunk& out = hunks_[static_cast<std::size_t>(run.hunk)];
                    if (out.moved || static_cast<std::size_t>(run.hunk) == h) continue;
                    // THE MOVE takes the two runs out of their hunks; what else either hunk held stays a change.
                    const Pair& from = pairs_[static_cast<std::size_t>(out.pair)];
                    const Pair& to = pairs_[static_cast<std::size_t>(in.pair)];
                    Difference d;
                    d.kind = DifferenceKind::Move;
                    d.basePage = from.base;
                    d.revisedPage = to.revised;
                    describe(d, runOf(out, true, run.offset), inserted, from, to);
                    moves_.push_back(std::move(d));
                    slide(out, run.offset);
                    slide(in, k);
                    out.deleted.clear();
                    in.inserted.clear();
                    out.moved = in.moved = true;
                    found = true;
                    break;
                }
                if (found) break;
            }
        }
    }

    void describe(Difference& d, const std::vector<int>& deleted, const std::vector<int>& inserted,
                  const Pair& from, const Pair& to) const
    {
        const std::vector<Token>& ta = baseWords_[static_cast<std::size_t>(from.base)].tokens;
        const std::vector<Token>& tb = revisedWords_[static_cast<std::size_t>(to.revised)].tokens;
        std::vector<PdfRect> before, after;
        std::set<Region> regions;
        for (const int t : deleted) {
            const Token& token = ta[static_cast<std::size_t>(t)];
            d.before += (d.before.isEmpty() ? QString() : QStringLiteral(" ")) + token.text;
            before.push_back(token.box);
            regions.insert(token.region);
        }
        for (const int t : inserted) {
            const Token& token = tb[static_cast<std::size_t>(t)];
            d.after += (d.after.isEmpty() ? QString() : QStringLiteral(" ")) + token.text;
            after.push_back(token.box);
            regions.insert(token.region);
        }
        d.tokensBefore = static_cast<int>(deleted.size());
        d.tokensAfter = static_cast<int>(inserted.size());
        d.baseRects = byLine(before);
        d.revisedRects = byLine(after);
        d.region = regions.size() == 1 ? *regions.begin() : Region::Body;
    }

    void emitHunks()
    {
        for (const Hunk& hunk : hunks_) {
            if (hunk.deleted.empty() && hunk.inserted.empty()) continue;
            const Pair& pair = pairs_[static_cast<std::size_t>(hunk.pair)];
            const std::vector<Token>& ta = baseWords_[static_cast<std::size_t>(pair.base)].tokens;
            const std::vector<Token>& tb = revisedWords_[static_cast<std::size_t>(pair.revised)].tokens;
            // ONE DIFFERENCE PER REGION: a hunk at the end of the body that reaches into the footer is a
            // body change and a footer change, and a footer change is what the band's ignore-list removes.
            for (const Region region : {Region::Header, Region::Body, Region::Footer}) {
                std::vector<int> deleted, inserted;
                for (const int t : hunk.deleted)
                    if (ta[static_cast<std::size_t>(t)].region == region) deleted.push_back(t);
                for (const int t : hunk.inserted)
                    if (tb[static_cast<std::size_t>(t)].region == region) inserted.push_back(t);
                if (deleted.empty() && inserted.empty()) continue;
                Difference d;
                d.kind = deleted.empty() ? DifferenceKind::Insert
                       : inserted.empty() ? DifferenceKind::Delete
                                          : DifferenceKind::Replace;
                d.basePage = pair.base;
                d.revisedPage = pair.revised;
                describe(d, deleted, inserted, pair, pair);
                pairDifferences_.push_back(std::move(d));
            }
        }
    }

    // ---- Layer 3: appearance -----------------------------------------------------------------------------

    void appearance(int index)
    {
        const Pair& pair = pairs_[static_cast<std::size_t>(index)];
        if (pair.kind == PageMatch::Kind::Replaced || !rasters_) return;
        const std::optional<RasterImage> a = rasters_(CompareSide::Base, pair.base);
        const std::optional<RasterImage> b = rasters_(CompareSide::Revised, pair.revised);
        if (!a || !b || !a->isValid() || !b->isValid()) {
            ++result_.rastersMissing;
            return;
        }
        const ComparePage& pa = base_[static_cast<std::size_t>(pair.base)];
        const ComparePage& pb = revised_[static_cast<std::size_t>(pair.revised)];
        const int width = std::min(a->width, b->width);
        const int height = std::min(a->height, b->height);
        std::vector<std::uint8_t> masked(static_cast<std::size_t>(width) * height, 0);

        // EVERY RECTANGLE LAYER 2 REPORTED ON THIS PAIR, both sides', masked on both pages.
        const auto mask = [&](const ComparePage& page, const PdfRect& rect) {
            const PageTransform t(page.cropBox.size, 1.0, 72.0, page.rotation);
            double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
            for (const PdfPoint corner : {PdfPoint{rect.left(), rect.bottom()}, PdfPoint{rect.right(), rect.top()},
                                          PdfPoint{rect.left(), rect.top()}, PdfPoint{rect.right(), rect.bottom()}}) {
                const DevicePoint p = t.toDevice(PdfPoint{corner.x - page.cropBox.left(), corner.y - page.cropBox.bottom()});
                x0 = std::min(x0, p.x);
                y0 = std::min(y0, p.y);
                x1 = std::max(x1, p.x);
                y1 = std::max(y1, p.y);
            }
            const int m = options_.maskMargin;
            const int ix0 = std::max(0, static_cast<int>(std::floor(x0)) - m);
            const int iy0 = std::max(0, static_cast<int>(std::floor(y0)) - m);
            const int ix1 = std::min(width - 1, static_cast<int>(std::ceil(x1)) + m);
            const int iy1 = std::min(height - 1, static_cast<int>(std::ceil(y1)) + m);
            for (int y = iy0; y <= iy1; ++y)
                for (int x = ix0; x <= ix1; ++x) masked[static_cast<std::size_t>(y) * width + x] = 1;
        };
        const auto claimed = [&](const Difference& d) {
            if (d.basePage == pair.base)
                for (const PdfRect& r : d.baseRects) mask(pa, r);
            if (d.revisedPage == pair.revised)
                for (const PdfRect& r : d.revisedRects) mask(pb, r);
        };
        for (const Difference& d : pairDifferences_) claimed(d);
        for (const Difference& d : moves_) claimed(d);
        // AND WHAT AN IGNORE-LIST DROPPED: a page number ignored as text is ignored as pixels, or ignoring it
        // at this sensitivity would ignore nothing (found at T12: every footer number of an inserted page's
        // successors came back as an appearance difference).
        for (const auto& [words, page] : {std::pair{&baseWords_[static_cast<std::size_t>(pair.base)], &pa},
                                          std::pair{&revisedWords_[static_cast<std::size_t>(pair.revised)], &pb}})
            for (std::size_t t = 0; t < words->tokens.size(); ++t)
                if (words->ignored[t]) mask(*page, words->tokens[t].box);

        // THE DIFFERING PIXELS, in cells.
        constexpr int kCell = 8;
        const int cw = (width + kCell - 1) / kCell;
        const int ch = (height + kCell - 1) / kCell;
        struct Cell {
            int count = 0;
            int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
        };
        std::vector<Cell> cells(static_cast<std::size_t>(cw) * ch);
        const char* pa8 = a->pixels.constData();
        const char* pb8 = b->pixels.constData();
        for (int y = 0; y < height; ++y) {
            const auto* ra = reinterpret_cast<const std::uint8_t*>(pa8 + static_cast<std::ptrdiff_t>(y) * a->bytesPerLine);
            const auto* rb = reinterpret_cast<const std::uint8_t*>(pb8 + static_cast<std::ptrdiff_t>(y) * b->bytesPerLine);
            for (int x = 0; x < width; ++x) {
                if (masked[static_cast<std::size_t>(y) * width + x]) continue;
                int worst = 0;
                for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(int(ra[x * 4 + c]) - int(rb[x * 4 + c])));
                if (worst <= options_.pixelTolerance) continue;
                Cell& cell = cells[static_cast<std::size_t>(y / kCell) * cw + x / kCell];
                ++cell.count;
                cell.x0 = std::min(cell.x0, x);
                cell.y0 = std::min(cell.y0, y);
                cell.x1 = std::max(cell.x1, x);
                cell.y1 = std::max(cell.y1, y);
            }
        }

        // CLUSTERS: cells with a differing pixel, joined across a gap of one cell.
        std::vector<int> label(cells.size(), -1);
        for (int start = 0; start < static_cast<int>(cells.size()); ++start) {
            if (cells[static_cast<std::size_t>(start)].count == 0 || label[static_cast<std::size_t>(start)] >= 0) continue;
            std::vector<int> stack{start};
            label[static_cast<std::size_t>(start)] = start;
            int count = 0, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
            while (!stack.empty()) {
                const int c = stack.back();
                stack.pop_back();
                const Cell& cell = cells[static_cast<std::size_t>(c)];
                count += cell.count;
                x0 = std::min(x0, cell.x0);
                y0 = std::min(y0, cell.y0);
                x1 = std::max(x1, cell.x1);
                y1 = std::max(y1, cell.y1);
                const int cx = c % cw, cy = c / cw;
                for (int ny = std::max(0, cy - 2); ny <= std::min(ch - 1, cy + 2); ++ny)
                    for (int nx = std::max(0, cx - 2); nx <= std::min(cw - 1, cx + 2); ++nx) {
                        const int n = ny * cw + nx;
                        if (cells[static_cast<std::size_t>(n)].count == 0 || label[static_cast<std::size_t>(n)] >= 0) continue;
                        label[static_cast<std::size_t>(n)] = start;
                        stack.push_back(n);
                    }
            }
            if (count < options_.minClusterPixels) continue;
            Difference d;
            d.kind = DifferenceKind::Appearance;
            d.basePage = pair.base;
            d.revisedPage = pair.revised;
            d.baseRects.push_back(toPage(pa, x0, y0, x1 + 1, y1 + 1));
            d.revisedRects.push_back(toPage(pb, x0, y0, x1 + 1, y1 + 1));
            appearance_.push_back(std::move(d));
        }
    }

    static PdfRect toPage(const ComparePage& page, int x0, int y0, int x1, int y1)
    {
        const PageTransform t(page.cropBox.size, 1.0, 72.0, page.rotation);
        const PdfPoint a = t.toPdf(DevicePoint{static_cast<double>(x0), static_cast<double>(y0)});
        const PdfPoint b = t.toPdf(DevicePoint{static_cast<double>(x1), static_cast<double>(y1)});
        const double left = std::min(a.x, b.x) + page.cropBox.left();
        const double bottom = std::min(a.y, b.y) + page.cropBox.bottom();
        return PdfRect{PdfPoint{left, bottom}, PdfSize{std::abs(b.x - a.x), std::abs(b.y - a.y)}};
    }

    // ---- The outcome, in reading order ---------------------------------------------------------------------

    void orderPages()
    {
        for (const Pair& pair : pairs_) result_.pages.push_back(PageMatch{pair.kind, pair.base, pair.revised, pair.similarity});
        // A deleted page sits after the revised page its predecessor in the base went to.
        const auto key = [&](const PageMatch& m) -> double {
            if (m.revised >= 0) return m.revised;
            double after = -0.5;
            for (const PageMatch& other : result_.pages)
                if (other.base >= 0 && other.base < m.base && other.revised >= 0)
                    after = std::max(after, static_cast<double>(other.revised));
            return after + 0.5 + m.base * 1e-6;
        };
        std::stable_sort(result_.pages.begin(), result_.pages.end(),
                         [&](const PageMatch& x, const PageMatch& y) { return key(x) < key(y); });

        result_.differences = std::move(pageDifferences_);
        // Per pair, in the revised document's order: its moves, its text, then the layers above.
        std::vector<Difference> rest;
        rest.insert(rest.end(), std::make_move_iterator(moves_.begin()), std::make_move_iterator(moves_.end()));
        rest.insert(rest.end(), std::make_move_iterator(pairDifferences_.begin()), std::make_move_iterator(pairDifferences_.end()));
        rest.insert(rest.end(), std::make_move_iterator(appearance_.begin()), std::make_move_iterator(appearance_.end()));
        std::stable_sort(rest.begin(), rest.end(),
                         [](const Difference& x, const Difference& y) { return x.revisedPage < y.revisedPage; });
        result_.differences.insert(result_.differences.end(), std::make_move_iterator(rest.begin()),
                                   std::make_move_iterator(rest.end()));
    }

    const CompareDocument& base_;
    const CompareDocument& revised_;
    const CompareOptions& options_;
    const RasterProvider& rasters_;
    const std::atomic<bool>* cancel_;
    const DocumentComparer::Progress& progress_;

    Interner interner_;
    std::vector<PageWords> baseWords_;
    std::vector<PageWords> revisedWords_;
    std::vector<Pair> pairs_;
    std::vector<Hunk> hunks_;
    std::vector<Difference> pageDifferences_;
    std::vector<Difference> pairDifferences_;
    std::vector<Difference> moves_;
    std::vector<Difference> appearance_;
    ComparisonResult result_;
    int total_ = 0;
    int done_ = 0;
    int reported_ = -1;
};

} // namespace

int ComparisonResult::count(DifferenceKind kind) const
{
    return static_cast<int>(std::count_if(differences.begin(), differences.end(),
                                          [kind](const Difference& d) { return d.kind == kind; }));
}

std::optional<std::vector<DocumentComparer::Edit>> DocumentComparer::diff(const std::vector<int>& a,
                                                                          const std::vector<int>& b,
                                                                          int maxDistance, long long* explored)
{
    Differ differ(a, b, explored);
    // The forward search meets the backward one at HALF the distance, so the limit on it is half the cap.
    const int dLimit = maxDistance < 0 ? -1 : maxDistance / 2 + 1;
    if (!differ.run(0, static_cast<int>(a.size()), 0, static_cast<int>(b.size()), dLimit)) return std::nullopt;
    std::vector<Edit> edits = differ.take();
    if (maxDistance >= 0 && distanceOf(edits) > maxDistance) return std::nullopt;
    return edits;
}

ComparisonResult DocumentComparer::compare(const CompareDocument& base, const CompareDocument& revised,
                                           const CompareOptions& options, const RasterProvider& rasters,
                                           const std::atomic<bool>* cancel, const Progress& progress)
{
    Comparison comparison(base, revised, options, rasters, cancel, progress);
    return comparison.run();
}

// =========================================================================================================
// ComparisonRun
// =========================================================================================================

ComparisonRun::ComparisonRun(CompareDocument base, CompareDocument revised, CompareOptions options,
                             RasterProvider rasters)
    : base_(std::move(base)), revised_(std::move(revised)), options_(options), rasters_(std::move(rasters))
{
}

ComparisonRun::~ComparisonRun()
{
    cancel();
    if (worker_.joinable()) worker_.join();
}

void ComparisonRun::setWakeHandler(WakeHandler handler)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    wake_ = std::move(handler);
}

void ComparisonRun::start()
{
    if (worker_.joinable()) return;
    worker_ = std::thread([this] {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            workerId_ = std::this_thread::get_id();
        }
        // THE READINGS FIRST, HERE (`setReadingSource`): each page handed in without one, between which the
        // cancel is honoured as it is between pairs -- a comparison cancelled while 900 pages are analyzed
        // stops within one page.
        if (readings_) {
            const auto resolve = [this](CompareDocument& pages, CompareSide side) {
                for (std::size_t i = 0; i < pages.size(); ++i) {
                    if (cancelled_.load()) return;
                    if (!pages[i].reading) pages[i].reading = readings_(side, static_cast<int>(i));
                }
            };
            resolve(base_, CompareSide::Base);
            resolve(revised_, CompareSide::Revised);
        }
        ComparisonResult result = DocumentComparer::compare(
            base_, revised_, options_, rasters_, &cancelled_,
            [this](int done, int total) { post(Posted{false, done, total, nullptr}); });
        post(Posted{true, 0, 0, std::make_shared<const ComparisonResult>(std::move(result))});
    });
}

CompareOptions CompareOptions::fromSettings(const Settings& settings)
{
    CompareOptions options;
    options.sensitivity = settings.get(keys::documents::compareSensitivity);
    options.ignoreHeaderFooter = settings.get(keys::documents::compareIgnoreHeaderFooter);
    options.ignorePageNumbers = settings.get(keys::documents::compareIgnorePageNumbers);
    options.ignoreWhitespace = settings.get(keys::documents::compareIgnoreWhitespace);
    return options;
}

void ComparisonRun::post(Posted item)
{
    const bool finished = item.finished;
    WakeHandler wake;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (finished) workerFinished_ = true;
        posted_.push_back(std::move(item));
        wake = wake_;
    }
    if (finished) finishedCondition_.notify_all();
    if (wake) wake();
}

int ComparisonRun::deliverReady()
{
    std::deque<Posted> ready;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(posted_);
    }
    for (const Posted& item : ready) {
        if (item.finished) {
            if (onFinished_) onFinished_(*item.result);
        } else if (onProgress_) {
            onProgress_(item.done, item.total);
        }
    }
    return static_cast<int>(ready.size());
}

bool ComparisonRun::waitForFinished(int milliseconds)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return finishedCondition_.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] { return workerFinished_; });
}

bool ComparisonRun::isFinished() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return workerFinished_;
}

std::thread::id ComparisonRun::workerThread() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return workerId_;
}

} // namespace rawpage::core::text
