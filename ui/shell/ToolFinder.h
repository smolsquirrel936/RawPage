// The tool finder -- "a search box that locates any command by name and runs it"
// (`Features.md` §1.1), reached with `Ctrl+Shift+U` once T13 wires chords.
//
// ---------------------------------------------------------------------------------------------
// THE RANKING IS A FREE FUNCTION OVER THE REGISTRY, AND THE WIDGET IS A VIEW OF IT.
//
// `toolfinder::rank()` names no widget. That is what lets guide case A-56 assert the ORDER of the
// results -- exact before prefix before word-start before subsequence -- without a window, a line
// edit, or a list view, and it is the same split `RibbonModel` made at T10 for the same reason: a
// rule that can only be checked through a widget is a rule that gets checked once.
//
// ---------------------------------------------------------------------------------------------
// A DECLARED-BUT-UNREGISTERED COMMAND IS SHOWN, DISABLED. IT IS NOT HIDDEN.
//
// Stage 1 declares 358 commands and registers seventeen. Hiding the rest would make the finder
// disagree with the ribbon the user is looking at -- the ribbon shows those commands greyed with a
// tooltip saying they are not built yet, and a search that cannot find a button the user can see is
// a search they stop trusting. So they rank, they appear, and they are marked. (Guide case A-56.)
//
// ---------------------------------------------------------------------------------------------
// EACH RESULT CARRIES ITS CURRENT CHORD, FROM `KeyBindingStore` -- NEVER FROM A LIST HERE.
//
// The chord shown is the chord that would fire, remaps included, which is the whole value of
// showing one: a user who has remapped `Ctrl+K` needs the finder to say so. `Features.md` §8 is the
// source of the defaults and the store is the source of the overrides, and neither of them is here.

#ifndef RAWPAGE_UI_SHELL_TOOLFINDER_H
#define RAWPAGE_UI_SHELL_TOOLFINDER_H

#include "core/cmd/ActionId.h"
#include "core/cmd/KeyContext.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QLineEdit;
class QListWidget;

namespace rawpage::core {
class ActionRegistry;
class KeyBindingStore;
}

namespace rawpage::ui {

namespace toolfinder {

// Ordered by quality, best first. The enum's ORDER is the ranking -- a comparison sorts on it
// directly, so adding a kind in the middle changes the ranking by construction rather than by
// somebody remembering to update a score table as well.
enum class MatchKind {
    Exact,        // the label, case-folded, is the query
    Prefix,       // the label starts with the query
    WordStart,    // a word inside the label starts with the query -- "page" in "Rotate Page"
    Subsequence,  // the query's letters appear in order: "rtpg" finds "Rotate Page"
};

struct Match {
    core::ActionId id{};
    QString        label;
    QString        chord;        // empty when the command has no binding in this context
    bool           registered = false;   // false: declared, not built yet -- shown disabled
    bool           enabled = false;      // registered AND its enablement predicate says yes
    MatchKind      kind = MatchKind::Subsequence;
};

// `limit` bounds the list a popup has to render; 0 means unbounded, which is what a test asking
// "does this command rank at all" wants. Ties inside a kind break on the shorter label first and
// then alphabetically, so the order is total and a case can assert it.
std::vector<Match> rank(const QString& query, const core::ActionRegistry& registry,
                        const core::KeyBindingStore& bindings, core::KeyContext context,
                        int limit = 20);

} // namespace toolfinder

class ToolFinder : public QWidget {
    Q_OBJECT

public:
    ToolFinder(core::ActionRegistry& registry, core::KeyBindingStore& bindings,
               QWidget* parent = nullptr);
    ~ToolFinder() override;

    // The context every search is ranked and dispatched in. A callback for the same reason
    // `ActionFactory`'s is: T13 replaces it with `ShortcutBinder::activeContext()` and touches
    // nothing else here.
    using ContextProvider = std::function<core::KeyContext()>;
    void setContextProvider(ContextProvider provider) { context_ = std::move(provider); }

    void setQuery(const QString& query);
    QString query() const;

    const std::vector<toolfinder::Match>& results() const { return results_; }

    // Runs the result at `index`. Returns false when the index is out of range or the command is
    // not runnable -- an unregistered hit is displayed, and choosing it is a no-op that says why
    // rather than a dispatch that fails silently somewhere below.
    bool activate(int index);

    void focusSearchBox();

    // Puts the result list away. `Esc`, a click outside it, and the window going inactive all end
    // here -- see the comment in the .cpp on why a list that cannot be dismissed is not a cosmetic
    // problem (issue I-44).
    void dismissResults();
    bool resultsVisible() const;

    QLineEdit* searchBox() const { return search_; }

protected:
    // The result list is a `Qt::Popup` WINDOW, which grabs the mouse for the whole application while
    // it is up. That is what a popup is for -- and it is also why this class must guarantee the list
    // can always be put away. Filtered rather than subclassed: the behaviour belongs to the finder,
    // not to a list widget, and a QListWidget subclass would be a second file to look in.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();

    core::ActionRegistry&  registry_;
    core::KeyBindingStore& bindings_;
    ContextProvider        context_;

    QLineEdit*   search_ = nullptr;
    QListWidget* list_ = nullptr;

    std::vector<toolfinder::Match> results_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_TOOLFINDER_H
