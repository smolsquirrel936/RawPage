// ui/dialogs/RecoveryDialog -- what a launch after a crash asks: for each document whose changes were not
// saved, recover them, discard them, or decide later. Stage 3 T11, decision D3-11 (`Stage3-Mutation-Plan.md`
// §3), driven by `app/RecoveryPrompt`.
//
// THE RULE IT LIVES UNDER (CLAUDE.md): *every dialog collects parameters.* This one is handed a description of
// each journal -- a name, a path, and which of four states it is in -- and answers one disposition per row. It
// holds no journal, no document and no window; it replays nothing. So the REPORT of a replay that stopped at
// step *k* cannot be here, because the dialog has closed before any replay starts: it is the recovered
// document's own banner (`MainWindow::recoverDocument`).
//
// WHAT "RECOVER" MEANS DEPENDS ON THE ROW, and the button says which. A source that is still the file the
// changes were made to is recovered INTO it: the document opens under its own path, with the changes unsaved.
// A source that has changed since -- or a document that never had a file of its own -- is recovered into a
// NEW, UNTITLED document, and the file is never touched (D3-11). A source that is gone, or a journal that is
// damaged, cannot be recovered; its row offers Discard and Decide later only.
//
// NOBODY TO ASK (convention 16). Under the offscreen platform a modal dialog is shown to no one and never
// returns (Stage 1's I-23). `canAsk()` is false there unless a test has installed an answer, and the prompt
// then recovers nothing and discards nothing -- the journals wait for a launch that has a person in front of
// it. `setAnswerForTesting` is the parameterized twin; the widget itself is built and driven by
// `tests/ui/tst_recoverydialog.cpp`.

#ifndef RAWPAGE_UI_DIALOGS_RECOVERYDIALOG_H
#define RAWPAGE_UI_DIALOGS_RECOVERYDIALOG_H

#include <QDialog>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QLabel;
class QRadioButton;

namespace rawpage::ui {

enum class RecoveryDisposition { Recover, Discard, Later };

class RecoveryDialog : public QDialog {
    Q_OBJECT

public:
    struct Offer {
        enum class State {
            IntoFile,          // the source is unchanged: recover into it, under its own path
            SourceChanged,     // the source changed since: recover into a new, untitled document
            Untitled,          // the document had no file of its own: recover into a new, untitled document
            SourceMissing,     // nothing to replay onto
            Damaged,           // the journal cannot be read; `problem` says why
        };
        QString documentName;
        QString sourcePath;
        State state = State::IntoFile;
        QString problem;       // Damaged: the resolved reason
    };

    static bool canRecover(const Offer& offer) noexcept;

    explicit RecoveryDialog(std::vector<Offer> offers, QWidget* parent = nullptr);

    // One per offer, in order.
    std::vector<RecoveryDisposition> dispositions() const;

    // For a case that drives the widget itself.
    int rowCount() const noexcept { return static_cast<int>(rows_.size()); }
    QLabel* stateLabel(int row) const { return rows_.at(static_cast<std::size_t>(row)).state; }
    QRadioButton* recoverButton(int row) const { return rows_.at(static_cast<std::size_t>(row)).recover; }
    QRadioButton* discardButton(int row) const { return rows_.at(static_cast<std::size_t>(row)).discard; }
    QRadioButton* laterButton(int row) const { return rows_.at(static_cast<std::size_t>(row)).later; }

    // Whether anyone can answer: a person on a real platform, or a test's installed answer.
    static bool canAsk();

    // Shows the dialog and returns a disposition per offer -- or nothing, for Decide Later on all of them, which
    // is also the close box and Escape. The test's answer, if one is installed; nothing, immediately, when
    // `canAsk()` is false.
    static std::optional<std::vector<RecoveryDisposition>> ask(QWidget* parent, const std::vector<Offer>& offers);

    using Answer = std::function<std::optional<std::vector<RecoveryDisposition>>(const std::vector<Offer>&)>;
    static void setAnswerForTesting(Answer answer);

private:
    struct Row {
        QLabel* state = nullptr;
        QRadioButton* recover = nullptr;
        QRadioButton* discard = nullptr;
        QRadioButton* later = nullptr;
    };
    std::vector<Offer> offers_;
    std::vector<Row> rows_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_RECOVERYDIALOG_H
