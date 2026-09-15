#include "ui/dialogs/RotatePagesDialog.h"

#include "core/cmd/commands/PageCommands.h"
#include "ui/shell/Catalog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QGuiApplication>
#include <QRadioButton>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

RotatePagesDialog::Answer& installedAnswer()
{
    static RotatePagesDialog::Answer answer;
    return answer;
}

} // namespace

RotatePagesDialog::RotatePagesDialog(QWidget* parent) : QDialog(parent)
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
    auto* pagesColumn = new QVBoxLayout(pages);
    currentPage_ = new QRadioButton(catalog::translate(catalog::key::RotateCurrentPage), pages);
    allPages_ = new QRadioButton(catalog::translate(catalog::key::RotateAllPages), pages);
    currentPage_->setChecked(true);
    pagesColumn->addWidget(currentPage_);
    pagesColumn->addWidget(allPages_);
    column->addWidget(pages);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
}

RotatePagesDialog::Choice RotatePagesDialog::choice() const
{
    Choice choice;
    choice.degrees = half_->isChecked() ? 180 : counterClockwise_->isChecked() ? 270 : 90;
    choice.allPages = allPages_->isChecked();
    return choice;
}

bool RotatePagesDialog::canAsk()
{
    return static_cast<bool>(installedAnswer())
           || QGuiApplication::platformName() != QLatin1String("offscreen");
}

std::optional<RotatePagesDialog::Choice> RotatePagesDialog::ask(QWidget* parent)
{
    if (installedAnswer())
        return installedAnswer()();
    if (!canAsk())
        return std::nullopt;
    RotatePagesDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return dialog.choice();
}

void RotatePagesDialog::setAnswerForTesting(Answer answer)
{
    installedAnswer() = std::move(answer);
}

RotationPlan planRotation(const RotatePagesDialog::Choice& choice, int currentPage, int pageCount)
{
    RotationPlan plan;
    if (pageCount <= 0)
        return plan;
    if (!choice.allPages) {
        plan.commands.push_back(std::make_unique<core::RotatePageCommand>(std::clamp(currentPage, 0, pageCount - 1), choice.degrees));
        return plan;
    }
    // ONE UNDO STEP for "all pages" (D3-13, D3-11): the group is what makes one Ctrl+Z take it all back.
    plan.group = core::CatalogText{QLatin1String("command.page.rotateAll"),
                                   {QString::number(pageCount), QString::number(choice.degrees)}};
    for (int page = 0; page < pageCount; ++page)
        plan.commands.push_back(std::make_unique<core::RotatePageCommand>(page, choice.degrees));
    return plan;
}

} // namespace rawpage::ui
