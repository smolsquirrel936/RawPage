// ui/shell/MainWindowDecorate -- headers, footers and Bates numbers: *Add*, *Update* and *Remove* a header and footer,
// *Add* and *Remove* Bates numbering across the open documents, and the run's report. Stage 7 T14
// (`Stage7-Assembly-Plan.md` §6, D7-7, D7-14, D7-44). A translation unit of `MainWindow` for `MainWindowBookmarks.cpp`'s
// reason: one feature's registrations and handlers in one place.
//
// EVERY EDIT IS A COMMAND HANDED TO A JOURNAL (CLAUDE.md: `ui/`'s whole mutation vocabulary) -- through `runPageCommand`,
// which says on the status bar why one was refused and warns once before a signed document's first page command
// (D7-20). The dialogs collect; `core/doc/HeaderFooter` lays out; the commands decide.
//
// BATES ACROSS DOCUMENTS (D7-14) is one `core::BatesAllocator` and one `ApplyBatesCommand` per document, in the order
// chosen, each run in its OWN document's journal -- undone and saved there -- and a document whose command is refused
// gives its share back, so the next starts where it would have and the report says why that one was not numbered. The
// report is shown when a person can read it, and always logged and kept (`lastBatesReport`).
//
// THE FIELDS ARE FIXED HERE, by the host (convention 11): the date and time now, the file's name and path -- an untitled
// document's tab title and no path -- and the CJK forms from the view's input language.

#include "ui/shell/MainWindow.h"

#include "app/Services.h"
#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/DecorationCommands.h"
#include "core/doc/BatesAllocator.h"
#include "core/doc/Document.h"
#include "core/settings/ProfileStore.h"
#include "core/settings/Settings.h"
#include "ui/dialogs/HeaderFooterDialog.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/PageView.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>

#include <memory>

namespace rawpage::ui {

using core::ActionId;
using core::LogSubsystem;
using core::KeyContext;

core::FieldValues MainWindow::fieldValuesOf(const DocumentWindow& document) const
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString path = document.path();
    core::FieldValues values;
    values.date = now.date();
    values.time = QTime(now.time().hour(), now.time().minute(), now.time().second());
    values.fileName = path.isEmpty() ? document.title() : QFileInfo(path).fileName();
    values.path = path.isEmpty() ? QString() : QDir::toNativeSeparators(path);
    return values;
}

core::Status MainWindow::applyHeaderFooter(DocumentWindow* document, const core::HeaderFooterSpec& spec, bool replace)
{
    if (document == nullptr) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()};
    const core::Status status =
        runPageCommand(document, std::make_unique<core::ApplyHeaderFooterCommand>(spec, fieldValuesOf(*document), replace));
    if (status) lastHeaderFooter_ = spec;
    return status;
}

core::Status MainWindow::removeDecorations(DocumentWindow* document, core::DecorationKind kind)
{
    if (document == nullptr) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()};
    std::unique_ptr<core::Command> command;
    if (kind == core::DecorationKind::Bates) command = std::make_unique<core::RemoveBatesCommand>();
    else command = std::make_unique<core::RemoveHeaderFooterCommand>();
    const core::Command* raw = command.get();
    const core::Status status = runPageCommand(document, std::move(command));
    if (status && status_) status_->setMessage(catalog::translateText(raw->name()));
    return status;
}

std::vector<BatesReportLine> MainWindow::applyBates(const std::vector<DocumentWindow*>& order, const core::HeaderFooterSpec& spec)
{
    core::BatesAllocator run(spec.bates);
    std::vector<BatesReportLine> report;
    for (DocumentWindow* document : order) {
        if (document == nullptr || document->journal() == nullptr) continue;
        const QString name = document->title();
        BatesReportLine line{name, std::nullopt, QString()};
        core::Result<core::BatesShare> share = run.take(name, document->pageCount());
        if (!share) {
            line.refusal = catalog::translateError(QString(share.error().messageKey), share.error().context);
            report.push_back(std::move(line));
            continue;
        }
        core::HeaderFooterSpec mine = spec;
        mine.bates.start = share.value().first;
        const core::Status status = runPageCommand(document, std::make_unique<core::ApplyBatesCommand>(mine, fieldValuesOf(*document)));
        if (!status) {
            (void)run.giveBack(share.value());   // no gap: the next document starts where this one would have
            line.refusal = catalog::translateError(QString(status.error().messageKey), status.error().context);
        } else {
            line.share = share.value();
        }
        report.push_back(std::move(line));
    }
    lastBatesReport_ = batesReportText(report, spec.bates);
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "bates: run of" << static_cast<int>(order.size()) << "document(s) --" << lastBatesReport_;
    return report;
}

void MainWindow::registerDecorationActions()
{
    const auto registerOne = [this](ActionId id, core::ActionRegistry::Handler handler, core::ActionRegistry::Enable enable,
                                    const char* unavailable) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = std::move(handler);
        registration.enable = std::move(enable);
        if (unavailable != nullptr) registration.unavailableKey = unavailable;
        const core::Result<void> registered = services_.actions.register_(id, std::move(registration));
        if (!registered) {
            const std::string_view name = core::actionStringId(id);
            RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "decoration action registration refused for"
                                                << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())) << "--"
                                                << registered.error().messageKey;
        }
    };
    const auto editable = [] {
        const MainWindow* window = activeShell();
        const DocumentWindow* document = window ? window->currentDocument() : nullptr;
        return document != nullptr && document->isEditable() && document->pageCount() > 0;
    };
    const auto withDocument = [](auto act) {
        return [act](KeyContext) {
            MainWindow* window = activeShell();
            DocumentWindow* document = window ? window->currentDocument() : nullptr;
            if (window != nullptr && document != nullptr) act(*window, document);
        };
    };

    // --- headerFooter.add / headerFooter.update -- the dialog; with nobody to ask, disabled (convention 16) ------------
    for (const bool update : {false, true}) {
        registerOne(update ? ActionId::HeaderFooterUpdate : ActionId::HeaderFooterAdd,
                    withDocument([update](MainWindow& window, DocumentWindow* document) {
                        // WHERE THE FIELDS START: the last header and footer applied in this session, else the profile the
                        // settings name, else an empty spec -- in the view's CJK forms.
                        core::HeaderFooterSpec initial;
                        const QString profile = window.services_.settings.get(core::keys::documents::headerFooterProfile);
                        if (window.lastHeaderFooter_) {
                            initial = *window.lastHeaderFooter_;
                        } else if (window.services_.stamps != nullptr && !profile.isEmpty()) {
                            if (auto json = window.services_.stamps->profile(core::ProfileKind::HeaderFooter, profile)) {
                                if (auto spec = core::specFromJson(json.value())) initial = std::move(spec).value();
                            }
                        }
                        if (const PageView* view = document->view()) initial.han = view->hanPreference();
                        const core::PageTree& pages = document->journal()->document().pages();
                        const std::optional<HeaderFooterDialog::Choice> choice = HeaderFooterDialog::ask(
                            &window, update ? HeaderFooterDialog::Mode::Update : HeaderFooterDialog::Mode::Add, pages,
                            window.fieldValuesOf(*document), initial, window.services_.stamps, profile);
                        if (!choice) return;
                        if (!choice->profile.isEmpty()) window.services_.settings.set(core::keys::documents::headerFooterProfile, choice->profile);
                        (void)window.applyHeaderFooter(document, choice->spec, update);
                    }),
                    [editable] { return editable() && HeaderFooterDialog::canAsk(); }, "shell.tooltip.needsDocument");
    }

    // --- headerFooter.remove / bates.remove -- every one RawPage added, no question ------------------------------------
    registerOne(ActionId::HeaderFooterRemove,
                withDocument([](MainWindow& window, DocumentWindow* document) {
                    (void)window.removeDecorations(document, core::DecorationKind::HeaderFooter);
                }),
                editable, "shell.tooltip.needsDocument");
    registerOne(ActionId::BatesRemove,
                withDocument([](MainWindow& window, DocumentWindow* document) {
                    (void)window.removeDecorations(document, core::DecorationKind::Bates);
                }),
                editable, "shell.tooltip.needsDocument");

    // --- bates.add -- the open documents chosen and ordered, the format, and the report -------------------------------
    registerOne(ActionId::BatesAdd,
                withDocument([](MainWindow& window, DocumentWindow* current) {
                    std::vector<DocumentWindow*> open;
                    std::vector<BatesDialog::Document> listed;
                    std::size_t at = 0;
                    for (int i = 0; i < window.documentCount(); ++i) {
                        DocumentWindow* document = window.documentAt(i);
                        if (document == nullptr) continue;
                        if (document == current) at = open.size();
                        open.push_back(document);
                        listed.push_back(BatesDialog::Document{document->title(), document->pageCount(),
                                                               document->isEditable() && document->pageCount() > 0});
                    }
                    const core::Settings& settings = window.services_.settings;
                    const core::BatesFormat format{settings.get(core::keys::documents::batesPrefix),
                                                   settings.get(core::keys::documents::batesSuffix),
                                                   settings.get(core::keys::documents::batesDigits),
                                                   settings.get(core::keys::documents::batesStart)};
                    const std::optional<BatesDialog::Choice> choice = BatesDialog::ask(&window, listed, at, format);
                    if (!choice) return;
                    std::vector<DocumentWindow*> order;
                    for (const std::size_t index : choice->order) {
                        if (index < open.size()) order.push_back(open[index]);
                    }
                    core::HeaderFooterSpec spec = choice->spec;
                    if (const PageView* view = current->view()) spec.han = view->hanPreference();
                    (void)window.applyBates(order, spec);
                    if (QGuiApplication::platformName() != QLatin1String("offscreen"))
                        QMessageBox::information(&window, catalog::translate(catalog::key::DecorateBatesReportTitle), window.lastBatesReport_);
                }),
                [editable] { return editable() && BatesDialog::canAsk(); }, "shell.tooltip.needsDocument");
}

} // namespace rawpage::ui
