#include "ui/shell/SidebarDock.h"

#include "core/cmd/ActionRegistry.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DockEdge.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
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

    // THE SIDE TO GO BACK TO, for `toggleFloating()` -- only ever a real side: a floating dock reports
    // `NoDockWidgetArea`, and recording that would leave nowhere to return to.
    connect(this, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea area) {
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) lastDockedArea_ = area;
    });

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
    connect(floatButton, &QToolButton::clicked, this, [this]() { toggleFloating(); });
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
    // THE EDGE THAT FACES THE PAGE, drawn (`ui/shell/DockEdge`, the user's request, S5 guide A-124).
    new DockEdge(this);
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

void SidebarDock::toggleFloating()
{
    if (!isFloating()) {
        setFloating(true);
        return;
    }

    // RE-DOCKING IS OURS, because Qt's is broken (S4 guide I-24). All measured on Qt 6.11.1 on the
    // `windows` platform, 2026-09-27:
    //
    //   * `setFloating(false)` clears the flag and leaves the dock OUT of the layout -- a child widget
    //     at its last screen position read as a window position, the page running beneath it. The
    //     same on a bare `QMainWindow`, with and without a custom title bar.
    //   * `restoreDockWidget` puts it back -- but only if the floating pane was NEVER DRAGGED.
    //   * **After the floating pane has been dragged anywhere but an edge** (the user's re-walk found
    //     it), the window's layout is FROZEN: `restoreDockWidget` returns true and moves nothing,
    //     and `addDockWidget`, `removeDockWidget` + `addDockWidget`, a relayout and a resize all
    //     leave the dock where it was. What unfreezes it is a round trip through the public state
    //     API, `restoreState(saveState())`, after which the dock is laid out where the state says
    //     and ordinary docking works again. The mechanism inside Qt was not read -- only its effect
    //     was measured -- so the round trip is ALWAYS done rather than guessed at: it re-applies the
    //     window's own current layout, which is a no-op for everything that was not stuck.
    //
    // The main window is reached as a `QMainWindow` -- Qt's type, not ours -- because docking is what
    // a `QDockWidget` and its `QMainWindow` do together. The header's rule against reaching through
    // the parent is about placing a SIBLING, which this is not. A drag onto an edge never comes
    // here: that is Qt's own plug, which works.
    setFloating(false);
    auto* window = qobject_cast<QMainWindow*>(parentWidget());
    if (window == nullptr) return;

    const bool restored = window->restoreDockWidget(this);
    if (!restored) window->addDockWidget(lastDockedArea_, this);
    window->restoreState(window->saveState());
    if (!restored) window->resizeDocks({this}, {rememberedWidth_}, Qt::Horizontal);
}

bool SidebarDock::event(QEvent* event)
{
    // A DOUBLE-CLICK ON THE TITLE IS THE FLOAT BUTTON. `QDockWidget::event` would otherwise take it
    // and toggle through the same broken `setFloating(false)` (I-24), so it is answered here first --
    // anywhere in the title bar but its buttons, which consume their own clicks.
    if (event->type() == QEvent::MouseButtonDblClick) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const QWidget* title = titleBarWidget();
        if (mouse->button() == Qt::LeftButton && title != nullptr
            && title->geometry().contains(mouse->position().toPoint())) {
            toggleFloating();
            return true;
        }
    }
    return QDockWidget::event(event);
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
