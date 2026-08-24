// The Preferences surface -- Stage 1 T14.
//
// ---------------------------------------------------------------------------------------------
// IT IS MODELESS, AND IT APPLIES IMMEDIATELY. Both halves of that are one decision.
//
// The obvious shape is a modal dialog with OK / Cancel / Apply, and it is wrong here for a reason
// that is structural rather than stylistic: `Cancel` means "put everything back", and putting a
// setting back requires `core::Settings` to hold a transaction it does not have -- it loads once
// into memory and writes on `sync()`, deliberately, because D1-16's downgrade rule cannot be
// expressed through a live `QSettings` (see `Settings.h`). A `Cancel` that only stopped FUTURE
// writes while the theme the user just previewed stayed dark would be a button that lies.
//
// Applying immediately is also what makes the surface honest about flow J. Changing
// `general/theme` repaints the window behind the dialog, and changing `general/uiFontScale`
// resizes it, because `ThemeManager` subscribes to `Settings` and always has. A modal dialog would
// hide the very thing the setting does.
//
// And modeless is what keeps `file.preferences` REACHABLE. `file.open` had to be given an
// enablement predicate that is false with no user present (issue I-23), because `QFileDialog::exec`
// under `QT_QPA_PLATFORM=offscreen` shows a dialog to nobody and never returns -- it stopped a
// whole `ctest` run dead. A modeless `show()` blocks nothing, so this command needs no such guard,
// dispatches headlessly, and is testable end to end from the chord that raises it. That is a
// better answer than I-23's, and it was available here because the dialog had no reason to be
// modal in the first place.
//
// ---------------------------------------------------------------------------------------------
// THREE PAGES, AND WHICH THREE.
//
// `Features.md` §9.1 lists 26 preference groups and `Stage1-AppShell-Plan.md` §11 says three ship
// in Stage 1. The plan's T14 row names two of them -- General and Shortcuts -- and Logging is the
// third: T14 also ships the log pane, `logging/level` and `logging/subsystems` are the filters
// that decide what that pane can ever show, and `logging/retainFiles` had been a declared key with
// no surface since T5. A generated page costs a row in `shippedPages()`, so the choice was between
// three pages and three settings nobody could change.
//
// The other 23 groups are declared in `SettingsGroup` and have no page. That is not a gap: a page
// for a group with no keys is an empty box that says the product has a preference it does not.

#ifndef RAWPAGE_UI_DIALOGS_PREFERENCESDIALOG_H
#define RAWPAGE_UI_DIALOGS_PREFERENCESDIALOG_H

#include "app/Services.h"
#include "core/settings/SettingsSchema.h"

#include <QDialog>

#include <map>
#include <vector>

class QListWidget;
class QStackedWidget;

namespace rawpage::ui {

class SettingsPage;
class ShortcutsPage;

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(const app::Services& services, QWidget* parent = nullptr);
    ~PreferencesDialog() override;

    // The groups that have a page, in the order they are listed. Static so a test can assert the
    // dialog builds every one of them without constructing a window first.
    static const std::vector<core::SettingsGroup>& shippedPages();

    void                showPage(core::SettingsGroup group);
    core::SettingsGroup currentPage() const;

    // Null for a group with no page, which is 23 of the 26.
    SettingsPage*  settingsPage(core::SettingsGroup group) const;
    ShortcutsPage* shortcutsPage() const { return shortcuts_; }

private:
    void restoreCurrentPageDefaults();

    app::Services services_;

    QListWidget*    pageList_ = nullptr;
    QStackedWidget* pageStack_ = nullptr;
    ShortcutsPage*  shortcuts_ = nullptr;

    std::map<core::SettingsGroup, SettingsPage*> pages_;
    std::vector<core::SettingsGroup>             order_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_PREFERENCESDIALOG_H
