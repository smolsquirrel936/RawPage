#include "ui/dialogs/UnsavedChangesDialog.h"

#include "ui/shell/Catalog.h"

#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

UnsavedChangesDialog::Answer& installedAnswer()
{
    static UnsavedChangesDialog::Answer answer;
    return answer;
}

} // namespace

UnsavedChangesDialog::UnsavedChangesDialog(const QString& documentName, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("unsavedChangesDialog"));
    setWindowTitle(catalog::translate(catalog::key::UnsavedTitle));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(12);
    column->addWidget(new QLabel(catalog::translate(catalog::key::UnsavedQuestion).arg(documentName), this));

    auto* buttons = new QDialogButtonBox(this);
    save_ = buttons->addButton(catalog::translate(catalog::key::UnsavedSave), QDialogButtonBox::AcceptRole);
    discard_ = buttons->addButton(catalog::translate(catalog::key::UnsavedDiscard), QDialogButtonBox::DestructiveRole);
    cancel_ = buttons->addButton(catalog::translate(catalog::key::UnsavedCancel), QDialogButtonBox::RejectRole);
    save_->setDefault(true);
    connect(save_, &QPushButton::clicked, this, [this] { disposition_ = CloseDisposition::Save; accept(); });
    connect(discard_, &QPushButton::clicked, this, [this] { disposition_ = CloseDisposition::Discard; accept(); });
    connect(cancel_, &QPushButton::clicked, this, [this] { disposition_ = CloseDisposition::Cancel; reject(); });
    column->addWidget(buttons);
}

bool UnsavedChangesDialog::canAsk()
{
    return static_cast<bool>(installedAnswer())
           || QGuiApplication::platformName() != QLatin1String("offscreen");
}

std::optional<CloseDisposition> UnsavedChangesDialog::ask(QWidget* parent, const QString& documentName)
{
    if (installedAnswer())
        return installedAnswer()(documentName);
    if (!canAsk())
        return std::nullopt;
    UnsavedChangesDialog dialog(documentName, parent);
    dialog.exec();
    return dialog.disposition();     // Escape and the close box are Cancel
}

void UnsavedChangesDialog::setAnswerForTesting(Answer answer)
{
    installedAnswer() = std::move(answer);
}

} // namespace rawpage::ui
