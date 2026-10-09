// ui/panes/StampPalettePane -- the stamp palette: every stamp a person can place, chosen with a click. Stage 6
// T10, decision D6-16 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// WHAT IT LISTS
// ===========================================================================================
// One tree, by category: FAVOURITES (whatever the library marks, built-in or custom), STANDARD (ISO 32000-1
// Table 181's fourteen), DYNAMIC (RawPage's five that bake who and when), SIGNATURES (scanned signatures and
// drawn or typed initials -- markup, not a digital signature, which is Stage 11's) and then each category the
// person filed custom stamps under. A built-in stamp is named in the person's language (`stamps.name.<Name>`)
// beside a preview of the words it draws -- which are the NAME's, in every language (`core/doc/Stamps`); a
// custom one by its library name beside its own picture.
//
// ===========================================================================================
// WHAT IT DOES
// ===========================================================================================
// Choosing a stamp makes it the stamp tool's (`ui::setCurrentStamp`) and asks the window to arm the tool
// (`chosen`) -- a custom stamp's image is read and decoded HERE, once, never per click. The buttons favour,
// refile and delete a library stamp and ask for a new one (`customRequested`, which the window answers with
// `CustomStampDialog`). Nothing here changes a DOCUMENT: the library is the person's (`core::ProfileStore`), and
// placing a stamp is the tool's one command. A window composed with no library (a case, a diagnostic run --
// `app::Services::stamps` null) lists the built-in stamps alone and offers no custom one.
//
// A dock along the right, as Advanced Search is, and for its reason: its subject is the person's stamps, not the
// document in front -- and so it needs no pane id of its own; `annot.stamp` (`Ctrl+Alt+S`) shows it.

#ifndef RAWPAGE_UI_PANES_STAMPPALETTEPANE_H
#define RAWPAGE_UI_PANES_STAMPPALETTEPANE_H

#include "core/Result.h"

#include <QDockWidget>
#include <QPixmap>
#include <QString>

class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace rawpage::core {
class ProfileStore;
}

namespace rawpage::ui {

class StampPalettePane : public QDockWidget {
    Q_OBJECT

public:
    // `library` may be null: built-in stamps only.
    explicit StampPalettePane(core::ProfileStore* library, QWidget* parent = nullptr);

    core::ProfileStore* library() const noexcept { return library_; }
    QTreeWidget* tree() const noexcept { return tree_; }

    // Re-lists everything, keeping the choice where it still exists.
    void reload();

    // A stamp by its key -- `standard:<Name>`, `dynamic:<Name>`, `custom:<id>` -- made the tool's. False, and
    // `refused` emitted, for one that cannot be (an unknown key, a custom stamp whose image is damaged).
    bool choose(const QString& key);
    // The key of the stamp the tool places now, as this palette last chose it.
    QString chosenKey() const;

    // The chosen stamp marked a favourite or not (built-in or custom), filed under `category` (custom only), or
    // deleted (custom only) -- each written to the library at once.
    core::Status setFavourite(bool favourite);
    core::Status fileUnder(const QString& category);
    core::Status deleteChosen();

    // A preview of a built-in stamp's words in its colour, `height` logical pixels tall.
    static QPixmap builtInPreview(const QString& name, int height = 24);

signals:
    // A stamp was chosen: the window arms the stamp tool.
    void chosen();
    // "Custom stamp…" pressed: the window opens the dialog (`annot.customStamp`).
    void customRequested();
    void refused(const core::Error& error);

private:
    void rebuild();
    QTreeWidgetItem* itemFor(const QString& key) const;
    void updateButtons();
    void moveChosenTo();

    core::ProfileStore* library_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QPushButton* custom_ = nullptr;
    QToolButton* favourite_ = nullptr;
    QToolButton* refile_ = nullptr;
    QToolButton* delete_ = nullptr;
    QString chosen_;
    bool rebuilding_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_STAMPPALETTEPANE_H
