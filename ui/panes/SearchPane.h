// ui/panes/SearchPane -- Advanced Search: a FOLDER of PDFs, optionally its subfolders, its result list
// sorted, filtered, refined and exported. Stage 5 T9, decision D5-13 (`Stage5-TextSearch-Plan.md` §3).
// The catalog index and portfolios are Stage 16's (R2).
//
// ---------------------------------------------------------------------------------------------
// WHERE IT SITS
// ---------------------------------------------------------------------------------------------
// A dock of its own, along the right, like the log pane along the bottom and for the log pane's reason:
// it is about FILES, not about the document in front, so it is not a sidebar pane, and it outlives every
// tab. `Ctrl+Shift+F` -- a Global row since D5-15 -- shows it and puts the focus in its query, from the
// page, from a pane, from the find bar. With the focus anywhere in it §8's Find / Search table applies
// (`KeyContext::FindSearch`): `Enter` searches from the query and opens the current result from the list,
// `↑`/`↓` are the list's own, `F3`/`Shift+F3` step through the results, and `Esc` hides the pane and gives
// the focus back to the page. `Alt+R`, `Alt+W` and `Alt+C` are the find bar's toggles and the same
// settings; the content-class switches are Preferences' Search page. Every search reads them through
// `SearchQuery::fromSettings`, so the pane, the bar and Preferences cannot disagree.
//
// AND THE PANE SHOWS THE THREE (S5 guide I-20). Until 2026-10-03 it read them and showed none, so M-07's
// walk searched with regex off and nothing on screen said so. They are the find bar's buttons: the same
// labels, the same commands dispatched, checked from the settings and following them whoever changes
// them. A changed option RE-RUNS the pane's last search, as it re-runs the bar's -- the query under new
// flags is a new query, and a list left over from the old flags beside a button saying the new ones is
// the state I-20 was. A refine in progress is dropped by it: its first search no longer exists.
//
// ---------------------------------------------------------------------------------------------
// WHAT `ENTER` MEANS, AND WHAT THE LIST SAYS IT IS (the user's design, 2026-10-03; S5 guide I-21, I-22)
// ---------------------------------------------------------------------------------------------
// `Enter` NARROWS A LIST THAT EXISTS and searches when there is none. In the query, author, subject or date
// fields, with a finished list holding results: the filter fields are applied, and a query that differs
// from the last one the list was built from REFINES the list by it -- only the pages both found. With no
// list, or once the folder or *Include subfolders* has changed since the list was built, `Enter` searches
// anew; from the folder field it always does. From the list it opens the current result. Anywhere else -- a
// button, the sort box, a toggle -- it does nothing: until 2026-10-03 every `Enter` outside the query field
// opened the FIRST RESULT, so `Enter` in the author field opened a file (I-21). *Clear* empties the list and
// its refinements, and keeps every field, so the next `Enter` is a new search.
//
// THE LIST SAYS WHAT IT IS: a refined list names its chain of queries on the status line ("Refined:
// corpus → Hebrew"), and an opened result carries the LAST query of the chain -- the one whose hits the list
// holds. Until 2026-10-03 it carried the first, so the tab's find bar showed one query over another's
// highlights (I-22).
//
// ---------------------------------------------------------------------------------------------
// WHAT RUNS WHERE
// ---------------------------------------------------------------------------------------------
// The search itself is `core/text/FolderSearch` -- the UI thread opens nothing (guide A-88) -- and the
// list is `core/text/SearchResults`, which owns relevance, the sorts, the filter, refine and the CSV's
// bytes. This class shows them, and nothing it does reaches a document: activating a result asks the
// window to open the file and show the hit (`resultActivated`), which is `MainWindow::openFiles`'s
// path -- so a file already open is not opened twice.
//
// ---------------------------------------------------------------------------------------------
// THE EXPORT, AND ITS ONE WAIVER (D5-13)
// ---------------------------------------------------------------------------------------------
// The list is written as CSV to a file the person chose -- outside every directory `layer-lint` allows a
// write in, which is why there is ONE `allow-filesystem-write` waiver in `SearchPane.cpp`, planned and
// named, and the census moves 14 to 15 with it (guide A-09). It is a `QFile` opened with `WriteOnly |
// Truncate` on the waived line, not the `QSaveFile` the plan named: `#include <QSaveFile>` itself matches
// the rule, so `QSaveFile` costs two waivers where the plan allows one -- and a result list is a few
// kilobytes written at once. `exportTo` is the parameterized form; the button asks for a path only where
// there is somebody to ask (Stage 1's I-23).

#ifndef RAWPAGE_UI_PANES_SEARCHPANE_H
#define RAWPAGE_UI_PANES_SEARCHPANE_H

#include "app/Services.h"
#include "core/Result.h"
#include "core/text/FolderSearch.h"
#include "core/text/SearchResults.h"

#include <QDockWidget>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <set>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace rawpage::ui {

class SearchPane : public QDockWidget {
    Q_OBJECT

public:
    explicit SearchPane(app::Services services, QWidget* parent = nullptr);
    ~SearchPane() override;

    // `Ctrl+Shift+F`: shown, raised, the focus in the query with its text selected.
    void showAndFocus();
    bool hasFocusWithin() const;

    // ---- What its controls do, as calls -- for the commands and the cases ---------------------------
    void setFolder(const QString& folder);
    void setRecursive(bool recursive);
    void setQuery(const QString& query);
    // Searches the folder with the query; the error that refused it otherwise (no folder, an empty or an
    // invalid pattern), which the pane also says.
    core::Status search();
    // A second query over the files and pages the current results hold -- only pages carrying both.
    core::Status refine(const QString& query);
    void stop();
    // *Clear*: the list, its count and its refinements go; every field stays as typed.
    void clear();
    bool isSearching() const;
    // A finished list holding at least one result -- what `Enter` and *Refine* narrow.
    bool hasList() const;
    // The queries the list was built from: the search's, then each refinement's.
    QStringList queryChain() const { return chain_; }

    void setSort(core::text::SearchResults::Sort sort);
    void setFilter(const core::text::SearchResults::Filter& filter);
    // The list as CSV at `path` -- UTF-8 with a byte-order mark, one row per listed hit (D5-13).
    core::Status exportTo(const QString& path);

    // The commands' behaviour with the focus in the pane (`find.jumpToResult`, `find.next`,
    // `find.previous`, `find.close`). `jumpToCurrent` is `Enter`: see the header for what it means where.
    void jumpToCurrent();
    void selectNext();
    void selectPrevious();
    void closePane();

    // ---- What it holds ---------------------------------------------------------------------------
    const core::text::SearchResults& results() const noexcept { return results_; }
    std::vector<core::text::SearchResults::Row> listedRows() const { return rows_; }
    int currentRow() const;
    QString statusText() const;
    QLineEdit* queryField() const noexcept { return query_; }
    QTreeWidget* list() const noexcept { return list_; }
    // The three search options, shown as the find bar shows them (S5 guide I-20).
    QToolButton* regexToggle() const noexcept { return regex_; }
    QToolButton* wholeWordsToggle() const noexcept { return wholeWords_; }
    QToolButton* caseSensitiveToggle() const noexcept { return caseSensitive_; }
    // The pattern whose hits the list holds -- the search's, or the last refinement's -- which an opened
    // result is shown under (I-22).
    QString pattern() const noexcept { return pattern_; }
    QPushButton* clearButton() const noexcept { return clearButton_; }
    QPushButton* refineButton() const noexcept { return refineButton_; }

signals:
    // A result to show: the window opens the file -- or goes to its tab -- and shows the hit, highlighted.
    void resultActivated(const QString& path, const QString& pattern,
                         const std::vector<core::text::SearchHit>& hits, int current);
    // `Esc`: the window gives the focus back to the page.
    void closed();

protected:
    void showEvent(QShowEvent* event) override;

private:
    core::Status run(core::text::FolderSearch::Request request, bool refining);
    void onFile(core::text::FileResult file);
    void onFinished(const core::text::FolderSearch::Summary& summary);
    void rebuildList();
    void readFilter();
    void updateStatus();
    void activate(int row);
    void refreshToggles();
    void onSettingsChanged();
    // `Enter` in a field with a list: the filters applied, and a changed query refined by.
    void narrow();
    // Whether the folder or the subfolders box differs from what the list was searched with.
    bool scopeChanged() const;
    void refreshButtons();

    app::Services services_;
    std::unique_ptr<core::text::FolderSearch> folder_;

    QLineEdit* query_ = nullptr;
    QLineEdit* folderField_ = nullptr;
    QPushButton* browse_ = nullptr;
    QCheckBox* recursive_ = nullptr;
    QPushButton* searchButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QToolButton* regex_ = nullptr;
    QToolButton* wholeWords_ = nullptr;
    QToolButton* caseSensitive_ = nullptr;
    QComboBox* sort_ = nullptr;
    QLineEdit* author_ = nullptr;
    QLineEdit* subject_ = nullptr;
    QLineEdit* from_ = nullptr;
    QLineEdit* to_ = nullptr;
    QPushButton* refineButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QLabel* status_ = nullptr;

    core::text::SearchResults results_;
    // A refine runs into here, and is cut to the first search's pages when it finishes.
    core::text::SearchResults incoming_;
    std::map<QString, std::set<int>> refineTo_;
    bool refining_ = false;
    // The refinement running, joined to the chain when it finishes -- not before, so a stopped one is not
    // claimed.
    QString refiningBy_;
    QStringList chain_;
    // What the list was searched over: a change of either makes `Enter` a new search.
    QString listFolder_;
    bool listRecursive_ = true;
    std::vector<core::text::SearchResults::Row> rows_;
    core::text::SearchResults::Sort sortOrder_ = core::text::SearchResults::Sort::Relevance;
    core::text::SearchResults::Filter filter_;
    QString pattern_;
    QString error_;
    QString note_;
    bool limitReached_ = false;
    int filesDone_ = 0;
    int filesTotal_ = 0;
    // Whether a search has been asked for since the pane was built: a changed option re-runs only that.
    bool searched_ = false;
    // An option changed while the pane was hidden: its list is from the old flags, and is searched again
    // when the pane is shown.
    bool staleOptions_ = false;
    int settingsToken_ = 0;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_SEARCHPANE_H
