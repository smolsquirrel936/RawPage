// Arranging the windows this process owns -- Stage 4 T11, `Architecture-FileMap.md` §3.13,
// decision **D4-17** (`Stage4-ViewingSurface-Plan.md` §3).
//
// ===========================================================================================
// IT ADDS NO `platform/` INTERFACE, AND THAT IS D4-17.
// ===========================================================================================
// Placing our own top-level windows is `QWidget::setGeometry` over `QScreen::availableGeometry()`,
// which Qt does on every platform. `Tech-Stack-Plan.md` §4.3's rule is that what Qt does
// cross-platform gets no interface, so an `IWindowArranger` would be a Windows dependency for
// arithmetic. The OS-specific half -- *which* monitor, at what scale -- is `IDisplayTopology`, and
// it already exists; what this file needs from a monitor is its WORK AREA, the rectangle a taskbar
// has already been subtracted from, and that is `QScreen`'s answer rather than the topology's.
//
// ===========================================================================================
// THE ARITHMETIC IS SEPARATE FROM THE WINDOWS, SO IT CAN BE JUDGED WITH NO SCREEN.
// ===========================================================================================
// `tileGeometry` and `cascadeGeometry` are pure functions of a work area and a count. Guide case
// **A-64** (`Stage4-ViewingSurface-Testing-Guide.md` §5.12) is written against them over synthetic
// geometry, because the offscreen platform has one screen of a size no case may depend on and
// because *"the tiles fill the work area and do not overlap"* is a claim about numbers rather than
// about pixels. The half that touches windows is below them and is three lines each.
//
// ===========================================================================================
// WHICH WAY IS "HORIZONTAL". THE SAME COIN FLIP `SplitViewContainer` RECORDS, AND THE SAME ANSWER.
// ===========================================================================================
// **`Horizontal` is a horizontal DIVIDER: windows in full-width strips, one above another** --
// what Windows' own *Show windows stacked* does. `Vertical` is a vertical divider: windows side by
// side. The orientation names the line between them, not the axis they march along, exactly as
// `SplitViewContainer::Mode` does one layer down; two spellings of the same word meaning opposite
// things inside one product is how a feature ships backwards and looks like it works.
//
// ===========================================================================================
// A TILE NEVER GOES BELOW A MINIMUM, AND THE FALLBACK IS A GRID RATHER THAN A REFUSAL.
// ===========================================================================================
// Eight windows in full-width strips on a 1080-pixel work area is 135 pixels apiece, which is a
// title bar and a sliver. So the requested orientation decides the PREFERRED shape and the minimum
// decides how far it can be taken: strips are added until one would fall below `kMinTileHeight`
// (or `kMinTileWidth`), and the surplus windows are then laid ACROSS the strips. What the caller
// asked for survives wherever it fits, and the result is always an exact, non-overlapping cover of
// the work area -- gaps read as a tiling bug just as clearly as overlaps do.
//
// **The one case the minimum cannot be honoured** is a work area too small to hold `count` cells at
// that size at all. The cover then still holds and the cells are simply smaller: a window pushed
// off the work area to keep it large would be worse than a small window, and no arrangement can
// make a 200-pixel-tall desktop hold four readable windows. The bound is stated at the case
// (convention 19) rather than left for a reader to discover.
//
// ===========================================================================================
// A CELL IS WHERE THE WINDOW'S FRAME GOES, NOT ITS CONTENTS -- issue I-23.
// ===========================================================================================
// `QWidget::setGeometry` on a top-level window places the CLIENT area; the title bar is drawn
// ABOVE it. Until 2026-09-27 every cell was handed to `setGeometry` as it was, so on the user's
// 150 % monitor (M-09's walk, `Stage4-ViewingSurface-Testing-Guide.md` §8) the first cascaded
// window and the top tile had their title bars 45 physical pixels above the screen, every column
// of a vertical tiling did, and each lower strip's title bar covered the bottom of the strip above
// it. A-64 could not see it: it judges the arithmetic, and the arithmetic was right.
//
// So `placeFrame` removes the frame margins Qt reports -- `frameGeometry()` minus `geometry()` --
// from the cell first, and the cascade steps by the window's OWN title-bar height from the same
// margins rather than by the style's `PM_TitleBarHeight`, which on this machine said 21 where the
// native title bar is 30 and would have left each window covering a third of the one before it.
// **Still Qt's answer, and still no `platform/` interface** (D4-17): Qt already knows the frame.
//
// **What Qt's margins do NOT include, measured** (convention 19): Windows 11 draws a visible
// border about one logical pixel wide at the left, right and bottom that Qt counts as client, so a
// tiled window's visible edge sits that far outside its cell -- 2 physical pixels at 150 %, 3 where
// a logical edge lands on a half pixel (guide case A-94 bounds it). Two
// neighbouring tiles share a border line and the bottom one touches the taskbar's edge by that
// much. Removing it would need DWM's extended frame bounds, which is `platform/` and a decision.
//
// ===========================================================================================
// MERGE, AND WHY IT TARGETS THE FIRST WINDOW RATHER THAN THE ACTIVE ONE.
// ===========================================================================================
// *Merge all windows* must produce **one window with every tab in its original order** (guide case
// **A-65**), and "original order" only means something if the target is fixed: merging into the
// window the person happens to be standing in would put that window's own tabs first and the
// others after it, so the result would depend on where they clicked. The first window in
// `MainWindow::shellWindows()` is the creation-order first, which is also the one `app/main.cpp`
// owns as a stack object and therefore the one that can never be closed. Every other window is
// emptied in creation order, its tabs in tab order, and is then closed if it owns itself --
// `MainWindow::closeIfEmptied` holds that rule and this file does not repeat it.
//
// ===========================================================================================
// SYNCHRONIZED SCROLLING BETWEEN TWO WINDOWS.
// ===========================================================================================
// `window.new` opens a second view of one document in a second window (`DocumentWindow`'s
// companion constructor). Those two views scroll together by default, which is what the feature is
// for, and `window.syncScroll` unlinks them. The mirror is the same arithmetic
// `SplitViewContainer` uses between panes and for the same reason -- it goes through PDF-point
// space, so two views at different zooms stay at the same PLACE in the document rather than at the
// same pixel offset -- and it carries the same echo guard, because two windows each following the
// other is a loop with no fixed point. The guard is a file-scope flag rather than a member
// precisely because there is no one object in the loop to hang it on.

#ifndef RAWPAGE_UI_SHELL_WINDOWARRANGER_H
#define RAWPAGE_UI_SHELL_WINDOWARRANGER_H

#include <QList>
#include <QRect>
#include <QStringList>

class QScreen;
class QWidget;

namespace rawpage::ui {

class DocumentWindow;
class MainWindow;
class PageView;

class WindowArranger {
public:
    // Named for the DIVIDER. See the header before assuming.
    enum class Orientation {
        Horizontal,   // full-width strips, one above another
        Vertical,     // full-height columns, side by side
    };

    // The smallest a tiled window may be made. Not a guess about a person's screen: it is roughly
    // the width at which `ui/shell/Ribbon`'s chevron overflow has nothing left to collapse and the
    // height at which a page surface is shorter than its own scrollbar's arrows.
    static constexpr int kMinTileWidth = 360;
    static constexpr int kMinTileHeight = 260;

    // The diagonal step of a cascade, used when the style cannot say what a title bar is worth.
    // Guide case A-64 asserts the step against the value the arrangement actually used, never
    // against this constant, so a style with taller title bars does not redden the case.
    static constexpr int kCascadeFallbackStep = 28;

    // ---- The arithmetic, judged with no screen (A-64) ---------------------------------------

    // `count` rectangles that exactly cover `workArea` and do not overlap. An empty work area or a
    // count below one yields an empty list rather than a degenerate rectangle.
    static QList<QRect> tileGeometry(const QRect& workArea, int count, Orientation orientation);

    // `count` equally sized rectangles stepped diagonally by `step`, each entirely inside
    // `workArea` -- which is what *"every title bar stays reachable"* reduces to once a window
    // cannot be placed outside the work area at all. The diagonal restarts from the top-left when
    // the next step would not fit, so the twentieth window is as reachable as the second.
    static QList<QRect> cascadeGeometry(const QRect& workArea, int count, int step);

    // What a cascade will step by: `sample`'s own title-bar height from the frame Qt reports around
    // it (I-23), else the style's title-bar height, else `kCascadeFallbackStep`.
    static int cascadeStep(const QWidget* sample = nullptr);

    // ---- The operations, over `MainWindow::shellWindows()` -----------------------------------

    // Arranges each MONITOR'S windows within that monitor's own work area, rather than gathering
    // every window onto one screen. A person who put two windows on the second monitor and pressed
    // Tile meant *tile what is here*, and moving them all to the primary would be the product
    // rearranging their desk. Manual case **M-09** is where that is seen; this is the rule it walks.
    static void tile(Orientation orientation);
    static void cascade();

    // Every other window's documents into the first window, in order. Returns how many tabs moved.
    static int mergeWindows();

    // The window list (`Features.md` §1.1): one entry per window, in creation order, named by its
    // own title -- which is the document in front of it. `activateWindow` raises one by index.
    static QStringList windowList();
    static void        activateWindow(int index);

    // ---- Synchronized scrolling between two views of one document ----------------------------
    // `leader` has just been scrolled by the person. Mirrors onto every view of the same document
    // in the OTHER windows, when the pair is linked. Called from `MainWindow`, which is what
    // connects a tab's `scrolled` signal; a tab may not name another window.
    static void mirrorScroll(DocumentWindow& document, PageView* leader);

private:
    // Every window that is currently on `screen`, in creation order.
    static QList<MainWindow*> windowsOnScreen(const QScreen* screen);

    // Puts `window`'s FRAME -- title bar included -- on `cell` (I-23).
    static void placeFrame(MainWindow* window, const QRect& cell);
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_WINDOWARRANGER_H
