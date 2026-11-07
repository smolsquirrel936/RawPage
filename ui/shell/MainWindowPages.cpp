// ui/shell/MainWindowPages -- the thumbnails pane's edits: the page actions §8's Page Thumbnails table names, the drag,
// the clipboard and the insert, delete and replace dialogs. Stage 7 T8 (`Stage7-Assembly-Plan.md` §6, D7-13, D7-20).
// A translation unit of `MainWindow` for `MainWindowExchange.cpp`'s reason: one feature's registrations and handlers
// in one place, the window's own file already the size it is.
//
// EVERY HANDLER IS A COMMAND HANDED TO A JOURNAL (CLAUDE.md: `ui/`'s whole mutation vocabulary), and every one goes
// through `runPageCommand`, so D7-20's warning and the report of what a command did to references (D7-2) are said in
// one place for every surface -- a key, the ribbon, the tool finder, a drag, a paste.
//
// THE PAGES ARE THE SET (D7-13): `selectedPagesOf` -- the thumbnails' set when the pane shows the document, else the
// view's current page. A handler resolves the window and its document when it runs, never when it was registered
// (convention 14); a command that needs a person is DISABLED where there is none (convention 16).

#include "ui/shell/MainWindow.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Assembly.h"
#include "core/doc/CopiedPages.h"
#include "core/doc/Document.h"
#include "core/doc/PageMap.h"
#include "core/doc/PageRanges.h"
#include "core/doc/PageTree.h"
#include "core/settings/Settings.h"
#include "ui/panes/ThumbnailsPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MessageBar.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ViewportController.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QStringList>

#include <algorithm>
#include <map>

namespace rawpage::ui {

using core::ActionId;
using core::KeyContext;
using core::LogSubsystem;

namespace {

// What a page command did to references (D7-2), or nothing: the five commands that report.
const core::PageCommandReport* reportOf(const core::Command* command)
{
    if (const auto* move = dynamic_cast<const core::MovePagesCommand*>(command)) return &move->report();
    if (const auto* deleted = dynamic_cast<const core::DeletePagesCommand*>(command)) return &deleted->report();
    if (const auto* duplicate = dynamic_cast<const core::DuplicatePagesCommand*>(command)) return &duplicate->report();
    if (const auto* insert = dynamic_cast<const core::InsertPagesCommand*>(command)) return &insert->report();
    if (const auto* replace = dynamic_cast<const core::ReplacePagesCommand*>(command)) return &replace->report();
    return nullptr;
}

// THE REPORT IN WORDS: per kind, how many and what was done to them -- *Bookmarks: 1 cleared; Named destinations: 1
// removed*. Each reference, with its name and object, is in the log (`PageCommandReport`'s names are diagnostic).
QString describe(const core::PageCommandReport& report)
{
    std::map<std::pair<int, int>, int> counted;
    for (const core::PageReference& reference : report.references)
        ++counted[{static_cast<int>(reference.kind), static_cast<int>(reference.treatment)}];
    QStringList parts;
    for (const auto& [key, count] : counted) {
        const auto kind = static_cast<core::PageReference::Kind>(key.first);
        const auto treatment = static_cast<core::PageReference::Treatment>(key.second);
        parts << catalog::translate(catalog::key::PagesReportEntry)
                     .arg(catalog::translate(QStringLiteral("pages.reference.") + QLatin1String(core::pageReferenceKindName(kind))),
                          catalog::translate(QStringLiteral("pages.treatment.") + QLatin1String(core::pageReferenceTreatmentName(treatment)))
                              .arg(count));
    }
    return parts.join(QStringLiteral("; "));
}

// The pages a set of `pages` lands on moved one place by `step`: the set's leading edge one further, gathered there.
// Nothing for a move that changes nothing.
std::optional<core::PageMap> steppedMap(int count, const std::vector<int>& pages, int step)
{
    if (pages.empty() || count <= 0) return std::nullopt;
    const int n = static_cast<int>(pages.size());
    const int to = step < 0 ? pages.front() - 1 : pages.back() + 1 - (n - 1);
    std::optional<core::PageMap> map = core::PageMap::move(count, pages, std::clamp(to, 0, count - n));
    if (!map || map->isIdentity()) return std::nullopt;
    return map;
}

int stepTarget(int count, const std::vector<int>& pages, int step)
{
    const int n = static_cast<int>(pages.size());
    const int to = step < 0 ? pages.front() - 1 : pages.back() + 1 - (n - 1);
    return std::clamp(to, 0, count - n);
}

} // namespace

std::vector<int> MainWindow::selectedPagesOf(const DocumentWindow& document) const
{
    if (thumbnails_ != nullptr && thumbnails_->document() == document.handle()) {
        std::vector<int> pages = thumbnails_->commandPages();
        if (!pages.empty()) return pages;
    }
    const int current = document.view() ? document.view()->controller().currentPage() : 0;
    return document.pageCount() > 0 ? std::vector<int>{std::clamp(current, 0, document.pageCount() - 1)} : std::vector<int>{};
}

core::Status MainWindow::runPageCommand(DocumentWindow* document, std::unique_ptr<core::Command> command)
{
    const auto say = [this](const QString& text) {
        if (status_) status_->setMessage(text);
    };
    if (document == nullptr || document->journal() == nullptr || !command) {
        const core::Error error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()};
        say(catalog::translateError(QString(error.messageKey), error.context));
        return error;
    }

    // D7-20: SAID BEFORE THE FIRST, AND ONCE. The banner is a condition the document keeps, so a tab switch away and back
    // shows it again and a second command does not raise it twice.
    const int signatures = services_.backend.signatureCount(document->handle());
    const int signedKind = static_cast<int>(MessageBar::Kind::Signed);
    if (signatures > 0 && document->conditions().count(signedKind) == 0) {
        const QString text = catalog::translate(catalog::key::PagesSignedWarning).arg(signatures);
        document->setCondition(signedKind, text);
        if (messageBar_ && document == currentDocument()) messageBar_->showMessage({MessageBar::Kind::Signed, text, {}, {}});
        RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "pages: warned that" << signatures << "signature(s) of" << document->title()
                                           << "will read as modified";
    }

    const core::Command* raw = command.get();
    const QString name = catalog::translateText(command->name());
    const core::Status status = document->journal()->run(std::move(command));
    if (!status) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "page command refused --" << status.error().messageKey << status.error().context;
        say(catalog::translate(catalog::key::PagesRefused).arg(name, catalog::translateError(QString(status.error().messageKey),
                                                                                             status.error().context)));
        return status;
    }
    // THE COMMAND LIVES IN THE JOURNAL'S HISTORY NOW, so its report can be read; a refusal above never reaches here.
    const core::PageCommandReport* report = reportOf(raw);
    if (report != nullptr && !report->isEmpty()) {
        for (const core::PageReference& reference : report->references)
            RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "pages: reference" << core::pageReferenceKindName(reference.kind)
                                               << core::pageReferenceTreatmentName(reference.treatment) << reference.name
                                               << "object" << reference.object << "page" << reference.page;
        say(catalog::translate(catalog::key::PagesDoneReported).arg(name, describe(*report)));
    } else {
        say(catalog::translate(catalog::key::PagesDone).arg(name));
    }
    return status;
}

core::Status MainWindow::deletePages(DocumentWindow* document, const std::vector<int>& pages)
{
    return runPageCommand(document, std::make_unique<core::DeletePagesCommand>(pages));
}

core::Status MainWindow::stepPages(DocumentWindow* document, const std::vector<int>& pages, int step)
{
    if (document == nullptr) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()};
    if (!steppedMap(document->pageCount(), pages, step)) return {};   // already there: nothing to undo
    return runPageCommand(document, std::make_unique<core::MovePagesCommand>(pages, stepTarget(document->pageCount(), pages, step)));
}

core::Status MainWindow::duplicatePages(DocumentWindow* document, const std::vector<int>& pages)
{
    return runPageCommand(document, std::make_unique<core::DuplicatePagesCommand>(pages));
}

core::Status MainWindow::movePagesTo(DocumentWindow* document, const std::vector<int>& pages, int insertionIndex)
{
    if (document == nullptr || pages.empty())
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()};
    // BEFORE PAGE n, AS THE PANE COUNTED: the pages that were above the marker leave, so the set lands at the marker less
    // them -- `MovePagesCommand`'s `to` counts the document after the move.
    const int count = document->pageCount();
    const int above = static_cast<int>(std::count_if(pages.begin(), pages.end(), [insertionIndex](int page) { return page < insertionIndex; }));
    const int to = std::clamp(insertionIndex - above, 0, std::max(0, count - static_cast<int>(pages.size())));
    const std::optional<core::PageMap> map = core::PageMap::move(count, pages, to);
    if (!map || map->isIdentity()) return {};   // dropped where they were: nothing changes, nothing to undo
    return runPageCommand(document, std::make_unique<core::MovePagesCommand>(pages, to));
}

core::Status MainWindow::dropPages(DocumentWindow* document, core::DocumentHandle source, const std::vector<int>& pages,
                                   int insertionIndex)
{
    const auto refuse = [this](const core::Error& error) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "page drop refused --" << error.messageKey << error.context;
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return core::Status(error);
    };
    if (document == nullptr) return refuse(core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()});
    // THE SOURCE AS IT STANDS AT THE DROP -- its unsaved edits included -- copied once; the source changes in nothing.
    core::Result<std::unique_ptr<core::AssemblySource>> snapshot = services_.backend.assemblySource(source);
    if (!snapshot) return refuse(snapshot.error());
    core::Result<core::CopiedPages> copied = snapshot.value()->copy(pages);
    if (!copied) return refuse(copied.error());
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "pages: dropped" << copied.value().pageCount << "page(s) of handle"
                                       << static_cast<int>(source.id) << "into" << document->title() << "at" << insertionIndex;
    return runPageCommand(document, std::make_unique<core::InsertPagesCommand>(
                                        std::clamp(insertionIndex, 0, document->pageCount()), std::move(copied).value()));
}

core::Status MainWindow::copyPages(DocumentWindow* document, const std::vector<int>& pages)
{
    const auto refuse = [this](const core::Error& error) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "page copy refused --" << error.messageKey << error.context;
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return core::Status(error);
    };
    if (document == nullptr || pages.empty())
        return refuse(core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()});
    core::Result<std::unique_ptr<core::AssemblySource>> snapshot = services_.backend.assemblySource(document->handle());
    if (!snapshot) return refuse(snapshot.error());
    core::Result<core::CopiedPages> copied = snapshot.value()->copy(pages);
    if (!copied) return refuse(copied.error());
    auto* mime = new QMimeData;
    mime->setData(QLatin1String(core::kPagesMimeType), core::encodeCopiedPages(copied.value()));
    QGuiApplication::clipboard()->setMimeData(mime);
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "pages: copied" << copied.value().pageCount << "page(s) of" << document->title() << "--"
                                       << copied.value().graph.size() << "bytes";
    if (status_) status_->setMessage(catalog::translate(catalog::key::PagesCopied).arg(copied.value().pageCount));
    return {};
}

core::Status MainWindow::cutPages(DocumentWindow* document, const std::vector<int>& pages)
{
    // A CUT IS A COPY AND ONE DELETE (D7-13, D6-22's shape): the delete only once the copy is on the clipboard.
    if (const core::Status copied = copyPages(document, pages); !copied) return copied;
    return deletePages(document, pages);
}

int MainWindow::clipboardPageCount()
{
    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* held = clipboard != nullptr ? clipboard->mimeData() : nullptr;
    if (held == nullptr || !held->hasFormat(QLatin1String(core::kPagesMimeType))) return 0;
    const core::Result<core::CopiedPages> pages = core::decodeCopiedPages(held->data(QLatin1String(core::kPagesMimeType)));
    return pages ? pages.value().pageCount : 0;
}

core::Status MainWindow::pastePages(DocumentWindow* document, int at)
{
    const auto refuse = [this](const core::Error& error) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "page paste refused --" << error.messageKey << error.context;
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return core::Status(error);
    };
    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* held = clipboard != nullptr ? clipboard->mimeData() : nullptr;
    if (document == nullptr || held == nullptr || !held->hasFormat(QLatin1String(core::kPagesMimeType)))
        return refuse(core::Error{core::ErrorCode::Unavailable, QLatin1String("error.clipboard.noPages"), QString()});
    // SOMEONE ELSE'S BYTES, maybe another process's: bounded and checked before a command exists (`decodeCopiedPages`).
    core::Result<core::CopiedPages> pages = core::decodeCopiedPages(held->data(QLatin1String(core::kPagesMimeType)));
    if (!pages) return refuse(pages.error());
    return runPageCommand(document, std::make_unique<core::InsertPagesCommand>(std::clamp(at, 0, document->pageCount()),
                                                                               std::move(pages).value()));
}

core::Status MainWindow::insertPages(DocumentWindow* document, const InsertPagesDialog::Choice& choice)
{
    const auto refuse = [this](const core::Error& error) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "insert refused --" << error.messageKey << error.context;
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return core::Status(error);
    };
    if (document == nullptr) return refuse(core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()});
    core::Result<core::CopiedPages> copied = core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pages.noFile"), QString()};
    switch (choice.source) {
    case InsertPagesDialog::Source::File: {
        // THE FILE IS COUNTED FIRST, so a range typed against it -- `3-`, or none for every page -- means its pages.
        const core::Result<int> count = services_.backend.filePageCount(choice.path);
        if (!count) return refuse(count.error());
        std::vector<int> pages;
        if (choice.range.isEmpty()) {
            for (int page = 0; page < count.value(); ++page) pages.push_back(page);
        } else {
            core::Result<std::vector<int>> parsed = core::parsePageRanges(choice.range, count.value());
            if (!parsed) return refuse(parsed.error());
            pages = std::move(parsed).value();
        }
        copied = services_.backend.copyPages(choice.path, pages);
        break;
    }
    case InsertPagesDialog::Source::Blank:
        copied = services_.backend.blankPages(choice.blankSize, choice.blankCount);
        break;
    case InsertPagesDialog::Source::Clipboard:
        return pastePages(document, choice.at);
    }
    if (!copied) return refuse(copied.error());
    return runPageCommand(document, std::make_unique<core::InsertPagesCommand>(std::clamp(choice.at, 0, document->pageCount()),
                                                                               std::move(copied).value()));
}

core::Status MainWindow::replacePages(DocumentWindow* document, const ReplacePagesDialog::Choice& choice)
{
    const auto refuse = [this](const core::Error& error) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "replace refused --" << error.messageKey << error.context;
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return core::Status(error);
    };
    if (document == nullptr || choice.pages.empty())
        return refuse(core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()});
    const core::Result<int> count = services_.backend.filePageCount(choice.path);
    if (!count) return refuse(count.error());
    // AS MANY AS ARE REPLACED, from the first page asked: a file too short to give them is refused by the command's own key.
    const int n = static_cast<int>(choice.pages.size());
    if (choice.sourceFirst < 0 || choice.sourceFirst + n > count.value())
        return refuse(core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.edit.replaceCount"),
                                  QString::number(count.value())});
    std::vector<int> source;
    for (int page = choice.sourceFirst; page < choice.sourceFirst + n; ++page) source.push_back(page);
    core::Result<core::CopiedPages> copied = services_.backend.copyPages(choice.path, source);
    if (!copied) return refuse(copied.error());
    return runPageCommand(document, std::make_unique<core::ReplacePagesCommand>(choice.pages, std::move(copied).value()));
}

void MainWindow::updatePageBadges()
{
    if (thumbnails_ == nullptr) return;
    const DocumentWindow* document = currentDocument();
    const core::CommandJournal* journal = document != nullptr ? document->journal() : nullptr;
    const core::AnnotationStore* store = journal != nullptr ? &journal->document().annotations()
                                         : (document != nullptr ? services_.backend.annotations(document->handle()) : nullptr);
    std::vector<core::AnnotationStore::PageCarries> badges;
    if (store != nullptr && store->status()) {
        badges.reserve(static_cast<std::size_t>(std::max(0, store->pageCount())));
        for (int page = 0; page < store->pageCount(); ++page) badges.push_back(store->carries(page));
    }
    thumbnails_->setBadges(std::move(badges));
}

void MainWindow::registerPageActions()
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
            RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "page action registration refused for"
                                                << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())) << "--"
                                                << registered.error().messageKey;
        }
    };
    // The document in front and the pages a command means -- resolved when the handler runs (convention 14).
    const auto front = []() -> DocumentWindow* {
        MainWindow* window = activeShell();
        return window ? window->currentDocument() : nullptr;
    };
    const auto editable = [front] {
        const DocumentWindow* document = front();
        return document != nullptr && document->isEditable() && document->pageCount() > 0;
    };
    const auto pagesInFront = [front]() -> std::vector<int> {
        MainWindow* window = activeShell();
        const DocumentWindow* document = front();
        return window != nullptr && document != nullptr ? window->selectedPagesOf(*document) : std::vector<int>{};
    };
    const auto withDocument = [](auto act) {
        return [act](KeyContext context) {
            MainWindow* window = activeShell();
            DocumentWindow* document = window ? window->currentDocument() : nullptr;
            if (window != nullptr && document != nullptr) act(*window, document, context);
        };
    };

    // --- page.selectAll (`Ctrl+A`) -----------------------------------------------------------------------------
    registerOne(ActionId::PageSelectAll,
                [](KeyContext) {
                    MainWindow* window = activeShell();
                    if (window != nullptr && window->thumbnails_ != nullptr) window->thumbnails_->selectAll();
                },
                [] {
                    const MainWindow* window = activeShell();
                    const DocumentWindow* document = window ? window->currentDocument() : nullptr;
                    return document != nullptr && window->thumbnails_ != nullptr && window->thumbnails_->document() == document->handle()
                           && document->pageCount() > 0;
                },
                "shell.tooltip.needsDocument");

    // --- page.delete (`Delete`) --------------------------------------------------------------------------------
    // IN THE PANE, THE SET, NO QUESTION (§8: *Delete the selected pages*). From anywhere else -- the ribbon, the tool
    // finder -- the dialog, starting at the set; with nobody to ask, the set.
    registerOne(ActionId::PageDelete,
                withDocument([](MainWindow& window, DocumentWindow* document, KeyContext context) {
                    std::vector<int> pages = window.selectedPagesOf(*document);
                    if (context != KeyContext::PageThumbnails && DeletePagesDialog::canAsk()) {
                        const std::optional<DeletePagesDialog::Choice> choice = DeletePagesDialog::ask(&window, document->pageCount(), pages);
                        if (!choice) return;
                        pages = choice->pages;
                    }
                    (void)window.deletePages(document, pages);
                }),
                [front, editable] { return editable() && front()->pageCount() > 1; }, "shell.tooltip.needsDocument");

    // --- page.cut / page.copy / page.paste (`Ctrl+X` / `Ctrl+C` / `Ctrl+V`) ------------------------------------------
    registerOne(ActionId::PageCut,
                withDocument([](MainWindow& window, DocumentWindow* document, KeyContext) {
                    (void)window.cutPages(document, window.selectedPagesOf(*document));
                }),
                [front, editable, pagesInFront] {
                    return editable() && static_cast<int>(pagesInFront().size()) < front()->pageCount();
                },
                "shell.tooltip.needsDocument");
    registerOne(ActionId::PageCopy,
                withDocument([](MainWindow& window, DocumentWindow* document, KeyContext) {
                    (void)window.copyPages(document, window.selectedPagesOf(*document));
                }),
                [front] {
                    const DocumentWindow* document = front();
                    return document != nullptr && document->handle().isValid() && document->pageCount() > 0;
                },
                "shell.tooltip.needsDocument");
    registerOne(ActionId::PagePaste,
                withDocument([](MainWindow& window, DocumentWindow* document, KeyContext) {
                    // AFTER THE SET'S LAST PAGE, as a file manager pastes after what is selected.
                    const std::vector<int> pages = window.selectedPagesOf(*document);
                    (void)window.pastePages(document, pages.empty() ? document->pageCount() : pages.back() + 1);
                }),
                [editable] {
                    const QClipboard* clipboard = QGuiApplication::clipboard();
                    const QMimeData* held = clipboard != nullptr ? clipboard->mimeData() : nullptr;
                    return editable() && held != nullptr && held->hasFormat(QLatin1String(core::kPagesMimeType));
                },
                "pages.tooltip.needsClipboardPages");

    // --- page.moveEarlier / page.moveLater (`Alt+Up` / `Alt+Down`) ---------------------------------------------------
    for (const int step : {-1, 1}) {
        registerOne(step < 0 ? ActionId::PageMoveEarlier : ActionId::PageMoveLater,
                    withDocument([step](MainWindow& window, DocumentWindow* document, KeyContext) {
                        (void)window.stepPages(document, window.selectedPagesOf(*document), step);
                    }),
                    [front, editable, pagesInFront, step] {
                        return editable() && steppedMap(front()->pageCount(), pagesInFront(), step).has_value();
                    },
                    "shell.tooltip.needsDocument");
    }

    // --- page.duplicate (`Ctrl+Shift+D`) -----------------------------------------------------------------------------
    registerOne(ActionId::PageDuplicate,
                withDocument([](MainWindow& window, DocumentWindow* document, KeyContext) {
                    (void)window.duplicatePages(document, window.selectedPagesOf(*document));
                }),
                editable, "shell.tooltip.needsDocument");

    // --- page.insert / page.replace -- dialogs, disabled with nobody to ask (convention 16) ----------------------------
    registerOne(ActionId::PageInsert,
                withDocument([](MainWindow& window, DocumentWindow* document, KeyContext) {
                    const std::vector<int> pages = window.selectedPagesOf(*document);
                    const std::optional<InsertPagesDialog::Choice> choice = InsertPagesDialog::ask(
                        &window, document->pageCount(), pages.empty() ? 0 : pages.back(),
                        window.services_.settings.get(core::keys::documents::defaultPageSize), clipboardPageCount());
                    if (choice) (void)window.insertPages(document, *choice);
                }),
                [editable] { return editable() && InsertPagesDialog::canAsk(); }, "shell.tooltip.needsDocument");
    registerOne(ActionId::PageReplace,
                withDocument([](MainWindow& window, DocumentWindow* document, KeyContext) {
                    const std::optional<ReplacePagesDialog::Choice> choice =
                        ReplacePagesDialog::ask(&window, document->pageCount(), window.selectedPagesOf(*document));
                    if (choice) (void)window.replacePages(document, *choice);
                }),
                [editable] { return editable() && ReplacePagesDialog::canAsk(); }, "shell.tooltip.needsDocument");
}

} // namespace rawpage::ui
