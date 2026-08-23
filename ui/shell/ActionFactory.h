// The one place in RawPage that constructs a QAction -- decision D1-7(a).
//
// `tools/layer-lint`'s `bare-qaction` rule permits `QAction(`, `new QAction` and the string-taking
// `addAction(` overloads in `ui/shell/ActionFactory.cpp` and nowhere else, and the rule is
// `escapable: false` -- there is no inline waiver, because there is no legitimate second place.
// Everything else asks here.
//
// WHAT THE FACTORY GUARANTEES, and each is something a hand-built QAction gets wrong quietly:
//
//   * the label is `ActionId.def`'s, resolved through the catalog -- so a button and the tool
//     finder and a menu entry for one command cannot disagree about its name;
//   * `triggered` has EXACTLY ONE receiver, and it dispatches through `ActionRegistry`;
//   * an action the registry does not know is created DISABLED with a tooltip saying it is not
//     built yet, rather than omitted. A missing button reads as a bug in the ribbon; a greyed one
//     that says why reads as a stage. Stage 1 declares 358 ids and registers a handful, so this is
//     the common case rather than an edge;
//   * a toggle is `checkable`, and only a toggle is. `ActionRegistry` distinguishes "not a toggle"
//     from "a toggle that is off", and this is where that distinction reaches the screen.

#ifndef RAWPAGE_UI_SHELL_ACTIONFACTORY_H
#define RAWPAGE_UI_SHELL_ACTIONFACTORY_H

#include "core/cmd/ActionId.h"
#include "core/cmd/KeyContext.h"

#include <QList>
#include <QString>

#include <functional>

class QAction;
class QMenu;
class QObject;
class QWidget;

namespace rawpage::core { class ActionRegistry; }

namespace rawpage::ui {

class RawPageAction;

class ActionFactory {
public:
    // How the factory learns which context a dispatch happened in. At T13 this is
    // `ShortcutBinder::activeContext()`, a pure function of the focus chain; until then it is a
    // constant. It is a callback rather than a direct call so that T13 is one composition-root line
    // instead of an edit to every call site -- and so this class stays constructible in a test
    // without a focus chain.
    using ContextProvider = std::function<core::KeyContext()>;

    ActionFactory(core::ActionRegistry& registry, ContextProvider context);

    // Creates the action for `id`, parented to `parent`. Never returns null.
    RawPageAction* create(core::ActionId id, QObject* parent);

    // Re-reads enablement and check state from the registry for every action this factory made.
    // Called when document state changes; at Stage 1 that is only ever composition time.
    void refresh();

    // ---------------------------------------------------------------------------------------
    // THE ONE NON-COMMAND ACTION SURFACE, named here so it is impossible to find by accident.
    // ---------------------------------------------------------------------------------------
    // A QMenu's items ARE QActions -- that is Qt's design and there is no way around it. The
    // ribbon's overflow chevron (criterion R-3) offers the tabs that did not fit, and a ribbon TAB
    // is not a command: it has no `ActionId`, it appears in no menu of commands, and there is
    // nothing for `ActionRegistry` to route, so it cannot be reached from the CLI or the batch
    // runner and is not supposed to be.
    //
    // Rather than carve `ui/shell/Ribbon.cpp` out of the lint rule -- which would leave a second
    // file able to build command-shaped actions with nobody watching -- the construction stays
    // HERE, where every other one is, and the resulting action is stamped so that
    // `wiringComplaints()` knows to expect it. Two consequences worth keeping: the lint rule stays
    // absolute and non-escapable with exactly one allowed path, and a reader who greps for who is
    // allowed to build an action finds one answer.
    static QAction* addNonCommandMenuItem(QMenu* menu, const QString& text);

    // The dynamic-property name `addNonCommandMenuItem` stamps. Exposed so the widget suite asserts
    // against the real marker rather than a copy of its spelling.
    static const char* nonCommandMarker();

    // ---------------------------------------------------------------------------------------
    // The debug self-check -- D1-7(b), guide case A-54.
    // ---------------------------------------------------------------------------------------
    // Walks `root->findChildren<QAction*>()` and reports every action that is either not a
    // `RawPageAction` at all, or is one whose `triggered` has a receiver count other than 1.
    //
    // Returns the complaints rather than asserting, for two reasons. A test has to be able to see
    // the finding without the process dying; and the caller decides the severity -- `MainWindow`
    // asserts in a debug build, where a developer is present to fix it, and logs in a release build,
    // where killing a user's session over a wiring mistake would be the worse failure.
    //
    // NAMES BOTH KINDS SEPARATELY, because the two mechanisms catch different things: a bare QAction
    // is what the lint would have caught at the moment it was written, and a second `connect` on a
    // properly-created action is what the lint structurally cannot see.
    static QList<QString> wiringComplaints(const QWidget* root);

    // Convenience for the caller that only wants to know. Debug builds assert on this in
    // `MainWindow::showEvent`.
    static bool wiringIsSound(const QWidget* root) { return wiringComplaints(root).isEmpty(); }

private:
    core::ActionRegistry&  registry_;
    ContextProvider        context_;
    QList<RawPageAction*>  made_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_ACTIONFACTORY_H
