#include "ui/shell/SidebarDock.h"

#include "core/cmd/ActionRegistry.h"
#include "ui/shell/Catalog.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

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

    titleLabel_ = new QLabel(catalog::translate(catalog::key::SidebarTitle), titleBar);
    titleLabel_->setObjectName(QStringLiteral("sidebarTitleLabel"));
    titleLayout->addWidget(titleLabel_);
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
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // THE PANE CHOOSER, and it DISPATCHES rather than switching the stack itself. Selecting a pane
    // is `pane.thumbnails` -- a declared command with a §8 chord, reachable from the ribbon, the
    // tool finder and the Stage 14 batch runner -- so a combo that called `showPane()` directly
    // would be a second way to do the same thing that none of those three could share. It is the
    // close button's argument (above), arriving at the one other control here that has an id.
    //
    // Hidden while there are fewer than two panes: a chooser with one entry is furniture.
    chooser_ = new QComboBox(body);
    chooser_->setObjectName(QStringLiteral("sidebarPaneChooser"));
    chooser_->hide();
    connect(chooser_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (suppressChooserSignal_ || index < 0 || index >= static_cast<int>(panes_.size())) return;
        const core::ActionId id = panes_[static_cast<std::size_t>(index)].id;
        const core::Result<void> dispatched = actions_.dispatch(id, core::KeyContext::Global);
        if (!dispatched) showPane(id);   // unregistered: still switch, as the close button still closes
    });
    bodyLayout->addWidget(chooser_);

    stack_ = new QStackedWidget(body);
    stack_->setObjectName(QStringLiteral("sidebarPaneStack"));

    // PAGE 0 IS THE PLACEHOLDER, and it is what a shell with no panes registered shows. Its wrapper
    // carries the margins the panes do not want: a thumbnail column draws to its own edges.
    auto* placeholderPage = new QWidget(stack_);
    auto* placeholderLayout = new QVBoxLayout(placeholderPage);
    placeholderLayout->setContentsMargins(12, 12, 12, 12);
    placeholder_ = new QLabel(catalog::translate(catalog::key::SidebarEmpty), placeholderPage);
    placeholder_->setObjectName(QStringLiteral("sidebarPlaceholder"));
    placeholder_->setWordWrap(true);
    placeholder_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    placeholderLayout->addWidget(placeholder_);
    placeholderLayout->addStretch(1);
    stack_->addWidget(placeholderPage);

    bodyLayout->addWidget(stack_, 1);

    setWidget(body);
    updateTitleBarState();
}

SidebarDock::~SidebarDock() = default;

void SidebarDock::setPlaceholderText(const QString& text)
{
    if (placeholder_ != nullptr) placeholder_->setText(text);
}

// =============================================================================================
// The pane registry -- D4-2
// =============================================================================================

bool SidebarDock::addPane(core::ActionId id, const QString& title, QWidget* pane)
{
    if (pane == nullptr) return false;
    if (std::any_of(panes_.begin(), panes_.end(), [id](const Pane& p) { return p.id == id; }))
        return false;

    panes_.push_back(Pane{id, title, pane});
    stack_->addWidget(pane);
    updateChooser();

    // THE FIRST PANE COMES TO THE FRONT, which is what makes the placeholder invisible from the
    // moment one exists rather than after the first click.
    if (panes_.size() == 1) showPane(id);
    return true;
}

bool SidebarDock::showPane(core::ActionId id)
{
    const auto found = std::find_if(panes_.begin(), panes_.end(),
                                    [id](const Pane& p) { return p.id == id; });
    if (found == panes_.end()) return false;

    stack_->setCurrentWidget(found->widget);

    // The title names the pane in front rather than saying "Panes", because a dock showing
    // thumbnails under a heading that says "Panes" tells the reader nothing they cannot already
    // see. The chooser follows without re-dispatching -- see `suppressChooserSignal_`.
    if (titleLabel_ != nullptr) titleLabel_->setText(found->title);
    if (chooser_ != nullptr) {
        const auto index = static_cast<int>(std::distance(panes_.begin(), found));
        if (chooser_->currentIndex() != index) {
            suppressChooserSignal_ = true;
            chooser_->setCurrentIndex(index);
            suppressChooserSignal_ = false;
        }
    }

    emit currentPaneChanged(id);
    return true;
}

std::optional<core::ActionId> SidebarDock::currentPane() const
{
    if (panes_.empty() || stack_ == nullptr) return std::nullopt;
    const QWidget* front = stack_->currentWidget();
    for (const Pane& pane : panes_)
        if (pane.widget == front) return pane.id;
    return std::nullopt;
}

QWidget* SidebarDock::paneWidget(core::ActionId id) const
{
    for (const Pane& pane : panes_)
        if (pane.id == id) return pane.widget;
    return nullptr;
}

void SidebarDock::updateChooser()
{
    if (chooser_ == nullptr) return;

    // REBUILT FROM `panes_`, never appended to. A combo maintained incrementally beside a vector is
    // two lists of the same thing, which is the shape convention 17 is about: the surface reads the
    // table rather than a running description of it.
    suppressChooserSignal_ = true;
    const std::optional<core::ActionId> front = currentPane();
    chooser_->clear();
    for (const Pane& pane : panes_) chooser_->addItem(pane.title);
    if (front) {
        for (std::size_t i = 0; i < panes_.size(); ++i)
            if (panes_[i].id == *front) chooser_->setCurrentIndex(static_cast<int>(i));
    }
    suppressChooserSignal_ = false;

    chooser_->setVisible(panes_.size() > 1);
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
