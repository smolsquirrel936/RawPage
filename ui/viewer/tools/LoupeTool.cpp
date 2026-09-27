#include "ui/viewer/tools/LoupeTool.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QStyleHints>
#include <QPalette>
#include <QPixmap>

#include <algorithm>
#include <iterator>

namespace rawpage::ui {

QRect LoupeTool::lensRect(const ToolHost& host) const
{
    const QSize viewport = host.viewportSize();
    QRect lens(centre_.x() - kLensSize / 2, centre_.y() - kLensSize / 2, kLensSize, kLensSize);

    // CLAMPED INSIDE THE VIEWPORT. A lens hanging half off the edge is a lens you cannot read at
    // exactly the moment you are looking at a margin, which is where small print lives.
    if (lens.right() > viewport.width()) lens.moveRight(viewport.width());
    if (lens.bottom() > viewport.height()) lens.moveBottom(viewport.height());
    if (lens.left() < 0) lens.moveLeft(0);
    if (lens.top() < 0) lens.moveTop(0);
    return lens;
}

void LoupeTool::press(ToolHost& host, QPoint pos)
{
    // A PRESS DECIDES NOTHING YET -- S4 guide I-26. Whether it is a CLICK (which toggles the pin) or
    // the start of a DRAG (which aims the lens) is only known at the release, so the press puts the
    // lens under the pointer and waits. Until 2026-09-27 the press toggled the pin, so a drag pinned
    // the lens where it began and the rest of the drag moved nothing. Nothing scrolls and nothing
    // zooms, either way -- see the header, and guide case A-43.
    pressed_ = true;
    moved_ = false;
    pressAt_ = pos;
    centre_ = pos;
    visible_ = true;
    host.requestRepaint();
}

void LoupeTool::move(ToolHost& host, QPoint pos)
{
    // WITH THE BUTTON DOWN THE LENS FOLLOWS, pinned or not: a drag is how a pinned lens is moved.
    // Past the platform's drag distance it is no longer a click, and the release will not toggle.
    if (pressed_) {
        if ((pos - pressAt_).manhattanLength() >= QGuiApplication::styleHints()->startDragDistance())
            moved_ = true;
    } else if (pinned_) {
        return;
    }
    centre_ = pos;
    visible_ = true;
    host.requestRepaint();
}

void LoupeTool::release(ToolHost& host, QPoint pos)
{
    if (!pressed_) return;
    pressed_ = false;

    // A CLICK TOGGLES THE PIN -- the lens stays at the click, or follows the pointer again. A DRAG
    // AIMED IT and leaves the pin as it was: an unpinned lens goes on following the hover, and a
    // pinned one stays pinned where the drag put it.
    if (!moved_) pinned_ = !pinned_;
    else centre_ = pos;
    host.requestRepaint();
}

void LoupeTool::cancel(ToolHost& host)
{
    pressed_ = false;
    moved_ = false;
    if (!visible_ && !pinned_) return;
    visible_ = false;
    pinned_ = false;
    host.requestRepaint();
}

void LoupeTool::paint(QPainter& painter, const ToolHost& host) const
{
    if (!visible_) return;

    const QPixmap* frame = host.lastFrame();
    if (frame == nullptr || frame->isNull()) return;

    const QRect lens = lensRect(host);

    // THE SOURCE REGION, in the frame's own DEVICE pixels. The frame is captured at the device
    // pixel ratio, so a lens described in logical pixels has to be scaled into it -- and forgetting
    // that gives a magnifier that is correct at 100 % and shows the wrong part of the page on a
    // 150 % panel, which no offscreen case can see (manual M-12's half).
    const double ratio = host.devicePixelRatio();
    const double span = kLensSize / magnification();
    const QRectF source((centre_.x() - span / 2.0) * ratio, (centre_.y() - span / 2.0) * ratio,
                        span * ratio, span * ratio);

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipRect(lens);
    painter.drawPixmap(lens, *frame, source);

    // THEN SHARP, OVER IT, WHEN THE HOST HAS IT (D4-20). The enlarged frame above is what the lens
    // shows the instant it moves; the host re-rasterizes the same region at the magnified scale once
    // it has been still, and paints that on top. Where it has nothing yet -- or where the lens
    // hangs over the desk beside a page -- the enlarged frame is what shows, never a hole.
    (void)host.paintSharpLens(painter, lens,
                              QRectF(centre_.x() - span / 2.0, centre_.y() - span / 2.0, span, span));
    painter.setClipping(false);

    // A BORDER, from the palette rather than a fixed colour (convention 20): without one the
    // magnified region and the page around it are the same picture at two scales, and the eye
    // cannot find the seam.
    painter.setPen(QGuiApplication::palette().color(QPalette::Highlight));
    painter.drawRect(lens.adjusted(0, 0, -1, -1));

    // AND ITS MAGNIFICATION, in the corner, so an `Alt`+wheel notch has something to show for itself
    // (D4-22) -- a number and the multiplication sign, not a word, so it has no catalog key to need.
    const QString label = QString::number(magnification(), 'g', 2) + QChar(0x00D7);
    const QFontMetrics metrics(painter.font());
    const QRect box = metrics.boundingRect(label).adjusted(-4, -1, 4, 1);
    const QRect corner(lens.right() - box.width() - 2, lens.bottom() - box.height() - 2, box.width(),
                       box.height());
    painter.fillRect(corner, QGuiApplication::palette().color(QPalette::Highlight));
    painter.setPen(QGuiApplication::palette().color(QPalette::HighlightedText));
    painter.drawText(corner, Qt::AlignCenter, label);
    painter.restore();
}

bool LoupeTool::stepMagnification(int steps)
{
    constexpr int top = static_cast<int>(std::size(kMagnifications)) - 1;
    const int next = std::clamp(rung_ + steps, 0, top);
    if (next == rung_) return false;
    rung_ = next;
    return true;
}

} // namespace rawpage::ui
