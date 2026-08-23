// The only QAction subclass in RawPage -- decision D1-7(b) (Stage1-AppShell-Plan.md §3, §5.4).
//
// ---------------------------------------------------------------------------------------------
// WHY A SUBCLASS AT ALL, WHEN QAction ALREADY CARRIES setData().
//
// The rule this stage exists to make permanent is *nothing is wired directly to a widget slot*:
// every ribbon button, menu item, chord, QAT entry, tool-finder hit, CLI verb and batch step
// resolves to one `ActionId` and calls `ActionRegistry::dispatch`. That is what makes the headless
// batch runner (flow K, Stage 14) reach every feature without a second implementation of any of
// them, and it is why a feature reachable only from a widget is unfinished rather than done.
//
// A rule stated in prose is a rule enforced by whoever last read the prose. So it is enforced twice,
// statically and dynamically, and this class is the dynamic half:
//
//   (a) `tools/layer-lint` has a NON-ESCAPABLE `bare-qaction` token rule. Constructing a QAction, or
//       calling an `addAction()` overload that builds one from a string, is a gate failure anywhere
//       but `ui/shell/ActionFactory.cpp`. It catches the violation at the moment it is WRITTEN.
//
//   (b) This class knows its own `ActionId` and can report how many receivers `triggered` has.
//       `ActionFactory` makes exactly one connection, so the answer is always 1; anything else is
//       somebody's second `connect`, and `assertWiring()` finds it at show time.
//
// NEITHER ALONE IS ENOUGH, which is the whole reason there are two. The lint cannot see a
// `connect()` added to a legitimately-created action -- the construction it was watching for already
// passed. The assertion cannot see an action on a dialog nobody opened. Guide case A-54 runs the
// assertion against a PLANTED violation of each kind, because a self-check nobody has ever watched
// fail is a self-check nobody knows works.
// ---------------------------------------------------------------------------------------------

#ifndef RAWPAGE_UI_SHELL_RAWPAGEACTION_H
#define RAWPAGE_UI_SHELL_RAWPAGEACTION_H

#include "core/cmd/ActionId.h"

#include <QAction>

namespace rawpage::ui {

class RawPageAction : public QAction {
    Q_OBJECT

public:
    // The base is constructed textless on purpose: the label comes from the catalog, through
    // `ActionFactory`, which is the only caller. A `QAction(const QString&)` overload here would
    // reintroduce exactly the "somebody can disagree with ActionId.def" that D1-6 removed.
    RawPageAction(core::ActionId id, QObject* parent)
        : QAction(parent), id_(id) {}

    core::ActionId id() const { return id_; }

    // Wraps the protected `QObject::receivers()`, which is why this has to be a member rather than
    // a free function in the test. Counts connections to `triggered(bool)` -- the signal a widget is
    // wired through -- and nothing else, so an unrelated connection to `changed()` does not read as
    // a violation.
    int triggeredReceiverCount() const { return receivers(SIGNAL(triggered(bool))); }

    // ---------------------------------------------------------------------------------------
    // HOW MANY RECEIVERS ARE LEGITIMATE, and why the answer is not simply one.
    // ---------------------------------------------------------------------------------------
    // `ActionFactory` makes exactly one connection, so "exactly one" was the obvious invariant --
    // and it was wrong, which the first run against the real window said plainly: all 192 ribbon
    // actions reported two. `QToolButton::setDefaultAction` adds the action to the button, and Qt
    // connects the button to `triggered` itself. Placing an action on a widget is the ordinary,
    // correct thing to do with one; an invariant that forbids it forbids the ribbon.
    //
    // So the honest statement is that every receiver must be ACCOUNTED FOR: one from the factory,
    // plus at most one per widget the action has been placed on -- which is exactly what
    // `associatedObjects()` enumerates. Anything beyond that is a connection somebody added, and
    // that is the thing D1-7(b) exists to find.
    //
    // The residual gap, stated rather than papered over: an action placed on two widgets where only
    // one of them connects could hide a single direct connection. Narrowing that needs the
    // receiver's identity, which Qt does not expose. It is a bound, not a proof -- and it is still
    // the only mechanism that can see a second `connect()` at all, which the lint structurally
    // cannot. (Issue I-20.)
    int accountedReceiverCount() const
    {
        return 1 + static_cast<int>(associatedObjects().size());
    }

    bool isCorrectlyWired() const
    {
        const int actual = triggeredReceiverCount();
        return actual >= 1 && actual <= accountedReceiverCount();
    }

private:
    core::ActionId id_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_RAWPAGEACTION_H
