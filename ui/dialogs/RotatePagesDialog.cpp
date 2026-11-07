#include "ui/dialogs/RotatePagesDialog.h"

#include "core/cmd/commands/PageCommands.h"
#include "core/doc/PageTree.h"
#include "ui/shell/Catalog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QVBoxLayout>

#include <algorithm>

namespace rawpage::ui {
namespace {

RotatePagesDialog::Answer& installedAnswer()
{
    static RotatePagesDialog::Answer answer;
    return answer;
}

} // namespace

RotatePagesDialog::RotatePagesDialog(int currentPage, std::vector<int> selected, int pageCount, QWidget* parent)
    : QDialog(parent), pageCount_(pageCount)
{
    setObjectName(QStringLiteral("rotatePagesDialog"));
    setWindowTitle(catalog::translate(catalog::key::RotateTitle));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);

    auto* direction = new QGroupBox(catalog::translate(catalog::key::RotateDirection), this);
    auto* directionColumn = new QVBoxLayout(direction);
    clockwise_ = new QRadioButton(catalog::translate(catalog::key::RotateClockwise), direction);
    half_ = new QRadioButton(catalog::translate(catalog::key::RotateHalf), direction);
    counterClockwise_ = new QRadioButton(catalog::translate(catalog::key::RotateCounterClockwise), direction);
    clockwise_->setChecked(true);
    directionColumn->addWidget(clockwise_);
    directionColumn->addWidget(half_);
    directionColumn->addWidget(counterClockwise_);
    column->addWidget(direction);

    auto* pages = new QGroupBox(catalog::translate(catalog::key::RotatePages), this);
    auto* pagesGrid = new QGridLayout(pages);
    currentPage_ = new QRadioButton(catalog::translate(catalog::key::RotateCurrentPage).arg(currentPage + 1), pages);
    // THE SELECTED PAGES, NAMED (S7 T8): as the current page is, so a person sees which pages will turn.
    selectedPages_ = new QRadioButton(catalog::translate(catalog::key::PagesRotateSelected).arg(core::formatPageRanges(selected)), pages);
    selectedPages_->setVisible(selected.size() > 1);
    allPages_ = new QRadioButton(catalog::translate(catalog::key::RotateAllPages), pages);
    range_ = new QRadioButton(catalog::translate(catalog::key::PagesRotateRange), pages);
    rangeField_ = new QLineEdit(pages);
    rangeField_->setObjectName(QStringLiteral("rangeField"));
    rangeField_->setPlaceholderText(catalog::translate(catalog::key::ExtractPagesHint));
    rangeField_->setText(core::formatPageRanges(selected.empty() ? std::vector<int>{currentPage} : selected));
    rangeField_->setEnabled(false);
    connect(range_, &QRadioButton::toggled, rangeField_, &QWidget::setEnabled);
    (selected.size() > 1 ? selectedPages_ : currentPage_)->setChecked(true);
    pagesGrid->addWidget(currentPage_, 0, 0, 1, 2);
    pagesGrid->addWidget(selectedPages_, 1, 0, 1, 2);
    pagesGrid->addWidget(allPages_, 2, 0, 1, 2);
    pagesGrid->addWidget(range_, 3, 0);
    pagesGrid->addWidget(rangeField_, 3, 1);
    column->addWidget(pages);

    auto* filters = new QFormLayout();
    parity_ = new QComboBox(this);
    parity_->setObjectName(QStringLiteral("parityBox"));
    parity_->addItem(catalog::translate(catalog::key::PagesParityAny), static_cast<int>(core::PageParity::Any));
    parity_->addItem(catalog::translate(catalog::key::PagesParityOdd), static_cast<int>(core::PageParity::Odd));
    parity_->addItem(catalog::translate(catalog::key::PagesParityEven), static_cast<int>(core::PageParity::Even));
    orientation_ = new QComboBox(this);
    orientation_->setObjectName(QStringLiteral("orientationBox"));
    orientation_->addItem(catalog::translate(catalog::key::PagesOrientationAny), static_cast<int>(core::PageOrientation::Any));
    orientation_->addItem(catalog::translate(catalog::key::PagesOrientationPortrait), static_cast<int>(core::PageOrientation::Portrait));
    orientation_->addItem(catalog::translate(catalog::key::PagesOrientationLandscape), static_cast<int>(core::PageOrientation::Landscape));
    filters->addRow(catalog::translate(catalog::key::PagesParity), parity_);
    filters->addRow(catalog::translate(catalog::key::PagesOrientation), orientation_);
    column->addLayout(filters);

    refusal_ = new QLabel(this);
    refusal_->setObjectName(QStringLiteral("refusal"));
    refusal_->setWordWrap(true);
    refusal_->hide();
    column->addWidget(refusal_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
}

core::Result<RotatePagesDialog::Choice> RotatePagesDialog::choice() const
{
    Choice choice;
    choice.degrees = half_->isChecked() ? 180 : counterClockwise_->isChecked() ? 270 : 90;
    choice.pages = selectedPages_->isChecked() ? Pages::Selected
                   : allPages_->isChecked()    ? Pages::All
                   : range_->isChecked()       ? Pages::Range
                                               : Pages::Current;
    if (choice.pages == Pages::Range) {
        choice.range = rangeField_->text().trimmed();
        if (core::Result<std::vector<int>> parsed = core::parsePageRanges(choice.range, pageCount_); !parsed) return parsed.error();
    }
    choice.parity = static_cast<core::PageParity>(parity_->currentData().toInt());
    choice.orientation = static_cast<core::PageOrientation>(orientation_->currentData().toInt());
    return choice;
}

void RotatePagesDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        refusal_->setText(catalog::translateError(QString(chosen.error().messageKey), chosen.error().context));
        refusal_->show();
        return;
    }
    QDialog::accept();
}

bool RotatePagesDialog::canAsk()
{
    return static_cast<bool>(installedAnswer())
           || QGuiApplication::platformName() != QLatin1String("offscreen");
}

std::optional<RotatePagesDialog::Choice> RotatePagesDialog::ask(QWidget* parent, int currentPage, const std::vector<int>& selected,
                                                               int pageCount)
{
    if (installedAnswer())
        return installedAnswer()();
    if (!canAsk())
        return std::nullopt;
    RotatePagesDialog dialog(currentPage, selected, pageCount, parent);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void RotatePagesDialog::setAnswerForTesting(Answer answer)
{
    installedAnswer() = std::move(answer);
}

core::Result<std::unique_ptr<core::Command>> planRotation(const RotatePagesDialog::Choice& choice, int currentPage,
                                                          const std::vector<int>& selected, const core::PageTree& pages)
{
    const int count = pages.count();
    if (count <= 0) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pages.noneMatch"), QString()};
    std::vector<int> set;
    switch (choice.pages) {
    case RotatePagesDialog::Pages::Current:
        set.push_back(std::clamp(currentPage, 0, count - 1));
        break;
    case RotatePagesDialog::Pages::Selected:
        set = selected;
        if (set.empty()) set.push_back(std::clamp(currentPage, 0, count - 1));
        break;
    case RotatePagesDialog::Pages::All:
        for (int page = 0; page < count; ++page) set.push_back(page);
        break;
    case RotatePagesDialog::Pages::Range: {
        core::Result<std::vector<int>> parsed = core::parsePageRanges(choice.range, count);
        if (!parsed) return parsed.error();
        set = std::move(parsed).value();
        break;
    }
    }
    // ONE COMMAND, ONE UNDO STEP, however many pages pass the filters (S7 T8).
    set = core::filterPages(set, choice.parity, choice.orientation, pages);
    if (set.empty()) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pages.noneMatch"), QString()};
    return std::unique_ptr<core::Command>(std::make_unique<core::RotatePageCommand>(std::move(set), choice.degrees));
}

} // namespace rawpage::ui
