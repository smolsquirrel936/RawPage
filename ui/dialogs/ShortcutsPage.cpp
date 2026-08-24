#include "ui/dialogs/ShortcutsPage.h"

#include "core/Log.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/Settings.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/ShortcutBinder.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

using core::ActionId;
using core::Chord;
using core::KeyContext;
using core::LogSubsystem;

QString chordText(const Chord& chord)
{
    return QString::fromStdString(chord.toString());
}

// The context's own name, translated. The KEY comes from `catalog::keyContextKey`, which derives it
// from `keyContextName` -- for the reason D1-6 gives every time this comes up, and so that
// `--dump-ui-strings` emits exactly the keys this page asks for.
QString contextLabel(KeyContext context)
{
    return catalog::translate(catalog::keyContextKey(context));
}

} // namespace

ShortcutsPage::ShortcutsPage(core::Settings& settings,
                             core::KeyBindingStore& bindings,
                             QWidget* parent)
    : QWidget(parent), settings_(settings), bindings_(bindings)
{
    setObjectName(QStringLiteral("shortcutsPage"));

    auto* column = new QVBoxLayout(this);

    // --- The context selector ------------------------------------------------------------------
    auto* contextRow = new QHBoxLayout;
    contextRow->addWidget(new QLabel(catalog::translate(catalog::key::PrefsShortcutsContext), this));

    contextChooser_ = new QComboBox(this);
    contextChooser_->setObjectName(QStringLiteral("shortcutContext"));
    for (std::size_t i = 0; i < core::kKeyContextCount; ++i) {
        const auto context = static_cast<KeyContext>(i);
        contextChooser_->addItem(contextLabel(context), static_cast<int>(i));
    }
    contextRow->addWidget(contextChooser_, 1);
    column->addLayout(contextRow);

    // --- The table -----------------------------------------------------------------------------
    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("shortcutTable"));
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels(
        QStringList{catalog::translate(catalog::key::PrefsShortcutsColumnCommand),
                    catalog::translate(catalog::key::PrefsShortcutsColumnChord),
                    catalog::translate(catalog::key::PrefsShortcutsColumnSource)});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    column->addWidget(table_, 1);

    // --- The editor ----------------------------------------------------------------------------
    auto* editorRow = new QHBoxLayout;
    editorRow->addWidget(new QLabel(catalog::translate(catalog::key::PrefsShortcutsCapture), this));

    capture_ = new QKeySequenceEdit(this);
    capture_->setObjectName(QStringLiteral("shortcutCapture"));
    // ONE chord, not a sequence. §8 binds single chords -- `Chord` cannot express `Ctrl+K, Ctrl+D`
    // and neither can the settings key that persists it -- so letting the widget collect four would
    // be offering the user something the product cannot store.
    capture_->setMaximumSequenceLength(1);
    editorRow->addWidget(capture_, 1);

    assign_ = new QPushButton(catalog::translate(catalog::key::PrefsShortcutsAssign), this);
    assign_->setObjectName(QStringLiteral("shortcutAssign"));
    editorRow->addWidget(assign_);

    reset_ = new QPushButton(catalog::translate(catalog::key::PrefsShortcutsReset), this);
    reset_->setObjectName(QStringLiteral("shortcutReset"));
    editorRow->addWidget(reset_);
    column->addLayout(editorRow);

    // --- The shadow report ---------------------------------------------------------------------
    shadows_ = new QLabel(this);
    shadows_->setObjectName(QStringLiteral("shortcutShadows"));
    shadows_->setWordWrap(true);
    shadows_->setTextFormat(Qt::PlainText);
    column->addWidget(shadows_);

    connect(contextChooser_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        setContext(static_cast<KeyContext>(contextChooser_->itemData(index).toInt()));
    });
    connect(assign_, &QPushButton::clicked, this, [this] { applyPendingChord(); });
    connect(reset_, &QPushButton::clicked, this, [this] {
        resetRow(table_->currentRow());
    });

    rebuildTable();
}

ShortcutsPage::~ShortcutsPage()
{
    // Every connection above names `this` as its receiver and every child dies with this widget,
    // so Qt drops them all. The destructor is declared out of line anyway because the header
    // forward-declares the widget types -- the members are raw pointers Qt owns, and a
    // `= default` in the header would need every one of those types complete.
}

void ShortcutsPage::setContext(KeyContext context)
{
    if (context == context_) return;
    context_ = context;

    // The chooser may already be showing it -- this function is called both from the chooser and
    // directly -- and setting it again would re-enter through `currentIndexChanged`. The early
    // return above is what stops that being a loop.
    if (contextChooser_->currentData().toInt() != static_cast<int>(context))
        contextChooser_->setCurrentIndex(contextChooser_->findData(static_cast<int>(context)));

    rebuildTable();
}

int ShortcutsPage::rowCount() const { return static_cast<int>(rows_.size()); }

std::optional<Chord> ShortcutsPage::chordAt(int row) const
{
    if (row < 0 || row >= rowCount()) return std::nullopt;
    return rows_[static_cast<std::size_t>(row)].chord;
}

std::optional<ActionId> ShortcutsPage::actionAt(int row) const
{
    if (row < 0 || row >= rowCount()) return std::nullopt;
    const RowModel& model = rows_[static_cast<std::size_t>(row)];
    if (!model.dispatched) return std::nullopt;
    return model.action;
}

bool ShortcutsPage::isOverriddenAt(int row) const
{
    const std::optional<ActionId> action = actionAt(row);
    if (!action) return false;
    return bindings_.overrideChordFor(*action, context_).has_value();
}

void ShortcutsPage::rebuildTable()
{
    rows_.clear();
    for (const core::KeyBinding& binding : bindings_.bindingsFor(context_)) {
        RowModel model;
        model.chord      = binding.chord;
        model.dispatched = binding.action.has_value();
        // A widget-native row has no action. `ActionId` is an enum with no null, so the flag is
        // what carries the difference and this value is never read for such a row.
        model.action = binding.action.value_or(static_cast<ActionId>(0));
        rows_.push_back(model);
    }

    table_->setRowCount(rowCount());
    for (int row = 0; row < rowCount(); ++row) {
        const RowModel& model = rows_[static_cast<std::size_t>(row)];

        // A widget-native row is SHOWN. §8 documents those chords, the store records them, and
        // hiding them here would make the page disagree with the document it is a view of -- the
        // user would find `Tab` missing from Forms and reasonably conclude it was unbound.
        const QString command = model.dispatched
            ? catalog::translateAction(model.action)
            : catalog::translate(catalog::key::PrefsShortcutsSourceWidget);

        const QString source = !model.dispatched
            ? catalog::translate(catalog::key::PrefsShortcutsSourceWidget)
            : (bindings_.overrideChordFor(model.action, context_)
                   ? catalog::translate(catalog::key::PrefsShortcutsSourceCustom)
                   : catalog::translate(catalog::key::PrefsShortcutsSourceDefault));

        table_->setItem(row, 0, new QTableWidgetItem(command));
        table_->setItem(row, 1, new QTableWidgetItem(chordText(model.chord)));
        table_->setItem(row, 2, new QTableWidgetItem(source));
    }

    updateShadowLabel();
}

QStringList ShortcutsPage::shadowSummary() const
{
    QStringList lines;
    for (const core::Shadow& shadow : bindings_.shadows()) {
        if (shadow.context != context_) continue;
        lines << catalog::translate(catalog::key::PrefsShortcutsShadowLine)
                     .arg(chordText(shadow.chord),
                          catalog::translateAction(shadow.contextAction),
                          catalog::translateAction(shadow.globalAction));
    }
    return lines;
}

void ShortcutsPage::updateShadowLabel()
{
    const QStringList lines = shadowSummary();
    if (lines.isEmpty()) {
        shadows_->setText(catalog::translate(catalog::key::PrefsShortcutsNoShadows));
        return;
    }
    shadows_->setText(catalog::translate(catalog::key::PrefsShortcutsShadowsHeading)
                      + QLatin1Char('\n') + lines.join(QLatin1Char('\n')));
}

void ShortcutsPage::applyPendingChord()
{
    const QKeySequence sequence = capture_->keySequence();
    if (sequence.count() != 1) return;

    // THE CONVERSION IS `ShortcutBinder`'s, not a second one written here. `core/cmd/Chord` exists
    // because `core/` may not name a QtGui type, and `ShortcutBinder` is the file its header
    // nominated to do the conversion -- a page that parsed `QKeySequence::toString()` back into a
    // chord would be a second spelling of every key in §8.
    const QKeyCombination combination = sequence[0];
    const std::optional<Chord> chord =
        ShortcutBinder::chordFor(combination.key(), combination.keyboardModifiers());
    if (!chord) {
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "shortcuts: no chord can be formed from" << sequence.toString();
        return;
    }

    if (rebindRow(table_->currentRow(), *chord)) capture_->clear();
}

bool ShortcutsPage::rebindRow(int row, const Chord& chord)
{
    const std::optional<ActionId> action = actionAt(row);
    if (!action) return false;

    const core::Result<void> bound = bindings_.rebind(chord, context_, *action);
    if (!bound) {
        // The store's one refusal: remapping ONTO a chord the focused widget owns natively. Logged
        // rather than silent, because from the user's side the button simply did nothing.
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "shortcuts: refused --" << bound.error().messageKey << bound.error().context;
        return false;
    }

    persist(context_, *action, chord);
    rebuildTable();
    return true;
}

void ShortcutsPage::resetRow(int row)
{
    const std::optional<ActionId> action = actionAt(row);
    if (!action) return;

    // Cleared by the ACTION's override chord rather than by the row's chord. After a rebind the row
    // on screen may be the new chord or the freed default, and clearing "whatever chord this row
    // shows" would do nothing at all in the second case -- which is exactly the row a user clicks
    // when they want their key back.
    if (const std::optional<Chord> chosen = bindings_.overrideChordFor(*action, context_))
        bindings_.clearOverride(*chosen, context_);

    persist(context_, *action, std::nullopt);
    rebuildTable();
}

void ShortcutsPage::restoreDefaults()
{
    // Every context, not just the one on screen: "restore defaults" on a page whose selector says
    // Viewer restoring only Viewer would leave a user hunting through twelve contexts for the one
    // remap they cannot remember making.
    for (const core::KeyBindingStore::OverrideEntry& entry : bindings_.exportOverrides()) {
        if (const std::optional<ActionId> action = core::actionFromStringId(entry.actionStringId))
            persist(entry.context, *action, std::nullopt);
    }
    bindings_.clearAllOverrides();
    rebuildTable();

    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "shortcuts: every user remap restored to Features.md 8";
}

void ShortcutsPage::persist(KeyContext context, ActionId action, const std::optional<Chord>& chord)
{
    // BOTH OBJECTS, and this is the half that is not the store. See the header: the store makes the
    // chord work now, this makes it work tomorrow, and a remap that reaches only one of them is a
    // feature the user will describe as broken either way.
    const core::ShortcutKey key = core::ShortcutKey::forAction(context, action);
    if (chord)
        settings_.setShortcutOverride(key, chordText(*chord));
    else
        settings_.clearShortcutOverride(key);
}

} // namespace rawpage::ui
