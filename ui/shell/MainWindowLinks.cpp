// ui/shell/MainWindowLinks -- links and named destinations: the Link tool's rectangle, a link from selected text, a
// link's properties and its delete, URLs found in the text, a link followed with the Hand tool or `Enter`, *Verify links*,
// and the Destinations pane's create, rename and delete. Stage 7 T12 (`Stage7-Assembly-Plan.md` §6, D7-12, D7-35). A
// translation unit of `MainWindow` for `MainWindowBookmarks.cpp`'s reason: one feature's registrations and handlers in
// one place.
//
// EVERY EDIT IS A COMMAND HANDED TO A JOURNAL (CLAUDE.md: `ui/`'s whole mutation vocabulary). A link is an ANNOTATION
// (D7-12), so a new one is an `AddAnnotationCommand`, an edit an `EditAnnotationCommand` and a delete a
// `DeleteAnnotationCommand` -- Stage 6's commands, through `runLinkCommand`, which says on the status bar what was done
// or why not. A named destination's three are `core/cmd/commands/DestinationCommands`.
//
// FOLLOWING DECIDES NOTHING HERE. `core::followLink` says what a link does -- navigate, ask then open, refuse, or nothing
// and why -- and this file only asks (`LinkOpenDialog`, whose answer is NO when nobody can answer) and hands over: a web
// address or a file to `Services::openExternally`, another PDF to `openFiles`. A handler resolves the window and its
// document when it runs, never when it was registered (convention 14).

#include "ui/shell/MainWindow.h"

#include "app/Services.h"
#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/DestinationCommands.h"
#include "core/doc/Document.h"
#include "core/doc/LinkActions.h"
#include "core/doc/PageWorkRunner.h"
#include "core/text/PageTextModel.h"
#include "ui/dialogs/LinkPropertiesDialog.h"
#include "ui/panes/DestinationsPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SelectionOverlay.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPointer>

#include <map>
#include <memory>
#include <mutex>

namespace rawpage::ui {

using core::ActionId;
using core::KeyContext;
using core::LinkFollow;
using core::LogSubsystem;

namespace {

core::Error notSaid(const char* key, QString context = {})
{
    return core::Error{core::ErrorCode::Unavailable, QLatin1String(key), std::move(context)};
}

QStringList namesOf(const core::Destinations& table)
{
    QStringList out;
    for (const core::NamedDestination* entry : table.sortedByName()) out << entry->name;
    return out;
}

std::int64_t nowSeconds()
{
    return QDateTime::currentSecsSinceEpoch();
}

// The links of `document` the overlay has selected, as the store holds them now.
std::vector<core::Annotation> selectedLinks(const DocumentWindow& document)
{
    std::vector<core::Annotation> out;
    if (const AnnotationOverlay* overlay = document.annotations()) {
        for (core::Annotation& a : overlay->selected()) {
            if (a.subtype == core::AnnotationSubtype::Link) out.push_back(std::move(a));
        }
    }
    return out;
}

} // namespace

// ---- The Destinations pane, handed the model ----------------------------------------------------------------------

void MainWindow::updateDestinationsPane(DocumentWindow* document)
{
    if (!destinations_) return;
    if (document == nullptr) {
        destinations_->setDocument({}, nullptr);
        return;
    }
    // THE MODEL'S TABLE where the tab has a journal -- what every command changes, and what a page command remaps (D7-3);
    // else the backend's reading.
    const QString selected = destinations_->document() == document->handle() ? destinations_->selectedName() : QString();
    if (const core::CommandJournal* journal = document->journal())
        destinations_->setDocument(document->handle(), &journal->document().destinations());
    else
        destinations_->setDocument(document->handle(), services_.backend.destinations(document->handle()));
    if (!selected.isEmpty()) destinations_->selectName(selected);
}

core::Status MainWindow::runLinkCommand(DocumentWindow* document, std::unique_ptr<core::Command> command)
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
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "link command refused --" << status.error().messageKey << status.error().context;
        say(catalog::translate(catalog::key::LinksRefused)
                .arg(name, catalog::translateError(QString(status.error().messageKey), status.error().context)));
        return status;
    }
    say(catalog::translate(catalog::key::LinksDone).arg(name));
    if (document == currentDocument()) updateDestinationsPane(document);
    return status;
}

// ---- Making a link -------------------------------------------------------------------------------------------------

core::Status MainWindow::createLinkFromBox(DocumentWindow* document, int page, core::PdfRect box)
{
    if (document == nullptr || document->journal() == nullptr) return notSaid("error.command.noSink");
    const QString author = document->annotations() != nullptr ? document->annotations()->author() : QString();
    core::Annotation link = core::newLink(page, box, {}, core::OutlineAction{}, author, nowSeconds());
    const core::Document& model = document->journal()->document();
    const std::optional<LinkPropertiesDialog::Choice> choice =
        LinkPropertiesDialog::ask(this, link, document->pageCount(), namesOf(model.destinations()),
                                  currentViewDestination(*document).value_or(core::Destination{}));
    if (!choice) return {};   // cancelled: nothing made
    link = choice->appliedTo(std::move(link));
    return runLinkCommand(document, std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{link}));
}

core::Status MainWindow::createLinksFromSelection(DocumentWindow* document)
{
    if (document == nullptr || document->journal() == nullptr) return notSaid("error.command.noSink");
    SelectionOverlay* selection = document->selection();
    if (selection == nullptr || !selection->hasSelection()) return notSaid("error.command.noSink");
    const core::text::TextSelection& text = selection->selection();
    // A selection whose pages have not all been read yet: read now, and asked again once they are.
    const std::vector<int> missing = text.missingPages(selection->text());
    if (!selection->isResolved() || !missing.empty()) {
        for (const int page : missing) (void)selection->ensurePage(page, core::render::RenderPriority::Visible);
        if (status_) status_->setMessage(catalog::translate(catalog::key::LinksSelectionReading));
        return notSaid("links.selectionReading");
    }
    // ONE LINK PER LINE: each run of the selection a quadrilateral of its own (Stage 6's markup path), its rectangle that
    // quadrilateral's bounds.
    const QString author = document->annotations() != nullptr ? document->annotations()->author() : QString();
    std::vector<core::Annotation> made;
    for (int page = text.firstPage(); page <= text.lastPage(); ++page) {
        const core::text::PageReading* reading = selection->text().page(page);
        if (reading == nullptr) continue;
        const std::vector<double> quads = text.markupQuadsOn(*reading);
        for (std::size_t at = 0; at + 8 <= quads.size(); at += 8) {
            std::vector<double> one(quads.begin() + static_cast<std::ptrdiff_t>(at), quads.begin() + static_cast<std::ptrdiff_t>(at + 8));
            const std::optional<core::PdfRect> rect = core::boundsOfPoints(one);
            if (!rect) continue;
            made.push_back(core::newLink(page, *rect, std::move(one), core::OutlineAction{}, author, nowSeconds()));
        }
    }
    if (made.empty()) return notSaid("error.command.noSink");
    const core::Document& model = document->journal()->document();
    const std::optional<LinkPropertiesDialog::Choice> choice =
        LinkPropertiesDialog::ask(this, made.front(), document->pageCount(), namesOf(model.destinations()),
                                  currentViewDestination(*document).value_or(core::Destination{}));
    if (!choice) return {};
    for (core::Annotation& link : made) link = choice->appliedTo(std::move(link));
    return runLinkCommand(document, std::make_unique<core::AddAnnotationCommand>(std::move(made)));
}

core::Status MainWindow::editLink(DocumentWindow* document)
{
    if (document == nullptr || document->journal() == nullptr) return notSaid("error.command.noSink");
    const std::vector<core::Annotation> links = selectedLinks(*document);
    if (links.size() != 1) return notSaid("links.tooltip.needsLink");
    const core::Document& model = document->journal()->document();
    const std::optional<LinkPropertiesDialog::Choice> choice =
        LinkPropertiesDialog::ask(this, links.front(), document->pageCount(), namesOf(model.destinations()),
                                  currentViewDestination(*document).value_or(core::Destination{}));
    if (!choice) return {};
    const core::Annotation after = choice->appliedTo(links.front());
    if (after == links.front()) return {};
    return runLinkCommand(document, std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{after}));
}

// ---- Following ---------------------------------------------------------------------------------------------------

core::Status MainWindow::followLink(DocumentWindow* document, const core::AnnotationId& id)
{
    if (document == nullptr) return notSaid("error.command.noSink");
    const AnnotationOverlay* overlay = document->annotations();
    const core::AnnotationStore* store = overlay != nullptr ? overlay->store() : nullptr;
    const std::optional<std::size_t> index = store != nullptr ? store->find(id) : std::nullopt;
    if (!index) return notSaid("links.follow.noTarget");
    // The table a name is looked up in NOW: the model's, where the tab has a journal.
    const core::Destinations* table = document->journal() != nullptr ? &document->journal()->document().destinations()
                                                                     : services_.backend.destinations(document->handle());
    const core::Destinations none;
    const LinkFollow step = core::followLink(store->at(*index).action, table != nullptr ? *table : none, document->pageCount());
    lastLinkFollow_ = step;
    const auto say = [this](const QString& text) {
        if (status_) status_->setMessage(text);
    };
    // A file a link names, against the document's own folder when it is relative.
    const auto resolved = [document](const QString& file) {
        const QFileInfo info(file);
        return info.isAbsolute() ? QDir::cleanPath(file) : QDir::cleanPath(QFileInfo(document->path()).dir().filePath(file));
    };

    switch (step.step) {
    case LinkFollow::Step::Navigate:
        if (PageView* view = document->view()) view->goToDestination(step.destination);
        return {};
    case LinkFollow::Step::OpenUri:
        if (!LinkOpenDialog::ask(this, step.step, step.uri)) return {};
        if (!services_.openExternally || !services_.openExternally(step.uri)) {
            say(catalog::translateError(QStringLiteral("error.link.openFailed"), step.uri));
            return notSaid("error.link.openFailed", step.uri);
        }
        return {};
    case LinkFollow::Step::LaunchFile: {
        const QString path = resolved(step.file);
        if (!LinkOpenDialog::ask(this, step.step, path)) return {};
        if (!services_.openExternally || !services_.openExternally(path)) {
            say(catalog::translateError(QStringLiteral("error.link.openFailed"), path));
            return notSaid("error.link.openFailed", path);
        }
        return {};
    }
    case LinkFollow::Step::OpenRemote: {
        const QString path = resolved(step.file);
        if (!LinkOpenDialog::ask(this, step.step, path)) return {};
        openFiles({path});
        const int at = indexOfPath(QFileInfo(path).absoluteFilePath());
        DocumentWindow* opened = at >= 0 ? documentAt(at) : nullptr;
        if (opened == nullptr || opened->view() == nullptr) return {};   // refused at open: `openFiles` said why
        // ITS DESTINATION: the named one in the file it opened, else its page.
        if (!step.name.isEmpty()) {
            const core::Destinations* there = opened->journal() != nullptr ? &opened->journal()->document().destinations()
                                                                           : services_.backend.destinations(opened->handle());
            const core::NamedDestination* named = there != nullptr ? there->find(step.name) : nullptr;
            if (named != nullptr && named->destination.isResolved()) opened->view()->goToDestination(named->destination);
        } else if (step.destination.pageIndex >= 0 && step.destination.pageIndex < opened->pageCount()) {
            opened->view()->goToDestination(step.destination);
        }
        return {};
    }
    case LinkFollow::Step::Refused:
    case LinkFollow::Step::NotRun:
    case LinkFollow::Step::Nowhere: {
        const QString what = !step.file.isEmpty() ? step.file : !step.uri.isEmpty() ? step.uri : step.name;
        say(catalog::translateError(step.reason, what));
        return core::Error{core::ErrorCode::Unavailable, step.reason, what};
    }
    }
    return {};
}

// ---- URLs found in the text, and the verify -------------------------------------------------------------------------

core::Status MainWindow::autoDetectLinks(DocumentWindow* document)
{
    const auto refuse = [this](const core::Error& error) -> core::Status {
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return error;
    };
    if (document == nullptr || document->journal() == nullptr) return refuse(notSaid("error.command.noSink"));
    if (!pageWork_) pageWork_ = std::make_unique<core::PageWorkRunner>(services_.renderScheduler);
    if (pageWork_->isRunning()) return refuse(notSaid("error.pages.busy"));
    // EVERY PAGE'S TEXT READ ON THE RENDER THREAD (CLAUDE.md: the UI thread never parses), kept until the run is over;
    // the links made from it on the pump, as ONE command (A-84).
    struct Found {
        std::mutex mutex;
        std::map<int, core::text::PageTextModel> pages;
    };
    auto found = std::make_shared<Found>();
    core::IDocumentBackend* backend = &services_.backend;
    const core::DocumentHandle handle = document->handle();
    const core::Revision revision = document->journal()->revision();
    const QPointer<MainWindow> self(this);
    const QPointer<DocumentWindow> target(document);
    std::vector<int> pages;
    for (int page = 0; page < document->pageCount(); ++page) pages.push_back(page);
    const core::Status started = pageWork_->start(
        handle, pages,
        [backend, handle, found](int page) {
            core::text::PageTextModel model(page);
            if (core::ITextExtractor* extractor = backend->textExtractor(handle); extractor != nullptr && extractor->extractPage(handle, page, model)) {
                const std::lock_guard<std::mutex> guard(found->mutex);
                found->pages.emplace(page, std::move(model));
            }
        },
        [self, target, found, revision](core::Status done) {
            if (!self || !target) return;
            const auto refused = [&](const core::Error& error) {
                if (self->status_) self->status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
            };
            if (!done) return refused(done.error());
            if (target->journal() == nullptr || target->journal()->revision() != revision)
                return refused(notSaid("error.pages.documentChanged"));
            const QString author = target->annotations() != nullptr ? target->annotations()->author() : QString();
            std::vector<core::Annotation> made;
            {
                const std::lock_guard<std::mutex> guard(found->mutex);
                for (const auto& [page, model] : found->pages) {
                    for (core::Annotation& link : core::linksForUrls(target->journal()->document(), page, model, author, nowSeconds()))
                        made.push_back(std::move(link));
                }
            }
            if (made.empty()) {
                if (self->status_) self->status_->setMessage(catalog::translate(catalog::key::LinksAutoDetectNone));
                return;
            }
            (void)self->runLinkCommand(target, std::make_unique<core::AddAnnotationCommand>(std::move(made)));
        });
    if (!started) return refuse(started.error());
    connect(document, &QObject::destroyed, this, [this, handle] {
        if (pageWork_ && pageWork_->isRunning() && pageWork_->runningFor() == handle) pageWork_->cancel();
    }, Qt::SingleShotConnection);
    if (pageWork_->isRunning() && status_)
        status_->setMessage(catalog::translate(catalog::key::LinksAutoDetectReading).arg(static_cast<int>(pages.size())));
    return {};
}

QStringList MainWindow::verifyLinks(DocumentWindow* document)
{
    QStringList lines;
    if (document == nullptr || document->journal() == nullptr) return lines;
    const QDir folder = QFileInfo(document->path()).dir();
    // FETCHES NOTHING: a file asked of the disk, against the document's folder; a web address never asked at all.
    const std::vector<core::LinkProblem> problems = core::verifyLinks(document->journal()->document(), [&folder](const QString& file) {
        return QFileInfo::exists(QFileInfo(file).isAbsolute() ? file : folder.filePath(file));
    });
    for (const core::LinkProblem& problem : problems)
        lines << catalog::translate(QString::fromLatin1(core::linkProblemKey(problem.kind))).arg(problem.page + 1).arg(problem.target);
    lastLinkReport_ = lines;
    if (status_)
        status_->setMessage(lines.isEmpty() ? catalog::translate(catalog::key::LinksVerifyNone)
                                            : catalog::translate(catalog::key::LinksVerifyFound).arg(lines.size()));
    LinkReportDialog::show(this, lines);
    return lines;
}

// ---- Registration ------------------------------------------------------------------------------------------------

void MainWindow::registerLinkActions()
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
            RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "link action registration refused for"
                                                << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())) << "--"
                                                << registered.error().messageKey;
        }
    };
    const auto front = []() -> DocumentWindow* {
        MainWindow* window = activeShell();
        return window ? window->currentDocument() : nullptr;
    };
    // The document in front may be annotated: a journal, and its annotations read first-party (D6-3).
    const auto annotatable = [front] {
        const DocumentWindow* document = front();
        return document != nullptr && document->isEditable() && document->journal() != nullptr
               && static_cast<bool>(document->journal()->document().annotations().status()) && document->pageCount() > 0;
    };
    const auto destinationsEditable = [front] {
        const DocumentWindow* document = front();
        return document != nullptr && document->isEditable() && document->journal() != nullptr
               && static_cast<bool>(document->journal()->document().destinations().status());
    };
    // The destination the pane has selected, in the document in front.
    const auto selectedDestination = [front]() -> std::optional<std::pair<DocumentWindow*, QString>> {
        MainWindow* window = activeShell();
        DocumentWindow* document = front();
        if (window == nullptr || document == nullptr || window->destinations_ == nullptr || document->journal() == nullptr) return std::nullopt;
        if (window->destinations_->document() != document->handle()) return std::nullopt;
        const QString name = window->destinations_->selectedName();
        if (name.isEmpty() || document->journal()->document().destinations().find(name) == nullptr) return std::nullopt;
        return std::make_pair(document, name);
    };

    // --- link.create -- the Link tool, or one link per line of the text selected ----------------------------------
    registerOne(ActionId::LinkCreate,
                [](KeyContext) {
                    MainWindow* window = activeShell();
                    DocumentWindow* document = window ? window->currentDocument() : nullptr;
                    if (document == nullptr) return;
                    if (const SelectionOverlay* selection = document->selection(); selection != nullptr && selection->hasSelection()) {
                        (void)window->createLinksFromSelection(document);
                        return;
                    }
                    if (PageView* view = window->currentView()) {
                        view->setTool(ToolKind::Link);
                        window->updateViewChrome();
                    }
                },
                [annotatable] { return annotatable() && LinkPropertiesDialog::canAsk(); }, "shell.tooltip.needsDocument");

    // --- link.edit / link.delete -- the link the overlay has selected --------------------------------------------
    registerOne(ActionId::LinkEdit,
                [front](KeyContext) {
                    if (MainWindow* window = activeShell()) (void)window->editLink(front());
                },
                [front, annotatable] { return annotatable() && selectedLinks(*front()).size() == 1 && LinkPropertiesDialog::canAsk(); },
                "links.tooltip.needsLink");
    registerOne(ActionId::LinkDelete,
                [front](KeyContext) {
                    MainWindow* window = activeShell();
                    DocumentWindow* document = front();
                    if (window == nullptr || document == nullptr) return;
                    std::vector<core::AnnotationId> ids;
                    for (const core::Annotation& link : selectedLinks(*document)) ids.push_back(link.id);
                    if (ids.empty()) return;
                    (void)window->runLinkCommand(document, std::make_unique<core::DeleteAnnotationCommand>(std::move(ids)));
                },
                [front, annotatable] { return annotatable() && !selectedLinks(*front()).empty(); }, "links.tooltip.needsLink");

    // --- link.autoDetect / link.verify ---------------------------------------------------------------------------
    registerOne(ActionId::LinkAutoDetect,
                [front](KeyContext) {
                    if (MainWindow* window = activeShell()) (void)window->autoDetectLinks(front());
                },
                annotatable, "shell.tooltip.needsDocument");
    registerOne(ActionId::LinkVerify,
                [front](KeyContext) {
                    if (MainWindow* window = activeShell()) (void)window->verifyLinks(front());
                },
                [front] {
                    const DocumentWindow* document = front();
                    return document != nullptr && document->journal() != nullptr;
                },
                "shell.tooltip.needsDocument");

    // --- destination.create / rename / delete ---------------------------------------------------------------------
    registerOne(ActionId::DestinationCreate,
                [front](KeyContext) {
                    MainWindow* window = activeShell();
                    DocumentWindow* document = front();
                    if (window == nullptr || document == nullptr || document->journal() == nullptr) return;
                    const std::optional<core::Destination> view = window->currentViewDestination(*document);
                    if (!view) return;
                    const core::Destinations& table = document->journal()->document().destinations();
                    QString suggested;
                    for (int n = static_cast<int>(table.size()) + 1; suggested.isEmpty() || table.find(suggested) != nullptr; ++n)
                        suggested = catalog::translate(catalog::key::LinksDestinationDefault).arg(n);
                    const std::optional<QString> name =
                        DestinationNameDialog::ask(window, catalog::translate(catalog::key::LinksDestinationNewTitle), suggested);
                    if (!name) return;
                    core::NamedDestination entry;
                    entry.name = *name;
                    entry.destination = *view;
                    if (window->runLinkCommand(document, std::make_unique<core::CreateDestinationCommand>(entry)) && window->destinations_)
                        window->destinations_->selectName(*name);
                },
                [front, destinationsEditable] { return destinationsEditable() && front()->pageCount() > 0 && DestinationNameDialog::canAsk(); },
                "shell.tooltip.needsDocument");
    registerOne(ActionId::DestinationRename,
                [selectedDestination](KeyContext) {
                    MainWindow* window = activeShell();
                    const auto chosen = selectedDestination();
                    if (window == nullptr || !chosen) return;
                    const std::optional<QString> name =
                        DestinationNameDialog::ask(window, catalog::translate(catalog::key::LinksDestinationRenameTitle), chosen->second);
                    if (!name || *name == chosen->second) return;
                    if (window->runLinkCommand(chosen->first, std::make_unique<core::RenameDestinationCommand>(chosen->second, *name))
                        && window->destinations_)
                        window->destinations_->selectName(*name);
                },
                [destinationsEditable, selectedDestination] {
                    return destinationsEditable() && selectedDestination().has_value() && DestinationNameDialog::canAsk();
                },
                "links.tooltip.needsDestination");
    registerOne(ActionId::DestinationDelete,
                [selectedDestination](KeyContext) {
                    MainWindow* window = activeShell();
                    const auto chosen = selectedDestination();
                    if (window == nullptr || !chosen) return;
                    auto command = std::make_unique<core::DeleteDestinationCommand>(std::vector<QString>{chosen->second});
                    // The journal keeps a command it ran on its undo stack, so this stays valid once it succeeds: what the
                    // delete cleared (D7-12) is said, not left to be found.
                    const core::DeleteDestinationCommand* deleting = command.get();
                    if (!window->runLinkCommand(chosen->first, std::move(command))) return;
                    if ((!deleting->clearedLinks().empty() || deleting->clearedBookmarks() != 0) && window->status_)
                        window->status_->setMessage(catalog::translate(catalog::key::LinksDestinationCleared)
                                                        .arg(chosen->second)
                                                        .arg(static_cast<int>(deleting->clearedLinks().size()))
                                                        .arg(static_cast<int>(deleting->clearedBookmarks())));
                },
                [destinationsEditable, selectedDestination] { return destinationsEditable() && selectedDestination().has_value(); },
                "links.tooltip.needsDestination");
}

} // namespace rawpage::ui
