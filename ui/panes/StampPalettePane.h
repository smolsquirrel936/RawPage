// ui/panes/StampPalettePane -- the stamp palette: every stamp a person can place, chosen with a click. Stage 6
// T10, decision D6-16 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// WHAT IT LISTS
// ===========================================================================================
// One tree, by category: FAVOURITES (whatever the library marks, built-in or custom -- first, and shown when it
// holds nothing, so a person knows it is there: S6 guide I-41, the user's request at M-07), STANDARD (ISO 32000-1
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
// (`chosen`) -- a custom stamp's image is read and decoded HERE, once, never per click. The highlight is the
// TOOL'S: the window reports it put away (`followTool`) and the highlight goes, because a choice is a change of
// the current item and the item a person just used would otherwise stay current, unclickable (I-39). The buttons
// refile -- into a category chosen from a list, `StampCategoryDialog` (I-40) -- and delete a library stamp and
// ask for a new one (`customRequested`, which the window answers with `CustomStampDialog`); a RIGHT click on any
// stamp adds it to Favourites or removes it (I-41), and a right press is the menu's alone -- it chooses nothing
// and arms nothing. Nothing here changes a DOCUMENT: the library is the person's (`core::ProfileStore`), and
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

class QMenu;
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

    // A stamp by its key marked a favourite or not (built-in or custom) -- the right-click menu's one row (I-41) --
    // and the chosen stamp filed under `category` (custom only) or deleted (custom only); each written to the
    // library at once.
    core::Status setFavourite(const QString& key, bool favourite);
    core::Status fileUnder(const QString& category);
    core::Status deleteChosen();

    // The window says whether the stamp tool is armed: put away, the highlight goes, so one click on the same
    // stamp chooses it again (I-39); armed -- by `Ctrl+Alt+S` too -- the stamp it places is highlighted.
    void followTool(bool armed);

    // The right-click menu at `pos` in the tree's viewport: *Add to* or *Remove from Favourites* for the stamp
    // there. False, and no menu, on a heading, on nothing, or with no library.
    bool showItemMenu(const QPoint& pos);
    QMenu* itemMenu() const noexcept { return itemMenu_; }

    // A preview of a built-in stamp's words in its colour, `height` logical pixels tall.
    static QPixmap builtInPreview(const QString& name, int height = 24);

    // Every custom stamp filed under a built-in heading's name (`StampCategoryDialog::isReservedName`) moved to the
    // default category, *My stamps* in the person's language, each logged; how many moved (S6 guide I-42). Run
    // when a palette is built over a library -- the name refused from then on.
    static int refileReservedCategories(core::ProfileStore& library);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

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
    QToolButton* refile_ = nullptr;
    QToolButton* delete_ = nullptr;
    QMenu* itemMenu_ = nullptr;
    QTreeWidgetItem* favourites_ = nullptr;   // the heading, rebuilt with the tree
    QString chosen_;
    bool rebuilding_ = false;
    bool toolArmed_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_STAMPPALETTEPANE_H
