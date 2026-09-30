#include "ui/shell/MainWindow.h"

#include "core/Log.h"
// T9: `openFiles()` asks the backend whether a file is a PDF (guide case A-47). It is a `core/`
// header, so it widens nothing -- convention 9's list of what `ui/` looks OUTWARD to is unchanged.
#include "core/backend.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/panes/LogPane.h"
#include "ui/panes/BookmarksPane.h"
#include "ui/panes/DestinationsPane.h"
#include "ui/panes/ThumbnailsPane.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentTabBar.h"
#include "ui/dialogs/PasswordDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MessageBar.h"
#include "ui/viewer/GridGuideLayer.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/FullScreenController.h"
#include "ui/viewer/PresentationSurface.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/ToolController.h"
#include "ui/shell/QuickAccessToolbar.h"
#include "ui/shell/Ribbon.h"
#include "ui/shell/RibbonModel.h"
#include "ui/shell/RibbonTabBuilder.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/StatusBar.h"
#include "ui/shell/ThemeManager.h"
#include "ui/shell/ToolFinder.h"
#include "ui/shell/UiCustomization.h"
#include "ui/shell/WindowArranger.h"

#include <QCloseEvent>
#include <QCursor>
#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QGuiApplication>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QShowEvent>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <string_view>

namespace rawpage::ui {
namespace {

using core::ActionId;
using core::KeyContext;
using core::LogSubsystem;

QToolButton* makeEdgeStrip(QWidget* parent, const QString& text, const QString& tooltip)
{
    auto* strip = new QToolButton(parent);
    strip->setText(text);
    strip->setToolTip(tooltip);
    strip->setAutoRaise(true);
    strip->setMaximumWidth(24);
    strip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    strip->hide();
    return strip;
}

// Qt has no "draw this button's label rotated", and a custom paintEvent for a 24-pixel strip is a
// DPI-scaling bug waiting to be written. One character per line is what the Windows shell does for
// the same control, and it is correct at every scale factor for free.
QString verticalize(const QString& text)
{
    QStringList characters;
    characters.reserve(text.size());
    for (const QChar character : text) characters << QString(character);
    return characters.join(QLatin1Char('\n'));
}

// THE WINDOW SET. File-scope rather than a member, because "which window is active" is a fact
// about the application and not about any one window -- and because a static member would put the
// same two variables in the class every window then has to keep in step. Creation order is the
// order `Ctrl+F6` walks, which is what a person expects: the window you made second is the second
// one you reach.
QList<MainWindow*>& shellWindowSet()
{
    static QList<MainWindow*> windows;
    return windows;
}

MainWindow* g_activeShell = nullptr;

// WHETHER THERE IS ANYBODY TO ASK. A modal file picker needs a person in front of it; under
// `QT_QPA_PLATFORM=offscreen` there is none, so the dialog is built, shown to nobody, and never
// returns. That is not a hypothetical -- it is a ten-minute CI hang that happened the first time a
// test dispatched `file.open` after T12 registered it (issue I-23).
//
// It is an ENABLEMENT PREDICATE rather than a check inside the handler, because the registry
// already has the vocabulary for exactly this: `dispatch` refuses a disabled command with
// `ErrorCode::Unavailable`, distinct from the `NotFound` of a command that does not exist. A
// headless caller -- a test today, Stage 14's batch runner tomorrow -- gets a clean, distinguishable
// refusal instead of a process that stops. The parameterized form of "open this file" is
// `MainWindow::openFiles`, which is what flow B and the CLI already use and which needs no user.
bool canPromptTheUser()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

MainWindow::SaveAsAnswer& installedSaveAsAnswer()
{
    static MainWindow::SaveAsAnswer answer;
    return answer;
}

} // namespace

const QList<MainWindow*>& MainWindow::shellWindows() { return shellWindowSet(); }

MainWindow* MainWindow::activeShell()
{
    // The last window activated, if it is still alive. The fallback is the last window CREATED
    // rather than the first: a command dispatched immediately after a tear-off belongs to the
    // window the user is now looking at.
    if (g_activeShell && shellWindowSet().contains(g_activeShell)) return g_activeShell;
    return shellWindowSet().isEmpty() ? nullptr : shellWindowSet().last();
}

void MainWindow::stepWindow(int delta)
{
    const QList<MainWindow*>& windows = shellWindowSet();
    if (windows.size() < 2) return;

    const int from = windows.indexOf(activeShell());
    if (from < 0) return;

    const int size = windows.size();
    const int to   = ((from + delta) % size + size) % size;   // wraps in both directions
    windows.at(to)->raiseToForeground();
}

MainWindow::MainWindow(const app::Services& services, QWidget* parent)
    : QMainWindow(parent), services_(services)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(catalog::translate(catalog::key::WindowTitle));
    resize(1280, 820);

    // Joins the set BEFORE anything else, because `registerActions()` below may be the first
    // registration in this registry and its handlers resolve `activeShell()` -- which has to be
    // able to answer with this window from the moment it exists.
    shellWindowSet().append(this);
    g_activeShell = this;

    theme_ = std::make_unique<ThemeManager>(services_.settings, this);

    // S4 T10 (D4-12). Built with the window and owned by it: one screen saver, one controller,
    // and the levels are a property of THIS window rather than of the application -- a second
    // window is a second thing that can go full screen.
    fullScreen_ = std::make_unique<FullScreenController>(services_, this);
    theme_->apply();

    // THE KEYBOARD -- T13. Before `registerActions()` and before the ribbon, because the action
    // factory built there asks it which §8 context a dispatch happened in, and because a window
    // that is shown before its binder exists is a window with a dead keyboard for those frames.
    shortcuts_ = std::make_unique<ShortcutBinder>(services_.actions, services_.keyBindings, this);

    status_ = new StatusBar(this);
    setStatusBar(status_);

    // The page box has been an input with no consumer since T9. This is the consumer -- and it goes
    // through `goToPage`, which is the same path the `nav.goToPage` command takes, rather than
    // through a second implementation that could scroll somewhere slightly different.
    connect(status_, &StatusBar::pageRequested, this, [this](int oneBased) {
        if (PageView* view = currentView()) view->goToPage(oneBased - 1);
    });

    // S4 T12 -- *Go to Page* accepts a LOGICAL page label ("iv", "A-12"), which no integer parse
    // could ever reach. The resolver is a lambda rather than a pointer into a page tree because the
    // tree belongs to whichever tab is in front and is replaced on every tab switch; resolving at
    // the moment somebody presses Enter is the same discipline every command handler here follows
    // (convention 14).
    //
    // EXACT MATCH, case-sensitively: a PDF page label is a byte string and "A-12" and "a-12" are
    // two different labels. The first page carrying the label wins, which matters only for a
    // document that labels two pages the same -- itself malformed, and refusing it outright would
    // strand the reader on a file every other viewer navigates.
    status_->setPageLabelResolver([this](const QString& typed) {
        const PageView* view = currentView();
        const core::PageTree* tree = view ? view->pages() : nullptr;
        if (tree == nullptr) return -1;
        const std::vector<core::Page>& pages = tree->pages();
        for (std::size_t i = 0; i < pages.size(); ++i) {
            if (pages[i].label() == typed) return static_cast<int>(i);
        }
        return -1;
    });

    sidebar_ = new SidebarDock(services_.actions, this);
    addDockWidget(Qt::LeftDockWidgetArea, sidebar_);
    resizeDocks({sidebar_}, {sidebar_->rememberedWidth()}, Qt::Horizontal);

    // THE DOCUMENT PANES -- Stage 4 T5, D4-2. ONE PANE PER WINDOW, RE-POINTED AT WHICHEVER TAB IS IN
    // FRONT, never one pane per document: a pane rebuilt on every tab switch would throw away the
    // thumbnails it had just rendered and ask the one worker for them again, which is the cost
    // D4-10 exists to bound. `updateDocumentPanes()` is what re-points it.
    //
    // Only Thumbnails at T5; Bookmarks and Destinations are T6's and are registered there. The
    // other seven panes in `Features.md` §1.1 are not listed at all until their models exist --
    // D4-2, and the reason the registry has no notion of a disabled row.
    thumbnails_ = new ThumbnailsPane(services_.renderScheduler, sidebar_);
    sidebar_->addPane(core::ActionId::PaneThumbnails,
                      catalog::translate(catalog::key::PanesThumbnailsTitle), thumbnails_);

    // ONE DIRECTION PER GESTURE (plan §5.3). The pane emits only on a gesture and `setCurrentPage`
    // emits nothing, so this connection cannot come back around: the view's `viewChanged` reaches
    // `updateViewChrome()`, which sets the selection silently.
    connect(thumbnails_, &ThumbnailsPane::activated, this, [this](int pageIndex) {
        if (PageView* view = currentView()) view->goToPage(pageIndex);
    });

    // S4 · T6 -- the two navigation panes, over the models T4 hydrated at open (D4-3). Both are
    // read-only halves: every editing row in §8's Bookmarks table is Stage 7's and stays
    // unregistered, so its key reaches the tree unconsumed rather than a handler that does nothing.
    bookmarks_ = new BookmarksPane(sidebar_);
    sidebar_->addPane(core::ActionId::PaneBookmarks,
                      catalog::translate(catalog::key::PanesBookmarksTitle), bookmarks_);

    destinations_ = new DestinationsPane(sidebar_);
    sidebar_->addPane(core::ActionId::PaneDestinations,
                      catalog::translate(catalog::key::PanesDestinationsTitle), destinations_);

    // ONE NAVIGATION, whichever pane asked. Both panes hand up a `core::Destination` and this is
    // the single place that turns one into a movement, so a bookmark and a named destination
    // cannot land in different places for the same target.
    //
    // **T12 APPLIES THE FIT** (I-05, closed). Until then this went to the destination's PAGE and
    // left `/XYZ`'s point and zoom unread on the floor, because the conversion from a PDF-space
    // point to a scroll offset did not exist yet -- it arrived with Fit Visible's, and both now
    // run through `PageView`.
    const auto goToDestination = [this](const core::Destination& destination) {
        if (PageView* view = currentView()) view->goToDestination(destination);
    };
    connect(bookmarks_, &BookmarksPane::activated, this, goToDestination);
    connect(destinations_, &DestinationsPane::activated, this, goToDestination);

    // THE LOG PANE -- T14. Its own dock along the bottom rather than a pane in the sidebar: the
    // sidebar's subject is the document and the log's is the application. Hidden, because a shell
    // that opens with its log showing looks like a shell in trouble.
    logPane_ = new LogPane(services_.logBuffer, this);
    addDockWidget(Qt::BottomDockWidgetArea, logPane_);
    logPane_->hide();

    // ORDER MATTERS, and not for a cosmetic reason. `ActionFactory` reads each action's
    // registration state at the moment it builds the button, so anything this window registers has
    // to be registered BEFORE the ribbon is built -- otherwise the shell's own commands render as
    // permanently disabled "not built yet" buttons, which is the honest state for the other ~355
    // ids and a lie about these three.
    registerActions();
    buildHeader();
    buildCentralSurface();
    buildAutoHideStrips();

    // AFTER the header, because it is the header's widgets it restores into -- and after
    // registerActions(), because a restored QAT command is rendered by the same factory that reads
    // registration state as it builds a button.
    loadUiCustomization();

    connect(sidebar_, &SidebarDock::autoHiddenChanged, this, [this]() { updateAutoHideStrips(); });
    connect(sidebar_, &QDockWidget::dockLocationChanged, this, [this]() { updateAutoHideStrips(); });
    connect(sidebar_, &QDockWidget::visibilityChanged, this, [this]() { updateAutoHideStrips(); });
    updateAutoHideStrips();
}

MainWindow::~MainWindow()
{
    // THE KEYBOARD GOES FIRST. The binder filters the APPLICATION's events, so it is reached from
    // outside this object; leaving it installed while ~QWidget takes the window apart is the same
    // hazard as I-12's signal connection, arriving through a different door. Releasing it here
    // removes the filter while `this` is still a whole MainWindow.
    shortcuts_.reset();

    // AND THE STRIP FILTERS WITH IT, for the same reason. `~QWidget` hides this window's children,
    // and an event delivered to a strip while `this` is a half-destroyed MainWindow would dispatch
    // `eventFilter` on an object whose derived part is gone -- issue I-12's hazard reached through
    // the event system rather than through a signal.
    if (leftStrip_) leftStrip_->removeEventFilter(this);
    if (rightStrip_) rightStrip_->removeEventFilter(this);

    // NOT `= default`, and the reason is a real crash rather than a precaution.
    //
    // The lambdas below are connected to the SIDEBAR's signals with `this` as the receiver, and Qt
    // drops such a connection when the receiver's QObject part is destroyed. That happens LAST --
    // after ~MainWindow has run. In between, ~QWidget closes the window, which hides its children,
    // which makes QDockWidget emit visibilityChanged, which calls a MainWindow member function on
    // an object whose MainWindow part no longer exists. Offscreen it segfaults inside
    // qt_metacall; on a real desktop it would be whatever the freed memory happened to contain.
    //
    // Found by tests/ui on its first run (issue I-12), which is the same way T7 found its pipe
    // race: by running the thing, not by reading it.
    disconnect(sidebar_, nullptr, this, nullptr);

    // The same reasoning for T11's three connections. `~QWidget` takes the header down with the
    // window, and a QAT rebuild or a placement change reaching a half-destroyed MainWindow would be
    // I-12 a second time -- the class of bug that is invisible until the day it is not.
    if (qat_) disconnect(qat_, nullptr, this, nullptr);
    if (ribbon_) disconnect(ribbon_, nullptr, this, nullptr);
    if (tabs_) disconnect(tabs_, nullptr, this, nullptr);

    // OUT OF THE SET, and out of `g_activeShell` with it. The registry outlives every window and
    // holds handlers that resolve `activeShell()` at dispatch time; a destroyed window left in the
    // set would be dispatched into. This is the same class of bug as I-12 -- an object reached
    // after its destructor ran -- arriving through a different door.
    shellWindowSet().removeAll(this);
    if (g_activeShell == this)
        g_activeShell = shellWindowSet().isEmpty() ? nullptr : shellWindowSet().last();

    // The window that inherits `activeShell()` inherits the `Visible` lane with it (S4 T11).
    refreshWindowFocusState();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
        g_activeShell = this;
        refreshWindowFocusState();   // S4 T11 -- the window in front keeps the `Visible` lane
    }
}

void MainWindow::buildHeader()
{
    actionFactory_ = std::make_unique<ActionFactory>(
        services_.actions,
        // The context every ribbon dispatch is attributed to, computed from the focus chain by
        // `ShortcutBinder::activeContext()` -- T13, and it was one line exactly as the callback was
        // shaped to make it. Capturing `this` is safe here in a way it is NOT in a registry handler
        // (see the class comment): the factory is owned by this window and dies with it, whereas a
        // registration outlives every window that made it.
        [this] { return shortcuts_->activeContext(); });

    // setMenuWidget, not a QToolBar in the top dock area. A ribbon REPLACES the menu bar rather than
    // sitting under one, and the menu-widget slot is the only place QMainWindow puts a widget above
    // the dock areas -- so a docked pane cannot end up above the ribbon, which it can if the ribbon
    // is a toolbar. At T11 the slot holds a column rather than the ribbon alone, because the Quick
    // Access Toolbar has to be inside the same slot to be above or below the ribbon without a pane
    // ever landing between them.
    header_ = new QWidget(this);
    header_->setObjectName(QStringLiteral("shellHeader"));
    headerColumn_ = new QVBoxLayout(header_);
    headerColumn_->setContentsMargins(0, 0, 0, 0);
    headerColumn_->setSpacing(0);

    ribbon_ = new Ribbon(header_);
    headerColumn_->addWidget(ribbon_);

    qat_ = new QuickAccessToolbar(*actionFactory_, header_);
    applyQatPlacement();

    // Persist on every change rather than at shutdown. `Settings::sync()` runs at teardown
    // (T8/I-14), but a customization that only reaches the file if the process exits cleanly is a
    // customization a crash takes with it -- and this is the stage that built a crash handler.
    connect(qat_, &QuickAccessToolbar::commandsChanged, this, [this]() { saveUiCustomization(); });
    connect(qat_, &QuickAccessToolbar::lockedChanged, this, [this](bool) { saveUiCustomization(); });
    connect(qat_, &QuickAccessToolbar::placementChanged, this, [this](bool) {
        applyQatPlacement();
        saveUiCustomization();
    });

    // The tool finder rides in the tab strip's right-hand corner, which is where the reference
    // (P-4) puts it and where it does not compete with the tabs for the left edge.
    toolFinder_ = new ToolFinder(services_.actions, services_.keyBindings);
    ribbon_->setCornerWidget(toolFinder_);

    // Right-click a ribbon control -> offer to put THAT command on the toolbar. The ribbon knows
    // which control; the toolbar knows what to do with it; neither knows about the other. See
    // QuickAccessToolbar.h on the parameterized-command question this is the Stage 1 answer to.
    connect(ribbon_, &Ribbon::controlContextMenuRequested, this,
            [this](core::ActionId id, const QPoint& globalPos) {
                if (qat_) qat_->showAddMenu(globalPos, id);
            });

    setMenuWidget(header_);

    RibbonTabBuilder builder(*ribbon_, *actionFactory_);
    const QStringList problems = builder.build(ribbon::shipped());
    for (const QString& problem : problems)
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "ribbon:" << problem;

    ribbon_->setCurrentPage(0);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    // D1-7(b), guide case A-54. Every action on this window must be a RawPageAction with exactly one
    // receiver on `triggered`, and that receiver dispatches through ActionRegistry.
    //
    // The two severities are deliberate. In a debug build a developer is present and an assert is
    // the fastest possible feedback; in a release build, killing a user's session over a wiring
    // mistake would be a worse failure than the mistake. Neither is silence.
    const QList<QString> complaints = ActionFactory::wiringComplaints(this);
    for (const QString& complaint : complaints)
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "action wiring:" << complaint;
    Q_ASSERT_X(complaints.isEmpty(), "MainWindow::showEvent",
               "an action on this window bypasses ActionRegistry -- see the log lines above");

    // T13's half of the same rule. A `QAction` carrying its own key sequence, or a free-standing
    // `QShortcut`, resolves its key through Qt against no context and no user remap -- it works,
    // and it makes §8's per-context bindings a document about something the product does not do.
    const QList<QString> keyComplaints = ShortcutBinder::shortcutComplaints(this);
    for (const QString& complaint : keyComplaints)
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "shortcut wiring:" << complaint;
    Q_ASSERT_X(keyComplaints.isEmpty(), "MainWindow::showEvent",
               "a chord on this window bypasses ShortcutBinder -- see the log lines above");
}

void MainWindow::buildCentralSurface()
{
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralSurface"));

    auto* row = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    const QString stripLabel = verticalize(catalog::translate(catalog::key::SidebarTitle));

    leftStrip_ = makeEdgeStrip(central, stripLabel, catalog::translate(catalog::key::SidebarShow));
    leftStrip_->setObjectName(QStringLiteral("sidebarStripLeft"));
    row->addWidget(leftStrip_);

    auto* body = new QFrame;
    body->setObjectName(QStringLiteral("placeholderSurface"));
    body->setFrameShape(QFrame::NoFrame);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(48, 48, 48, 48);
    column->setSpacing(12);
    column->addStretch(1);

    auto* heading = new QLabel(catalog::translate(catalog::key::PlaceholderHeading), body);
    heading->setObjectName(QStringLiteral("placeholderHeading"));
    QFont headingFont = heading->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 2.0);
    heading->setFont(headingFont);
    heading->setAlignment(Qt::AlignHCenter);
    column->addWidget(heading);

    auto* bodyText = new QLabel(catalog::translate(catalog::key::PlaceholderBody), body);
    bodyText->setObjectName(QStringLiteral("placeholderBody"));
    bodyText->setWordWrap(true);
    bodyText->setAlignment(Qt::AlignHCenter);
    column->addWidget(bodyText);

    // What this build's OS bundle cannot do, named rather than implied. platform/platform.h makes
    // an unimplemented interface a NULL member precisely so that this is printable instead of being
    // discovered by a caller getting no result -- so the shell prints it.
    const std::vector<std::string_view> missing =
        platform::unimplementedInterfaces(services_.platform);
    auto* interfaces = new QLabel(body);
    interfaces->setObjectName(QStringLiteral("placeholderInterfaces"));
    interfaces->setAlignment(Qt::AlignHCenter);
    interfaces->setText(catalog::translate(catalog::key::PlaceholderInterfaces)
                            .arg(static_cast<int>(platform::kPlatformInterfaceCount - missing.size()))
                            .arg(static_cast<int>(platform::kPlatformInterfaceCount)));
    column->addWidget(interfaces);

    column->addStretch(2);
    placeholder_ = body;

    // THE DOCUMENT COLUMN -- T12. The tab strip over a two-page stack: the placeholder when there
    // is nothing open, the documents when there is. Two stacks rather than one with the placeholder
    // at index 0, so that a document's index in the strip and its index in the stack are the same
    // number -- arithmetic that has to be written twice is arithmetic that goes wrong quietly.
    auto* documentColumn = new QWidget(central);
    documentColumn->setObjectName(QStringLiteral("documentColumn"));
    auto* documentLayout = new QVBoxLayout(documentColumn);
    documentLayout->setContentsMargins(0, 0, 0, 0);
    documentLayout->setSpacing(0);

    tabs_ = new DocumentTabBar(documentColumn);
    documentLayout->addWidget(tabs_);

    // BETWEEN THE TAB STRIP AND THE PAGE (S2 T14). Above the strip it would be a statement about
    // the window, and it is a statement about the document in front -- which is also why
    // `updateDocumentChrome()` clears it on every tab change. It hides itself when empty.
    messageBar_ = new MessageBar(documentColumn);
    documentLayout->addWidget(messageBar_);

    // THE BANNER'S COLOURS COME FROM THIS WINDOW'S THEME, and follow it: `themeChanged` fires only
    // when the RESOLVED theme moves, which is the only thing the two tokens depend on (issue I-29).
    const auto paintMessageBar = [this] {
        messageBar_->setColors(theme_->color(ThemeToken::MessageBackground),
                               theme_->color(ThemeToken::MessageText));
        messageBar_->setFailureColors(theme_->color(ThemeToken::ErrorBackground),
                                      theme_->color(ThemeToken::ErrorText));
    };
    paintMessageBar();
    connect(theme_.get(), &ThemeManager::themeChanged, messageBar_, paintMessageBar);

    documents_ = new QStackedWidget;
    documents_->setObjectName(QStringLiteral("documentStack"));

    body_ = new QStackedWidget(documentColumn);
    body_->setObjectName(QStringLiteral("shellBody"));
    body_->addWidget(placeholder_);   // page 0
    body_->addWidget(documents_);     // page 1
    documentLayout->addWidget(body_, 1);

    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < documents_->count()) documents_->setCurrentIndex(index);
        updateDocumentChrome();
    });
    connect(tabs_, &DocumentTabBar::closeRequested, this, [this](int index) { closeDocument(index); });
    connect(tabs_, &QTabBar::tabMoved, this, [this](int from, int to) {
        // The strip reordered itself; the stack has to follow, or the tab at position 3 shows the
        // document that used to be there. QStackedWidget has no move, so the widget is taken out
        // and put back -- `removeWidget` does not delete it, which is what this relies on.
        QWidget* moved = documents_->widget(from);
        if (!moved) return;
        documents_->removeWidget(moved);
        documents_->insertWidget(to, moved);
        documents_->setCurrentIndex(tabs_->currentIndex());
    });
    connect(tabs_, &DocumentTabBar::tearOffRequested, this,
            [this](int index, const QPoint& globalPos) { detachDocument(index, globalPos); });

    row->addWidget(documentColumn, 1);

    rightStrip_ = makeEdgeStrip(central, stripLabel, catalog::translate(catalog::key::SidebarShow));
    rightStrip_->setObjectName(QStringLiteral("sidebarStripRight"));
    row->addWidget(rightStrip_);

    setCentralWidget(central);
    updateDocumentChrome();
}

// =======================================================================================
// Documents -- T12. Every one of them a stub (D1-2): a path, a name, and a placeholder.
// =======================================================================================

int MainWindow::documentCount() const { return documents_ ? documents_->count() : 0; }

DocumentWindow* MainWindow::documentAt(int index) const
{
    if (!documents_ || index < 0 || index >= documents_->count()) return nullptr;
    return qobject_cast<DocumentWindow*>(documents_->widget(index));
}

DocumentWindow* MainWindow::currentDocument() const
{
    return tabs_ ? documentAt(tabs_->currentIndex()) : nullptr;
}

int MainWindow::indexOfPath(const QString& absolutePath) const
{
    for (int index = 0; index < documentCount(); ++index)
        if (DocumentWindow* document = documentAt(index))
            if (document->path() == absolutePath) return index;
    return -1;
}

DocumentWindow* MainWindow::openDocument(const QString& absolutePath, core::DocumentHandle handle)
{
    if (const int existing = indexOfPath(absolutePath); existing >= 0) {
        // The same path twice in one window would give the user two identical tabs and no way to
        // tell them apart. Focusing the one already there is what every editor does.
        //
        // `openFiles()` checks for this BEFORE it asks the backend, so this branch is reached only
        // by a direct caller -- and a handle arriving here would leak, since nothing would ever own
        // it. Closing it is the honest recovery rather than an assertion nobody would see.
        if (handle.isValid()) {
            RAWPAGE_LOG_DEBUG(LogSubsystem::Ui)
                << "open: already open in this window; releasing the duplicate handle for"
                << absolutePath;
            services_.backend.close(handle);
        }
        activateDocument(existing);
        return documentAt(existing);
    }

    auto* document = new DocumentWindow(services_, absolutePath, handle);
    adoptDocument(document);
    return document;
}

void MainWindow::adoptDocument(DocumentWindow* document, int index)
{
    if (!document || !documents_ || !tabs_) return;

    // THE VIEW TELLS THE CHROME, and it has to: the status bar's page number changes when the user
    // SCROLLS, which no command and no tab change goes through. Without this the numbers were
    // correct exactly once, when the tab was adopted, and then stayed there while the document
    // moved under them -- which is worse than the em dash, because it is a confident wrong answer.
    //
    // Connected here rather than in `DocumentWindow` because a tab is re-adopted by another window
    // on a re-dock, and this window is the one whose status bar must follow.
    //
    // DISCONNECT THEN CONNECT, and not `Qt::UniqueConnection`: **Qt refuses a unique connection to
    // a lambda** -- "Unique connection requires the slot to be a pointer to a member function of a
    // QObject subclass" -- and it refuses it with an `ASSERT` that fail-fasts the process rather
    // than with a warning, so the mistake reads as 42 unrelated suites dying at once. The explicit
    // disconnect does the same job and says what it is doing.
    if (PageView* view = document->view()) {
        disconnect(view, &PageView::viewChanged, this, nullptr);
        connect(view, &PageView::viewChanged, this,
                [this](int, int, double) { updateViewChrome(); });
    }

    // AND IT FOLLOWS THE SPLIT'S ACTIVE PANE (S4 T9). The connection above is to ONE view, and a
    // split gives a tab up to four -- so the status bar would go on reporting the page and zoom of
    // whichever pane happened to be active when the tab was adopted, which is the confident wrong
    // answer the block above exists to prevent, arriving by a second route.
    //
    // EVERY PANE OF THIS TAB IS DISCONNECTED AND THE NEW ACTIVE ONE RECONNECTED, rather than the
    // previously bound pane being remembered. A second copy of "which pane was the status bar
    // listening to" is a second thing to keep in agreement with the container, which already
    // knows; walking at most four views costs nothing and cannot disagree.
    disconnect(document, &DocumentWindow::activeViewChanged, this, nullptr);
    connect(document, &DocumentWindow::activeViewChanged, this, [this, document](PageView* view) {
        for (PageView* pane : document->split()->views())
            disconnect(pane, &PageView::viewChanged, this, nullptr);
        connect(view, &PageView::viewChanged, this, [this](int, int, double) { updateViewChrome(); });
        updateViewChrome();
    });

    // THE DOCUMENT TELLS THE CHROME TOO (Stage 3 T10): a command, an undo or a redo re-asks every action
    // and moves the dirty marker; a recovery journal that stopped recording becomes a banner the document
    // keeps. Disconnected in `releaseDocument`, so a tab torn off into another window stops reaching this one.
    disconnect(document, nullptr, this, nullptr);
    connect(document, &DocumentWindow::mutated, this, [this, document] {
        updateDocumentTitles();
        // THE PANE IS TOLD THE NEW REVISION, not re-pointed at the document (Stage 4 T5). A
        // `setDocument` would reset the selection and the scroll, so rotating page 40 would send
        // the pane back to page 1 -- and the revision is part of every request's key, so a pane
        // that was not told would show the un-rotated page with no error and no failing test
        // (D2-12's argument, arriving at the second consumer of the scheduler).
        if (thumbnails_ && document == currentDocument()) {
            if (const core::CommandJournal* journal = document->journal())
                thumbnails_->modelChanged(journal->revision());
        }
        updateViewChrome();
    });
    connect(document, &DocumentWindow::guideMenuRequested, this,
            [this](int guideIndex, QPoint global) { showGuideMenu(guideIndex, global); });

    // THE SELECTION'S COPY, WORDED (S5 T5, D5-9). Progress while a copy reads its pages, then what reached
    // the clipboard -- or why nothing did. Only for the tab in front: a copy finishing in a tab the
    // person has left must not overwrite the status of the one they are looking at.
    connect(document, &DocumentWindow::selectionCopyProgress, this, [this, document](int done, int total) {
        if (document != currentDocument() || !status_) return;
        status_->setTaskProgress(done, total);
        status_->setMessage(catalog::translate(catalog::key::StatusSelectionPreparing));
    });
    connect(document, &DocumentWindow::selectionCopied, this, [this, document](int characters, int unmapped) {
        if (document != currentDocument() || !status_) return;
        status_->clearTaskProgress();
        status_->setMessage(unmapped > 0
                                ? catalog::translate(catalog::key::StatusSelectionCopiedSkipped).arg(characters).arg(unmapped)
                                : catalog::translate(catalog::key::StatusSelectionCopied).arg(characters));
    });
    connect(document, &DocumentWindow::selectionNotExtractable, this, [this, document](int unmapped) {
        if (document != currentDocument() || !status_) return;
        status_->clearTaskProgress();
        status_->setMessage(catalog::translate(catalog::key::StatusSelectionNotExtractable).arg(unmapped));
    });
    connect(document, &DocumentWindow::selectionCopyCancelled, this, [this, document] {
        if (document != currentDocument() || !status_) return;
        status_->clearTaskProgress();
        status_->setMessage(catalog::translate(catalog::key::StatusSelectionCopyCancelled));
    });
    connect(document, &DocumentWindow::selectionPresenceChanged, this, [this](bool) {
        if (actionFactory_) actionFactory_->refresh();
    });

    // SYNCHRONIZED SCROLLING BETWEEN TWO WINDOWS (S4 T11). The tab says it was scrolled; the
    // ARRANGER decides who follows, because a tab may not name another window. Connected here
    // rather than inside `DocumentWindow` for the same reason the status bar's connection is: a tab
    // is re-adopted by another window on a re-dock, and this is the object that outlives that.
    connect(document, &DocumentWindow::scrolled, this,
            [document](PageView* pane) { WindowArranger::mirrorScroll(*document, pane); });
    connect(document, &DocumentWindow::dirtyChanged, this, [this](bool) {
        updateDocumentTitles();
        updateViewChrome();
    });
    connect(document, &DocumentWindow::recoveryUnavailable, this, [this, document](const QString& reason) {
        const QString text = catalog::translate(catalog::key::MessageBarRecoveryUnavailable)
                                 .arg(document->title(), reason);
        document->setCondition(static_cast<int>(MessageBar::Kind::RecoveryUnavailable), text);
        if (document == currentDocument() && messageBar_)
            messageBar_->showMessage({MessageBar::Kind::RecoveryUnavailable, text, {}, {}});
    });

    const int at = (index < 0 || index > documentCount()) ? documentCount() : index;
    documents_->insertWidget(at, document);
    tabs_->insertTab(at, document->title());
    tabs_->setTabToolTip(at, document->path());
    tabs_->setCurrentIndex(at);
    documents_->setCurrentIndex(at);
    updateDocumentChrome();

    // S4 T11. A tab adopted into a window that is not in front must start in that window's lane,
    // or a document dragged into a background window would render at `Visible` until something
    // else happened to change the focus.
    refreshWindowFocusState();
}

DocumentWindow* MainWindow::releaseDocument(int index)
{
    DocumentWindow* document = documentAt(index);
    if (!document) return nullptr;

    // REMOVED, NOT DELETED -- and reparented to nothing, so the caller genuinely owns it. A tab
    // moved between windows keeps its identity; at Stage 2 that identity is a document, a journal
    // and a scroll position, and a move that destroyed and rebuilt it would discard all three.
    disconnect(document, nullptr, this, nullptr);
    documents_->removeWidget(document);
    document->setParent(nullptr);
    tabs_->removeTab(index);
    updateDocumentChrome();
    return document;
}

void MainWindow::closeIfEmptied()
{
    if (documentCount() > 0) return;

    // ONLY A WINDOW THAT OWNS ITSELF CAN GO. `WA_DeleteOnClose` is set on every window this class
    // creates -- a tear-off, and `window.new` -- and on no other. The PRIMARY window is a stack
    // object in `app/main.cpp`, which Qt may not delete and which `close()` would merely hide,
    // leaving a hidden entry in the window set for `Ctrl+F6` to walk into and for a single-instance
    // forward to call `openFiles()` on. So it stays, and that is not a shortfall: an empty primary
    // window is exactly the state a launch with no files produces, which is a place the product is
    // designed to sit.
    if (!testAttribute(Qt::WA_DeleteOnClose)) return;

    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "window closing: its last document was moved to another";

    // `close()` on a `WA_DeleteOnClose` window posts a DeferredDelete rather than destroying now,
    // which is what makes this safe from here: this call is reached from `DocumentTabBar`'s own
    // mouse handler, by way of a signal, and destroying the strip inside its own event would leave
    // that handler returning through freed memory. The event loop does it once the stack is gone.
    close();
}

core::Status MainWindow::closeDocument(int index, std::optional<CloseDisposition> disposition)
{
    DocumentWindow* document = documentAt(index);
    if (!document) return {};

    if (document->isDirty()) {
        if (!disposition) {
            // The question is about a document, so that document is in front while it is asked.
            activateDocument(index);
            disposition = UnsavedChangesDialog::ask(this, document->title());
        }
        if (!disposition) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "close refused: unsaved changes and nobody to ask about"
                                                 << document->path();
            return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.close.needsDisposition"),
                               document->title()};
        }
        switch (*disposition) {
        case CloseDisposition::Cancel:
            return core::Error{core::ErrorCode::Cancelled, QLatin1String("error.close.cancelled"), document->title()};
        case CloseDisposition::Discard:
            RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "close: changes discarded for" << document->path();
            document->discardRecovery();
            break;
        case CloseDisposition::Save:
            if (!saveDocument(document) || document->isDirty())
                return core::Error{core::ErrorCode::Cancelled, QLatin1String("error.close.notSaved"),
                                   document->title()};
            break;
        }
        // THE TAB BY ITS WIDGET, never by its path: a Save As moved the path, and an untitled document has
        // none -- two of them would both answer to the empty path, and the first would be closed (Stage 3 T11).
        index = documents_->indexOf(document);
    }

    if (DocumentWindow* released = releaseDocument(index)) released->deleteLater();
    return {};
}

core::Status MainWindow::closeAllDocuments()
{
    while (documentCount() > 0) {
        if (core::Status closed = closeDocument(0); !closed) return closed;
    }
    return {};
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    for (int index = documentCount() - 1; index >= 0; --index) {
        DocumentWindow* document = documentAt(index);
        if (!document || !document->isDirty()) continue;
        if (const core::Status closed = closeDocument(index); !closed) {
            event->ignore();
            return;
        }
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::setSaveAsAnswerForTesting(SaveAsAnswer answer)
{
    installedSaveAsAnswer() = std::move(answer);
}

bool MainWindow::canAskSavePath()
{
    return static_cast<bool>(installedSaveAsAnswer()) || canPromptTheUser();
}

namespace {

// The page "current page" means. ONE reading for the dialog's label and the rotation, so the page the
// dialog names is the page that turns.
int rotationPageOf(const DocumentWindow& document)
{
    return document.view() ? document.view()->controller().currentPage() : 0;
}

} // namespace

core::Status MainWindow::rotatePages(DocumentWindow* document, const RotatePagesDialog::Choice& choice)
{
    if (!document || !document->journal())
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()};

    RotationPlan plan = planRotation(choice, rotationPageOf(*document), document->pageCount());
    core::CommandJournal& journal = *document->journal();

    // ONE GROUP, ONE UNDO STEP (D3-13). A command refused part-way leaves the ones before it applied and in
    // the step -- `CommandJournal`'s rule -- so the group is still closed, and one `Ctrl+Z` takes it back.
    if (plan.group) journal.beginGroup(*plan.group);
    core::Status status;
    for (std::unique_ptr<core::Command>& command : plan.commands) {
        status = journal.run(std::move(command));
        if (!status) break;
    }
    if (plan.group) journal.endGroup();

    if (!status) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "rotate refused for" << document->path() << "--"
                                             << status.error().messageKey;
    }
    return status;
}

void MainWindow::reportSaveFailure(DocumentWindow* document, const core::Error& error)
{
    RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "save failed for" << document->path() << "--" << error.messageKey
                                         << error.context;
    const QString text = catalog::translate(catalog::key::MessageBarSaveFailed)
                             .arg(document->title(), catalog::translateError(error.messageKey, error.context));
    document->setCondition(static_cast<int>(MessageBar::Kind::SaveFailed), text);
    if (document == currentDocument() && messageBar_)
        messageBar_->showMessage({MessageBar::Kind::SaveFailed, text, {}, {}});
}

core::Status MainWindow::saveDocument(DocumentWindow* document)
{
    if (!document) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), QString()};

    core::Result<core::SaveOutcome> saved = document->save();
    if (!saved) {
        reportSaveFailure(document, saved.error());
        return saved.error();
    }

    if (saved.value().kind == core::SaveOutcome::Kind::SaveAsRequired) {
        // THE EXPLANATION FIRST, THEN THE QUESTION (guide case A-77): a Save that opens a Save As with no
        // word of why reads as the product ignoring the command.
        //
        // `documents/warnOnRepairedSave` off (Stage 3 T12, D3-6) skips the explanation for a REPAIRED document
        // and for nothing else -- and skips only the explanation: the Save As below runs either way, because
        // the setting may never turn the forcing off. A file changed on disk or an untitled document still
        // says why.
        const bool repaired = QLatin1String(saved.value().reasonKey) == QLatin1String("error.save.repairedDocument");
        const bool explain = !repaired || services_.settings.get(core::keys::documents::warnOnRepairedSave);
        if (explain) {
            // `translateError`, not `translate`: since I-15 a reason can be a file-system refusal
            // whose text takes the path (`error.file.readOnly`). A reason that takes no argument is
            // returned unchanged, so the three older ones read exactly as before.
            const QString why = catalog::translateError(QString(saved.value().reasonKey), saved.value().reasonContext);
            const QString text = catalog::translate(catalog::key::MessageBarSaveAsRequired).arg(document->title(), why);
            document->setCondition(static_cast<int>(MessageBar::Kind::SaveAsRequired), text);
            if (document == currentDocument() && messageBar_)
                messageBar_->showMessage({MessageBar::Kind::SaveAsRequired, text, {}, {}});
        }
        RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "save: Save As required for" << document->path() << "--"
                                          << saved.value().reasonKey
                                          << (explain ? "" : "(explanation off: documents/warnOnRepairedSave)");
        return saveDocumentAs(document);
    }

    document->clearCondition(static_cast<int>(MessageBar::Kind::SaveFailed));
    if (messageBar_ && document == currentDocument()) messageBar_->dismiss(MessageBar::Kind::SaveFailed);
    updateDocumentTitles();
    updateViewChrome();
    return {};
}

core::Status MainWindow::saveDocumentAs(DocumentWindow* document)
{
    if (!document) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), QString()};

    std::optional<QString> target;
    const QString proposed = document->suggestedSavePath();
    if (installedSaveAsAnswer()) {
        target = installedSaveAsAnswer()(proposed);
    } else if (canPromptTheUser()) {
        const QString chosen = QFileDialog::getSaveFileName(this, catalog::translate(catalog::key::SaveAsTitle),
                                                            proposed,
                                                            catalog::translate(catalog::key::SaveAsFilter));
        if (!chosen.isEmpty()) target = chosen;
    }
    if (!target) return core::Error{core::ErrorCode::Cancelled, QLatin1String("error.save.cancelled"), document->title()};

    core::Result<core::SaveOutcome> saved = document->saveAs(*target);
    if (!saved) {
        reportSaveFailure(document, saved.error());
        return saved.error();
    }
    if (saved.value().kind == core::SaveOutcome::Kind::SaveAsRequired) {
        // Either the target named the document's own file, which still cannot be saved in place, or the
        // chosen path refused the write on its own account (I-15). Not asked again in a loop: the banner
        // already says why, and the user chooses another path with Save As.
        const core::Error refused{core::ErrorCode::Unavailable, saved.value().reasonKey,
                                  saved.value().reasonContext.isEmpty() ? document->title()
                                                                        : saved.value().reasonContext};
        reportSaveFailure(document, refused);
        return refused;
    }

    document->clearCondition(static_cast<int>(MessageBar::Kind::SaveFailed));
    document->clearCondition(static_cast<int>(MessageBar::Kind::SaveAsRequired));
    document->clearCondition(static_cast<int>(MessageBar::Kind::Recovered));   // saved: nothing recovered is unsaved now
    if (const int index = documents_->indexOf(document); index >= 0) tabs_->setTabToolTip(index, document->path());
    updateDocumentChrome();
    return {};
}

core::Result<DocumentWindow*> MainWindow::recoverDocument(const core::RecoveryCandidate& candidate)
{
    const QString directory = candidate.directory;
    const QFileInfo source(candidate.meta.sourcePath);

    // A RECOVERY THAT DID NOT HAPPEN IS SAID, not only logged -- Stage 2 I-32's lesson, where a refused open
    // produced no tab and no word. A window-scoped banner, one line per refusal, as `openFiles` reports; the
    // crashed session's directory is kept, so the next launch can offer it again.
    const auto refuse = [this, &source, &directory](const core::Error& error) -> core::Result<DocumentWindow*> {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "recovery: kept" << directory << "--" << error.messageKey << error.context;
        if (messageBar_) {
            const QString name = source.fileName().isEmpty() ? QFileInfo(directory).fileName() : source.fileName();
            QString text = catalog::translate(catalog::key::MessageBarRecoveryFailed)
                               .arg(name, catalog::translateError(error.messageKey, error.context));
            if (messageBar_->isShowing(MessageBar::Kind::OpenFailed))
                text = messageBar_->textFor(MessageBar::Kind::OpenFailed) + QLatin1Char('\n') + text;
            messageBar_->showMessage({MessageBar::Kind::OpenFailed, text, {}, {}});
        }
        return error;
    };

    if (candidate.problemKey.size() != 0)
        return refuse(core::Error{core::ErrorCode::ParseFailure, candidate.problemKey, QFileInfo(directory).fileName()});
    if (!source.exists() || !source.isFile())
        return refuse(core::Error{core::ErrorCode::NotFound, QLatin1String("error.recovery.sourceMissing"), source.fileName()});

    // INTO THE FILE only when it is still the file the changes were made to AND the document was that file.
    // Anything else is a new, untitled document, and the source is never written (D3-11).
    const bool intoFile = candidate.sourceMatches && !candidate.meta.untitled;
    const QString sourcePath = source.absoluteFilePath();
    if (intoFile && indexOfPath(sourcePath) >= 0)
        return refuse(core::Error{core::ErrorCode::Unavailable, QLatin1String("error.recovery.alreadyOpen"), source.fileName()});

    // No password prompt: a document this build could not edit never had a recovery journal, so an encrypted
    // source means the file was replaced by one -- and the open refuses it by key, which is the right answer.
    const core::Result<core::DocumentHandle> opened =
        services_.backend.open(sourcePath, [](int) { return std::optional<QString>(); });
    if (!opened)
        return refuse(opened.error());

    auto* document = new DocumentWindow(services_, opened.value(),
                                        DocumentWindow::Recovery{directory, sourcePath, !intoFile});
    if (!document->recoveryReport()) {
        // THE JOURNAL COULD NOT BE REPLAYED AT ALL. The document is closed again -- a clean tab nobody asked for
        // is not a recovery -- and the crashed session's directory stays for another attempt.
        const core::Error failure = document->recoveryFailure().value_or(
            core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.recovery.badJournal"), directory});
        delete document;
        return refuse(failure);
    }
    const core::ReplayReport report = *document->recoveryReport();

    // THE BANNER, one sentence per fact, one per line -- a locale-neutral join, as `openFiles` joins refusals.
    QStringList lines;
    const QString sourceName = source.fileName();
    lines << catalog::translate(catalog::key::MessageBarRecovered).arg(document->title(), QString::number(report.stepsApplied));
    if (!intoFile) {
        lines << catalog::translate(candidate.meta.untitled ? catalog::key::MessageBarRecoveredUntitled
                                                            : catalog::key::MessageBarRecoveredSourceChanged)
                     .arg(sourceName);
    }
    if (report.stoppedAtStep) {
        lines << catalog::translate(catalog::key::MessageBarRecoveredStopped)
                     .arg(QString::number(*report.stoppedAtStep),
                          catalog::translateError(QString(report.stopKey), report.stopContext));
    }
    if (report.tornTrailingLine || report.droppedUnterminatedGroup)
        lines << catalog::translate(catalog::key::MessageBarRecoveredIncomplete);
    document->setCondition(static_cast<int>(MessageBar::Kind::Recovered), lines.join(QLatin1Char('\n')));

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "recovery:" << report.stepsApplied << "step(s) replayed from" << directory << "into"
        << (intoFile ? sourcePath : QStringLiteral("an untitled document from ") + sourcePath)
        << "-- stopped at step" << (report.stoppedAtStep ? *report.stoppedAtStep : 0) << report.stopKey
        << (report.tornTrailingLine ? "; a torn last line ignored" : "")
        << (report.droppedUnterminatedGroup ? "; an unfinished group dropped" : "");

    adoptDocument(document);
    if (intoFile) {
        tabs_->setTabToolTip(documents_->indexOf(document), sourcePath);
        services_.recentFiles.note(sourcePath);
    }

    // THE CRASHED SESSION'S DIRECTORY GOES once its steps are this document's: they are in the new journal,
    // seeded from what replay applied -- or that journal has said, in its own banner, that it cannot keep them,
    // which is the state of every document whose recovery is unavailable. Kept on a refusal, and said so.
    if (const core::Status discarded = core::RecoveryJournal::discardDirectory(directory, services_.platform.durableFiles.get());
        !discarded) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "recovery: the recovered session's directory was kept --"
                                             << discarded.error().messageKey << directory;
    }
    updateDocumentChrome();
    return document;
}

void MainWindow::activateDocument(int index)
{
    if (!tabs_ || index < 0 || index >= documentCount()) return;
    tabs_->setCurrentIndex(index);
}

void MainWindow::stepDocument(int delta)
{
    const int count = documentCount();
    if (count < 2) return;
    const int from = tabs_->currentIndex();
    activateDocument(((from + delta) % count + count) % count);
}

MainWindow* MainWindow::detachDocument(int index, const QPoint& globalPos)
{
    DocumentWindow* document = documentAt(index);
    if (!document) return nullptr;

    // DROPPED ON ANOTHER WINDOW'S STRIP, if that is where the pointer is. One gesture, two
    // outcomes, and the tab strip cannot tell them apart because it knows only its own tabs -- see
    // DocumentTabBar's class comment.
    for (MainWindow* other : shellWindowSet()) {
        if (other == this || !other->tabs_) continue;
        const QPoint local = other->tabs_->mapFromGlobal(globalPos);
        if (!other->tabs_->rect().contains(local)) continue;

        const int at = other->tabs_->tabAt(local);
        other->adoptDocument(releaseDocument(index), at);
        other->raiseToForeground();
        closeIfEmptied();   // I-35 -- nothing below this line may touch `this`
        return other;
    }

    // A WINDOW'S ONLY TAB HAS NOWHERE TO GO (issue I-33). Detaching it into a new window is a null
    // move: the same lone document ends up alone in a window either way, and the user is left with
    // a second, empty shell to close. Refuse, and the drag simply does nothing.
    //
    // THE PLACEMENT IS THE POINT -- this sits AFTER the loop above, not before it. A torn-off
    // window holds exactly one document by construction, so guarding at the top of this function
    // would make dragging it back the one re-dock nobody could perform, which is the commonest one
    // there is and a required step of M-03.
    if (documentCount() <= 1) {
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "tear-off refused: this window's only document has nowhere to go";
        return nullptr;
    }

    auto* window = new MainWindow(services_);
    window->setAttribute(Qt::WA_DeleteOnClose);   // a torn-off window closes for good
    window->adoptDocument(releaseDocument(index));
    if (!globalPos.isNull()) window->move(globalPos - QPoint(80, 20));
    window->show();
    window->raiseToForeground();
    return window;
}

void MainWindow::updateDocumentChrome()
{
    if (!tabs_ || !body_) return;

    const int count = documentCount();
    tabs_->setVisible(count > 0);
    body_->setCurrentIndex(count > 0 ? 1 : 0);

    DocumentWindow* current = currentDocument();
    updateDocumentTitles();

    if (status_) {
        // The status bar was built DISABLED at T9 because there was no document, and it stayed
        // that way through T13 because a page on screen is not the same as a page COUNTED. It is
        // counted now: `updateViewChrome()` below fills in the real numbers, and the em dash comes
        // back when the last tab closes rather than being replaced by a guess.
        status_->setDocumentPresent(current != nullptr);
        status_->setMessage(current ? current->title()
                                    : catalog::translate(catalog::key::StatusNoDocument));
    }

    updateDocumentPanes();

    if (messageBar_) {
        // THE BANNER BELONGS TO THE TAB, not to the window. A "this file was repaired" banner left
        // on screen over the document that replaced it is worse than no banner: it is a false
        // statement about the file the user is now looking at, and Stage 3 forces a Save-As on
        // exactly that statement.
        messageBar_->clearDocumentConditions();
        if (current) showDocumentConditions(current);
    }

    updateViewChrome();
}

void MainWindow::updateDocumentTitles()
{
    // THE DIRTY MARKER IS THE CATALOG'S, not a hard-coded asterisk: `shell.document.dirtyTitle` places it,
    // and a locale that marks a changed document differently says so there (guide case A-76).
    const auto titleOf = [](const DocumentWindow* document) {
        return document->isDirty() ? catalog::translate(catalog::key::DocumentDirtyTitle).arg(document->title())
                                   : document->title();
    };
    if (tabs_) {
        for (int index = 0; index < documentCount() && index < tabs_->count(); ++index)
            if (const DocumentWindow* document = documentAt(index)) tabs_->setTabText(index, titleOf(document));
    }
    const DocumentWindow* current = currentDocument();
    setWindowTitle(current ? catalog::translate(catalog::key::WindowTitleWithDocument).arg(titleOf(current))
                           : catalog::translate(catalog::key::WindowTitle));
}

void MainWindow::promptForZoom()
{
    PageView* view = currentView();
    if (!view) return;

    // THE DIALOG COLLECTS A PARAMETER AND NOTHING ELSE -- the standing rule about dialogs, and the
    // reason this is four lines. `QInputDialog` supplies its own translated chrome; the prompt is
    // ours and comes from the action's own label, so the box and the command cannot disagree.
    bool accepted = false;
    const int percent = QInputDialog::getInt(
        this, catalog::translateAction(core::ActionId::ViewZoomTo),
        catalog::translateAction(core::ActionId::ViewZoomTo),
        qRound(view->controller().zoom().factor * 100.0),
        qRound(core::render::Zoom::kMin * 100.0), qRound(core::render::Zoom::kMax * 100.0), 1,
        &accepted);
    if (!accepted) return;

    view->setZoom(core::render::Zoom::percent(percent));
    updateViewChrome();
}

void MainWindow::promptForPage()
{
    PageView* view = currentView();
    if (!view || view->pageCount() <= 0) return;

    bool accepted = false;
    const int page = QInputDialog::getInt(
        this, catalog::translateAction(core::ActionId::NavGoToPage),
        catalog::translateAction(core::ActionId::NavGoToPage),
        view->controller().currentPage() + 1, 1, view->pageCount(), 1, &accepted);
    if (!accepted) return;

    // ONE-BASED AS TYPED, zero-based in the model -- the same conversion the status bar's page box
    // makes, and it is here rather than inside `goToPage` so there is exactly one convention in
    // `core/` and exactly one place each surface converts.
    view->goToPage(page - 1);
    updateViewChrome();
}

void MainWindow::registerViewerActions()
{
    // THE §8 VIEWER TABLE, or the subset `Implementation-Plan.md` § Stage 2 lists. The rest of that
    // table -- `Ctrl+4` Reflow, `Ctrl+R` rotate PAGES, `S`, `Ctrl+B`, `Ctrl+E`, `Ctrl+L`, `Ctrl+U`,
    // `Ctrl+'`, `Ctrl+;`, `Ctrl+Alt+C`, `Ctrl+.`/`Ctrl+,`, `Ctrl+]`/`Ctrl+[` -- belongs to Stages 3
    // to 13 and stays `NotFound`, which deliberately leaves the key unconsumed. That list is in
    // `Stage2-Rendering-Plan.md` §10 and is not re-decided here.
    //
    // EVERY HANDLER RESOLVES THE VIEW AT DISPATCH TIME, for convention 14's reason in its sharpest
    // form yet: a handler capturing a `PageView` would act on a tab that has since been closed or
    // torn off into a different window, and the registry is one object shared by every window.
    const auto registerView = [this](ActionId id, void (PageView::*method)(),
                                     core::ActionRegistry::Enable enable = {},
                                     std::string_view unavailableKey = {}) {
        if (services_.actions.isRegistered(id)) return;

        core::ActionRegistry::Registration registration;
        registration.handler = [method](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                (view->*method)();
                window->updateViewChrome();
            }
        };
        registration.enable = std::move(enable);
        registration.unavailableKey = unavailableKey;

        const core::Result<void> registered =
            services_.actions.register_(id, std::move(registration));
        if (registered) return;
        const std::string_view name = core::actionStringId(id);
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "viewer action registration refused for"
            << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()))
            << "--" << registered.error().messageKey;
    };

    // A view is the precondition for every one of these. It is also I-53's SUBJECT: with no
    // document these are registered and refused, which is the kind of disabled that said nothing at
    // all until this task -- and now says "needs a document to be open" rather than the generic
    // sentence, because the registration names a reason.
    const auto hasView = [] {
        MainWindow* window = activeShell();
        return window != nullptr && window->currentView() != nullptr
               && window->currentView()->pageCount() > 0;
    };

    // --- Zoom -----------------------------------------------------------------------------------
    registerView(ActionId::ViewZoomIn, &PageView::zoomIn, hasView,
                 "shell.tooltip.needsDocument");
    registerView(ActionId::ViewZoomOut, &PageView::zoomOut, hasView,
                 "shell.tooltip.needsDocument");

    const auto registerFit = [this, hasView](ActionId id, FitMode fit) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = [fit](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                view->setFitMode(fit);
                view->refresh();
                window->updateViewChrome();
            }
        };
        registration.enable = hasView;
        registration.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(id, std::move(registration));
    };

    registerFit(ActionId::ViewFitPage, FitMode::Page);
    registerFit(ActionId::ViewFitWidth, FitMode::Width);
    registerFit(ActionId::ViewFitVisible, FitMode::Visible);

    // ACTUAL SIZE IS NOT A FIT. It is 100 % at the monitor's PHYSICAL DPI -- a ruler on the glass
    // agreeing with the ruler on the page -- which is what `DpiPolicy` takes three inputs for, and
    // it is a fixed zoom rather than a mode that survives a resize.
    if (!services_.actions.isRegistered(ActionId::ViewActualSize)) {
        core::ActionRegistry::Registration actualSize;
        actualSize.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                view->setZoom(core::render::Zoom::actualSize());
                window->updateViewChrome();
            }
        };
        actualSize.enable = hasView;
        actualSize.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::ViewActualSize, std::move(actualSize));
    }

    // `view.zoomTo` raises a dialog, so it is DISABLED WHERE THERE IS NO USER -- convention 16, and
    // Stage 1's I-23: a modal `exec()` under the offscreen platform is shown to nobody and never
    // returns, which stopped a whole `ctest` run dead with no failure and no output.
    if (!services_.actions.isRegistered(ActionId::ViewZoomTo)) {
        core::ActionRegistry::Registration zoomTo;
        zoomTo.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            window->promptForZoom();
        };
        zoomTo.enable = [hasView] { return hasView() && canPromptTheUser(); };
        zoomTo.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::ViewZoomTo, std::move(zoomTo));
    }

    // --- Navigation -----------------------------------------------------------------------------
    // The two page steps carry their OWN tooltip reason. "Not available for this document" is true
    // of Next Page on the last page and is not the useful thing to say; `unavailableKey` exists
    // exactly so a registration can be more specific than the generic sentence, and this is the
    // first pair of commands in the product to use it. It is a SEPARATE field from `tooltipKey`
    // because the two are shown in mutually exclusive states and say opposite things -- one field
    // serving both put "needs a document to be open" on every enabled Viewer button, which is what
    // the first attempt at I-53 in this task actually did.
    const auto notLastPage = [] {
        MainWindow* window = activeShell();
        const PageView* view = window ? window->currentView() : nullptr;
        return view != nullptr && view->controller().currentPage() < view->pageCount() - 1;
    };
    const auto notFirstPage = [] {
        MainWindow* window = activeShell();
        const PageView* view = window ? window->currentView() : nullptr;
        return view != nullptr && view->controller().currentPage() > 0;
    };

    registerView(ActionId::NavFirstPage, &PageView::firstPage, notFirstPage,
                 "shell.tooltip.atFirstPage");
    registerView(ActionId::NavLastPage, &PageView::lastPage, notLastPage,
                 "shell.tooltip.atLastPage");
    registerView(ActionId::NavNextPage, &PageView::nextPage, notLastPage,
                 "shell.tooltip.atLastPage");
    registerView(ActionId::NavPrevPage, &PageView::prevPage, notFirstPage,
                 "shell.tooltip.atFirstPage");

    // A SCREEN STEP IS NOT A PAGE STEP and is enabled on the last page: there is usually more of it
    // below the fold. Giving it `notLastPage` would grey out `Page Down` halfway down the last page.
    registerView(ActionId::NavNextScreen, &PageView::nextScreen, hasView,
                 "shell.tooltip.needsDocument");
    registerView(ActionId::NavPrevScreen, &PageView::prevScreen, hasView,
                 "shell.tooltip.needsDocument");

    registerView(ActionId::NavPrevView, &PageView::goBackView, [] {
        MainWindow* window = activeShell();
        const PageView* view = window ? window->currentView() : nullptr;
        return view != nullptr && view->controller().canGoBack();
    }, "shell.tooltip.needsDocument");
    registerView(ActionId::NavNextView, &PageView::goForwardView, [] {
        MainWindow* window = activeShell();
        const PageView* view = window ? window->currentView() : nullptr;
        return view != nullptr && view->controller().canGoForward();
    }, "shell.tooltip.needsDocument");

    if (!services_.actions.isRegistered(ActionId::NavGoToPage)) {
        core::ActionRegistry::Registration goTo;
        goTo.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (window) window->promptForPage();
        };
        goTo.enable = [hasView] { return hasView() && canPromptTheUser(); };
        goTo.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::NavGoToPage, std::move(goTo));
    }

    // --- The view's rotation, which is NOT the document's --------------------------------------
    registerView(ActionId::ViewRotateViewCw, &PageView::rotateViewClockwise, hasView,
                 "shell.tooltip.needsDocument");
    registerView(ActionId::ViewRotateViewCcw, &PageView::rotateViewCounterClockwise, hasView,
                 "shell.tooltip.needsDocument");

    // --- Layout modes ---------------------------------------------------------------------------
    // `registerMode` SELECTS one arrangement outright; it reports its own state so the ribbon
    // renders a check mark saying which is in force -- `ActionFactory` reads `checked` for that,
    // and `isToggle` decides checkability (I-21).
    //
    // **REPORTING STATE IS NOT THE SAME AS TOGGLING, and calling both of them "toggle" is what hid
    // S2 issue I-23 for eight days**: `ViewTwoUp` was registered here, so `Ctrl+Shift+T` set
    // `TwoUpContinuous` every time and could never leave it, while `checked` made it look like a
    // toggle to every surface that asked. The two chords below are the real toggles and neither
    // goes through this lambda.
    const auto registerMode = [this, hasView](ActionId id, core::render::LayoutMode mode) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = [mode](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                view->setLayoutMode(mode);
                window->updateViewChrome();
            }
        };
        registration.checked = [mode] {
            MainWindow* window = activeShell();
            const PageView* view = window ? window->currentView() : nullptr;
            return view != nullptr && view->controller().layoutMode() == mode;
        };
        registration.enable = hasView;
        registration.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(id, std::move(registration));
    };

    registerMode(ActionId::ViewSinglePage, core::render::LayoutMode::Single);
    registerMode(ActionId::ViewContinuous, core::render::LayoutMode::SingleContinuous);

    // `Ctrl+Shift+T` TOGGLES two-up and PRESERVES the continuity, which is the exact mirror of
    // `Ctrl+Shift+H` below -- one chord owns one axis of `LayoutMode` and leaves the other alone.
    // `Features.md` §8 calls it "Toggle Two-Up" and that table is the authority (I-23).
    //
    // `checked` asks `isTwoUp`, NOT equality with one mode: the button means *two-up is on*, and
    // `TwoUp` and `TwoUpContinuous` are both on. Equality is what `registerMode` wants and is why
    // this cannot go through it.
    if (!services_.actions.isRegistered(ActionId::ViewTwoUp)) {
        core::ActionRegistry::Registration twoUp;
        twoUp.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            PageView* view = window->currentView();
            if (!view) return;

            using core::render::LayoutMode;
            const LayoutMode now = view->controller().layoutMode();
            view->setLayoutMode(core::render::isTwoUp(now)
                                    ? (core::render::isContinuous(now) ? LayoutMode::SingleContinuous
                                                                       : LayoutMode::Single)
                                    : (core::render::isContinuous(now) ? LayoutMode::TwoUpContinuous
                                                                       : LayoutMode::TwoUp));
            window->updateViewChrome();
        };
        twoUp.checked = [] {
            MainWindow* window = activeShell();
            const PageView* view = window ? window->currentView() : nullptr;
            return view != nullptr && core::render::isTwoUp(view->controller().layoutMode());
        };
        twoUp.enable = hasView;
        twoUp.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::ViewTwoUp, std::move(twoUp));
    }

    // `Ctrl+Shift+H` TOGGLES between paged and continuous rather than selecting one, which is what
    // its §8 row says ("toggle single-page <-> continuous scrolling") and is a different command
    // from the two selects above. It preserves the two-up-ness, exactly as `Ctrl+Shift+T`
    // preserves the continuity.
    if (!services_.actions.isRegistered(ActionId::ViewTogglePageMode)) {
        core::ActionRegistry::Registration toggle;
        toggle.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            PageView* view = window->currentView();
            if (!view) return;

            using core::render::LayoutMode;
            const LayoutMode now = view->controller().layoutMode();
            view->setLayoutMode(core::render::isContinuous(now)
                                    ? (core::render::isTwoUp(now) ? LayoutMode::TwoUp
                                                                  : LayoutMode::Single)
                                    : (core::render::isTwoUp(now) ? LayoutMode::TwoUpContinuous
                                                                  : LayoutMode::SingleContinuous));
            window->updateViewChrome();
        };
        toggle.enable = hasView;
        toggle.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::ViewTogglePageMode, std::move(toggle));
    }

    // --- The tools (S2 T14's three bindings; S4 T7's real state machine) ------------------------
    // Stage 2 registered three ids against a three-value enum on `PageView`. **T7 re-points them
    // at `ui/viewer/ToolController`** and adds a fourth, the Loupe -- the sticky arming, the
    // temporary-hand modifier, `Esc` cancellation and the cursor now live there. The registrations
    // themselves are unchanged in shape, which is the point of having had them: the chord, the
    // ribbon button and the tool finder all reached `setTool` before and still do.
    const auto registerTool = [this, hasView](ActionId id, ToolKind tool) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = [tool](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                view->setTool(tool);
                window->updateViewChrome();
            }
        };
        registration.checked = [tool] {
            MainWindow* window = activeShell();
            const PageView* view = window ? window->currentView() : nullptr;
            return view != nullptr && view->tool() == tool;
        };
        registration.enable = hasView;
        registration.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(id, std::move(registration));
    };

    registerTool(ActionId::ToolHand, ToolKind::Hand);
    registerTool(ActionId::ToolSelect, ToolKind::Select);
    registerTool(ActionId::ToolMarqueeZoom, ToolKind::MarqueeZoom);

    // S4 · T7's FOURTH TOOL. `tool.loupe` is one of the two ids this stage declares — see the
    // note at its `ActionId.def` row — and it has no §8 chord, so it is reached from the ribbon,
    // the tool finder and the batch runner. `Features.md` §8 is untouched by this stage.
    registerTool(ActionId::ToolLoupe, ToolKind::Loupe);

    // --- `edit.copy` and `edit.selectAll` (S5 · T5, D5-9) ----------------------------------------
    //
    // §8's COMMON rows, so they consume `Ctrl+C` and `Ctrl+A` wherever the key falls through to Common
    // -- and a text widget that owns the key keeps it, because `ShortcutBinder` offers it to the focused
    // widget first through Qt's `ShortcutOverride` handshake (guide A-52: Stage 4's I-06 turned round).
    // Both reach the DOCUMENT WINDOW's selection, which every pane of a split shares (D5-8).
    //
    // No `copyAsTable` or `copyFormatted` id: one copy writes every flavour and the paste target
    // chooses (D5-9). `Ctrl+A` is the whole DOCUMENT (Q6) and extracts nothing by itself; the copy that
    // follows reads what it needs at `Background`, with progress, and `Esc` cancels it.
    {
        const auto selectionInFront = []() -> SelectionOverlay* {
            MainWindow* window = activeShell();
            DocumentWindow* document = window ? window->currentDocument() : nullptr;
            return document ? document->selection() : nullptr;
        };

        if (!services_.actions.isRegistered(ActionId::EditCopy)) {
            core::ActionRegistry::Registration copy;
            copy.handler = [selectionInFront](KeyContext) {
                if (SelectionOverlay* selection = selectionInFront()) (void)selection->copy();
            };
            copy.enable = [selectionInFront] {
                const SelectionOverlay* selection = selectionInFront();
                return selection != nullptr && selection->hasSelection();
            };
            copy.unavailableKey = "shell.tooltip.needsSelection";
            services_.actions.register_(ActionId::EditCopy, std::move(copy));
        }

        if (!services_.actions.isRegistered(ActionId::EditSelectAll)) {
            core::ActionRegistry::Registration selectAll;
            selectAll.handler = [selectionInFront](KeyContext) {
                if (SelectionOverlay* selection = selectionInFront()) selection->selectAll();
            };
            selectAll.enable = hasView;
            selectAll.unavailableKey = "shell.tooltip.needsDocument";
            services_.actions.register_(ActionId::EditSelectAll, std::move(selectAll));
        }
    }

    // --- `view.autoScroll` (S4 · T7) --------------------------------------------------------------
    //
    // **`edit.cancel` IS DELIBERATELY NOT REGISTERED HERE, AND THE REASON IS ITS CONTEXT.** `Esc`
    // is `Common`'s row in §8 — it applies in every context unless one overrides it — and the
    // contexts that DO override it belong to stages that do not exist yet: `find.close` in
    // FindSearch (S5), `form.revertField` in FormsFill (S9), `present.exit` in FullScreen (T10),
    // `edit.cancel` again in AnnotationTools (S6). Registering the Common row here would make T7
    // the owner of what `Esc` means in nine contexts it did not build.
    //
    // It would also break `Esc` everywhere immediately, which is how this was found rather than
    // reasoned: a registered command that is DISABLED answers `Unavailable`, and
    // `ShortcutBinder::consumes` counts that as consuming the key — so a disabled `edit.cancel`
    // ate the `Esc` that puts the tool finder's result popup away, and `tst_toolfinder`'s
    // *the result list can always be put away* went red. That list is a `Qt::Popup` holding a
    // mouse grab (Stage 1's **I-44**), so swallowing its `Esc` is not cosmetic.
    //
    // So cancelling a drag is handled where the drag is: `PageView::keyPressEvent`. A drag is not
    // a command and neither is abandoning one — no more than a mouse-move is — and leaving the id
    // unregistered keeps the key unconsumed for the tool finder, the find bar and the form field.
    // The day `edit.cancel` is registered, it needs an owner that knows all nine contexts.
    {
        const auto toolsInFront = []() -> ToolController* {
            MainWindow* window = activeShell();
            PageView* view = window ? window->currentView() : nullptr;
            return view ? &view->tools() : nullptr;
        };

        core::ActionRegistry::Registration autoScroll;
        autoScroll.handler = [toolsInFront](KeyContext) {
            if (ToolController* tools = toolsInFront()) tools->toggleAutoScroll();
        };
        // DISABLED AT ZERO SPEED, because starting an auto-scroll that cannot move is a command
        // that reports success and does nothing. The speed is `pageDisplay/autoScrollSpeed`, and
        // its SIGN is the direction — reverse has no id of its own for that reason.
        autoScroll.enable = [toolsInFront] {
            const ToolController* tools = toolsInFront();
            return tools != nullptr && tools->autoScrollSpeed() != 0;
        };
        autoScroll.checked = [toolsInFront] {
            const ToolController* tools = toolsInFront();
            return tools != nullptr && tools->autoScrollRunning();
        };
        autoScroll.unavailableKey = "shell.tooltip.needsDocument";
        if (!services_.actions.isRegistered(ActionId::ViewAutoScroll))
            services_.actions.register_(ActionId::ViewAutoScroll, std::move(autoScroll));
    }

    // --- Rulers, grid, guides and the page colour (S4 T8) ----------------------------------------
    //
    // **THE THREE §8 CHORDS WRITE A SETTING, AND THAT IS THE WHOLE HANDLER.** `Ctrl+U`, `Ctrl+'`
    // and `Ctrl+;` toggle `units/showRulers`, `units/showGrid` and `units/snapToGrid`; every open
    // `DocumentWindow` subscribes to the `UnitsAndGuides` group and applies the change. A handler
    // that reached into the view in front of it instead would make the toggle mean *this tab* on
    // Monday and *the preference* on Tuesday, and the state would not survive a restart.
    //
    // `checked` therefore reads the SETTING and not the widget, which is the same fact read from
    // the place that owns it (`SettingsKeys.def` rule 2 across surfaces).
    // **THE HANDLER MUST NOT CAPTURE THIS WINDOW** -- convention 14, and it cost a crash here
    // before it cost a comment. A registration outlives the window that made it: the guard above
    // means the FIRST window to open registers these, and the second window's ribbon then asks
    // the first window's captured `services_` for a value. In `tst_recoverydialog`, where a case
    // launches twice, that read was of a destroyed object and the segfault was inside
    // `Settings::get` with `this == nullptr` -- during `ActionFactory::create`, before a single
    // assertion had run. `activeShell()` is the way to reach the window in front, and it is the
    // way every other registration in this file already does it.
    const auto settingsInFront = []() -> core::Settings* {
        MainWindow* window = activeShell();
        return window ? &window->services_.settings : nullptr;
    };

    const auto registerSettingToggle = [this, hasView, settingsInFront](ActionId id,
                                                                       core::Key<bool> key) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = [settingsInFront, key](KeyContext) {
            if (core::Settings* settings = settingsInFront())
                settings->set(key, !settings->get(key));
        };
        registration.checked = [settingsInFront, key] {
            const core::Settings* settings = settingsInFront();
            return settings != nullptr && settings->get(key);
        };
        registration.enable = hasView;
        registration.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(id, std::move(registration));
    };

    registerSettingToggle(ActionId::ViewToggleRulers, core::keys::units::showRulers);
    registerSettingToggle(ActionId::ViewToggleGrid, core::keys::units::showGrid);
    registerSettingToggle(ActionId::ViewToggleSnapToGrid, core::keys::units::snapToGrid);
    registerSettingToggle(ActionId::ViewToggleGuides, core::keys::units::showGuides);

    // `view.invertColors` has no chord and no settings row: it is a per-VIEW reading convenience,
    // like the view rotation beside it, and a person reading one scanned document inverted does
    // not mean *every document from now on*. **A paint, not a render flag** -- D4-11, and guide
    // case A-52 asserts that toggling it enqueues nothing.
    //
    // **`view.pageBackground` IS DELIBERATELY NOT REGISTERED.** `Features.md` §1.2 offers *invert
    // OR SUBSTITUTE* page colours, and substitution needs a colour pair that no declared key holds
    // -- D4-5's fifteen (eighteen as built) contain none, and inventing one here would be a Stage
    // 4 key nothing planned. The id stays declared and unregistered, which leaves nothing consumed
    // and leaves a row to fill in; `Stage4-ViewingSurface-Testing-Guide.md` §8 carries it.
    if (!services_.actions.isRegistered(ActionId::ViewInvertColors)) {
        core::ActionRegistry::Registration invert;
        invert.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                view->setInvertColors(!view->invertColors());
                window->updateViewChrome();
            }
        };
        invert.checked = [] {
            MainWindow* window = activeShell();
            const PageView* view = window ? window->currentView() : nullptr;
            return view != nullptr && view->invertColors();
        };
        invert.enable = hasView;
        invert.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::ViewInvertColors, std::move(invert));
    }

    // --- The three guide commands (S4 T8) ---------------------------------------------------------
    // None takes an argument, because `dispatch` does not: lock and delete act on the guide the
    // context menu was raised over, recorded by `GridGuideLayer::setContextGuide` at that moment
    // (Stage 1's I-50). Each is DISABLED when there is no such guide -- convention 16's rule, in
    // the form where the missing thing is a target rather than a user.
    const auto layerInFront = []() -> GridGuideLayer* {
        MainWindow* window = activeShell();
        PageView* view = window ? window->currentView() : nullptr;
        return view ? &view->guides() : nullptr;
    };

    if (!services_.actions.isRegistered(ActionId::ViewLockGuide)) {
        core::ActionRegistry::Registration lock;
        lock.handler = [layerInFront](KeyContext) {
            GridGuideLayer* layer = layerInFront();
            if (!layer) return;
            const int index = layer->contextGuide();
            if (index < 0) return;
            layer->setGuideLocked(index, !layer->guides()[static_cast<std::size_t>(index)].locked);
        };
        lock.checked = [layerInFront] {
            const GridGuideLayer* layer = layerInFront();
            if (!layer) return false;
            const int index = layer->contextGuide();
            return index >= 0 && layer->guides()[static_cast<std::size_t>(index)].locked;
        };
        lock.enable = [layerInFront] {
            const GridGuideLayer* layer = layerInFront();
            return layer != nullptr && layer->contextGuide() >= 0;
        };
        lock.unavailableKey = "shell.tooltip.needsGuide";
        services_.actions.register_(ActionId::ViewLockGuide, std::move(lock));
    }

    if (!services_.actions.isRegistered(ActionId::ViewDeleteGuide)) {
        core::ActionRegistry::Registration remove;
        remove.handler = [layerInFront](KeyContext) {
            GridGuideLayer* layer = layerInFront();
            if (layer) layer->removeGuide(layer->contextGuide());
        };
        // A LOCKED GUIDE IS NOT DELETED FROM HERE. `GridGuideLayer::clearGuides` makes the same
        // refusal for the same reason: the lock's meaning is *leave this one alone*, and a delete
        // that ignored it would make locking protection against nothing.
        remove.enable = [layerInFront] {
            const GridGuideLayer* layer = layerInFront();
            if (!layer) return false;
            const int index = layer->contextGuide();
            return index >= 0 && !layer->guides()[static_cast<std::size_t>(index)].locked;
        };
        remove.unavailableKey = "shell.tooltip.needsGuide";
        services_.actions.register_(ActionId::ViewDeleteGuide, std::move(remove));
    }

    if (!services_.actions.isRegistered(ActionId::ViewClearGuides)) {
        core::ActionRegistry::Registration clear;
        clear.handler = [layerInFront](KeyContext) {
            if (GridGuideLayer* layer = layerInFront()) layer->clearGuides();
        };
        clear.enable = [layerInFront] {
            const GridGuideLayer* layer = layerInFront();
            return layer != nullptr && layer->guideCount(-1) > 0;
        };
        clear.unavailableKey = "shell.tooltip.needsGuide";
        services_.actions.register_(ActionId::ViewClearGuides, std::move(clear));
    }

    registerPresentationActions();
}

// =============================================================================================
// The seven §8 Full Screen rows (S4 T10, D4-12)
// =============================================================================================
// Every one of these was DECLARED at Stage 1 with its chord in `KeyBindingDefaults.def` and
// registered by nothing, so `Features.md` §8's whole Full Screen table resolved `NotFound` and
// left its keys unconsumed. T10 is what fills it in; **no id and no chord is added**, which is
// what keeps plan §10's *"§8 is not touched"* true for this task as it was for T7, T8 and T9.
//
// Nothing is captured and the controller is resolved at DISPATCH (convention 14) -- which matters
// here for the ordinary reason and one extra: these commands are dispatched while a PRESENTATION
// SURFACE has focus, and that surface belongs to a controller that any of them may destroy.
void MainWindow::registerPresentationActions()
{
    const auto controller = []() -> FullScreenController* {
        MainWindow* window = activeShell();
        return window ? window->fullScreen() : nullptr;
    };

    // PRESENTING IS THE PRECONDITION FOR ALL SEVEN. Each is registered with an `enable` that says
    // so rather than with a handler that checks and returns: a disabled command reports
    // `Unavailable` and a batch run can tell that apart from one that ran and did nothing.
    const auto presenting = [controller] {
        const FullScreenController* active = controller();
        return active != nullptr && active->isPresenting();
    };

    // ENTERING, which none of the seven below can do: every one of them resolves in the
    // `FullScreen` context and is therefore unreachable until something has already entered it.
    // Registered here with its own enablement rather than through `registerPresent`, because its
    // precondition is the opposite one -- a document and NOT already presenting.
    if (!services_.actions.isRegistered(ActionId::PresentStart)) {
        core::ActionRegistry::Registration start;
        start.handler = [controller](KeyContext) {
            MainWindow* window = activeShell();
            FullScreenController* active = controller();
            if (window == nullptr || active == nullptr) return;
            active->enterPresentation(window, window->currentDocument());
        };
        start.enable = [controller] {
            MainWindow* window = activeShell();
            const FullScreenController* active = controller();
            return window != nullptr && active != nullptr && !active->isPresenting()
                   && window->currentDocument() != nullptr
                   && window->currentDocument()->pageCount() > 0;
        };
        start.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::PresentStart, std::move(start));
    }

    const auto registerPresent = [this](ActionId id, core::ActionRegistry::Handler handler,
                                        core::ActionRegistry::Enable enable,
                                        core::ActionRegistry::Checked checked) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = std::move(handler);
        registration.enable = std::move(enable);
        registration.checked = std::move(checked);
        registration.unavailableKey = "shell.tooltip.needsPresentation";
        services_.actions.register_(id, std::move(registration));
    };

    // `Esc` and `F11`. **ENABLED AT EITHER LEVEL, not only while presenting** -- it is the way out
    // of level 1 too, and a Leave Full Screen that was disabled in full screen would be the one
    // command a person needs when they cannot see the chrome.
    registerPresent(ActionId::PresentExit,
                    [controller](KeyContext) {
                        if (FullScreenController* active = controller()) active->exit();
                    },
                    [controller] {
                        const FullScreenController* active = controller();
                        return active != nullptr && active->isActive();
                    },
                    {});

    registerPresent(ActionId::PresentBlankBlack,
                    [controller](KeyContext) {
                        if (FullScreenController* active = controller()) active->toggleBlack();
                    },
                    presenting,
                    [controller] {
                        const FullScreenController* active = controller();
                        return active != nullptr
                               && active->blank() == FullScreenController::Blank::Black;
                    });

    registerPresent(ActionId::PresentBlankWhite,
                    [controller](KeyContext) {
                        if (FullScreenController* active = controller()) active->toggleWhite();
                    },
                    presenting,
                    [controller] {
                        const FullScreenController* active = controller();
                        return active != nullptr
                               && active->blank() == FullScreenController::Blank::White;
                    });

    registerPresent(ActionId::PresentLaserPointer,
                    [controller](KeyContext) {
                        if (FullScreenController* active = controller()) active->toggleLaserPointer();
                    },
                    presenting,
                    [controller] {
                        const FullScreenController* active = controller();
                        return active != nullptr && active->laserPointer();
                    });

    registerPresent(ActionId::PresentToggleCursor,
                    [controller](KeyContext) {
                        if (FullScreenController* active = controller()) active->toggleCursorVisible();
                    },
                    presenting,
                    [controller] {
                        const FullScreenController* active = controller();
                        return active != nullptr && active->cursorVisible();
                    });

    registerPresent(ActionId::PresentAutoAdvance,
                    [controller](KeyContext) {
                        if (FullScreenController* active = controller()) active->toggleAutoAdvance();
                    },
                    presenting,
                    [controller] {
                        const FullScreenController* active = controller();
                        return active != nullptr && active->autoAdvancing();
                    });

    // REFUSED WITH ONE MONITOR, rather than enabled and doing nothing. `Ctrl+Shift+M` on a
    // single-display machine is the commonest case there is, and a command that appears to work is
    // worse than one that says it cannot -- Stage 1's I-53, at its fourth subject this stage.
    registerPresent(ActionId::PresentNextMonitor,
                    [controller](KeyContext) {
                        if (FullScreenController* active = controller()) active->moveToNextMonitor();
                    },
                    [presenting] { return presenting() && QGuiApplication::screens().size() > 1; },
                    {});
}

void MainWindow::showGuideMenu(int guideIndex, QPoint global)
{
    if (!actionFactory_) return;

    // Parented and `WA_DeleteOnClose`, exactly as the QAT's is: the items are `RawPageAction`s,
    // and a menu kept between right-clicks would accumulate a set of them per click.
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // THE TWO THAT NEED A GUIDE ARE OFFERED ONLY OVER ONE, rather than shown disabled: a
    // right-click on a ruler is not a failed attempt to lock something, and listing *Delete Guide*
    // greyed out there would read as a product that could not tell what was under the cursor.
    if (guideIndex >= 0) {
        actionFactory_->addToMenu(menu, ActionId::ViewLockGuide);
        actionFactory_->addToMenu(menu, ActionId::ViewDeleteGuide);
        actionFactory_->addMenuSeparator(menu);
    }

    actionFactory_->addToMenu(menu, ActionId::ViewToggleGuides);
    actionFactory_->addToMenu(menu, ActionId::ViewClearGuides);
    actionFactory_->addMenuSeparator(menu);
    actionFactory_->addToMenu(menu, ActionId::ViewToggleRulers);
    actionFactory_->addToMenu(menu, ActionId::ViewToggleGrid);
    actionFactory_->addToMenu(menu, ActionId::ViewToggleSnapToGrid);

    menu->popup(global);
}

void MainWindow::showDocumentConditions(DocumentWindow* document)
{
    if (!messageBar_ || !document || !document->handle().isValid()) return;

    // THE REPAIR REPORT IS NOT REACHABLE FROM `ui/` YET, AND THAT IS ISSUE I-08 REPEATING ITSELF
    // EXACTLY AS T13 PREDICTED. `repairReport()` is declared on the concrete
    // `pdf::DocumentBackendImpl`, which `ui/` may never name -- the same position `pages()` was in
    // before T13 moved it onto `core::IDocumentBackend`. T14 makes the same two-line move, at this
    // first consumer rather than speculatively, which is the rule that produced the finding.
    if (const core::RepairReport* report = services_.backend.repairReport(document->handle())) {
        messageBar_->showRepairReport(*report, document->title(), this);
    }

    if (document->isProtected()) {
        MessageBar::Message protectedMessage;
        protectedMessage.kind = MessageBar::Kind::Protected;
        protectedMessage.text = catalog::translate(catalog::key::MessageBarProtected)
                                    .arg(document->title());
        messageBar_->showMessage(std::move(protectedMessage));
    }

    // What the document itself keeps (Stage 3 T10): a failed save, a forced Save As's reason, recovery off.
    for (const auto& [kind, text] : document->conditions())
        messageBar_->showMessage({static_cast<MessageBar::Kind>(kind), text, {}, {}});
}

PageView* MainWindow::currentView() const
{
    DocumentWindow* document = currentDocument();
    return document ? document->view() : nullptr;
}

void MainWindow::updateDocumentPanes()
{
    if (!thumbnails_) return;

    DocumentWindow* current = currentDocument();
    if (current == nullptr) {
        thumbnails_->setDocument({}, {}, nullptr);
        if (bookmarks_) bookmarks_->setDocument({}, nullptr);
        if (destinations_) destinations_->setDocument({}, nullptr);
        return;
    }

    // THE BACKEND'S OWN MODELS, hydrated ONCE at open (D4-3, guide case A-30) and handed out by
    // pointer -- these calls read nothing and touch no engine, which is what makes re-pointing a
    // pane on every tab change free. A null answer means *not a handle of mine*; an EMPTY model
    // means *this document has none*, and each pane shows its empty state for the second rather
    // than an error (`core/backend.h` draws that distinction deliberately).
    if (bookmarks_)
        bookmarks_->setDocument(current->handle(), services_.backend.outline(current->handle()));
    if (destinations_)
        destinations_->setDocument(current->handle(),
                                   services_.backend.destinations(current->handle()));

    // THE MODEL'S TREE FOR WHAT IS SHOWN, THE ENGINE'S FOR THE DELTA (D3-15) -- the same pair
    // `DocumentWindow` hands `PageView`, so an unsaved rotation is in the thumbnail and in the page
    // for the same reason and at the same revision. A document the backend would not hydrate has no
    // journal, and the engine's tree stands alone.
    if (const core::CommandJournal* journal = current->journal()) {
        thumbnails_->setDocument(current->handle(), journal->revision(),
                                 &journal->document().pages(),
                                 services_.backend.pages(current->handle()));
    } else {
        thumbnails_->setDocument(current->handle(), current->revision(),
                                 services_.backend.pages(current->handle()));
    }
}

void MainWindow::updateViewChrome()
{
    if (status_) {
        if (const PageView* view = currentView()) {
            // S4 T12: the LOGICAL label beside the physical index. `Page::label()` is empty for a
            // document with no `/PageLabels`, which is most of them, and the status bar then reads
            // exactly as it did before this task.
            const int page = view->controller().currentPage();
            QString label;
            if (const core::PageTree* tree = view->pages()) {
                if (const core::Page* current = tree->at(page)) label = current->label();
            }
            status_->setPageState(page, view->pageCount(), label);
            status_->setZoom(view->controller().zoom().factor);
        }
    }

    // THE PANE FOLLOWS THE VIEW, SILENTLY. `setCurrentPage` emits nothing (see `ThumbnailsPane.h`),
    // which is what makes this safe to call from the very signal a pane click ends up producing --
    // the structural answer to a selection and a scroll chasing each other.
    if (thumbnails_) {
        if (const PageView* view = currentView())
            thumbnails_->setCurrentPage(view->controller().currentPage());
    }

    // EVERY ACTION IS RE-ASKED, not only the ones that obviously moved. Enablement predicates read
    // the active window's state, so opening a document changes the answer for commands nothing in
    // this function mentions -- and `refresh()` re-computes the TOOLTIP too, which is I-53's half:
    // a control that has just become available must stop saying it is not.
    if (actionFactory_) actionFactory_->refresh();
}

void MainWindow::buildAutoHideStrips()
{
    // HOVER REVEALS, CLICK ALSO REVEALS. The first is the gesture -- reaching for the edge is how a
    // person opens an auto-hidden pane, and it is what manual case M-04 expects (decided 2026-08-30;
    // the strip answered only to a click until then, which is a different product). The second stays
    // because a 24-pixel strip that looks like a button and does nothing when pressed reads as a
    // broken control, and `peek()` is idempotent, so a click on an already-peeked pane is a no-op.
    //
    // An event filter rather than a signal: `QToolButton` has no "the pointer arrived" signal, and
    // `enterEvent` is protected. Filtering the two strips costs one virtual call on their events and
    // nothing on anyone else's.
    leftStrip_->installEventFilter(this);
    rightStrip_->installEventFilter(this);

    // A CLOSED SIDEBAR'S STRIP OPENS IT (S4 guide I-25). The strip stands in for an auto-hidden pane
    // AND for a closed one (`updateAutoHideStrips`), and from Stage 1 T9 until 2026-09-27 both kinds
    // only peeked -- which does nothing to a pane that is not auto-hidden, so a strip whose tooltip
    // says *Show the sidebar* showed nothing, and a sidebar lost to I-24 had no way back but `F4`.
    // A click on a closed pane's strip is `view.toggleSidebar`, the command `F4` and the pane's own
    // close button already are. HOVER stays a peek and nothing else: opening a pinned pane because
    // the pointer crossed the edge would be the pane moving under a person who did not ask.
    const auto reach = [this]() {
        if (sidebar_->isAutoHidden()) {
            sidebar_->peek();
            return;
        }
        if (!services_.actions.dispatch(ActionId::ViewToggleSidebar, core::KeyContext::Global))
            toggleSidebar();   // unregistered in a bare fixture: still open it, as the close button still closes
    };
    connect(leftStrip_, &QToolButton::clicked, this, reach);
    connect(rightStrip_, &QToolButton::clicked, this, reach);

    // Which edge the pane would return to is named in the strip's tooltip rather than given a
    // button. Moving a pane between edges is a rare deliberate act, and two more permanent controls
    // on a 24-pixel strip would cost more than they earn; dragging the pane already does it.
    leftStrip_->setToolTip(leftStrip_->toolTip() + QLatin1String(" (")
                           + catalog::translate(catalog::key::SidebarDockLeft) + QLatin1Char(')'));
    rightStrip_->setToolTip(rightStrip_->toolTip() + QLatin1String(" (")
                            + catalog::translate(catalog::key::SidebarDockRight) + QLatin1Char(')'));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // `peek()` does nothing unless the pane is auto-hidden, so there is no state to check here and
    // no way for a stray Enter to open a pinned pane. The event is NOT consumed: the strip is still
    // a button and still has a tooltip to show.
    if (event->type() == QEvent::Enter && (watched == leftStrip_ || watched == rightStrip_))
        sidebar_->peek();

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateAutoHideStrips()
{
    // A PEEKED PANE IS ON SCREEN, SO ITS STRIP IS NOT (issue I-39). The strip is what stands in for
    // the pane while the pane is away; showing both at once puts a 24-pixel bar beside the very pane
    // it exists to summon. `isAutoHidden()` stays true through a peek -- deliberately, because a peek
    // is a look and not a pin -- so it is the wrong question to ask here on its own.
    //
    // The pointer is standing on the strip at the moment it goes: that is fine and is the reason the
    // pane can be reached at all. The dock area sits OUTSIDE the central widget, so a revealed pane
    // takes the space the strip was drawn in and arrives under the pointer; leaving the pane is what
    // puts it away (`SidebarDock::leaveEvent`).
    const bool collapsed =
        (sidebar_->isAutoHidden() && !sidebar_->isPeeking())
        || (!sidebar_->isVisible() && !sidebar_->isFloating());
    const bool onLeft = dockWidgetArea(sidebar_) != Qt::RightDockWidgetArea;

    leftStrip_->setVisible(collapsed && onLeft);
    rightStrip_->setVisible(collapsed && !onLeft);
}

void MainWindow::applyQatPlacement()
{
    if (!qat_ || !headerColumn_) return;

    // The SAME widget, moved. Building a second toolbar for the second placement would give the two
    // positions two states, and the first thing a user would notice is a customization that only
    // appears in one of them.
    headerColumn_->removeWidget(qat_);
    if (qat_->isBelowRibbon())
        headerColumn_->addWidget(qat_);
    else
        headerColumn_->insertWidget(0, qat_);
}

void MainWindow::loadUiCustomization()
{
    if (!qat_) return;

    // Nothing written back while restoring. Without this, the first launch would persist the
    // factory defaults as though the user had chosen them -- and `isSet` below, which is how "never
    // customized" is told from "emptied on purpose", would answer the same for both from then on.
    suppressCustomizationSave_ = true;

    qat_->setBelowRibbon(services_.settings.get(core::keys::ui::qatBelowRibbon));
    qat_->setLocked(services_.settings.get(core::keys::ui::qatLocked));

    if (services_.settings.isSet(core::keys::ui::qatCommands)) {
        QStringList dropped;
        const std::vector<ActionId> commands = UiCustomization::resolveCommands(
            services_.settings.get(core::keys::ui::qatCommands), &dropped);
        for (const QString& name : dropped) {
            // A saved command whose id no longer exists and has no alias row. Named rather than
            // counted: this is the exact failure D1-16's alias table exists to prevent, so the day
            // one appears the log has to say which.
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                << "quick access toolbar: dropping unknown command" << name;
        }
        qat_->setCommands(commands);
    }

    suppressCustomizationSave_ = false;
}

void MainWindow::saveUiCustomization()
{
    if (!qat_ || suppressCustomizationSave_) return;
    services_.settings.set(core::keys::ui::qatCommands,
                           UiCustomization::toStringIds(qat_->commands()));
    services_.settings.set(core::keys::ui::qatBelowRibbon, qat_->isBelowRibbon());
    services_.settings.set(core::keys::ui::qatLocked, qat_->isLocked());
}

void MainWindow::registerActions()
{
    // Stage 1 registers what a shell window can actually service. An ActionId that is DECLARED and
    // UNREGISTERED renders as a visible, disabled control with a tooltip naming what it is waiting
    // for (Stage1-AppShell-Plan.md 5.1) -- which is the honest state, and is why the other ~340 ids
    // are not given placeholder handlers here.
    //
    // ONCE PER REGISTRY, NOT ONCE PER WINDOW -- T12. There can be several of these windows now, and
    // `ActionRegistry` is one object they share; registration is deliberately not idempotent
    // (property 3), so the second window would collect one "duplicate registration" error per
    // command. Whichever window gets here first registers; the rest find the commands present.
    const auto registerRegistration = [this](ActionId id,
                                             core::ActionRegistry::Registration registration) {
        if (services_.actions.isRegistered(id)) return;

        const core::Result<void> registered = services_.actions.register_(id, std::move(registration));
        if (registered) return;
        const std::string_view name = core::actionStringId(id);
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "action registration refused for"
            << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()))
            << "--" << registered.error().messageKey;
    };

    // EVERY HANDLER BELOW CAPTURES NOTHING AND RESOLVES THE ACTIVE WINDOW AT DISPATCH TIME. A
    // handler holding `this` would bind a Global command to the window that happened to register it
    // -- forever, including after that window was closed and the lambda was left pointing into
    // freed memory. See the class comment in MainWindow.h.
    const auto onActiveShell = [](void (MainWindow::*method)()) {
        return [method](KeyContext) {
            if (MainWindow* window = activeShell()) (window->*method)();
        };
    };

    const auto registerAction = [&registerRegistration](ActionId id,
                                                        core::ActionRegistry::Handler handler,
                                                        core::ActionRegistry::Enable enable = {}) {
        core::ActionRegistry::Registration registration;
        registration.handler = std::move(handler);
        registration.enable  = std::move(enable);
        registerRegistration(id, std::move(registration));
    };

    // --- The shell frame (T9, T10) ----------------------------------------------------------
    registerAction(ActionId::ViewToggleSidebar, onActiveShell(&MainWindow::toggleSidebar));
    registerAction(ActionId::ViewFullScreen, onActiveShell(&MainWindow::toggleFullScreen));

    // R-7 of the A3 criteria, reached as a command rather than as a keypress. F9 is its default
    // chord in Features.md 8 and it starts working at T13, when ShortcutBinder exists -- but the
    // BEHAVIOUR is registered now, which is what makes it reachable from the ribbon, from a test
    // and from the Stage 14 batch runner without a second implementation.
    registerAction(ActionId::ViewToggleRibbon, onActiveShell(&MainWindow::toggleRibbon));

    // S4 T12 -- READING MODE (`Ctrl+L`, `Features.md` §1.2). A window-chrome command and not a view
    // one, which is why it is registered here beside Full Screen rather than with the `registerView`
    // family: there is nothing about it that needs a document, and a person who has just closed
    // their last tab must still be able to get their ribbon back.
    registerAction(ActionId::ViewReadingMode, onActiveShell(&MainWindow::toggleReadingMode));

    // DOCUMENT PROPERTIES (`Ctrl+D`, D4-4). **DISABLED WHERE THERE IS NO USER** -- convention 16
    // and Stage 1's I-23: a modal `exec()` under the offscreen platform is shown to nobody and
    // never returns, which stopped a whole `ctest` run dead with no failure and no output. The
    // facts it shows are composed by `documentProperties()`, which a case drives directly.
    registerAction(ActionId::FileDocumentProperties, [](KeyContext) {
        if (MainWindow* window = activeShell()) window->showDocumentProperties();
    }, [] {
        MainWindow* window = activeShell();
        return window != nullptr && window->currentDocument() != nullptr
               && DocumentPropertiesDialog::canAsk();
    });

    // R-5, AND THE COMMAND THAT MAKES `Alt` MEAN ANYTHING (issue I-42). The ribbon has drawn key
    // tips since T10 and nothing ever asked it to: `Alt` is bound to this id in
    // `KeyBindingDefaults.def`, the chord forms correctly, and the id was simply never registered --
    // so `dispatch` answered `NotFound`, left the key unconsumed, and pressing Alt did nothing in
    // every locale. R-5 had been recorded as met against the T2 spike, which had its own handler.
    // The follow-up letter is the ribbon's own: `showKeyTips()` takes focus, so it arrives in
    // `Ribbon::keyPressEvent` rather than through a second application-wide key path.
    registerAction(ActionId::AppRibbonKeyTips, [](KeyContext) {
        MainWindow* window = activeShell();
        if (!window || !window->ribbon()) return;
        if (window->ribbon()->keyTipsVisible()) window->ribbon()->hideKeyTips();
        else                                    window->ribbon()->showKeyTips();
    });

    // Quit closes EVERY shell window rather than the active one -- `file.close` is the command for
    // one window's document and `file.quit` is the application. Closing them in reverse order so
    // that the set is not mutated from under the loop.
    registerAction(ActionId::FileQuit, [](KeyContext) {
        // A window that refuses -- a dirty document whose close was cancelled -- stops the quit there: the
        // windows before it in the loop are closed, it and the rest stay (D3-14).
        const QList<MainWindow*> windows = shellWindows();
        for (auto it = windows.crbegin(); it != windows.crend(); ++it)
            if (!(*it)->close()) return;
    });

    // --- The document panes (S4 · T5, D4-2) --------------------------------------------------
    // FOUR IDS, and not one more. `pane.thumbnails` opens the pane; `page.goToSelected`,
    // `page.thumbnailsLarger` and `page.thumbnailsSmaller` are the three §8 Page Thumbnails rows
    // this stage services. `Ctrl+R` is in that table too and is NOT here: `page.rotate` was
    // registered at Stage 3 T10 and the pane reaches it by declaring its context and nothing else,
    // which is D4-1's claim made structural rather than promised.
    //
    // The other nine rows in that table -- delete, cut, copy, paste, move earlier/later, extract,
    // duplicate, select-all -- are Stage 7's and stay unregistered, so their keys reach the widget
    // unconsumed rather than being swallowed by a handler that does nothing.
    const auto activeThumbnails = []() -> ThumbnailsPane* {
        MainWindow* window = activeShell();
        return window ? window->thumbnailsPane() : nullptr;
    };

    registerAction(ActionId::PaneThumbnails, [activeThumbnails](KeyContext) {
        MainWindow* window = activeShell();
        if (!window || !window->sidebar()) return;

        // SHOW, SELECT, FOCUS -- in that order, and all three. A command that selected a pane
        // inside a sidebar that is closed or auto-hidden would read as doing nothing; and the focus
        // is what makes the §8 Page Thumbnails table live, because `activeContext()` is computed
        // from the focus chain (D1-5).
        window->sidebar()->setAutoHidden(false);
        window->sidebar()->show();
        window->sidebar()->showPane(ActionId::PaneThumbnails);
        if (ThumbnailsPane* pane = activeThumbnails()) pane->setFocus(Qt::OtherFocusReason);
    });

    // THE THREE PANE-CONTEXT COMMANDS. Each is DISABLED where there is no pane to act on rather
    // than refused after the press -- convention 16's shape, and what makes the ribbon button say
    // so before it is clicked.
    registerAction(
        ActionId::PageGoToSelected,
        [activeThumbnails](KeyContext) {
            if (ThumbnailsPane* pane = activeThumbnails()) pane->activateCurrentPage();
        },
        [activeThumbnails] {
            const ThumbnailsPane* pane = activeThumbnails();
            return pane != nullptr && pane->pageCount() > 0;
        });

    registerAction(
        ActionId::PageThumbnailsLarger,
        [activeThumbnails](KeyContext) {
            if (ThumbnailsPane* pane = activeThumbnails()) pane->larger();
        },
        [activeThumbnails] {
            const ThumbnailsPane* pane = activeThumbnails();
            return pane != nullptr && pane->thumbnailWidth() < ThumbnailsPane::kMaxThumbnailWidth;
        });

    registerAction(
        ActionId::PageThumbnailsSmaller,
        [activeThumbnails](KeyContext) {
            if (ThumbnailsPane* pane = activeThumbnails()) pane->smaller();
        },
        [activeThumbnails] {
            const ThumbnailsPane* pane = activeThumbnails();
            return pane != nullptr && pane->thumbnailWidth() > ThumbnailsPane::kMinThumbnailWidth;
        });

    // --- The two navigation panes (S4 · T6, D4-2 and D4-3) -----------------------------------
    // SEVEN IDS. `pane.bookmarks` and `pane.destinations` open them, and five of §8's thirteen
    // Bookmarks rows are serviced: `Enter`, `→`, `←`, `*`, `/`. The other eight are the EDITING
    // rows — rename, delete, new, demote, promote, move up, move down, set-destination — and they
    // stay unregistered, because each needs a `Command` and this stage adds none (D4-1). Their keys
    // therefore reach the tree unconsumed, which `tst_panes` presses each of them to prove.
    //
    // **`pane.destinations` has no chord, and that is §8's rather than an omission here.** The
    // Global table binds nine `Ctrl+Shift+*` panes and this is not one of them; the command is
    // reachable from the sidebar's chooser, the ribbon, the tool finder and the batch runner.
    // `Features.md` §8 is not touched by this stage (plan §10), so it is recorded rather than given
    // one.
    const auto activeBookmarks = []() -> BookmarksPane* {
        MainWindow* window = activeShell();
        return window ? window->bookmarksPane() : nullptr;
    };

    // Both panes open the same way the Thumbnails pane does: show, select, FOCUS. The focus is what
    // makes §8's Bookmarks table live at all, because `activeContext()` is computed from the focus
    // chain (D1-5) — and it is why the destinations pane, which declares no context, still needs it.
    const auto showPane = [](ActionId id) {
        return [id](KeyContext) {
            MainWindow* window = activeShell();
            if (!window || !window->sidebar()) return;
            window->sidebar()->setAutoHidden(false);
            window->sidebar()->show();
            window->sidebar()->showPane(id);
            if (QWidget* pane = window->sidebar()->paneWidget(id))
                pane->setFocus(Qt::OtherFocusReason);
        };
    };
    registerAction(ActionId::PaneBookmarks, showPane(ActionId::PaneBookmarks));
    registerAction(ActionId::PaneDestinations, showPane(ActionId::PaneDestinations));

    // The five navigation rows. Each is DISABLED where there is no bookmark to act on rather than
    // refused after the press (convention 16), so a ribbon button says so before it is clicked.
    const auto withBookmarks = [activeBookmarks](void (BookmarksPane::*method)()) {
        return [activeBookmarks, method](KeyContext) {
            if (BookmarksPane* pane = activeBookmarks()) (pane->*method)();
        };
    };
    const auto haveBookmarks = [activeBookmarks] {
        const BookmarksPane* pane = activeBookmarks();
        return pane != nullptr && pane->nodeCount() > 0;
    };

    registerAction(ActionId::BookmarkGoTo, withBookmarks(&BookmarksPane::goToSelected),
                   haveBookmarks);
    registerAction(ActionId::BookmarkExpand, withBookmarks(&BookmarksPane::expandSelected),
                   haveBookmarks);
    registerAction(ActionId::BookmarkCollapse, withBookmarks(&BookmarksPane::collapseSelected),
                   haveBookmarks);
    registerAction(ActionId::BookmarkExpandAll, withBookmarks(&BookmarksPane::expandAllItems),
                   haveBookmarks);
    registerAction(ActionId::BookmarkCollapseAll, withBookmarks(&BookmarksPane::collapseAllItems),
                   haveBookmarks);

    // --- The Quick Access Toolbar and the tool finder (T11) ---------------------------------
    // `ui.customize` is deliberately NOT registered. The customization dialog is T14, and a handler
    // that opened nothing would make a menu item that does nothing look like one that works.

    registerAction(ActionId::AppToolFinder, [](KeyContext) {
        if (MainWindow* window = activeShell())
            if (ToolFinder* finder = window->toolFinder()) finder->focusSearchBox();
    });

    // ADD AND REMOVE TAKE A PARAMETER, and the registry has no channel for one. The command that
    // was under the cursor is held by the toolbar and read here; the enablement predicate is what
    // makes the absence of a target visible in the menu rather than a refusal after the click. See
    // QuickAccessToolbar.h, and open decision O11 in Implementation-Plan.md 8.
    const auto activeQat = []() -> QuickAccessToolbar* {
        MainWindow* window = activeShell();
        return window ? window->quickAccessToolbar() : nullptr;
    };

    registerAction(
        ActionId::UiQatAdd,
        [activeQat](KeyContext) {
            QuickAccessToolbar* qat = activeQat();
            if (qat && qat->pendingTarget()) qat->addCommand(*qat->pendingTarget());
        },
        [activeQat] {
            QuickAccessToolbar* qat = activeQat();
            return qat && qat->pendingTarget().has_value();
        });

    registerAction(
        ActionId::UiQatRemove,
        [activeQat](KeyContext) {
            QuickAccessToolbar* qat = activeQat();
            if (qat && qat->pendingTarget()) qat->removeCommand(*qat->pendingTarget());
        },
        [activeQat] {
            QuickAccessToolbar* qat = activeQat();
            if (!qat || !qat->pendingTarget()) return false;
            const std::vector<ActionId>& commands = qat->commands();
            return std::find(commands.begin(), commands.end(), *qat->pendingTarget())
                   != commands.end();
        });

    // REORDER, WHICH THE TOOLBAR COULD DO AND NOBODY COULD ASK IT TO (issue I-43). `moveCommand`
    // has been public since T11 and `UiCustomization` has persisted the order for just as long; the
    // only caller was `tst_qat`. Two commands rather than a drag: a drag reaches nothing but a
    // pointer, and the rule this toolbar was built under is that every one of its operations is a
    // command the batch runner and a chord can reach too (flow K).
    //
    // The index is resolved at DISPATCH time from the pending target, never captured: the same
    // registry serves every window (convention 14), and the button that was right-clicked is the
    // parameter (O11).
    const auto qatIndexOfTarget = [activeQat]() -> int {
        QuickAccessToolbar* qat = activeQat();
        if (!qat || !qat->pendingTarget()) return -1;
        const std::vector<ActionId>& commands = qat->commands();
        const auto found = std::find(commands.begin(), commands.end(), *qat->pendingTarget());
        return found == commands.end() ? -1 : static_cast<int>(found - commands.begin());
    };

    const auto registerQatMove = [&](ActionId id, int delta) {
        core::ActionRegistry::Registration move;
        move.handler = [activeQat, qatIndexOfTarget, delta](KeyContext) {
            QuickAccessToolbar* qat = activeQat();
            const int from = qatIndexOfTarget();
            if (qat && from >= 0) qat->moveCommand(from, from + delta);
        };
        // Disabled at the end it cannot move towards, rather than offered and silently refused --
        // `moveCommand` already rejects an out-of-range index, and a menu item that does nothing
        // when clicked is how a user learns to distrust the menu.
        move.enable = [activeQat, qatIndexOfTarget, delta] {
            QuickAccessToolbar* qat = activeQat();
            if (!qat) return false;
            const int from = qatIndexOfTarget();
            const int to   = from + delta;
            return from >= 0 && to >= 0 && to < static_cast<int>(qat->commands().size());
        };
        registerRegistration(id, std::move(move));
    };
    registerQatMove(ActionId::UiQatMoveLeft, -1);
    registerQatMove(ActionId::UiQatMoveRight, +1);

    // THE FIRST TOGGLE IN THE PRODUCT. `checked` is what makes `ActionFactory` mark the action
    // checkable at all -- and getting here is what showed that the factory had never done so
    // (I-21). `tooltipKey` is the other half: the label is a noun, so the check mark needs a
    // sentence saying which way it points.
    core::ActionRegistry::Registration placement;
    placement.handler    = [activeQat](KeyContext) {
        if (QuickAccessToolbar* qat = activeQat()) qat->setBelowRibbon(!qat->isBelowRibbon());
    };
    placement.checked    = [activeQat] {
        QuickAccessToolbar* qat = activeQat();
        return qat && qat->isBelowRibbon();
    };
    placement.tooltipKey = "shell.qat.placementHint";
    registerRegistration(ActionId::UiQatPlacement, std::move(placement));

    // LOCK, a toggle like placement. Drag-to-reorder is the gesture the toolbar now offers, and a
    // lock is the guard against performing it by accident -- so it is offered in the same menu and
    // persisted with the rest of the customization.
    core::ActionRegistry::Registration lock;
    lock.handler = [activeQat](KeyContext) {
        if (QuickAccessToolbar* qat = activeQat()) qat->setLocked(!qat->isLocked());
    };
    lock.checked = [activeQat] {
        QuickAccessToolbar* qat = activeQat();
        return qat && qat->isLocked();
    };
    registerRegistration(ActionId::UiQatLock, std::move(lock));

    registerAction(ActionId::UiResetCustomization, [activeQat](KeyContext) {
        if (QuickAccessToolbar* qat = activeQat()) {
            qat->resetToDefaults();
            qat->setLocked(false);   // a reset that left the toolbar locked would not be one
        }
    });

    // --- Preferences and the log pane (T14) -------------------------------------------------
    // `file.preferences` needs NO enablement predicate, and that is the difference between this
    // dialog and `file.open`'s. A modeless dialog blocks nothing, so there is no headless hazard
    // to guard against -- issue I-23's `canPromptTheUser` exists for a modal `exec()` that never
    // returns when there is nobody to answer it. See ui/dialogs/PreferencesDialog.h.
    // Not through `onActiveShell`: that helper takes a `void ()` member and `showPreferences`
    // returns the dialog, which is what makes it usable from a test without reaching for
    // `findChild`. One lambda is cheaper than a second overload of the helper.
    registerAction(ActionId::FilePreferences, [](KeyContext) {
        if (MainWindow* window = activeShell()) window->showPreferences();
    });

    core::ActionRegistry::Registration logPane;
    logPane.handler = [](KeyContext) {
        MainWindow* window = activeShell();
        if (!window || !window->logPane()) return;
        window->logPane()->setVisible(!window->logPane()->isVisible());
    };
    // A toggle, so the ribbon renders it checkable and the check mark says whether the pane is up.
    // `ActionFactory` reads `checked` to decide that -- see I-21.
    logPane.checked = [] {
        MainWindow* window = activeShell();
        return window && window->logPane() && window->logPane()->isVisible();
    };
    registerRegistration(ActionId::PaneLog, std::move(logPane));

    // `log.openFolder` is DECLARED AND NOT REGISTERED, and the reason is a layer boundary rather
    // than an oversight. The folder belongs to `app/LogFile`, and `ui/` looks outward to exactly
    // two files -- `platform/platform.h` and `app/Services.h` -- each constrained by its own
    // `layer-lint` entry (issue I-15). Including a third to open a directory would widen that list
    // for a convenience, and widening it is an architectural decision rather than a diff. The
    // command stays a visible, disabled button saying it is not built yet, which is the same
    // honest state the other ~340 ids are in.

    // --- The §8 Viewer commands (S2 T14) ----------------------------------------------------
    registerViewerActions();

    // --- Undo, redo, rotate, save (Stage 3 T10) -----------------------------------------------
    registerMutationActions();

    // --- Documents and windows (T12) --------------------------------------------------------
    // `file.open` raises a file dialog and hands what it collects to `openFiles`, which checks a
    // name and a suffix and reads not one byte (D1-2, guide case A-58). It is registered here
    // rather than at T14 with the rest of the dialogs because `openFiles` now has somewhere to put
    // a file: a dead Open button above working machinery is worse than a missing one.
    registerAction(ActionId::FileOpen, onActiveShell(&MainWindow::promptForFiles),
                   [] { return canPromptTheUser(); });

    const auto hasDocument = [] {
        MainWindow* window = activeShell();
        return window && window->documentCount() > 0;
    };

    registerAction(
        ActionId::FileClose,
        [](KeyContext) {
            if (MainWindow* window = activeShell())
                window->closeDocument(window->documentTabs()->currentIndex());
        },
        hasDocument);

    registerAction(
        ActionId::FileCloseAll,
        [](KeyContext) {
            if (MainWindow* window = activeShell()) window->closeAllDocuments();
        },
        hasDocument);

    // The four navigation commands. Their enablement is what makes `Ctrl+Tab` in a window with one
    // tab report "not applicable right now" rather than appearing to work -- the distinction T3's
    // `ErrorCode::Unavailable` exists for, and the one a batch report has to be able to make.
    const auto stepDocuments = [](int delta) {
        return [delta](KeyContext) {
            if (MainWindow* window = activeShell()) window->stepDocument(delta);
        };
    };
    const auto manyDocuments = [] {
        MainWindow* window = activeShell();
        return window && window->documentCount() > 1;
    };

    registerAction(ActionId::WindowNextTab, stepDocuments(1), manyDocuments);
    registerAction(ActionId::WindowPrevTab, stepDocuments(-1), manyDocuments);

    const auto manyWindows = [] { return shellWindows().size() > 1; };
    registerAction(ActionId::WindowNext, [](KeyContext) { stepWindow(1); }, manyWindows);
    registerAction(ActionId::WindowPrev, [](KeyContext) { stepWindow(-1); }, manyWindows);

    // A SECOND VIEW OF THE DOCUMENT IN FRONT -- S4 T11, `Features.md` §1.1, D4-13's exception. It
    // opened a new EMPTY window from Stage 1 T12 until now, and that is still what it does when
    // there is no document to make a view OF; `openSecondView` holds both branches and the reasons.
    // Tearing a tab out remains a gesture on the tab rather than this command.
    registerAction(ActionId::WindowNew, [](KeyContext) {
        if (MainWindow* from = activeShell()) (void)from->openSecondView();
    });

    // --- The split (S4 T9, D4-13) ----------------------------------------------------------------
    // Nothing is captured and the split is resolved at DISPATCH (convention 14), which matters more
    // here than almost anywhere else: the container these act on is destroyed and rebuilt by these
    // very commands, so a handler holding one would be holding a widget it had just retired.
    const auto splitOfActiveShell = []() -> SplitViewContainer* {
        MainWindow* window = activeShell();
        DocumentWindow* document = window ? window->currentDocument() : nullptr;
        return document ? document->split() : nullptr;
    };

    const auto setSplitMode = [splitOfActiveShell](SplitViewContainer::Mode mode) {
        return [splitOfActiveShell, mode](KeyContext) {
            if (SplitViewContainer* split = splitOfActiveShell()) split->setMode(mode);
            if (MainWindow* window = activeShell()) window->updateViewChrome();
        };
    };

    // A DOCUMENT IS THE PRECONDITION, and an EMPTY one is not a document for this purpose: splitting
    // a tab that is drawing nothing gives two panes drawing nothing, which reads as a broken split
    // rather than as an empty tab. The same reading `registerViewerActions`'s `hasView` uses.
    const auto hasPages = [splitOfActiveShell] {
        SplitViewContainer* split = splitOfActiveShell();
        return split != nullptr && split->primaryView()->pageCount() > 0;
    };

    const auto registerSplit = [this, hasPages](ActionId id, core::ActionRegistry::Handler handler,
                                                core::ActionRegistry::Enable enable,
                                                core::ActionRegistry::Checked checked) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = std::move(handler);
        registration.enable = std::move(enable);
        registration.checked = std::move(checked);
        registration.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(id, std::move(registration));
    };

    // CHECKED RATHER THAN THREE SEPARATE STATES, because the three modes are mutually exclusive and
    // the ribbon shows which one the window is in. Pressing the mode it is already in does nothing
    // (`SplitViewContainer::setMode`), so a second press is not a second split.
    const auto inMode = [splitOfActiveShell](SplitViewContainer::Mode mode) {
        return [splitOfActiveShell, mode] {
            SplitViewContainer* split = splitOfActiveShell();
            return split != nullptr && split->mode() == mode;
        };
    };

    registerSplit(ActionId::WindowSplit, setSplitMode(SplitViewContainer::Mode::Horizontal),
                  hasPages, inMode(SplitViewContainer::Mode::Horizontal));
    registerSplit(ActionId::WindowSplitVertical, setSplitMode(SplitViewContainer::Mode::Vertical),
                  hasPages, inMode(SplitViewContainer::Mode::Vertical));
    registerSplit(ActionId::WindowSpreadsheetSplit, setSplitMode(SplitViewContainer::Mode::Spreadsheet),
                  hasPages, inMode(SplitViewContainer::Mode::Spreadsheet));

    // REFUSED WHEN THERE IS NO SPLIT, rather than silently doing nothing. A *Remove Split* that is
    // enabled on an unsplit window is a control that cannot be told apart from one that is broken --
    // Stage 1's I-53, which is why `unavailableKey` exists at all.
    registerSplit(ActionId::WindowRemoveSplit, setSplitMode(SplitViewContainer::Mode::None),
                  [splitOfActiveShell] {
                      SplitViewContainer* split = splitOfActiveShell();
                      return split != nullptr && split->mode() != SplitViewContainer::Mode::None;
                  },
                  {});

    // AND SYNCHRONIZED SCROLLING IS REFUSED IN A SPREADSHEET SPLIT, not merely ignored there. The
    // frozen panes' scroll is DERIVED from the body's -- that is the mode, not an option on it --
    // so a switch offered in that state would be one that could not do anything.
    //
    // ONE SWITCH, TWO SUBJECTS -- S4 T11. `Features.md` lists *synchronize scrolling* twice, once
    // under split views (§1.2) and once under document windows (§1.1), and to the person holding
    // the mouse they are one idea: *the views of this document move together*. Where the views are
    // -- two panes of one window, or two windows -- is not part of that idea, so it is not part of
    // the command either. A split takes precedence when there is one, because that is the surface
    // the switch is sitting next to; with no split and a second view in another window, the same
    // switch unlinks the windows.
    const auto companionOfActiveShell = []() -> DocumentWindow* {
        MainWindow* window = activeShell();
        DocumentWindow* document = window ? window->currentDocument() : nullptr;
        if (document == nullptr) return nullptr;
        const bool paired = document->leader() != nullptr || !document->companions().isEmpty();
        return paired ? document : nullptr;
    };

    registerSplit(ActionId::WindowSyncScroll,
                  [splitOfActiveShell, companionOfActiveShell](KeyContext) {
                      SplitViewContainer* split = splitOfActiveShell();
                      if (split != nullptr && split->mode() != SplitViewContainer::Mode::None
                          && split->mode() != SplitViewContainer::Mode::Spreadsheet) {
                          split->setSynchronizedScrolling(!split->synchronizedScrolling());
                          return;
                      }
                      if (DocumentWindow* paired = companionOfActiveShell()) {
                          paired->setSynchronizedWithOtherWindows(
                              !paired->synchronizedWithOtherWindows());
                      }
                  },
                  [splitOfActiveShell, companionOfActiveShell] {
                      SplitViewContainer* split = splitOfActiveShell();
                      const bool splitPair = split != nullptr
                                             && (split->mode() == SplitViewContainer::Mode::Horizontal
                                                 || split->mode() == SplitViewContainer::Mode::Vertical);
                      return splitPair || companionOfActiveShell() != nullptr;
                  },
                  [splitOfActiveShell, companionOfActiveShell] {
                      SplitViewContainer* split = splitOfActiveShell();
                      if (split != nullptr && split->mode() != SplitViewContainer::Mode::None
                          && split->mode() != SplitViewContainer::Mode::Spreadsheet) {
                          return split->synchronizedScrolling();
                      }
                      DocumentWindow* paired = companionOfActiveShell();
                      return paired != nullptr && paired->synchronizedWithOtherWindows();
                  });

    // --- Tile, cascade, merge and the window list (S4 T11, D4-17) ------------------------------
    registerArrangeActions();
}

void MainWindow::registerArrangeActions()
{
    // S4 T11, D4-17. Nothing is captured and every window is resolved at DISPATCH (convention 14),
    // which matters here for a reason it does not elsewhere: `window.merge` DESTROYS windows, so a
    // handler holding one would be holding a window it had just closed.
    const auto registerArrange = [this](ActionId id, core::ActionRegistry::Handler handler,
                                        core::ActionRegistry::Enable enable) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = std::move(handler);
        registration.enable = std::move(enable);
        registration.unavailableKey = "shell.tooltip.needsWindows";
        services_.actions.register_(id, std::move(registration));
    };

    // TWO OR MORE WINDOWS IS THE PRECONDITION, and it is a refusal rather than a no-op (Stage 1's
    // I-53): arranging one window is a command that cannot be told apart from a broken one, and
    // guide case A-64's control -- *arranging with one window changes nothing* -- is asserted on
    // the geometry rather than on the refusal precisely because both must hold.
    const auto manyWindows = [] { return shellWindows().size() > 1; };

    const auto tileAs = [](WindowArranger::Orientation orientation) {
        return [orientation](KeyContext) { WindowArranger::tile(orientation); };
    };

    registerArrange(ActionId::WindowTileHorizontal, tileAs(WindowArranger::Orientation::Horizontal),
                    manyWindows);
    registerArrange(ActionId::WindowTileVertical, tileAs(WindowArranger::Orientation::Vertical),
                    manyWindows);
    registerArrange(ActionId::WindowCascade, [](KeyContext) { WindowArranger::cascade(); },
                    manyWindows);
    registerArrange(ActionId::WindowMerge, [](KeyContext) { (void)WindowArranger::mergeWindows(); },
                    manyWindows);

    // THE WINDOW LIST IS OFFERED WITH ONE WINDOW TOO, and that is not an oversight: a list of one
    // is a true statement about the application, whereas *tile one window* is not an operation. It
    // is refused only where there is nobody to show a popup menu to, which is the same predicate
    // `file.open` uses and the same trap it was written for (I-23).
    registerArrange(ActionId::WindowList,
                    [](KeyContext) {
                        if (MainWindow* window = activeShell()) window->showWindowList();
                    },
                    [] { return canPromptTheUser(); });
}

void MainWindow::showWindowList()
{
    // EVERY ITEM IS NAVIGATION RATHER THAN A COMMAND, so each goes through
    // `ActionFactory::addNonCommandMenuItem` -- the ribbon overflow's precedent, and the one place
    // in the product allowed to build a `QAction` that carries no `ActionId` (D1-7). A window is
    // not a feature: it has no id, it is in no catalog, and there is nothing for `ActionRegistry`
    // to route. What IS a command is opening this list, which is `window.list`.
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    const QStringList titles = WindowArranger::windowList();
    for (int i = 0; i < titles.size(); ++i) {
        QAction* item = ActionFactory::addNonCommandMenuItem(menu, titles.at(i));
        item->setCheckable(true);
        item->setChecked(shellWindows().value(i) == activeShell());
        connect(item, &QAction::triggered, this, [i] { WindowArranger::activateWindow(i); });
    }

    menu->popup(QCursor::pos());
}

core::Result<MainWindow*> MainWindow::openSecondView()
{
    DocumentWindow* source = currentDocument();

    // NO DOCUMENT, NO VIEW TO MAKE A SECOND OF. A new empty window is what this command did from
    // Stage 1 T12 to Stage 4 T10 and is still the honest answer here.
    if (source == nullptr || !source->handle().isValid()) {
        auto* window = new MainWindow(services_);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();
        window->raiseToForeground();
        return window;
    }

    // A SECOND VIEW OF A SECOND VIEW IS A SECOND VIEW OF THE DOCUMENT -- resolved here as well as
    // in the companion constructor, because the PATH has to come from the tab that owns the file.
    DocumentWindow* leader = source->leader() != nullptr ? source->leader() : source;

    // AN UNTITLED DOCUMENT HAS NOTHING TO RE-OPEN. Its bytes came from a crashed session's source
    // and its changes live in one journal (D3-11); opening the source again would give a second
    // view of the file the person chose NOT to write into, which is the opposite of what they asked
    // for. Refused by key, so the ribbon button says why rather than doing something surprising.
    if (leader->path().isEmpty()) {
        RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "window.new: refused -- the document has no file of its own";
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.window.untitled"),
                           leader->title()};
    }

    // NO PASSWORD PROMPT AND NO `recentFiles` NOTE. The file is already open in this process, so it
    // was already answered for and already noted; asking again for a second view of a document
    // whose first view is on screen would read as the product forgetting.
    const core::Result<core::DocumentHandle> opened =
        services_.backend.open(leader->path(), [](int) { return std::optional<QString>(); });
    if (!opened) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "window.new: the backend refused a second handle for" << leader->path() << "--"
            << opened.error().messageKey.data();
        return opened.error();
    }

    auto* window = new MainWindow(services_);
    window->setAttribute(Qt::WA_DeleteOnClose);

    // BUILT HERE AND ADOPTED, rather than going through `openDocument`: that function opens a tab
    // for a document, and this is a second VIEW of one -- the difference is the leader, and it is
    // the whole of the companion contract (`DocumentWindow`'s third constructor).
    auto* companion = new DocumentWindow(services_, leader->path(), opened.value(), leader);
    companion->setProtected(leader->isProtected());
    window->adoptDocument(companion);

    window->show();
    window->raiseToForeground();

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "window.new: a second view of" << leader->path() << "as handle"
        << static_cast<int>(opened.value().id);
    return window;
}

void MainWindow::refreshWindowFocusState()
{
    const MainWindow* front = activeShell();
    for (MainWindow* window : shellWindowSet()) {
        const bool background = window != front;
        for (int i = 0; i < window->documentCount(); ++i)
            if (DocumentWindow* document = window->documentAt(i))
                document->setBackgroundWindow(background);
    }
}

void MainWindow::registerMutationActions()
{
    // SAME MECHANISM AS `registerActions()` -- once per registry, nothing captured, the window and its
    // document resolved at dispatch (convention 14). What is new is `tooltipText`/`unavailableText`: text
    // that depends on the document in front, which a key alone cannot carry.
    const auto registerRegistration = [this](ActionId id, core::ActionRegistry::Registration registration) {
        if (services_.actions.isRegistered(id)) return;
        const core::Result<void> registered = services_.actions.register_(id, std::move(registration));
        if (registered) return;
        const std::string_view name = core::actionStringId(id);
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "mutation action registration refused for"
            << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())) << "--"
            << registered.error().messageKey;
    };

    const auto currentDocumentOfActiveShell = []() -> DocumentWindow* {
        MainWindow* window = activeShell();
        return window ? window->currentDocument() : nullptr;
    };
    const auto journalInFront = [currentDocumentOfActiveShell]() -> core::CommandJournal* {
        DocumentWindow* document = currentDocumentOfActiveShell();
        return document ? document->journal() : nullptr;
    };
    const auto afterHistoryMove = [] {
        if (MainWindow* window = activeShell()) {
            window->updateDocumentTitles();
            window->updateViewChrome();
        }
    };

    // --- edit.undo / edit.redo ------------------------------------------------------------------
    // The tooltip names the step: "Undo: Rotate page 3 by 90 degrees", in the locale in front, from the
    // step's `command.*` entry (guide case A-76).
    const auto registerHistory = [&](ActionId id, bool undo) {
        core::ActionRegistry::Registration registration;
        registration.handler = [journalInFront, afterHistoryMove, undo](KeyContext) {
            if (core::CommandJournal* journal = journalInFront()) {
                if (undo ? journal->undo() : journal->redo()) afterHistoryMove();
            }
        };
        registration.enable = [journalInFront, undo] {
            const core::CommandJournal* journal = journalInFront();
            return journal && (undo ? journal->canUndo() : journal->canRedo());
        };
        registration.tooltipText = [journalInFront, undo] {
            const core::CommandJournal* journal = journalInFront();
            if (!journal) return QString();
            const std::optional<core::CatalogText> name = undo ? journal->undoName() : journal->redoName();
            if (!name) return QString();
            return catalog::translate(undo ? catalog::key::TooltipUndoCommand : catalog::key::TooltipRedoCommand)
                .arg(catalog::translateText(*name));
        };
        registration.unavailableKey = undo ? "shell.tooltip.nothingToUndo" : "shell.tooltip.nothingToRedo";
        registerRegistration(id, std::move(registration));
    };
    registerHistory(ActionId::EditUndo, true);
    registerHistory(ActionId::EditRedo, false);

    // WHY A DOCUMENT CANNOT BE CHANGED, in its own words -- the permissions' refusal ("this document is
    // encrypted"), or the generic sentence when there is no document or no sink (guide case A-79).
    const auto whyNotEditable = [currentDocumentOfActiveShell](ActionId id) {
        return [currentDocumentOfActiveShell, id] {
            const DocumentWindow* document = currentDocumentOfActiveShell();
            if (!document)
                return catalog::translate(catalog::key::TooltipNeedsDocument).arg(catalog::translateAction(id));
            if (document->journal() && !document->journal()->document().permissions().canEditPages()) {
                const core::Permissions& permissions = document->journal()->document().permissions();
                return catalog::translateError(QString(permissions.refusalKey()), permissions.refusalContext());
            }
            if (!document->isEditable())
                return catalog::translate(catalog::key::TooltipNotEditable).arg(catalog::translateAction(id));
            return QString();   // editable: the reason is the command's own key
        };
    };
    const auto editableInFront = [currentDocumentOfActiveShell] {
        const DocumentWindow* document = currentDocumentOfActiveShell();
        return document && document->isEditable() && document->pageCount() > 0;
    };

    // --- page.rotate ----------------------------------------------------------------------------
    // `Ctrl+R` in Viewer and Page Thumbnails (Features.md §8). The dialog collects a choice; the window runs
    // the commands it means. Disabled where nobody can answer the dialog (convention 16).
    {
        core::ActionRegistry::Registration rotate;
        rotate.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            DocumentWindow* document = window ? window->currentDocument() : nullptr;
            if (!document) return;
            if (const std::optional<RotatePagesDialog::Choice> choice = RotatePagesDialog::ask(window, rotationPageOf(*document)))
                window->rotatePages(document, *choice);
        };
        rotate.enable = [editableInFront] { return editableInFront() && RotatePagesDialog::canAsk(); };
        rotate.unavailableText = whyNotEditable(ActionId::PageRotate);
        registerRegistration(ActionId::PageRotate, std::move(rotate));
    }

    // --- file.save / file.saveAs ----------------------------------------------------------------
    // Save needs something to save; Save As needs somebody to name the file (convention 16). A clean
    // document is not saved -- `SaveCoordinator` would refuse it by key, and a Save that did nothing
    // should say so before the click rather than after it.
    {
        core::ActionRegistry::Registration save;
        save.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (DocumentWindow* document = window ? window->currentDocument() : nullptr)
                window->saveDocument(document);
        };
        save.enable = [currentDocumentOfActiveShell] {
            const DocumentWindow* document = currentDocumentOfActiveShell();
            return document && document->isEditable() && document->isDirty();
        };
        const auto whyNot = whyNotEditable(ActionId::FileSave);
        save.unavailableText = whyNot;
        save.unavailableKey = "shell.tooltip.nothingToSave";
        registerRegistration(ActionId::FileSave, std::move(save));
    }
    {
        core::ActionRegistry::Registration saveAs;
        saveAs.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (DocumentWindow* document = window ? window->currentDocument() : nullptr)
                window->saveDocumentAs(document);
        };
        saveAs.enable = [currentDocumentOfActiveShell] {
            const DocumentWindow* document = currentDocumentOfActiveShell();
            return document && document->isEditable() && canAskSavePath();
        };
        saveAs.unavailableText = whyNotEditable(ActionId::FileSaveAs);
        registerRegistration(ActionId::FileSaveAs, std::move(saveAs));
    }
}

PreferencesDialog* MainWindow::showPreferences()
{
    // Built on first use and KEPT, so that reopening it returns to the page the user was last on.
    // Parented to this window, so a torn-off window gets its own -- which is right: the pages read
    // one `Settings`, and two dialogs showing the same values is not a conflict, it is two views.
    if (!preferences_) preferences_ = new PreferencesDialog(services_, this);

    preferences_->show();
    preferences_->raise();
    preferences_->activateWindow();
    return preferences_;
}

void MainWindow::toggleSidebar()
{
    if (sidebar_->isVisible()) {
        sidebar_->setRememberedWidth(sidebar_->width());
        sidebar_->hide();
    } else {
        sidebar_->setAutoHidden(false);
        sidebar_->show();
        resizeDocks({sidebar_}, {sidebar_->rememberedWidth()}, Qt::Horizontal);
    }
    updateAutoHideStrips();
}

void MainWindow::toggleFullScreen()
{
    // S4 T10: THIS IS NOW LEVEL 1 OF THE CONTROLLER (D4-12), and the change is not cosmetic. From
    // Stage 1 to Stage 4 T9 this was the window state and nothing else, and the comment that used
    // to be here said so. What it gains by going through the controller is the thing a window
    // state cannot express: `IPowerSession::setPresentationModeActive`, which is called on level 1
    // as well as level 2 because a person reading full-screen for ten minutes without touching the
    // mouse is exactly whom a lock screen interrupts.
    if (fullScreen_) fullScreen_->toggleFullScreen(this);
    else             setWindowState(windowState() ^ Qt::WindowFullScreen);
}

void MainWindow::toggleRibbon()
{
    if (ribbon_) ribbon_->setBodyCollapsed(!ribbon_->isBodyCollapsed());
}

void MainWindow::setReadingMode(bool on)
{
    if (on == readingMode_) return;

    if (on) {
        // RECORDED BEFORE ANYTHING IS HIDDEN, because `isVisible()` on a widget that has just been
        // hidden is the answer to the wrong question. The sidebar's WIDTH is read from the widget
        // rather than from `rememberedWidth()`: a person who has just dragged the splitter has a
        // width the dock has not written down yet, and restoring the remembered one would move a
        // divider they set thirty seconds ago.
        beforeReading_ = ChromeState{};
        beforeReading_.active = true;
        beforeReading_.header = header_ != nullptr && header_->isVisible();
        beforeReading_.sidebar = sidebar_ != nullptr && sidebar_->isVisible();
        beforeReading_.sidebarFloating = sidebar_ != nullptr && sidebar_->isFloating();
        beforeReading_.sidebarArea =
            sidebar_ != nullptr ? dockWidgetArea(sidebar_) : Qt::LeftDockWidgetArea;
        beforeReading_.sidebarWidth = sidebar_ != nullptr ? sidebar_->width() : 0;
        beforeReading_.logPane = logPane_ != nullptr && logPane_->isVisible();
        beforeReading_.statusBar = status_ != nullptr && status_->isVisible();

        if (header_) header_->hide();
        if (sidebar_) sidebar_->hide();
        if (logPane_) logPane_->hide();

        // THE STATUS BAR STAYS. It is one line saying which page of how many, at what zoom, and it
        // is the one piece of chrome a reader is reading. `Features.md` §1.2 calls this mode
        // *"maximum page area"*, not *"no information at all"* -- and a person who has hidden the
        // ribbon has no other way to find out where they are in the document.
        readingMode_ = true;
    } else {
        readingMode_ = false;

        // NOTHING RECORDED MEANS NOTHING TO RESTORE, which is the state a window is in if reading
        // mode was never entered -- a `setReadingMode(false)` from a restored session, say. Putting
        // a default window together here would be this function inventing a layout.
        if (beforeReading_.active) {
            if (header_) header_->setVisible(beforeReading_.header);
            if (logPane_) logPane_->setVisible(beforeReading_.logPane);
            if (sidebar_ && beforeReading_.sidebar) {
                if (!beforeReading_.sidebarFloating)
                    addDockWidget(beforeReading_.sidebarArea, sidebar_);
                sidebar_->show();
                if (beforeReading_.sidebarWidth > 0) {
                    resizeDocks({sidebar_}, {beforeReading_.sidebarWidth}, Qt::Horizontal);
                }
            }
        }
        beforeReading_ = ChromeState{};
    }

    // The strips stand in for a collapsed pane, and in reading mode there is no pane to summon:
    // showing a 24-pixel bar at the edge of a window whose whole point is the page would be the
    // chrome coming back one widget at a time.
    updateAutoHideStrips();
    if (actionFactory_) actionFactory_->refresh();
}

// ---------------------------------------------------------------------------------------------
// The sidebar, as one string (S4 T13)
// ---------------------------------------------------------------------------------------------

namespace {

// `key=value` pairs joined by `;`. A hand-readable form rather than `QMainWindow::saveState()`'s
// opaque blob, deliberately: this string lands in a JSON file a person may open, and a base64 blob
// there is a field nobody can diagnose. It also survives a Qt upgrade, which `saveState()`'s
// version-stamped format does not promise to.
QString field(const QString& state, QLatin1String key)
{
    for (const QString& part : state.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype split = part.indexOf(QLatin1Char('='));
        if (split > 0 && QStringView(part).left(split) == key)
            return part.mid(split + 1);
    }
    return {};
}

} // namespace

QString MainWindow::paneLayoutState() const
{
    if (sidebar_ == nullptr || !sidebar_->isVisible()) return {};

    QStringList fields;

    // The WIDGET's width, not `rememberedWidth()` -- A-70's lesson about reading mode, which is the
    // same widget and the same question: a person who has just dragged the splitter has a width the
    // dock has not written down yet, and capturing the remembered one would restore a divider they
    // moved thirty seconds ago.
    const int width = sidebar_->width() > 0 ? sidebar_->width() : sidebar_->rememberedWidth();
    if (width > 0) fields << QStringLiteral("width=%1").arg(width);

    // A FLOATING dock has no area, and `dockWidgetArea()` says `NoDockWidgetArea` for one. Omitting
    // the field rather than writing that value is what lets the reader fall back to the left edge
    // instead of asking Qt to dock a widget nowhere.
    if (!sidebar_->isFloating())
        fields << QStringLiteral("area=%1").arg(static_cast<int>(dockWidgetArea(sidebar_)));
    if (sidebar_->isAutoHidden()) fields << QStringLiteral("auto=1");

    if (const std::optional<ActionId> pane = sidebar_->currentPane()) {
        const std::string_view id = core::actionStringId(*pane);
        fields << QStringLiteral("pane=%1").arg(
            QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size())));
    }

    // A sidebar that is open with no pane selected and no width worth naming still has to encode as
    // something non-empty, because empty means CLOSED. `open=1` is that something.
    fields << QStringLiteral("open=1");
    return fields.join(QLatin1Char(';'));
}

void MainWindow::applyPaneLayoutState(const QString& state)
{
    if (sidebar_ == nullptr) return;

    if (state.isEmpty()) {
        sidebar_->hide();
        updateAutoHideStrips();
        return;
    }

    const int area = field(state, QLatin1String("area")).toInt();
    if (area != 0) addDockWidget(static_cast<Qt::DockWidgetArea>(area), sidebar_);

    // THE PANE BEFORE THE SHOW, so the dock is never painted once holding whatever was in front and
    // again holding what the session asked for. A saved id that no longer resolves -- a pane whose
    // stage was reverted, an id retired between builds -- leaves whichever pane the window opened
    // with rather than emptying the dock, and `showPane` answering false is that case.
    const QString pane = field(state, QLatin1String("pane"));
    if (!pane.isEmpty()) {
        const QByteArray utf8 = pane.toUtf8();
        if (const std::optional<ActionId> id = core::actionFromStringId(
                std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())))) {
            if (!sidebar_->showPane(*id)) {
                RAWPAGE_LOG_INFO(LogSubsystem::Ui)
                    << "session: the remembered pane is not in this build's registry:" << pane;
            }
        }
    }

    sidebar_->setAutoHidden(field(state, QLatin1String("auto")) == QLatin1String("1"));
    sidebar_->show();

    if (const int width = field(state, QLatin1String("width")).toInt(); width > 0) {
        sidebar_->setRememberedWidth(width);
        resizeDocks({sidebar_}, {width}, Qt::Horizontal);
    }
    updateAutoHideStrips();
}

DocumentPropertiesDialog::Properties MainWindow::documentProperties() const
{
    DocumentPropertiesDialog::Properties properties;

    const DocumentWindow* document = currentDocument();
    if (document == nullptr) return properties;

    const core::DocumentHandle handle = document->handle();
    properties.encrypted = document->isProtected();

    if (const core::Metadata* info = services_.backend.metadata(handle)) properties.info = *info;

    // THE IDENTITY TAKEN AT OPEN, not a fresh `QFileInfo` (D3-16). A second read here would
    // describe a file that may have changed since and would disagree with the document on screen --
    // and after a save `adoptRevision` has already replaced this with the identity of the bytes now
    // on disk, so it stays right rather than going stale.
    if (const core::DocumentId* id = services_.backend.documentId(handle)) {
        properties.path = id->path;
        properties.fileSize = id->size;
    }
    if (properties.path.isEmpty()) properties.path = document->path();

    if (const core::RepairReport* repair = services_.backend.repairReport(handle))
        properties.repaired = !repair->isEmpty();

    // THE MODEL'S TREE, through the view, so an UNSAVED rotation is reflected in the page size this
    // dialog reports. Asking the backend would give the engine's tree, which is the bytes that were
    // loaded -- correct for the file and wrong for the document on screen (D3-15).
    if (const PageView* view = document->view()) {
        properties.pageCount = view->pageCount();
        properties.currentPage = view->controller().currentPage();
        if (const core::PageTree* tree = view->pages()) {
            if (const core::Page* page = tree->at(properties.currentPage)) {
                properties.currentPageLabel = page->label();
                properties.currentPageSize = page->displaySize();
            }
        }
    }

    return properties;
}

void MainWindow::showDocumentProperties()
{
    if (currentDocument() == nullptr) return;
    if (!DocumentPropertiesDialog::canAsk()) return;

    DocumentPropertiesDialog dialog(documentProperties(), this);
    dialog.exec();
}

void MainWindow::promptForFiles()
{
    // The dialog COLLECTS PARAMETERS and nothing else -- the standing rule about dialogs, and the
    // reason this is three lines. Where the user was last is a Stage 14 preference; until then the
    // dialog opens wherever Qt opens it.
    const QStringList chosen = QFileDialog::getOpenFileNames(
        this, catalog::translate(catalog::key::DialogOpenTitle), QString(),
        catalog::translate(catalog::key::DialogPdfFilter));
    if (!chosen.isEmpty()) openFiles(chosen);
}

void MainWindow::openFiles(const QStringList& paths)
{
    // EVERY REFUSAL IN THIS CALL, for one window-level banner at the end (Stage 2 issue I-32). The
    // status bar kept the only copy until 2026-09-13, bottom-left, in the grey of everything else:
    // a double-click on a damaged file produced no tab, no dialog and no banner, and the user
    // reasonably concluded nothing had happened. One banner per call rather than per file, so a
    // handoff of six damaged files is one strip listing six sentences rather than six strips.
    QStringList refusals;

    for (const QString& path : paths) {
        const QFileInfo info(path);

        // Existence is still ours: "there is no such file" is a different message from "that is
        // not a PDF", and asking the engine to open something that is not there would turn the
        // first into the second.
        if (!info.exists() || !info.isFile()) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "open: no such file:" << path;
            const QString reason =
                catalog::translateError(QStringLiteral("error.open.notFound"), path);
            status_->setMessage(reason);
            refusals << reason;
            continue;
        }

        // **T9: THE SUFFIX CHECK IS GONE, AND THE BACKEND DECIDES.** Guide case A-47, which
        // replaces Stage 1's A-58 -- that case asserted the OPPOSITE and was correct then, because
        // D1-2 said the shell reads no bytes and there was no engine to ask. There is now.
        //
        // The consequence is user-visible in both directions: a PDF someone saved as `.dat` opens,
        // and a `.pdf` that is really a spreadsheet is refused **with the reason the engine gave**
        // rather than with a guess from its name. Deciding whether bytes are a PDF is
        // `IDocumentBackend::open()`'s question and nowhere else's.
        //
        // ALREADY OPEN IN THIS WINDOW? Then the backend is not asked at all, and that is a change
        // T13 made deliberately: T9 opened first and discovered the duplicate afterwards, which
        // was harmless while the handle was thrown away and is a leaked engine handle now that a
        // tab owns one. Focusing the existing tab is also what the user meant.
        if (const int existing = indexOfPath(info.absoluteFilePath()); existing >= 0) {
            activateDocument(existing);
            continue;
        }

        // THE PASSWORD PROMPT (T14), which the T9 comment here said was a dialog and therefore
        // T14's. The callback is built PER FILE, so a handoff of six encrypted documents names
        // each one rather than asking the same anonymous question six times -- and it answers
        // `std::nullopt` immediately when there is nobody to ask, which is `Cancelled` rather than
        // a failure and is what stops a headless run from hanging on a modal nobody can see
        // (Stage 1's I-23).
        int passwordAsks = 0;
        const core::PasswordCallback prompt =
            [&passwordAsks, callback = PasswordDialog::callbackFor(info.fileName(), this)](
                int attempt) {
                ++passwordAsks;
                return callback(attempt);
            };

        const core::Result<core::DocumentHandle> opened =
            services_.backend.open(info.absoluteFilePath(), prompt);
        if (!opened) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                << "open: backend refused" << info.absoluteFilePath() << "--"
                << opened.error().messageKey.data();
            const QString reason =
                catalog::translateError(opened.error().messageKey, opened.error().context);
            status_->setMessage(reason);

            // A CANCELLED PROMPT IS NOT A FAILURE (A-43): the person pressed Escape, and a red
            // banner telling them the file could not be opened would be telling them off for it.
            if (opened.error().code != core::ErrorCode::Cancelled) refusals << reason;
            continue;
        }

        // The recent list is a list of paths (T5's deviation (c)). It is noted only AFTER the
        // backend accepted the file: Stage 1 noted every path it was handed because it could not
        // tell whether one was openable, and a recent list full of things that will not open is
        // worse than a short one.
        services_.recentFiles.note(info.absoluteFilePath());

        // Whether the file was ENCRYPTED is not something the backend reports separately, and it
        // does not need to be: the callback was asked, so it was. That is the honest signal rather
        // than a second query, and it is why the counter exists.
        DocumentWindow* document = openDocument(info.absoluteFilePath(), opened.value());
        if (document && passwordAsks > 0) document->setProtected(true);
        updateDocumentChrome();
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "open: accepted" << info.absoluteFilePath() << "as handle"
            << static_cast<int>(opened.value().id);
    }

    if (!refusals.isEmpty() && messageBar_) {
        MessageBar::Message failed;
        failed.kind = MessageBar::Kind::OpenFailed;
        failed.text = refusals.join(QLatin1Char('\n'));
        messageBar_->showMessage(std::move(failed));
    }
}

void MainWindow::openHandoff(const QStringList& files)
{
    // RAISE FIRST. The secondary granted this process the right to come forward before it exited
    // (`AllowSetForegroundWindow`, D1-11), and spending it here is what lets the password prompt that
    // `openFiles()` may raise be shown by the FOREGROUND process, in front, parented to a window
    // that is already there. A refused file benefits too: its reason lands in the status bar of a
    // window the person is looking at, rather than of one still behind whatever they clicked in.
    raiseToForeground();
    openFiles(files);
}

void MainWindow::raiseToForeground()
{
    // The permission to come forward was granted by the SECONDARY process before it exited
    // (AllowSetForegroundWindow, decision D1-11): Windows refuses it to a process that received no
    // input, so without that grant these three calls do nothing at all and the symptom is "I
    // double-clicked a PDF and nothing happened".
    if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();

    // AND SET THE ACTIVE SHELL DIRECTLY, rather than waiting for the ActivationChange that
    // `activateWindow()` normally causes. Measured at T12: under `offscreen` that event never
    // arrives, so `activeShell()` went on naming the previous window and a Global command
    // dispatched into the window nobody had raised. Bringing a window forward IS making it the
    // active shell -- deriving it from the platform's event was the indirection, not this.
    g_activeShell = this;

    // AND THE LANES FOLLOW IT (S4 T11). Here rather than only in `changeEvent` for exactly the
    // reason above: offscreen there is no activation event, so a suite that raised a window would
    // leave every window queueing at `Prefetch` and could not judge A2's named work at all.
    refreshWindowFocusState();
}

} // namespace rawpage::ui
