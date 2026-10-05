// tst_markupink -- Stage 6 T6, guide cases A-49 … A-53 and A-34/A-35's tool halves
// (`Stage6-Annotations-Testing-Guide.md` §5.8, §5.6): the text markup tools and the sticky note driven with
// MOUSE AND KEY EVENTS over the product's own composition, graded against the RENDERED INK (rule 6) -- the
// engine's pixels, located by this file and by nothing the tools compute.
//
//   * **A-49.** A highlight dragged across `columns-two.pdf`'s column break, across words on
//     `annots-rotated.pdf`'s page at `/Rotate` 90, and down a column of `cjk-vertical.pdf`: every selected
//     glyph's ink is COVERED -- the paper beside each of its ink pixels, across or along the line, is the
//     highlight's -- and no unselected glyph's ink more than 10 % so.
//   * **A-50.** An underline and a squiggly over a line of `text-a4.pdf` lie below the baseline and inside the
//     descender band; a strikeout's centre in the x-height's middle third -- each measured from the glyphs'
//     own ink (a run of x-height letters for the baseline and the x-height, the descenders for the band).
//   * **A-51.** A replace -- a word selected, *Replace Text*, a replacement typed -- is a strikeout and a caret
//     in one group, ONE step in the journal, one top-level item, and one `Ctrl+Z` takes both.
//   * **A-52.** An insertion clicked between two letters has its caret's ink centred within 1 pt of the
//     boundary between the two letters' ink.
//   * **A-53.** A note made where the pointer is, by the login name `app/` supplied, then by `identity/author`.
//   * **A-34, A-35 through the tools.** One of each kind T6 makes, made through its tool, saved, the file
//     reopened FRESH: 0 differing bytes at 72 and 144 dpi and turned 90°; and every one undone, 0 from the
//     page as it was.
//
// WHAT THIS DOES NOT COVER (convention 19): the pointer itself (events are sent to the viewport), a real
// 150 % monitor (M-01), and any reader but PDFium (M-02). Every document is OURS (D6-26).

#include "tests/composition/AnnotProduct.h"

#include "app/BackendRasterizer.h"
#include "core/cmd/ActionRegistry.h"
#include "core/doc/PageTree.h"
#include "core/settings/Settings.h"
#include "core/text/TextSelection.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/viewer/AnnotationTextEditor.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/ToolController.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include <cstring>
#include <iostream>
#include <set>

using namespace rawpage;
using annot_product::Product;
using annot_product::onSheet;
using core::Annotation;
using core::AnnotationSubtype;

namespace {

const core::text::PageReading& readingOf(Product& product, int page)
{
    ui::SelectionOverlay* text = product.document()->selection();
    REQUIRE(text != nullptr);
    QElapsedTimer timer;
    timer.start();
    while (!text->ensurePage(page, core::render::RenderPriority::Visible) && timer.elapsed() < 30000) product.settle();
    const core::text::PageReading* reading = text->text().page(page);
    REQUIRE(reading != nullptr);
    return *reading;
}

// A page point in the viewport's logical pixels.
QPoint widgetAt(const ui::PageView& view, int page, core::PdfPoint point)
{
    const std::optional<core::DevicePoint> d = view.layoutEngine().layout().toContent(page, point);
    REQUIRE(d);
    const core::DeviceRect visible = view.controller().visibleRect();
    const double dpr = view.devicePixelRatioF();
    return QPoint(static_cast<int>(std::lround((d->x - visible.left()) / dpr)),
                  static_cast<int>(std::lround((d->y - visible.top()) / dpr)));
}

void mouse(ui::PageView& view, QEvent::Type type, QPoint pos, int clicks = 1)
{
    (void)clicks;
    QMouseEvent event(type, QPointF(pos), view.viewport()->mapToGlobal(QPointF(pos)), Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &event);
    QCoreApplication::processEvents();
}

void key(QWidget* target, int k, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString& text = {})
{
    QKeyEvent press(QEvent::KeyPress, k, modifiers, text);
    QCoreApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, k, modifiers, text);
    QCoreApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
}

// Typed into whatever has the focus, a character at a time, through the application -- so the window's
// `ShortcutBinder` sees each key first, as it sees a person's.
void type(const QString& text)
{
    for (const QChar c : text) {
        QWidget* focus = QApplication::focusWidget();
        REQUIRE(focus != nullptr);
        key(focus, c.toUpper().unicode(), Qt::NoModifier, QString(c));
    }
}

ui::AnnotationTextEditor* editorOf(Product& product)
{
    QCoreApplication::processEvents();
    ui::AnnotationTextEditor* editor = product.view->annotationTextEditor();
    REQUIRE(editor != nullptr);
    REQUIRE(editor->hasFocus());
    return editor;
}

bool dispatch(Product& product, core::ActionId id)
{
    return static_cast<bool>(product.root->services().actions.dispatch(id, core::KeyContext::Viewer));
}

// The reading's text, position by position, for finding a phrase.
QString textOf(const core::text::PageReading& reading)
{
    QString out;
    for (int p = 0; p < reading.size(); ++p) {
        const char32_t c = reading.charAt(p).codepoint;
        out += c < 0x10000 ? QChar(static_cast<char16_t>(c)) : QChar(0xFFFD);
    }
    return out;
}

bool isVertical(const core::text::PageReading& reading, int position)
{
    return reading.layout().lines[static_cast<std::size_t>(reading.lineOf(position))].direction
           == core::text::WritingDirection::TopToBottom;
}

// Where a press puts the caret BEFORE `position`, and a release AFTER it -- inside the glyph, on the side of
// its centre the reading direction calls first or last.
core::PdfPoint before(const core::text::PageReading& reading, int position)
{
    const core::PdfRect b = reading.charAt(position).box;
    if (isVertical(reading, position)) return {b.left() + b.size.width / 2, b.top() - b.size.height * 0.15};
    return {b.left() + b.size.width * 0.15, b.bottom() + b.size.height / 2};
}

core::PdfPoint after(const core::text::PageReading& reading, int position)
{
    const core::PdfRect b = reading.charAt(position).box;
    if (isVertical(reading, position)) return {b.left() + b.size.width / 2, b.bottom() + b.size.height * 0.15};
    return {b.right() - b.size.width * 0.15, b.bottom() + b.size.height / 2};
}

// The tool's drag from the caret before `first` to the caret after `last`; the positions it selected, read
// before the release lets the selection go.
std::set<int> dragAcross(Product& product, int page, int first, int last)
{
    const core::text::PageReading& reading = readingOf(product, page);
    ui::PageView& view = *product.view;
    const QPoint from = widgetAt(view, page, before(reading, first));
    const QPoint to = widgetAt(view, page, after(reading, last));
    REQUIRE(view.viewport()->rect().contains(from));
    REQUIRE(view.viewport()->rect().contains(to));
    mouse(view, QEvent::MouseButtonPress, from);
    mouse(view, QEvent::MouseMove, (from + to) / 2);
    mouse(view, QEvent::MouseMove, to);
    product.settle();
    ui::SelectionOverlay* text = product.document()->selection();
    REQUIRE(text->isResolved());
    std::set<int> selected;
    for (const core::text::Span& span : text->selection().spansOn(reading))
        for (int p = span.begin; p < span.end; ++p) selected.insert(p);
    REQUIRE_FALSE(selected.empty());
    mouse(view, QEvent::MouseButtonRelease, to);
    product.settle();
    return selected;
}

int gray(const QImage& image, int x, int y)
{
    return qGray(image.pixel(x, y));
}

int delta(const QImage& a, const QImage& b, int x, int y)
{
    const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
    return std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)), std::abs(qBlue(p) - qBlue(q))});
}

// RULE 6'S ORACLE FOR COVERAGE. A glyph ink pixel (dark in `base`, the page drawn with no annotations) is
// COVERED when the paper on both sides of the ink it belongs to -- walking across or along the line from it
// until the page is light again -- is coloured by the mark (`after` differs from `before` there). The mark is
// a filled quadrilateral, so paper marked on both sides means the ink between is inside it.
struct Coverage {
    int inked = 0;
    int covered = 0;
    double fraction() const { return inked > 0 ? static_cast<double>(covered) / inked : 0.0; }
};

// `own` is the glyph's box on the sheet: only ink CONNECTED to a pixel inside it is the glyph's -- the region
// grown across the line also catches a neighbouring line's descender or ascender, which is not this glyph's ink
// (found by this case's first run: a `g` from the line above counted against the `e` below it).
Coverage coverageOf(const QImage& base, const QImage& before, const QImage& after, const QRect& region,
                    std::optional<QRect> own = std::nullopt)
{
    Coverage c;
    const int w = base.width(), h = base.height();
    const auto marked = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && delta(after, before, x, y) > 24; };
    const auto bracketed = [&](int x, int y, int dx, int dy) {
        const auto walk = [&](int sx, int sy) {
            int px = x, py = y;
            while (px >= 0 && py >= 0 && px < w && py < h && gray(base, px, py) < 200) {
                px += sx;
                py += sy;
            }
            return marked(px, py);
        };
        return walk(dx, dy) && walk(-dx, -dy);
    };
    const QRect inside = region.intersected(QRect(0, 0, w, h));
    // The glyph's ink: dark pixels of the region reachable (8-connected, inside the region) from its own box.
    std::vector<char> mine(static_cast<std::size_t>(inside.width()) * std::max(0, inside.height()), own ? 0 : 1);
    if (own) {
        const auto at = [&](int x, int y) -> char& {
            return mine[static_cast<std::size_t>(y - inside.top()) * inside.width() + (x - inside.left())];
        };
        std::vector<QPoint> stack;
        for (int y = inside.top(); y <= inside.bottom(); ++y)
            for (int x = inside.left(); x <= inside.right(); ++x)
                if (own->contains(x, y) && gray(base, x, y) < 128 && !at(x, y)) {
                    at(x, y) = 1;
                    stack.emplace_back(x, y);
                }
        while (!stack.empty()) {
            const QPoint p = stack.back();
            stack.pop_back();
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = p.x() + dx, y = p.y() + dy;
                    if (!inside.contains(x, y) || at(x, y) || gray(base, x, y) >= 128) continue;
                    at(x, y) = 1;
                    stack.emplace_back(x, y);
                }
        }
    }
    for (int y = inside.top(); y <= inside.bottom(); ++y) {
        for (int x = inside.left(); x <= inside.right(); ++x) {
            if (gray(base, x, y) >= 128) continue;
            if (!mine[static_cast<std::size_t>(y - inside.top()) * inside.width() + (x - inside.left())]) continue;
            ++c.inked;
            if (bracketed(x, y, 0, 1) || bracketed(x, y, 1, 0)) ++c.covered;
        }
    }
    return c;
}

QRect sheetRect(const ui::PageView& view, int page, const core::PdfRect& box)
{
    const QRectF r = onSheet(view, page, box);
    return QRect(QPoint(static_cast<int>(std::floor(r.left())), static_cast<int>(std::floor(r.top()))),
                 QPoint(static_cast<int>(std::ceil(r.right())) - 1, static_cast<int>(std::ceil(r.bottom())) - 1));
}

// The sheet region a glyph's ink is looked for in: its box, grown ACROSS its line by a quarter -- ink above
// the ascent or below the descent is still its ink, when it is connected to the ink in its box -- and not
// along it, where its neighbours' ink begins.
QRect glyphRegion(const ui::PageView& view, int page, const core::text::PageReading& reading, int position)
{
    core::PdfRect b = reading.charAt(position).box;
    if (isVertical(reading, position)) {
        const double grow = b.size.width * 0.25;
        b = core::PdfRect{core::PdfPoint{b.left() - grow, b.bottom()}, core::PdfSize{b.size.width + 2 * grow, b.size.height}};
    } else {
        const double grow = b.size.height * 0.25;
        b = core::PdfRect{core::PdfPoint{b.left(), b.bottom() - grow}, core::PdfSize{b.size.width, b.size.height + 2 * grow}};
    }
    return sheetRect(view, page, b);
}

struct Graded {
    int selected = 0;
    int unselected = 0;
    double worstSelected = 1.0;
    double worstUnselected = 0.0;
};

// The zoom every ink case is GRADED at: three device pixels a point, enough ink in a 10-point glyph to judge
// coverage pixel by pixel. A gesture is made at whatever zoom puts both its ends on screen.
constexpr double kGradeZoom = 2.0;

void zoomTo(Product& product, double zoom, int page, const core::PdfRect& around)
{
    product.view->setZoom(core::render::Zoom{zoom});
    product.settle();
    product.view->revealPageRect(page, around);
    product.settle();
}

// A-49 over one highlight made through its tool across `first` … `last` on `page`, the drag made at `zoom`.
Graded highlightAcross(Product& product, int page, int first, int last, double zoom)
{
    ui::PageView& view = *product.view;
    const core::text::PageReading& text = readingOf(product, page);
    core::PdfRect around = text.charAt(first).box;
    for (int p = first; p <= last; ++p) {
        const core::PdfRect b = text.charAt(p).box;
        const double l = std::min(around.left(), b.left()), r = std::max(around.right(), b.right());
        const double bo = std::min(around.bottom(), b.bottom()), t = std::max(around.top(), b.top());
        around = core::PdfRect{core::PdfPoint{l, bo}, core::PdfSize{r - l, t - bo}};
    }
    zoomTo(product, kGradeZoom, page, around);
    const QImage base = product.page(page, false);
    const QImage before = product.page(page, true);
    zoomTo(product, zoom, page, around);
    REQUIRE(dispatch(product, core::ActionId::AnnotHighlight));
    REQUIRE(view.tools().armedAnnotation() == ui::AnnotationToolKind::Highlight);
    const std::size_t steps = product.document()->journal()->position();
    const std::set<int> selected = dragAcross(product, page, first, last);
    CHECK(product.document()->journal()->position() == steps + 1);   // one command
    zoomTo(product, kGradeZoom, page, around);
    const QImage after = product.page(page, true);
    REQUIRE(after.size() == base.size());

    const core::text::PageReading& reading = readingOf(product, page);
    Graded g;
    for (int p = 0; p < reading.size(); ++p) {
        // A space has no ink of its own -- what its box holds is its neighbours'.
        if (QChar::isSpace(reading.charAt(p).codepoint)) continue;
        const Coverage c = coverageOf(base, before, after, glyphRegion(view, page, reading, p),
                                      sheetRect(view, page, reading.charAt(p).box));
        if (c.inked == 0) continue;
        if (selected.count(p) != 0) {
            ++g.selected;
            if (c.fraction() < g.worstSelected) g.worstSelected = c.fraction();
            INFO("selected glyph " << p << " U+" << std::hex << static_cast<unsigned>(reading.charAt(p).codepoint)
                                   << std::dec << " covered " << c.covered << " of " << c.inked);
            CHECK(c.fraction() >= 0.95);
        } else {
            ++g.unselected;
            if (c.fraction() > g.worstUnselected) g.worstUnselected = c.fraction();
            INFO("unselected glyph " << p << " U+" << std::hex << static_cast<unsigned>(reading.charAt(p).codepoint)
                                     << std::dec << " covered " << c.covered << " of " << c.inked);
            CHECK(c.fraction() <= 0.10);
        }
    }
    CHECK(g.selected > 0);
    return g;
}

int positionOf(const core::text::PageReading& reading, const QString& phrase)
{
    const int at = static_cast<int>(textOf(reading).indexOf(phrase));
    INFO("phrase " << phrase.toStdString());
    REQUIRE(at >= 0);
    return at;
}

} // namespace

TEST_CASE("a highlight made with its tool covers the selected glyphs' ink and no other's", "[markupink][A-49]")
{
    // COLUMNS-TWO: from the first character of the first column's LAST line to the fifth of the second column's
    // FIRST line -- a drag across the column break, in reading order.
    {
        Product product;
        ui::PageView* view = product.open("ordinary/columns-two.pdf");
        // Graded at `kGradeZoom`; the drag at 0.6, so the foot of one column and the head of the next -- a page
        // apart -- are both on screen.
        product.settle();
        const core::text::PageReading& reading = readingOf(product, 0);
        const core::PdfRect crop = product.root->services().backend.pages(view->document())->at(0)->cropBox();
        const double middle = crop.left() + crop.size.width / 2.0;
        // THE BREAK IN READING ORDER: the first character read in the right half after one read in the left,
        // and higher up the page -- the top of the second column following the foot of the first.
        std::optional<int> breakAt;
        for (int p = 1; p < reading.size() && !breakAt; ++p) {
            const core::PdfRect a = reading.charAt(p - 1).box;
            const core::PdfRect b = reading.charAt(p).box;
            if (a.right() < middle && b.left() > middle && b.bottom() > a.top()) breakAt = p;
        }
        REQUIRE(breakAt);
        const int first = reading.lineSpan(reading.lineOf(*breakAt - 1)).begin;
        const int last = *breakAt + 4;
        const Graded g = highlightAcross(product, 0, first, last, 0.6);
        std::cout << "A-49 columns-two: " << g.selected << " selected glyphs, the worst covered " << g.worstSelected
                  << "; " << g.unselected << " unselected, the most covered " << g.worstUnselected << "\n";
    }
    // ANNOTS-ROTATED page 2, at `/Rotate` 90: "Marked words" -- left of the existing highlight.
    {
        Product product;
        ui::PageView* view = product.open("ordinary/annots-rotated.pdf");
        view->setZoom(core::render::Zoom{1.0});
        view->goToPage(1);
        product.settle();
        const core::text::PageReading& reading = readingOf(product, 1);
        const int first = positionOf(reading, QStringLiteral("Marked words"));
        const Graded g = highlightAcross(product, 1, first, first + 11, 1.0);
        std::cout << "A-49 annots-rotated p2: " << g.selected << " selected glyphs, the worst covered " << g.worstSelected
                  << "; " << g.unselected << " unselected, the most covered " << g.worstUnselected << "\n";
    }
    // CJK-VERTICAL: four characters down the first vertical column.
    {
        Product product;
        ui::PageView* view = product.open("ordinary/cjk-vertical.pdf");
        view->setZoom(core::render::Zoom{1.0});
        product.settle();
        const core::text::PageReading& reading = readingOf(product, 0);
        std::optional<int> start;
        for (int p = 0; p + 3 < reading.size() && !start; ++p) {
            if (isVertical(reading, p) && reading.lineOf(p) == reading.lineOf(p + 3)) start = p;
        }
        REQUIRE(start);
        const Graded g = highlightAcross(product, 0, *start, *start + 3, 1.0);
        std::cout << "A-49 cjk-vertical: " << g.selected << " selected glyphs, the worst covered " << g.worstSelected
                  << "; " << g.unselected << " unselected, the most covered " << g.worstUnselected << "\n";
    }
}

namespace {

// The glyph's own ink on the sheet (connected to its box): its rows and columns, or nothing for no ink.
struct InkExtent {
    int top = 0, bottom = -1, left = 0, right = -1;
    bool any() const { return bottom >= top; }
};

InkExtent inkOf(const QImage& base, const QRect& own)
{
    InkExtent e;
    bool first = true;
    const QRect region = own.adjusted(0, -own.height() / 3, 0, own.height() / 3).intersected(base.rect());
    std::vector<char> mine(static_cast<std::size_t>(region.width()) * std::max(0, region.height()), 0);
    const auto at = [&](int x, int y) -> char& {
        return mine[static_cast<std::size_t>(y - region.top()) * region.width() + (x - region.left())];
    };
    std::vector<QPoint> stack;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
            if (own.contains(x, y) && gray(base, x, y) < 128) {
                at(x, y) = 1;
                stack.emplace_back(x, y);
            }
    while (!stack.empty()) {
        const QPoint p = stack.back();
        stack.pop_back();
        if (first) {
            e = InkExtent{p.y(), p.y(), p.x(), p.x()};
            first = false;
        }
        e.top = std::min(e.top, p.y());
        e.bottom = std::max(e.bottom, p.y());
        e.left = std::min(e.left, p.x());
        e.right = std::max(e.right, p.x());
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = p.x() + dx, y = p.y() + dy;
                if (!region.contains(x, y) || at(x, y) || gray(base, x, y) >= 128) continue;
                at(x, y) = 1;
                stack.emplace_back(x, y);
            }
    }
    return e;
}

double median(std::vector<int> values)
{
    REQUIRE_FALSE(values.empty());
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

bool isOneOf(char32_t c, const char* letters)
{
    return c < 128 && c > 0 && std::strchr(letters, static_cast<char>(c)) != nullptr;
}

// A line of `reading` whose text holds x-height letters and a descender, and that no earlier choice took --
// the oracle needs both to know where the baseline and the descender band are.
std::optional<int> lineWithDescenders(const core::text::PageReading& reading, const std::set<int>& taken)
{
    for (std::size_t line = 0; line < reading.layout().lines.size(); ++line) {
        if (taken.count(static_cast<int>(line)) != 0) continue;
        const core::text::Span span = reading.lineSpan(static_cast<int>(line));
        if (span.end - span.begin < 20) continue;
        bool descender = false, xheight = false;
        for (int p = span.begin; p < span.end; ++p) {
            descender = descender || isOneOf(reading.charAt(p).codepoint, "gpqy");
            xheight = xheight || isOneOf(reading.charAt(p).codepoint, "acemnorsuvwxz");
        }
        if (descender && xheight) return static_cast<int>(line);
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("underline and squiggly sit in the descender band and strikeout through the x-height", "[markupink][A-50]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    product.settle();
    std::set<int> taken;
    struct Kind {
        core::ActionId action;
        AnnotationSubtype subtype;
        const char* name;
    };
    for (const Kind kind : {Kind{core::ActionId::AnnotUnderline, AnnotationSubtype::Underline, "underline"},
                            Kind{core::ActionId::AnnotStrikeout, AnnotationSubtype::StrikeOut, "strikeout"},
                            Kind{core::ActionId::AnnotSquiggly, AnnotationSubtype::Squiggly, "squiggly"}}) {
        INFO(kind.name);
        const core::text::PageReading& reading = readingOf(product, 0);
        const std::optional<int> line = lineWithDescenders(reading, taken);
        REQUIRE(line);
        taken.insert(*line);
        const core::text::Span span = reading.lineSpan(*line);
        const core::PdfRect lineBox = reading.layout().lines[static_cast<std::size_t>(*line)].box;
        zoomTo(product, kGradeZoom, 0, lineBox);
        const QImage base = product.page(0, false);
        const QImage before = product.page(0, true);

        // THE GLYPHS' OWN GEOMETRY, from their ink: the baseline and the x-height from the x-height letters,
        // the descender band's foot from the descenders.
        std::vector<int> bottoms, tops, feet;
        for (int p = span.begin; p < span.end; ++p) {
            const char32_t c = reading.charAt(p).codepoint;
            const bool xheight = isOneOf(c, "acemnorsuvwxz");
            const bool descender = isOneOf(c, "gpqy");
            if (!xheight && !descender) continue;
            const InkExtent ink = inkOf(base, sheetRect(*view, 0, reading.charAt(p).box));
            if (!ink.any()) continue;
            if (xheight) {
                bottoms.push_back(ink.bottom);
                tops.push_back(ink.top);
            }
            if (descender) feet.push_back(ink.bottom);
        }
        const double baseline = median(bottoms);   // the last ink row of an x-height letter
        const double xTop = median(tops);
        const double foot = median(feet);
        REQUIRE(foot > baseline + 2);

        // Made with the tool, across the whole line.
        zoomTo(product, 1.0, 0, lineBox);
        REQUIRE(dispatch(product, kind.action));
        const std::size_t steps = product.document()->journal()->position();
        (void)dragAcross(product, 0, span.begin, span.end - 1);
        REQUIRE(product.document()->journal()->position() == steps + 1);
        zoomTo(product, kGradeZoom, 0, lineBox);
        const QImage after = product.page(0, true);

        // ITS INK: what changed, within the line's sheet rectangle grown by a line above and below.
        const QRect band = sheetRect(*view, 0, lineBox);
        const QRect search = band.adjusted(0, -band.height(), 0, band.height()).intersected(after.rect());
        int topRow = 1 << 30, bottomRow = -1;
        double weighted = 0.0, weight = 0.0;
        for (int y = search.top(); y <= search.bottom(); ++y)
            for (int x = search.left(); x <= search.right(); ++x) {
                const int d = delta(after, before, x, y);
                if (d <= 24) continue;
                topRow = std::min(topRow, y);
                bottomRow = std::max(bottomRow, y);
                weighted += static_cast<double>(y) * d;
                weight += d;
            }
        REQUIRE(bottomRow >= topRow);
        const double centre = weighted / weight;
        std::cout << "A-50 " << kind.name << ": baseline row " << baseline << ", x-height top " << xTop
                  << ", descender foot " << foot << "; the mark's rows " << topRow << "-" << bottomRow
                  << ", centred at " << centre << " (" << view->layoutEngine().layout().presentedScale() << " px/pt)\n";
        if (kind.subtype == AnnotationSubtype::StrikeOut) {
            const double third = (baseline - xTop) / 3.0;
            CHECK(centre >= xTop + third);
            CHECK(centre <= baseline - third);
        } else {
            // BELOW THE BASELINE AND INSIDE THE DESCENDER BAND: every row of the mark's ink. One row of
            // anti-aliasing is allowed at either edge -- a stroke whose edge falls inside a pixel tints it.
            CHECK(topRow >= baseline);
            CHECK(bottomRow <= foot + 1);
        }
    }
}

TEST_CASE("an insertion mark sits on the boundary between the two letters clicked", "[markupink][A-52]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    product.settle();
    const core::text::PageReading& reading = readingOf(product, 0);
    // Two letters of one word, mid-line: the first "ea" the page reads.
    const int k = positionOf(reading, QStringLiteral("ea"));
    REQUIRE(reading.lineOf(k) == reading.lineOf(k + 1));
    const core::PdfRect left = reading.charAt(k).box;
    const core::PdfRect right = reading.charAt(k + 1).box;
    const core::PdfRect around{core::PdfPoint{left.left() - 20, left.bottom() - 10}, core::PdfSize{60, 30}};
    zoomTo(product, kGradeZoom, 0, around);
    const QImage base = product.page(0, false);
    const QImage before = product.page(0, true);
    const InkExtent a = inkOf(base, sheetRect(*view, 0, left));
    const InkExtent b = inkOf(base, sheetRect(*view, 0, right));
    REQUIRE(a.any());
    REQUIRE(b.any());
    const double boundary = (a.right + b.left) / 2.0;

    // The click between them -- where the two boxes meet -- with the tool, and the text typed.
    REQUIRE(dispatch(product, core::ActionId::AnnotInsertText));
    REQUIRE(view->tools().armedAnnotation() == ui::AnnotationToolKind::InsertText);
    const QPoint at =
        widgetAt(*view, 0, core::PdfPoint{(left.right() + right.left()) / 2.0, left.bottom() + left.size.height / 2});
    mouse(*view, QEvent::MouseButtonPress, at);
    mouse(*view, QEvent::MouseButtonRelease, at);
    (void)editorOf(product);
    type(QStringLiteral("inserted"));
    key(QApplication::focusWidget(), Qt::Key_Return);
    product.settle();

    std::optional<Annotation> caret;
    for (const Annotation& x : product.store().all())
        if (x.subtype == AnnotationSubtype::Caret && x.id.kind == core::AnnotationId::Kind::Created) caret = x;
    REQUIRE(caret);
    CHECK(caret->contents == QStringLiteral("inserted"));
    CHECK(caret->author == QString::fromLatin1(annot_product::kLoginName));

    // ITS INK'S CENTRE, along the line, against the boundary between the two letters' ink.
    const QImage after = product.page(0, true);
    const QRect search = sheetRect(*view, 0, around).intersected(after.rect());
    double weighted = 0.0, weight = 0.0;
    for (int y = search.top(); y <= search.bottom(); ++y)
        for (int x = search.left(); x <= search.right(); ++x) {
            const int d = delta(after, before, x, y);
            if (d <= 24) continue;
            weighted += static_cast<double>(x) * d;
            weight += d;
        }
    REQUIRE(weight > 0.0);
    const double centre = weighted / weight;
    const double scale = view->layoutEngine().layout().presentedScale();
    std::cout << "A-52: boundary at sheet x " << boundary << ", the caret's ink centred at " << centre << " -- "
              << std::abs(centre - boundary) / scale << " pt apart\n";
    CHECK(std::abs(centre - boundary) <= scale);   // within 1 pt
}

TEST_CASE("a replace is a strikeout and a caret in one group and one step", "[markupink][A-51]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    product.settle();
    const core::text::PageReading& reading = readingOf(product, 0);
    // A WORD selected with the SELECTION tool, then *Replace Text* -- the command acts on what is selected.
    const int first = positionOf(reading, QStringLiteral("ea"));
    int begin = first, end = first;
    while (begin > 0 && reading.charAt(begin - 1).codepoint > U' ' && reading.lineOf(begin - 1) == reading.lineOf(first))
        --begin;
    while (end + 1 < reading.size() && reading.charAt(end + 1).codepoint > U' '
           && reading.lineOf(end + 1) == reading.lineOf(first))
        ++end;
    zoomTo(product, 1.0, 0, reading.charAt(first).box);
    REQUIRE(view->tools().armed() == ui::ToolKind::Select);
    (void)dragAcross(product, 0, begin, end);
    REQUIRE(product.document()->selection()->hasSelection());

    core::CommandJournal& journal = *product.document()->journal();
    const std::size_t steps = journal.position();
    const std::size_t count = product.store().size();
    REQUIRE(dispatch(product, core::ActionId::AnnotReplaceText));
    (void)editorOf(product);
    CHECK(journal.position() == steps);   // nothing made until the replacement is typed
    type(QStringLiteral("replacement"));
    key(QApplication::focusWidget(), Qt::Key_Return);
    product.settle();

    // ONE STEP, TWO ANNOTATIONS, ONE ITEM.
    CHECK(journal.position() == steps + 1);
    REQUIRE(product.store().size() == count + 2);
    std::optional<Annotation> caret, strike;
    for (const Annotation& x : product.store().all()) {
        if (x.id.kind != core::AnnotationId::Kind::Created) continue;
        if (x.subtype == AnnotationSubtype::Caret) caret = x;
        if (x.subtype == AnnotationSubtype::StrikeOut) strike = x;
    }
    REQUIRE(caret);
    REQUIRE(strike);
    CHECK(caret->contents == QStringLiteral("replacement"));
    CHECK(strike->inReplyTo == caret->id);
    CHECK(strike->replyType == core::ReplyType::Group);
    const std::size_t caretIndex = *product.store().find(caret->id);
    const std::size_t strikeIndex = *product.store().find(strike->id);
    CHECK(product.store().isTopLevel(caretIndex));
    CHECK_FALSE(product.store().isTopLevel(strikeIndex));
    CHECK(product.store().groupMembers(caretIndex) == std::vector<std::size_t>{strikeIndex});
    CHECK_FALSE(product.document()->selection()->hasSelection());

    // ONE UNDO TAKES BOTH.
    REQUIRE(dispatch(product, core::ActionId::EditUndo));
    product.settle();
    CHECK(journal.position() == steps);
    CHECK(product.store().size() == count);
}

TEST_CASE("a note is made where the pointer is and by whoever is set", "[markupink][A-53]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    const auto makeNote = [&](core::PdfPoint where, const QString& text, bool commit) -> std::optional<Annotation> {
        REQUIRE(dispatch(product, core::ActionId::AnnotStickyNote));
        REQUIRE(view->tools().armedAnnotation() == ui::AnnotationToolKind::StickyNote);
        const std::size_t count = product.store().size();
        view->revealPageRect(0, core::PdfRect{core::PdfPoint{where.x - 20, where.y - 40}, core::PdfSize{60, 60}});
        product.settle();
        const QPoint at = widgetAt(*view, 0, where);
        REQUIRE(view->viewport()->rect().contains(at));
        mouse(*view, QEvent::MouseButtonPress, at);
        mouse(*view, QEvent::MouseButtonRelease, at);
        (void)editorOf(product);
        type(text);
        // THE EDITOR'S KEYS ARE ITS OWN: a `k` typed is a letter, not Annotation tools' keep-the-tool.
        CHECK_FALSE(view->tools().keepToolSelected());
        key(QApplication::focusWidget(), commit ? Qt::Key_Return : Qt::Key_Escape);
        product.settle();
        if (!commit) {
            CHECK(product.store().size() == count);
            return std::nullopt;
        }
        REQUIRE(product.store().size() == count + 2);
        std::optional<Annotation> note;
        for (const Annotation& x : product.store().all())
            if (x.subtype == AnnotationSubtype::Text && x.contents == text) note = x;
        REQUIRE(note);
        // Its pop-up, and the two name each other.
        REQUIRE(note->popup);
        const std::optional<std::size_t> popup = product.store().find(*note->popup);
        REQUIRE(popup);
        CHECK(product.store().at(*popup).subtype == AnnotationSubtype::Popup);
        CHECK(product.store().at(*popup).parent == note->id);
        // AT THE POINTER: the icon's top-left corner, within a pixel of where the click was.
        const double scale = view->layoutEngine().layout().presentedScale();
        CHECK(std::abs(note->rect.left() - where.x) <= 1.0 / scale + 1e-6);
        CHECK(std::abs(note->rect.top() - where.y) <= 1.0 / scale + 1e-6);
        // `K` is off, so the tool went back to Selection once it made something.
        CHECK(view->tools().armed() == ui::ToolKind::Select);
        return note;
    };

    const std::optional<Annotation> first = makeNote(core::PdfPoint{300.0, 600.0}, QStringLiteral("keep this"), true);
    REQUIRE(first);
    CHECK(first->author == QString::fromLatin1(annot_product::kLoginName));   // identity/author empty: the login

    product.root->settings().set(core::keys::identity::author, QStringLiteral("Ada Reviewer"));
    const std::optional<Annotation> second = makeNote(core::PdfPoint{350.0, 560.0}, QStringLiteral("second"), true);
    REQUIRE(second);
    CHECK(second->author == QStringLiteral("Ada Reviewer"));

    // `Esc` in the editor makes nothing.
    (void)makeNote(core::PdfPoint{400.0, 520.0}, QStringLiteral("abandoned"), false);
}

namespace {

// The page as the product's engine draws it NOW -- the live document after every reload -- or as a fresh
// engine draws a saved file: one request, as `tst_annotdisplay` builds it, at a scale and a view rotation.
core::RasterImage rasterOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page, double scale,
                           core::PageRotation view)
{
    const core::PageTree* tree = backend.pages(handle);
    REQUIRE(tree != nullptr);
    const core::PdfSize size = tree->at(page)->displaySize();
    const bool swap = core::swapsAxes(view);
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();   // a label for the cache, which a direct raster does not use
    request.viewRotation = view;
    request.flags = core::render::RenderFlags::Annotations;
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil((swap ? size.height : size.width) * scale)),
                                          static_cast<int>(std::ceil((swap ? size.width : size.height) * scale))};
    core::IPageRasterizer* rasterizer = backend.rasterizer(handle);
    REQUIRE(rasterizer != nullptr);
    auto image = rasterizer->raster(request);
    INFO("raster: " << (image ? std::string() : std::string(image.error().messageKey.data()) + " "
                                                     + image.error().context.toStdString()));
    REQUIRE(image);
    return image.value();
}

long long differingBytes(const core::RasterImage& a, const core::RasterImage& b)
{
    if (a.width != b.width || a.height != b.height) return std::max(a.pixels.size(), b.pixels.size());
    long long n = 0;
    for (qsizetype i = 0; i < a.pixels.size(); ++i)
        if (a.pixels[i] != b.pixels[i]) ++n;
    return n;
}

struct ShownView {
    double scale;
    core::PageRotation rotation;
    const char* name;
};
// Guide §3.2's three: 72 dpi, 144 dpi, and a 90-degree view rotation.
const ShownView kShownViews[] = {{1.0, core::PageRotation::None, "72 dpi"},
                                 {2.0, core::PageRotation::None, "144 dpi"},
                                 {1.0, core::PageRotation::Clockwise90, "72 dpi turned 90"}};

} // namespace

TEST_CASE("every kind T6 makes through its tool is shown as it is saved and undone to the page it was",
          "[markupink][A-34][A-35]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    core::IDocumentBackend& backend = product.root->services().backend;
    const core::DocumentHandle handle = product.document()->handle();
    core::CommandJournal& journal = *product.document()->journal();
    product.root->services().renderScheduler.waitForIdle(4000);
    std::vector<core::RasterImage> original;
    for (const ShownView& v : kShownViews) original.push_back(rasterOf(backend, handle, 0, v.scale, v.rotation));

    const core::text::PageReading& reading = readingOf(product, 0);
    std::vector<int> lines;
    for (std::size_t line = 0; line < reading.layout().lines.size() && lines.size() < 6; ++line) {
        const core::text::Span span = reading.lineSpan(static_cast<int>(line));
        if (span.end - span.begin >= 24) lines.push_back(static_cast<int>(line));
    }
    REQUIRE(lines.size() == 6);
    const auto lineBox = [&](int line) { return reading.layout().lines[static_cast<std::size_t>(line)].box; };
    const auto show = [&](int line) {
        view->revealPageRect(0, lineBox(line));
        product.settle();
    };

    // THE FOUR MARKUP KINDS, each dragged across the first words of its own line.
    const core::ActionId markup[] = {core::ActionId::AnnotHighlight, core::ActionId::AnnotUnderline,
                                     core::ActionId::AnnotStrikeout, core::ActionId::AnnotSquiggly};
    for (std::size_t i = 0; i < 4; ++i) {
        show(lines[i]);
        REQUIRE(dispatch(product, markup[i]));
        const int begin = readingOf(product, 0).lineSpan(lines[i]).begin;
        (void)dragAcross(product, 0, begin, begin + 15);
    }
    // A REPLACE, dragged with its tool across a word, the replacement typed.
    show(lines[4]);
    REQUIRE(dispatch(product, core::ActionId::AnnotReplaceText));
    {
        const int begin = readingOf(product, 0).lineSpan(lines[4]).begin;
        (void)dragAcross(product, 0, begin, begin + 3);
        (void)editorOf(product);
        type(QStringLiteral("new words"));
        key(QApplication::focusWidget(), Qt::Key_Return);
        product.settle();
    }
    // AN INSERTION, clicked between two letters of the sixth line.
    show(lines[5]);
    REQUIRE(dispatch(product, core::ActionId::AnnotInsertText));
    {
        const core::text::PageReading& r = readingOf(product, 0);
        const int p = r.lineSpan(lines[5]).begin + 2;
        const core::PdfRect b = r.charAt(p).box;
        const QPoint at = widgetAt(*view, 0, core::PdfPoint{b.right(), b.bottom() + b.size.height / 2});
        mouse(*view, QEvent::MouseButtonPress, at);
        mouse(*view, QEvent::MouseButtonRelease, at);
        (void)editorOf(product);
        type(QStringLiteral("inserted"));
        key(QApplication::focusWidget(), Qt::Key_Return);
        product.settle();
    }
    // A NOTE, in the margin.
    REQUIRE(dispatch(product, core::ActionId::AnnotStickyNote));
    {
        const core::PdfPoint where{40.0, lineBox(lines[5]).top()};
        view->revealPageRect(0, core::PdfRect{core::PdfPoint{0, where.y - 40}, core::PdfSize{120, 60}});
        product.settle();
        const QPoint at = widgetAt(*view, 0, where);
        mouse(*view, QEvent::MouseButtonPress, at);
        mouse(*view, QEvent::MouseButtonRelease, at);
        (void)editorOf(product);
        type(QStringLiteral("a note made with the tool"));
        key(QApplication::focusWidget(), Qt::Key_Return);
        product.settle();
    }
    REQUIRE(journal.position() == 7);   // seven gestures, seven commands
    std::set<AnnotationSubtype> made;
    for (const Annotation& a : product.store().all())
        if (a.id.kind == core::AnnotationId::Kind::Created) made.insert(a.subtype);
    CHECK(made == std::set<AnnotationSubtype>{AnnotationSubtype::Highlight, AnnotationSubtype::Underline,
                                              AnnotationSubtype::StrikeOut, AnnotationSubtype::Squiggly,
                                              AnnotationSubtype::Caret, AnnotationSubtype::Text, AnnotationSubtype::Popup});

    // SAVED, and the file reopened FRESH: the live page and the file's, 0 bytes apart in every view.
    REQUIRE(dispatch(product, core::ActionId::FileSave));
    product.settle();
    REQUIRE_FALSE(journal.isDirty());
    product.root->services().renderScheduler.waitForIdle(4000);
    {
        pdf::DocumentBackendImpl fresh;
        auto reopened = fresh.open(product.openedPath, nullptr);
        REQUIRE(reopened);
        long long total = 0;
        for (std::size_t i = 0; i < std::size(kShownViews); ++i) {
            INFO(kShownViews[i].name);
            const core::RasterImage live = rasterOf(backend, handle, 0, kShownViews[i].scale, kShownViews[i].rotation);
            const core::RasterImage file = rasterOf(fresh, reopened.value(), 0, kShownViews[i].scale, kShownViews[i].rotation);
            const long long differing = differingBytes(live, file);
            CHECK(differing == 0);
            CHECK(differingBytes(live, original[i]) > 0);   // rule 1: the edits are in the pixels
            total += differing;
        }
        std::cout << "A-34 through the tools: seven kinds saved, live against the file reopened fresh: " << total
                  << " differing bytes over three views\n";
        CHECK(fresh.close(reopened.value()));
    }

    // EVERY ONE UNDONE -- after the save, so each undo writes the page back -- and the page is the original.
    for (int step = 0; step < 7; ++step) REQUIRE(dispatch(product, core::ActionId::EditUndo));
    product.settle();
    REQUIRE(journal.position() == 0);
    product.root->services().renderScheduler.waitForIdle(4000);
    long long undone = 0;
    for (std::size_t i = 0; i < std::size(kShownViews); ++i) {
        INFO(kShownViews[i].name);
        const long long differing =
            differingBytes(rasterOf(backend, handle, 0, kShownViews[i].scale, kShownViews[i].rotation), original[i]);
        CHECK(differing == 0);
        undone += differing;
    }
    std::cout << "A-35 through the tools: all seven undone after the save, " << undone << " bytes from the original\n";
}
