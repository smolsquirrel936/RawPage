// ui/dialogs/UnsavedChangesDialog -- Save, Don't Save or Cancel, asked once per dirty document before it
// closes. Stage 3 T10, decision D3-14 (`Stage3-Mutation-Plan.md` §3).
//
// THE FIRST STAGE IN WHICH CLOSING CAN LOSE WORK, and so every route that closes a document asks: closing a
// tab, closing a window (a torn-off one too), and quitting. The dialog collects a disposition and does
// nothing with it; `MainWindow::closeDocument` acts -- Save saves first, Don't Save discards the recovery
// directory, Cancel leaves the document open and dirty.
//
// NOBODY TO ASK (convention 16): `ask()` returns nothing at once under the offscreen platform unless a test
// has installed an answer, and a close with no disposition is REFUSED rather than guessed. A headless
// caller that means to close a dirty document passes the disposition itself.

#ifndef RAWPAGE_UI_DIALOGS_UNSAVEDCHANGESDIALOG_H
#define RAWPAGE_UI_DIALOGS_UNSAVEDCHANGESDIALOG_H

#include <QDialog>
#include <QString>

#include <functional>
#include <optional>

class QPushButton;

namespace rawpage::ui {

enum class CloseDisposition { Save, Discard, Cancel };

class UnsavedChangesDialog : public QDialog {
    Q_OBJECT

public:
    UnsavedChangesDialog(const QString& documentName, QWidget* parent = nullptr);

    CloseDisposition disposition() const noexcept { return disposition_; }

    QPushButton* saveButton() const { return save_; }
    QPushButton* discardButton() const { return discard_; }
    QPushButton* cancelButton() const { return cancel_; }

    static bool canAsk();
    static std::optional<CloseDisposition> ask(QWidget* parent, const QString& documentName);

    using Answer = std::function<std::optional<CloseDisposition>(const QString& documentName)>;
    static void setAnswerForTesting(Answer answer);

private:
    CloseDisposition disposition_ = CloseDisposition::Cancel;
    QPushButton* save_ = nullptr;
    QPushButton* discard_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_UNSAVEDCHANGESDIALOG_H
