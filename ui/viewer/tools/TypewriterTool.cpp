// ui/viewer/tools/TypewriterTool -- implementation. Stage 6 T7. See the header for the three gestures.

#include "ui/viewer/tools/TypewriterTool.h"

#include "core/doc/TypedText.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/PropertiesBar.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPen>
#include <QStyleHints>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

int dragSlop()
{
    const QStyleHints* hints = QGuiApplication::styleHints();
    return hints != nullptr ? hints->startDragDistance() : 4;
}

core::FreeTextKind freeTextKindOf(AnnotationToolKind kind)
{
    switch (kind) {
    case AnnotationToolKind::Callout: return core::FreeTextKind::Callout;
    case AnnotationToolKind::Typewriter: return core::FreeTextKind::Typewriter;
    default: break;
    }
    return core::FreeTextKind::Box;
}

} // namespace

bool TypewriterTool::handles(AnnotationToolKind kind) noexcept
{
    return kind == AnnotationToolKind::TextBox || kind == AnnotationToolKind::Callout
           || kind == AnnotationToolKind::Typewriter;
}

bool TypewriterTool::dragged(QPoint pos) const
{
    return (pos - pressedAt_).manhattanLength() >= dragSlop();
}

void TypewriterTool::press(ToolHost& host, QPoint pos)
{
    pressedAt_ = pos;
    at_ = pos;
    dragging_ = true;
    host.requestRepaint();
}

void TypewriterTool::move(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    at_ = pos;
    host.requestRepaint();
}

void TypewriterTool::release(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    dragging_ = false;
    at_ = pos;
    host.requestRepaint();

    AnnotationOverlay* annotations = host.annotationOverlay();
    const PagePoint pressed = host.pagePointAt(pressedAt_);
    if (annotations == nullptr || !pressed.isValid()) return;
    const int page = pressed.page;
    const core::FreeTextKind kind = freeTextKindOf(which_);
    const AnnotationToolDefaults& defaults = sessionToolDefaults(which_);
    AnnotationOverlay::TypedStyle style;
    style.colour = defaults.colour;
    style.opacity = defaults.opacity;
    style.borderWidth = kind == core::FreeTextKind::Typewriter ? 0.0 : defaults.borderWidth;
    style.fontSize = defaults.fontSize;
    style.han = host.hanPreference();

    const double ppp = host.pixelsPerPoint() > 0.0 ? host.pixelsPerPoint() : 1.0;
    const double inset = style.borderWidth + core::kFreeTextPadding;
    const int linePx = static_cast<int>(std::ceil(style.fontSize * core::kTypedLeading * ppp + 2.0 * inset * ppp));

    // WHERE THE TEXT GOES, on screen: its box's top-left corner and width -- and, for a callout, what it points at.
    QPoint topLeft = pressedAt_;
    std::optional<double> widthPoints;
    std::optional<core::PdfPoint> target;
    QRect box;
    switch (kind) {
    case core::FreeTextKind::Box:
        if (dragged(pos)) {
            const QRect band = QRect(pressedAt_, pos).normalized();
            topLeft = band.topLeft();
            widthPoints = std::max(band.width() / ppp, style.fontSize + 2.0 * inset);
            box = QRect(topLeft, QSize(static_cast<int>(std::lround(*widthPoints * ppp)), std::max(band.height(), linePx)));
        } else {
            widthPoints = AnnotationOverlay::kFreeTextWidthPoints;
            box = QRect(topLeft, QSize(static_cast<int>(std::lround(*widthPoints * ppp)), linePx * 3));
        }
        break;
    case core::FreeTextKind::Callout:
        target = pressed.point;
        topLeft = dragged(pos) ? pos : pressedAt_ + QPoint(kCalloutOffsetX, -kCalloutOffsetY);
        widthPoints = kCalloutWidthPoints;
        box = QRect(topLeft, QSize(static_cast<int>(std::lround(*widthPoints * ppp)), linePx * 3));
        break;
    case core::FreeTextKind::Typewriter: {
        const int room = std::max(host.viewportSize().width() - topLeft.x() - 8, linePx * 4);
        box = QRect(topLeft, QSize(room, linePx * 3));
        break;
    }
    }
    const PagePoint corner = host.pagePointOn(page, topLeft);
    if (!corner.isValid()) return;

    AnnotationTextRequest request;
    request.box = box;
    request.fontPixels = style.fontSize * ppp;
    request.wrap = kind != core::FreeTextKind::Typewriter;
    request.typed = true;
    const int rotate = host.displayedRotation(page);
    ToolHost* const where = &host;
    const core::PdfPoint topLeftPoint = corner.point;
    (void)host.requestAnnotationText(request, [=](const QString& text) {
        if (annotations->addFreeText(page, kind, topLeftPoint, widthPoints, target, rotate, text, style))
            where->annotationToolUsed();
    });
}

void TypewriterTool::cancel(ToolHost& host)
{
    dragging_ = false;
    host.requestRepaint();
}

void TypewriterTool::paint(QPainter& painter, const ToolHost& host) const
{
    (void)host;
    if (!dragging_ || !dragged(at_)) return;
    // A PREVIEW of where the text will go: the box being dragged, or a callout's leader.
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent(0x00, 0x78, 0xd4);
    painter.setPen(QPen(accent, 1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    if (which_ == AnnotationToolKind::Callout) {
        painter.drawLine(pressedAt_, at_);
        painter.drawRect(QRect(at_, QSize(80, 24)));
    } else if (which_ == AnnotationToolKind::TextBox) {
        painter.drawRect(QRect(pressedAt_, at_).normalized());
    }
    painter.restore();
}

} // namespace rawpage::ui
