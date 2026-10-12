// ui/panes/AttachmentsPane -- the files a document carries, listed. Stage 6 T11, decision D6-17
// (`Stage6-Annotations-Plan.md` §3; `Features.md` §4.2's *attach a file to the document as a whole, via the
// Attachments pane*).
//
// ===========================================================================================
// WHAT IT LISTS
// ===========================================================================================
// BOTH kinds of attachment, in one list (`core/doc/Attachments` says why there are two): every file of the
// document's `/EmbeddedFiles` tree, and every `FileAttachment` annotation's file, with its page -- each row its
// name, description, size, modification date and where it is. It reads the JOURNAL'S document -- the model every
// command changes -- so an attachment added or deleted is listed or gone at once, saved or not.
//
// ===========================================================================================
// WHAT IT DOES NOT DO
// ===========================================================================================
// It opens, extracts, adds and deletes NOTHING itself. Its buttons, a double click and `Enter` ask the window for a
// COMMAND (`actionRequested` -- `attachment.open`, `attachment.extract`, `attachment.add`, `attachment.delete`),
// which `MainWindow` registers and runs: one behaviour reachable from the pane, the tool finder and the batch path
// alike (the action layer's rule), and the asking, the refusing and the writing are the window's and the
// backend's. The pane holds the selection, and says which attachment is current.
//
// A search hit in an attachment selects it here (D6-11): `selectByName`.

#ifndef RAWPAGE_UI_PANES_ATTACHMENTSPANE_H
#define RAWPAGE_UI_PANES_ATTACHMENTSPANE_H

#include "core/cmd/ActionId.h"
#include "core/doc/Attachments.h"
#include "core/doc/AnnotationStore.h"

#include <QWidget>

#include <optional>
#include <vector>

class QLabel;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace rawpage::ui {

class AttachmentsPane : public QWidget {
    Q_OBJECT

public:
    // One listed attachment: which, the file, and its page (-1 for a file of the document as a whole).
    struct Row {
        core::AttachmentRef ref;
        core::EmbeddedFile file;
        int page = -1;
    };

    explicit AttachmentsPane(QWidget* parent = nullptr);

    // The document in front -- its files and its annotations, both read and never written -- and whether a command
    // may change them. Null and null for no document, when the pane says so. `canChange` is false for a document
    // this build will not edit (a companion window's view, an attached PDF opened read-only).
    void setDocument(const core::Attachments* files, const core::AnnotationStore* annotations, bool canChange);
    // The document changed: the list re-read, the current attachment kept when it is still listed.
    void refresh();

    const std::vector<Row>& rows() const noexcept { return rows_; }
    std::optional<Row> current() const;
    bool canChange() const noexcept { return canChange_ && files_ != nullptr && files_->status(); }

    // Makes `ref` current and visible; false when it is not listed.
    bool select(const core::AttachmentRef& ref);
    // A search hit names a file (D6-11): the first listed file with this name -- a document's first.
    bool selectByName(const QString& name);

    QTreeWidget* view() const noexcept { return list_; }
    QLabel* status() const noexcept { return status_; }

signals:
    // A button, a double click or `Enter`: the window is asked to run the command.
    void actionRequested(core::ActionId id);
    void currentChanged();

private:
    void rebuild();
    void updateButtons();
    QToolButton* button(core::ActionId id, QLatin1String word);

    const core::Attachments* files_ = nullptr;
    const core::AnnotationStore* annotations_ = nullptr;
    bool canChange_ = false;
    std::vector<Row> rows_;
    QTreeWidget* list_ = nullptr;
    QLabel* status_ = nullptr;
    QToolButton* open_ = nullptr;
    QToolButton* extract_ = nullptr;
    QToolButton* add_ = nullptr;
    QToolButton* delete_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_ATTACHMENTSPANE_H
