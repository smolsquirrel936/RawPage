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
        action->setEnabled(registry_.isEnabled(id));
        action->setChecked(registry_.isChecked(id));
    } else {
        // DECLARED BUT NOT REGISTERED. Stage 1 declares 358 ids and registers what the shell can
        // actually service, so this is the ordinary case for most of the ribbon. The button is
        // built, placed, sized and disabled -- and it says why. Leaving it out instead would make
        // the ribbon's real width and collapse behaviour a fiction until Stage 13, which is exactly
        // the thing T10 exists to get right early.
        action->setEnabled(false);
        action->setToolTip(catalog::translate(catalog::key::RibbonTooltipUnregistered)
                               .arg(action->text()));
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

void ActionFactory::refresh()
{
    for (RawPageAction* action : made_) {
        if (!action) continue;
        if (!registry_.isRegistered(action->id())) continue;
        action->setEnabled(registry_.isEnabled(action->id()));
        action->setChecked(registry_.isChecked(action->id()));
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
