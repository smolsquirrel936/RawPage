// ui/dialogs/StampCategoryDialog -- implementation. Stage 6 guide I-40. See the header.

#include "ui/dialogs/StampCategoryDialog.h"

#include "core/settings/ProfileStore.h"
#include "ui/shell/Catalog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace rawpage::ui {

StampCategoryDialog::StampCategoryDialog(const QStringList& categories, const QString& current, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("stampCategoryDialog"));
    setWindowTitle(catalog::translate(catalog::key::StampsRefile));
    auto* column = new QVBoxLayout(this);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(catalog::translate(catalog::key::StampsRefilePrompt), this));
    list_ = new QComboBox(this);
    list_->setObjectName(QStringLiteral("stampCategoryList"));
    list_->addItem(catalog::translate(catalog::key::StampsCategorySignatures),
                   QString::fromLatin1(core::ProfileStore::kSignaturesCategory));
    for (const QString& category : categories) {
        if (category != QLatin1String(core::ProfileStore::kSignaturesCategory)) list_->addItem(category, category);
    }
    if (const int at = list_->findData(current); at >= 0) list_->setCurrentIndex(at);
    row->addWidget(list_, 1);
    new_ = new QPushButton(catalog::translate(catalog::key::StampsRefileNew), this);
    new_->setObjectName(QStringLiteral("stampCategoryNew"));
    row->addWidget(new_);
    column->addLayout(row);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(new_, &QPushButton::clicked, this, [this] { askNewCategory(); });
}

bool StampCategoryDialog::addCategory(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return false;
    // The reserved category is named by its shown word, in whatever language that is.
    const QString category = trimmed == catalog::translate(catalog::key::StampsCategorySignatures)
                                 ? QString::fromLatin1(core::ProfileStore::kSignaturesCategory)
                                 : trimmed;
    int at = list_->findData(category);
    if (at < 0) {
        list_->addItem(trimmed, category);
        at = list_->count() - 1;
    }
    list_->setCurrentIndex(at);
    return true;
}

QString StampCategoryDialog::chosenCategory() const
{
    return list_->currentData().toString();
}

void StampCategoryDialog::askNewCategory()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, catalog::translate(catalog::key::StampsRefile),
                                               catalog::translate(catalog::key::StampsRefileNewPrompt), QLineEdit::Normal,
                                               QString(), &accepted);
    if (accepted) (void)addCategory(name);
}

} // namespace rawpage::ui
