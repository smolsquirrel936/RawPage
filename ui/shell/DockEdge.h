// ui/shell/DockEdge -- the line on the side of a docked pane that faces the page. The user's requests,
// 2026-10-03 (the Advanced Search pane, `Stage5-TextSearch-Testing-Guide.md` A-123) and 2026-10-04 (the
// sidebar and the log pane, A-124).
//
// ---------------------------------------------------------------------------------------------
// WHY IT EXISTS
// ---------------------------------------------------------------------------------------------
// The main window's dock separator -- the strip a person drags to resize a docked pane -- is painted in the
// window's own colour by the native style, so nothing showed where the page ended and a pane began, or where
// to grab. The product sets no style sheet or proxy style that could draw it. This draws a strip in the
// theme's `Border` colour (`QPalette::Mid`, set by `ThemeManager::apply`, so it follows a theme switch) on the
// edge the dock is docked against: left when docked right, right when docked left, top when docked at the
// bottom, bottom when docked at the top, and nothing when the dock floats -- its window frame is the edge then.
//
// ---------------------------------------------------------------------------------------------
// WHY A CHILD WIDGET, AND WHY ONE LOGICAL PIXEL WIDE
// ---------------------------------------------------------------------------------------------
// A CHILD, STACKED ABOVE THE DOCK'S CONTENTS: the dock's own `paintEvent` paints UNDER its children, and the
// sidebar's body has no margin, so a line the dock painted would be covered by the very contents it borders.
// The search pane's first version painted from its own `paintEvent`, which showed only because its body sits
// six pixels in. It takes no mouse events, so a press on it reaches whatever is beneath.
//
// A STRIP ONE LOGICAL PIXEL WIDE, NOT A HAIRLINE ON THE EDGE: at 150 % -- this machine's monitors -- a dock at
// logical x 793 starts at device 1189.5, and a cosmetic hairline on the edge rasterized to device 1189, the
// window's pixel, and was clipped away. It passed every check at a whole scale and was invisible on the screen
// (A-123). A strip of one logical pixel always covers at least one device pixel the dock owns.

#ifndef RAWPAGE_UI_SHELL_DOCKEDGE_H
#define RAWPAGE_UI_SHELL_DOCKEDGE_H

#include <QWidget>

class QDockWidget;
class QEvent;
class QPaintEvent;

namespace rawpage::ui {

class DockEdge final : public QWidget {
public:
    // Owned by `dock`, as its child; follows it from then on -- moved, re-docked, floated, resized.
    explicit DockEdge(QDockWidget* dock);

    // Which side it is on now, or `Qt::NoDockWidgetArea` when it is drawn nowhere -- for the cases.
    Qt::DockWidgetArea side() const noexcept { return side_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void place();

    QDockWidget* dock_ = nullptr;
    Qt::DockWidgetArea side_ = Qt::NoDockWidgetArea;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_DOCKEDGE_H
