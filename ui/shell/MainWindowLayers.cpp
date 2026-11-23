// ui/shell/MainWindowLayers -- the Layers pane's commands and its eye. Stage 7 T17 (`Stage7-Assembly-Plan.md` §6, D7-9).
// A translation unit of `MainWindow` for `MainWindowBookmarks.cpp`'s reason: one feature's registrations in one place.
//
// TWO KINDS OF CHANGE, KEPT APART (D7-9). Showing and hiding a layer is VIEW STATE: `DocumentWindow::setLayerShown`, no
// command, no journal, nothing dirtied -- `layer.show`, `layer.hide` and the pane's eye. Everything else is a COMMAND
// handed to the tab's journal through `runLayerCommand`, which says on the status bar what was done or why not:
// `layer.create`, `layer.rename` (typed in place in the pane), `layer.lock`, `layer.merge`, `layer.delete` and
// `layer.properties` -- `core/cmd/commands/StructureCommands`. `layer.flatten` stays unregistered: flattening rewrites
// content, which is Stage 10's (Q2). A handler resolves the window and its document when it runs, never when it was
// registered (convention 14), and an action that needs a person is disabled with nobody to ask (convention 16).

#include "ui/shell/MainWindow.h"

#include "app/Services.h"
#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/StructureCommands.h"
#include "core/doc/Document.h"
#include "core/doc/LayerTree.h"
#include "ui/dialogs/LayerDialogs.h"
#include "ui/panes/LayersPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/StatusBar.h"

#include <QUuid>

namespace rawpage::ui {

using core::ActionId;
using core::KeyContext;
using core::LogSubsystem;

namespace {

core::Error notSaid(const char* key, QString context = {})
{
    return core::Error{core::ErrorCode::Unavailable, QLatin1String(key), std::move(context)};
}

} // namespace

void MainWindow::updateLayersPane(DocumentWindow* document)
{
    if (!layers_) return;
    if (document == nullptr) {
        layers_->setDocument({}, nullptr, {}, false);
        return;
    }
    const core::LayerTree* tree = document->layerTree();
    const bool editable = document->isEditable() && document->journal() != nullptr && tree != nullptr && static_cast<bool>(tree->status());
    layers_->setDocument(document->handle(), tree, document->hiddenLayers(), editable);
}

core::Status MainWindow::runLayerCommand(DocumentWindow* document, std::unique_ptr<core::Command> command)
{
    const auto say = [this](const QString& text) {
        if (status_) status_->setMessage(text);
    };
    if (document == nullptr || document->journal() == nullptr || !command) {
        const core::Error error = notSaid("error.command.noSink");
        say(catalog::translateError(QString(error.messageKey), error.context));
        return error;
    }
    const QString name = catalog::translateText(command->name());
    const core::Status status = document->journal()->run(std::move(command));
    if (!status) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "layer command refused --" << status.error().messageKey << status.error().context;
        say(catalog::translate(catalog::key::LayersRefused)
                .arg(name, catalog::translateError(QString(status.error().messageKey), status.error().context)));
        return status;
    }
    say(catalog::translate(catalog::key::LayersDone).arg(name));
    if (document == currentDocument()) updateLayersPane(document);
    return status;
}

core::Status MainWindow::setLayerShown(DocumentWindow* document, const core::LayerId& id, bool shown)
{
    if (document == nullptr) return notSaid("error.layer.viewNotShown");
    const core::Status status = document->setLayerShown(id, shown);
    if (!status && status_) {
        status_->setMessage(catalog::translate(catalog::key::LayersViewRefused)
                                .arg(catalog::translateError(QString(status.error().messageKey), status.error().context)));
    }
    if (document == currentDocument()) updateLayersPane(document);
    return status;
}

void MainWindow::registerLayerActions()
{
    const auto registerOne = [this](ActionId id, std::function<void(KeyContext)> handler, std::function<bool()> enable,
                                    const char* unavailable) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = std::move(handler);
        registration.enable = std::move(enable);
        if (unavailable != nullptr) registration.unavailableKey = unavailable;
        const core::Result<void> registered = services_.actions.register_(id, std::move(registration));
        if (!registered) {
            const std::string_view name = core::actionStringId(id);
            RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "layer action registration refused for"
                                                << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())) << "--"
                                                << registered.error().messageKey;
        }
    };
    const auto front = []() -> DocumentWindow* {
        MainWindow* window = activeShell();
        return window ? window->currentDocument() : nullptr;
    };
    // The layers of the document in front may be edited: a journal, and a tree read whole.
    const auto editable = [front] {
        const DocumentWindow* document = front();
        return document != nullptr && document->isEditable() && document->journal() != nullptr
               && static_cast<bool>(document->journal()->document().layers().status());
    };
    // The group the pane has selected, in the document in front, and its layer.
    struct Chosen {
        DocumentWindow* document = nullptr;
        core::LayerId id;
        const core::Layer* layer = nullptr;
    };
    const auto chosen = [front]() -> std::optional<Chosen> {
        MainWindow* window = activeShell();
        DocumentWindow* document = front();
        if (window == nullptr || document == nullptr || window->layers_ == nullptr || window->layers_->document() != document->handle())
            return std::nullopt;
        const std::optional<core::LayerId> id = window->layers_->selectedLayer();
        const core::LayerTree* tree = document->layerTree();
        const core::Layer* layer = id && tree != nullptr ? tree->find(*id) : nullptr;
        if (layer == nullptr || layer->decoration) return std::nullopt;
        return Chosen{document, *id, layer};
    };

    // --- pane.layers -- `Ctrl+Shift+L` (Features.md §8, Global) -----------------------------------------------------
    registerOne(ActionId::PaneLayers,
                [](KeyContext) {
                    MainWindow* window = activeShell();
                    if (!window || !window->sidebar()) return;
                    window->sidebar()->setAutoHidden(false);
                    window->sidebar()->show();
                    window->sidebar()->showPane(ActionId::PaneLayers);
                    if (QWidget* pane = window->sidebar()->paneWidget(ActionId::PaneLayers)) pane->setFocus(Qt::OtherFocusReason);
                },
                {}, nullptr);

    // --- layer.show / layer.hide -- VIEW STATE: no command (D7-9) ---------------------------------------------------
    const auto shownNow = [](const Chosen& c) { return c.document->hiddenLayers().count(c.id) == 0; };
    registerOne(ActionId::LayerShow,
                [chosen](KeyContext) {
                    const auto c = chosen();
                    if (MainWindow* window = activeShell(); window != nullptr && c) (void)window->setLayerShown(c->document, c->id, true);
                },
                [chosen, shownNow] {
                    const auto c = chosen();
                    return c && !c->layer->locked && !shownNow(*c);
                },
                "layers.tooltip.needsHidden");
    registerOne(ActionId::LayerHide,
                [chosen](KeyContext) {
                    const auto c = chosen();
                    if (MainWindow* window = activeShell(); window != nullptr && c) (void)window->setLayerShown(c->document, c->id, false);
                },
                [chosen, shownNow] {
                    const auto c = chosen();
                    return c && !c->layer->locked && shownNow(*c);
                },
                "layers.tooltip.needsShown");

    // --- layer.create -- minted (plan §4): a new, empty group at the top level ---------------------------------------
    registerOne(ActionId::LayerCreate,
                [front](KeyContext) {
                    MainWindow* window = activeShell();
                    DocumentWindow* document = front();
                    if (window == nullptr || document == nullptr || document->journal() == nullptr) return;
                    const core::LayerTree& tree = document->journal()->document().layers();
                    QString suggested;
                    for (int n = static_cast<int>(tree.visibleCount()) + 1; suggested.isEmpty(); ++n) {
                        const QString candidate = catalog::translate(catalog::key::LayersDefaultName).arg(n);
                        if (std::none_of(tree.layers().begin(), tree.layers().end(), [&](const core::Layer& l) { return l.name == candidate; }))
                            suggested = candidate;
                    }
                    const std::optional<QString> name = LayerNameDialog::ask(window, catalog::translate(catalog::key::LayersNewTitle), suggested);
                    if (!name) return;
                    core::Layer layer;
                    layer.id = core::LayerId::ofCreated(QUuid::createUuid().toString(QUuid::WithoutBraces));
                    layer.name = *name;
                    const core::LayerId id = layer.id;
                    if (window->runLayerCommand(document, std::make_unique<core::CreateLayerCommand>(std::move(layer), core::LayerId{})) &&
                        window->layers_)
                        window->layers_->selectLayer(id);
                },
                [editable] { return editable() && LayerNameDialog::canAsk(); }, "shell.tooltip.needsDocument");

    // --- layer.rename -- minted: the pane's in-place edit; what is typed comes back as `renameRequested` ------------
    registerOne(ActionId::LayerRename,
                [chosen](KeyContext) {
                    MainWindow* window = activeShell();
                    if (window == nullptr || !chosen() || window->layers_ == nullptr) return;
                    window->sidebar()->showPane(ActionId::PaneLayers);
                    window->layers_->renameSelected();
                },
                [editable, chosen] { return editable() && chosen().has_value(); }, "layers.tooltip.needsLayer");

    // --- layer.lock -- the lock turned over: a property, so a command -------------------------------------------------
    registerOne(ActionId::LayerLock,
                [chosen](KeyContext) {
                    MainWindow* window = activeShell();
                    const auto c = chosen();
                    if (window == nullptr || !c) return;
                    core::LayerProperties properties = core::LayerProperties::of(*c->document->layerTree(), c->id);
                    properties.locked = !properties.locked;
                    (void)window->runLayerCommand(c->document, std::make_unique<core::SetLayerPropertiesCommand>(c->id, std::move(properties)));
                },
                [editable, chosen] { return editable() && chosen().has_value(); }, "layers.tooltip.needsLayer");

    // --- layer.merge -- into the group a person names -----------------------------------------------------------------
    registerOne(ActionId::LayerMerge,
                [chosen](KeyContext) {
                    MainWindow* window = activeShell();
                    const auto c = chosen();
                    if (window == nullptr || !c) return;
                    const std::optional<core::LayerId> into = LayerMergeDialog::ask(window, *c->document->layerTree(), c->id);
                    if (!into) return;
                    if (window->runLayerCommand(c->document, std::make_unique<core::MergeLayersCommand>(std::vector<core::LayerId>{c->id}, *into)) &&
                        window->layers_)
                        window->layers_->selectLayer(*into);
                },
                [editable, chosen] {
                    const auto c = chosen();
                    return editable() && c && LayerMergeDialog::canAsk() && !LayerMergeDialog::choices(*c->document->layerTree(), c->id).empty();
                },
                "layers.tooltip.needsTwo");

    // --- layer.delete -- minted: the group gone, its content orphaned (D7-9) ----------------------------------------
    registerOne(ActionId::LayerDelete,
                [chosen](KeyContext) {
                    MainWindow* window = activeShell();
                    const auto c = chosen();
                    if (window == nullptr || !c) return;
                    if (window->runLayerCommand(c->document, std::make_unique<core::DeleteLayerCommand>(std::vector<core::LayerId>{c->id})) &&
                        window->status_)
                        window->status_->setMessage(catalog::translate(catalog::key::LayersDeleted).arg(c->layer->name));
                },
                [editable, chosen] { return editable() && chosen().has_value(); }, "layers.tooltip.needsLayer");

    // --- layer.properties -----------------------------------------------------------------------------------------------
    registerOne(ActionId::LayerProperties,
                [chosen](KeyContext) {
                    MainWindow* window = activeShell();
                    const auto c = chosen();
                    if (window == nullptr || !c) return;
                    const core::LayerTree& tree = *c->document->layerTree();
                    const std::optional<core::LayerProperties> properties = LayerPropertiesDialog::ask(window, tree, c->id);
                    if (!properties || *properties == core::LayerProperties::of(tree, c->id)) return;
                    (void)window->runLayerCommand(c->document, std::make_unique<core::SetLayerPropertiesCommand>(c->id, *properties));
                },
                [editable, chosen] { return editable() && chosen().has_value() && LayerPropertiesDialog::canAsk(); },
                "layers.tooltip.needsLayer");
}

} // namespace rawpage::ui
