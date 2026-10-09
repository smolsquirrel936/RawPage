// ui/viewer/tools/StampTool -- implementation. Stage 6 T10. See the header for the two gestures and the choice.

#include "ui/viewer/tools/StampTool.h"

#include "core/doc/Stamps.h"
#include "core/settings/ProfileStore.h"
#include "ui/viewer/AnnotationOverlay.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QStyleHints>

namespace rawpage::ui {
namespace {

int dragSlop()
{
    const QStyleHints* hints = QGuiApplication::styleHints();
    return hints != nullptr ? hints->startDragDistance() : 4;
}

StampChoice& choice()
{
    static StampChoice current;
    return current;
}

} // namespace

const StampChoice& currentStamp() { return choice(); }

void setCurrentStamp(StampChoice next) { choice() = std::move(next); }

core::Result<core::StampImage> decodeStampImage(const QByteArray& bytes, const QString& sourceName)
{
    // A JPEG keeps its own bytes -- written `/DCTDecode` as they are (guide A-77). One `/DCTDecode` cannot read
    // (CMYK, twelve bits, lossless) is decoded like any other image instead of refused.
    if (core::ProfileStore::imageFormatOf(bytes) == QLatin1String("jpg")) {
        if (core::Result<core::StampImage> jpeg = core::stampImageFromJpeg(bytes)) return jpeg;
    }
    QImage image = QImage::fromData(bytes);
    if (image.isNull())
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.stamps.notAnImage"), sourceName};
    return stampImageOf(std::move(image));
}

core::Result<core::StampImage> stampImageOf(QImage image)
{
    if (image.isNull()) return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.stamps.notAnImage"), QString()};
    if (std::max(image.width(), image.height()) > kMaxDecodedSide)
        image = image.scaled(kMaxDecodedSide, kMaxDecodedSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_RGBA8888);   // straight alpha: what an `/SMask` holds
    QByteArray rgb;
    QByteArray alpha;
    rgb.reserve(static_cast<qsizetype>(image.width()) * image.height() * 3);
    alpha.reserve(static_cast<qsizetype>(image.width()) * image.height());
    for (int y = 0; y < image.height(); ++y) {
        const uchar* line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            rgb.append(static_cast<char>(line[4 * x]));
            rgb.append(static_cast<char>(line[4 * x + 1]));
            rgb.append(static_cast<char>(line[4 * x + 2]));
            alpha.append(static_cast<char>(line[4 * x + 3]));
        }
    }
    return core::stampImageFromPixels(image.width(), image.height(), rgb, image.hasAlphaChannel() ? alpha : QByteArray());
}

core::Result<core::Annotation> StampTool::stampAt(ToolHost& host, int page, core::PdfPoint centre)
{
    AnnotationOverlay* annotations = host.annotationOverlay();
    if (annotations == nullptr)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.stamps.noDocument"), QString()};
    // Who and when, as every tool asks: the overlay's author and clock.
    const core::Annotation template_ = annotations->newAnnotation(core::AnnotationSubtype::Stamp, page);
    const QString author = template_.author;
    const std::int64_t now = template_.created.utcSeconds.value_or(0);
    const StampChoice& stamp = currentStamp();
    core::Result<core::Annotation> made = [&]() -> core::Result<core::Annotation> {
        switch (stamp.kind) {
        case StampChoice::Kind::Standard: return core::newStandardStamp(stamp.name, page, centre, author, now);
        case StampChoice::Kind::Dynamic: {
            // The HOST's offset at the instant placed (convention 11): the stamp says the time its placer saw.
            const int offset = QDateTime::fromSecsSinceEpoch(now).offsetFromUtc();
            return core::newDynamicStamp(stamp.name, page, centre, author, now, offset, host.hanPreference());
        }
        case StampChoice::Kind::Custom: return core::newImageStamp(stamp.image, stamp.name, page, centre, author, now);
        }
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.edit.stampUnknown"), stamp.name};
    }();
    if (!made) return made;
    const core::ToolDefaults defaults = host.toolDefaults(AnnotationToolKind::Stamp);
    if (defaults.opacity < 1.0) made.value().opacity = defaults.opacity;
    return made;
}

void StampTool::press(ToolHost& host, QPoint pos)
{
    const PagePoint at = host.pagePointAt(pos);
    if (!at.isValid()) return;
    page_ = at.page;
    pressedAt_ = pos;
    at_ = pos;
    dragging_ = true;
}

void StampTool::move(ToolHost& host, QPoint pos)
{
    at_ = pos;
    if (dragging_) host.requestRepaint();
}

void StampTool::release(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    dragging_ = false;
    at_ = pos;
    host.requestRepaint();
    AnnotationOverlay* annotations = host.annotationOverlay();
    if (annotations == nullptr) return;

    const PagePoint start = host.pagePointOn(page_, pressedAt_);
    if (!start.isValid()) return;
    const bool dragged = (pos - pressedAt_).manhattanLength() >= dragSlop();
    core::Result<core::Annotation> made = stampAt(host, page_, start.point);
    if (made && dragged) {
        const PagePoint end = host.pagePointOn(page_, pos);
        if (!end.isValid()) return;
        const std::optional<core::PdfRect> box = core::normalizedRect(start.point.x, start.point.y, end.point.x, end.point.y);
        if (!box || !(box->size.width > 0.0) || !(box->size.height > 0.0)) return;
        made = core::stampFittedTo(made.value(), *box, host.hanPreference());
    }
    if (!made) {
        annotations->reportRefusal(made.error());
        return;
    }
    if (annotations->addMade({made.value()})) host.annotationToolUsed();
}

void StampTool::cancel(ToolHost& host)
{
    dragging_ = false;
    host.requestRepaint();
}

void StampTool::paint(QPainter& painter, const ToolHost& host) const
{
    (void)host;
    if (!dragging_ || (at_ - pressedAt_).manhattanLength() < dragSlop()) return;
    painter.save();
    painter.setPen(QPen(QColor(0x00, 0x78, 0xd4), 1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRect(pressedAt_, at_).normalized());
    painter.restore();
}

} // namespace rawpage::ui
