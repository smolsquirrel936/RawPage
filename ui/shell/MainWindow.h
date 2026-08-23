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
//   * the ribbon and the Quick Access Toolbar -- T10 and T11;
//   * document tabs and the stub `DocumentWindow` -- T12, which is when `openFiles()` stops
//     reporting a path and starts opening a tab for it (decision D1-2);
//   * chord input, which is `ui/shell/ShortcutBinder` at T13 -- until then the actions registered
//     below are reachable through the registry (and so through a test and the batch runner), and
//     through the pane chrome, but not from the keyboard;
//   * a real page surface, which is Stage 2.
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

class QToolButton;

namespace rawpage::ui {

class SidebarDock;
class StatusBar;
class ThemeManager;

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

    // Called by the primary instance when a secondary hands off. Separate from `openFiles()`
    // because raising is a window operation and opening is not, and Stage 4's window set will need
    // to choose WHICH window to raise.
    void raiseToForeground();

    SidebarDock*  sidebar() const { return sidebar_; }
    StatusBar*    statusBar() const { return status_; }
    ThemeManager* theme() const { return theme_.get(); }

private:
    void buildCentralSurface();
    void buildAutoHideStrips();
    void registerActions();
    void updateAutoHideStrips();

    app::Services services_;

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
