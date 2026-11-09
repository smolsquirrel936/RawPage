// ui/dialogs/MergeDialog -- what `merge.documents` opens. Stage 7 T10 (`Stage7-Assembly-Plan.md` §6, D7-16, D7-33;
// guide A-66 ... A-72, M-03).
//
// IT COLLECTS PARAMETERS (CLAUDE.md): the files, in the order chosen, a page range for each, and the options --
// a bookmark per file named from its file name or its title (`documents/mergeBookmarkPerFile` first), colliding form
// fields renamed or merged (`documents/renameCollidingFields` first), a blank page between files. `MainWindow` hands the
// choice to `core::MergeRunner`; the dialog builds nothing.
//
// FILES COME IN THREE WAYS: *Add Files…*, a drag from Explorer onto the dialog, and the list given when it opens. A
// file may be listed twice -- a merge of one file with itself is a merge. Each row says what the file is before the
// merge does: its page count, or why it will be skipped (encrypted, not a PDF, unreadable) -- read through `Counter`,
// the window's `IDocumentBackend::filePageCount`, so the dialog itself holds no backend. A row the merge will skip is
// still merged-in-order with the rest and named in the report: the dialog warns, the merge decides.
//
// A PAGE RANGE IS CHECKED BEFORE THE DIALOG CLOSES (`core::parsePageRanges`, against the file's count): empty means
// every page, and a range it refuses keeps the dialog open with the file and the refusal under the list.
//
// NO PREVIEW BEYOND THE COUNT (D7-33): `Features.md` §5.3's *preview inputs* is each row's page count or skip reason;
// a thumbnail of a file not open would mean opening it in the engine for the dialog, and is not built.
//
// NOBODY TO ASK (convention 16): `canAsk()` is false under the offscreen platform unless a test installed an answer,
// and `merge.documents` is DISABLED rather than blocking. *Add Files…* asks through a file picker; a case drives
// `addFiles` instead.

#ifndef RAWPAGE_UI_DIALOGS_MERGEDIALOG_H
#define RAWPAGE_UI_DIALOGS_MERGEDIALOG_H

#include "core/Result.h"
#include "core/doc/Merge.h"

#include <QDialog>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QTableWidget;

namespace rawpage::ui {

class MergeDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        std::vector<core::MergeInput> inputs;   // in the order chosen
        core::MergeOptions options;
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    // A file's page count, or why it cannot be read -- the window's `filePageCount`.
    using Counter = std::function<core::Result<int>(const QString& path)>;

    // The table's columns.
    enum Column { FileColumn = 0, RangeColumn = 1, CountColumn = 2 };

    MergeDialog(const core::MergeOptions& defaults, Counter counter, QWidget* parent = nullptr);

    // Rows for `paths`, at the end, in order -- each counted as it is added.
    void addFiles(const QStringList& paths);
    // The selected rows moved one place up (-1) or down (+1), kept selected; removed.
    void moveSelected(int by);
    void removeSelected();

    // The choice the widgets hold, or the refusal that keeps the dialog open: no file at all (`error.merge.noInputs`),
    // or a range a file's count refuses (`error.merge.badRange`, its context the sentence naming the file and why).
    core::Result<Choice> choice() const;

    // For a case that drives the widget itself.
    QTableWidget* table() const { return table_; }
    QPushButton* addButton() const { return add_; }
    QPushButton* removeButton() const { return remove_; }
    QPushButton* upButton() const { return up_; }
    QPushButton* downButton() const { return down_; }
    QComboBox* bookmarksBox() const { return bookmarks_; }
    QRadioButton* renameButton() const { return rename_; }
    QRadioButton* mergeButton() const { return merge_; }
    QCheckBox* separatorBox() const { return separator_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, const core::MergeOptions& defaults, Counter counter, const QStringList& initial = {});
    using Answer = std::function<std::optional<Choice>(const core::MergeOptions& defaults)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void updateButtons();

    Counter counter_;
    QTableWidget* table_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
    QComboBox* bookmarks_ = nullptr;
    QRadioButton* rename_ = nullptr;
    QRadioButton* merge_ = nullptr;
    QCheckBox* separator_ = nullptr;
    QLabel* refusal_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_MERGEDIALOG_H
