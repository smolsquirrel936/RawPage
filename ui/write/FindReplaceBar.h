// ui/write/FindReplaceBar -- the find toolbar: the FIND HALF at Stage 5 T7, decision D5-14
// (`Stage5-TextSearch-Plan.md` §3). Replace -- and with it the rest of `ui/write/` -- is Stage 13's.
//
// ---------------------------------------------------------------------------------------------
// WHY IT LIVES IN `ui/write/` A STAGE AND A HALF EARLY
// ---------------------------------------------------------------------------------------------
// `Architecture-FileMap.md` §3.15 gives the find toolbar ONE file for both modes: Write mode's find is this
// bar with replace switched on. Putting the find half somewhere else now would put a Stage 13 rename on the
// critical path, so the file is at its own path and the directory holds nothing else yet. Nothing in it
// mutates (D5-1): a search is view state, and the bar has no replace field at all until Stage 13.
//
// ---------------------------------------------------------------------------------------------
// ONE PER DOCUMENT WINDOW
// ---------------------------------------------------------------------------------------------
// It searches the document of the tab it sits in -- that tab's `core/text/DocumentIndex` entry -- and
// paints through that tab's `SelectionOverlay`, which every pane of a split shares (guide A-75's "hits in a
// split"). Two tabs keep two searches. It declares `KeyContext::FindSearch`, so with the focus anywhere in
// it §8's Find / Search table applies; `Ctrl+F` reaches it from everywhere else through §8's Common row.
//
// ---------------------------------------------------------------------------------------------
// INCREMENTAL, DEBOUNCED, CANCELLED (guide A-70)
// ---------------------------------------------------------------------------------------------
// It searches as the user types: each edit restarts a `kDebounceMs` timer, and the search starts when the
// typing pauses -- so `lorem` typed in one burst is ONE search, not five. A search started while another
// runs cancels it through `core::text::SearchSession`, after the new one has asked for its pages, so the
// index build both want carries on. `Enter` starts a pending search at once.
//
// ---------------------------------------------------------------------------------------------
// THE TOGGLES ARE THE SETTINGS (D5-14, Stage 4's D4-5)
// ---------------------------------------------------------------------------------------------
// The three toggle buttons dispatch `find.toggleRegex`, `find.toggleWholeWords` and
// `find.toggleCaseSensitive` -- the commands `Alt+R`, `Alt+W` and `Alt+C` dispatch -- and those write
// `search/useRegex`, `search/wholeWords` and `search/caseSensitive`. The bar keeps NO copy: it subscribes
// to the Search group and shows what the settings say, so the bar and Preferences cannot disagree (A-73),
// and a changed flag re-runs the search. Every button dispatches through `ActionRegistry` -- nothing here
// is wired to a slot (the action layer's rule) -- and none carries a `QAction`, so convention 23's
// re-connecting default action has nothing to re-connect.
//
// ---------------------------------------------------------------------------------------------
// WHAT IT SAYS
// ---------------------------------------------------------------------------------------------
// The count (`3 of 27`), the progress of a first search, *no matches*, a wrap past either end, the bound
// when `search/maxResults` was reached (A-61), and -- always, when there are any -- how many glyphs in
// scope have no Unicode text and so could not be searched (A-21's find half). **An invalid regular
// expression is the error itself, and nothing is searched** (A-57): an empty list would read as *not found*.
//
// THE OTHER FOUR CONTENT CLASSES (T8, D5-12) are searched when their `search/include*` settings say so --
// Preferences' Search page, which the bar follows like the three toggles. A hit in one of them is stepped
// to like any other and navigates as D5-12 says: a comment or a field to its page and rectangle, a bookmark
// to its destination (`destinationActivated`), and **an attachment hit is selected and opens nothing** --
// the status line names the file, and opening it is Stage 6's Attachments pane. The status line says which
// class the current hit is in, because a highlight on a field's box does not say it matched the field's NAME.
//
// WHAT IT DOES NOT DO: stream hits while the index builds -- a first search shows its progress and its hits
// arrive when it finishes (1.7 s for 900 dense pages, guide A-63).
//
// ---------------------------------------------------------------------------------------------
// IT NEVER WIDENS THE WINDOW (Stage 5 guide I-10, T14)
// ---------------------------------------------------------------------------------------------
// Until T14 every control sat in one row that could not shrink -- three toggles labelled as their settings
// are, *Highlight all*, the scope box, the buttons -- so the bar's minimum width was its whole row: 1,079
// logical pixels on Windows, and opening it raised the main window's minimum from 468 to 1,102. Qt then
// WIDENED the window to fit: measured on the `windows` platform, a 900-pixel window became 1,102 at
// `Ctrl+F`, which cannot be snapped to half of a 1920 screen and, at 175 % on 1920x1080, is wider than the
// screen. T14's bench found it, because its offscreen window could not be made A2's size with the bar open.
//
// **The options COLLAPSE, the way a ribbon group does** (`ui/shell/Ribbon.h`'s `RibbonGroup`): the three
// toggles, *Highlight all*, the scope and the page range live in one container that sits in the row when
// the bar has room and is RE-PARENTED into a popup behind a *More* button when it does not -- the same
// widgets, never a second copy, so the toggles stay the settings either way. **The bar's minimum width is
// therefore its essentials** -- the field, Previous, Next, More, Close -- and `minimumSizeHint` says so in
// both states; a hysteresis band keeps it from flapping at the boundary. In the popup the options stack
// vertically; `Alt+R`, `Alt+W` and `Alt+C` reach them there as anywhere in the bar, and `Esc` closes the
// popup before a second `Esc` closes the bar. The status line may clip when narrow, and its tooltip holds
// the whole of it.

#ifndef RAWPAGE_UI_WRITE_FINDREPLACEBAR_H
#define RAWPAGE_UI_WRITE_FINDREPLACEBAR_H

#include "app/Services.h"
#include "core/Geometry.h"
#include "core/doc/Destinations.h"
#include "core/text/DocumentIndex.h"
#include "core/text/SearchEngine.h"

#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QBoxLayout;
class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QResizeEvent;
class QToolButton;

namespace rawpage::ui {

class SelectionOverlay;

class FindReplaceBar : public QWidget {
    Q_OBJECT

public:
    // How long the typing must pause before a search starts.
    static constexpr int kDebounceMs = 200;

    explicit FindReplaceBar(app::Services services, QWidget* parent = nullptr);
    ~FindReplaceBar() override;

    // The document it searches -- its index entry, 0 for none -- and the overlay it paints through.
    void setDocument(core::text::DocumentIndex::EntryId entry, int pageCount, SelectionOverlay* overlay);
    // Which page the reader is on, so a search's current hit is the first at or after it.
    void setCurrentPageProvider(std::function<int()> provider) { currentPage_ = std::move(provider); }

    // ---- The five things its commands do ------------------------------------------------------------
    void open();             // `find.find`: shown, focused, its text selected
    void close();            // `find.close`: hidden, the search cancelled, every highlight cleared
    void findNext();         // `find.next`, wrapping past the last hit and saying so
    void findPrevious();     // `find.previous`, wrapping past the first
    void jumpToCurrent();    // `find.jumpToResult`: a pending search now, then the current hit revealed
    // S5 T9: hits found ELSEWHERE -- an Advanced Search result opened in this tab -- shown as this bar's own:
    // the bar open with `pattern` in its field, every hit highlighted, `current` revealed, and `F3` stepping
    // on from it. Nothing is searched again; a later edit of the field searches as usual.
    void presentHits(const QString& pattern, const std::vector<core::text::SearchHit>& hits, int current);

    bool isOpen() const noexcept { return isVisible(); }

    // ---- What it holds, for the cases and the status line ------------------------------------------
    enum class Scope { Document, Selection, Pages };
    void setScope(Scope scope);
    Scope scope() const noexcept;
    void setPageRange(const QString& range);
    bool highlightAll() const noexcept;
    void setHighlightAll(bool all);

    const std::vector<core::text::SearchHit>& hits() const noexcept { return hits_; }
    int currentIndex() const noexcept { return current_; }
    bool isSearching() const;
    bool isSearchPending() const noexcept { return debounce_.isActive(); }
    QString statusText() const;
    int searchesStarted() const noexcept { return started_; }
    int searchesFinished() const noexcept { return finished_; }

    QLineEdit* field() const noexcept { return field_; }

    // ---- The collapse (guide I-10) ----------------------------------------------------------------
    // Whether the options are behind *More* rather than in the row; the popup that holds them then; the
    // button; and the width at which they come back into the row.
    bool isCollapsed() const noexcept { return collapsed_; }
    QWidget* optionsPopup() const noexcept { return popup_; }
    QToolButton* moreButton() const noexcept { return more_; }
    int expandedWidth() const;
    // The band, in logical pixels, a collapsed bar must clear before the options come back -- the ribbon's.
    static constexpr int kHysteresis = 16;

    // THE ESSENTIALS, in either state -- which is what keeps the bar from setting the window's minimum.
    QSize minimumSizeHint() const override;

signals:
    // The document window reveals it in the pane a person is in.
    void hitActivated(int page, core::PdfRect rect);
    // A bookmark hit (T8): the document window goes to the bookmark's destination, as the Bookmarks pane does.
    void destinationActivated(const core::Destination& destination);
    // Closed by `Esc` or its button: the window gives the focus back to the page.
    void closed();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    // The width of the row without the options: margins, the field's minimum, Previous, Next, the status
    // line's `statusRoom` and Close, and the gaps between them.
    int essentialsWidth(int statusRoom) const;
    // The options' width laid out in a ROW, whichever way they are laid out now.
    int optionsRowWidth() const;
    void updateCollapse();
    void setCollapsed(bool collapsed);
    void showOptions();
    void restoreTabOrder();

    void startSearch();
    void onProgress(const core::text::SearchEngine::Progress& progress);
    void onFinished(const core::text::SearchEngine::Outcome& outcome);
    void onSettingsChanged();
    void refreshToggles();
    void refreshButtons();
    void showCurrent(bool reveal);
    void clearResults();
    void updateStatus();
    void dispatch(core::ActionId id);

    app::Services services_;
    core::text::SearchSession session_;
    QTimer debounce_;
    int settingsToken_ = 0;

    core::text::DocumentIndex::EntryId entry_ = 0;
    int pageCount_ = 0;
    QPointer<SelectionOverlay> overlay_;
    std::function<int()> currentPage_;

    QHBoxLayout* row_ = nullptr;
    QWidget* options_ = nullptr;          // the toggles, Highlight all, the scope and the range
    QBoxLayout* optionsLayout_ = nullptr; // a row in the bar, a column in the popup
    QWidget* popup_ = nullptr;            // holds `options_` while collapsed
    QToolButton* more_ = nullptr;         // shown while collapsed; opens `popup_`
    bool collapsed_ = false;

    QLineEdit* field_ = nullptr;
    QToolButton* previous_ = nullptr;
    QToolButton* next_ = nullptr;
    QToolButton* regex_ = nullptr;
    QToolButton* wholeWords_ = nullptr;
    QToolButton* caseSensitive_ = nullptr;
    QCheckBox* highlightAll_ = nullptr;
    QComboBox* scope_ = nullptr;
    QLineEdit* range_ = nullptr;
    QLabel* status_ = nullptr;
    QToolButton* close_ = nullptr;

    // The last search's results.
    std::vector<core::text::SearchHit> hits_;
    int current_ = -1;
    bool searched_ = false;            // a search has finished since the text last changed
    bool jumpWhenDone_ = false;        // `Enter` arrived before the search finished
    QString error_;                    // an invalid pattern, or a scope that cannot be searched
    QString note_;                     // a wrap, said once
    bool limitReached_ = false;
    int unmappedGlyphs_ = 0;
    int unmappedPages_ = 0;
    int failedPages_ = 0;
    int progressDone_ = 0;
    int progressTotal_ = 0;
    int started_ = 0;
    int finished_ = 0;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_WRITE_FINDREPLACEBAR_H
