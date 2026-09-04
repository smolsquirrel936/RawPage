#include "ui/dialogs/PasswordDialog.h"

#include "core/Log.h"
#include "ui/shell/Catalog.h"

#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;

} // namespace

bool PasswordDialog::canPrompt()
{
    // THE SAME QUESTION `file.open`'s enablement asks, and deliberately the same answer. Stage 1's
    // I-23: a modal dialog under the offscreen platform is shown to nobody and never returns, which
    // stopped a whole `ctest` run with no failure and no output.
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

PasswordDialog::PasswordDialog(QString documentName, int attempt, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("passwordDialog"));
    setWindowTitle(catalog::translate(catalog::key::PasswordTitle));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);

    // THE SECOND ASK SAYS SOMETHING DIFFERENT. `attempt` is 1-based (`core/backend.h`), and a
    // prompt that repeated itself is one the user believes is not registering their typing.
    prompt_ = new QLabel(this);
    prompt_->setObjectName(QStringLiteral("passwordPrompt"));
    prompt_->setWordWrap(true);
    prompt_->setText(attempt <= 1
                         ? catalog::translate(catalog::key::PasswordPrompt).arg(documentName)
                         : catalog::translate(catalog::key::PasswordRetry).arg(documentName));
    column->addWidget(prompt_);

    password_ = new QLineEdit(this);
    password_->setObjectName(QStringLiteral("passwordBox"));
    password_->setEchoMode(QLineEdit::Password);
    password_->setPlaceholderText(catalog::translate(catalog::key::PasswordLabel));
    column->addWidget(password_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Enter submits, which is what every password field in existence does and what a user will do
    // without looking for a button.
    connect(password_, &QLineEdit::returnPressed, this, &QDialog::accept);

    password_->setFocus();
}

QString PasswordDialog::password() const
{
    return password_ != nullptr ? password_->text() : QString();
}

core::PasswordCallback PasswordDialog::callbackFor(const QString& documentName, QWidget* parent)
{
    // `parent` is captured as a QPointer: a callback outliving the window that made it is exactly
    // the shape convention 10 is about, and this one is handed to the engine and invoked from
    // inside `open()`, which a tear-off or a close can happen around.
    QPointer<QWidget> guard(parent);

    return [documentName, guard](int attempt) -> std::optional<QString> {
        if (!canPrompt()) {
            // NOBODY TO ASK. `std::nullopt` here is `Cancelled` rather than a failure, and the
            // encrypted file is refused by name -- which is the honest headless outcome and is what
            // stops a batch run from hanging on a modal nobody can see (I-23).
            RAWPAGE_LOG_INFO(LogSubsystem::Ui)
                << "password: no user to prompt; declining" << documentName;
            return std::nullopt;
        }

        PasswordDialog dialog(documentName, attempt, guard);
        if (dialog.exec() != QDialog::Accepted) return std::nullopt;

        // AN EMPTY PASSWORD IS STILL AN ANSWER, and it is not the same as cancelling: a document
        // with an owner password and an empty user password opens with one, and returning
        // `nullopt` for it would refuse a file that is not actually locked.
        return dialog.password();
    };
}

} // namespace rawpage::ui
