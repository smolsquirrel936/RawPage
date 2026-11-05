#include "ui/dialogs/PageOpsDialogs.h"

#include "core/doc/PageRanges.h"
#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace rawpage::ui {
namespace {

ExtractPagesDialog::Answer& installedExtract()
{
    static ExtractPagesDialog::Answer answer;
    return answer;
}

SplitDocumentDialog::Answer& installedSplit()
{
    static SplitDocumentDialog::Answer answer;
    return answer;
}

bool aPersonCanAnswer()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

// A folder field and its Browse button, in one row.
QWidget* folderRow(QWidget* parent, QLineEdit*& field)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    field = new QLineEdit(row);
    field->setObjectName(QStringLiteral("folderField"));
    auto* browse = new QPushButton(catalog::translate(catalog::key::AssemblyBrowse), row);
    QLineEdit* target = field;
    QObject::connect(browse, &QPushButton::clicked, row, [row, target] {
        const QString chosen = QFileDialog::getExistingDirectory(row, catalog::translate(catalog::key::AssemblyChooseFolder), target->text());
        if (!chosen.isEmpty()) target->setText(chosen);
    });
    layout->addWidget(field, 1);
    layout->addWidget(browse);
    return row;
}

QLabel* refusalLine(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setObjectName(QStringLiteral("refusal"));
    label->setWordWrap(true);
    label->hide();
    return label;
}

void showRefusal(QLabel* label, const core::Error& error)
{
    label->setText(catalog::translateError(QString(error.messageKey), error.context));
    label->show();
}

} // namespace

// --- Extract ---------------------------------------------------------------------------------------------------

ExtractPagesDialog::ExtractPagesDialog(int pageCount, int currentPage, bool mayDelete, const QString& folder, QWidget* parent)
    : QDialog(parent), pageCount_(pageCount)
{
    setObjectName(QStringLiteral("extractPagesDialog"));
    setWindowTitle(catalog::translate(catalog::key::ExtractTitle));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);

    auto* rangeRow = new QHBoxLayout();
    rangeRow->addWidget(new QLabel(catalog::translate(catalog::key::ExtractPages), this));
    range_ = new QLineEdit(QString::number(std::clamp(currentPage, 0, std::max(0, pageCount - 1)) + 1), this);
    range_->setObjectName(QStringLiteral("rangeField"));
    range_->setPlaceholderText(catalog::translate(catalog::key::ExtractPagesHint));
    rangeRow->addWidget(range_, 1);
    rangeRow->addWidget(new QLabel(catalog::translate(catalog::key::ExtractOfPages).arg(pageCount), this));
    column->addLayout(rangeRow);

    auto* form = new QGroupBox(catalog::translate(catalog::key::ExtractAs), this);
    auto* formColumn = new QVBoxLayout(form);
    one_ = new QRadioButton(catalog::translate(catalog::key::ExtractOneDocument), form);
    perPage_ = new QRadioButton(catalog::translate(catalog::key::ExtractFilePerPage), form);
    one_->setChecked(true);
    formColumn->addWidget(one_);
    formColumn->addWidget(perPage_);
    QWidget* folderLine = folderRow(form, folder_);
    folder_->setText(folder);
    folderLine->setEnabled(false);
    connect(perPage_, &QRadioButton::toggled, folderLine, &QWidget::setEnabled);
    formColumn->addWidget(folderLine);
    column->addWidget(form);

    delete_ = new QCheckBox(catalog::translate(catalog::key::ExtractDelete), this);
    delete_->setEnabled(mayDelete);
    column->addWidget(delete_);

    refusal_ = refusalLine(this);
    column->addWidget(refusal_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
}

core::Result<ExtractPagesDialog::Choice> ExtractPagesDialog::choice() const
{
    core::Result<std::vector<int>> pages = core::parsePageRanges(range_->text(), pageCount_);
    if (!pages) return pages.error();
    Choice choice;
    choice.pages = std::move(pages).value();
    choice.filePerPage = perPage_->isChecked();
    choice.deleteAfter = delete_->isEnabled() && delete_->isChecked();
    choice.folder = folder_->text().trimmed();
    if (choice.filePerPage && choice.folder.isEmpty())
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.assembly.noFolder"), QString()};
    return choice;
}

void ExtractPagesDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        showRefusal(refusal_, chosen.error());
        return;
    }
    QDialog::accept();
}

bool ExtractPagesDialog::canAsk()
{
    return static_cast<bool>(installedExtract()) || aPersonCanAnswer();
}

std::optional<ExtractPagesDialog::Choice> ExtractPagesDialog::ask(QWidget* parent, int pageCount, int currentPage, bool mayDelete,
                                                                 const QString& folder)
{
    if (installedExtract()) return installedExtract()(pageCount, currentPage);
    if (!aPersonCanAnswer()) return std::nullopt;
    ExtractPagesDialog dialog(pageCount, currentPage, mayDelete, folder, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void ExtractPagesDialog::setAnswerForTesting(Answer answer)
{
    installedExtract() = std::move(answer);
}

// --- Split -----------------------------------------------------------------------------------------------------

SplitDocumentDialog::SplitDocumentDialog(int pageCount, int outlineDepth, const QString& folder, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("splitDocumentDialog"));
    setWindowTitle(catalog::translate(catalog::key::SplitTitle));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);

    auto* rules = new QGroupBox(catalog::translate(catalog::key::SplitRule), this);
    auto* grid = new QGridLayout(rules);
    const auto addMethod = [&](core::SplitMethod method, const QString& text, QWidget* field, int row) {
        auto* button = new QRadioButton(text, rules);
        methods_.emplace_back(method, button);
        grid->addWidget(button, row, 0);
        if (field != nullptr) {
            grid->addWidget(field, row, 1);
            field->setEnabled(false);
            connect(button, &QRadioButton::toggled, field, &QWidget::setEnabled);
        }
        return button;
    };
    pages_ = new QSpinBox(rules);
    pages_->setRange(1, std::max(1, pageCount));
    pages_->setValue(1);
    level_ = new QSpinBox(rules);
    level_->setRange(1, std::max(1, outlineDepth));
    level_->setValue(std::min(2, std::max(1, outlineDepth)));
    size_ = new QLineEdit(QLocale().toString(10.0, 'g', 6), rules);
    auto* patternBox = new QWidget(rules);
    {
        auto* patternColumn = new QVBoxLayout(patternBox);
        patternColumn->setContentsMargins(0, 0, 0, 0);
        pattern_ = new QLineEdit(patternBox);
        regex_ = new QCheckBox(catalog::translate(catalog::key::SplitRegex), patternBox);
        case_ = new QCheckBox(catalog::translate(catalog::key::SplitCaseSensitive), patternBox);
        patternColumn->addWidget(pattern_);
        patternColumn->addWidget(regex_);
        patternColumn->addWidget(case_);
    }
    QRadioButton* first = addMethod(core::SplitMethod::EveryN, catalog::translate(catalog::key::SplitEveryN), pages_, 0);
    QRadioButton* topLevel = addMethod(core::SplitMethod::TopLevelBookmarks, catalog::translate(catalog::key::SplitTopLevel), nullptr, 1);
    QRadioButton* level = addMethod(core::SplitMethod::BookmarkLevel, catalog::translate(catalog::key::SplitLevel), level_, 2);
    addMethod(core::SplitMethod::MaxSize, catalog::translate(catalog::key::SplitSize), size_, 3);
    addMethod(core::SplitMethod::BlankPages, catalog::translate(catalog::key::SplitBlank), nullptr, 4);
    addMethod(core::SplitMethod::TextPattern, catalog::translate(catalog::key::SplitPattern), patternBox, 5);
    // A document with no outline has no bookmark to split at: the two rules say so by being unavailable.
    topLevel->setEnabled(outlineDepth > 0);
    level->setEnabled(outlineDepth > 0);
    first->setChecked(true);
    pages_->setEnabled(true);
    column->addWidget(rules);

    auto* folderLine = new QHBoxLayout();
    folderLine->addWidget(new QLabel(catalog::translate(catalog::key::AssemblyFolder), this));
    folderLine->addWidget(folderRow(this, folder_), 1);
    folder_->setText(folder);
    column->addLayout(folderLine);

    refusal_ = refusalLine(this);
    column->addWidget(refusal_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
}

QRadioButton* SplitDocumentDialog::methodButton(core::SplitMethod method) const
{
    for (const auto& [kind, button] : methods_) {
        if (kind == method) return button;
    }
    return nullptr;
}

core::Result<SplitDocumentDialog::Choice> SplitDocumentDialog::choice() const
{
    Choice choice;
    for (const auto& [kind, button] : methods_) {
        if (button->isChecked()) choice.method = kind;
    }
    choice.pagesPerFile = pages_->value();
    choice.level = level_->value();
    bool ok = false;
    choice.maxMegabytes = QLocale().toDouble(size_->text().trimmed(), &ok);
    if (choice.method == core::SplitMethod::MaxSize && (!ok || !(choice.maxMegabytes > 0.0)))
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.split.badSize"), size_->text().trimmed()};
    choice.pattern = pattern_->text();
    choice.regex = regex_->isChecked();
    choice.caseSensitive = case_->isChecked();
    if (choice.method == core::SplitMethod::TextPattern && choice.pattern.trimmed().isEmpty())
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.search.emptyPattern"), QString()};
    choice.folder = folder_->text().trimmed();
    if (choice.folder.isEmpty()) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.assembly.noFolder"), QString()};
    return choice;
}

void SplitDocumentDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        showRefusal(refusal_, chosen.error());
        return;
    }
    QDialog::accept();
}

bool SplitDocumentDialog::canAsk()
{
    return static_cast<bool>(installedSplit()) || aPersonCanAnswer();
}

std::optional<SplitDocumentDialog::Choice> SplitDocumentDialog::ask(QWidget* parent, int pageCount, int outlineDepth,
                                                                   const QString& folder)
{
    if (installedSplit()) return installedSplit()(pageCount);
    if (!aPersonCanAnswer()) return std::nullopt;
    SplitDocumentDialog dialog(pageCount, outlineDepth, folder, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void SplitDocumentDialog::setAnswerForTesting(Answer answer)
{
    installedSplit() = std::move(answer);
}

} // namespace rawpage::ui
