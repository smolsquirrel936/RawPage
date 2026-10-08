#include "ui/write/FindReplaceBar.h"

#include "core/cmd/ActionRegistry.h"
#include "core/settings/Settings.h"
#include "core/text/SearchQuery.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/SelectionOverlay.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

namespace rawpage::ui {
namespace {

using core::ActionId;
using core::text::SearchEngine;
using core::text::SearchHit;

QToolButton* commandButton(QWidget* parent, ActionId id, const QString& text)
{
    // NOT a default action (convention 23): the button carries its text and dispatches its command, and
    // the bar decides when it is enabled -- it is the one object that knows whether there are hits.
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(catalog::translateAction(id));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::TabFocus);   // keyboard operability, retained
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

// `3-5`, `4` or `5-3`, in the page numbers a person sees (one-based) -> zero-based and ordered.
std::optional<std::pair<int, int>> parseRange(const QString& text, int pageCount)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(\\d+)\\s*(?:-\\s*(\\d+))?\\s*$"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) return std::nullopt;
    int first = match.captured(1).toInt();
    int last = match.captured(2).isEmpty() ? first : match.captured(2).toInt();
    if (first > last) std::swap(first, last);
    if (first < 1 || first > pageCount) return std::nullopt;
    return std::make_pair(first - 1, std::min(last, pageCount) - 1);
}

// THE POPUP THE OPTIONS MOVE INTO when the bar is narrow (guide I-10). A `Qt::Popup`, so a click outside
// closes it -- and `Esc` closes it TOO, before the bar: the binder offers a key to the focused widget first
// (`ShortcutBinder::resolveKey`'s `ShortcutOverride` probe, which propagates up to this window), so
// accepting the probe for `Esc` leaves the key to the widget, and the key press the button ignores reaches
// this window, which closes and hands the focus back (`onEscape`). Every other key is the binder's as
// anywhere in the bar -- `Alt+R`, `Alt+W` and `Alt+C` included. The focus is handed back AFTER the close,
// because closing a popup restores the focus Qt remembered from before it opened.
class OptionsPopup : public QFrame {
public:
    explicit OptionsPopup(QWidget* parent) : QFrame(parent, Qt::Popup)
    {
        setFrameShape(QFrame::StyledPanel);
    }

    std::function<void()> onEscape;

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
        return QFrame::event(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() != Qt::Key_Escape) {
            QFrame::keyPressEvent(event);
            return;
        }
        event->accept();
        close();
        if (onEscape) onEscape();
    }
};

// The status line's room when the options are in the row -- what its minimum was before I-10.
constexpr int kStatusRoom = 120;

} // namespace

FindReplaceBar::FindReplaceBar(app::Services services, QWidget* parent)
    : QWidget(parent), services_(services), session_(services.searchEngine)
{
    setObjectName(QStringLiteral("findBar"));
    // §8's FIND / SEARCH TABLE APPLIES ANYWHERE INSIDE IT -- the field, the scope, the range, the buttons.
    ShortcutBinder::declareContext(this, core::KeyContext::FindSearch);

    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(6, 3, 6, 3);
    row_->setSpacing(4);

    field_ = new QLineEdit(this);
    field_->setObjectName(QStringLiteral("findField"));
    field_->setPlaceholderText(catalog::translate(catalog::key::SearchBarPlaceholder));
    field_->setClearButtonEnabled(false);
    field_->setMinimumWidth(200);
    // ONE THIRD OF THE ROOM IT SHARES WITH THE STATUS LINE -- half the two thirds it had at 2 : 1, on the
    // user's request (2026-10-03, guide A-116). The minimum is unchanged, so a narrow window loses nothing.
    row_->addWidget(field_, 1);

    previous_ = commandButton(this, ActionId::FindPrevious, catalog::translateAction(ActionId::FindPrevious));
    next_ = commandButton(this, ActionId::FindNext, catalog::translateAction(ActionId::FindNext));
    row_->addWidget(previous_);
    row_->addWidget(next_);

    // THE OPTIONS, ONE CONTAINER -- in the row when there is room, in the popup when there is not (I-10).
    options_ = new QWidget(this);
    options_->setObjectName(QStringLiteral("findOptions"));
    optionsLayout_ = new QBoxLayout(QBoxLayout::LeftToRight, options_);
    optionsLayout_->setContentsMargins(0, 0, 0, 0);
    optionsLayout_->setSpacing(4);

    // THE TOGGLES, labelled as their settings are on the Search page -- they ARE those settings.
    regex_ = commandButton(options_, ActionId::FindToggleRegex,
                           catalog::translate(QStringLiteral("prefs.setting.search.useRegex")));
    wholeWords_ = commandButton(options_, ActionId::FindToggleWholeWords,
                                catalog::translate(QStringLiteral("prefs.setting.search.wholeWords")));
    caseSensitive_ = commandButton(options_, ActionId::FindToggleCaseSensitive,
                                   catalog::translate(QStringLiteral("prefs.setting.search.caseSensitive")));
    for (QToolButton* toggle : {regex_, wholeWords_, caseSensitive_}) {
        toggle->setCheckable(true);
        optionsLayout_->addWidget(toggle);
    }

    highlightAll_ = new QCheckBox(catalog::translate(catalog::key::SearchBarHighlightAll), options_);
    highlightAll_->setChecked(true);
    optionsLayout_->addWidget(highlightAll_);

    scope_ = new QComboBox(options_);
    scope_->addItem(catalog::translate(catalog::key::SearchBarScopeDocument));
    scope_->addItem(catalog::translate(catalog::key::SearchBarScopeSelection));
    scope_->addItem(catalog::translate(catalog::key::SearchBarScopePages));
    optionsLayout_->addWidget(scope_);

    range_ = new QLineEdit(options_);
    range_->setObjectName(QStringLiteral("findRange"));
    range_->setPlaceholderText(catalog::translate(catalog::key::SearchBarRangePlaceholder));
    range_->setMaximumWidth(90);
    range_->setVisible(false);
    optionsLayout_->addWidget(range_);
    row_->addWidget(options_);

    // *MORE*, shown only while the options are behind it. A plain button rather than a command: it opens
    // part of this bar, as a ribbon group's button opens its group.
    more_ = new QToolButton(this);
    more_->setObjectName(QStringLiteral("findMore"));
    more_->setText(catalog::translate(catalog::key::SearchBarMore));
    more_->setAutoRaise(true);
    more_->setFocusPolicy(Qt::TabFocus);
    more_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    more_->setVisible(false);
    row_->addWidget(more_);

    auto* popup = new OptionsPopup(this);
    popup->setObjectName(QStringLiteral("findOptionsPopup"));
    auto* popupLayout = new QVBoxLayout(popup);
    popupLayout->setContentsMargins(6, 6, 6, 6);
    // `Esc` gives the focus back to *More*; a click elsewhere leaves it where the click put it.
    popup->onEscape = [this] {
        if (collapsed_ && more_->isVisible()) more_->setFocus(Qt::PopupFocusReason);
    };
    popup_ = popup;

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("findStatus"));
    // NO MINIMUM (I-10): the room it is given in the row is `kStatusRoom`, counted in `expandedWidth`, and a
    // narrow bar clips it rather than widening the window -- its tooltip holds the whole of it.
    status_->setMinimumWidth(0);
    row_->addWidget(status_, 2);

    close_ = commandButton(this, ActionId::FindClose, catalog::translateAction(ActionId::FindClose));
    row_->addWidget(close_);
    connect(more_, &QToolButton::clicked, this, [this] { showOptions(); });

    // EVERY BUTTON DISPATCHES ITS COMMAND -- the same path `F3`, `Alt+R` and `Esc` take.
    connect(previous_, &QToolButton::clicked, this, [this] { dispatch(ActionId::FindPrevious); });
    connect(next_, &QToolButton::clicked, this, [this] { dispatch(ActionId::FindNext); });
    connect(regex_, &QToolButton::clicked, this, [this] { dispatch(ActionId::FindToggleRegex); });
    connect(wholeWords_, &QToolButton::clicked, this, [this] { dispatch(ActionId::FindToggleWholeWords); });
    connect(caseSensitive_, &QToolButton::clicked, this, [this] { dispatch(ActionId::FindToggleCaseSensitive); });
    connect(close_, &QToolButton::clicked, this, [this] { dispatch(ActionId::FindClose); });
    // `Enter` reaches the field only when the binder left it there; either way it is the same command.
    connect(field_, &QLineEdit::returnPressed, this, [this] { dispatch(ActionId::FindJumpToResult); });

    debounce_.setSingleShot(true);
    debounce_.setInterval(kDebounceMs);
    connect(&debounce_, &QTimer::timeout, this, [this] { startSearch(); });
    connect(field_, &QLineEdit::textEdited, this, [this] {
        // A NEW QUERY: the old results are not this one's, so they go now, and the search waits for a pause.
        session_.cancel();
        clearResults();
        if (field_->text().trimmed().isEmpty()) {
            debounce_.stop();
            updateStatus();
            return;
        }
        debounce_.start();
        updateStatus();
    });
    connect(highlightAll_, &QCheckBox::toggled, this, [this](bool all) {
        if (overlay_) overlay_->setHighlightAllHits(all);
    });
    connect(scope_, &QComboBox::currentIndexChanged, this, [this](int index) {
        range_->setVisible(index == static_cast<int>(Scope::Pages));
        // The range widens the options, so the row may no longer hold them (I-10); in the popup it resizes.
        updateCollapse();
        if (collapsed_ && popup_->isVisible()) popup_->adjustSize();
        if (!field_->text().trimmed().isEmpty()) startSearch();
    });
    connect(range_, &QLineEdit::editingFinished, this, [this] {
        if (scope() == Scope::Pages && !field_->text().trimmed().isEmpty()) startSearch();
    });

    // THE TOGGLES FOLLOW THE SETTINGS, whoever changed them -- this bar, `Alt+R` in another window, or the
    // Search page -- and a changed flag re-runs the search under it. Released in the destructor (convention
    // 10's form for a subscription list).
    settingsToken_ = services_.settings.subscribe([this](const core::Settings::Change& change) {
        if (change.group == core::SettingsGroup::Search) onSettingsChanged();
    });

    refreshToggles();
    refreshButtons();
    updateStatus();
    restoreTabOrder();
    setVisible(false);
}

FindReplaceBar::~FindReplaceBar()
{
    services_.settings.unsubscribe(settingsToken_);
    session_.cancel();
    if (overlay_) overlay_->clearSearchHits();
}

void FindReplaceBar::dispatch(ActionId id)
{
    (void)services_.actions.dispatch(id, core::KeyContext::FindSearch);
}

void FindReplaceBar::setDocument(core::text::DocumentIndex::EntryId entry, int pageCount, SelectionOverlay* overlay)
{
    session_.cancel();
    if (overlay_) overlay_->clearSearchHits();
    entry_ = entry;
    pageCount_ = std::max(0, pageCount);
    overlay_ = overlay;
    if (overlay_) overlay_->setHighlightAllHits(highlightAll());
    clearResults();
    updateStatus();
}

// =====================================================================================================
// The commands
// =====================================================================================================

void FindReplaceBar::open()
{
    setVisible(true);
    field_->setFocus(Qt::ShortcutFocusReason);
    field_->selectAll();
    // Re-opened over a query whose highlights `close` cleared: they come back, without asking again.
    if (!hits_.empty() && overlay_) overlay_->setSearchHits(hits_, current_);
}

void FindReplaceBar::close()
{
    debounce_.stop();
    session_.cancel();
    // The popup is a window of its own: hiding the bar does not hide it.
    popup_->hide();
    // EVERY HIGHLIGHT GOES (§8's `Esc`, guide A-72). The query and its results stay, so `Ctrl+F` again
    // shows them; the overlay is what the page paints, and it is empty.
    if (overlay_) overlay_->clearSearchHits();
    setVisible(false);
    emit closed();
}

void FindReplaceBar::findNext()
{
    if (debounce_.isActive() || (!searched_ && !isSearching() && !field_->text().trimmed().isEmpty())) {
        jumpWhenDone_ = true;
        startSearch();
        return;
    }
    if (hits_.empty()) return;
    if (current_ + 1 >= static_cast<int>(hits_.size())) {
        current_ = 0;
        note_ = catalog::translate(catalog::key::SearchBarWrappedToFirst);
    } else {
        ++current_;
        note_.clear();
    }
    showCurrent(true);
}

void FindReplaceBar::findPrevious()
{
    if (debounce_.isActive() || (!searched_ && !isSearching() && !field_->text().trimmed().isEmpty())) {
        jumpWhenDone_ = true;
        startSearch();
        return;
    }
    if (hits_.empty()) return;
    if (current_ <= 0) {
        current_ = static_cast<int>(hits_.size()) - 1;
        note_ = catalog::translate(catalog::key::SearchBarWrappedToLast);
    } else {
        --current_;
        note_.clear();
    }
    showCurrent(true);
}

void FindReplaceBar::jumpToCurrent()
{
    if (debounce_.isActive() || isSearching() || (!searched_ && !field_->text().trimmed().isEmpty())) {
        jumpWhenDone_ = true;
        if (!isSearching()) startSearch();
        return;
    }
    if (hits_.empty()) return;
    if (current_ < 0) current_ = 0;
    showCurrent(true);
}

void FindReplaceBar::presentHits(const QString& pattern, const std::vector<SearchHit>& hits, int current)
{
    debounce_.stop();
    session_.cancel();
    clearResults();
    // `setText` emits no `textEdited`, so this starts no search of its own.
    field_->setText(pattern);
    hits_ = hits;
    searched_ = true;
    current_ = hits_.empty() ? -1 : std::clamp(current, 0, static_cast<int>(hits_.size()) - 1);
    setVisible(true);
    if (overlay_) overlay_->setSearchHits(hits_, current_);
    showCurrent(current_ >= 0);
}

// =====================================================================================================
// The search
// =====================================================================================================

void FindReplaceBar::setScope(Scope scope)
{
    scope_->setCurrentIndex(static_cast<int>(scope));
}

FindReplaceBar::Scope FindReplaceBar::scope() const noexcept
{
    return static_cast<Scope>(std::clamp(scope_->currentIndex(), 0, 2));
}

void FindReplaceBar::setPageRange(const QString& range)
{
    range_->setText(range);
    if (scope() == Scope::Pages && !field_->text().trimmed().isEmpty()) startSearch();
}

bool FindReplaceBar::highlightAll() const noexcept
{
    return highlightAll_->isChecked();
}

void FindReplaceBar::setHighlightAll(bool all)
{
    highlightAll_->setChecked(all);
}

bool FindReplaceBar::isSearching() const
{
    return session_.isRunning();
}

void FindReplaceBar::startSearch()
{
    debounce_.stop();
    clearResults();
    const QString pattern = field_->text();
    if (pattern.trimmed().isEmpty() || entry_ == 0) {
        session_.cancel();
        updateStatus();
        return;
    }

    core::text::SearchScope where;
    switch (scope()) {
    case Scope::Document:
        break;
    case Scope::Selection:
        if (overlay_ == nullptr || !overlay_->hasSelection()) {
            session_.cancel();
            error_ = catalog::translate(catalog::key::SearchBarNoSelection);
            updateStatus();
            return;
        }
        where = core::text::SearchScope::within(overlay_->selection());
        break;
    case Scope::Pages: {
        const auto range = parseRange(range_->text(), pageCount_);
        if (!range) {
            session_.cancel();
            error_ = catalog::translate(catalog::key::SearchBarBadRange);
            updateStatus();
            return;
        }
        where = core::text::SearchScope::pages(range->first, range->second);
        break;
    }
    }

    // THE FLAGS AND THE BOUND FROM THE SETTINGS -- the one reader (`SearchQuery::fromSettings`, D5-14).
    const core::text::SearchQuery query = core::text::SearchQuery::fromSettings(services_.settings, pattern, where);
    SearchEngine::Handlers handlers;
    handlers.progress = [this](const SearchEngine::Progress& progress) { onProgress(progress); };
    handlers.finished = [this](const SearchEngine::Outcome& outcome) { onFinished(outcome); };
    const core::Status started = session_.start(entry_, query, std::move(handlers), commentSource_ ? commentSource_() : nullptr);
    if (!started) {
        // AN INVALID PATTERN SEARCHES NOTHING AND SAYS SO (guide A-57): the error, never an empty list.
        error_ = catalog::translateError(QString(started.error().messageKey), started.error().context);
        jumpWhenDone_ = false;
        updateStatus();
        return;
    }
    ++started_;
    progressTotal_ = 0;
    updateStatus();
}

void FindReplaceBar::onProgress(const SearchEngine::Progress& progress)
{
    progressDone_ = progress.pagesSearched;
    progressTotal_ = progress.pagesInScope;
    updateStatus();
}

void FindReplaceBar::onFinished(const SearchEngine::Outcome& outcome)
{
    ++finished_;
    searched_ = true;
    hits_ = outcome.hits;
    limitReached_ = outcome.limitReached;
    unmappedGlyphs_ = outcome.unmappedGlyphs;
    unmappedPages_ = static_cast<int>(outcome.unmappedPages.size());
    failedPages_ = static_cast<int>(outcome.failedPages.size());
    progressTotal_ = 0;
    note_.clear();

    // THE CURRENT HIT IS THE FIRST WHERE THE READER IS, OR AFTER IT -- a search started on page 400 that
    // jumped back to page 1 would take the reader away from where they are looking.
    current_ = hits_.empty() ? -1 : 0;
    if (!hits_.empty() && currentPage_) {
        const int here = currentPage_();
        const auto at = std::find_if(hits_.begin(), hits_.end(), [here](const SearchHit& hit) { return hit.page >= here; });
        current_ = at == hits_.end() ? 0 : static_cast<int>(at - hits_.begin());
    }
    if (overlay_) overlay_->setSearchHits(hits_, current_);
    // AN INCREMENTAL FIND FOLLOWS ITS RESULT: the view goes to the current hit as soon as it is known.
    jumpWhenDone_ = false;
    showCurrent(!hits_.empty());
}

void FindReplaceBar::showCurrent(bool reveal)
{
    if (overlay_) overlay_->setCurrentSearchHit(current_);
    if (reveal && overlay_ && current_ >= 0 && current_ < static_cast<int>(hits_.size())) {
        const SearchHit& hit = hits_[static_cast<std::size_t>(current_)];
        switch (hit.contentClass()) {
        case core::text::ContentClass::Bookmarks:
            // TO ITS DESTINATION (D5-12), through the path the Bookmarks pane takes.
            if (hit.unit->destination.pageIndex >= 0) emit destinationActivated(hit.unit->destination);
            break;
        case core::text::ContentClass::Attachments:
            // SELECTED, AND NOTHING OPENS (D5-12): the status line names the file; opening it is Stage 6's.
            // (Until T14 this `break` sat at the end of the comment line above, so the case fell through to
            // `default:` -- harmless only because an attachment hit has no rectangle on any page.)
            break;
        default:
            // Page text, a comment, a field: the page and the rectangle.
            if (const std::optional<core::PdfRect> rect = overlay_->searchHitRect(hit)) emit hitActivated(hit.page, *rect);
            // A COMMENT HIT SELECTS ITS COMMENT (S6 T9, D6-11) -- once revealed, so the page is where it is.
            if (hit.contentClass() == core::text::ContentClass::Comments) emit annotationHitActivated(hit.unit->locator);
            break;
        }
    }
    refreshButtons();
    updateStatus();
}

void FindReplaceBar::clearResults()
{
    hits_.clear();
    current_ = -1;
    searched_ = false;
    error_.clear();
    note_.clear();
    limitReached_ = false;
    unmappedGlyphs_ = unmappedPages_ = failedPages_ = 0;
    progressDone_ = progressTotal_ = 0;
    if (overlay_) overlay_->clearSearchHits();
    refreshButtons();
}

void FindReplaceBar::onSettingsChanged()
{
    refreshToggles();
    // THE QUERY UNDER NEW FLAGS IS A NEW QUERY: run again at once, not after a pause nobody is typing in.
    if (isVisible() && !field_->text().trimmed().isEmpty()) startSearch();
}

void FindReplaceBar::refreshToggles()
{
    regex_->setChecked(services_.settings.get(core::keys::search::useRegex));
    wholeWords_->setChecked(services_.settings.get(core::keys::search::wholeWords));
    caseSensitive_->setChecked(services_.settings.get(core::keys::search::caseSensitive));
}

void FindReplaceBar::refreshButtons()
{
    const bool any = !hits_.empty();
    previous_->setEnabled(any);
    next_->setEnabled(any);
}

// =====================================================================================================
// What it says
// =====================================================================================================

QString FindReplaceBar::statusText() const
{
    return status_->text();
}

void FindReplaceBar::updateStatus()
{
    QStringList parts;
    if (!error_.isEmpty()) {
        parts << error_;
    } else if (session_.isRunning()) {
        parts << catalog::translate(catalog::key::SearchBarSearching).arg(progressDone_).arg(progressTotal_);
    } else if (searched_) {
        if (hits_.empty())
            parts << catalog::translate(catalog::key::SearchBarNoMatches);
        else
            parts << catalog::translate(catalog::key::SearchBarCount)
                         .arg(current_ + 1)
                         .arg(static_cast<int>(hits_.size()));
        // WHERE THE CURRENT HIT IS, when it is not page text (T8): a highlight on a field's box does not say
        // whether the name or the value matched, and an attachment hit shows nothing on any page.
        if (current_ >= 0 && current_ < static_cast<int>(hits_.size())) {
            const SearchHit& hit = hits_[static_cast<std::size_t>(current_)];
            switch (hit.contentClass()) {
            case core::text::ContentClass::PageText:
                break;
            case core::text::ContentClass::Comments:
                parts << catalog::translate(catalog::key::SearchBarInComment);
                break;
            case core::text::ContentClass::Fields:
                parts << catalog::translate(catalog::key::SearchBarInField).arg(hit.unit->locator);
                break;
            case core::text::ContentClass::Bookmarks:
                parts << catalog::translate(catalog::key::SearchBarInBookmark);
                break;
            case core::text::ContentClass::Attachments:
                parts << catalog::translate(catalog::key::SearchBarInAttachment).arg(hit.unit->locator);
                break;
            }
        }
        if (!note_.isEmpty()) parts << note_;
        if (limitReached_)
            parts << catalog::translate(catalog::key::SearchBarLimitReached).arg(static_cast<int>(hits_.size()));
        // ALWAYS SAID WHEN THERE ARE ANY (guide A-21): glyphs with no Unicode were not searched, and a
        // *no matches* without this would claim the document holds no such word.
        if (unmappedGlyphs_ > 0)
            parts << catalog::translate(catalog::key::SearchBarUnmapped).arg(unmappedGlyphs_).arg(unmappedPages_);
        if (failedPages_ > 0) parts << catalog::translate(catalog::key::SearchBarFailedPages).arg(failedPages_);
    }
    status_->setText(parts.join(QStringLiteral(" · ")));
    // A narrow bar clips the line rather than widening the window (I-10); the tooltip holds all of it.
    status_->setToolTip(status_->text());
}

// =====================================================================================================
// The collapse (guide I-10)
// =====================================================================================================

int FindReplaceBar::essentialsWidth(int statusRoom) const
{
    const QMargins margins = row_->contentsMargins();
    int width = margins.left() + margins.right() + field_->minimumWidth() + statusRoom;
    for (const QWidget* button : {static_cast<const QWidget*>(previous_), static_cast<const QWidget*>(next_),
                                  static_cast<const QWidget*>(close_)})
        width += button->sizeHint().width();
    // field | previous | next | status | close
    return width + 4 * row_->spacing();
}

int FindReplaceBar::optionsRowWidth() const
{
    // From the widgets themselves, not from `options_->sizeHint()`: in the popup the options are a COLUMN,
    // and the question is how wide they would be as a row.
    int width = 0;
    int shown = 0;
    for (const QWidget* option : {static_cast<const QWidget*>(regex_), static_cast<const QWidget*>(wholeWords_),
                                  static_cast<const QWidget*>(caseSensitive_), static_cast<const QWidget*>(highlightAll_),
                                  static_cast<const QWidget*>(scope_), static_cast<const QWidget*>(range_)}) {
        if (option->isHidden()) continue;
        width += option->sizeHint().width();
        ++shown;
    }
    return width + std::max(0, shown - 1) * optionsLayout_->spacing();
}

int FindReplaceBar::expandedWidth() const
{
    return essentialsWidth(kStatusRoom) + row_->spacing() + optionsRowWidth();
}

QSize FindReplaceBar::minimumSizeHint() const
{
    // In BOTH states: in the row, the options' own minimum would otherwise become the window's again.
    const int width = essentialsWidth(0) + row_->spacing() + more_->sizeHint().width();
    return {width, QWidget::minimumSizeHint().height()};
}

void FindReplaceBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCollapse();
}

void FindReplaceBar::updateCollapse()
{
    const int available = width();
    if (!collapsed_ && available < expandedWidth()) {
        setCollapsed(true);
    } else if (collapsed_ && available >= expandedWidth() + kHysteresis) {
        setCollapsed(false);
    }
}

void FindReplaceBar::setCollapsed(bool collapsed)
{
    if (collapsed == collapsed_) return;
    collapsed_ = collapsed;
    // THE SAME WIDGETS MOVE, never a copy -- so the toggles stay the settings in either place.
    if (collapsed) {
        row_->removeWidget(options_);
        optionsLayout_->setDirection(QBoxLayout::TopToBottom);
        options_->setParent(popup_);
        popup_->layout()->addWidget(options_);
        options_->show();
        more_->show();
    } else {
        popup_->hide();
        popup_->layout()->removeWidget(options_);
        optionsLayout_->setDirection(QBoxLayout::LeftToRight);
        options_->setParent(this);
        row_->insertWidget(row_->indexOf(more_), options_);
        options_->show();
        more_->hide();
    }
    restoreTabOrder();
    updateGeometry();
}

void FindReplaceBar::showOptions()
{
    if (!collapsed_) return;
    popup_->adjustSize();
    // Below *More*, right-aligned to it, and kept on the screen it is on.
    QPoint at = more_->mapToGlobal(QPoint(more_->width() - popup_->width(), more_->height()));
    if (const QScreen* screen = more_->screen()) {
        const QRect area = screen->availableGeometry();
        at.setX(std::clamp(at.x(), area.left(), std::max(area.left(), area.right() - popup_->width())));
        at.setY(std::clamp(at.y(), area.top(), std::max(area.top(), area.bottom() - popup_->height())));
    }
    popup_->move(at);
    popup_->show();
    regex_->setFocus(Qt::PopupFocusReason);
}

void FindReplaceBar::restoreTabOrder()
{
    // RE-PARENTING APPENDS A WIDGET TO THE END OF THE CHAIN (the ribbon's I-05), so the chain is set in
    // VISUAL order after every move: the options in the row when they are there; *More* when they are not,
    // and they chain among themselves inside the popup.
    std::vector<QWidget*> chain{field_, previous_, next_};
    const std::vector<QWidget*> options{regex_, wholeWords_, caseSensitive_, highlightAll_, scope_, range_};
    if (collapsed_) {
        chain.push_back(more_);
        for (std::size_t at = 1; at < options.size(); ++at) QWidget::setTabOrder(options[at - 1], options[at]);
    } else {
        chain.insert(chain.end(), options.begin(), options.end());
    }
    chain.push_back(close_);
    for (std::size_t at = 1; at < chain.size(); ++at) QWidget::setTabOrder(chain[at - 1], chain[at]);
}

} // namespace rawpage::ui
