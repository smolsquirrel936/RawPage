#include "ui/dialogs/CompareDialog.h"

#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

CompareDialog::Answer& installedAnswer()
{
    static CompareDialog::Answer answer;
    return answer;
}

// A path field: the open tabs' files to pick from, and any other path typed or browsed to.
QComboBox* pathField(const QStringList& openPaths, int current, QWidget* parent)
{
    auto* field = new QComboBox(parent);
    field->setEditable(true);
    field->setInsertPolicy(QComboBox::NoInsert);
    field->setMinimumContentsLength(48);
    field->addItems(openPaths);
    if (current >= 0 && current < openPaths.size()) field->setCurrentIndex(current);
    else field->setEditText(QString());
    return field;
}

} // namespace

CompareDialog::CompareDialog(const QStringList& openPaths, const core::text::CompareOptions& options, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("compareDialog"));
    setWindowTitle(catalog::translate(catalog::key::CompareDialogTitle));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);

    // THE OLDER DEFAULTS TO THE TAB IN FRONT, THE NEWER TO THE NEXT ONE (the header).
    auto* files = new QFormLayout();
    older_ = pathField(openPaths, 0, this);
    older_->setObjectName(QStringLiteral("compareOlder"));
    newer_ = pathField(openPaths, openPaths.size() > 1 ? 1 : -1, this);
    newer_->setObjectName(QStringLiteral("compareNewer"));
    for (QComboBox* field : {older_, newer_}) {
        auto* row = new QHBoxLayout();
        row->addWidget(field, 1);
        auto* browseButton = new QPushButton(catalog::translate(catalog::key::CompareDialogBrowse), this);
        connect(browseButton, &QPushButton::clicked, this, [this, field] { browse(field); });
        row->addWidget(browseButton);
        files->addRow(catalog::translate(field == older_ ? catalog::key::CompareDialogOlder : catalog::key::CompareDialogNewer),
                      row);
        connect(field, &QComboBox::editTextChanged, this, [this] { updateOk(); });
    }
    column->addLayout(files);

    auto* how = new QFormLayout();
    sensitivity_ = new QComboBox(this);
    sensitivity_->setObjectName(QStringLiteral("compareSensitivity"));
    sensitivity_->addItem(catalog::translate(catalog::key::CompareSensitivityTextOnly));
    sensitivity_->addItem(catalog::translate(catalog::key::CompareSensitivityTextAndLayout));
    sensitivity_->addItem(catalog::translate(catalog::key::CompareSensitivityAppearance));
    sensitivity_->setCurrentIndex(static_cast<int>(options.sensitivity));
    how->addRow(catalog::translate(catalog::key::CompareDialogSensitivity), sensitivity_);
    column->addLayout(how);

    auto* ignore = new QGroupBox(catalog::translate(catalog::key::CompareDialogIgnore), this);
    auto* ignoreColumn = new QVBoxLayout(ignore);
    ignoreHeaderFooter_ = new QCheckBox(catalog::translate(catalog::key::CompareIgnoreHeaderFooter), ignore);
    ignorePageNumbers_ = new QCheckBox(catalog::translate(catalog::key::CompareIgnorePageNumbers), ignore);
    ignoreWhitespace_ = new QCheckBox(catalog::translate(catalog::key::CompareIgnoreWhitespace), ignore);
    ignoreHeaderFooter_->setChecked(options.ignoreHeaderFooter);
    ignorePageNumbers_->setChecked(options.ignorePageNumbers);
    ignoreWhitespace_->setChecked(options.ignoreWhitespace);
    ignoreColumn->addWidget(ignoreHeaderFooter_);
    ignoreColumn->addWidget(ignorePageNumbers_);
    ignoreColumn->addWidget(ignoreWhitespace_);
    column->addWidget(ignore);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons_);
    updateOk();
}

QPushButton* CompareDialog::okButton() const
{
    return buttons_->button(QDialogButtonBox::Ok);
}

CompareDialog::Choice CompareDialog::choice() const
{
    Choice choice;
    choice.olderPath = older_->currentText().trimmed();
    choice.newerPath = newer_->currentText().trimmed();
    const int sensitivity = sensitivity_->currentIndex();
    choice.options.sensitivity = sensitivity == 2   ? core::text::CompareSensitivity::Appearance
                                 : sensitivity == 1 ? core::text::CompareSensitivity::TextAndLayout
                                                    : core::text::CompareSensitivity::TextOnly;
    choice.options.ignoreHeaderFooter = ignoreHeaderFooter_->isChecked();
    choice.options.ignorePageNumbers = ignorePageNumbers_->isChecked();
    choice.options.ignoreWhitespace = ignoreWhitespace_->isChecked();
    return choice;
}

void CompareDialog::browse(QComboBox* field)
{
    const QString start = QFileInfo(field->currentText()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, catalog::translate(catalog::key::CompareDialogChooseFile), start,
                                                      catalog::translate(catalog::key::CompareDialogFileFilter));
    if (!path.isEmpty()) field->setEditText(QDir::toNativeSeparators(path));
}

void CompareDialog::updateOk()
{
    // TWO FILES NAMED. Whether each is a PDF is the backend's to say when the window opens it (A-47's rule),
    // not this dialog's.
    if (QPushButton* ok = okButton())
        ok->setEnabled(!older_->currentText().trimmed().isEmpty() && !newer_->currentText().trimmed().isEmpty());
}

bool CompareDialog::canAsk()
{
    return static_cast<bool>(installedAnswer())
           || QGuiApplication::platformName() != QLatin1String("offscreen");
}

std::optional<CompareDialog::Choice> CompareDialog::ask(QWidget* parent, const QStringList& openPaths,
                                                        const core::text::CompareOptions& options)
{
    if (installedAnswer()) return installedAnswer()();
    if (!canAsk()) return std::nullopt;
    CompareDialog dialog(openPaths, options, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return dialog.choice();
}

void CompareDialog::setAnswerForTesting(Answer answer)
{
    installedAnswer() = std::move(answer);
}

} // namespace rawpage::ui
