#include "ui/shell/ActionFactory.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/RawPageAction.h"

#include <QDockWidget>
#include <QMenu>
#include <QPointer>
#include <QWidget>

namespace rawpage::ui {

using core::ActionId;
using core::LogSubsystem;

ActionFactory::ActionFactory(core::ActionRegistry& registry, ContextProvider context)
    : registry_(registry), context_(std::move(context))
{
}

RawPageAction* ActionFactory::create(ActionId id, QObject* parent)
{
    // THE ONE CONSTRUCTION. tools/layer-lint's `bare-qaction` rule allows this file and no other,
    // and it is not escapable -- there is no inline waiver marker for it, because a second place
    // that builds actions is a second place that can forget to route them through the registry.
    auto* action = new RawPageAction(id, parent);

    action->setText(catalog::translateAction(id));
    action->setObjectName(QString::fromLatin1(core::actionStringId(id).data(),
                                              static_cast<qsizetype>(core::actionStringId(id).size())));

    const bool registered = registry_.isRegistered(id);

    // A toggle is checkable and nothing else is. `ActionRegistry::isChecked` returns false both for
    // "a toggle that is off" and for "not a toggle", so checkability is decided by the registration
    // rather than by the answer -- which is the distinction the registry's own header insists on.
    if (registered) {
        // A TOGGLE IS CHECKABLE, AND ONLY A TOGGLE IS -- the header has promised this since T10 and
        // the code did not do it. `setChecked` on an action that is not checkable is a no-op, so
        // the promise held only because nothing registered a `checked` predicate until the QAT's
        // placement item at T11. `ActionRegistry::isToggle` is the question `isChecked` cannot
        // answer, and it was added for this. (Issue I-21.)
        action->setCheckable(registry_.isToggle(id));
        action->setEnabled(registry_.isEnabled(id));
        action->setChecked(registry_.isChecked(id));

        action->setToolTip(tooltipFor(registry_, id, action->text()));
    } else {
        // DECLARED BUT NOT REGISTERED. Stage 1 declares 358 ids and registers what the shell can
        // actually service, so this is the ordinary case for most of the ribbon. The button is
        // built, placed, sized and disabled -- and it says why. Leaving it out instead would make
        // the ribbon's real width and collapse behaviour a fiction until Stage 13, which is exactly
        // the thing T10 exists to get right early.
        action->setEnabled(false);
        action->setToolTip(tooltipFor(registry_, id, action->text()));
    }

    // EXACTLY ONE CONNECTION, always the same one. `RawPageAction::triggeredReceiverCount()` reads
    // 1 from here on, and anything else is somebody's second connect -- which is what
    // `wiringComplaints()` looks for and what the lint structurally cannot see.
    //
    // QPointer, not a raw capture: the lambda outlives nothing here, but the registry is a
    // reference to an object owned by CompositionRoot and the action is owned by a widget, and the
    // two are destroyed in an order this class does not control.
    QPointer<RawPageAction> guard(action);
    QObject::connect(action, &QAction::triggered, action, [this, guard]() {
        if (!guard) return;
        const core::Result<void> dispatched =
            registry_.dispatch(guard->id(), context_ ? context_() : core::KeyContext::Global);
        if (dispatched) return;

        // A failed dispatch is logged rather than swallowed. The registry distinguishes "no such
        // action" from "not available right now", and losing that distinction here would make a
        // Stage 14 batch report say the same thing for a user's typo and for document state.
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "dispatch refused for" << guard->objectName() << "--"
            << dispatched.error().messageKey;
    });

    made_.append(action);
    return action;
}

QString ActionFactory::tooltipFor(const core::ActionRegistry& registry, core::ActionId id,
                                 const QString& label)
{
    // 1. NOT BUILT. About this build, and the only one of the three Stage 1 ever produced.
    if (!registry.isRegistered(id))
        return catalog::translate(catalog::key::RibbonTooltipUnregistered).arg(label);

    const auto resolve = [](std::string_view key) {
        return key.empty() ? QString()
                           : catalog::translate(QString::fromUtf8(
                                 key.data(), static_cast<qsizetype>(key.size())));
    };

    // 2. BUILT AND REFUSED RIGHT NOW. About this DOCUMENT, and it is I-53's missing half: at
    // Stage 1 this case was nearly theoretical (338 of 361 ids were unregistered, so almost every
    // disabled control a person met was the explained kind) and a real document makes commands
    // CONDITIONALLY available, which is the kind that said nothing at all.
    //
    // The registration's own `tooltipKey` names the SPECIFIC reason when it has one -- "this is
    // already the first page" rather than "not available for this document" -- and the generic
    // sentence is the fallback. Falling back to the LABEL instead, which is what an available
    // action does, would put the command's own name in a tooltip explaining why it is greyed out.
    if (!registry.isEnabled(id)) {
        // The document's own reason first (Stage 3 T10) -- "this document is encrypted" -- then the key.
        if (const QString specific = registry.unavailableText(id); !specific.isEmpty())
            return specific;
        const QString why = resolve(registry.unavailableKey(id));
        return why.isEmpty() ? catalog::translate(catalog::key::RibbonTooltipUnavailable).arg(label)
                             : why.arg(label);
    }

    // 3. AVAILABLE. The registration's own tooltip if it named one, and otherwise the label --
    // never the *unavailable* key, which is the opposite statement and which the first attempt at
    // this function put on every enabled Viewer button by reusing one field for both.
    if (const QString specific = registry.tooltipText(id); !specific.isEmpty())
        return specific;                    // "Undo: rotate page 3 by 90 degrees" (Stage 3 T10)
    const QString tooltip = resolve(registry.tooltipKey(id));
    return tooltip.isEmpty() ? label : tooltip;
}

void ActionFactory::refresh()
{
    for (const QPointer<RawPageAction>& action : made_) {
        if (!action) continue;
        if (!registry_.isRegistered(action->id())) continue;
        action->setEnabled(registry_.isEnabled(action->id()));
        action->setChecked(registry_.isChecked(action->id()));

        // THE TOOLTIP IS RE-ASKED, not computed once at construction. Enablement changes while the
        // window is open -- a document opens, the last tab closes -- and I-53's whole point is that
        // the tooltip says which kind of disabled the control is. One computed at build time would
        // be a sentence about a state that has passed, which is worse than the generic one.
        action->setToolTip(tooltipFor(registry_, action->id(), action->text()));
    }
}

namespace {

bool isQtIntrinsicAction(const QAction* action)
{
    if (auto* menu = qobject_cast<QMenu*>(action->parent()); menu && menu->menuAction() == action)
        return true;
    if (auto* dock = qobject_cast<QDockWidget*>(action->parent());
        dock && dock->toggleViewAction() == action)
        return true;

    // A QLineEdit's clear button is a QAction as well, and it is NOT recognisable here -- measured
    // rather than assumed: `setClearButtonEnabled(true)` puts it on the line edit's private
    // side-widget list, not in `QWidget::actions()`, so an identity check of the shape above finds
    // nothing. It is handled by `adoptWidgetActions()` at the point the property is turned on
    // instead. (Issue I-22.)
    return false;
}

} // namespace

const char* ActionFactory::nonCommandMarker() { return "rawpageNonCommandItem"; }

QAction* ActionFactory::addNonCommandMenuItem(QMenu* menu, const QString& text)
{
    // The second and last construction site, and it is in the same file as the first on purpose --
    // see the header. `addAction(QString)` builds a QAction internally, which is why this call and
    // not just `new QAction` is in layer-lint's pattern list.
    QAction* item = menu->addAction(text);
    item->setProperty(nonCommandMarker(), true);
    return item;
}

RawPageAction* ActionFactory::addToMenu(QMenu* menu, ActionId id)
{
    RawPageAction* action = create(id, menu);
    menu->addAction(action);
    return action;
}

int ActionFactory::adoptWidgetActions(QWidget* widget)
{
    if (!widget) return 0;

    int stamped = 0;
    for (QAction* action : widget->findChildren<QAction*>()) {
        if (qobject_cast<RawPageAction*>(action)) continue;
        if (action->property(nonCommandMarker()).toBool()) continue;
        action->setProperty(nonCommandMarker(), true);
        ++stamped;
    }
    return stamped;
}

QAction* ActionFactory::addMenuSeparator(QMenu* menu)
{
    QAction* separator = menu->addSeparator();
    separator->setProperty(nonCommandMarker(), true);
    return separator;
}

QList<QString> ActionFactory::wiringComplaints(const QWidget* root)
{
    QList<QString> complaints;
    if (!root) return complaints;

    for (QAction* action : root->findChildren<QAction*>()) {
        auto* ours = qobject_cast<RawPageAction*>(action);

        // QT'S OWN INTRINSIC ACTIONS. Two widgets manufacture a QAction whether we want one or
        // not: QMenu's `menuAction()`, which is the action that opens the menu, and QDockWidget's
        // `toggleViewAction()`, which exists from the moment the dock does. Neither is something
        // anyone wired, neither can be prevented, and the shell has both -- the ribbon's overflow
        // chevron and the sidebar. Found by running this check against the real window for the
        // first time and being told the sidebar was a bare QAction called "Panes" (issue I-18).
        //
        // Recognised by IDENTITY -- it is *this* widget's own action -- rather than by type or
        // name, so an ordinary action that happens to sit on a menu is not excused. The gap that
        // remains: passing one of these to a command surface would go unreported, because its
        // parent does not change when it is used. Small, real, and written down.
        if (isQtIntrinsicAction(action)) continue;

        // The stamped exception -- a menu item that is navigation rather than a command. Checked by
        // PROPERTY rather than by object name or by parent type, so it cannot be satisfied by a
        // widget that merely looks similar, and so the exception stays visible in the one file that
        // is allowed to grant it.
        if (action->property(nonCommandMarker()).toBool()) continue;

        // KIND ONE -- a bare QAction. The lint would have caught this at the moment it was written,
        // so seeing it here means either the lint was bypassed or the action came from a Qt
        // convenience that builds one for you (QMenu::addAction, QToolBar::addAction). Both
        // spellings of that convenience are in the lint's pattern list for that reason.
        if (!ours) {
            complaints.append(QStringLiteral("bare QAction \"%1\" (%2) -- every action must be a "
                                             "RawPageAction built by ActionFactory")
                                  .arg(action->text(), action->objectName()));
            continue;
        }

        // KIND TWO -- a second connect on a legitimate action. THIS is why the runtime half exists:
        // the construction was correct, the lint saw nothing wrong, and the action now does two
        // things when triggered, one of which bypasses the registry and is therefore unreachable
        // from the batch runner and the CLI.
        if (!ours->isCorrectlyWired()) {
            complaints.append(QStringLiteral("RawPageAction \"%1\" has %2 receivers on triggered "
                                             "where %3 are accounted for -- a direct connection "
                                             "bypasses ActionRegistry and is unreachable headlessly")
                                  .arg(ours->objectName())
                                  .arg(ours->triggeredReceiverCount())
                                  .arg(ours->accountedReceiverCount()));
        }
    }

    return complaints;
}

} // namespace rawpage::ui
