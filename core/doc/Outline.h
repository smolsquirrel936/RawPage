// core/doc/Outline -- the document outline, what a reader calls bookmarks. Stage 4 T4 (decision D4-3,
// `Stage4-ViewingSurface-Plan.md` §3); the EDITING half Stage 7 T11 (D7-10, `Stage7-Assembly-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// TWO READINGS, HELD TO AGREE (D7-10)
// ---------------------------------------------------------------------------------------------
// Stage 4 read the outline through PDFium alone, which gives titles and destinations and no object
// identity -- and editing needs each item's object, so its unknown keys survive and the items not edited
// are not rewritten (D6-5's rule). Since T11 `pdf/ops/OutlineReader` reads it first-party too, and the
// backend compares the two per document: the same count, depth, titles and target pages, item by item in
// order, or the outline is READ-ONLY AND SAYS WHY -- `status()` carries the refusal, and the panes show
// the engine's reading (D6-3's rule). An outline whose readings agree is the first-party one, ids and all.
//
// ---------------------------------------------------------------------------------------------
// AN ITEM IS ITS IDENTITY
// ---------------------------------------------------------------------------------------------
// An item the file holds is named by its OBJECT NUMBER; one RawPage made is named by a KEY for its whole
// life -- before a save and after one -- exactly as a created annotation is named by its `/NM` (D6-4). A
// command names items by id and positions by (parent, index), so the journal can replay it (D7-24).
//
// ---------------------------------------------------------------------------------------------
// WHAT AN ITEM DOES, AND WHERE IT GOES
// ---------------------------------------------------------------------------------------------
// `action` is what the item does, as the file writes it: a destination in this document (`GoTo`), a named
// one (`GoToNamed`), a page of another file, a file to open, a web address, a script, a menu command, a
// form to submit -- or a kind this build does not model (`Other`), which is kept exactly as written.
// **Written, never run** (D7-12): nothing here or anywhere in the product executes an action.
// `destination` is where `Enter` goes: the page and view a `GoTo` or a resolved `GoToNamed` names, and
// unresolved for every other kind. It is the field a pane navigates by and a page command remaps.
//
// A PAGE COMMAND REMAPS THE INDICES (D7-3), and a page it removes leaves the item's destination
// UNRESOLVED while its action keeps its kind -- what `ReferenceRepair` writes is the file's business, and
// the model keeps what the item meant, so the undo of the removal puts the index back.
//
// ---------------------------------------------------------------------------------------------
// THE OPEN FLAG IS A DOCUMENT'S REQUEST, AND IT IS NOT THE PANE'S STATE
// ---------------------------------------------------------------------------------------------
// `/Count` in an outline item says how the author wanted the item to appear when the document is
// first shown: positive means expanded, negative means collapsed (ISO 32000-1 Table 153). It is a
// STARTING state, not a live one -- once a person expands a node, that is the pane's business and
// belongs in the widget or in `SessionState`, never here. This model is what the file says, and a
// command that sets it (`EditBookmarkCommand`) sets what the file says.
//
// ---------------------------------------------------------------------------------------------
// A TREE FROM A FILE IS NOT NECESSARILY A TREE
// ---------------------------------------------------------------------------------------------
// `/First`, `/Next` and `/Parent` are just references, and a damaged or hostile file can point them
// in a circle. Both readers are therefore bounded -- by depth and by total node count -- and so is every
// outline a command or an import makes (`validate`), because a reader that walks a cycle does not fail:
// it hangs, with a progress bar, on a document that opens fine in every other reader.

#ifndef RAWPAGE_CORE_DOC_OUTLINE_H
#define RAWPAGE_CORE_DOC_OUTLINE_H

#include "core/Result.h"
#include "core/doc/Destinations.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rawpage::core {

class PageMap;

// THE BOUNDS, shared by both readers, every command and every import. Depth is the nesting a real outline
// reaches many times over; the node cap is far above any real document's outline and far below a number
// that takes noticeable time on the UI thread.
inline constexpr int kMaxOutlineDepth = 32;
inline constexpr std::size_t kMaxOutlineNodes = 20000;
inline constexpr qsizetype kMaxOutlineTitleLength = 4096;
inline constexpr qsizetype kMaxOutlineTextLength = 65536;   // a URI, a file name, a script

struct OutlineItemId {
    long long object = -1;   // an item the file holds: its object number
    QString created;         // an item RawPage made: its key, kept for its life (D6-4's rule)

    static OutlineItemId ofObject(long long number);
    static OutlineItemId ofCreated(QString key);

    bool isValid() const noexcept { return object > 0 || !created.isEmpty(); }
    bool isCreated() const noexcept { return !created.isEmpty(); }

    // `o:<number>` or `c:<key>` -- the form a command carries. `fromString` refuses anything else: a key of
    // 1 ... 64 characters of `[0-9a-z-]`, a number of 1 ... 2^31.
    QString toString() const;
    static std::optional<OutlineItemId> fromString(const QString& text);

    friend bool operator==(const OutlineItemId& a, const OutlineItemId& b)
    {
        return a.object == b.object && a.created == b.created;
    }
    friend bool operator!=(const OutlineItemId& a, const OutlineItemId& b) { return !(a == b); }
    friend bool operator<(const OutlineItemId& a, const OutlineItemId& b)
    {
        return a.object != b.object ? a.object < b.object : a.created < b.created;
    }
};

struct OutlineAction {
    enum class Kind : std::uint8_t {
        None,         // the item goes nowhere -- a heading, or a destination a page command cleared
        GoTo,         // `destination`, in this document
        GoToNamed,    // the named destination `name`
        GoToRemote,   // page `destination.pageIndex` (0-based) of the file `file`
        Launch,       // open the file `file`
        Uri,          // open the web address `uri`
        JavaScript,   // `script` -- written, never run (D7-12)
        Named,        // the menu command `name` (`Print`, `NextPage` ...)
        SubmitForm,   // submit the form to `uri`
        // Stage 7 T12 (D7-12): the other form actions a link carries -- written, never run.
        ResetForm,    // reset the form's fields, every one (a `/Fields` list is the file's, kept while unchanged)
        ImportData,   // import the form data in the file `file`
        Hide,         // hide the field `name` -- or show it, when `hide` is false
        Other,        // a kind this build does not model, kept exactly as the file writes it
    };

    Kind kind = Kind::None;
    Destination destination;
    QString name;
    QString file;
    QString uri;
    QString script;
    bool hide = true;   // Hide: `/H`, true unless the action SHOWS the field

    friend bool operator==(const OutlineAction& a, const OutlineAction& b)
    {
        return a.kind == b.kind && a.destination == b.destination && a.name == b.name && a.file == b.file
               && a.uri == b.uri && a.script == b.script && a.hide == b.hide;
    }
    friend bool operator!=(const OutlineAction& a, const OutlineAction& b) { return !(a == b); }

    QJsonObject toJson() const;
    // `carried` (Stage 7 T12): an ANNOTATION carries the file's action as read, `Other` included -- a link recoloured
    // keeps an action this build cannot write, and its command must carry it to be replayed. A bookmark command never
    // carries one, so the default refuses it.
    static Result<OutlineAction> fromJson(const QJsonObject& json, bool carried = false);
    // A destination in this document -- what *from the current view* makes.
    static OutlineAction goTo(const Destination& destination);
};

// ASCII and stable: the token a command, an export and a log hold.
const char* outlineActionKindName(OutlineAction::Kind kind) noexcept;
std::optional<OutlineAction::Kind> outlineActionKindFromName(const QString& name) noexcept;
std::optional<Destination::Fit> destinationFitFromName(const QString& name) noexcept;
QJsonObject destinationToJson(const Destination& destination);
Result<Destination> destinationFromJson(const QJsonObject& json);

struct OutlineItem {
    OutlineItemId id;
    QString     title;
    Destination destination;   // where `Enter` goes -- see the header
    OutlineAction action;

    // `/C`, an RGB colour in 0 ... 1; none is the reader's default, black.
    std::optional<std::array<double, 3>> colour;
    bool italic = false;   // `/F` bit 1
    bool bold = false;     // `/F` bit 2

    // What the FILE asks for, not what the pane is doing. See the header.
    bool openByDefault = false;

    std::vector<OutlineItem> children;

    // Every field but the children -- what an edit of one item changes.
    bool sameProperties(const OutlineItem& other) const;
    // The item and its subtree, as a command carries them: `{"id", "title", "action", "colour", "italic",
    // "bold", "open", "children"}`. `fromJson` is a parser of hostile input (a journal line), bounded.
    QJsonObject toJson() const;
    static Result<OutlineItem> fromJson(const QJsonObject& json, int depth = 0);
};

// Where an item sits: under `parent` (invalid: the top level) at `index`.
struct OutlinePosition {
    OutlineItemId parent;
    int index = 0;
    friend bool operator==(const OutlinePosition& a, const OutlinePosition& b)
    {
        return a.parent == b.parent && a.index == b.index;
    }
};

class Outline {
public:
    std::vector<OutlineItem>&       roots() noexcept { return roots_; }
    const std::vector<OutlineItem>& roots() const noexcept { return roots_; }

    bool isEmpty() const noexcept { return roots_.empty(); }

    // Every node, at every depth. A pane sizes itself from this; a case asserts the shape with it.
    std::size_t nodeCount() const;

    // 0 for an empty outline, 1 for a flat list. `outline-labels.pdf` is 2.
    int depth() const;

    // WHETHER IT MAY BE EDITED (D7-10): success, or the refusal -- the two readings disagree, or nothing read
    // it. A default outline is an assessed, empty one; `refused` is one that says why it is read-only.
    const Status& status() const noexcept { return status_; }
    static Outline refused(Error why, std::vector<OutlineItem> roots = {});
    void setStatus(Status status) { status_ = std::move(status); }

    // ---- The editing half (Stage 7 T11) ----------------------------------------------------------------
    const OutlineItem* find(const OutlineItemId& id) const;
    OutlineItem* find(const OutlineItemId& id);
    std::optional<OutlinePosition> positionOf(const OutlineItemId& id) const;
    // The list `parent`'s children are in -- the top level for an invalid id; null for an id not here.
    std::vector<OutlineItem>* childrenOf(const OutlineItemId& parent);
    const std::vector<OutlineItem>* childrenOf(const OutlineItemId& parent) const;
    // True when `id` is `ancestor` or lies under it.
    bool isWithin(const OutlineItemId& id, const OutlineItemId& ancestor) const;
    // Every item, depth first, parents before children -- the order both readers are compared in.
    std::vector<const OutlineItem*> preorder() const;
    // The depth of `id`, 1 at the top level; 0 for an id not here.
    int depthOf(const OutlineItemId& id) const;

    // Inserts `items` under `parent` at `index` (0 ... its child count). Refused, changing nothing: a parent not
    // here, an index out of range, an id already here or invalid, or a result past the bounds.
    Status insert(const OutlinePosition& at, std::vector<OutlineItem> items);
    // Removes `id` with its subtree and returns it; refused, changing nothing, for an id not here.
    Result<OutlineItem> remove(const OutlineItemId& id);

    // D7-3: every destination named by page index carried through `map` -- unresolved where its page was removed
    // (see the header). A remote page is another file's and is not touched.
    void applyPageMap(const PageMap& map);

    // The bounds and the ids -- unique, valid -- of the whole tree. What a command checks an outline it is
    // handed against, and an import checks what it read against.
    Status validate() const;

    // Every item, every field, in order -- `Document::modelHash`'s input.
    QByteArray digest() const;

private:
    std::vector<OutlineItem> roots_;
    Status status_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_OUTLINE_H
