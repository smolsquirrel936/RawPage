// ui/panes/DifferencesPane -- a comparison's differences, listed, counted and navigated. Stage 5 T13,
// decisions D5-17 and D5-18 (`Stage5-TextSearch-Plan.md` §3), guide case A-106.
//
// ---------------------------------------------------------------------------------------------
// THE SUMMARY AT STAGE 5 IS THIS LIST (D5-17, the user's answer to Q4)
// ---------------------------------------------------------------------------------------------
// One row per difference -- its kind (and the header or footer band it is in), the older and the newer
// page, and the words before and after -- under a line counting them by kind. A summary PAGE and an
// annotated PDF report are Stage 8's: both write a new document, and this pane writes nothing.
//
// ---------------------------------------------------------------------------------------------
// IT SHOWS AN ORDER IT IS GIVEN, AND OWNS NO DIFFERENCE
// ---------------------------------------------------------------------------------------------
// `ui/viewer/ComparisonView` decides the order -- the order `Ctrl+]` steps in -- and hands the rows over;
// a row's index here IS that difference's index there, so selecting a row and stepping to a difference are
// one number. Choosing a row -- a click, the arrows, `Enter` -- says `differenceChosen`, and the view goes
// there; the view going somewhere by any other route calls `setCurrent`, which selects the row and says
// nothing back, so the two never chase each other.
//
// It lives in the comparison's window, beside its two pages, not in the sidebar: the sidebar's panes are
// views of the document in front, and a comparison is two documents, neither of which is a tab (D5-18).

#ifndef RAWPAGE_UI_PANES_DIFFERENCESPANE_H
#define RAWPAGE_UI_PANES_DIFFERENCESPANE_H

#include "core/text/DocumentComparer.h"

#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace rawpage::ui {

class DifferencesPane : public QWidget {
    Q_OBJECT

public:
    explicit DifferencesPane(QWidget* parent = nullptr);
    ~DifferencesPane() override;

    // The rows, in the order given. Clears the current row.
    void setDifferences(const std::vector<core::text::Difference>& differences);
    // A line in place of the summary while there are no rows yet -- the run's progress.
    void setStatus(const QString& text);

    // Selects row `index` (-1: none) and scrolls it into view, without saying `differenceChosen`.
    void setCurrent(int index);
    int current() const;
    int rowCount() const;

    QTreeWidget* list() const noexcept { return list_; }
    QString summary() const;

    // The words a row shows for a difference's kind, with its band: "Inserted", "Replaced (footer)".
    static QString kindText(const core::text::Difference& difference);

signals:
    void differenceChosen(int index);

private:
    void onCurrentItemChanged(QTreeWidgetItem* current);

    QLabel*      summary_ = nullptr;
    QTreeWidget* list_ = nullptr;
    bool         settingCurrent_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_DIFFERENCESPANE_H
