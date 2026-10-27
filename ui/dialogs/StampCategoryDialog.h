// ui/dialogs/StampCategoryDialog -- where the palette's *Move to…* files a custom stamp: one of the library's
// categories, chosen from a list, or a new one named by *New…*. Stage 6 guide I-40 (the user's request at M-07,
// 2026-10-27), replacing a free-text prompt in which a person had to remember and retype a category's name.
//
// IT COLLECTS; IT CHANGES NOTHING. Like `CustomStampDialog`, it constructs no command -- a stamp's category is the
// person's library, not a document -- and answers `chosenCategory()`, which `StampPalettePane::fileUnder` writes.
// A NEW category exists only once a stamp is filed under it (`core::ProfileStore::categories()` lists the ones in
// use), so *New…* adds the name to this list alone; Cancel leaves the library as it was. Signatures is always
// offered from the list.
//
// A BUILT-IN HEADING'S NAME IS NOT A CATEGORY'S (S6 guide I-42, the user's rule, 2026-10-27). A category named
// *Dynamic* is shown as a second *Dynamic* heading beside the built-in one, so Favourites, Standard, Dynamic and
// Signatures are refused as a new name -- in EVERY shipped language, whichever is showing, and in any case, once
// trimmed (`isReservedName`). The words are held here, one row per shipped catalog, because `core/` knows no shown
// word and the catalogs load after the library (`app/main.cpp`); `tests/ui/tst_stamps` holds the table to the
// catalogs themselves, so a heading renamed in a catalog fails a case rather than reopening the gap. The rule is
// the PALETTE'S, not the library's: `core::ProfileStore` reads any category, and a stamp already filed under such a
// name is moved to the default category when the palette is built (`StampPalettePane::refileReservedCategories`).
//
// NOR IS A CATEGORY ALREADY LISTED (S6 guide I-45, the user's choice, 2026-10-27): *New…* given a name the list holds
// -- in any case -- refuses it rather than quietly choosing it, so a person who meant a new category is told it is
// not one. A refusal of either kind is a WARNING BOX, not a line in this dialog, and the name is then asked again
// with what was typed in it.
//
// *New…* asks with a `QInputDialog` and warns with a `QMessageBox`, which offscreen cannot answer; `addCategory` is
// what it does with the name, `refusal()` what the box would say, and what a case drives.

#ifndef RAWPAGE_UI_DIALOGS_STAMPCATEGORYDIALOG_H
#define RAWPAGE_UI_DIALOGS_STAMPCATEGORYDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>

class QComboBox;
class QPushButton;

namespace rawpage::ui {

class StampCategoryDialog : public QDialog {
    Q_OBJECT

public:
    // `categories` as `core::ProfileStore::categories()` gives them; `current`, the stamp's own, is selected.
    StampCategoryDialog(const QStringList& categories, const QString& current, QWidget* parent = nullptr);

    // What *New…* does with the name typed: added to the list and selected. False for a name that is empty once
    // trimmed, a built-in heading's (`isReservedName`) or one the list holds already, in any case -- the last two
    // with a `refusal()`, which *New…* shows in a warning box (I-45).
    bool addCategory(const QString& name);
    // The category chosen, as the library stores it.
    QString chosenCategory() const;
    // Why the last name offered was not added; empty when it was, or when it was empty.
    QString refusal() const;

    // The built-in headings' names, in every shipped language: what no category may be called.
    static const QStringList& reservedNames();
    // Whether `name`, trimmed, is one of them in any case -- or the shown word of one in the catalog loaded now.
    static bool isReservedName(const QString& name);

    QComboBox* list() const noexcept { return list_; }
    QPushButton* newButton() const noexcept { return new_; }

private:
    void askNewCategory();

    QComboBox* list_ = nullptr;
    QPushButton* new_ = nullptr;
    QString refusal_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_STAMPCATEGORYDIALOG_H
