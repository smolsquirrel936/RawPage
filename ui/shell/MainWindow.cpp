#include "ui/shell/MainWindow.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/Ribbon.h"
#include "ui/shell/RibbonModel.h"
#include "ui/shell/RibbonTabBuilder.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/StatusBar.h"
#include "ui/shell/ThemeManager.h"

#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>

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

} // namespace

MainWindow::MainWindow(const app::Services& services, QWidget* parent)
    : QMainWindow(parent), services_(services)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(catalog::translate(catalog::key::WindowTitle));
    resize(1280, 820);

    theme_ = std::make_unique<ThemeManager>(services_.settings, this);
    theme_->apply();

    status_ = new StatusBar(this);
    setStatusBar(status_);

    sidebar_ = new SidebarDock(services_.actions, this);
    addDockWidget(Qt::LeftDockWidgetArea, sidebar_);
    resizeDocks({sidebar_}, {sidebar_->rememberedWidth()}, Qt::Horizontal);

    // ORDER MATTERS, and not for a cosmetic reason. `ActionFactory` reads each action's
    // registration state at the moment it builds the button, so anything this window registers has
    // to be registered BEFORE the ribbon is built -- otherwise the shell's own commands render as
    // permanently disabled "not built yet" buttons, which is the honest state for the other ~355
    // ids and a lie about these three.
    registerActions();
    buildRibbon();
    buildCentralSurface();
    buildAutoHideStrips();

    connect(sidebar_, &SidebarDock::autoHiddenChanged, this, [this]() { updateAutoHideStrips(); });
    connect(sidebar_, &QDockWidget::dockLocationChanged, this, [this]() { updateAutoHideStrips(); });
    connect(sidebar_, &QDockWidget::visibilityChanged, this, [this]() { updateAutoHideStrips(); });
    updateAutoHideStrips();
}

MainWindow::~MainWindow()
{
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
}

void MainWindow::buildRibbon()
{
    actionFactory_ = std::make_unique<ActionFactory>(
        services_.actions,
        // The context every ribbon dispatch is attributed to. A constant until T13, when
        // `ShortcutBinder::activeContext()` computes it from the focus chain -- which is why this is
        // a callback and not a parameter: T13 replaces this lambda and touches nothing else.
        [] { return KeyContext::Global; });

    ribbon_ = new Ribbon(this);

    // setMenuWidget, not a QToolBar in the top dock area. A ribbon REPLACES the menu bar rather than
    // sitting under one, and the menu-widget slot is the only place QMainWindow puts a widget above
    // the dock areas -- so a docked pane cannot end up above the ribbon, which it can if the ribbon
    // is a toolbar.
    setMenuWidget(ribbon_);

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

    auto* body = new QFrame(central);
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
    row->addWidget(body, 1);

    rightStrip_ = makeEdgeStrip(central, stripLabel, catalog::translate(catalog::key::SidebarShow));
    rightStrip_->setObjectName(QStringLiteral("sidebarStripRight"));
    row->addWidget(rightStrip_);

    setCentralWidget(central);
}

void MainWindow::buildAutoHideStrips()
{
    connect(leftStrip_, &QToolButton::clicked, this, [this]() { sidebar_->peek(); });
    connect(rightStrip_, &QToolButton::clicked, this, [this]() { sidebar_->peek(); });

    // Which edge the pane would return to is named in the strip's tooltip rather than given a
    // button. Moving a pane between edges is a rare deliberate act, and two more permanent controls
    // on a 24-pixel strip would cost more than they earn; dragging the pane already does it.
    leftStrip_->setToolTip(leftStrip_->toolTip() + QLatin1String(" (")
                           + catalog::translate(catalog::key::SidebarDockLeft) + QLatin1Char(')'));
    rightStrip_->setToolTip(rightStrip_->toolTip() + QLatin1String(" (")
                            + catalog::translate(catalog::key::SidebarDockRight) + QLatin1Char(')'));
}

void MainWindow::updateAutoHideStrips()
{
    const bool collapsed =
        sidebar_->isAutoHidden() || (!sidebar_->isVisible() && !sidebar_->isFloating());
    const bool onLeft = dockWidgetArea(sidebar_) != Qt::RightDockWidgetArea;

    leftStrip_->setVisible(collapsed && onLeft);
    rightStrip_->setVisible(collapsed && !onLeft);
}

void MainWindow::registerActions()
{
    // Stage 1 registers what this window can actually service. An ActionId that is DECLARED and
    // UNREGISTERED renders as a visible, disabled control with a tooltip naming the stage it
    // arrives in (Stage1-AppShell-Plan.md 5.1) -- which is the honest state, and is why the other
    // ~355 ids are not given placeholder handlers here.
    const auto registerAction = [this](ActionId id, core::ActionRegistry::Handler handler) {
        const core::Result<void> registered = services_.actions.register_(id, std::move(handler));
        if (registered) return;
        const std::string_view name = core::actionStringId(id);
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "action registration refused for"
            << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()))
            << "--" << registered.error().messageKey;
    };

    registerAction(ActionId::ViewToggleSidebar, [this](KeyContext) {
        if (sidebar_->isVisible()) {
            sidebar_->setRememberedWidth(sidebar_->width());
            sidebar_->hide();
        } else {
            sidebar_->setAutoHidden(false);
            sidebar_->show();
            resizeDocks({sidebar_}, {sidebar_->rememberedWidth()}, Qt::Horizontal);
        }
        updateAutoHideStrips();
    });

    registerAction(ActionId::ViewFullScreen, [this](KeyContext) {
        // Full screen at Stage 1 is the WINDOW STATE ONLY. Presentation mode -- auto-advance, the
        // laser pointer, blank-to-black, choosing a monitor -- is Stage 4's
        // ui/viewer/FullScreenController, and it needs a page surface to present.
        setWindowState(windowState() ^ Qt::WindowFullScreen);
    });

    registerAction(ActionId::FileQuit, [this](KeyContext) { close(); });

    // R-7 of the A3 criteria, reached as a command rather than as a keypress. F9 is its default
    // chord in Features.md 8 and it starts working at T13, when ShortcutBinder exists -- but the
    // BEHAVIOUR is registered now, which is what makes it reachable from the ribbon, from a test
    // and from the Stage 14 batch runner without a second implementation.
    registerAction(ActionId::ViewToggleRibbon, [this](KeyContext) {
        if (ribbon_) ribbon_->setBodyCollapsed(!ribbon_->isBodyCollapsed());
    });
}

void MainWindow::openFiles(const QStringList& paths)
{
    for (const QString& path : paths) {
        const QFileInfo info(path);

        // D1-2: existence and suffix. NOTHING here reads the file. See the header.
        if (!info.exists() || !info.isFile()) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "open: no such file:" << path;
            status_->setMessage(catalog::translateError(QStringLiteral("error.open.notFound"), path));
            continue;
        }
        if (info.suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) != 0) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "open: not a .pdf by name:" << path;
            status_->setMessage(
                catalog::translateError(QStringLiteral("error.open.notPdf"), info.fileName()));
            continue;
        }

        // The recent list is a list of paths (T5's deviation (c)), and a path handed to us on the
        // command line belongs in it whether or not there is yet a document behind it.
        services_.recentFiles.note(info.absoluteFilePath());

        // T12 turns this into a stub tab and Stage 2 renders it. Naming the file NOW is what makes
        // flow B verifiable at this task: a second launch really did reach this window with this
        // path, and it survived every process boundary between there and here byte for byte.
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "open: accepted" << info.absoluteFilePath() << "-- a document tab arrives at T12";
        status_->setMessage(info.fileName());
    }
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
}

} // namespace rawpage::ui
