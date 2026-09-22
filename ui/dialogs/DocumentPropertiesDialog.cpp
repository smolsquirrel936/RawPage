#include "ui/dialogs/DocumentPropertiesDialog.h"

#include "ui/shell/Catalog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

// The em dash an absent field shows. A CATALOG ENTRY rather than a literal, for the reason
// `StatusBar` records for the same character: CJK typography sets a dash differently, and "it is
// only punctuation" is how a Latin em dash ends up in the middle of a Chinese dialog.
QString absent()
{
    return catalog::translate(catalog::key::PropertiesAbsent);
}

QString orAbsent(const QString& text)
{
    return text.isEmpty() ? absent() : text;
}

// A stored PDF date, formatted where it parses and shown VERBATIM where it does not (guide case
// A-28's rule, at the surface it was written for). A date the product invented would be a fact
// about the document that the document does not contain.
QString formatDate(const QString& stored)
{
    if (stored.isEmpty()) return absent();
    const std::optional<QDateTime> parsed = core::Metadata::parseDate(stored);
    if (!parsed.has_value()) return stored;
    return QLocale().toString(*parsed, QLocale::ShortFormat);
}

} // namespace

DocumentPropertiesDialog::DocumentPropertiesDialog(const Properties& properties, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("documentPropertiesDialog"));
    setWindowTitle(catalog::translate(catalog::key::PropertiesTitle));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    addRow(form, Field::Title, catalog::key::PropertiesFieldTitle, orAbsent(properties.info.title));
    addRow(form, Field::Author, catalog::key::PropertiesFieldAuthor, orAbsent(properties.info.author));
    addRow(form, Field::Subject, catalog::key::PropertiesFieldSubject, orAbsent(properties.info.subject));
    addRow(form, Field::Keywords, catalog::key::PropertiesFieldKeywords, orAbsent(properties.info.keywords));
    addRow(form, Field::Creator, catalog::key::PropertiesFieldCreator, orAbsent(properties.info.creator));
    addRow(form, Field::Producer, catalog::key::PropertiesFieldProducer, orAbsent(properties.info.producer));
    addRow(form, Field::Created, catalog::key::PropertiesFieldCreated, formatDate(properties.info.creationDate));
    addRow(form, Field::Modified, catalog::key::PropertiesFieldModified, formatDate(properties.info.modificationDate));

    addRow(form, Field::Version, catalog::key::PropertiesFieldVersion, orAbsent(properties.info.pdfVersion));
    addRow(form, Field::Pages, catalog::key::PropertiesFieldPages,
           properties.pageCount > 0 ? QString::number(properties.pageCount) : absent());

    // THE CURRENT PAGE'S SIZE, not the document's: a PDF may carry a different size on every page,
    // and `corpus/ordinary/mixed-page-sizes.pdf` exists because it does. Which page it is, is said
    // on the row rather than assumed -- by its LABEL where the document has one, because a
    // document with roman front matter has two page 1s and the reader is looking at one of them.
    if (properties.pageCount > 0 && properties.currentPageSize.width > 0.0) {
        const QString page = properties.currentPageLabel.isEmpty()
                                 ? QString::number(properties.currentPage + 1)
                                 : properties.currentPageLabel;
        addRow(form, Field::PageSize, catalog::key::PropertiesFieldPageSize,
               catalog::translate(catalog::key::PropertiesPageSizeValue)
                   .arg(QString::number(properties.currentPageSize.width, 'f', 1),
                        QString::number(properties.currentPageSize.height, 'f', 1),
                        QString::number(properties.currentPageSize.width * 25.4 / 72.0, 'f', 0),
                        QString::number(properties.currentPageSize.height * 25.4 / 72.0, 'f', 0),
                        page));
    } else {
        addRow(form, Field::PageSize, catalog::key::PropertiesFieldPageSize, absent());
    }

    // **WHAT THIS ROW CLAIMS AND WHAT IT DOES NOT.** It says whether a password was asked for on
    // the way in, which is all the product knows today: the backend does not report encryption
    // separately (`ui/shell/DocumentWindow::isProtected`). It says nothing about WHAT the
    // protection permits -- printing, copying, editing -- because permission ENFORCEMENT is Stage
    // 10's and a dialog that listed permissions nothing enforces would be the product claiming a
    // capability it does not have.
    addRow(form, Field::Security, catalog::key::PropertiesFieldSecurity,
           catalog::translate(properties.encrypted ? catalog::key::PropertiesSecurityPassword
                                                   : catalog::key::PropertiesSecurityNone));

    addRow(form, Field::Repaired, catalog::key::PropertiesFieldRepaired,
           catalog::translate(properties.repaired ? catalog::key::PropertiesRepairedYes
                                                  : catalog::key::PropertiesRepairedNo));

    addRow(form, Field::Path, catalog::key::PropertiesFieldPath, orAbsent(properties.path));
    addRow(form, Field::FileSize, catalog::key::PropertiesFieldSize,
           properties.fileSize >= 0
               ? QLocale().formattedDataSize(properties.fileSize, 2, QLocale::DataSizeSIFormat)
               : absent());

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("documentPropertiesButtons"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

DocumentPropertiesDialog::~DocumentPropertiesDialog() = default;

void DocumentPropertiesDialog::addRow(QFormLayout* form, Field field, QLatin1String labelKey,
                                      const QString& text)
{
    auto* value = new QLabel(text, this);

    // SELECTABLE, NOT EDITABLE. A person reading a support thread has to be able to copy the
    // producer string and the path out of here; a `QLineEdit` would let them copy it too and would
    // also look like a field that ought to accept typing, which is the confusion D4-4 refuses.
    value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    value->setWordWrap(true);

    form->addRow(catalog::translate(labelKey), value);
    values_[field] = value;
}

QLabel* DocumentPropertiesDialog::valueLabel(Field field) const
{
    const auto found = values_.find(field);
    return found == values_.end() ? nullptr : found->second;
}

QString DocumentPropertiesDialog::value(Field field) const
{
    const QLabel* label = valueLabel(field);
    return label == nullptr ? QString() : label->text();
}

bool DocumentPropertiesDialog::canAsk()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

} // namespace rawpage::ui
