// The Loupe -- `Features.md` §1.2 *"a magnifier window over a small area"*, Stage 4 T7.
//
// ===========================================================================================
// IT MAGNIFIES WHAT IS ALREADY ON SCREEN, AND THAT IS A DECISION WITH A COST.
// ===========================================================================================
// The obvious implementation asks the engine for the region under the cursor at a higher scale.
// It is also the wrong one here: a raster is asynchronous (`RenderScheduler`, one worker behind
// PDFium's engine lock), so a lens fed that way would lag the pointer by a frame or more and would
// put one more competitor in front of the viewport's own tiles — the exact cost **D4-10** bounds
// for the thumbnails pane and **A2 at Stage 4** grades.
//
// So the lens magnifies the **last painted frame**, which the host keeps only while a tool asks
// for it (`Tool::needsFrame()`). What that buys: the lens is exact, instant, and costs one
// viewport-sized blit per paint while the loupe is armed and nothing at all when it is not. **What
// it costs, stated rather than discovered** (convention 19): the magnified pixels are the SCREEN's
// pixels enlarged, not a re-rasterization, so the lens shows the page's own resolution magnified —
// it makes small things bigger, and it does not make blurry things sharp. A loupe that
// re-rasterized is a different feature and would be measured against A2 before it was built.
//
// ===========================================================================================
// IT MOVES NOTHING.
// ===========================================================================================
// No scroll, no zoom, no view-history entry — which is guide case **A-43**'s second half and the
// reason this tool's `press`/`release` do almost nothing. It follows the pointer on HOVER rather
// than on drag (`tracksHover()`), because a magnifier you have to hold a button down to aim is a
// magnifier nobody can aim; pressing only pins it, so a person can let go of the mouse and look.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_LOUPETOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_LOUPETOOL_H

#include "ui/viewer/tools/Tool.h"

#include <QPoint>
#include <QRect>

namespace rawpage::ui {

class LoupeTool final : public Tool {
public:
    // The lens, in logical pixels, and how much it enlarges. Fixed rather than configurable at
    // Stage 4: `D4-5` declares fifteen settings keys and none of them is this, and a knob nothing
    // reads back is the shape Stage 2's I-11 warns about.
    static constexpr int    kLensSize = 160;
    static constexpr double kMagnification = 2.5;

    ToolKind kind() const override { return ToolKind::Loupe; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    bool paintsOverlay() const override { return true; }
    bool needsFrame() const override { return true; }
    bool tracksHover() const override { return true; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;

    // A loupe is never mid-gesture in the sense `Esc` cares about: it changes nothing, so there is
    // nothing to abandon. `cancel()` hides the lens, and this stays false so that `Esc` with the
    // loupe armed reports *nothing was cancelled* — the control in A-42.
    bool isDragging() const override { return false; }

    void paint(QPainter& painter, const ToolHost& host) const override;

    bool   isVisible() const noexcept { return visible_; }
    QPoint centre() const noexcept { return centre_; }

    // The lens rectangle in widget coordinates, clamped inside the viewport so the magnifier never
    // hangs half off the edge where it cannot be read.
    QRect lensRect(const ToolHost& host) const;

private:
    bool   visible_ = false;
    bool   pinned_ = false;
    QPoint centre_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_LOUPETOOL_H
