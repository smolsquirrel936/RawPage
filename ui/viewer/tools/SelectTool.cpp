#include "ui/viewer/tools/SelectTool.h"

#include "ui/viewer/SelectionOverlay.h"

#include <algorithm>

namespace rawpage::ui {
namespace {

core::text::Granularity granularityFor(int clicks)
{
    // One click a caret, two a word, three a line, four a block -- and a fifth starts again at one, which
    // `PageView`'s click counter does by wrapping, so this never sees more than four.
    switch (std::clamp(clicks, 1, 4)) {
    case 2: return core::text::Granularity::Word;
    case 3: return core::text::Granularity::Line;
    case 4: return core::text::Granularity::Block;
    default: return core::text::Granularity::Character;
    }
}

} // namespace

void SelectTool::pressWith(ToolHost& host, QPoint pos, const PressContext& context)
{
    SelectionOverlay* selection = host.selection();
    if (selection == nullptr) return;
    const PagePoint at = host.pagePointAt(pos);
    if (!at.isValid()) return;

    SelectionOverlay::PressOptions options;
    options.granularity = granularityFor(context.clicks);
    options.column = context.modifiers.testFlag(kColumnModifier);
    // A multi-click is a unit, never a column: `Alt`+double-click selects the word, as a plain one does.
    if (context.clicks > 1) options.column = false;
    selection->press(at.page, at.point, options);
    dragging_ = true;
}

void SelectTool::move(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    SelectionOverlay* selection = host.selection();
    if (selection == nullptr) return;

    // A COLUMN STAYS ON ITS PAGE: the point is taken in that page's space wherever the pointer went.
    const int columnPage = selection->columnGesturePage();
    const PagePoint at = columnPage >= 0 ? host.pagePointOn(columnPage, pos) : host.pagePointAt(pos);
    if (!at.isValid()) return;
    selection->drag(at.page, at.point);
}

void SelectTool::release(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    move(host, pos);
    dragging_ = false;
    if (SelectionOverlay* selection = host.selection()) selection->release();
}

void SelectTool::cancel(ToolHost& host)
{
    if (!dragging_) return;
    dragging_ = false;
    if (SelectionOverlay* selection = host.selection()) selection->cancelGesture();
    host.requestRepaint();
}

void SelectTool::extendTo(ToolHost& host, QPoint pos)
{
    SelectionOverlay* selection = host.selection();
    if (selection == nullptr) return;
    const PagePoint at = host.pagePointAt(pos);
    if (!at.isValid()) return;

    SelectionOverlay::PressOptions options;
    options.extend = true;
    selection->press(at.page, at.point, options);
    selection->release();
}

} // namespace rawpage::ui
