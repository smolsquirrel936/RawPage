// The pane host: dock left or right, float, auto-hide, and remember the width.
//
// ---------------------------------------------------------------------------------------------
// WHAT QT GIVES US AND WHAT IT DOES NOT.
//
// Tech-Stack-Plan.md §4.3 lists docking among the things Qt does cross-platform, and it is right:
// `QDockWidget` handles dock-left, dock-right, drag-to-float and drag-to-re-dock, and this class
// does not reimplement any of it. What Qt has no notion of is AUTO-HIDE -- the pane collapsing to a
// narrow strip on the window edge and sliding back out when you reach for it -- which every PDF
// editor and IDE has and which Features.md §1.1 specifies. So that part is ours, and it is the only
// part of this file that is not configuration.
//
// The division of labour with `MainWindow` is deliberate and worth stating, because the obvious
// alternative is worse: THIS class owns the pane's own state (hidden, floating, auto-hidden, how
// wide it was), and the WINDOW owns the edge strip that an auto-hidden pane collapses into. A dock
// widget cannot place a sibling in its parent window's layout without reaching through the parent,
// and a class that reaches through its parent is a class that cannot be tested on its own.
//
// WIDTH MEMORY IS PER SESSION AT STAGE 1. The width survives hide/show, auto-hide/pin, and
// float/re-dock, because losing it there is a defect people notice within a minute. It does NOT
// survive a restart yet: that is `core/settings/SessionState`, which is Stage 4's file, and
// inventing settings keys here would put per-window state in `SettingsSchema` where its owner
// cannot find it later.
// ---------------------------------------------------------------------------------------------

#ifndef RAWPAGE_UI_SHELL_SIDEBARDOCK_H
#define RAWPAGE_UI_SHELL_SIDEBARDOCK_H

#include <QDockWidget>

class QLabel;

namespace rawpage::core {
class ActionRegistry;
}

namespace rawpage::ui {

class SidebarDock : public QDockWidget {
    Q_OBJECT

public:
    explicit SidebarDock(core::ActionRegistry& actions, QWidget* parent = nullptr);
    ~SidebarDock() override;

    // The width the pane should return to. Updated whenever the pane is visible and docked, so a
    // hide/show round trip is a no-op from the user's point of view.
    int rememberedWidth() const { return rememberedWidth_; }
    void setRememberedWidth(int width);

    bool isAutoHidden() const { return autoHidden_; }

    // Auto-hiding a pane hides the dock and hands the window a strip to put on the edge. Pinning it
    // restores the dock at its remembered width. Both are idempotent.
    void setAutoHidden(bool autoHidden);

    // Temporarily reveal an auto-hidden pane -- the "reach for the strip" gesture. `unpeek()` puts
    // it back. Calling either on a pinned pane does nothing, which is what lets the window connect
    // them unconditionally.
    void peek();
    void unpeek();
    bool isPeeking() const { return peeking_; }

    // The panes themselves are Stage 4 and later (`ui/panes/*`). Until one exists this hosts an
    // explicit placeholder saying so -- not an empty pane, which reads as a bug.
    void setPlaceholderText(const QString& text);

signals:
    void autoHiddenChanged(bool autoHidden);

protected:
    // Width memory is captured on resize rather than on hide: by the time hide() runs, a dock
    // being collapsed by a splitter drag has already reported width 0.
    void resizeEvent(QResizeEvent* event) override;

    // Leaving a peeked pane puts it away again. This is the whole of the auto-hide interaction and
    // it is why the class needs an event handler at all.
    void leaveEvent(QEvent* event) override;

private:
    void updateTitleBarState();

    core::ActionRegistry& actions_;
    QLabel* placeholder_ = nullptr;

    int  rememberedWidth_ = 280;
    bool autoHidden_ = false;
    bool peeking_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_SIDEBARDOCK_H
