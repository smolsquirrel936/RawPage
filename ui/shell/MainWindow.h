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
//   * chord input, which is `ui/shell/ShortcutBinder` at T13 -- until then the actions registered
//     below are reachable through the registry (and so through a test and the batch runner), and
//     through the ribbon, the toolbar and the tool finder, but not from the keyboard;
//   * a real page surface, which is Stage 2 -- a document tab is a stub under decision D1-2.
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

#include <QMainWindow>
#include <QStringList>

#include <memory>

class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace rawpage::ui {

class ActionFactory;
class DocumentTabBar;
class DocumentWindow;
class QuickAccessToolbar;
class Ribbon;
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
    // Documents -- T12. Every one of these is a stub tab (D1-2): a path and a placeholder.
    // ---------------------------------------------------------------------------------------
    // Adds a tab, or focuses the one that already has this path. The same document twice in one
    // window is not a feature anyone asked for, and the path is the identity until `DocumentId`
    // arrives with `core/doc/` at Stage 2.
    DocumentWindow* openDocument(const QString& absolutePath);

    int             documentCount() const;
    DocumentWindow* documentAt(int index) const;
    DocumentWindow* currentDocument() const;
    int             indexOfPath(const QString& absolutePath) const;

    void activateDocument(int index);
    void closeDocument(int index);
    void closeAllDocuments();

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

    Ribbon*             ribbon() const { return ribbon_; }
    QuickAccessToolbar* quickAccessToolbar() const { return qat_; }
    ToolFinder*         toolFinder() const { return toolFinder_; }
    SidebarDock*        sidebar() const { return sidebar_; }
    StatusBar*          statusBar() const { return status_; }
    ThemeManager*       theme() const { return theme_.get(); }

protected:
    // Activation is what `activeShell()` follows. Offscreen it may never arrive, which is why
    // `raiseToForeground()` sets the same state directly -- a test that raises a window must get
    // the same answer a desktop does.
    void changeEvent(QEvent* event) override;

    // The debug half of D1-7. Runs at SHOW time rather than at construction because an action can
    // be added to the window after the constructor -- a dialog's, a pane's -- and the check is worth
    // having on the tree as the user actually sees it.
    void showEvent(QShowEvent* event) override;

private:
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

    // `file.open`'s dialog. A modal file picker is the one place Stage 1 asks the user a question,
    // and it collects a parameter and calls `openFiles` -- no document logic, per the standing rule
    // about dialogs.
    void promptForFiles();

    void toggleSidebar();
    void toggleFullScreen();
    void toggleRibbon();

    app::Services services_;

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
    StatusBar*   status_ = nullptr;
    std::unique_ptr<ThemeManager> theme_;

    // The edge strip an auto-hidden pane collapses into. It belongs to the WINDOW rather than to
    // the dock: a dock widget that placed a sibling in its parent's layout would be a class that
    // cannot be constructed without that parent, and therefore one that cannot be tested alone.
    QToolButton* leftStrip_ = nullptr;
    QToolButton* rightStrip_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_MAINWINDOW_H
