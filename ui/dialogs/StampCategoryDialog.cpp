// ui/dialogs/StampCategoryDialog -- implementation. Stage 6 guide I-40, I-42. See the header.

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

const QStringList& StampCategoryDialog::reservedNames()
{
    // ONE ROW PER SHIPPED CATALOG, its translations of `stamps.category.favourites`, `.standard`, `.dynamic` and
    // `.signatures` in that order. Compared, never shown. `tst_stamps`' [I-42] case reads the catalogs and fails
    // when a row and its catalog disagree, or a shipped catalog has no row.
    static const QStringList names{
        // en
        QStringLiteral("Favourites"), QStringLiteral("Standard"), QStringLiteral("Dynamic"), QStringLiteral("Signatures"),
        // zh_TW
        QStringLiteral("\u6211\u7684\u6700\u611B"), QStringLiteral("\u6A19\u6E96"), QStringLiteral("\u52D5\u614B"),
        QStringLiteral("\u7C3D\u540D"),
    };
    return names;
}

bool StampCategoryDialog::isReservedName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return false;
    QStringList shown = reservedNames();
    for (const QLatin1String key : {catalog::key::StampsCategoryFavourites, catalog::key::StampsCategoryStandard,
                                    catalog::key::StampsCategoryDynamic, catalog::key::StampsCategorySignatures})
        shown << catalog::translate(key);
    return shown.contains(trimmed, Qt::CaseInsensitive);
}

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

    refusal_ = new QLabel(this);
    refusal_->setObjectName(QStringLiteral("stampCategoryRefusal"));
    refusal_->setWordWrap(true);
    refusal_->hide();
    column->addWidget(refusal_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(new_, &QPushButton::clicked, this, [this] { askNewCategory(); });
}

bool StampCategoryDialog::addCategory(const QString& name)
{
    const QString trimmed = name.trimmed();
    refusal_->clear();
    refusal_->hide();
    if (trimmed.isEmpty()) return false;
    if (isReservedName(trimmed)) {
        refusal_->setText(catalog::translateError(QStringLiteral("error.stamps.reservedCategory"), trimmed));
        refusal_->show();
        return false;
    }
    int at = list_->findData(trimmed);
    if (at < 0) {
        list_->addItem(trimmed, trimmed);
        at = list_->count() - 1;
    }
    list_->setCurrentIndex(at);
    return true;
}

QString StampCategoryDialog::chosenCategory() const
{
    return list_->currentData().toString();
}

QString StampCategoryDialog::refusal() const
{
    return refusal_->text();
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
