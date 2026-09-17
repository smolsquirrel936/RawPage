// The application frame.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS CLASS IS ALLOWED TO KNOW.
//
// It receives `app::Services` -- interfaces only -- and it holds NO document logic. Every command
// it offers resolves to an `ActionId` and goes through `ActionRegistry::dispatch`; nothing here is
// wired straight from a widget to a behaviour, because the headless batch runner (flow K, Stage 14)
// has to be able to reach every feature and it has no widgets to click.
//
// WHAT IT DOES NOT HAVE YET, and where each arrives. Saying so here is cheaper than a reader
// searching for a ribbon that is not missing so much as not yet built:
//
//   * a real page surface, which is Stage 2 -- a document tab is a stub under decision D1-2.
//
// Chord input arrived at T13 and is `ui/shell/ShortcutBinder`, one per window: it computes the
// §8 context from the focus chain, resolves the chord through `KeyBindingStore`, and dispatches
// through the same `ActionRegistry` call the ribbon makes. NOTHING here sets a `QAction`
// shortcut -- see `ShortcutBinder.h` for why Qt's own shortcut system cannot express §8's
// per-context bindings, and `showEvent` for the check that keeps it that way.
//
// ---------------------------------------------------------------------------------------------
// THERE CAN BE MORE THAN ONE OF THESE, AND THE ACTION LAYER HAD TO LEARN THAT AT T12.
//
// Tearing a tab out makes a second `MainWindow`, and `ActionRegistry` is one object shared by all
// of them: registration is not idempotent (property 3), so a second window cannot register
// `view.toggleSidebar` again -- and if the first window's handler had captured `this`, every window
// in the application would go on toggling the FIRST window's sidebar, including after that window
// was closed and its handler left pointing at freed memory.
//
// So the handlers below capture nothing. Each resolves `MainWindow::activeShell()` at DISPATCH time
// and acts on that window. Registration happens once per registry, whichever window gets there
// first, and every window afterwards finds the commands already present and leaves them alone.
// This is what makes a Global command mean "do it to the window I am looking at", which is what a
// Global command has always meant.
//
// The central widget is an explicit placeholder that says which of those is which. An empty grey
// rectangle reads as a broken build; a surface that names what is missing reads as a stage.
// ---------------------------------------------------------------------------------------------

#ifndef RAWPAGE_UI_SHELL_MAINWINDOW_H
#define RAWPAGE_UI_SHELL_MAINWINDOW_H

#include "app/Services.h"
#include "core/backend.h"
#include "core/cmd/RecoveryJournal.h"
#include "ui/dialogs/RotatePagesDialog.h"
#include "ui/dialogs/UnsavedChangesDialog.h"

#include <QMainWindow>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

class QCloseEvent;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace rawpage::ui {

class ActionFactory;
class DocumentTabBar;
class DocumentWindow;
class LogPane;
class MessageBar;
class PageView;
class PreferencesDialog;
class QuickAccessToolbar;
class Ribbon;
class ShortcutBinder;
class SidebarDock;
class StatusBar;
class ThemeManager;
class ToolFinder;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const app::Services& services, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Flow B's landing point: a file list from the command line, or forwarded by a secondary
    // instance over `platform::ISingleInstanceBus`.
    //
    // DECISION D1-2 GOVERNS THIS FUNCTION. It checks that a path EXISTS and ends in `.pdf`, and it
    // does not read a single byte of the file. Deciding whether bytes are a PDF is
    // `IDocumentBackend`'s job at Stage 2, and pre-empting that boundary inside `ui/` is precisely
    // the violation this architecture exists to prevent -- guide case A-58 checks for it.
    void openFiles(const QStringList& paths);

    // ---------------------------------------------------------------------------------------
    // Documents -- S1 T12, and a real one since S2 T13: a handle, a page tree and a `PageView`.
    // ---------------------------------------------------------------------------------------
    // Adds a tab for a document the BACKEND HAS ALREADY OPENED, or focuses the one that already
    // has this path. The tab takes ownership of the handle and closes it (see `DocumentWindow`),
    // which is why the open happens in `openFiles()` and the handle is passed in rather than
    // acquired here: a function that both opened and might discard would have to close on one of
    // its two paths, and that is the leak.
    //
    // The same document twice in one window is not a feature anyone asked for, and the path is the
    // identity until `DocumentId` arrives with the rest of `core/doc/` at Stage 3.
    DocumentWindow* openDocument(const QString& absolutePath, core::DocumentHandle handle);

    int             documentCount() const;
    DocumentWindow* documentAt(int index) const;
    DocumentWindow* currentDocument() const;
    int             indexOfPath(const QString& absolutePath) const;

    void activateDocument(int index);

    // CLOSING A DIRTY DOCUMENT NEEDS A DISPOSITION (Stage 3 T10, D3-14). With none given, the user is
    // asked; where there is nobody to ask (convention 16) the close is REFUSED with `Unavailable` and the
    // document stays open and dirty -- never closed with its changes silently dropped, never blocked on a
    // dialog nobody can see. Cancel refuses with `Cancelled`; Discard removes the recovery directory and
    // closes; Save closes only once the save -- or the Save-As it turned into -- succeeded. A clean
    // document closes without a question, whatever is passed.
    core::Status closeDocument(int index, std::optional<CloseDisposition> disposition = std::nullopt);

    // Every document, first to last, STOPPING at the first refusal -- a Cancel on the second of three
    // leaves the second and the third open.
    core::Status closeAllDocuments();

    // ---------------------------------------------------------------------------------------
    // Recovery -- Stage 3 T11 (D3-11). `app/RecoveryPrompt` discovers and asks; this acts on one answer.
    // ---------------------------------------------------------------------------------------
    // Opens the candidate's source through the backend and replays its journal into a new tab: under the
    // source's own path when the source is still the file the changes were made to, and otherwise into an
    // UNTITLED document whose source is never written. The tab says what happened in a `Recovered` banner --
    // the steps restored, the step *k* that could not be and why, a last change that was still being made.
    // Once the document holds its steps the crashed session's directory is removed; a refusal -- a damaged
    // journal, a source that is gone, a journal that cannot be replayed at all, the same file already open
    // here -- opens nothing and KEEPS it, so nothing is lost by a recovery that did not happen.
    core::Result<DocumentWindow*> recoverDocument(const core::RecoveryCandidate& candidate);

    // ---------------------------------------------------------------------------------------
    // Mutation and save -- Stage 3 T10.
    // ---------------------------------------------------------------------------------------
    // Runs a Rotate Pages choice through the document's journal: one command, or one group (D3-13).
    core::Status rotatePages(DocumentWindow* document, const RotatePagesDialog::Choice& choice);

    // Save in place, through `SaveCoordinator`. A document that must be saved as a new file -- repaired,
    // changed on disk since it was opened, or refused by the file system (read-only, held by another
    // process, a directory that cannot hold the temporary file) -- first shows WHY, as a `SaveAsRequired`
    // banner, and then goes to `saveDocumentAs` (D3-6, D3-16; the last of the three since Stage 3 guide
    // I-15). A failure no other path could answer is a `SaveFailed` banner as well as the result.
    core::Status saveDocument(DocumentWindow* document);
    core::Status saveDocumentAs(DocumentWindow* document);

    // WHERE A SAVE AS GOES, and the parameterized twin a headless case drives (convention 16). The
    // answer is handed the path the dialog would propose -- the document's own, or for an untitled document
    // `DocumentWindow::suggestedSavePath()`; nothing means the user cancelled. `canAskSavePath()`
    // is false offscreen unless an answer is installed, and `file.saveAs` is then disabled.
    using SaveAsAnswer = std::function<std::optional<QString>(const QString& currentPath)>;
    static void setSaveAsAnswerForTesting(SaveAsAnswer answer);
    static bool canAskSavePath();

    // `Ctrl+Tab` / `Ctrl+Shift+Tab` once T13 wires the chords. Wraps at both ends, which is what
    // every tab strip does and what makes the chord usable without looking.
    void stepDocument(int delta);

    // ---------------------------------------------------------------------------------------
    // Tear-off and re-dock -- the structural behaviour D1-2 exists to let us build now.
    // ---------------------------------------------------------------------------------------
    // Takes the document OUT without destroying it. The caller owns it until it is adopted, which
    // is what makes moving one between windows a move rather than a close and a re-open -- the tab
    // keeps its identity, and at Stage 2 it will keep its journal and its scroll position with it.
    DocumentWindow* releaseDocument(int index);
    void            adoptDocument(DocumentWindow* document, int index = -1);

    // Release into a brand-new window, positioned near where the drag ended. Returns it.
    MainWindow* detachDocument(int index, const QPoint& globalPos = QPoint());

    DocumentTabBar* documentTabs() const { return tabs_; }

    // ---------------------------------------------------------------------------------------
    // The window set -- T12.
    // ---------------------------------------------------------------------------------------
    // In creation order. `Ctrl+F6` walks it; a Global command dispatches into `activeShell()`.
    static const QList<MainWindow*>& shellWindows();

    // The window a Global command applies to: the last one activated, falling back to the last one
    // created. Never a window that has been destroyed -- the destructor takes it out of the set,
    // which is the whole reason the set exists rather than a raw `QApplication::activeWindow()`
    // cast, since that answers null whenever the application is in the background.
    static MainWindow* activeShell();

    static void stepWindow(int delta);

    // Called by the primary instance when a secondary hands off. Separate from `openFiles()`
    // because raising is a window operation and opening is not, and Stage 4's window set will need
    // to choose WHICH window to raise.
    void raiseToForeground();

    // WHAT A HANDOFF DOES: raise, THEN open. The order is the whole of this method (Stage 2 guide
    // issue I-30). Opening an encrypted file blocks inside a modal password prompt, so raising
    // AFTER the open left the prompt to be shown by a background process -- which Windows does not
    // bring forward -- and the window came up only once somebody had found the prompt and answered
    // it. A method rather than two lines in `app/main.cpp`'s listener so the order has a case.
    void openHandoff(const QStringList& files);

    // The window's keyboard. `activeContext()` is what attributes every dispatch -- a ribbon
    // click's as much as a chord's -- to the §8 context the focus is actually in.
    ShortcutBinder*     shortcutBinder() const { return shortcuts_.get(); }

    Ribbon*             ribbon() const { return ribbon_; }
    QuickAccessToolbar* quickAccessToolbar() const { return qat_; }
    ToolFinder*         toolFinder() const { return toolFinder_; }
    SidebarDock*        sidebar() const { return sidebar_; }

    // The log pane -- T14. Built with the window and HIDDEN, because a shell that opens with a
    // log pane showing is a shell that looks like it is in trouble. `pane.log` toggles it.
    LogPane*            logPane() const { return logPane_; }

    // The Preferences dialog, built on first use and kept. Null until `file.preferences` has been
    // dispatched once. Modeless, so this is not a blocking call's return value -- see
    // `ui/dialogs/PreferencesDialog.h` for why modeless is the load-bearing choice.
    PreferencesDialog*  preferences() const { return preferences_; }
    PreferencesDialog*  showPreferences();
    StatusBar*          statusBar() const { return status_; }
    MessageBar*         messageBar() const { return messageBar_; }

    // The one place a `QAction` is constructed (D1-7). Exposed for the suite, which needs an action
    // the FACTORY built in order to assert what `refresh()` does to it -- a hand-made `QAction`
    // could not be built anyway, because `layer-lint`'s `bare-qaction` rule is non-escapable.
    ActionFactory*      actionFactory() const { return actionFactory_.get(); }

    // The viewer of the tab in front, or null when there is no document. Every §8 Viewer command
    // resolves through here at DISPATCH time -- convention 14: a handler that captured a view
    // would act for a tab that has since been closed or torn off into another window.
    PageView*           currentView() const;
    ThemeManager*       theme() const { return theme_.get(); }

protected:
    // Activation is what `activeShell()` follows. Offscreen it may never arrive, which is why
    // `raiseToForeground()` sets the same state directly -- a test that raises a window must get
    // the same answer a desktop does.
    void changeEvent(QEvent* event) override;

    // EVERY WAY A WINDOW CLOSES -- its close box, `file.quit`, a torn-off window's close -- asks about each
    // dirty document through `closeDocument` (D3-14, guide case A-78), and a refusal keeps the window.
    // Clean documents stay in the window as they always did; only a dirty one needs a disposition.
    void closeEvent(QCloseEvent* event) override;

    // The debug half of D1-7, in both of its forms -- no direct connection, and no QAction that
    // resolves its own key. Runs at SHOW time rather than at construction because an action can
    // be added to the window after the constructor -- a dialog's, a pane's -- and the check is worth
    // having on the tree as the user actually sees it.
    void showEvent(QShowEvent* event) override;

    // REACHING FOR AN AUTO-HIDDEN PANE IS A HOVER, and the edge strips are the only widgets this
    // filters. A strip is a `QToolButton` and a button has no "pointer arrived" signal, so the
    // window watches the two of them for `QEvent::Enter` -- see `buildAutoHideStrips()` for why the
    // filter is removed in the destructor rather than left to Qt.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // A window emptied by a MOVE has done its job and goes (issue I-35). Called after a document is
    // adopted away by another window, and only then -- a window emptied by a CLOSE stays, because
    // the user is still standing in it and may open the next file into it.
    void closeIfEmptied();

    void buildHeader();
    void buildCentralSurface();
    void buildAutoHideStrips();
    void registerActions();
    void updateAutoHideStrips();

    // The Quick Access Toolbar's two placements (`Features.md` §1.1) are two positions in the
    // header's column, above the ribbon or below it. Re-inserting the same widget is what makes the
    // placement a property of the toolbar rather than two toolbars.
    void applyQatPlacement();

    // The QAT half of the UI customization, in both directions. Kept here rather than inside
    // `QuickAccessToolbar` because a widget that reads and writes `Settings` on its own behalf is a
    // widget that cannot be tested without one -- and because the window is what knows that a
    // change is worth persisting at all.
    void loadUiCustomization();
    void saveUiCustomization();

    // Tab strip visibility, the body's current page, the window title and the status bar all follow
    // from the document set. One function, called after every change to it: four call sites each
    // updating three of the four is how a title bar ends up naming a closed document.
    void updateDocumentChrome();

    // The tab texts and the window title, with the dirty marker (Stage 3 T10). Separate from
    // `updateDocumentChrome()` because a mutation moves the marker and must NOT re-raise the document's
    // banners: one the user dismissed would come back with every command (guide case A-80).
    void updateDocumentTitles();

    // Pushes the current view's page number and zoom into the status bar, and re-asks
    // `ActionFactory` for every action's enabled state and tooltip (I-53). One function, because
    // the three move together: a page changes, the Next Page button greys out, and its tooltip has
    // to stop saying "not available for this document" and start saying why.
    void updateViewChrome();

    // Raises the banners a document's own condition earns -- repaired, protected. Called on every
    // tab change, because the condition belongs to the tab and not to the window.
    void showDocumentConditions(DocumentWindow* document);

    // The §8 Viewer commands. Separate from `registerActions()` only because that function was
    // already 250 lines: the registration mechanism, the once-per-registry guard and the
    // resolve-at-dispatch rule are all identical and all argued there.
    void registerViewerActions();

    // `edit.undo`, `edit.redo`, `page.rotate`, `file.save` and `file.saveAs` -- Stage 3 T10.
    void registerMutationActions();

    // A failed save, as a banner the document keeps (Stage 3 T10).
    void reportSaveFailure(DocumentWindow* document, const core::Error& error);

    // The two Viewer commands that ask the user a question. Both are refused where there is no one
    // to ask (convention 16, Stage 1's I-23), and both end in the same `PageView` call a chord or
    // the status bar's page box would take -- one behaviour, three routes.
    void promptForZoom();
    void promptForPage();

    // `file.open`'s dialog. A modal file picker is the one place Stage 1 asks the user a question,
    // and it collects a parameter and calls `openFiles` -- no document logic, per the standing rule
    // about dialogs.
    void promptForFiles();

    void toggleSidebar();
    void toggleFullScreen();
    void toggleRibbon();

    app::Services services_;

    // Constructed before the action factory, because the factory's context provider asks it which
    // §8 context a dispatch happened in, and destroyed first, because it filters the application's
    // events and must stop doing so before the window it answers for comes apart (I-12's lesson,
    // reached through a different door).
    std::unique_ptr<ShortcutBinder> shortcuts_;

    // Constructed before the ribbon and destroyed after it: every RawPageAction the ribbon shows
    // was made here, and the lambda each one carries holds this factory by reference.
    std::unique_ptr<ActionFactory> actionFactory_;

    Ribbon*             ribbon_ = nullptr;
    QuickAccessToolbar* qat_ = nullptr;
    ToolFinder*         toolFinder_ = nullptr;

    // The menu-widget slot holds a COLUMN, not the ribbon directly: the QAT sits above or below the
    // ribbon and both have to be in the one slot QMainWindow puts above the dock areas, or a docked
    // pane can land between them.
    QWidget*      header_ = nullptr;
    QVBoxLayout*  headerColumn_ = nullptr;

    // True only while `loadUiCustomization()` is restoring. Without it the restore would write the
    // factory defaults back as though the user had chosen them, and "never customized" would stop
    // being distinguishable from "customized to look exactly like the default".
    bool suppressCustomizationSave_ = false;

    // The central area: an edge strip, the document column, an edge strip. The column is the tab
    // strip over a two-page stack -- the "no documents" placeholder, and the documents themselves.
    DocumentTabBar* tabs_ = nullptr;
    QStackedWidget* body_ = nullptr;        // page 0: the placeholder. page 1: documents_
    QStackedWidget* documents_ = nullptr;
    QWidget*        placeholder_ = nullptr;

    SidebarDock* sidebar_ = nullptr;
    LogPane*     logPane_ = nullptr;
    StatusBar*   status_ = nullptr;
    MessageBar*  messageBar_ = nullptr;

    // Created on first use rather than with the window: three quarters of sessions never open it,
    // and building three pages and a 248-row table for every window that might be torn off is a
    // cost nobody asked for. Parented to this window, so it dies with it.
    PreferencesDialog* preferences_ = nullptr;
    std::unique_ptr<ThemeManager> theme_;

    // The edge strip an auto-hidden pane collapses into. It belongs to the WINDOW rather than to
    // the dock: a dock widget that placed a sibling in its parent's layout would be a class that
    // cannot be constructed without that parent, and therefore one that cannot be tested alone.
    QToolButton* leftStrip_ = nullptr;
    QToolButton* rightStrip_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_MAINWINDOW_H
