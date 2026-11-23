// ui/shell/MainWindowImpose -- *Booklet* and *N-up*: the document in front imposed onto sheets, as a NEW document in an
// untitled tab. Stage 7 T16 (`Stage7-Assembly-Plan.md` §6, D7-8, D7-22, D7-46). A translation unit of `MainWindow` for
// `MainWindowBookmarks.cpp`'s reason: one feature's registrations and handlers in one place.
//
// AN IMPOSITION IS NOT A COMMAND (D7-5): it changes no open document, so nothing is journalled and nothing is undone. The
// document is taken AS IT STANDS -- unsaved edits included, as an extract takes it -- through its snapshot
// (`IDocumentBackend::assemblySource`), imposed there (`AssemblySource::impose`, `pdf/ops/ImpositionOps`), and the bytes
// opened as an untitled tab (`openBytes`, `BytesOrigin::Assembly`) that a Save As keeps (Q1). It runs on the UI thread,
// as extract-to-one does: the plan is arithmetic, and the copy and the forms are the same work an extract of every page is.
//
// WHERE THE DIALOG STARTS: the last imposition made in this session, else the profile `documents/impositionProfile` names,
// else the layout's default on the paper `documents/defaultPageSize` names -- with the layout of the button pressed. The
// sheet labels' words are the catalog's (`imposition.label.*`), drawn in the view's CJK forms.

#include "ui/shell/MainWindow.h"

#include "app/Services.h"
#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/doc/Assembly.h"
#include "core/doc/Attachments.h"
#include "core/doc/Imposition.h"
#include "core/doc/PageTree.h"
#include "core/doc/PaperSize.h"
#include "core/settings/ProfileStore.h"
#include "core/settings/Settings.h"
#include "ui/dialogs/ImpositionDialog.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/PageView.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace rawpage::ui {

using core::ActionId;
using core::KeyContext;
using core::LogSubsystem;

namespace {

core::ImpositionSpec defaultImposition(core::ImpositionLayout layout, core::PdfSize paper)
{
    core::ImpositionSpec spec;
    spec.layout = layout;
    spec.sheet = paper;
    if (layout == core::ImpositionLayout::Booklet) spec.duplex = core::DuplexFlip::ShortEdge;
    return spec;
}

QString stemFor(const DocumentWindow& document)
{
    const QString path = !document.path().isEmpty() ? document.path() : document.sourcePath();
    const QString stem = QFileInfo(path).completeBaseName();
    return stem.isEmpty() ? QFileInfo(document.title()).completeBaseName() : stem;
}

QString folderFor(const DocumentWindow& document)
{
    const QString path = !document.path().isEmpty() ? document.path() : document.sourcePath();
    return path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(path).absolutePath();
}

} // namespace

core::ImpositionText MainWindow::impositionTextFor(const DocumentWindow& document) const
{
    core::ImpositionText text;
    text.labels.single = catalog::translate(catalog::key::ImpositionLabelSingle);
    text.labels.front = catalog::translate(catalog::key::ImpositionLabelFront);
    text.labels.back = catalog::translate(catalog::key::ImpositionLabelBack);
    if (const PageView* view = document.view()) text.han = view->hanPreference();
    return text;
}

core::Status MainWindow::imposeDocument(DocumentWindow* document, const core::ImpositionSpec& spec)
{
    const auto refuse = [this](const core::Error& error) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "imposition refused --" << error.messageKey << error.context;
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return core::Status(error);
    };
    if (document == nullptr || !document->handle().isValid())
        return refuse(core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()});
    core::Result<std::unique_ptr<core::AssemblySource>> source = services_.backend.assemblySource(document->handle());
    if (!source) return refuse(source.error());
    core::Result<core::ImposedDocument> imposed = source.value()->impose(spec, impositionTextFor(*document));
    if (!imposed) return refuse(imposed.error());
    const QString suggested = QDir(folderFor(*document))
                                  .filePath(core::safeFileName(catalog::translate(catalog::key::ImpositionSuggestedName).arg(stemFor(*document)))
                                            + QLatin1String(".pdf"));
    core::Result<core::DocumentHandle> opened = services_.backend.openBytes(imposed.value().bytes, catalog::translate(catalog::key::DocumentUntitled),
                                                                            core::PasswordCallback{}, core::BytesOrigin::Assembly);
    if (!opened) return refuse(opened.error());
    lastImposition_ = spec;
    adoptDocument(new DocumentWindow(services_, opened.value(), DocumentWindow::Assembled{suggested}));
    updateDocumentChrome();
    QString told = catalog::translate(catalog::key::ImpositionDone)
                       .arg(imposed.value().sourcePages)
                       .arg(imposed.value().sheets)
                       .arg(imposed.value().pageCount);
    if (imposed.value().annotationsNotDrawn > 0)
        told += QLatin1Char(' ') + catalog::translate(catalog::key::ImpositionAnnotationsNotDrawn).arg(imposed.value().annotationsNotDrawn);
    if (status_) status_->setMessage(told);
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "imposition:" << QString::fromUtf8(core::impositionLayoutName(spec.layout).data())
                                       << imposed.value().sourcePages << "page(s) on" << imposed.value().sheets << "sheet(s),"
                                       << imposed.value().pageCount << "side(s)," << imposed.value().blanks << "blank(s),"
                                       << imposed.value().annotationsNotDrawn << "annotation(s) not drawn; opened untitled as handle"
                                       << static_cast<int>(opened.value().id);
    return {};
}

void MainWindow::registerImpositionActions()
{
    const auto registerOne = [this](ActionId id, core::ActionRegistry::Handler handler, std::function<bool()> enable) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = std::move(handler);
        registration.enable = std::move(enable);
        registration.unavailableKey = "shell.tooltip.needsDocument";
        const core::Result<void> registered = services_.actions.register_(id, std::move(registration));
        if (!registered) {
            const std::string_view name = core::actionStringId(id);
            RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "imposition action registration refused for"
                                                << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())) << "--"
                                                << registered.error().messageKey;
        }
    };
    // A document with pages -- read-only ones too: an imposition changes nothing -- and somebody to ask (convention 16).
    const auto enabled = [] {
        const MainWindow* window = activeShell();
        const DocumentWindow* document = window ? window->currentDocument() : nullptr;
        return document != nullptr && document->pageCount() > 0 && ImpositionDialog::canAsk();
    };
    for (const auto layout : {core::ImpositionLayout::Booklet, core::ImpositionLayout::NUp}) {
        registerOne(layout == core::ImpositionLayout::Booklet ? ActionId::ImpositionBooklet : ActionId::ImpositionNUp,
                    [layout](KeyContext) {
                        MainWindow* window = activeShell();
                        DocumentWindow* document = window ? window->currentDocument() : nullptr;
                        if (window == nullptr || document == nullptr) return;
                        const core::PageTree* pages = window->services_.backend.pages(document->handle());
                        if (pages == nullptr || pages->count() == 0) return;
                        const QString profile = window->services_.settings.get(core::keys::documents::impositionProfile);
                        core::ImpositionSpec initial =
                            defaultImposition(layout, core::paperSizePoints(window->services_.settings.get(core::keys::documents::defaultPageSize)));
                        if (window->lastImposition_) {
                            initial = *window->lastImposition_;
                        } else if (window->services_.stamps != nullptr && !profile.isEmpty()) {
                            if (auto json = window->services_.stamps->profile(core::ProfileKind::Imposition, profile)) {
                                if (auto spec = core::impositionFromJson(json.value())) initial = std::move(spec).value();
                            }
                        }
                        initial.layout = layout;   // the button pressed decides the layout
                        std::vector<core::PdfSize> shown;
                        shown.reserve(static_cast<std::size_t>(pages->count()));
                        for (int at = 0; at < pages->count(); ++at) shown.push_back(pages->at(at)->displaySize());
                        const std::optional<ImpositionDialog::Choice> choice =
                            ImpositionDialog::ask(window, initial, std::move(shown), window->services_.stamps, profile);
                        if (!choice) return;
                        if (!choice->profile.isEmpty()) window->services_.settings.set(core::keys::documents::impositionProfile, choice->profile);
                        (void)window->imposeDocument(document, choice->spec);
                    },
                    enabled);
    }
}

} // namespace rawpage::ui
