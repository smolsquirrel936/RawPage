// The Loupe -- `Features.md` §1.2 *"a magnifier window over a small area"*, Stage 4 T7.
//
// ===========================================================================================
// IT IS SHARP — decision D4-20, 2026-09-27, which reverses T7's choice and was measured first.
// ===========================================================================================
// T7 magnified only the **last painted frame**: a raster is asynchronous (`RenderScheduler`, one
// worker behind PDFium's engine lock), so a lens fed by the engine would lag the pointer and put
// one more competitor in front of the viewport's tiles. The cost it recorded was that the lens
// made small things bigger and never made blurry things sharp — and that a re-rasterizing loupe
// *"would be measured against A2 before it was built"*. The user asked for exactly that during
// M-12 (*"I want it to be sharp in len, not just enlarge the pixel"*), so:
//
//   * **The frame is still drawn first**, so the lens appears the instant it moves and never
//     waits for the engine — T7's reason, kept.
//   * **The host then paints the region SHARP over it** (`ToolHost::paintSharpLens`): once the
//     region has been still for `PageView::kLoupeSettleMs`, `ui/viewer/LoupeRaster` asks the
//     shipped scheduler for exactly that region at `presentedScale × magnification()`, one request
//     at a time with the last one cancelled, and the page's inversion, grid and guides are laid
//     over it at the same magnification.
//   * **Measured as T7 said it must be** (guide case A-100): a `+ loupe` column in
//     `bench_surface`, two lens requests a round inside the graded Visible lane, read 4.44–4.61 ms
//     against the plain viewer's 4.40–4.92 ms in the same runs — noise, and inside the 16.667 ms
//     budget. `Tech-Stack-Plan.md` §3.3 records it.
//
// ===========================================================================================
// IT MOVES NOTHING.
// ===========================================================================================
// No scroll, no zoom, no view-history entry — which is guide case **A-43**'s second half. It follows
// the pointer on HOVER (`tracksHover()`), because a magnifier you have to hold a button down to aim
// is a magnifier nobody can aim.
//
// ===========================================================================================
// A CLICK PINS IT, A DRAG AIMS IT — S4 guide I-26, chosen by the user during M-12, 2026-09-27.
// ===========================================================================================
// A PIN holds the lens still so a person can let go of the mouse and look. Until 2026-09-27 a
// PRESS toggled it, so a drag pinned the lens where the drag began and the rest of the drag did
// nothing. Now the press decides nothing: a release within the platform's drag distance of the
// press is a CLICK and toggles the pin; anything further is a DRAG, during which the lens follows
// the pointer — pinned or not — and after which the pin is what it was. So a pinned lens is moved
// by dragging it and stays pinned there.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_LOUPETOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_LOUPETOOL_H

#include "ui/viewer/tools/Tool.h"

#include <QPoint>
#include <QRect>

namespace rawpage::ui {

class LoupeTool final : public Tool {
public:
    // The lens, in logical pixels -- fixed.
    static constexpr int kLensSize = 160;

    // HOW MUCH IT ENLARGES: a rung of this ladder, stepped by `Alt`+wheel (decision D4-22), 2.5 until
    // somebody steps it. A LADDER rather than a continuous factor, so a notch is a visible change and
    // the ends are round numbers a person can find again. It belongs to the view for the session and
    // is not a setting: `D4-5` declared Stage 4's settings keys and none of them is this.
    static constexpr double kMagnifications[] = {1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0};
    static constexpr int    kDefaultRung = 2;   // 2.5
    static constexpr double kDefaultMagnification = kMagnifications[kDefaultRung];

    double magnification() const noexcept { return kMagnifications[rung_]; }

    // Moves `steps` rungs up (positive) or down, clamped at both ends. Returns whether the
    // magnification changed -- a notch at the top of the ladder changes nothing and asks for nothing.
    bool stepMagnification(int steps);

    ToolKind kind() const override { return ToolKind::Loupe; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    bool paintsOverlay() const override { return true; }
    bool needsFrame() const override { return true; }
    bool tracksHover() const override { return true; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;

    // TRUE WHILE THE BUTTON IS DOWN (I-26) — a click or a drag in progress. It is what keeps
    // `ToolController` from swapping to the temporary hand under a held button, which would hand
    // the release to a tool that never saw the press, and what makes arming another tool mid-press
    // put the lens away. It changes nothing in the document either way.
    bool isDragging() const override { return pressed_; }

    void paint(QPainter& painter, const ToolHost& host) const override;

    bool   isVisible() const noexcept { return visible_; }
    bool   isPinned() const noexcept { return pinned_; }
    QPoint centre() const noexcept { return centre_; }

    // The lens rectangle in widget coordinates, clamped inside the viewport so the magnifier never
    // hangs half off the edge where it cannot be read.
    QRect lensRect(const ToolHost& host) const;

private:
    bool   visible_ = false;
    bool   pinned_ = false;
    bool   pressed_ = false;   // the button is down: a click or a drag, not yet known which
    bool   moved_ = false;     // it went past the drag distance, so the release is not a click
    QPoint pressAt_;
    QPoint centre_;
    int    rung_ = kDefaultRung;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_LOUPETOOL_H
