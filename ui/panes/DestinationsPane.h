// The Destinations pane -- `Features.md` §5.5, Stage 4 T6, decisions D4-2 and D4-3.
//
// ===========================================================================================
// WHY IT IS A STAGE 4 FILE AT ALL -- A CONTRADICTION BETWEEN TWO OWNING DOCUMENTS.
// ===========================================================================================
// `Implementation-Plan.md` § Stage 4 has always listed *"`ui/panes/DestinationsPane` (read-only)"*
// among this stage's components, while `Architecture-FileMap.md` §3.15 tagged the file and its
// model ○ (S7). One of them had to be wrong. **D4-3 settled it for Stage 4**: a named-destination
// list is a *navigation* surface and navigation is this stage, so both rows were re-tagged
// ○ (S7) → ○ (S4 read, S7 edit) when the plan was written, and this file is the read half. Creating,
// renaming, deleting and repairing destinations after pages move is Stage 7's, with the commands
// that make each undoable.
//
// ===========================================================================================
// IT HAS NO §8 TABLE, AND THAT IS WHY IT DECLARES NO KeyContext.
// ===========================================================================================
// `core/cmd/KeyContext.h` says its list *"is NOT invented here"* -- it is exactly the set of tables
// in `Features.md` §8, and the two are asserted to agree by `tst_keybindings`. §8 has a Page
// Thumbnails table and a Bookmarks table; it has **no Destinations table**. So this pane declares
// nothing, `activeContext()` answers `Global` above it, and `Enter` reaches the widget rather than
// a command -- which is the honest arrangement and not a gap. Inventing a `Destinations` context
// here would create a context nothing can bind to, and adding a table to §8 is a binding change
// that this stage explicitly does not make (plan §10: *"`Features.md` §8 is not touched by this
// stage"*).
//
// The same fact explains the pane's opening: **`pane.destinations` has no default chord.** §8's
// Global table binds nine `Ctrl+Shift+*` panes and this is not one of them, so the command is
// reachable from the sidebar's chooser, the ribbon, the tool finder and the batch runner, and from
// no keystroke. Recorded rather than quietly given one.
//
// ===========================================================================================
// AN UNRESOLVED DESTINATION IS SHOWN, NOT DROPPED.
// ===========================================================================================
// `corpus/ordinary/dests-named.pdf` (T1) carries one destination that names a page which does not
// exist, deliberately, as the control -- and `core::Destinations::unresolvedCount()` exists so a
// surface can say so. This pane lists it with its name, marks it unresolvable, and refuses to
// activate it. A destination silently missing from the list is worse than one that says it goes
// nowhere: the first looks like a document with fewer destinations than it has.
//
// ===========================================================================================
// WHAT NAVIGATING DOES, AND WHAT IT DOES NOT (convention 19).
// ===========================================================================================
// The same bound `ui/panes/BookmarksPane` carries, for the same reason and with the same owner: it
// goes to the destination's **page**, and does not yet apply `/XYZ`'s point and zoom, `/FitH`'s top
// or `/FitR`'s rectangle. `core::Destination` carries all of them and nothing here discards them.
// See **I-05** in `Stage4-ViewingSurface-Testing-Guide.md` §8; the arithmetic that would apply them
// is **T12**'s.

#ifndef RAWPAGE_UI_PANES_DESTINATIONSPANE_H
#define RAWPAGE_UI_PANES_DESTINATIONSPANE_H

#include "core/backend.h"
#include "core/doc/Destinations.h"

#include <QTreeWidget>

#include <cstddef>

namespace rawpage::ui {

class DestinationsPane : public QTreeWidget {
    Q_OBJECT

public:
    explicit DestinationsPane(QWidget* parent = nullptr);
    ~DestinationsPane() override;

    // `destinations` is the backend's, hydrated at open and owned by it; null empties the pane. An
    // EMPTY table is not null and is not an error -- most documents have no named destinations.
    void setDocument(core::DocumentHandle document, const core::Destinations* destinations);

    core::DocumentHandle document() const noexcept { return document_; }

    void goToSelected();

    // ---- What a case can assert, rather than a screenshot ------------------------------------
    // Every row, resolvable or not -- the count that must equal the model's, because the whole
    // point of the unresolved row is that it is PRESENT.
    int rowCount() const;
    int unresolvedRowCount() const;

    // The name in row `index` as the tree currently ORDERS it, which is what makes the sort
    // assertable: a case sorts, then reads the order back through the surface rather than through
    // the model it was built from.
    QString nameAt(int index) const;

    // WHAT THE PAGE COLUMN SAYS -- the page number, or the unresolvable text. It exists because
    // `unresolvedRowCount()` above reads the MODEL, and a planted-bug control proved that is not
    // the same claim: marking every row unresolvable on screen left that count at 1 and the case
    // green (convention 22 -- a case proving a mechanism fires is half of proving it fires when it
    // should). A case that means *the reader can see which one goes nowhere* must read this.
    QString pageTextAt(int index) const;

    QString selectedName() const;
    core::Destination selectedDestination() const;

signals:
    void activated(core::Destination destination);

private:
    core::DocumentHandle document_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_DESTINATIONSPANE_H
