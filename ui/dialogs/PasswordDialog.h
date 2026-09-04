// The password prompt an encrypted document raises. Stage 2 T14.
//
// ---------------------------------------------------------------------------------------------
// WHY THIS IS A DIALOG AND THE PROTECTED BANNER IS NOT.
//
// They answer different questions at different moments. The prompt is a QUESTION the engine asks
// mid-open — `core::PasswordCallback` is called from inside `IDocumentBackend::open()` and the open
// cannot continue until it is answered — so it is modal by necessity rather than by preference.
// The banner is a STATEMENT about a document that is already open, and it persists.
//
// ---------------------------------------------------------------------------------------------
// `attempt` IS 1-BASED, AND THE SECOND ASK SAYS SOMETHING DIFFERENT.
//
// `core/backend.h` documents the counter for exactly this: a caller can say *that was wrong, try
// again* on the second ask without keeping its own state. A prompt that showed the same sentence
// three times is a prompt the user believes is not registering their typing.
//
// **`std::nullopt` means the user gave up, and that is `Cancelled` rather than a failure to open.**
// The two lead to different messages and only one of them is worth reporting as a problem.
//
// ---------------------------------------------------------------------------------------------
// IT IS REFUSED WHEN THERE IS NOBODY TO ASK.
//
// Stage 1's I-23: a modal `exec()` under `QT_QPA_PLATFORM=offscreen` is shown to nobody and never
// returns, which stopped a whole `ctest` run dead with no failure and no output. So the CALLBACK
// this class vends checks `canPromptTheUser()` first and answers `std::nullopt` immediately when
// there is no one there — the batch runner and the test suite get a clean *cancelled*, and the
// encrypted file is refused by name instead of hanging the process.

#ifndef RAWPAGE_UI_DIALOGS_PASSWORDDIALOG_H
#define RAWPAGE_UI_DIALOGS_PASSWORDDIALOG_H

#include "core/backend.h"

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;

namespace rawpage::ui {

class PasswordDialog : public QDialog {
    Q_OBJECT

public:
    PasswordDialog(QString documentName, int attempt, QWidget* parent = nullptr);

    QString password() const;

    // Named accessors, for the suite: a case that looked these up by object name would keep passing
    // after the widget was replaced by a different one with the same name.
    QLineEdit* passwordBox() const { return password_; }
    QLabel*    promptLabel() const { return prompt_; }

    // THE CALLBACK `IDocumentBackend::open()` TAKES. It raises this dialog once per attempt, and
    // answers `std::nullopt` the moment the user cancels or there is nobody to ask.
    //
    // `parent` may be null. `documentName` is what the prompt names, so a handoff of six files does
    // not ask six identical questions.
    static core::PasswordCallback callbackFor(const QString& documentName, QWidget* parent);

    // Whether a modal prompt can be shown at all — false under the offscreen platform, in the batch
    // runner, and anywhere else with no user. Public because `MainWindow` asks the same question
    // about `file.open`'s enablement and the two must not disagree.
    static bool canPrompt();

private:
    QLabel*    prompt_ = nullptr;
    QLineEdit* password_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_PASSWORDDIALOG_H
