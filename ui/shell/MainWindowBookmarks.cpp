// ui/shell/MainWindowBookmarks -- the Bookmarks pane's edits: §8's Bookmarks table, `Ctrl+B` in the Viewer, the
// properties, the generators, import and export, and the contents page. Stage 7 T11 (`Stage7-Assembly-Plan.md` §6,
// D7-10, D7-11). A translation unit of `MainWindow` for `MainWindowExchange.cpp`'s reason: one feature's registrations
// and handlers in one place.
//
// EVERY HANDLER IS A COMMAND HANDED TO A JOURNAL (CLAUDE.md: `ui/`'s whole mutation vocabulary), through
// `runBookmarkCommand`, which says on the status bar what was done or why not and hands the pane the model again with
// the item the command made or moved selected. The contents page is a PAGE command -- one `InsertPagesCommand` (D7-11)
// -- and goes through `runPageCommand` like every page command.
//
// WHERE AN ITEM GOES -- the four keys that restructure (§8): `Tab` makes the bookmark the last child of the one before
// it; `Shift+Tab` makes it the next sibling of its parent; `Alt+Up` and `Alt+Down` swap it with its neighbour. Each is one
// `ReparentBookmarkCommand`, and each is DISABLED where it has nowhere to go (convention 16 -- a key that does nothing
// is a key that looks broken). A handler resolves the window and its document when it runs, never when it was
// registered (convention 14).

#include "ui/shell/MainWindow.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/BookmarkCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/OutlineGenerator.h"
#include "core/doc/PageWorkRunner.h"
#include "core/doc/TableOfContents.h"
#include "core/text/PageTextModel.h"
#include "ui/dialogs/BookmarkDialogs.h"
#include "ui/panes/BookmarksPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/ViewportController.h"

#include <QPointer>

#include <map>
#include <memory>
#include <mutex>

namespace rawpage::ui {

using core::ActionId;
using core::KeyContext;
using core::LogSubsystem;
using core::OutlineItemId;
using core::OutlinePosition;

namespace {

// Where `Tab`, `Shift+Tab`, `Alt+Up` and `Alt+Down` would put `id` -- or nowhere.
enum class Step { Demote, Promote, Up, Down };

std::optional<OutlinePosition> stepped(const core::Outline& outline, const OutlineItemId& id, Step step)
{
    const std::optional<OutlinePosition> at = outline.positionOf(id);
    if (!at) return std::nullopt;
    const std::vector<core::OutlineItem>* siblings = outline.childrenOf(at->parent);
    switch (step) {
    case Step::Demote: {
        if (at->index == 0) return std::nullopt;
        const core::OutlineItem& before = (*siblings)[static_cast<std::size_t>(at->index - 1)];
        if (outline.depthOf(id) >= core::kMaxOutlineDepth) return std::nullopt;
        return OutlinePosition{before.id, static_cast<int>(before.children.size())};
    }
    case Step::Promote: {
        if (!at->parent.isValid()) return std::nullopt;
        const std::optional<OutlinePosition> parent = outline.positionOf(at->parent);
        if (!parent) return std::nullopt;
        return OutlinePosition{parent->parent, parent->index + 1};
    }
    case Step::Up:
        if (at->index == 0) return std::nullopt;
        return OutlinePosition{at->parent, at->index - 1};
    case Step::Down:
        if (at->index + 1 >= static_cast<int>(siblings->size())) return std::nullopt;
        return OutlinePosition{at->parent, at->index + 1};
    }
    return std::nullopt;
}

core::Error notSaid(const char* key, QString context = {})
{
    return core::Error{core::ErrorCode::Unavailable, QLatin1String(key), std::move(context)};
}

} // namespace

// ---- The pane, handed the model ------------------------------------------------------------------------------------

void MainWindow::updateBookmarksPane(DocumentWindow* document)
{
    if (!bookmarks_) return;
    if (document == nullptr) {
        bookmarks_->setDocument({}, nullptr);
        return;
    }
    // THE MODEL'S OUTLINE where the tab has a journal -- what every command changes -- editable when the document is
    // and the outline's two readings agree (D7-10); else the backend's reading, read-only.
    if (const core::CommandJournal* journal = document->journal()) {
        const core::Outline& outline = journal->document().outline();
        const bool editable = document->isEditable() && static_cast<bool>(outline.status());
        QString reason;
        if (!outline.status()) reason = catalog::translateError(QString(outline.status().error().messageKey), outline.status().error().context);
        bookmarks_->setDocument(document->handle(), &outline, editable, reason);
        return;
    }
    bookmarks_->setDocument(document->handle(), services_.backend.outline(document->handle()));
}

core::Status MainWindow::runBookmarkCommand(DocumentWindow* document, std::unique_ptr<core::Command> command,
                                            const OutlineItemId& select)
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
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "bookmark command refused --" << status.error().messageKey << status.error().context;
        say(catalog::translate(catalog::key::BookmarksRefused)
                .arg(name, catalog::translateError(QString(status.error().messageKey), status.error().context)));
        return status;
    }
    say(catalog::translate(catalog::key::BookmarksDone).arg(name));
    if (document == currentDocument()) {
        updateBookmarksPane(document);
        if (bookmarks_ && select.isValid()) bookmarks_->selectId(select);
    }
    return status;
}

std::optional<core::Destination> MainWindow::currentViewDestination(const DocumentWindow& document) const
{
    const PageView* view = document.view();
    if (view == nullptr || document.pageCount() <= 0) return std::nullopt;
    core::Destination at;
    at.pageIndex = view->controller().currentPage();
    at.fit = core::Destination::Fit::XYZ;
    // THE VIEW'S TOP-LEFT on its page, and the zoom: what *the current view* means to a reader opening the bookmark.
    const PagePoint corner = view->pagePointAt(QPoint(0, 0));
    if (corner.isValid() && corner.page == at.pageIndex) {
        at.hasX = true;
        at.x = corner.point.x;
        at.hasY = true;
        at.y = corner.point.y;
    } else if (const core::PageTree* pages = view->pages()) {
        if (const core::Page* page = pages->at(at.pageIndex)) {
            at.hasY = true;
            at.y = page->cropBox().top();
        }
    }
    at.hasZoom = true;
    at.zoom = view->controller().zoom().factor;
    return at;
}

core::Status MainWindow::newBookmark(DocumentWindow* document, bool renameAfter)
{
    if (document == nullptr || document->journal() == nullptr) return notSaid("error.command.noSink");
    const std::optional<core::Destination> view = currentViewDestination(*document);
    if (!view) return notSaid("error.command.noSink");
    const core::Outline& outline = document->journal()->document().outline();
    // AFTER THE SELECTED BOOKMARK, as its next sibling; with none selected, last at the top level.
    OutlinePosition at{OutlineItemId{}, static_cast<int>(outline.roots().size())};
    if (bookmarks_ && bookmarks_->document() == document->handle()) {
        if (const std::optional<OutlinePosition> selected = outline.positionOf(bookmarks_->selectedId()))
            at = OutlinePosition{selected->parent, selected->index + 1};
    }
    core::OutlineItem item;
    item.id = core::newBookmarkId(core::newBookmarkKey(), 0);
    // FROM SELECTED TEXT, its text the title (`Features.md` §5.4); else *Untitled*, to be named in place.
    QString title;
    if (const SelectionOverlay* selection = document->selection(); selection != nullptr && selection->hasSelection())
        title = selection->copiedText().plain.simplified().left(200);
    item.title = title.isEmpty() ? catalog::translate(catalog::key::BookmarksUntitled) : title;
    item.action = core::OutlineAction::goTo(*view);
    item.destination = *view;
    const OutlineItemId made = item.id;
    const core::Status status =
        runBookmarkCommand(document, std::make_unique<core::CreateBookmarkCommand>(at, std::vector<core::OutlineItem>{item}), made);
    if (status && renameAfter && title.isEmpty() && bookmarks_ != nullptr && bookmarks_->isVisible()) bookmarks_->beginRename();
    return status;
}

core::Status MainWindow::generateBookmarks(DocumentWindow* document, const core::OutlineGeneratorOptions& options)
{
    const auto refuse = [this](const core::Error& error) -> core::Status {
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return error;
    };
    if (document == nullptr || document->journal() == nullptr) return refuse(notSaid("error.command.noSink"));
    if (!pageWork_) pageWork_ = std::make_unique<core::PageWorkRunner>(services_.renderScheduler);
    if (pageWork_->isRunning()) return refuse(notSaid("error.pages.busy"));
    // EVERY PAGE READ ON THE RENDER THREAD (CLAUDE.md: the UI thread never parses): its text, and -- for a tagged
    // document -- its structure tree's headings. Kept here, under a lock, until the run is over.
    struct Found {
        std::mutex mutex;
        std::map<int, std::vector<core::HeadingLine>> lines;
        std::map<int, std::vector<core::HeadingLine>> structure;
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
        [backend, handle, found, options](int page) {
            std::vector<core::HeadingLine> structure;
            if (options.mode == core::OutlineGeneratorOptions::Mode::Headings) (void)backend->structureHeadings(handle, page, structure);
            std::vector<core::HeadingLine> lines;
            if (core::ITextExtractor* extractor = backend->textExtractor(handle)) {
                core::text::PageTextModel model;
                if (extractor->extractPage(handle, page, model)) lines = core::OutlineGenerator::linesOf(page, model);
            }
            const std::lock_guard<std::mutex> guard(found->mutex);
            found->structure[page] = std::move(structure);
            found->lines[page] = std::move(lines);
        },
        [self, target, found, options, revision](core::Status done) {
            if (!self || !target) return;
            const auto refused = [&](const core::Error& error) {
                if (self->status_) self->status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
            };
            if (!done) return refused(done.error());
            // A COMMAND IN BETWEEN made these other pages' lines: nothing is generated.
            if (target->journal() == nullptr || target->journal()->revision() != revision)
                return refused(notSaid("error.pages.documentChanged"));
            std::vector<core::HeadingLine> structure;
            std::vector<core::HeadingLine> lines;
            {
                const std::lock_guard<std::mutex> guard(found->mutex);
                for (auto& [page, list] : found->structure) structure.insert(structure.end(), list.begin(), list.end());
                for (auto& [page, list] : found->lines) lines.insert(lines.end(), list.begin(), list.end());
            }
            // THE STRUCTURE TREE FIRST, when the document's names any heading (D7-10); else the layout's lines.
            core::Result<std::vector<core::HeadingLine>> headings = structure;
            if (structure.empty()) headings = core::OutlineGenerator::select(lines, options);
            if (!headings) return refused(headings.error());
            const core::Outline& outline = target->journal()->document().outline();
            std::vector<core::OutlineItem> items = core::OutlineGenerator::tree(headings.value(), core::newBookmarkKey());
            const OutlineItemId first = items.front().id;
            const core::GenerateBookmarksCommand::Source source =
                options.mode == core::OutlineGeneratorOptions::Mode::Fonts     ? core::GenerateBookmarksCommand::Source::Fonts
                : options.mode == core::OutlineGeneratorOptions::Mode::Pattern ? core::GenerateBookmarksCommand::Source::Pattern
                                                                               : core::GenerateBookmarksCommand::Source::Headings;
            (void)self->runBookmarkCommand(target,
                                           std::make_unique<core::GenerateBookmarksCommand>(
                                               OutlinePosition{OutlineItemId{}, static_cast<int>(outline.roots().size())}, std::move(items), source),
                                           first);
        });
    if (!started) return refuse(started.error());
    connect(document, &QObject::destroyed, this, [this, handle] {
        if (pageWork_ && pageWork_->isRunning() && pageWork_->runningFor() == handle) pageWork_->cancel();
    }, Qt::SingleShotConnection);
    if (pageWork_->isRunning() && status_)
        status_->setMessage(catalog::translate(catalog::key::BookmarksReadingPages).arg(static_cast<int>(pages.size())));
    return {};
}

core::Status MainWindow::importBookmarks(DocumentWindow* document, const QString& path)
{
    if (document == nullptr || document->journal() == nullptr) return notSaid("error.command.noSink");
    auto items = services_.backend.readOutlineExchange(path, document->pageCount(), core::newBookmarkKey());
    if (!items) {
        if (status_) status_->setMessage(catalog::translateError(QString(items.error().messageKey), items.error().context));
        return items.error();
    }
    const core::Outline& outline = document->journal()->document().outline();
    const OutlineItemId first = items.value().front().id;
    return runBookmarkCommand(document,
                              std::make_unique<core::GenerateBookmarksCommand>(
                                  OutlinePosition{OutlineItemId{}, static_cast<int>(outline.roots().size())}, std::move(items).value(),
                                  core::GenerateBookmarksCommand::Source::Import),
                              first);
}

core::Status MainWindow::exportBookmarks(DocumentWindow* document, const QString& path, core::OutlineFormat format)
{
    if (document == nullptr) return notSaid("error.command.noSink");
    const core::Outline* outline = document->journal() != nullptr ? &document->journal()->document().outline()
                                                                   : services_.backend.outline(document->handle());
    if (outline == nullptr || outline->isEmpty()) return notSaid("error.outline.nothingToList");
    const core::Status written = services_.backend.writeOutlineExchange(outline->roots(), format, path, true);
    if (status_) {
        status_->setMessage(written ? catalog::translate(catalog::key::BookmarksExported).arg(static_cast<int>(outline->nodeCount())).arg(path)
                                    : catalog::translateError(QString(written.error().messageKey), written.error().context));
    }
    return written;
}

core::Status MainWindow::insertTableOfContents(DocumentWindow* document)
{
    if (document == nullptr || document->journal() == nullptr) return notSaid("error.command.noSink");
    const core::Document& model = document->journal()->document();
    core::TableOfContents toc = core::tableOfContentsFrom(model.outline(), model.pages(), catalog::translate(catalog::key::BookmarksTocHeading));
    if (const PageView* view = document->view()) toc.hanPreference = view->hanPreference();
    auto payload = services_.backend.tableOfContents(toc);
    if (!payload) {
        if (status_) status_->setMessage(catalog::translateError(QString(payload.error().messageKey), payload.error().context));
        return payload.error();
    }
    // ONE INSERT, BEFORE THE FIRST PAGE (D7-11): one undo step, its links resolved to the pages as they stand now.
    return runPageCommand(document, std::make_unique<core::InsertPagesCommand>(0, std::move(payload).value()));
}

// ---- The registrations ----------------------------------------------------------------------------------------------

void MainWindow::registerBookmarkActions()
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
            RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "bookmark action registration refused for"
                                                << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())) << "--"
                                                << registered.error().messageKey;
        }
    };
    // The document in front, its outline editable, and the pane's selection -- resolved when asked (convention 14).
    const auto front = []() -> DocumentWindow* {
        MainWindow* window = activeShell();
        return window ? window->currentDocument() : nullptr;
    };
    const auto outlineEditable = [front] {
        const DocumentWindow* document = front();
        return document != nullptr && document->isEditable() && document->journal() != nullptr
               && static_cast<bool>(document->journal()->document().outline().status());
    };
    const auto selected = [front]() -> std::optional<std::pair<DocumentWindow*, OutlineItemId>> {
        MainWindow* window = activeShell();
        DocumentWindow* document = front();
        if (window == nullptr || document == nullptr || window->bookmarks_ == nullptr || document->journal() == nullptr) return std::nullopt;
        if (window->bookmarks_->document() != document->handle()) return std::nullopt;
        const OutlineItemId id = window->bookmarks_->selectedId();
        if (!id.isValid() || document->journal()->document().outline().find(id) == nullptr) return std::nullopt;
        return std::make_pair(document, id);
    };

    // --- bookmark.new (`Ctrl+B`, the Viewer and the pane) ------------------------------------------------------------
    registerOne(ActionId::BookmarkNew,
                [](KeyContext context) {
                    MainWindow* window = activeShell();
                    if (window != nullptr) (void)window->newBookmark(window->currentDocument(), context == KeyContext::Bookmarks);
                },
                [front, outlineEditable] { return outlineEditable() && front()->pageCount() > 0; }, "shell.tooltip.needsDocument");

    // --- bookmark.rename (`F2`) -------------------------------------------------------------------------------------
    registerOne(ActionId::BookmarkRename,
                [](KeyContext) {
                    if (MainWindow* window = activeShell(); window != nullptr && window->bookmarks_ != nullptr) window->bookmarks_->beginRename();
                },
                [outlineEditable, selected] { return outlineEditable() && selected().has_value(); }, "bookmarks.tooltip.needsBookmark");

    // --- bookmark.delete (`Delete`) -- asks about the children, where there are any --------------------------------
    registerOne(ActionId::BookmarkDelete,
                [selected](KeyContext) {
                    MainWindow* window = activeShell();
                    const auto chosen = selected();
                    if (window == nullptr || !chosen) return;
                    const core::OutlineItem* item = chosen->first->journal()->document().outline().find(chosen->second);
                    bool keepChildren = false;
                    if (!item->children.empty()) {
                        const std::optional<bool> answer =
                            DeleteBookmarkDialog::ask(window, item->title, static_cast<int>(item->children.size()));
                        if (!answer) return;
                        keepChildren = *answer;
                    }
                    (void)window->runBookmarkCommand(chosen->first, std::make_unique<core::DeleteBookmarkCommand>(
                                                                        std::vector<OutlineItemId>{chosen->second}, keepChildren));
                },
                [outlineEditable, selected] {
                    if (!outlineEditable()) return false;
                    const auto chosen = selected();
                    if (!chosen) return false;
                    const core::OutlineItem* item = chosen->first->journal()->document().outline().find(chosen->second);
                    return item->children.empty() || DeleteBookmarkDialog::canAsk();
                },
                "bookmarks.tooltip.needsBookmark");

    // --- bookmark.demote / promote / moveUp / moveDown (`Tab`, `Shift+Tab`, `Alt+Up`, `Alt+Down`) -------------------
    for (const auto& [id, step] : {std::pair{ActionId::BookmarkDemote, Step::Demote}, std::pair{ActionId::BookmarkPromote, Step::Promote},
                                   std::pair{ActionId::BookmarkMoveUp, Step::Up}, std::pair{ActionId::BookmarkMoveDown, Step::Down}}) {
        registerOne(id,
                    [selected, step](KeyContext) {
                        MainWindow* window = activeShell();
                        const auto chosen = selected();
                        if (window == nullptr || !chosen || (window->bookmarks_ != nullptr && window->bookmarks_->isRenaming())) return;
                        const std::optional<OutlinePosition> to = stepped(chosen->first->journal()->document().outline(), chosen->second, step);
                        if (!to) return;
                        (void)window->runBookmarkCommand(chosen->first, std::make_unique<core::ReparentBookmarkCommand>(chosen->second, *to),
                                                         chosen->second);
                    },
                    [outlineEditable, selected, step] {
                        const auto chosen = selected();
                        return outlineEditable() && chosen
                               && stepped(chosen->first->journal()->document().outline(), chosen->second, step).has_value();
                    },
                    "bookmarks.tooltip.needsBookmark");
    }

    // --- bookmark.setDestination (`Ctrl+Shift+D`) -- the current view ----------------------------------------------
    registerOne(ActionId::BookmarkSetDestination,
                [selected](KeyContext) {
                    MainWindow* window = activeShell();
                    const auto chosen = selected();
                    if (window == nullptr || !chosen) return;
                    const std::optional<core::Destination> view = window->currentViewDestination(*chosen->first);
                    if (!view) return;
                    (void)window->runBookmarkCommand(
                        chosen->first, std::make_unique<core::SetBookmarkTargetCommand>(chosen->second, core::OutlineAction::goTo(*view)),
                        chosen->second);
                },
                [outlineEditable, selected] { return outlineEditable() && selected().has_value(); }, "bookmarks.tooltip.needsBookmark");

    // --- bookmark.properties -- title, colour, style, open state, target: one undo step -----------------------------
    registerOne(ActionId::BookmarkProperties,
                [selected](KeyContext) {
                    MainWindow* window = activeShell();
                    const auto chosen = selected();
                    if (window == nullptr || !chosen) return;
                    const core::OutlineItem item = *chosen->first->journal()->document().outline().find(chosen->second);
                    const core::Destination view = window->currentViewDestination(*chosen->first).value_or(core::Destination{});
                    const std::optional<BookmarkPropertiesDialog::Choice> choice =
                        BookmarkPropertiesDialog::ask(window, item, chosen->first->pageCount(), view);
                    if (!choice) return;
                    const bool propertiesChanged = !(choice->properties == core::EditBookmarkCommand::Properties::of(item));
                    const bool targetChanged = choice->action != item.action;
                    if (!propertiesChanged && !targetChanged) return;
                    // BOTH AS ONE STEP: an undo of a properties dialog undoes what the dialog did.
                    core::CommandJournal* journal = chosen->first->journal();
                    journal->beginGroup(core::CatalogText{QLatin1String("command.bookmark.edit"), {choice->properties.title}});
                    if (propertiesChanged)
                        (void)window->runBookmarkCommand(chosen->first, std::make_unique<core::EditBookmarkCommand>(chosen->second, choice->properties),
                                                         chosen->second);
                    if (targetChanged)
                        (void)window->runBookmarkCommand(chosen->first, std::make_unique<core::SetBookmarkTargetCommand>(chosen->second, choice->action),
                                                         chosen->second);
                    journal->endGroup();
                    window->updateBookmarksPane(chosen->first);
                },
                [outlineEditable, selected] { return outlineEditable() && selected().has_value() && BookmarkPropertiesDialog::canAsk(); },
                "bookmarks.tooltip.needsBookmark");

    // --- bookmark.generate ------------------------------------------------------------------------------------------
    registerOne(ActionId::BookmarkGenerate,
                [front](KeyContext) {
                    MainWindow* window = activeShell();
                    DocumentWindow* document = front();
                    if (window == nullptr || document == nullptr) return;
                    const std::optional<core::OutlineGeneratorOptions> options = GenerateBookmarksDialog::ask(window);
                    if (options) (void)window->generateBookmarks(document, *options);
                },
                [front, outlineEditable] { return outlineEditable() && front()->pageCount() > 0 && GenerateBookmarksDialog::canAsk(); },
                "shell.tooltip.needsDocument");

    // --- bookmark.import / bookmark.export --------------------------------------------------------------------------
    registerOne(ActionId::BookmarkImport,
                [front](KeyContext) {
                    MainWindow* window = activeShell();
                    DocumentWindow* document = front();
                    if (window == nullptr || document == nullptr) return;
                    if (const std::optional<QString> path = BookmarkFileDialog::askImport(window)) (void)window->importBookmarks(document, *path);
                },
                [outlineEditable] { return outlineEditable() && BookmarkFileDialog::canAsk(); }, "shell.tooltip.needsDocument");
    registerOne(ActionId::BookmarkExport,
                [front](KeyContext) {
                    MainWindow* window = activeShell();
                    DocumentWindow* document = front();
                    if (window == nullptr || document == nullptr) return;
                    if (const auto chosen = BookmarkFileDialog::askExport(window))
                        (void)window->exportBookmarks(document, chosen->first, chosen->second);
                },
                [front, this] {
                    const DocumentWindow* document = front();
                    if (document == nullptr || !BookmarkFileDialog::canAsk()) return false;
                    const core::Outline* outline = document->journal() != nullptr ? &document->journal()->document().outline()
                                                                                   : services_.backend.outline(document->handle());
                    return outline != nullptr && !outline->isEmpty();
                },
                "bookmarks.tooltip.needsBookmarks");

    // --- bookmark.tableOfContents -- one inserted page (D7-11) ------------------------------------------------------
    registerOne(ActionId::BookmarkTableOfContents,
                [front](KeyContext) {
                    MainWindow* window = activeShell();
                    if (window != nullptr) (void)window->insertTableOfContents(front());
                },
                [front, outlineEditable] {
                    const DocumentWindow* document = front();
                    if (!outlineEditable()) return false;
                    for (const core::OutlineItem* item : document->journal()->document().outline().preorder())
                        if (item->destination.isResolved()) return true;
                    return false;
                },
                "bookmarks.tooltip.needsBookmarks");

    // THE PANE'S OWN GESTURES: a title typed in place, a row dropped somewhere -- each a command, or nothing.
    if (bookmarks_ != nullptr) {
        connect(bookmarks_, &BookmarksPane::renameRequested, this, [this](OutlineItemId id, QString title) {
            DocumentWindow* document = currentDocument();
            if (document == nullptr || document->journal() == nullptr || title.trimmed().isEmpty()) return;
            const core::OutlineItem* item = document->journal()->document().outline().find(id);
            if (item == nullptr) return;
            core::EditBookmarkCommand::Properties properties = core::EditBookmarkCommand::Properties::of(*item);
            properties.title = title;
            (void)runBookmarkCommand(document, std::make_unique<core::EditBookmarkCommand>(id, properties), id);
        });
        connect(bookmarks_, &BookmarksPane::moveRequested, this, [this](OutlineItemId id, OutlinePosition to) {
            DocumentWindow* document = currentDocument();
            if (document == nullptr) return;
            (void)runBookmarkCommand(document, std::make_unique<core::ReparentBookmarkCommand>(id, to), id);
        });
    }
}

} // namespace rawpage::ui
