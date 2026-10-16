// ui/viewer/tools/RedactTool -- implementation. Stage 6 T14. See the header.

#include "ui/viewer/tools/RedactTool.h"

#include "core/text/TextSelection.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/SelectionOverlay.h"

#include <QGuiApplication>
#include <QPainter>
#include <QStyleHints>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

int clickSlop()
{
    const QStyleHints* hints = QGuiApplication::styleHints();
    return hints != nullptr ? hints->startDragDistance() : 4;
}

core::text::Granularity granularityFor(int clicks)
{
    switch (std::clamp(clicks, 1, 4)) {
    case 2: return core::text::Granularity::Word;
    case 3: return core::text::Granularity::Line;
    case 4: return core::text::Granularity::Block;
    default: return core::text::Granularity::Character;
    }
}

// Whether `point` on `page` lies on a glyph of the page's reading -- inside the nearest character's box, grown a point.
bool onText(const SelectionOverlay& selection, int page, core::PdfPoint point)
{
    const core::text::PageReading* reading = selection.text().page(page);
    if (reading == nullptr || reading->isEmpty()) return false;
    const int position = reading->positionAt(point);
    if (position < 0) return false;
    const core::PdfRect& box = reading->charAt(position).box;
    constexpr double kSlack = 1.0;
    return point.x >= box.left() - kSlack && point.x <= box.right() + kSlack && point.y >= box.bottom() - kSlack
           && point.y <= box.top() + kSlack;
}

} // namespace

core::RedactionStyle RedactTool::styleFor(const ToolHost& host)
{
    const core::ToolDefaults defaults = host.toolDefaults(AnnotationToolKind::RedactMark);
    core::RedactionStyle style;
    style.outline = defaults.colour;
    style.fill = defaults.fillColour;
    style.opacity = defaults.opacity;
    style.overlayText = defaults.overlayText;
    style.repeat = defaults.repeat;
    style.textSize = defaults.fontSize;
    return style;
}

void RedactTool::pressWith(ToolHost& host, QPoint pos, const PressContext& context)
{
    pressedAt_ = pos;
    at_ = pos;
    const PagePoint at = host.pagePointAt(pos);
    if (!at.isValid()) return;
    page_ = at.page;
    SelectionOverlay* selection = host.selection();
    if (selection != nullptr && (context.clicks > 1 || onText(*selection, at.page, at.point))) {
        mode_ = Mode::Text;
        SelectionOverlay::PressOptions options;
        options.granularity = granularityFor(context.clicks);
        selection->press(at.page, at.point, options);
        return;
    }
    mode_ = Mode::Area;
}

void RedactTool::move(ToolHost& host, QPoint pos)
{
    if (mode_ == Mode::None) return;
    at_ = pos;
    if (mode_ == Mode::Area) {
        host.requestRepaint();
        return;
    }
    SelectionOverlay* selection = host.selection();
    if (selection == nullptr || !selection->isGestureActive()) return;
    const PagePoint at = host.pagePointAt(pos);
    if (!at.isValid()) return;
    selection->drag(at.page, at.point);
}

void RedactTool::release(ToolHost& host, QPoint pos)
{
    if (mode_ == Mode::None) return;
    move(host, pos);
    const Mode mode = mode_;
    mode_ = Mode::None;
    AnnotationOverlay* annotations = host.annotationOverlay();
    if (annotations == nullptr) return;
    const core::RedactionStyle style = styleFor(host);

    if (mode == Mode::Area) {
        host.requestRepaint();
        if ((pos - pressedAt_).manhattanLength() < clickSlop()) return;   // a click marks nothing
        const PagePoint from = host.pagePointOn(page_, pressedAt_);
        const PagePoint to = host.pagePointOn(page_, pos);
        if (!from.isValid() || !to.isValid()) return;
        const std::optional<core::PdfRect> area =
            core::normalizedRect(from.point.x, from.point.y, to.point.x, to.point.y);
        if (!area) return;
        if (annotations->markArea(page_, *area, style)) host.annotationToolUsed();
        return;
    }
    SelectionOverlay* selection = host.selection();
    if (selection == nullptr || !selection->isGestureActive()) return;
    selection->release();
    if (selection->isResolved() && !selection->hasSelection()) return;
    const AnnotationOverlay::MarkOutcome made = annotations->markRedaction(*selection, style);
    if (made == AnnotationOverlay::MarkOutcome::Made || made == AnnotationOverlay::MarkOutcome::Pending)
        host.annotationToolUsed();
}

void RedactTool::cancel(ToolHost& host)
{
    if (mode_ == Mode::Text) {
        if (SelectionOverlay* selection = host.selection()) selection->cancelGesture();
    }
    mode_ = Mode::None;
    host.requestRepaint();
}

void RedactTool::paint(QPainter& painter, const ToolHost& host) const
{
    (void)host;
    // The AREA being dragged, as the mark will outline it: red, solid -- a preview, never the annotation.
    if (mode_ != Mode::Area || (at_ - pressedAt_).manhattanLength() < clickSlop()) return;
    painter.save();
    painter.setPen(QPen(QColor(0xd0, 0x20, 0x20), 1.0, Qt::SolidLine));
    painter.setBrush(QColor(0xd0, 0x20, 0x20, 0x20));
    painter.drawRect(QRect(pressedAt_, at_).normalized());
    painter.restore();
}

} // namespace rawpage::ui
