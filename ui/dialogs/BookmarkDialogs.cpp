#include "ui/dialogs/BookmarkDialogs.h"

#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace rawpage::ui {
namespace {

bool aPersonCanAnswer()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

DeleteBookmarkDialog::Answer& installedDelete()
{
    static DeleteBookmarkDialog::Answer answer;
    return answer;
}

BookmarkPropertiesDialog::Answer& installedProperties()
{
    static BookmarkPropertiesDialog::Answer answer;
    return answer;
}

GenerateBookmarksDialog::Answer& installedGenerate()
{
    static GenerateBookmarksDialog::Answer answer;
    return answer;
}

BookmarkFileDialog::ImportAnswer& installedImport()
{
    static BookmarkFileDialog::ImportAnswer answer;
    return answer;
}

BookmarkFileDialog::ExportAnswer& installedExport()
{
    static BookmarkFileDialog::ExportAnswer answer;
    return answer;
}

QDialogButtonBox* okCancel(QDialog* dialog)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

// The target kinds, in the order the combo lists them -- what `Features.md` §5.4 names.
enum TargetChoice {
    kKeep = 0,       // as the bookmark has it now
    kPage,           // a page of this document, its view kept
    kCurrentView,    // the view in front
    kOpenFile,       // Launch
    kWebLink,        // URI
    kScript,         // JavaScript -- written, never run (D7-12)
    kPrint,          // Named /Print
    kSubmit,         // SubmitForm
    kRemotePage,     // GoToR
    kNothing,        // no target
};

// A few colours a reader draws well on white, by name -- what the colour combo offers.
const std::array<std::array<double, 3>, 6> kColours{{{0.0, 0.0, 0.0}, {0.8, 0.0, 0.0}, {0.0, 0.5, 0.0}, {0.0, 0.0, 0.8},
                                                     {0.8, 0.4, 0.0}, {0.5, 0.0, 0.5}}};

} // namespace

// --- Delete --------------------------------------------------------------------------------------------------------

bool DeleteBookmarkDialog::canAsk()
{
    return static_cast<bool>(installedDelete()) || aPersonCanAnswer();
}

std::optional<bool> DeleteBookmarkDialog::ask(QWidget* parent, const QString& title, int children)
{
    if (installedDelete()) return installedDelete()(title, children);
    if (!aPersonCanAnswer()) return std::nullopt;
    QMessageBox box(QMessageBox::Question, catalog::translate(catalog::key::BookmarksDeleteTitle),
                    catalog::translate(catalog::key::BookmarksDeleteQuestion).arg(title).arg(children), QMessageBox::NoButton, parent);
    QPushButton* withChildren = box.addButton(catalog::translate(catalog::key::BookmarksDeleteWithChildren), QMessageBox::DestructiveRole);
    QPushButton* keep = box.addButton(catalog::translate(catalog::key::BookmarksDeleteKeepChildren), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(keep);
    box.exec();
    if (box.clickedButton() == withChildren) return false;
    if (box.clickedButton() == keep) return true;
    return std::nullopt;
}

void DeleteBookmarkDialog::setAnswerForTesting(Answer answer)
{
    installedDelete() = std::move(answer);
}

// --- Properties ----------------------------------------------------------------------------------------------------

BookmarkPropertiesDialog::BookmarkPropertiesDialog(const core::OutlineItem& item, int pageCount, const core::Destination& currentView,
                                                   QWidget* parent)
    : QDialog(parent), item_(item), currentView_(currentView)
{
    setObjectName(QStringLiteral("bookmarkPropertiesDialog"));
    setWindowTitle(catalog::translate(catalog::key::BookmarksPropertiesTitle));
    setModal(true);
    auto* column = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    title_ = new QLineEdit(item.title, this);
    title_->setObjectName(QStringLiteral("titleField"));
    form->addRow(catalog::translate(catalog::key::BookmarksTitle), title_);
    coloured_ = new QCheckBox(catalog::translate(catalog::key::BookmarksColour), this);
    colour_ = new QComboBox(this);
    for (const auto key : {catalog::key::BookmarksColourBlack, catalog::key::BookmarksColourRed, catalog::key::BookmarksColourGreen,
                           catalog::key::BookmarksColourBlue, catalog::key::BookmarksColourOrange, catalog::key::BookmarksColourPurple})
        colour_->addItem(catalog::translate(key));
    coloured_->setChecked(item.colour.has_value());
    if (item.colour) {
        for (std::size_t i = 0; i < kColours.size(); ++i) {
            if (std::fabs(kColours[i][0] - (*item.colour)[0]) < 0.02 && std::fabs(kColours[i][1] - (*item.colour)[1]) < 0.02
                && std::fabs(kColours[i][2] - (*item.colour)[2]) < 0.02)
                colour_->setCurrentIndex(static_cast<int>(i));
        }
    }
    form->addRow(coloured_, colour_);
    bold_ = new QCheckBox(catalog::translate(catalog::key::BookmarksBold), this);
    bold_->setChecked(item.bold);
    italic_ = new QCheckBox(catalog::translate(catalog::key::BookmarksItalic), this);
    italic_->setChecked(item.italic);
    open_ = new QCheckBox(catalog::translate(catalog::key::BookmarksOpen), this);
    open_->setChecked(item.openByDefault);
    form->addRow(QString(), bold_);
    form->addRow(QString(), italic_);
    form->addRow(QString(), open_);
    target_ = new QComboBox(this);
    target_->setObjectName(QStringLiteral("targetCombo"));
    for (const auto key : {catalog::key::BookmarksTargetKeep, catalog::key::BookmarksTargetPage, catalog::key::BookmarksTargetCurrentView,
                           catalog::key::BookmarksTargetOpenFile, catalog::key::BookmarksTargetWebLink, catalog::key::BookmarksTargetScript,
                           catalog::key::BookmarksTargetPrint, catalog::key::BookmarksTargetSubmit,
                           catalog::key::BookmarksTargetRemotePage, catalog::key::BookmarksTargetNothing})
        target_->addItem(catalog::translate(key));
    form->addRow(catalog::translate(catalog::key::BookmarksTarget), target_);
    text_ = new QLineEdit(this);
    text_->setObjectName(QStringLiteral("targetText"));
    form->addRow(catalog::translate(catalog::key::BookmarksTargetText), text_);
    page_ = new QSpinBox(this);
    page_->setObjectName(QStringLiteral("targetPage"));
    page_->setRange(1, std::max(1, std::max(pageCount, 99999)));
    page_->setValue(item.destination.isResolved() ? item.destination.pageIndex + 1 : 1);
    if (item.action.kind == core::OutlineAction::Kind::GoToRemote) page_->setValue(item.action.destination.pageIndex + 1);
    form->addRow(catalog::translate(catalog::key::BookmarksTargetPageNumber), page_);
    switch (item.action.kind) {
    case core::OutlineAction::Kind::Launch:
    case core::OutlineAction::Kind::GoToRemote: text_->setText(item.action.file); break;
    case core::OutlineAction::Kind::Uri:
    case core::OutlineAction::Kind::SubmitForm: text_->setText(item.action.uri); break;
    case core::OutlineAction::Kind::JavaScript: text_->setText(item.action.script); break;
    default: break;
    }
    column->addLayout(form);
    column->addWidget(okCancel(this));
    connect(target_, &QComboBox::currentIndexChanged, this, &BookmarkPropertiesDialog::showFieldsFor);
    showFieldsFor(kKeep);
}

void BookmarkPropertiesDialog::showFieldsFor(int kind)
{
    text_->setEnabled(kind == kOpenFile || kind == kWebLink || kind == kScript || kind == kSubmit || kind == kRemotePage);
    page_->setEnabled(kind == kPage || kind == kRemotePage);
}

BookmarkPropertiesDialog::Choice BookmarkPropertiesDialog::choice() const
{
    Choice out;
    out.properties.title = title_->text();
    if (coloured_->isChecked()) out.properties.colour = kColours[static_cast<std::size_t>(std::max(0, colour_->currentIndex()))];
    out.properties.bold = bold_->isChecked();
    out.properties.italic = italic_->isChecked();
    out.properties.open = open_->isChecked();
    out.action = item_.action;
    core::OutlineAction& a = out.action;
    switch (target_->currentIndex()) {
    case kPage: {
        core::Destination view = item_.action.kind == core::OutlineAction::Kind::GoTo ? item_.action.destination : core::Destination{};
        if (view.fit == core::Destination::Fit::Unknown) view.fit = core::Destination::Fit::Fit;
        view.pageIndex = page_->value() - 1;
        a = core::OutlineAction::goTo(view);
        break;
    }
    case kCurrentView: a = core::OutlineAction::goTo(currentView_); break;
    case kOpenFile: a = core::OutlineAction{}; a.kind = core::OutlineAction::Kind::Launch; a.file = text_->text(); break;
    case kWebLink: a = core::OutlineAction{}; a.kind = core::OutlineAction::Kind::Uri; a.uri = text_->text().trimmed(); break;
    case kScript: a = core::OutlineAction{}; a.kind = core::OutlineAction::Kind::JavaScript; a.script = text_->text(); break;
    case kPrint: a = core::OutlineAction{}; a.kind = core::OutlineAction::Kind::Named; a.name = QStringLiteral("Print"); break;
    case kSubmit: a = core::OutlineAction{}; a.kind = core::OutlineAction::Kind::SubmitForm; a.uri = text_->text().trimmed(); break;
    case kRemotePage:
        a = core::OutlineAction{};
        a.kind = core::OutlineAction::Kind::GoToRemote;
        a.file = text_->text();
        a.destination.pageIndex = page_->value() - 1;
        a.destination.fit = core::Destination::Fit::Fit;
        break;
    case kNothing: a = core::OutlineAction{}; break;
    default: break;
    }
    return out;
}

bool BookmarkPropertiesDialog::canAsk()
{
    return static_cast<bool>(installedProperties()) || aPersonCanAnswer();
}

std::optional<BookmarkPropertiesDialog::Choice> BookmarkPropertiesDialog::ask(QWidget* parent, const core::OutlineItem& item, int pageCount,
                                                                              const core::Destination& currentView)
{
    if (installedProperties()) return installedProperties()(item, pageCount, currentView);
    if (!aPersonCanAnswer()) return std::nullopt;
    BookmarkPropertiesDialog dialog(item, pageCount, currentView, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return dialog.choice();
}

void BookmarkPropertiesDialog::setAnswerForTesting(Answer answer)
{
    installedProperties() = std::move(answer);
}

// --- Generate ------------------------------------------------------------------------------------------------------

GenerateBookmarksDialog::GenerateBookmarksDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("generateBookmarksDialog"));
    setWindowTitle(catalog::translate(catalog::key::BookmarksGenerateTitle));
    setModal(true);
    auto* column = new QVBoxLayout(this);
    headings_ = new QRadioButton(catalog::translate(catalog::key::BookmarksGenerateHeadings), this);
    fonts_ = new QRadioButton(catalog::translate(catalog::key::BookmarksGenerateFonts), this);
    pattern_ = new QRadioButton(catalog::translate(catalog::key::BookmarksGeneratePattern), this);
    headings_->setChecked(true);
    auto* form = new QFormLayout();
    minimum_ = new QDoubleSpinBox(this);
    minimum_->setRange(0.0, 400.0);
    minimum_->setDecimals(1);
    minimum_->setSpecialValueText(catalog::translate(catalog::key::BookmarksGenerateLargerThanBody));
    boldOnly_ = new QCheckBox(catalog::translate(catalog::key::BookmarksGenerateBoldOnly), this);
    expression_ = new QLineEdit(this);
    expression_->setPlaceholderText(QStringLiteral("^[0-9]+(\\.[0-9]+)* "));
    form->addRow(catalog::translate(catalog::key::BookmarksGenerateMinimumSize), minimum_);
    form->addRow(QString(), boldOnly_);
    form->addRow(catalog::translate(catalog::key::BookmarksGenerateExpression), expression_);
    column->addWidget(headings_);
    column->addWidget(fonts_);
    column->addWidget(pattern_);
    column->addLayout(form);
    column->addWidget(okCancel(this));
}

core::OutlineGeneratorOptions GenerateBookmarksDialog::choice() const
{
    core::OutlineGeneratorOptions out;
    out.mode = fonts_->isChecked() ? core::OutlineGeneratorOptions::Mode::Fonts
               : pattern_->isChecked() ? core::OutlineGeneratorOptions::Mode::Pattern
                                       : core::OutlineGeneratorOptions::Mode::Headings;
    out.minimumSize = minimum_->value();
    out.boldOnly = boldOnly_->isChecked();
    out.pattern = expression_->text();
    return out;
}

bool GenerateBookmarksDialog::canAsk()
{
    return static_cast<bool>(installedGenerate()) || aPersonCanAnswer();
}

std::optional<core::OutlineGeneratorOptions> GenerateBookmarksDialog::ask(QWidget* parent)
{
    if (installedGenerate()) return installedGenerate()();
    if (!aPersonCanAnswer()) return std::nullopt;
    GenerateBookmarksDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return dialog.choice();
}

void GenerateBookmarksDialog::setAnswerForTesting(Answer answer)
{
    installedGenerate() = std::move(answer);
}

// --- Import and export ---------------------------------------------------------------------------------------------

bool BookmarkFileDialog::canAsk()
{
    return static_cast<bool>(installedImport()) || static_cast<bool>(installedExport()) || aPersonCanAnswer();
}

std::optional<QString> BookmarkFileDialog::askImport(QWidget* parent)
{
    if (installedImport()) return installedImport()();
    if (!aPersonCanAnswer()) return std::nullopt;
    const QString path = QFileDialog::getOpenFileName(parent, catalog::translate(catalog::key::BookmarksImportTitle), QString(),
                                                      catalog::translate(catalog::key::BookmarksFileFilter));
    if (path.isEmpty()) return std::nullopt;
    return path;
}

std::optional<std::pair<QString, core::OutlineFormat>> BookmarkFileDialog::askExport(QWidget* parent)
{
    if (installedExport()) return installedExport()();
    if (!aPersonCanAnswer()) return std::nullopt;
    QString chosenFilter;
    const QString path = QFileDialog::getSaveFileName(parent, catalog::translate(catalog::key::BookmarksExportTitle), QString(),
                                                      catalog::translate(catalog::key::BookmarksFileFilter), &chosenFilter);
    if (path.isEmpty()) return std::nullopt;
    const core::OutlineFormat format = path.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive) ? core::OutlineFormat::Xml
                                                                                                  : core::OutlineFormat::Text;
    return std::make_pair(path, format);
}

void BookmarkFileDialog::setAnswersForTesting(ImportAnswer importAnswer, ExportAnswer exportAnswer)
{
    installedImport() = std::move(importAnswer);
    installedExport() = std::move(exportAnswer);
}

} // namespace rawpage::ui
