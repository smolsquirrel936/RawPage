// One Preferences page, GENERATED from `core/settings/SettingsKeys.def` -- Stage 1 T14.
//
// ---------------------------------------------------------------------------------------------
// "EVERY PAGE BUILT FROM `SettingsSchema`, NOT HAND-LAID-OUT" IS THE WHOLE POINT OF THIS FILE.
//
// `Stage1-AppShell-Plan.md` §6 puts it in the T14 row and it is not a style preference. There are
// 26 preference groups and this stage builds three of them; the other 23 arrive across thirteen
// stages, each one adding keys. If a page is a hand-written form, then adding a key in Stage 9 is
// a schema row plus a widget plus a read plus a write plus a change subscription -- five places,
// four of which someone will forget once -- and the symptom is a preference that exists in the
// file, is honoured by the code, and cannot be changed by anybody. If a page is generated, adding
// a key is a row in `SettingsKeys.def` and nothing else, exactly as adding a ribbon button is a
// line in `res/ribbon.json` (D1-8) and adding an action is a row in `ActionId.def` (D1-6). This is
// the third instance of the same decision, and the reason it keeps being made is that it keeps
// being right.
//
// ---------------------------------------------------------------------------------------------
// HOW IT IS GENERATED, AND WHY THE WALK IS TYPED RATHER THAN OVER `allKeys()`.
//
// `SettingsSchema::allKeys()` is a runtime table of descriptors and it would have been the obvious
// thing to iterate. It cannot be, and the reason is D1-10: `core::Settings` has NO string-keyed
// `get`/`set`, on purpose, because "an undeclared key does not compile" is only true if there is
// no way to name a key that does not exist. A page built from descriptors would need exactly that
// API, and adding it for the Preferences dialog's convenience would make the guarantee decorative
// for the whole product -- which is the failure D1-10 was written to prevent.
//
// So this file `#include`s `SettingsKeys.def` itself, with a macro that calls a TEMPLATE for each
// row. Each instantiation holds the real `constexpr Key<T>` and calls the typed `get`/`set`. The
// generation is the same mechanism, and the type safety is not weakened by an inch.
//
// `SettingsSchema`'s runtime view is still what says which WIDGET a row wants
// (`SettingsEditorTraits`) and what to call it (`settingsLabelKey`), so nothing here decides
// anything about a key that the schema does not already know.
//
// ---------------------------------------------------------------------------------------------
// WHAT IS NOT HERE, STATED SO ITS ABSENCE IS A FACT RATHER THAN A GAP.
//
//   * Policy locks. `Key<T>::policyLockable` is carried by the schema and `Stage 15`'s
//     `PolicyOverlay` is what will set them; nothing locks anything today, so nothing renders as
//     locked. `applyLock()` is the one place that will change, and it is one line.
//   * Preference SEARCH, and export/import of the whole set (`Features.md` §9.1). Stage 14.
//   * A per-key "reset this one" affordance. The page resets as a page, which is what §9.1 asks
//     for; per-key reset needs a hover surface this stage has no icons for.

#ifndef RAWPAGE_UI_DIALOGS_SETTINGSPAGE_H
#define RAWPAGE_UI_DIALOGS_SETTINGSPAGE_H

#include "core/settings/SettingsSchema.h"

#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <map>
#include <vector>

class QFormLayout;

namespace rawpage::core {
class Settings;
}

namespace rawpage::ui {

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    SettingsPage(core::Settings& settings, core::SettingsGroup group, QWidget* parent = nullptr);
    ~SettingsPage() override;

    core::SettingsGroup group() const { return group_; }

    // Every settings path this page rendered a row for, in declaration order. The tests compare it
    // against `SettingsSchema::allKeys()` filtered by group, which is what makes "a key added to
    // the schema appears on its page with no widget code" an assertion rather than a claim.
    QStringList rowPaths() const;
    int         rowCount() const { return static_cast<int>(rowPaths().size()); }

    // The editor widget for a path, or null. The page names each editor `pref:<path>`, so a test
    // can also find one with `findChild`; this is the direct route.
    QWidget* editorFor(const QString& path) const;

    // Every key on this page back to its declared default. The page-level half of `Features.md`
    // §9.1's "reset one page, or all preferences, to defaults".
    void restoreDefaults();

signals:
    // Emitted after this page writes a value. The dialog uses it for nothing today; it exists
    // because a page that changes settings silently is a page whose effect cannot be observed by
    // a test without reaching into `Settings`, and reaching in is how a test stops describing the
    // user's path.
    void settingChanged(const QString& path);

private:
    // One row per key whose group matches. Defined in the .cpp and instantiated by including
    // `SettingsKeys.def` there -- see the header comment on why the walk is typed.
    template <class T>
    void addRow(const core::Key<T>& key);

    template <class T>
    void resetKey(const core::Key<T>& key);

    // Re-reads every row from `Settings`. Called when something else changes a value -- another
    // window's page, a `restoreDefaults()`, or (from Stage 15) a policy refresh.
    void refreshFromSettings();

    core::Settings&     settings_;
    core::SettingsGroup group_;
    QFormLayout*        form_ = nullptr;

    struct Row {
        QString                  path;
        QWidget*                 editor = nullptr;
        std::function<void()>    refresh;   // Settings -> widget
    };
    std::vector<Row>            rows_;
    std::map<QString, QWidget*> editors_;

    int settingsSubscription_ = 0;

    // True while a row is being written FROM `Settings`. Without it, `setValue` on a spin box emits
    // `valueChanged`, which writes back to `Settings`, which notifies, which refreshes the row --
    // a loop that terminates only because the value stops changing, and that leaves a spurious
    // `changed` in its wake. Flow J invalidates the render cache off that signal from Stage 2.
    bool updating_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_SETTINGSPAGE_H
