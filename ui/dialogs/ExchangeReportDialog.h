// ui/dialogs/ExchangeReportDialog -- what an import or a migration of comments did, comment by comment. Stage 6 T13,
// decisions D6-20 and D6-21 (`Stage6-Annotations-Plan.md` §3).
//
// A merge and a migration each say what became of every comment -- added, replaced, kept, refused; placed, reported
// -- and a comment that was NOT brought over must be said by name (D6-21: *"reported and never placed"*). The status
// bar carries the counts; this carries the rows, and the reason for each one not brought over in the person's own
// language. It COLLECTS NOTHING and constructs no command: by the time it is shown, the import has been run (or not).
// Modeless, so the page it describes can be read beside it.

#ifndef RAWPAGE_UI_DIALOGS_EXCHANGEREPORTDIALOG_H
#define RAWPAGE_UI_DIALOGS_EXCHANGEREPORTDIALOG_H

#include <QDialog>
#include <QString>

#include <vector>

class QTreeWidget;

namespace rawpage::ui {

struct ExchangeReport {
    struct Row {
        QString name;      // the comment's name in the exchange
        QString kind;      // its type, worded
        int page = -1;     // 0-based, where it was in the file (or the original); -1 for none
        QString outcome;   // worded: added, replaced, kept, refused, placed, reported
        QString reason;    // worded; empty for one brought over
        QString text;      // what a markup covered, when a migration knows it
    };
    QString title;
    QString summary;
    std::vector<Row> rows;
};

class ExchangeReportDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ExchangeReportDialog(const ExchangeReport& report, QWidget* parent = nullptr);

    QTreeWidget* list() const noexcept { return list_; }

private:
    QTreeWidget* list_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_EXCHANGEREPORTDIALOG_H
