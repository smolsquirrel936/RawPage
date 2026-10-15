// core/doc/CommentMigrator -- implementation. Stage 6 T13. See the header for the two rules and why a comment that
// cannot be placed is reported rather than guessed at.

#include "core/doc/CommentMigrator.h"

#include "core/doc/AnnotationGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace rawpage::core {
namespace {

using text::PageReading;
using text::ReadingItem;

Error migrateError(const char* key, const QString& context)
{
    return Error{ErrorCode::NotFound, QLatin1String(key), context};
}

bool isMarkup(const Annotation& a)
{
    switch (a.subtype) {
    case AnnotationSubtype::Highlight:
    case AnnotationSubtype::Underline:
    case AnnotationSubtype::StrikeOut:
    case AnnotationSubtype::Squiggly:
        return true;
    case AnnotationSubtype::Redact:
        return a.quadPoints.size() >= 8;   // a redaction mark over text; one over an area is a box
    default:
        return false;
    }
}

PdfPoint centreOf(const PdfRect& r)
{
    return PdfPoint{r.left() + r.size.width / 2.0, r.bottom() + r.size.height / 2.0};
}

// Whether `p` is inside the quadrilateral `q` (8 numbers, in any corner order): its corners taken round their centre
// -- Acrobat's order and ISO's figure's differ (guide A-20) -- and the point on the inner side of every edge.
bool insideQuad(PdfPoint p, const double* q)
{
    std::array<PdfPoint, 4> corners{PdfPoint{q[0], q[1]}, PdfPoint{q[2], q[3]}, PdfPoint{q[4], q[5]}, PdfPoint{q[6], q[7]}};
    const double cx = (q[0] + q[2] + q[4] + q[6]) / 4.0;
    const double cy = (q[1] + q[3] + q[5] + q[7]) / 4.0;
    std::sort(corners.begin(), corners.end(), [cx, cy](const PdfPoint& a, const PdfPoint& b) {
        return std::atan2(a.y - cy, a.x - cx) < std::atan2(b.y - cy, b.x - cx);
    });
    bool positive = false;
    bool negative = false;
    for (std::size_t i = 0; i < 4; ++i) {
        const PdfPoint& a = corners[i];
        const PdfPoint& b = corners[(i + 1) % 4];
        const double cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (cross > 1e-9) positive = true;
        if (cross < -1e-9) negative = true;
    }
    return !(positive && negative);
}

void appendChar(CommentMigrator::FlatText& flat, char32_t codepoint, int position)
{
    const QString s = QString::fromUcs4(&codepoint, 1);
    for (const QChar c : s) {
        flat.text += c;
        flat.position.push_back(position);
    }
}

void appendSpace(CommentMigrator::FlatText& flat)
{
    if (flat.text.isEmpty() || flat.text.back() == QLatin1Char(' ')) return;
    flat.text += QLatin1Char(' ');
    flat.position.push_back(-1);
}

bool isWhite(char32_t c)
{
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == 0xA0 || c == 0x3000;
}

// The words of `page`'s reading positions [begin, end), a break of any kind one space.
QString textOf(const PageReading& page, int begin, int end)
{
    CommentMigrator::FlatText flat;
    for (int pos = begin; pos < end; ++pos) {
        if (pos > begin && page.breakBefore(pos) != ReadingItem::Kind::Char) appendSpace(flat);
        const char32_t c = page.charAt(pos).codepoint;
        if (isWhite(c)) appendSpace(flat);
        else appendChar(flat, c, pos);
    }
    return flat.text.trimmed();
}

double squaredGap(PdfPoint p, const PdfRect& box)
{
    const double dx = std::max({box.left() - p.x, 0.0, p.x - box.right()});
    const double dy = std::max({box.bottom() - p.y, 0.0, p.y - box.top()});
    return dx * dx + dy * dy;
}

struct Placement {
    int page = -1;
    double dx = 0.0;
    double dy = 0.0;
    std::vector<std::pair<double, double>> perQuad;   // markup: each quadrilateral's own move
    QString text;
};

// Where the revision put `page`: the page the alignment pairs it with, or none.
std::optional<int> alignedPage(const std::vector<text::PageMatch>& alignment, int page)
{
    for (const text::PageMatch& m : alignment) {
        if (m.base == page && m.revised >= 0) return m.revised;
    }
    return std::nullopt;
}

// THE FIRST RULE: a markup comment by the words it covers.
Result<Placement> placeMarkup(const Annotation& a, const PageReading& base, const PageReading& revised, int target)
{
    const std::vector<double>& quads = a.quadPoints;
    const std::size_t count = quads.size() / 8;
    // The positions each quadrilateral covers, and all of them.
    std::vector<int> firstOf(count, -1);
    int first = std::numeric_limits<int>::max();
    int last = -1;
    for (int pos = 0; pos < base.size(); ++pos) {
        const text::TextChar& c = base.charAt(pos);
        if (isWhite(c.codepoint)) continue;
        const PdfPoint centre = centreOf(c.box);
        for (std::size_t q = 0; q < count; ++q) {
            if (!insideQuad(centre, quads.data() + 8 * q)) continue;
            if (firstOf[q] < 0) firstOf[q] = pos;
            first = std::min(first, pos);
            last = std::max(last, pos);
            break;
        }
    }
    if (last < 0) return migrateError("error.migrate.noText", a.nm);

    const CommentMigrator::FlatText flatBase = CommentMigrator::flatten(base);
    const auto indexOfPosition = [](const CommentMigrator::FlatText& flat, int position) -> qsizetype {
        const auto found = std::find(flat.position.begin(), flat.position.end(), position);
        return found == flat.position.end() ? -1 : static_cast<qsizetype>(found - flat.position.begin());
    };
    const qsizetype startBase = indexOfPosition(flatBase, first);
    qsizetype endBase = indexOfPosition(flatBase, last);
    if (startBase < 0 || endBase < 0) return migrateError("error.migrate.noText", a.nm);
    while (endBase + 1 < static_cast<qsizetype>(flatBase.position.size()) && flatBase.position[endBase + 1] == last) ++endBase;
    const QString phrase = flatBase.text.mid(startBase, endBase - startBase + 1);
    // Whether the phrase is a whole word at either end on the original -- asked of an occurrence on the revision too,
    // so a highlight on a word is not moved onto the middle of a longer one.
    const auto wordEdge = [](const QString& text, qsizetype at) { return at < 0 || at >= text.size() || text[at] == QLatin1Char(' '); };
    const bool wholeBefore = wordEdge(flatBase.text, startBase - 1);
    const bool wholeAfter = wordEdge(flatBase.text, endBase + 1);

    const CommentMigrator::FlatText flatRevised = CommentMigrator::flatten(revised);
    std::vector<qsizetype> found;
    for (qsizetype at = flatRevised.text.indexOf(phrase); at >= 0; at = flatRevised.text.indexOf(phrase, at + 1)) {
        if (wholeBefore && !wordEdge(flatRevised.text, at - 1)) continue;
        if (wholeAfter && !wordEdge(flatRevised.text, at + phrase.size())) continue;
        found.push_back(at);
    }
    if (found.empty()) return migrateError("error.migrate.textGone", phrase);

    // Where the text was set on the original, and the occurrence nearest it.
    const PdfPoint was = base.charAt(first).origin;
    qsizetype chosen = found.front();
    double nearest = std::numeric_limits<double>::infinity();
    for (const qsizetype at : found) {
        const int pos = flatRevised.position[static_cast<std::size_t>(at)];
        if (pos < 0) continue;
        const PdfPoint now = revised.charAt(pos).origin;
        const double d = (now.x - was.x) * (now.x - was.x) + (now.y - was.y) * (now.y - was.y);
        if (d < nearest) {
            nearest = d;
            chosen = at;
        }
    }

    Placement out;
    out.page = target;
    out.text = phrase;
    for (std::size_t q = 0; q < count; ++q) {
        const int from = firstOf[q] >= 0 ? firstOf[q] : first;
        const qsizetype offset = indexOfPosition(flatBase, from) - startBase;
        const qsizetype there = chosen + std::max<qsizetype>(offset, 0);
        int to = there < static_cast<qsizetype>(flatRevised.position.size()) ? flatRevised.position[static_cast<std::size_t>(there)] : -1;
        if (to < 0) to = flatRevised.position[static_cast<std::size_t>(chosen)];
        const PdfPoint a0 = base.charAt(from).origin;
        const PdfPoint a1 = revised.charAt(to).origin;
        out.perQuad.emplace_back(a1.x - a0.x, a1.y - a0.y);
    }
    out.dx = out.perQuad.front().first;
    out.dy = out.perQuad.front().second;
    return out;
}

// A line of a TEXT block -- a table's cells are not a line of prose a note is written beside.
bool isProseLine(const PageReading& page, int line)
{
    const text::Span span = page.lineSpan(line);
    if (span.isEmpty()) return false;
    const int block = page.blockOf(span.begin);
    const std::vector<text::TextBlock>& blocks = page.layout().blocks;
    return block >= 0 && block < static_cast<int>(blocks.size()) && blocks[static_cast<std::size_t>(block)].kind == text::BlockKind::Text;
}

// THE SECOND RULE: any other comment by the nearest line of text.
Result<Placement> placeByLine(const Annotation& a, const PageReading& base, const PageReading& revised, int target)
{
    const PdfPoint centre = centreOf(a.rect);
    const std::vector<text::TextLine>& lines = base.layout().lines;
    int anchor = -1;
    double best = std::numeric_limits<double>::infinity();
    for (int l = 0; l < static_cast<int>(lines.size()); ++l) {
        if (!isProseLine(base, l)) continue;
        const text::Span span = base.lineSpan(l);
        if (textOf(base, span.begin, span.end).isEmpty()) continue;
        const double gap = squaredGap(centre, lines[static_cast<std::size_t>(l)].box);
        const bool higher = anchor >= 0 && lines[static_cast<std::size_t>(l)].baseline > lines[static_cast<std::size_t>(anchor)].baseline;
        if (gap < best || (gap == best && higher)) {
            best = gap;
            anchor = l;
        }
    }
    if (anchor < 0) return migrateError("error.migrate.noAnchor", a.nm);

    const text::Span was = base.lineSpan(anchor);
    const QString words = textOf(base, was.begin, was.end);
    std::vector<int> same;
    for (int l = 0; l < static_cast<int>(revised.layout().lines.size()); ++l) {
        const text::Span span = revised.lineSpan(l);
        if (!span.isEmpty() && textOf(revised, span.begin, span.end) == words) same.push_back(l);
    }
    if (same.size() != 1) return migrateError("error.migrate.anchorGone", words);

    const PdfPoint from = base.charAt(was.begin).origin;
    const PdfPoint to = revised.charAt(revised.lineSpan(same.front()).begin).origin;
    Placement out;
    out.page = target;
    out.dx = to.x - from.x;
    out.dy = to.y - from.y;
    return out;
}

} // namespace

int MigrationResult::count(MigrationEntry::Outcome outcome) const
{
    return static_cast<int>(std::count_if(entries.begin(), entries.end(),
                                          [outcome](const MigrationEntry& e) { return e.outcome == outcome; }));
}

CommentMigrator::FlatText CommentMigrator::flatten(const PageReading& page)
{
    FlatText flat;
    for (int pos = 0; pos < page.size(); ++pos) {
        if (pos > 0 && page.breakBefore(pos) != ReadingItem::Kind::Char) appendSpace(flat);
        const char32_t c = page.charAt(pos).codepoint;
        if (isWhite(c)) appendSpace(flat);
        else appendChar(flat, c, pos);
    }
    if (!flat.text.isEmpty() && flat.text.back() == QLatin1Char(' ')) {
        flat.text.chop(1);
        flat.position.pop_back();
    }
    return flat;
}

QString CommentMigrator::textUnder(const PageReading& page, const std::vector<double>& quadPoints)
{
    const std::size_t count = quadPoints.size() / 8;
    int first = std::numeric_limits<int>::max();
    int last = -1;
    for (int pos = 0; pos < page.size(); ++pos) {
        const text::TextChar& c = page.charAt(pos);
        if (isWhite(c.codepoint)) continue;
        for (std::size_t q = 0; q < count; ++q) {
            if (insideQuad(centreOf(c.box), quadPoints.data() + 8 * q)) {
                first = std::min(first, pos);
                last = std::max(last, pos);
                break;
            }
        }
    }
    if (last < 0) return QString();
    return textOf(page, first, last + 1);
}

MigrationResult CommentMigrator::migrate(const Fdf& original, const Pages& base, const Pages& revised,
                                         const std::vector<text::PageMatch>& alignment)
{
    MigrationResult out;
    out.placed.file = original.file;
    const std::vector<Annotation>& all = original.annotations;

    // THE THREADS: each comment's root -- the comment it answers, the one that answers, up to one that answers nothing
    // in the exchange -- and each pop-up with the comment it belongs to.
    std::map<QString, std::size_t> byName;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].subtype != AnnotationSubtype::Popup) byName.emplace(all[i].id.nm, i);
    }
    std::vector<std::size_t> rootOf(all.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        std::size_t at = i;
        std::set<std::size_t> seen{at};
        while (all[at].inReplyTo && all[at].inReplyTo->kind == AnnotationId::Kind::Created) {
            const auto parent = byName.find(all[at].inReplyTo->nm);
            if (parent == byName.end() || !seen.insert(parent->second).second) break;
            at = parent->second;
        }
        rootOf[i] = at;
    }
    std::map<QString, std::size_t> popupOf;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].subtype == AnnotationSubtype::Popup && all[i].parent && all[i].parent->kind == AnnotationId::Kind::Created)
            popupOf.emplace(all[i].parent->nm, i);
    }

    for (std::size_t r = 0; r < all.size(); ++r) {
        const Annotation& a = all[r];
        if (a.subtype == AnnotationSubtype::Popup || rootOf[r] != r) continue;

        MigrationEntry entry;
        entry.name = a.id.nm;
        entry.subtype = a.subtype;
        entry.page = a.page;

        Result<Placement> placed = [&]() -> Result<Placement> {
            const std::optional<int> target = alignedPage(alignment, a.page);
            if (!target) return migrateError("error.migrate.pageGone", QString::number(a.page + 1));
            const auto pageOf = [](const Pages& pages, int index) -> const PageReading* {
                return index >= 0 && index < static_cast<int>(pages.size()) ? pages[static_cast<std::size_t>(index)].get() : nullptr;
            };
            const PageReading* from = pageOf(base, a.page);
            const PageReading* to = pageOf(revised, *target);
            if (from == nullptr || to == nullptr || from->isEmpty() || to->isEmpty())
                return migrateError("error.migrate.noText", QString::number(a.page + 1));
            return isMarkup(a) ? placeMarkup(a, *from, *to, *target) : placeByLine(a, *from, *to, *target);
        }();
        if (!placed) {
            entry.outcome = MigrationEntry::Outcome::Reported;
            entry.why = placed.error();
            if (isMarkup(a) && placed.error().messageKey == QLatin1String("error.migrate.textGone")) entry.text = placed.error().context;
            out.entries.push_back(std::move(entry));
            continue;
        }
        const Placement& p = placed.value();
        entry.placedPage = p.page;
        entry.text = p.text;

        // The comment and its thread, moved; each with its pop-up.
        const auto land = [&](const Annotation& member, bool isRoot) {
            Annotation moved = translated(member, p.dx, p.dy);
            if (isRoot && !p.perQuad.empty()) {
                moved.quadPoints = member.quadPoints;
                bool uniform = true;
                for (std::size_t q = 0; q < p.perQuad.size(); ++q) {
                    for (std::size_t k = 0; k < 8; k += 2) {
                        moved.quadPoints[8 * q + k] += p.perQuad[q].first;
                        moved.quadPoints[8 * q + k + 1] += p.perQuad[q].second;
                    }
                    uniform = uniform && p.perQuad[q] == p.perQuad.front();
                }
                if (!uniform) {
                    if (const std::optional<PdfRect> bounds = boundsOfPoints(moved.quadPoints)) {
                        const double left = std::min(moved.rect.left(), bounds->left());
                        const double bottom = std::min(moved.rect.bottom(), bounds->bottom());
                        const double right = std::max(moved.rect.right(), bounds->right());
                        const double top = std::max(moved.rect.top(), bounds->top());
                        moved.rect = PdfRect{PdfPoint{left, bottom}, PdfSize{right - left, top - bottom}};
                    }
                }
            }
            moved.page = p.page;
            out.placed.annotations.push_back(std::move(moved));
            if (const auto popup = popupOf.find(member.id.nm); popup != popupOf.end()) {
                Annotation pop = translated(all[popup->second], p.dx, p.dy);
                pop.page = p.page;
                out.placed.annotations.push_back(std::move(pop));
            }
        };
        land(a, true);
        for (std::size_t m = 0; m < all.size(); ++m) {
            if (m != r && rootOf[m] == r && all[m].subtype != AnnotationSubtype::Popup) land(all[m], false);
        }
        out.entries.push_back(std::move(entry));
    }
    return out;
}

} // namespace rawpage::core
