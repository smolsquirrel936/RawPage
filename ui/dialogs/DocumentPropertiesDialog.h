// ui/dialogs/DocumentPropertiesDialog -- what `Ctrl+D` opens: what the document says about itself,
// and nothing a person can change. Stage 4 T12, decision **D4-4**
// (`Stage4-ViewingSurface-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// IT SHOWS WHAT IT WAS READ, AND IT EDITS NOTHING (D4-4)
// ---------------------------------------------------------------------------------------------
// The standing rule is *every dialog collects parameters and constructs a `Command`* (`CLAUDE.md`).
// This one collects nothing and constructs none, which is not an exception to the rule but the
// other half of it: **Stage 4 constructs no `Command` at all** (D4-1), and `mutation-lint` reports
// the same one command on the last day of this stage as on the first. Editing the Info dictionary
// is Stage 8's, with the `/ModDate` policy and the XMP writer that have to stay consistent with it.
//
// So every field here is a `QLabel`. Not a read-only `QLineEdit`, which looks like a field somebody
// forgot to enable and invites a support question this stage cannot answer.
//
// **NO XMP** (D4-4). `core::Metadata` reads the Info dictionary and the file header and says so at
// its own header; a dialog that showed a merge nobody specified would be a second source of truth
// for every field it duplicates.
//
// ---------------------------------------------------------------------------------------------
// IT HOLDS A STRUCT, NOT A BACKEND
// ---------------------------------------------------------------------------------------------
// `Properties` is assembled by `ui/shell/MainWindow` from the four places the facts actually live --
// `IDocumentBackend::metadata()`, `documentId()`, `repairReport()` and the `PageTree` -- and handed
// here as values. That is `RotatePagesDialog`'s arrangement and it is what makes this widget
// drivable by a case with no document, no engine and no window: `tests/ui` may link neither.
//
// It is also why the file size and the path are `DocumentId`'s and not a fresh `QFileInfo` read.
// The identity was taken at open, from the bytes that were opened (D3-16); a second read here would
// describe a file that may have changed since, and would disagree with the document on screen.
//
// ---------------------------------------------------------------------------------------------
// A DATE IS SHOWN AS IT WAS STORED WHEN IT CANNOT BE PARSED
// ---------------------------------------------------------------------------------------------
// `core::Metadata::parseDate` returns nothing for the forms producers mangle. This dialog then
// shows the raw string rather than an empty field or an invented substitute -- guide case A-28's
// rule, applied at the surface it was written for.

#ifndef RAWPAGE_UI_DIALOGS_DOCUMENTPROPERTIESDIALOG_H
#define RAWPAGE_UI_DIALOGS_DOCUMENTPROPERTIESDIALOG_H

#include "core/Geometry.h"
#include "core/doc/Metadata.h"

#include <QDialog>
#include <QString>

#include <map>

class QFormLayout;
class QLabel;

namespace rawpage::ui {

class DocumentPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    // Every fact the dialog shows, as values. Assembled by the shell; see the header.
    struct Properties {
        core::Metadata info;

        QString path;
        qint64  fileSize = -1;      // bytes, from the identity taken at open; -1 means unknown

        int pageCount = 0;
        int currentPage = 0;        // ZERO-BASED, as everything in `core/` is
        QString currentPageLabel;   // empty when the document has no `/PageLabels`
        core::PdfSize currentPageSize;   // PDF points, after the page's own `/Rotate`

        bool repaired = false;      // its cross-reference table had to be reconstructed
        bool encrypted = false;     // a password was asked for on the way in
    };

    // The rows, in the order they are shown. The enum exists so a case can look one up without
    // matching on a translated string -- a test that read the label text would pass in English and
    // fail under `--lang zh-TW`, which is exactly the shape of coverage this project does not want.
    enum class Field {
        Title, Author, Subject, Keywords, Creator, Producer, Created, Modified,
        Version, Pages, PageSize, Security, Repaired, Path, FileSize,
    };

    explicit DocumentPropertiesDialog(const Properties& properties, QWidget* parent = nullptr);
    ~DocumentPropertiesDialog() override;

    // The VALUE label for one row. Never null for a `Field` this dialog declares.
    QLabel* valueLabel(Field field) const;

    // The text one row shows, which is what a case asserts on.
    QString value(Field field) const;

    // Whether anyone can be shown a modal dialog: a person on a real platform, or a test that has
    // asked for the dialog to be suppressed. Convention 16 and Stage 1's I-23 -- a modal `exec()`
    // under the offscreen platform is shown to nobody and never returns, which stopped a whole
    // `ctest` run dead with no failure and no output.
    static bool canAsk();

private:
    void addRow(QFormLayout* form, Field field, QLatin1String labelKey, const QString& text);

    std::map<Field, QLabel*> values_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_DOCUMENTPROPERTIESDIALOG_H
