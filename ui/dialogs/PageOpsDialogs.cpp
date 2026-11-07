#include "ui/dialogs/PageOpsDialogs.h"

#include "core/cmd/commands/PageCommands.h"
#include "core/doc/PageRanges.h"
#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
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

// What a range field starts with: the pages a person picked, kept inside the document, or page 1 (S7 T8).
QString startingRange(const std::vector<int>& initial, int pageCount)
{
    std::vector<int> inside;
    for (const int page : initial)
        if (page >= 0 && page < pageCount) inside.push_back(page);
    if (inside.empty()) inside.push_back(0);
    return core::formatPageRanges(inside);
}

// A PDF field and its Browse button, in one row (S7 T8: insert's and replace's source).
QWidget* pdfRow(QWidget* parent, QLineEdit*& field)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    field = new QLineEdit(row);
    field->setObjectName(QStringLiteral("fileField"));
    auto* browse = new QPushButton(catalog::translate(catalog::key::AssemblyBrowse), row);
    QLineEdit* target = field;
    QObject::connect(browse, &QPushButton::clicked, row, [row, target] {
        const QString chosen = QFileDialog::getOpenFileName(row, catalog::translate(catalog::key::PagesChooseFile), target->text(),
                                                            catalog::translate(catalog::key::DialogPdfFilter));
        if (!chosen.isEmpty()) target->setText(chosen);
    });
    layout->addWidget(field, 1);
    layout->addWidget(browse);
    return row;
}

InsertPagesDialog::Answer& installedInsert()
{
    static InsertPagesDialog::Answer answer;
    return answer;
}

DeletePagesDialog::Answer& installedDelete()
{
    static DeletePagesDialog::Answer answer;
    return answer;
}

ReplacePagesDialog::Answer& installedReplace()
{
    static ReplacePagesDialog::Answer answer;
    return answer;
}

QDialogButtonBox* okCancel(QDialog* dialog)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

} // namespace

// --- Extract ---------------------------------------------------------------------------------------------------

ExtractPagesDialog::ExtractPagesDialog(int pageCount, const std::vector<int>& initial, bool mayDelete, const QString& folder,
                                       QWidget* parent)
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
    range_ = new QLineEdit(startingRange(initial, pageCount), this);
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

std::optional<ExtractPagesDialog::Choice> ExtractPagesDialog::ask(QWidget* parent, int pageCount, const std::vector<int>& initial,
                                                                 bool mayDelete, const QString& folder)
{
    if (installedExtract()) return installedExtract()(pageCount, initial);
    if (!aPersonCanAnswer()) return std::nullopt;
    ExtractPagesDialog dialog(pageCount, initial, mayDelete, folder, parent);
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


// --- Insert (S7 T8) --------------------------------------------------------------------------------------------

InsertPagesDialog::InsertPagesDialog(int pageCount, int page, core::PaperSize paperSize, int clipboardPages, QWidget* parent)
    : QDialog(parent), pageCount_(pageCount)
{
    setObjectName(QStringLiteral("insertPagesDialog"));
    setWindowTitle(catalog::translate(catalog::key::PagesInsertTitle));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);

    auto* from = new QGroupBox(catalog::translate(catalog::key::PagesInsertFrom), this);
    auto* grid = new QGridLayout(from);
    const auto addSource = [&](Source source, const QString& text, int row) {
        auto* button = new QRadioButton(text, from);
        sources_.emplace_back(source, button);
        grid->addWidget(button, row, 0, 1, 2);
        return button;
    };

    QRadioButton* file = addSource(Source::File, catalog::translate(catalog::key::PagesInsertFromFile), 0);
    QWidget* fileLine = pdfRow(from, file_);
    grid->addWidget(fileLine, 1, 0, 1, 2);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::ExtractPages), from), 2, 0);
    fileRange_ = new QLineEdit(from);
    fileRange_->setObjectName(QStringLiteral("fileRangeField"));
    fileRange_->setPlaceholderText(catalog::translate(catalog::key::PagesInsertAllPages));
    grid->addWidget(fileRange_, 2, 1);

    QRadioButton* blank = addSource(Source::Blank, catalog::translate(catalog::key::PagesInsertBlank), 3);
    auto* blankLine = new QWidget(from);
    auto* blankLayout = new QHBoxLayout(blankLine);
    blankLayout->setContentsMargins(0, 0, 0, 0);
    blankCount_ = new QSpinBox(blankLine);
    blankCount_->setObjectName(QStringLiteral("blankCountField"));
    blankCount_->setRange(1, 1000);
    blankSize_ = new QComboBox(blankLine);
    blankSize_->setObjectName(QStringLiteral("blankSizeBox"));
    for (const core::PaperSize size : core::kPaperSizes) {
        const std::string_view name = core::paperSizeName(size);
        // THE SETTING'S OWN WORDS for each size: the Preferences page and this box cannot name one size two ways.
        blankSize_->addItem(catalog::translate(QStringLiteral("prefs.choice.paperSize.") + QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()))),
                            static_cast<int>(size));
        if (size == paperSize) blankSize_->setCurrentIndex(blankSize_->count() - 1);
    }
    landscape_ = new QCheckBox(catalog::translate(catalog::key::PagesInsertLandscape), blankLine);
    blankLayout->addWidget(blankCount_);
    blankLayout->addWidget(blankSize_, 1);
    blankLayout->addWidget(landscape_);
    grid->addWidget(blankLine, 4, 0, 1, 2);

    QRadioButton* clipboard = addSource(Source::Clipboard,
                                        catalog::translate(catalog::key::PagesInsertClipboard).arg(std::max(0, clipboardPages)), 5);
    clipboard->setEnabled(clipboardPages > 0);

    const auto enableFor = [](QRadioButton* button, std::vector<QWidget*> fields) {
        for (QWidget* field : fields) field->setEnabled(false);
        QObject::connect(button, &QRadioButton::toggled, button, [fields](bool on) {
            for (QWidget* field : fields) field->setEnabled(on);
        });
    };
    enableFor(file, {fileLine, fileRange_});
    enableFor(blank, {blankLine});
    file->setChecked(true);
    column->addWidget(from);

    auto* whereRow = new QHBoxLayout();
    where_ = new QComboBox(this);
    where_->setObjectName(QStringLiteral("whereBox"));
    where_->addItem(catalog::translate(catalog::key::PagesInsertAfter), static_cast<int>(Where::After));
    where_->addItem(catalog::translate(catalog::key::PagesInsertBefore), static_cast<int>(Where::Before));
    where_->addItem(catalog::translate(catalog::key::PagesInsertFirst), static_cast<int>(Where::First));
    where_->addItem(catalog::translate(catalog::key::PagesInsertLast), static_cast<int>(Where::Last));
    page_ = new QSpinBox(this);
    page_->setObjectName(QStringLiteral("pageField"));
    page_->setRange(1, std::max(1, pageCount));
    page_->setValue(std::clamp(page + 1, 1, std::max(1, pageCount)));
    connect(where_, &QComboBox::currentIndexChanged, page_, [this] {
        const auto where = static_cast<Where>(where_->currentData().toInt());
        page_->setEnabled(where == Where::Before || where == Where::After);
    });
    whereRow->addWidget(new QLabel(catalog::translate(catalog::key::PagesInsertWhere), this));
    whereRow->addWidget(where_, 1);
    whereRow->addWidget(page_);
    whereRow->addWidget(new QLabel(catalog::translate(catalog::key::ExtractOfPages).arg(pageCount), this));
    column->addLayout(whereRow);

    refusal_ = refusalLine(this);
    column->addWidget(refusal_);
    column->addWidget(okCancel(this));
}

QRadioButton* InsertPagesDialog::sourceButton(Source source) const
{
    for (const auto& [kind, button] : sources_)
        if (kind == source) return button;
    return nullptr;
}

core::Result<InsertPagesDialog::Choice> InsertPagesDialog::choice() const
{
    Choice choice;
    for (const auto& [kind, button] : sources_)
        if (button->isChecked()) choice.source = kind;
    switch (static_cast<Where>(where_->currentData().toInt())) {
    case Where::Before: choice.at = page_->value() - 1; break;
    case Where::After:  choice.at = page_->value(); break;
    case Where::First:  choice.at = 0; break;
    case Where::Last:   choice.at = pageCount_; break;
    }
    choice.at = std::clamp(choice.at, 0, std::max(0, pageCount_));
    switch (choice.source) {
    case Source::File:
        choice.path = file_->text().trimmed();
        choice.range = fileRange_->text().trimmed();
        if (choice.path.isEmpty())
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pages.noFile"), QString()};
        break;
    case Source::Blank: {
        choice.blankCount = blankCount_->value();
        const core::PdfSize size = core::paperSizePoints(static_cast<core::PaperSize>(blankSize_->currentData().toInt()));
        choice.blankSize = landscape_->isChecked() ? core::PdfSize{size.height, size.width} : size;
        break;
    }
    case Source::Clipboard:
        break;
    }
    return choice;
}

void InsertPagesDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        showRefusal(refusal_, chosen.error());
        return;
    }
    QDialog::accept();
}

bool InsertPagesDialog::canAsk()
{
    return static_cast<bool>(installedInsert()) || aPersonCanAnswer();
}

std::optional<InsertPagesDialog::Choice> InsertPagesDialog::ask(QWidget* parent, int pageCount, int page, core::PaperSize paperSize,
                                                               int clipboardPages)
{
    if (installedInsert()) return installedInsert()(pageCount, page, paperSize, clipboardPages);
    if (!aPersonCanAnswer()) return std::nullopt;
    InsertPagesDialog dialog(pageCount, page, paperSize, clipboardPages, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void InsertPagesDialog::setAnswerForTesting(Answer answer)
{
    installedInsert() = std::move(answer);
}

// --- Delete (S7 T8) --------------------------------------------------------------------------------------------

DeletePagesDialog::DeletePagesDialog(int pageCount, const std::vector<int>& initial, QWidget* parent)
    : QDialog(parent), pageCount_(pageCount)
{
    setObjectName(QStringLiteral("deletePagesDialog"));
    setWindowTitle(catalog::translate(catalog::key::PagesDeleteTitle));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);
    auto* rangeRow = new QHBoxLayout();
    rangeRow->addWidget(new QLabel(catalog::translate(catalog::key::ExtractPages), this));
    range_ = new QLineEdit(startingRange(initial, pageCount), this);
    range_->setObjectName(QStringLiteral("rangeField"));
    range_->setPlaceholderText(catalog::translate(catalog::key::ExtractPagesHint));
    rangeRow->addWidget(range_, 1);
    rangeRow->addWidget(new QLabel(catalog::translate(catalog::key::ExtractOfPages).arg(pageCount), this));
    column->addLayout(rangeRow);
    refusal_ = refusalLine(this);
    column->addWidget(refusal_);
    column->addWidget(okCancel(this));
}

core::Result<DeletePagesDialog::Choice> DeletePagesDialog::choice() const
{
    core::Result<std::vector<int>> pages = core::parsePageRanges(range_->text(), pageCount_);
    if (!pages) return pages.error();
    if (static_cast<int>(pages.value().size()) >= pageCount_)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.edit.noPagesLeft"), QString()};
    return Choice{std::move(pages).value()};
}

void DeletePagesDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        showRefusal(refusal_, chosen.error());
        return;
    }
    QDialog::accept();
}

bool DeletePagesDialog::canAsk()
{
    return static_cast<bool>(installedDelete()) || aPersonCanAnswer();
}

std::optional<DeletePagesDialog::Choice> DeletePagesDialog::ask(QWidget* parent, int pageCount, const std::vector<int>& initial)
{
    if (installedDelete()) return installedDelete()(pageCount, initial);
    if (!aPersonCanAnswer()) return std::nullopt;
    DeletePagesDialog dialog(pageCount, initial, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void DeletePagesDialog::setAnswerForTesting(Answer answer)
{
    installedDelete() = std::move(answer);
}

// --- Replace (S7 T8) -------------------------------------------------------------------------------------------

ReplacePagesDialog::ReplacePagesDialog(int pageCount, const std::vector<int>& initial, QWidget* parent)
    : QDialog(parent), pageCount_(pageCount)
{
    setObjectName(QStringLiteral("replacePagesDialog"));
    setWindowTitle(catalog::translate(catalog::key::PagesReplaceTitle));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);
    auto* grid = new QGridLayout();
    grid->addWidget(new QLabel(catalog::translate(catalog::key::PagesReplacePages), this), 0, 0);
    range_ = new QLineEdit(startingRange(initial, pageCount), this);
    range_->setObjectName(QStringLiteral("rangeField"));
    range_->setPlaceholderText(catalog::translate(catalog::key::ExtractPagesHint));
    grid->addWidget(range_, 0, 1);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::ExtractOfPages).arg(pageCount), this), 0, 2);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::PagesReplaceWith), this), 1, 0);
    grid->addWidget(pdfRow(this, file_), 1, 1, 1, 2);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::PagesReplaceStartingAt), this), 2, 0);
    sourceFirst_ = new QSpinBox(this);
    sourceFirst_->setObjectName(QStringLiteral("sourceFirstField"));
    sourceFirst_->setRange(1, core::kMaxPagesPerCommand);
    grid->addWidget(sourceFirst_, 2, 1);
    column->addLayout(grid);
    refusal_ = refusalLine(this);
    column->addWidget(refusal_);
    column->addWidget(okCancel(this));
}

core::Result<ReplacePagesDialog::Choice> ReplacePagesDialog::choice() const
{
    core::Result<std::vector<int>> pages = core::parsePageRanges(range_->text(), pageCount_);
    if (!pages) return pages.error();
    Choice choice;
    choice.pages = std::move(pages).value();
    choice.path = file_->text().trimmed();
    if (choice.path.isEmpty()) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pages.noFile"), QString()};
    choice.sourceFirst = sourceFirst_->value() - 1;
    return choice;
}

void ReplacePagesDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        showRefusal(refusal_, chosen.error());
        return;
    }
    QDialog::accept();
}

bool ReplacePagesDialog::canAsk()
{
    return static_cast<bool>(installedReplace()) || aPersonCanAnswer();
}

std::optional<ReplacePagesDialog::Choice> ReplacePagesDialog::ask(QWidget* parent, int pageCount, const std::vector<int>& initial)
{
    if (installedReplace()) return installedReplace()(pageCount, initial);
    if (!aPersonCanAnswer()) return std::nullopt;
    ReplacePagesDialog dialog(pageCount, initial, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void ReplacePagesDialog::setAnswerForTesting(Answer answer)
{
    installedReplace() = std::move(answer);
}

// --- Crop / page boxes (S7 T9) ---------------------------------------------------------------------------------

namespace {

CropPagesDialog::Answer& installedCrop()
{
    static CropPagesDialog::Answer answer;
    return answer;
}

ResizePagesDialog::Answer& installedResize()
{
    static ResizePagesDialog::Answer answer;
    return answer;
}

RenumberPagesDialog::Answer& installedRenumber()
{
    static RenumberPagesDialog::Answer answer;
    return answer;
}

QDoubleSpinBox* pointsField(QWidget* parent, double value, double low, double high, const QString& name)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setObjectName(name);
    field->setDecimals(2);
    field->setRange(low, high);
    field->setValue(value);
    field->setSuffix(catalog::translate(catalog::key::PagesPointsSuffix));
    return field;
}

// A range field and its "of n" -- what every T8 and T9 dialog starts with.
QLineEdit* rangeRow(QWidget* parent, QVBoxLayout* column, const std::vector<int>& initial, int pageCount)
{
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(catalog::translate(catalog::key::ExtractPages), parent));
    auto* range = new QLineEdit(startingRange(initial, pageCount), parent);
    range->setObjectName(QStringLiteral("rangeField"));
    range->setPlaceholderText(catalog::translate(catalog::key::ExtractPagesHint));
    row->addWidget(range, 1);
    row->addWidget(new QLabel(catalog::translate(catalog::key::ExtractOfPages).arg(pageCount), parent));
    column->addLayout(row);
    return range;
}

} // namespace

CropPagesDialog::CropPagesDialog(int pageCount, const std::vector<int>& initial, std::array<double, 4> margins, double padding,
                                 QWidget* parent)
    : QDialog(parent), pageCount_(pageCount)
{
    setObjectName(QStringLiteral("cropPagesDialog"));
    setWindowTitle(catalog::translate(catalog::key::PagesCropTitle));
    setModal(true);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);

    range_ = rangeRow(this, column, initial, pageCount);
    sameSize_ = new QCheckBox(catalog::translate(catalog::key::PagesCropSameSize), this);
    sameSize_->setObjectName(QStringLiteral("sameSizeBox"));
    column->addWidget(sameSize_);

    auto* kindRow = new QHBoxLayout();
    kindRow->addWidget(new QLabel(catalog::translate(catalog::key::PagesCropBox), this));
    kind_ = new QComboBox(this);
    kind_->setObjectName(QStringLiteral("boxKindBox"));
    for (const auto& [kind, name] : {std::pair{core::PageBoxKind::Crop, "crop"}, std::pair{core::PageBoxKind::Trim, "trim"},
                                     std::pair{core::PageBoxKind::Bleed, "bleed"}, std::pair{core::PageBoxKind::Art, "art"},
                                     std::pair{core::PageBoxKind::Media, "media"}})
        kind_->addItem(catalog::translate(QStringLiteral("pages.crop.box.") + QLatin1String(name)), static_cast<int>(kind));
    kindRow->addWidget(kind_, 1);
    column->addLayout(kindRow);

    auto* how = new QGroupBox(this);
    auto* grid = new QGridLayout(how);
    const auto addHow = [&](How which, const QString& text, int row) {
        auto* button = new QRadioButton(text, how);
        hows_.emplace_back(which, button);
        grid->addWidget(button, row, 0, 1, 4);
        return button;
    };
    QRadioButton* byMargins = addHow(How::Margins, catalog::translate(catalog::key::PagesCropMargins), 0);
    const char* const edges[] = {"left", "bottom", "right", "top"};
    for (int edge = 0; edge < 4; ++edge) {
        grid->addWidget(new QLabel(catalog::translate(QStringLiteral("pages.crop.edge.") + QLatin1String(edges[edge])), how), 1 + edge / 2,
                        (edge % 2) * 2);
        margins_[static_cast<std::size_t>(edge)] =
            pointsField(how, margins[static_cast<std::size_t>(edge)], -14400.0, 14400.0, QStringLiteral("marginField%1").arg(edge));
        grid->addWidget(margins_[static_cast<std::size_t>(edge)], 1 + edge / 2, (edge % 2) * 2 + 1);
    }
    QRadioButton* white = addHow(How::WhiteMargins, catalog::translate(catalog::key::PagesCropWhiteMargins), 3);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::PagesCropPadding), how), 4, 0);
    padding_ = pointsField(how, padding, 0.0, 72.0, QStringLiteral("paddingField"));
    grid->addWidget(padding_, 4, 1);
    addHow(How::Remove, catalog::translate(catalog::key::PagesCropRemove), 5);
    byMargins->setChecked(true);
    const auto follow = [this](QRadioButton* button, std::vector<QWidget*> fields) {
        for (QWidget* field : fields) field->setEnabled(button->isChecked());
        QObject::connect(button, &QRadioButton::toggled, this, [fields](bool on) {
            for (QWidget* field : fields) field->setEnabled(on);
        });
    };
    follow(byMargins, {margins_[0], margins_[1], margins_[2], margins_[3]});
    follow(white, {padding_});
    column->addWidget(how);

    refusal_ = refusalLine(this);
    column->addWidget(refusal_);
    column->addWidget(okCancel(this));
}

QRadioButton* CropPagesDialog::howButton(How how) const
{
    for (const auto& [which, button] : hows_)
        if (which == how) return button;
    return nullptr;
}

core::Result<CropPagesDialog::Choice> CropPagesDialog::choice() const
{
    core::Result<std::vector<int>> pages = core::parsePageRanges(range_->text(), pageCount_);
    if (!pages) return pages.error();
    Choice out;
    out.pages = std::move(pages).value();
    out.sameSize = sameSize_->isChecked();
    out.box = static_cast<core::PageBoxKind>(kind_->currentData().toInt());
    for (const auto& [which, button] : hows_)
        if (button->isChecked()) out.how = which;
    out.left = margins_[0]->value();
    out.bottom = margins_[1]->value();
    out.right = margins_[2]->value();
    out.top = margins_[3]->value();
    out.padding = padding_->value();
    if (out.how == How::Remove && out.box == core::PageBoxKind::Media)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pages.mediaBoxRequired"), QString()};
    return out;
}

void CropPagesDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        showRefusal(refusal_, chosen.error());
        return;
    }
    QDialog::accept();
}

bool CropPagesDialog::canAsk()
{
    return static_cast<bool>(installedCrop()) || aPersonCanAnswer();
}

std::optional<CropPagesDialog::Choice> CropPagesDialog::ask(QWidget* parent, int pageCount, const std::vector<int>& initial,
                                                            std::array<double, 4> margins, double padding)
{
    if (installedCrop()) return installedCrop()(pageCount, initial, margins, padding);
    if (!aPersonCanAnswer()) return std::nullopt;
    CropPagesDialog dialog(pageCount, initial, margins, padding, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void CropPagesDialog::setAnswerForTesting(Answer answer)
{
    installedCrop() = std::move(answer);
}

// --- Resize (S7 T9) --------------------------------------------------------------------------------------------

ResizePagesDialog::ResizePagesDialog(int pageCount, const std::vector<int>& initial, core::PaperSize paperSize, QWidget* parent)
    : QDialog(parent), pageCount_(pageCount)
{
    setObjectName(QStringLiteral("resizePagesDialog"));
    setWindowTitle(catalog::translate(catalog::key::PagesResizeTitle));
    setModal(true);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);
    range_ = rangeRow(this, column, initial, pageCount);

    auto* grid = new QGridLayout();
    grid->addWidget(new QLabel(catalog::translate(catalog::key::PagesResizeSize), this), 0, 0);
    size_ = new QComboBox(this);
    size_->setObjectName(QStringLiteral("sizeBox"));
    for (const core::PaperSize size : core::kPaperSizes) {
        const std::string_view name = core::paperSizeName(size);
        size_->addItem(catalog::translate(QStringLiteral("prefs.choice.paperSize.") + QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()))),
                       static_cast<int>(size));
    }
    size_->addItem(catalog::translate(catalog::key::PagesResizeCustom), -1);
    grid->addWidget(size_, 0, 1, 1, 3);
    const core::PdfSize start = core::paperSizePoints(paperSize);
    width_ = pointsField(this, start.width, 3.0, 14400.0, QStringLiteral("widthField"));
    height_ = pointsField(this, start.height, 3.0, 14400.0, QStringLiteral("heightField"));
    grid->addWidget(new QLabel(catalog::translate(catalog::key::PagesResizeWidth), this), 1, 0);
    grid->addWidget(width_, 1, 1);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::PagesResizeHeight), this), 1, 2);
    grid->addWidget(height_, 1, 3);
    landscape_ = new QCheckBox(catalog::translate(catalog::key::PagesInsertLandscape), this);
    landscape_->setObjectName(QStringLiteral("landscapeBox"));
    grid->addWidget(landscape_, 2, 0, 1, 4);
    column->addLayout(grid);
    // A NAMED SIZE FILLS THE FIELDS, turned by the landscape box; Custom leaves them to be typed.
    const auto fill = [this] {
        const int named = size_->currentData().toInt();
        width_->setEnabled(named < 0);
        height_->setEnabled(named < 0);
        landscape_->setEnabled(named >= 0);
        if (named < 0) return;
        const core::PdfSize points = core::paperSizePoints(static_cast<core::PaperSize>(named));
        width_->setValue(landscape_->isChecked() ? points.height : points.width);
        height_->setValue(landscape_->isChecked() ? points.width : points.height);
    };
    connect(size_, &QComboBox::currentIndexChanged, this, fill);
    connect(landscape_, &QCheckBox::toggled, this, fill);
    size_->setCurrentIndex(size_->findData(static_cast<int>(paperSize)));
    fill();

    scale_ = new QCheckBox(catalog::translate(catalog::key::PagesResizeScale), this);
    scale_->setObjectName(QStringLiteral("scaleBox"));
    scale_->setChecked(true);
    column->addWidget(scale_);
    auto* anchorRow = new QHBoxLayout();
    anchorRow->addWidget(new QLabel(catalog::translate(catalog::key::PagesResizeAnchor), this));
    anchor_ = new QComboBox(this);
    anchor_->setObjectName(QStringLiteral("anchorBox"));
    const char* const anchors[] = {"topLeft", "top", "topRight", "left", "centre", "right", "bottomLeft", "bottom", "bottomRight"};
    for (int at = 0; at < 9; ++at) anchor_->addItem(catalog::translate(QStringLiteral("pages.anchor.") + QLatin1String(anchors[at])), at);
    anchor_->setCurrentIndex(static_cast<int>(core::ResizeAnchor::Centre));
    anchorRow->addWidget(anchor_, 1);
    column->addLayout(anchorRow);

    refusal_ = refusalLine(this);
    column->addWidget(refusal_);
    column->addWidget(okCancel(this));
}

core::Result<ResizePagesDialog::Choice> ResizePagesDialog::choice() const
{
    core::Result<std::vector<int>> pages = core::parsePageRanges(range_->text(), pageCount_);
    if (!pages) return pages.error();
    Choice out;
    out.pages = std::move(pages).value();
    out.size = core::PdfSize{width_->value(), height_->value()};
    out.scale = scale_->isChecked();
    out.anchor = static_cast<core::ResizeAnchor>(anchor_->currentData().toInt());
    if (out.size.width < core::kMinPageBoxPoints || out.size.height < core::kMinPageBoxPoints || out.size.width > 14400.0
        || out.size.height > 14400.0)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pages.badSize"), QString()};
    return out;
}

void ResizePagesDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        showRefusal(refusal_, chosen.error());
        return;
    }
    QDialog::accept();
}

bool ResizePagesDialog::canAsk()
{
    return static_cast<bool>(installedResize()) || aPersonCanAnswer();
}

std::optional<ResizePagesDialog::Choice> ResizePagesDialog::ask(QWidget* parent, int pageCount, const std::vector<int>& initial,
                                                                core::PaperSize paperSize)
{
    if (installedResize()) return installedResize()(pageCount, initial);
    if (!aPersonCanAnswer()) return std::nullopt;
    ResizePagesDialog dialog(pageCount, initial, paperSize, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void ResizePagesDialog::setAnswerForTesting(Answer answer)
{
    installedResize() = std::move(answer);
}

// --- Renumber (S7 T9) ------------------------------------------------------------------------------------------

RenumberPagesDialog::RenumberPagesDialog(int pageCount, const std::vector<int>& initial, QWidget* parent)
    : QDialog(parent), pageCount_(pageCount)
{
    setObjectName(QStringLiteral("renumberPagesDialog"));
    setWindowTitle(catalog::translate(catalog::key::PagesRenumberTitle));
    setModal(true);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);
    range_ = rangeRow(this, column, initial, pageCount);

    auto* grid = new QGridLayout();
    grid->addWidget(new QLabel(catalog::translate(catalog::key::PagesRenumberStyle), this), 0, 0);
    style_ = new QComboBox(this);
    style_->setObjectName(QStringLiteral("styleBox"));
    for (const auto& [style, name] : {std::pair{core::PageLabelStyle::Decimal, "decimal"}, std::pair{core::PageLabelStyle::RomanUpper, "romanUpper"},
                                      std::pair{core::PageLabelStyle::RomanLower, "romanLower"}, std::pair{core::PageLabelStyle::LettersUpper, "lettersUpper"},
                                      std::pair{core::PageLabelStyle::LettersLower, "lettersLower"}, std::pair{core::PageLabelStyle::None, "none"}})
        style_->addItem(catalog::translate(QStringLiteral("pages.renumber.style.") + QLatin1String(name)), static_cast<int>(style));
    grid->addWidget(style_, 0, 1);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::PagesRenumberPrefix), this), 1, 0);
    prefix_ = new QLineEdit(this);
    prefix_->setObjectName(QStringLiteral("prefixField"));
    prefix_->setMaxLength(core::RenumberPagesCommand::kMaxPrefix);
    grid->addWidget(prefix_, 1, 1);
    grid->addWidget(new QLabel(catalog::translate(catalog::key::PagesRenumberStart), this), 2, 0);
    start_ = new QSpinBox(this);
    start_->setObjectName(QStringLiteral("startField"));
    start_->setRange(1, core::RenumberPagesCommand::kMaxStart - pageCount);
    grid->addWidget(start_, 2, 1);
    column->addLayout(grid);

    refusal_ = refusalLine(this);
    column->addWidget(refusal_);
    column->addWidget(okCancel(this));
}

core::Result<RenumberPagesDialog::Choice> RenumberPagesDialog::choice() const
{
    core::Result<std::vector<int>> pages = core::parsePageRanges(range_->text(), pageCount_);
    if (!pages) return pages.error();
    return Choice{std::move(pages).value(), static_cast<core::PageLabelStyle>(style_->currentData().toInt()), prefix_->text(),
                  start_->value()};
}

void RenumberPagesDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        showRefusal(refusal_, chosen.error());
        return;
    }
    QDialog::accept();
}

bool RenumberPagesDialog::canAsk()
{
    return static_cast<bool>(installedRenumber()) || aPersonCanAnswer();
}

std::optional<RenumberPagesDialog::Choice> RenumberPagesDialog::ask(QWidget* parent, int pageCount, const std::vector<int>& initial)
{
    if (installedRenumber()) return installedRenumber()(pageCount, initial);
    if (!aPersonCanAnswer()) return std::nullopt;
    RenumberPagesDialog dialog(pageCount, initial, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void RenumberPagesDialog::setAnswerForTesting(Answer answer)
{
    installedRenumber() = std::move(answer);
}

} // namespace rawpage::ui
