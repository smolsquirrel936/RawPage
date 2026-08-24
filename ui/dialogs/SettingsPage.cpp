#include "ui/dialogs/SettingsPage.h"

#include "core/Log.h"
#include "core/settings/Settings.h"
#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVariant>

#include <limits>
#include <optional>
#include <type_traits>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;
using core::SettingsChoice;
using core::SettingsEditor;
using core::SettingsEditorTraits;

QString qstr(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// The catalog key for a key's label, resolved. `settingsLabelKey` derives it from the path -- see
// `SettingsSchema.h` on why it is derived rather than a column somebody fills in.
QString labelFor(std::string_view path)
{
    return catalog::translate(QString::fromStdString(core::settingsLabelKey(path)));
}

} // namespace

SettingsPage::SettingsPage(core::Settings& settings, core::SettingsGroup group, QWidget* parent)
    : QWidget(parent), settings_(settings), group_(group)
{
    setObjectName(QStringLiteral("settingsPage:") + qstr(core::settingsGroupName(group)));

    form_ = new QFormLayout(this);
    form_->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form_->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // THE GENERATION. One row per declared key whose group matches; the filter is inside `addRow`
    // rather than here, because the macro cannot see a `Key<T>`'s group without instantiating it.
#define RAWPAGE_SETTING(Namespace, Name, Type, Path, Default, Group, PolicyLockable) \
    addRow(core::keys::Namespace::Name);
#define RAWPAGE_SETTING_RANGED(Namespace, Name, Type, Path, Default, Group, PolicyLockable, Min, Max) \
    addRow(core::keys::Namespace::Name);
#include "core/settings/SettingsKeys.def"

    // Flow J, in the direction nobody remembers to build: something ELSE changed a setting -- the
    // other window's Preferences page, a `restoreDefaults()`, the status bar's theme item -- and
    // this page is looking at a stale value. One subscription for the page rather than one per row,
    // because a page has one lifetime and a row does not have its own.
    settingsSubscription_ = settings_.subscribe([this](const core::Settings::Change& change) {
        if (change.group != group_) return;
        refreshFromSettings();
    });
}

SettingsPage::~SettingsPage()
{
    // Not `= default`, and for the reason `core::Settings::subscribe` has tokens at all: the
    // subscription holds `this`, `Settings` outlives every window, and a handler called after this
    // destructor ran is issue I-12 arriving through a fourth door.
    if (settingsSubscription_ != 0) settings_.unsubscribe(settingsSubscription_);
}

QStringList SettingsPage::rowPaths() const
{
    QStringList paths;
    paths.reserve(static_cast<qsizetype>(rows_.size()));
    for (const Row& row : rows_) paths << row.path;
    return paths;
}

QWidget* SettingsPage::editorFor(const QString& path) const
{
    const auto it = editors_.find(path);
    return it == editors_.end() ? nullptr : it->second;
}

void SettingsPage::refreshFromSettings()
{
    // The guard is what stops a refresh writing back. See the member's comment.
    const bool was = updating_;
    updating_ = true;
    for (const Row& row : rows_)
        if (row.refresh) row.refresh();
    updating_ = was;
}

// ===============================================================================================
// One row
// ===============================================================================================

template <class T>
void SettingsPage::addRow(const core::Key<T>& key)
{
    if (key.group != group_) return;

    // `Internal` never reaches here because no page is built for it (SettingsKeys.def rule 4), but
    // saying so in code as well costs one line and makes a future page-for-every-group loop safe.
    if (key.group == core::SettingsGroup::Internal) return;

    const QString path  = qstr(key.path);
    const QString label = labelFor(key.path);

    Row row;
    row.path = path;

    constexpr SettingsEditor kEditor = SettingsEditorTraits<T>::editor;

    // `if constexpr` on the SCHEMA's answer rather than on the type directly. The two are the same
    // fact, and going through the schema is what makes them impossible to disagree: a type given a
    // new editor kind in `SettingsSchema.h` and no branch here fails to compile at that key's row.
    if constexpr (kEditor == SettingsEditor::Toggle) {
        auto* box = new QCheckBox(label, this);
        box->setChecked(settings_.get(key));
        connect(box, &QCheckBox::toggled, this, [this, key, path](bool on) {
            if (updating_) return;
            settings_.set(key, on);
            emit settingChanged(path);
        });
        // Spanning, with the text on the box itself: a checkbox in a form's field column beside a
        // repeated label reads as two controls.
        form_->addRow(box);
        row.editor  = box;
        row.refresh = [this, box, key] { box->setChecked(settings_.get(key)); };

    } else if constexpr (kEditor == SettingsEditor::Integer) {
        auto* spin = new QSpinBox(this);
        // A key with no declared range still needs bounds, because a spin box has them whether or
        // not anybody chose them. The widest int the control can hold is the honest choice: it
        // imposes no limit the schema did not.
        spin->setRange(key.hasRange ? key.minimum : std::numeric_limits<int>::min(),
                       key.hasRange ? key.maximum : std::numeric_limits<int>::max());
        spin->setValue(settings_.get(key));
        connect(spin, &QSpinBox::valueChanged, this, [this, key, path](int value) {
            if (updating_) return;
            settings_.set(key, value);
            emit settingChanged(path);
        });
        form_->addRow(label, spin);
        row.editor  = spin;
        row.refresh = [this, spin, key] { spin->setValue(settings_.get(key)); };

    } else if constexpr (kEditor == SettingsEditor::Decimal) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setDecimals(2);
        spin->setSingleStep(0.05);
        spin->setRange(key.hasRange ? key.minimum : -1e9, key.hasRange ? key.maximum : 1e9);
        spin->setValue(settings_.get(key));
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, key, path](double value) {
            if (updating_) return;
            settings_.set(key, value);
            emit settingChanged(path);
        });
        form_->addRow(label, spin);
        row.editor  = spin;
        row.refresh = [this, spin, key] { spin->setValue(settings_.get(key)); };

    } else if constexpr (kEditor == SettingsEditor::Text) {
        auto* edit = new QLineEdit(this);
        edit->setText(settings_.get(key));
        // `editingFinished` rather than `textChanged`: writing a settings key on every keystroke
        // would emit a `changed` per character, and from Stage 2 flow J invalidates the render
        // cache off that signal. A locale typed one letter at a time would be six invalidations.
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, key, path] {
            if (updating_) return;
            settings_.set(key, edit->text());
            emit settingChanged(path);
        });
        form_->addRow(label, edit);
        row.editor  = edit;
        row.refresh = [this, edit, key] { edit->setText(settings_.get(key)); };

    } else if constexpr (kEditor == SettingsEditor::List) {
        auto* edit = new QLineEdit(this);
        edit->setText(settings_.get(key).join(QLatin1Char(';')));
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, key, path] {
            if (updating_) return;
            settings_.set(key, edit->text().split(QLatin1Char(';'), Qt::SkipEmptyParts));
            emit settingChanged(path);
        });
        form_->addRow(label, edit);
        row.editor  = edit;
        row.refresh = [this, edit, key] {
            edit->setText(settings_.get(key).join(QLatin1Char(';')));
        };

    } else if constexpr (kEditor == SettingsEditor::Choice) {
        auto* combo = new QComboBox(this);
        // THE TOKENS ARE THE CODEC'S, so the combo box cannot offer a value the file cannot hold
        // and cannot fail to offer one it can. Each item carries its token as data and the value
        // is recovered through `SettingsCodec<T>` -- the same conversion the reader and the writer
        // use, rather than a second opinion about what "dark" means.
        for (const SettingsChoice& choice : SettingsEditorTraits<T>::choices())
            combo->addItem(catalog::translate(QString::fromStdString(choice.labelKey)),
                           QString::fromStdString(choice.token));

        const auto indexOfValue = [combo](const T& value) {
            const QVariant token = core::SettingsCodec<T>::toVariant(value);
            return combo->findData(token.toString());
        };
        combo->setCurrentIndex(indexOfValue(settings_.get(key)));

        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, key, path](int index) {
            if (updating_ || index < 0) return;
            const std::optional<T> decoded =
                core::SettingsCodec<T>::fromVariant(combo->itemData(index));
            if (!decoded) return;   // impossible unless the token table and the codec disagree
            settings_.set(key, *decoded);
            emit settingChanged(path);
        });
        form_->addRow(label, combo);
        row.editor  = combo;
        row.refresh = [this, combo, key, indexOfValue] {
            combo->setCurrentIndex(indexOfValue(settings_.get(key)));
        };

    } else if constexpr (kEditor == SettingsEditor::Flags) {
        // A subset of a closed set: one check box per member, in a grid so seven subsystems do not
        // become a seven-row form.
        auto* holder = new QWidget(this);
        auto* grid   = new QGridLayout(holder);
        grid->setContentsMargins(0, 0, 0, 0);

        const std::vector<SettingsChoice> choices = SettingsEditorTraits<T>::choices();
        std::vector<QCheckBox*> boxes;
        boxes.reserve(choices.size());

        for (std::size_t i = 0; i < choices.size(); ++i) {
            auto* box = new QCheckBox(
                catalog::translate(QString::fromStdString(choices[i].labelKey)), holder);
            box->setObjectName(QStringLiteral("prefFlag:") + path + QLatin1Char(':')
                               + QString::fromStdString(choices[i].token));
            grid->addWidget(box, static_cast<int>(i / 4), static_cast<int>(i % 4));
            boxes.push_back(box);
        }

        // Read and write the whole mask at once, from the boxes. Reading the mask back out of the
        // widgets rather than mutating a copy is what keeps the two in step when a refresh lands
        // between two clicks.
        const auto maskFromBoxes = [boxes, choices]() {
            QStringList on;
            for (std::size_t i = 0; i < boxes.size(); ++i)
                if (boxes[i]->isChecked()) on << QString::fromStdString(choices[i].token);
            // The empty set is a real, choosable value and the codec spells it `none`; an empty
            // string would read back as "unset" and silently restore the default.
            const std::optional<T> decoded = core::SettingsCodec<T>::fromVariant(
                on.isEmpty() ? QStringLiteral("none") : on.join(QLatin1Char(',')));
            return decoded;
        };

        const auto apply = [this, boxes, choices, key] {
            const QVariant text = core::SettingsCodec<T>::toVariant(settings_.get(key));
            const QStringList on = text.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (std::size_t i = 0; i < boxes.size(); ++i)
                boxes[i]->setChecked(on.contains(QString::fromStdString(choices[i].token)));
        };

        // APPLIED BEFORE THE CONNECTIONS ARE MADE, which every other branch above gets for free by
        // setting its widget's value before connecting. Here the check boxes are built first, so
        // the order has to be chosen: a `setChecked` that reached a connected slot would write the
        // current value back to `Settings` at construction and mark a key the user never touched.
        apply();

        for (QCheckBox* box : boxes) {
            connect(box, &QCheckBox::toggled, this, [this, key, path, maskFromBoxes] {
                if (updating_) return;
                if (const std::optional<T> mask = maskFromBoxes()) {
                    settings_.set(key, *mask);
                    emit settingChanged(path);
                }
            });
        }

        form_->addRow(label, holder);
        row.editor  = holder;
        row.refresh = apply;

    } else {
        // No `else` branch is reachable: `SettingsEditorTraits` is specialized per type and every
        // kind it can report is handled above. The assertion is what turns a NEW kind into a
        // compile error at the row that introduced it rather than a blank space on a page.
        static_assert(kEditor == SettingsEditor::Toggle, "unhandled SettingsEditor kind");
    }

    if (row.editor) {
        row.editor->setObjectName(QStringLiteral("pref:") + path);

        // Stage 15's `PolicyOverlay` disables the control here and says which policy locked it.
        // Nothing locks anything today, so `policyLockable` is carried and not acted on -- and
        // that is the honest state rather than a stub that greys nothing.
        editors_.emplace(path, row.editor);
    }
    rows_.push_back(std::move(row));
}

template <class T>
void SettingsPage::resetKey(const core::Key<T>& key)
{
    if (key.group != group_) return;
    settings_.reset(key);
}

void SettingsPage::restoreDefaults()
{
    // The same walk, a second time. Two includes of one table rather than a list of keys held
    // somewhere between them.
#define RAWPAGE_SETTING(Namespace, Name, Type, Path, Default, Group, PolicyLockable) \
    resetKey(core::keys::Namespace::Name);
#define RAWPAGE_SETTING_RANGED(Namespace, Name, Type, Path, Default, Group, PolicyLockable, Min, Max) \
    resetKey(core::keys::Namespace::Name);
#include "core/settings/SettingsKeys.def"

    // The subscription above already refreshes on each `reset`, but only for a value that actually
    // changed -- `changed` fires on an ACTUAL change only (A-42), which is exactly right and means
    // a page whose values were all already default gets no notification at all. Refreshing once
    // here makes the widgets right in both cases.
    refreshFromSettings();

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "preferences: restored defaults for" << qstr(core::settingsGroupName(group_));
}

} // namespace rawpage::ui
