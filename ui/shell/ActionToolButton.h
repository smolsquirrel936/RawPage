// ui/shell/ActionToolButton -- a QToolButton that shows a command's action, WITHOUT gaining a signal
// connection every time that action changes. Stage 2 issue I-35.
//
// ===============================================================================================
// THE QT BEHAVIOUR THIS EXISTS FOR, MEASURED AGAINST 6.11.1 RATHER THAN READ FROM A CHANGELOG.
// ===============================================================================================
//
// `QToolButton::setDefaultAction()` connects the button to the action's `changed()` signal. When
// the action changes, the button receives `QEvent::ActionChanged` and re-applies the default action
// -- and re-applying CONNECTS AGAIN, with no check for the connection it already has. So a stock
// button's action gains one connection per change, forever:
//
//     after setDefaultAction            1
//     after 6 enable toggles            7
//     after further text/tooltip/check  15        (a plain widget holding an action: 0)
//
// Every command's enabled state, checked state and tooltip are re-asked on every change of
// document state (`ActionFactory::refresh()`), so each document opened and closed added about
// fifty-six connections -- 96 + 32 bytes of Qt's heap apiece, and one more slot call on every
// later change. It was the bulk of I-35's per-document growth once the log pane was bounded,
// found by counting receivers per signal after the heap census had shown the blocks but not the
// owner; no object count moved, because a connection is not an object.
//
// THE FIX: before Qt re-applies the default action, drop the button's own `changed()` connections
// from it. Qt then re-adds exactly one, so after every change the button is in the state a single
// `setDefaultAction()` leaves -- the same one connection Qt wants, not zero and not N. Text,
// tooltip, enabled, checked, click-triggers-once and click-toggles were each checked identical to
// the stock button in the same measurement. Nothing else of ours connects `changed()` to a button,
// so the disconnect removes only Qt's own.
//
// NOT Q_OBJECT, on purpose: it adds no signal, slot or property, so its meta-object -- and every
// `QToolButton` style rule, `findChildren<QToolButton*>()` and `qobject_cast` -- is the base
// class's. If a later Qt stops re-connecting, `tests/ui` says so (its control is the stock button)
// and this class becomes a no-op that can be deleted.

#ifndef RAWPAGE_UI_SHELL_ACTIONTOOLBUTTON_H
#define RAWPAGE_UI_SHELL_ACTIONTOOLBUTTON_H

#include <QAction>
#include <QActionEvent>
#include <QToolButton>

namespace rawpage::ui {

class ActionToolButton final : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void actionEvent(QActionEvent* event) override
    {
        if (event->type() == QEvent::ActionChanged && event->action() != nullptr
            && event->action() == defaultAction()) {
            QObject::disconnect(defaultAction(), &QAction::changed, this, nullptr);
        }
        QToolButton::actionEvent(event);
    }
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_ACTIONTOOLBUTTON_H
