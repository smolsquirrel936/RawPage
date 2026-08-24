#include "ui/shell/ToolFinder.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/KeyBindingStore.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"

#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace rawpage::ui {
namespace toolfinder {
namespace {

using core::ActionId;
using core::KeyContext;

// A subsequence match, the loosest kind we accept: every letter of the query appears in the label
// in order, though not necessarily together. "rtpg" finds "Rotate Pages". Case-folded by the
// caller, once, rather than per character here.
bool isSubsequence(const QString& query, const QString& label)
{
    qsizetype at = 0;
    for (const QChar wanted : query) {
        at = label.indexOf(wanted, at);
        if (at < 0) return false;
        ++at;
    }
    return true;
}

bool anyWordStartsWith(const QString& label, const QString& query)
{
    qsizetype at = 0;
    while (at < label.size()) {
        // A "word" starts after anything that is not a letter or a digit, which handles the space
        // in "Rotate Page", the ellipsis in "Save As…" and the hyphen in "Full-Screen" without
        // three separate rules. It is deliberately naive about CJK, where there are no spaces --
        // there, Prefix and Subsequence carry the search, which is the same behaviour every other
        // ranked search in the product will need and is worth stating rather than pretending.
        if (at == 0 || !label.at(at - 1).isLetterOrNumber()) {
            if (label.mid(at).startsWith(query)) return true;
        }
        ++at;
    }
    return false;
}

std::optional<MatchKind> classify(const QString& foldedLabel, const QString& foldedQuery)
{
    if (foldedLabel == foldedQuery) return MatchKind::Exact;
    if (foldedLabel.startsWith(foldedQuery)) return MatchKind::Prefix;
    if (anyWordStartsWith(foldedLabel, foldedQuery)) return MatchKind::WordStart;
    if (isSubsequence(foldedQuery, foldedLabel)) return MatchKind::Subsequence;
    return std::nullopt;
}

// The chord that would actually fire, resolved the way the store resolves one: the innermost
// context first, then Common, then Global. Reading only the innermost would show nothing for
// `Ctrl+O`, which is a Global binding and is exactly the sort of command this box is for.
QString chordFor(const core::KeyBindingStore& bindings, ActionId id, KeyContext context)
{
    for (const KeyContext candidate : {context, KeyContext::Common, KeyContext::Global}) {
        if (const std::optional<core::Chord> chord = bindings.primaryChordFor(id, candidate))
            return QString::fromStdString(chord->toString());
    }
    return {};
}

} // namespace

std::vector<Match> rank(const QString& query, const core::ActionRegistry& registry,
                        const core::KeyBindingStore& bindings, KeyContext context, int limit)
{
    std::vector<Match> matches;

    const QString folded = query.simplified().toCaseFolded();
    if (folded.isEmpty()) {
        // An empty box lists nothing rather than listing all 358. A list the user did not ask for,
        // in an order nothing justifies, is worse than the placeholder text telling them what the
        // box is for.
        return matches;
    }

    for (const core::ActionRegistry::ActionState& state : registry.allActions()) {
        const QString label = catalog::translateAction(state.id);
        const std::optional<MatchKind> kind = classify(label.toCaseFolded(), folded);
        if (!kind) continue;

        Match match;
        match.id         = state.id;
        match.label      = label;
        match.chord      = chordFor(bindings, state.id, context);
        match.registered = state.registered;
        match.enabled    = registry.isEnabled(state.id);
        match.kind       = *kind;
        matches.push_back(std::move(match));
    }

    std::stable_sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        if (a.kind != b.kind) return a.kind < b.kind;

        // A shorter label that matched the same way matched MORE of itself, which is what a person
        // means by a better hit: for "page", "Pages" beats "Page Thumbnails". Then alphabetically,
        // so the order is total -- without that last tiebreak two equal-length labels could swap
        // between runs and a case asserting the order would flake.
        if (a.label.size() != b.label.size()) return a.label.size() < b.label.size();
        return a.label < b.label;
    });

    if (limit > 0 && matches.size() > static_cast<std::size_t>(limit))
        matches.resize(static_cast<std::size_t>(limit));
    return matches;
}

} // namespace toolfinder

// =======================================================================================
// The widget
// =======================================================================================

ToolFinder::ToolFinder(core::ActionRegistry& registry, core::KeyBindingStore& bindings,
                       QWidget* parent)
    : QWidget(parent), registry_(registry), bindings_(bindings)
{
    setObjectName(QStringLiteral("toolFinder"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("toolFinderSearch"));
    search_->setPlaceholderText(catalog::translate(catalog::key::ToolFinderPlaceholder));
    search_->setClearButtonEnabled(true);

    // That one line made Qt build a QAction for the clear button, and the wiring self-check called
    // it a bare action the first time this widget appeared on a shown window (I-22). It is Qt's,
    // not a command, and this is where the widget's owner says so -- see ActionFactory.h.
    ActionFactory::adoptWidgetActions(search_);
    search_->setMinimumWidth(160);
    search_->setMaximumWidth(240);
    column->addWidget(search_);

    // A popup WINDOW rather than a widget in the layout: the results have to be able to cover the
    // ribbon body below the box, and a list inside the tab strip would either be two pixels tall or
    // would push the ribbon down every time somebody typed.
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("toolFinderResults"));
    list_->setWindowFlags(Qt::Popup);
    list_->setFocusPolicy(Qt::NoFocus);   // typing stays in the box while the list is up
    list_->hide();

    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { refresh(); });
    connect(search_, &QLineEdit::returnPressed, this, [this]() {
        activate(list_->currentRow() >= 0 ? list_->currentRow() : 0);
    });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        activate(list_->row(item));
    });
}

ToolFinder::~ToolFinder()
{
    // I-12's rule, which this class is squarely inside: three child signals are connected to
    // lambdas holding `this`, and `~QWidget` hides children -- after `~ToolFinder` has run. Qt drops
    // those connections only when the QObject part goes, which is last.
    disconnect(search_, nullptr, this, nullptr);
    disconnect(list_, nullptr, this, nullptr);
}

void ToolFinder::setQuery(const QString& query)
{
    search_->setText(query);   // textChanged -> refresh(), so there is one path and not two
}

QString ToolFinder::query() const { return search_->text(); }

void ToolFinder::focusSearchBox()
{
    search_->setFocus(Qt::ShortcutFocusReason);
    search_->selectAll();
}

void ToolFinder::refresh()
{
    const core::KeyContext context = context_ ? context_() : core::KeyContext::Global;
    results_ = toolfinder::rank(search_->text(), registry_, bindings_, context);

    list_->clear();
    for (const toolfinder::Match& match : results_) {
        // The unavailable ones are MARKED rather than dropped -- see the header. The chord goes on
        // the same line, right-aligned by a tab, so the list reads as a command list rather than as
        // search output.
        const QString label = match.registered
                                  ? match.label
                                  : catalog::translate(catalog::key::ToolFinderUnavailable)
                                        .arg(match.label);
        auto* item = new QListWidgetItem(
            match.chord.isEmpty() ? label : label + QLatin1Char('\t') + match.chord, list_);
        item->setFlags(match.enabled ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::NoItemFlags);
    }

    if (results_.empty() && !search_->text().trimmed().isEmpty()) {
        auto* item = new QListWidgetItem(catalog::translate(catalog::key::ToolFinderNoResults)
                                             .arg(search_->text().trimmed()),
                                         list_);
        item->setFlags(Qt::NoItemFlags);
    }

    if (list_->count() > 0) list_->setCurrentRow(0);

    // Only when this widget is on screen. A test that sets a query on a window it never showed
    // should get results, not a popup window opening on somebody's desktop.
    if (isVisible() && list_->count() > 0) {
        list_->setGeometry(QRect(mapToGlobal(QPoint(0, height())),
                                 QSize(qMax(width(), 320), qMin(list_->count(), 10) * 22 + 4)));
        list_->show();
    } else {
        list_->hide();
    }
}

bool ToolFinder::activate(int index)
{
    if (index < 0 || index >= static_cast<int>(results_.size())) return false;

    const toolfinder::Match& match = results_.at(static_cast<std::size_t>(index));
    list_->hide();

    // THROUGH THE REGISTRY, like every other surface. The finder holds no handler of its own: what
    // it runs is the same code the ribbon button runs, which is the property that makes "reachable
    // from the tool finder" and "reachable from the batch runner" the same statement.
    const core::Result<void> dispatched = registry_.dispatch(match.id, context_ ? context_()
                                                                                : core::KeyContext::Global);
    if (dispatched) return true;

    RAWPAGE_LOG_INFO(core::LogSubsystem::Ui)
        << "tool finder: cannot run" << match.label << "--" << dispatched.error().messageKey;
    return false;
}

} // namespace rawpage::ui
