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
//
// ---------------------------------------------------------------------------------------------
// THE PANE REGISTRY -- Stage 4 T5, decision D4-2.
//
// **A pane is registered under its own `ActionId`**, and the list holds exactly the panes that
// exist. There is no row for a pane that is not built: no greyed entry, no *coming soon*. That is
// convention 6's rule for `platform/` interfaces -- *an unimplemented thing is a null member, never
// a stub* -- read across to a list a person chooses from, and it is the same argument D4-2 makes
// from Stage 2's **I-11**: a row that opens an empty dock is indistinguishable from a pane that is
// broken, and the user has no way to tell which they are looking at. The seven remaining
// `Ctrl+Shift+*` chords stay unregistered and leave their keys unconsumed until the models they
// would show exist.
//
// **Why the key is an `ActionId` and not a string of our own.** Each pane already has a permanent
// public name and a §8 chord -- `pane.thumbnails`, `pane.bookmarks`, `pane.destinations` -- so a
// second identifier here would be a second thing to keep in step for no gain, and the pane chooser
// can dispatch the real command rather than reaching into the dock. That is the same call the close
// button made at T9: pane CHROME (pin, float, dock side) gets no `ActionId` because nothing else
// needs to reach it, and anything the ribbon, a chord or the batch runner must reach goes through
// the registry.
//
// The Stage 1 placeholder is still here and now has one job: it is what the dock shows when NO pane
// is registered, which is a shell built without panes -- a test fixture, or a window before T5's
// registration runs. With panes present it is never visible.
// ---------------------------------------------------------------------------------------------

#ifndef RAWPAGE_UI_SHELL_SIDEBARDOCK_H
#define RAWPAGE_UI_SHELL_SIDEBARDOCK_H

#include "core/cmd/ActionId.h"

#include <QDockWidget>
#include <QString>

#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QStackedWidget;

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

    // What the dock shows when NO pane is registered -- see the header. Not an empty pane, which
    // reads as a bug.
    void setPlaceholderText(const QString& text);

    // ---- The pane registry -- Stage 4 T5, D4-2 ------------------------------------------------
    struct Pane {
        core::ActionId id;
        QString        title;
        QWidget*       widget = nullptr;
    };

    // Takes ownership of `pane` (it is re-parented into the dock's stack). Registering the same id
    // twice is refused rather than silently replacing: two panes under one name is a bug in the
    // window's construction, and the second would be unreachable.
    bool addPane(core::ActionId id, const QString& title, QWidget* pane);

    // In registration order, which is the order the chooser lists them. **Exactly the panes that
    // exist** -- the list a case reads for D4-2.
    const std::vector<Pane>& panes() const noexcept { return panes_; }

    // Brings a registered pane to the front. Does NOT show the dock or move focus -- those are the
    // window's, because an auto-hidden or closed sidebar is the window's state and not this dock's
    // to overrule. Returns false for an id nothing registered.
    bool showPane(core::ActionId id);

    std::optional<core::ActionId> currentPane() const;
    QWidget* paneWidget(core::ActionId id) const;

signals:
    void autoHiddenChanged(bool autoHidden);

    // The front pane changed -- by the chooser, or by `showPane`. The window uses it to keep the
    // title in step; it carries the id rather than the widget so nothing outside has to know the
    // pane's type.
    void currentPaneChanged(core::ActionId id);

protected:
    // Width memory is captured on resize rather than on hide: by the time hide() runs, a dock
    // being collapsed by a splitter drag has already reported width 0.
    void resizeEvent(QResizeEvent* event) override;

    // Leaving a peeked pane puts it away again. This is the whole of the auto-hide interaction and
    // it is why the class needs an event handler at all.
    void leaveEvent(QEvent* event) override;

private:
    void updateTitleBarState();
    void updateChooser();

    core::ActionRegistry& actions_;
    QLabel*         placeholder_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QComboBox*      chooser_ = nullptr;
    QLabel*         titleLabel_ = nullptr;

    std::vector<Pane> panes_;

    // True while `updateChooser()` is rewriting the combo. Without it the rebuild's own
    // `currentIndexChanged` would dispatch the first pane's command every time a pane was added --
    // Stage 1's `suppressCustomizationSave_` trap, met a second time.
    bool suppressChooserSignal_ = false;

    int  rememberedWidth_ = 280;
    bool autoHidden_ = false;
    bool peeking_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_SIDEBARDOCK_H
