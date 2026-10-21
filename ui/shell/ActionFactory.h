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
//   * a REGISTERED action that is currently refused says which kind of disabled it is -- Stage 1's
//     I-53, built at Stage 2 T14. `tooltipFor()` below is the one place that decides, and
//     `refresh()` re-asks it, because enablement changes while the window is open and a tooltip
//     computed once at construction would be a sentence about a state that has passed.
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
#include <QPointer>
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

    // ---------------------------------------------------------------------------------------
    // PLACING A COMMAND ON A MENU -- T11, and it is here for the same reason `create` is.
    // ---------------------------------------------------------------------------------------
    // `layer-lint`'s `bare-qaction` rule matches `addAction(` with no exception for the overload
    // that takes an already-built action, and that bluntness is deliberate: distinguishing
    // `addAction(QAction*)` from `addAction(QString)` by regular expression is exactly the kind of
    // "the gate is nearly right" that lets the real violation through. So a caller that wants a
    // command on a menu asks HERE, one line instead of two, and the rule stays absolute with a
    // single allowed file.
    RawPageAction* addToMenu(QMenu* menu, core::ActionId id);

    // ---------------------------------------------------------------------------------------
    // ADOPTING THE ACTIONS QT MADE FOR A WIDGET -- T11, issue I-22.
    // ---------------------------------------------------------------------------------------
    // Some Qt widgets manufacture an action as a side effect of an ordinary property.
    // `QLineEdit::setClearButtonEnabled(true)` is the case that found this: the clear button IS a
    // QAction, it is parented to the line edit, and -- unlike `QMenu::menuAction()` or
    // `QDockWidget::toggleViewAction()` -- there is no accessor that hands it back, because it goes
    // on the widget's private side-widget list rather than into `QWidget::actions()`. So it cannot
    // be recognised by identity the way the other two are, and recognising it by Qt's internal
    // object name would tie this gate to a private constant.
    //
    // Instead the widget's owner says so, once, at the moment it turns the property on: everything
    // hanging off `widget` right now that is not a RawPageAction is Qt's, and is stamped as
    // non-command. Returns how many were stamped, so a caller that expected one and got none knows
    // its Qt version changed. Called IMMEDIATELY after construction -- it stamps what exists at
    // that instant and nothing later, which is what keeps it from becoming a blanket amnesty.
    static int adoptWidgetActions(QWidget* widget);

    // A separator is a QAction too -- Qt has no other way to express one -- and it is not a
    // command. Built here and stamped, so `wiringComplaints()` recognises it rather than reporting
    // every menu's dividers as bare actions.
    // Static: it stamps, and needs no registry -- a pane without a factory of its own divides its menu here too.
    static QAction* addMenuSeparator(QMenu* menu);

    // Re-reads enablement and check state from the registry for every action this factory made.
    // Called when document state changes; at Stage 1 that is only ever composition time.
    void refresh();

    // The tooltip an action should be showing RIGHT NOW. Three answers, and the distinction is
    // Stage 1's I-53: a declared-but-unregistered command is about this BUILD, a
    // registered-but-refused one is about this DOCUMENT, and an available one says what it does.
    // Static and taking the registry, so a case can ask the question without a widget.
    static QString tooltipFor(const core::ActionRegistry& registry, core::ActionId id,
                              const QString& label);

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

    // QPointers, not raw pointers, and that is a correctness requirement rather than caution. The
    // Quick Access Toolbar rebuilds its buttons on every add, remove and reorder, and the actions
    // are parented to the buttons -- so entries in this list are destroyed under us as a matter of
    // routine. `refresh()` walked it. Raw pointers made that a use-after-free waiting for the first
    // reorder; found at T11 by needing the rebuild, not by a crash.
    QList<QPointer<RawPageAction>> made_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_ACTIONFACTORY_H
