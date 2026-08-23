#include "ui/shell/SidebarDock.h"

#include "core/cmd/ActionRegistry.h"
#include "ui/shell/Catalog.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

// The pane chrome buttons are NOT ActionIds, and that is a decision rather than an omission. T4 met
// the same question from the other side and answered it the same way: fifteen documented §8 rows
// move focus inside the focused widget and were recorded as `BindingKind::WidgetNative` rather than
// given ActionIds, because an ActionId is a permanent public name and minting one for a behaviour
// nothing dispatches is a name we can never remove. Pin, float and dock-side are that: pane chrome
// belonging to the pane. `view.toggleSidebar` -- which the ribbon, a chord and the batch runner all
// need to reach -- IS an ActionId, and the close button below dispatches it.
QToolButton* makeChromeButton(const QString& tooltip, const QString& glyph)
{
    auto* button = new QToolButton;
    button->setToolTip(tooltip);
    button->setText(glyph);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::TabFocus);
    return button;
}

} // namespace

SidebarDock::SidebarDock(core::ActionRegistry& actions, QWidget* parent)
    : QDockWidget(parent), actions_(actions)
{
    setObjectName(QStringLiteral("sidebarDock"));
    setWindowTitle(catalog::translate(catalog::key::SidebarTitle));

    // Left and right only. Top and bottom are technically possible and would be wrong: a pane strip
    // across the top of a page view is a ribbon that is not a ribbon, and Features.md §1.1 places
    // the sidebar on a side.
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);

    // --- the title bar -------------------------------------------------------------------------
    auto* titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("sidebarTitleBar"));
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(8, 2, 2, 2);
    titleLayout->setSpacing(2);

    auto* titleLabel = new QLabel(catalog::translate(catalog::key::SidebarTitle), titleBar);
    titleLabel->setObjectName(QStringLiteral("sidebarTitleLabel"));
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch(1);

    auto* pinButton = makeChromeButton(catalog::translate(catalog::key::SidebarAutoHide),
                                       QStringLiteral("\u25AD"));   // ▭
    pinButton->setObjectName(QStringLiteral("sidebarPinButton"));
    connect(pinButton, &QToolButton::clicked, this, [this]() { setAutoHidden(!autoHidden_); });
    titleLayout->addWidget(pinButton);

    auto* floatButton = makeChromeButton(catalog::translate(catalog::key::SidebarFloat),
                                         QStringLiteral("\u2750"));  // ❐
    floatButton->setObjectName(QStringLiteral("sidebarFloatButton"));
    connect(floatButton, &QToolButton::clicked, this, [this]() { setFloating(!isFloating()); });
    titleLayout->addWidget(floatButton);

    auto* closeButton = makeChromeButton(catalog::translate(catalog::key::SidebarHide),
                                         QStringLiteral("\u2715"));  // ✕
    closeButton->setObjectName(QStringLiteral("sidebarCloseButton"));
    connect(closeButton, &QToolButton::clicked, this, [this]() {
        // Through the registry, never straight to hide(). The same command has to be reachable from
        // the ribbon (T10), a chord (T13) and the batch runner (Stage 14), and a button that calls
        // hide() directly is a button whose behaviour those three can never share.
        const core::Result<void> dispatched =
            actions_.dispatch(core::ActionId::ViewToggleSidebar, core::KeyContext::Global);
        if (!dispatched) hide();   // unregistered at this stage of the build: still close the pane
    });
    titleLayout->addWidget(closeButton);

    setTitleBarWidget(titleBar);

    // --- the contents --------------------------------------------------------------------------
    auto* body = new QWidget(this);
    body->setObjectName(QStringLiteral("sidebarBody"));
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 12, 12, 12);

    placeholder_ = new QLabel(catalog::translate(catalog::key::SidebarEmpty), body);
    placeholder_->setObjectName(QStringLiteral("sidebarPlaceholder"));
    placeholder_->setWordWrap(true);
    placeholder_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    bodyLayout->addWidget(placeholder_);
    bodyLayout->addStretch(1);

    setWidget(body);
    updateTitleBarState();
}

SidebarDock::~SidebarDock() = default;

void SidebarDock::setPlaceholderText(const QString& text)
{
    if (placeholder_ != nullptr) placeholder_->setText(text);
}

void SidebarDock::setRememberedWidth(int width)
{
    // A zero or negative width is what a collapsed splitter reports on its way out. Recording it
    // would mean the pane comes back invisible and the user concludes it is broken.
    if (width > 0) rememberedWidth_ = width;
}

void SidebarDock::setAutoHidden(bool autoHidden)
{
    if (autoHidden_ == autoHidden) return;
    autoHidden_ = autoHidden;
    peeking_ = false;

    if (autoHidden_) {
        // Capture before hiding: see resizeEvent() for why the width is not read on the way out.
        setRememberedWidth(width());
        hide();
    } else {
        show();
    }

    updateTitleBarState();
    emit autoHiddenChanged(autoHidden_);
}

void SidebarDock::peek()
{
    if (!autoHidden_ || peeking_) return;
    peeking_ = true;
    show();
}

void SidebarDock::unpeek()
{
    if (!peeking_) return;
    peeking_ = false;
    hide();
}

void SidebarDock::resizeEvent(QResizeEvent* event)
{
    QDockWidget::resizeEvent(event);

    // Only while genuinely in use as a docked pane. A floating pane's width is its own window's,
    // and a peeked pane's width is temporary -- recording either would overwrite the width the
    // person actually chose.
    if (!isFloating() && isVisible() && !peeking_) setRememberedWidth(event->size().width());
}

void SidebarDock::leaveEvent(QEvent* event)
{
    QDockWidget::leaveEvent(event);
    unpeek();
}

void SidebarDock::updateTitleBarState()
{
    auto* pinButton = findChild<QToolButton*>(QStringLiteral("sidebarPinButton"));
    if (pinButton == nullptr) return;

    // The tooltip names what the button WILL DO, not what the state is. "Auto-hide" on a pane that
    // is already auto-hidden is the single most common tooltip bug in this kind of chrome.
    pinButton->setToolTip(autoHidden_ ? catalog::translate(catalog::key::SidebarPinOpen)
                                      : catalog::translate(catalog::key::SidebarAutoHide));
}

} // namespace rawpage::ui
