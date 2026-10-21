// ui/panes/CommentIcon -- implementation. See the header for why the pictures are drawn rather than shipped.

#include "ui/panes/CommentIcon.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {

using core::AnnotationSubtype;

CommentGlyph glyphFor(const core::AnnotationStore& store, std::size_t index)
{
    if (index >= store.size()) return CommentGlyph::Other;
    switch (store.at(index).subtype) {
    case AnnotationSubtype::Text: return CommentGlyph::Note;
    case AnnotationSubtype::Highlight: return CommentGlyph::Highlight;
    case AnnotationSubtype::Underline: return CommentGlyph::Underline;
    case AnnotationSubtype::StrikeOut: return CommentGlyph::StrikeOut;
    case AnnotationSubtype::Squiggly: return CommentGlyph::Squiggly;
    case AnnotationSubtype::Caret:
        // A REPLACEMENT is a caret holding the words struck out as its group member (D6-12).
        for (const std::size_t member : store.groupMembers(index)) {
            if (store.at(member).subtype == AnnotationSubtype::StrikeOut) return CommentGlyph::Replace;
        }
        return CommentGlyph::Insert;
    case AnnotationSubtype::FreeText: return CommentGlyph::TypedText;
    case AnnotationSubtype::Ink:
    case AnnotationSubtype::Line:
    case AnnotationSubtype::Square:
    case AnnotationSubtype::Circle:
    case AnnotationSubtype::Polygon:
    case AnnotationSubtype::PolyLine: return CommentGlyph::Drawing;
    case AnnotationSubtype::Stamp: return CommentGlyph::Stamp;
    case AnnotationSubtype::FileAttachment: return CommentGlyph::Attachment;
    default: return CommentGlyph::Other;
    }
}

QColor colourOf(const std::vector<double>& components)
{
    const auto c = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    switch (components.size()) {
    case 1: return QColor(c(components[0]), c(components[0]), c(components[0]));
    case 3: return QColor(c(components[0]), c(components[1]), c(components[2]));
    case 4: {   // CMYK, naively: enough for a picture
        const double k = components[3];
        return QColor(c((1 - components[0]) * (1 - k)), c((1 - components[1]) * (1 - k)), c((1 - components[2]) * (1 - k)));
    }
    default: return QColor();
    }
}

namespace {

// Drawn on a 16-by-16 grid, scaled to the square the view asks for.
class CommentIconEngine : public QIconEngine {
public:
    CommentIconEngine(CommentGlyph glyph, QColor fill, const QWidget* palette)
        : glyph_(glyph), fill_(fill), palette_(palette)
    {
    }

    QIconEngine* clone() const override { return new CommentIconEngine(glyph_, fill_, palette_.data()); }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap out(size);
        out.fill(Qt::transparent);
        QPainter painter(&out);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return out;
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap out(size * scale);
        out.setDevicePixelRatio(scale);
        out.fill(Qt::transparent);
        QPainter painter(&out);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return out;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        const QPalette palette = palette_ ? palette_->palette() : QApplication::palette();
        const QPalette::ColorGroup group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        const QColor ink = palette.color(group, mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::Text);
        const QColor fill = fill_.isValid() ? fill_ : palette.color(group, QPalette::Mid);
        const double side = std::min(rect.width(), rect.height());
        if (side <= 0) return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 16.0, side / 16.0);
        const QPen inkPen(ink, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

        // A line of TEXT, in the list's colour.
        const auto text = [&](double x1, double x2, double y) {
            painter->setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(x1, y), QPointF(x2, y));
        };
        // A stroke in the COMMENT's colour, edged in the list's so a pale one still reads.
        const auto stroke = [&](const QPainterPath& path, double width) {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(ink, width + 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPath(path);
            painter->setPen(QPen(fill, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPath(path);
        };
        const auto line = [](QPointF a, QPointF b) {
            QPainterPath path(a);
            path.lineTo(b);
            return path;
        };
        // A shape filled with the comment's colour and outlined in the list's.
        const auto shape = [&](const QPainterPath& path) {
            painter->setPen(inkPen);
            painter->setBrush(fill);
            painter->drawPath(path);
        };

        switch (glyph_) {
        case CommentGlyph::Note: {
            QPainterPath bubble;
            bubble.addRoundedRect(QRectF(1.5, 2.0, 13.0, 9.5), 2.0, 2.0);
            QPainterPath tail(QPointF(4.0, 11.0));
            tail.lineTo(4.0, 14.5);
            tail.lineTo(8.0, 11.0);
            tail.closeSubpath();
            shape(bubble.united(tail));
            text(4.0, 12.0, 5.5);
            text(4.0, 9.5, 8.0);
            break;
        }
        case CommentGlyph::Highlight: {
            QPainterPath band;
            band.addRect(QRectF(1.5, 4.5, 13.0, 7.0));
            shape(band);
            text(4.0, 12.0, 8.0);
            break;
        }
        case CommentGlyph::Underline:
            text(2.5, 13.5, 4.0);
            text(2.5, 13.5, 7.5);
            stroke(line(QPointF(2.0, 11.5), QPointF(14.0, 11.5)), 1.8);
            break;
        case CommentGlyph::StrikeOut:
            text(2.5, 13.5, 4.0);
            text(2.5, 13.5, 12.0);
            stroke(line(QPointF(2.0, 8.0), QPointF(14.0, 8.0)), 1.8);
            break;
        case CommentGlyph::Squiggly: {
            text(2.5, 13.5, 4.0);
            text(2.5, 13.5, 7.5);
            QPainterPath wave(QPointF(2.0, 12.0));
            for (int i = 1; i <= 6; ++i) wave.lineTo(2.0 + 2.0 * i, i % 2 ? 10.5 : 12.5);
            stroke(wave, 1.5);
            break;
        }
        case CommentGlyph::Insert: {
            text(1.5, 14.5, 13.0);
            QPainterPath caret(QPointF(4.0, 12.0));
            caret.lineTo(8.0, 4.0);
            caret.lineTo(12.0, 12.0);
            stroke(caret, 1.8);
            break;
        }
        case CommentGlyph::Replace: {
            text(1.5, 14.5, 5.0);
            stroke(line(QPointF(1.5, 5.0), QPointF(14.5, 5.0)), 1.6);
            QPainterPath caret(QPointF(5.0, 14.0));
            caret.lineTo(8.0, 8.5);
            caret.lineTo(11.0, 14.0);
            stroke(caret, 1.6);
            break;
        }
        case CommentGlyph::TypedText:
            painter->setPen(inkPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRectF(1.5, 1.5, 13.0, 13.0));
            stroke(line(QPointF(4.5, 4.5), QPointF(11.5, 4.5)), 1.6);
            stroke(line(QPointF(8.0, 4.5), QPointF(8.0, 12.0)), 1.6);
            break;
        case CommentGlyph::Drawing: {
            QPainterPath scribble(QPointF(2.0, 12.0));
            scribble.cubicTo(QPointF(5.0, 1.0), QPointF(9.0, 16.0), QPointF(14.0, 4.0));
            stroke(scribble, 1.8);
            break;
        }
        case CommentGlyph::Stamp: {
            QPainterPath handle;
            handle.addEllipse(QRectF(5.5, 1.5, 5.0, 5.0));
            handle.addRect(QRectF(7.0, 6.5, 2.0, 2.5));
            shape(handle);
            QPainterPath base;
            base.addRoundedRect(QRectF(2.0, 9.0, 12.0, 3.5), 1.0, 1.0);
            shape(base);
            text(2.0, 14.0, 14.5);
            break;
        }
        case CommentGlyph::Attachment: {
            QPainterPath clip(QPointF(10.0, 5.0));
            clip.lineTo(10.0, 11.5);
            clip.arcTo(QRectF(6.0, 9.5, 4.0, 4.0), 0.0, -180.0);
            clip.lineTo(6.0, 4.0);
            clip.arcTo(QRectF(6.0, 1.25, 5.5, 5.5), 180.0, -180.0);
            clip.lineTo(11.5, 12.5);
            stroke(clip, 1.5);
            break;
        }
        case CommentGlyph::Other: {
            QPainterPath square;
            square.addRoundedRect(QRectF(3.0, 3.0, 10.0, 10.0), 2.0, 2.0);
            shape(square);
            break;
        }
        }
        painter->restore();
    }

private:
    CommentGlyph glyph_;
    QColor fill_;
    QPointer<const QWidget> palette_;
};

} // namespace

QIcon commentIcon(CommentGlyph glyph, QColor fill, const QWidget* palette)
{
    return QIcon(new CommentIconEngine(glyph, fill, palette));
}

} // namespace rawpage::ui
