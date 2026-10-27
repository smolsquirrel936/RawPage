// ui/dialogs/StampCategoryDialog -- where the palette's *Move to…* files a custom stamp: one of the library's
// categories, chosen from a list, or a new one named by *New…*. Stage 6 guide I-40 (the user's request at M-07,
// 2026-10-27), replacing a free-text prompt in which a person had to remember and retype a category's name.
//
// IT COLLECTS; IT CHANGES NOTHING. Like `CustomStampDialog`, it constructs no command -- a stamp's category is the
// person's library, not a document -- and answers `chosenCategory()`, which `StampPalettePane::fileUnder` writes.
// A NEW category exists only once a stamp is filed under it (`core::ProfileStore::categories()` lists the ones in
// use), so *New…* adds the name to this list alone; Cancel leaves the library as it was. Signatures is always
// offered, by its shown word, and answered as the reserved key.
//
// *New…* asks with a `QInputDialog`, which offscreen cannot answer; `addCategory` is what it does with the name,
// and what a case drives.

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

    // What *New…* does with the name typed: added to the list -- or found there -- and selected. False for a name
    // that is empty once trimmed. The shown word for Signatures selects the reserved category.
    bool addCategory(const QString& name);
    // The category chosen, as the library stores it.
    QString chosenCategory() const;

    QComboBox* list() const noexcept { return list_; }
    QPushButton* newButton() const noexcept { return new_; }

private:
    void askNewCategory();

    QComboBox* list_ = nullptr;
    QPushButton* new_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_STAMPCATEGORYDIALOG_H
