#include "ui/shell/WindowArranger.h"

#include "core/Geometry.h"
#include "core/Log.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/ViewportController.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QStyle>

#include <algorithm>
#include <vector>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;

// THE ECHO GUARD, at file scope. Two windows each following the other is a loop with no fixed
// point, and unlike `SplitViewContainer`'s mirror there is no single object on the path to hang a
// member on -- the leader's tab writes the companion's controller, which emits from the companion's
// container, which arrives back here. One flag for the whole process is correct because the mirror
// is synchronous and runs on the UI thread: there is never a second one in flight.
bool g_mirroring = false;

// Device pixels per PDF point for a view, or 0 when it has not been laid out. The mirror divides by
// it, so "not laid out yet" has to be answerable rather than assumed non-zero -- the same guard
// `SplitViewContainer::propagateScroll` carries, for the same reason.
double presentedScaleOf(const PageView& view)
{
    return view.layoutEngine().layout().presentedScale();
}

// Splits `total` into `parts` whole numbers that sum to exactly `total`, the remainder spread one
// pixel at a time over the first parts. Returning the SIZES rather than the edges is what makes the
// cover exact by construction: an implementation that computed each edge as `i * total / parts`
// loses a pixel to truncation wherever the division is not whole, and a one-pixel gap between two
// tiled windows reads as a tiling bug from across the room.
std::vector<int> shares(int total, int parts)
{
    std::vector<int> out;
    if (parts < 1) return out;
    out.reserve(static_cast<std::size_t>(parts));

    const int base = total / parts;
    const int extra = total - base * parts;
    for (int i = 0; i < parts; ++i) out.push_back(base + (i < extra ? 1 : 0));
    return out;
}

// How many strips the preferred axis can hold without any of them falling below `minimum`, capped
// at `count` and never below one. This is the whole of the minimum rule: everything else follows
// from laying the surplus across the strips that survive it.
int stripsThatFit(int extent, int count, int minimum)
{
    if (minimum <= 0) return std::max(1, count);
    return std::clamp(extent / minimum, 1, std::max(1, count));
}

} // namespace

// =============================================================================================
// The arithmetic -- pure, and judged over synthetic geometry (A-64)
// =============================================================================================

QList<QRect> WindowArranger::tileGeometry(const QRect& workArea, int count, Orientation orientation)
{
    QList<QRect> out;
    if (count < 1 || workArea.width() <= 0 || workArea.height() <= 0) return out;

    // THE PREFERRED SHAPE FIRST, THEN THE MINIMUM. `Horizontal` wants `count` full-width strips;
    // it gets as many as the work area can hold at `kMinTileHeight` and lays the rest across them.
    const bool stacked = orientation == Orientation::Horizontal;
    const int  majorExtent = stacked ? workArea.height() : workArea.width();
    const int  majorMinimum = stacked ? kMinTileHeight : kMinTileWidth;
    const int  bands = stripsThatFit(majorExtent, count, majorMinimum);

    // Windows per band, differing by at most one, so a fifth window in a four-band tiling widens
    // one band's row rather than adding a band nobody asked for.
    const std::vector<int> perBand = shares(count, bands);
    const std::vector<int> bandSizes = shares(majorExtent, bands);

    int major = stacked ? workArea.top() : workArea.left();
    for (int band = 0; band < bands; ++band) {
        const std::vector<int> cellSizes = shares(stacked ? workArea.width() : workArea.height(),
                                                 perBand[static_cast<std::size_t>(band)]);
        int minor = stacked ? workArea.left() : workArea.top();
        for (const int cell : cellSizes) {
            out.append(stacked
                           ? QRect(minor, major, cell, bandSizes[static_cast<std::size_t>(band)])
                           : QRect(major, minor, bandSizes[static_cast<std::size_t>(band)], cell));
            minor += cell;
        }
        major += bandSizes[static_cast<std::size_t>(band)];
    }
    return out;
}

QList<QRect> WindowArranger::cascadeGeometry(const QRect& workArea, int count, int step)
{
    QList<QRect> out;
    if (count < 1 || workArea.width() <= 0 || workArea.height() <= 0) return out;
    if (step < 1) step = kCascadeFallbackStep;

    // THREE QUARTERS OF THE WORK AREA, floored at the tiling minimum. A cascaded window is meant to
    // be worked in, not glanced at: a cascade of window-sized windows would hide every title bar
    // but the last, and a cascade of thumbnails would be a tiling with extra steps.
    const int width = std::max(std::min(workArea.width(), kMinTileWidth), workArea.width() * 3 / 4);
    const int height = std::max(std::min(workArea.height(), kMinTileHeight), workArea.height() * 3 / 4);

    // HOW FAR THE DIAGONAL CAN RUN BEFORE IT RESTARTS. Every window stays wholly inside the work
    // area, which is a stronger statement than *"its title bar is on screen"* and is the one that
    // can be asserted without knowing what a title bar is worth on this machine.
    const int roomX = workArea.width() - width;
    const int roomY = workArea.height() - height;
    const int steps = std::max(1, std::min(roomX, roomY) / step + 1);

    for (int i = 0; i < count; ++i) {
        const int at = i % steps;
        out.append(QRect(workArea.left() + at * step, workArea.top() + at * step, width, height));
    }
    return out;
}

int WindowArranger::cascadeStep()
{
    if (QStyle* style = QApplication::style()) {
        const int titleBar = style->pixelMetric(QStyle::PM_TitleBarHeight);
        if (titleBar > 0) return titleBar;
    }
    return kCascadeFallbackStep;
}

// =============================================================================================
// The operations
// =============================================================================================

QList<MainWindow*> WindowArranger::windowsOnScreen(const QScreen* screen)
{
    QList<MainWindow*> out;
    for (MainWindow* window : MainWindow::shellWindows())
        if (window->screen() == screen) out.append(window);
    return out;
}

void WindowArranger::tile(Orientation orientation)
{
    // ONE WINDOW IS NOT AN ARRANGEMENT, and the guard is HERE as well as in the command's enable
    // predicate. The two say different things and both are needed: the predicate is why the ribbon
    // button is grey and why a dispatch is refused with `Unavailable` rather than appearing to
    // work (Stage 1's I-53), and this is why the operation itself moves nothing when it is reached
    // some other way -- guide case A-64's control asserts both.
    //
    // It counts the WHOLE window set rather than each screen's share: with three windows across two
    // monitors the monitor holding one of them does fill with that window, which is what every
    // tiling does and what manual case M-09 walks.
    if (MainWindow::shellWindows().size() < 2) return;

    // PER MONITOR, not per desktop. See the declaration: a person who put two windows on the second
    // screen and pressed Tile meant *tile what is here*.
    for (QScreen* screen : QGuiApplication::screens()) {
        const QList<MainWindow*> windows = windowsOnScreen(screen);
        if (windows.isEmpty()) continue;

        const QList<QRect> cells =
            tileGeometry(screen->availableGeometry(), static_cast<int>(windows.size()), orientation);
        for (int i = 0; i < windows.size() && i < cells.size(); ++i) {
            // `showNormal()` FIRST. A maximized or minimized window ignores `setGeometry` -- the
            // state wins over the rectangle -- so tiling a maximized window would silently do
            // nothing and look like a dead command.
            if (windows.at(i)->isMaximized() || windows.at(i)->isMinimized()) windows.at(i)->showNormal();
            windows.at(i)->setGeometry(cells.at(i));
        }
    }
}

void WindowArranger::cascade()
{
    if (MainWindow::shellWindows().size() < 2) return;   // see `tile()`

    const int step = cascadeStep();
    for (QScreen* screen : QGuiApplication::screens()) {
        const QList<MainWindow*> windows = windowsOnScreen(screen);
        if (windows.isEmpty()) continue;

        const QList<QRect> cells =
            cascadeGeometry(screen->availableGeometry(), static_cast<int>(windows.size()), step);
        for (int i = 0; i < windows.size() && i < cells.size(); ++i) {
            if (windows.at(i)->isMaximized() || windows.at(i)->isMinimized()) windows.at(i)->showNormal();
            windows.at(i)->setGeometry(cells.at(i));

            // RAISED IN ORDER, so the last window in the set is the one on top and the one the
            // person is left looking at. A cascade whose stacking order did not match its offsets
            // would put a window's title bar over another window's content.
            windows.at(i)->raise();
        }
    }
}

int WindowArranger::mergeWindows()
{
    const QList<MainWindow*> windows = MainWindow::shellWindows();
    if (windows.size() < 2) return 0;

    MainWindow* target = windows.first();
    int moved = 0;

    for (MainWindow* window : windows) {
        if (window == target) continue;

        // TAKEN OUT FROM THE FRONT, so tab order is preserved: `releaseDocument(0)` repeatedly walks
        // the strip left to right, and each one is appended to the target. A release-and-adopt is a
        // MOVE -- the tab keeps its handle, its journal and its scroll position (`releaseDocument`)
        // -- which is what makes merge a re-dock rather than a close and a re-open.
        while (window->documentCount() > 0) {
            if (DocumentWindow* document = window->releaseDocument(0)) {
                target->adoptDocument(document);
                ++moved;
            } else {
                break;   // nothing can be taken; leave the window rather than spin
            }
        }
        window->closeIfEmptied();
    }

    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "merge: moved" << moved << "document(s) into the first window";
    target->raiseToForeground();
    return moved;
}

QStringList WindowArranger::windowList()
{
    QStringList out;
    for (MainWindow* window : MainWindow::shellWindows()) out.append(window->windowTitle());
    return out;
}

void WindowArranger::activateWindow(int index)
{
    const QList<MainWindow*>& windows = MainWindow::shellWindows();
    if (index < 0 || index >= windows.size()) return;
    windows.at(index)->raiseToForeground();
}

// =============================================================================================
// Synchronized scrolling between two views of one document
// =============================================================================================

void WindowArranger::mirrorScroll(DocumentWindow& document, PageView* leader)
{
    if (g_mirroring || leader == nullptr || !document.synchronizedWithOtherWindows()) return;

    // THE GROUP IS THE LEADER AND ITS COMPANIONS, resolved from whichever end the scroll arrived
    // at. `DocumentWindow::leader()` never points at another companion, so this is a star and not a
    // chain, and the walk below is one level deep by construction.
    DocumentWindow* owner = document.leader() != nullptr ? document.leader() : &document;

    QList<DocumentWindow*> group;
    group.append(owner);
    group.append(owner->companions());
    if (group.size() < 2) return;

    const double leaderScale = presentedScaleOf(*leader);
    if (leaderScale <= 0.0) return;
    const core::DevicePoint from = leader->controller().scroll();

    g_mirroring = true;
    for (DocumentWindow* follower : group) {
        for (PageView* pane : follower->split()->views()) {
            if (pane == leader) continue;

            // THROUGH PDF-POINT SPACE, exactly as the split's mirror is. Two windows showing one
            // document at different zooms is the arrangement this feature exists for -- an overview
            // beside a detail -- and mirroring the raw device offset would walk them apart. The
            // ratio is exactly 1.0 when the zooms match, so the common case is a copy.
            const double scale = presentedScaleOf(*pane);
            if (scale <= 0.0) continue;
            const double ratio = scale / leaderScale;

            core::DevicePoint to = from;
            to.x *= ratio;
            to.y *= ratio;
            pane->controller().setScroll(to);
        }
    }
    g_mirroring = false;
}

} // namespace rawpage::ui
