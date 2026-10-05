// ui/viewer/tools/MarkupTool -- implementation. Stage 6 T6. See the header.

#include "ui/viewer/tools/MarkupTool.h"

#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/viewer/SelectionOverlay.h"

#include <QGuiApplication>
#include <QStyleHints>

#include <algorithm>

namespace rawpage::ui {
namespace {

core::text::Granularity granularityFor(int clicks)
{
    switch (std::clamp(clicks, 1, 4)) {
    case 2: return core::text::Granularity::Word;
    case 3: return core::text::Granularity::Line;
    case 4: return core::text::Granularity::Block;
    default: return core::text::Granularity::Character;
    }
}

int clickSlop()
{
    const QStyleHints* hints = QGuiApplication::styleHints();
    return hints != nullptr ? hints->startDragDistance() : 4;
}

AnnotationOverlay::Style styleFor(AnnotationToolKind kind)
{
    const AnnotationToolDefaults& defaults = sessionToolDefaults(kind);
    return AnnotationOverlay::Style{defaults.colour, defaults.opacity};
}

} // namespace

bool MarkupTool::handles(AnnotationToolKind kind) noexcept
{
    return markupSubtype(kind).has_value() || kind == AnnotationToolKind::ReplaceText
           || kind == AnnotationToolKind::InsertText;
}

void MarkupTool::pressWith(ToolHost& host, QPoint pos, const PressContext& context)
{
    pressedAt_ = pos;
    dragging_ = true;
    if (which_ == AnnotationToolKind::InsertText) return;   // a click: see `release`

    SelectionOverlay* selection = host.selection();
    if (selection == nullptr) return;
    const PagePoint at = host.pagePointAt(pos);
    if (!at.isValid()) return;
    SelectionOverlay::PressOptions options;
    options.granularity = granularityFor(context.clicks);
    options.column = context.clicks == 1 && context.modifiers.testFlag(Qt::AltModifier);
    selection->press(at.page, at.point, options);
}

void MarkupTool::move(ToolHost& host, QPoint pos)
{
    if (!dragging_ || which_ == AnnotationToolKind::InsertText) return;
    SelectionOverlay* selection = host.selection();
    if (selection == nullptr || !selection->isGestureActive()) return;
    const int columnPage = selection->columnGesturePage();
    const PagePoint at = columnPage >= 0 ? host.pagePointOn(columnPage, pos) : host.pagePointAt(pos);
    if (!at.isValid()) return;
    selection->drag(at.page, at.point);
}

void MarkupTool::release(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    move(host, pos);
    dragging_ = false;
    SelectionOverlay* selection = host.selection();
    AnnotationOverlay* annotations = host.annotationOverlay();
    if (selection == nullptr || annotations == nullptr) return;
    ToolHost* const where = &host;
    const AnnotationToolKind kind = which_;
    const AnnotationOverlay::Style style = styleFor(kind);

    if (kind == AnnotationToolKind::InsertText) {
        // A CLICK, not a drag: the caret goes where the person pointed.
        if ((pos - pressedAt_).manhattanLength() >= clickSlop()) return;
        const PagePoint at = host.pagePointAt(pos);
        if (!at.isValid()) return;
        (void)selection->ensurePage(at.page, core::render::RenderPriority::Visible);
        (void)host.requestAnnotationText(pos, [where, annotations, selection, at, style](const QString& text) {
            if (annotations->insertText(*selection, at.page, at.point, text, style)) where->annotationToolUsed();
        });
        return;
    }

    if (!selection->isGestureActive()) return;   // the press reached no page
    selection->release();
    // Nothing selected and nothing still resolving: a click on the page, which marks nothing.
    if (selection->isResolved() && !selection->hasSelection()) return;

    if (kind == AnnotationToolKind::ReplaceText) {
        (void)host.requestAnnotationText(pos, [where, annotations, selection, style](const QString& text) {
            const AnnotationOverlay::MarkOutcome made =
                annotations->markText(*selection, core::AnnotationSubtype::StrikeOut, style, text);
            if (made == AnnotationOverlay::MarkOutcome::Made || made == AnnotationOverlay::MarkOutcome::Pending)
                where->annotationToolUsed();
        });
        return;
    }
    const std::optional<core::AnnotationSubtype> subtype = markupSubtype(kind);
    if (!subtype) return;
    const AnnotationOverlay::MarkOutcome made = annotations->markText(*selection, *subtype, style);
    if (made == AnnotationOverlay::MarkOutcome::Made || made == AnnotationOverlay::MarkOutcome::Pending)
        host.annotationToolUsed();
}

void MarkupTool::cancel(ToolHost& host)
{
    if (!dragging_) return;
    dragging_ = false;
    if (which_ == AnnotationToolKind::InsertText) return;
    if (SelectionOverlay* selection = host.selection()) selection->cancelGesture();
    host.requestRepaint();
}

} // namespace rawpage::ui
