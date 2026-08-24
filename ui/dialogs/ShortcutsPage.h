// The Shortcuts preference page -- Stage 1 T14.
//
// ---------------------------------------------------------------------------------------------
// WHAT IT EDITS, AND WHY THAT IS TWO OBJECTS RATHER THAN ONE.
//
// `core::KeyBindingStore` holds the live binding and answers every chord the user presses;
// `core::Settings` holds the persisted override under D1-10's `shortcuts/<context>/<action>` key
// family. A remap has to reach BOTH, and in that order: the store so the new chord works before
// the user has closed the dialog, and the settings so it is still there tomorrow. Writing only the
// store gives a remap that vanishes at the next launch; writing only the settings gives one that
// arrives at the next launch, which is worse, because the user tests it immediately and concludes
// the feature is broken.
//
// Nothing is rebuilt when a chord changes. That is T13's design paying off rather than a claim
// made here: `ShortcutBinder` resolves through the store on every key press and holds no copy, so
// a rebind is live at the next keystroke with no widget touched. It is also why no chord is ever a
// `QAction` shortcut -- 358 actions carrying a shadow copy of the store is precisely what this
// page would have had to keep in step.
//
// ---------------------------------------------------------------------------------------------
// SHADOWS ARE SHOWN, NOT PREVENTED.
//
// `KeyBindingStore::shadows()` reports class 1 only -- a context redefining a chord `Global` also
// claims -- and §8 ships six of those BY DESIGN. A page that refused such a remap would be a page
// that refuses the product's own defaults. So the shadows for the context on screen are listed
// below the table, in the store's words, and the user decides.
//
// ---------------------------------------------------------------------------------------------
// WHY THE TABLE IS PER CONTEXT AND NOT ONE FLAT LIST OF 248 ROWS.
//
// Because the context is what makes two rows with the same chord different rather than
// contradictory. A flat list would show `Ctrl+K` twice with no way to say why, and the first
// support question would be which one wins -- which is the resolution order, which is the model.
// The context selector is the model made visible.

#ifndef RAWPAGE_UI_DIALOGS_SHORTCUTSPAGE_H
#define RAWPAGE_UI_DIALOGS_SHORTCUTSPAGE_H

#include "core/cmd/ActionId.h"
#include "core/cmd/Chord.h"
#include "core/cmd/KeyContext.h"

#include <QString>
#include <QStringList>
#include <QWidget>

#include <optional>
#include <vector>

class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QTableWidget;

namespace rawpage::core {
class KeyBindingStore;
class Settings;
}

namespace rawpage::ui {

class ShortcutsPage : public QWidget {
    Q_OBJECT

public:
    ShortcutsPage(core::Settings& settings,
                  core::KeyBindingStore& bindings,
                  QWidget* parent = nullptr);
    ~ShortcutsPage() override;

    core::KeyContext context() const { return context_; }
    void             setContext(core::KeyContext context);

    int rowCount() const;

    // The binding a row shows. Nothing outside a test needs it; a test needs it in order to drive
    // the page the way a user does rather than by reaching past it into the store.
    std::optional<core::Chord>    chordAt(int row) const;
    std::optional<core::ActionId> actionAt(int row) const;
    bool                          isOverriddenAt(int row) const;

    // Rebind the row's ACTION to `chord`, in the current context. Returns false and changes
    // nothing when the store refuses -- which it does for a chord a widget owns natively, the one
    // refusal `KeyBindingStore` makes.
    bool rebindRow(int row, const core::Chord& chord);

    // Drop the user's override on this row, restoring what §8 specifies. Not an error if there
    // was none.
    void resetRow(int row);

    // Every user override in every context, back to §8. The page-level counterpart of
    // `SettingsPage::restoreDefaults()`.
    void restoreDefaults();

    // What the store reports as shadowed in the CURRENT context, one line each, in the store's own
    // words. Empty is the ordinary state for most contexts.
    QStringList shadowSummary() const;

private:
    void rebuildTable();
    void updateShadowLabel();
    void applyPendingChord();
    void persist(core::KeyContext context, core::ActionId action,
                 const std::optional<core::Chord>& chord);

    core::Settings&        settings_;
    core::KeyBindingStore& bindings_;
    core::KeyContext       context_ = core::KeyContext::Global;

    QComboBox*        contextChooser_ = nullptr;
    QTableWidget*     table_ = nullptr;
    QKeySequenceEdit* capture_ = nullptr;
    QPushButton*      assign_ = nullptr;
    QPushButton*      reset_ = nullptr;
    QLabel*           shadows_ = nullptr;

    // Mirrors the table's rows. A `QTableWidget` can only carry a QVariant per cell, and putting a
    // chord and an action id through QVariant round trips them as strings -- one more place for
    // the two spellings of a chord to differ. The row model is kept here instead.
    struct RowModel {
        core::Chord    chord;
        core::ActionId action;
        bool           dispatched = true;   // false for a §8 row the focused widget owns
    };
    std::vector<RowModel> rows_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_SHORTCUTSPAGE_H
