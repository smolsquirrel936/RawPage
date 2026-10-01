#include "ui/write/FindReplaceBar.h"

#include "core/cmd/ActionRegistry.h"
#include "core/settings/Settings.h"
#include "core/text/SearchQuery.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/SelectionOverlay.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QToolButton>

#include <algorithm>

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

} // namespace

FindReplaceBar::FindReplaceBar(app::Services services, QWidget* parent)
    : QWidget(parent), services_(services), session_(services.searchEngine)
{
    setObjectName(QStringLiteral("findBar"));
    // §8's FIND / SEARCH TABLE APPLIES ANYWHERE INSIDE IT -- the field, the scope, the range, the buttons.
    ShortcutBinder::declareContext(this, core::KeyContext::FindSearch);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 3, 6, 3);
    row->setSpacing(4);

    field_ = new QLineEdit(this);
    field_->setObjectName(QStringLiteral("findField"));
    field_->setPlaceholderText(catalog::translate(catalog::key::SearchBarPlaceholder));
    field_->setClearButtonEnabled(false);
    field_->setMinimumWidth(200);
    row->addWidget(field_, 2);

    previous_ = commandButton(this, ActionId::FindPrevious, catalog::translateAction(ActionId::FindPrevious));
    next_ = commandButton(this, ActionId::FindNext, catalog::translateAction(ActionId::FindNext));
    row->addWidget(previous_);
    row->addWidget(next_);

    // THE TOGGLES, labelled as their settings are on the Search page -- they ARE those settings.
    regex_ = commandButton(this, ActionId::FindToggleRegex,
                           catalog::translate(QStringLiteral("prefs.setting.search.useRegex")));
    wholeWords_ = commandButton(this, ActionId::FindToggleWholeWords,
                                catalog::translate(QStringLiteral("prefs.setting.search.wholeWords")));
    caseSensitive_ = commandButton(this, ActionId::FindToggleCaseSensitive,
                                   catalog::translate(QStringLiteral("prefs.setting.search.caseSensitive")));
    for (QToolButton* toggle : {regex_, wholeWords_, caseSensitive_}) {
        toggle->setCheckable(true);
        row->addWidget(toggle);
    }

    highlightAll_ = new QCheckBox(catalog::translate(catalog::key::SearchBarHighlightAll), this);
    highlightAll_->setChecked(true);
    row->addWidget(highlightAll_);

    scope_ = new QComboBox(this);
    scope_->addItem(catalog::translate(catalog::key::SearchBarScopeDocument));
    scope_->addItem(catalog::translate(catalog::key::SearchBarScopeSelection));
    scope_->addItem(catalog::translate(catalog::key::SearchBarScopePages));
    row->addWidget(scope_);

    range_ = new QLineEdit(this);
    range_->setObjectName(QStringLiteral("findRange"));
    range_->setPlaceholderText(catalog::translate(catalog::key::SearchBarRangePlaceholder));
    range_->setMaximumWidth(90);
    range_->setVisible(false);
    row->addWidget(range_);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("findStatus"));
    status_->setMinimumWidth(120);
    row->addWidget(status_, 1);

    close_ = commandButton(this, ActionId::FindClose, catalog::translateAction(ActionId::FindClose));
    row->addWidget(close_);

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
    const core::Status started = session_.start(entry_, query, std::move(handlers));
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
        if (const std::optional<core::PdfRect> rect = overlay_->searchHitRect(hit)) emit hitActivated(hit.page, *rect);
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
}

} // namespace rawpage::ui
