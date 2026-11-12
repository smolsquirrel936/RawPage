// pdf/ops/OutlineReader -- the document outline, read first-party. Stage 7 T11, decision D7-10
// (`Stage7-Assembly-Plan.md` §3).
//
// -------------------------------------------------------------------------------------------
// WHY A SECOND READER, AND WHAT KEEPS IT HONEST
// -------------------------------------------------------------------------------------------
// PDFium reads the outline for the panes (Stage 4's D4-3), and it gives titles and destinations but no
// object identity -- and editing an item means rewriting ITS dictionary with its unknown keys kept, and
// leaving every other item's alone (D6-5's rule). So this reads the same tree from the COS layer, every
// item named by its object, and the backend holds the two readings to agree item by item -- count, depth,
// titles and target page (`agreesWith`) -- or the outline is read-only and says why (D6-3's rule). It
// therefore reads what PDFium reads, the way PDFium reads it: `/Dest` before `/A`, a destination named by
// a string or a name looked up in `/Names /Dests` or the catalog's `/Dests`, a GoTo's or a GoToR's `/D`,
// a page named by an integer taken as that index, `/Count` above zero as open, and a title's characters
// at or below U+0020 compared as spaces (PDFium's `CPDF_Bookmark::GetTitle`).
//
// -------------------------------------------------------------------------------------------
// WHAT IT GIVES, AND WHAT IT LEAVES TO ITS CALLER
// -------------------------------------------------------------------------------------------
// The tree as `core::Outline` items -- ids, titles, actions, colour, style, open -- with every destination's
// PAGE left as the page OBJECT it names (`Facts::page`), because only the caller knows where that object is
// drawn: the backend at open, by the tree it opened, and the sink at every edit, by the order it holds
// (`resolve`). Per item, the dictionary as read and its five links, which is what lets the sink write an
// item only where what it composes differs (D7-10: *items not edited are not rewritten*).
//
// BOUNDED, as both readers are (`core/doc/Outline.h`): a visited set, `kMaxOutlineDepth` and
// `kMaxOutlineNodes`. A walk cut short says so (`truncated`), and an outline read short is read-only -- an
// edit of a tree read in part would write the part it did not read out of the document.

#ifndef RAWPAGE_PDF_OPS_OUTLINEREADER_H
#define RAWPAGE_PDF_OPS_OUTLINEREADER_H

#include "core/doc/Outline.h"
#include "pdf/cos/CosObject.h"
#include "pdf/ops/ReferenceRepair.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rawpage::pdf::ops {

class OutlineReader {
public:
    // What one item names, beyond what the model holds.
    struct Facts {
        long long page = -1;         // a GoTo's page object; -1 for none
        int index = -1;              // a GoTo naming its page by integer (a producer's error PDFium reads); -1 none
        std::string name;            // a GoToNamed's name, as bytes
        bool nameIsString = true;    // ... and whether it was written as a string (a name otherwise)
        // The destination's target as PDFium resolves it for the panes, before the caller places it: a page object,
        // or an index (an integer page, a remote GoToR's page). `engineIndex` wins when it is set.
        long long enginePage = -1;
        int engineIndex = -1;
        cos::CosObject dictionary;   // as read, spans cleared
        // The links as read: each a referenced number, or -1.
        long long parent = -1, first = -1, last = -1, next = -1, prev = -1;
        std::optional<long long> count;   // `/Count` as written, when it is an integer
    };

    struct Reading {
        long long root = -1;                 // `/Outlines`' object; -1 for none, or a direct one
        std::optional<cos::CosObject> rootDictionary;
        core::Outline outline;               // ids the object numbers; every destination unresolved (-1)
        std::map<long long, Facts> facts;    // by item object
        bool truncated = false;              // a cycle, a bound, or a link that does not fetch
    };

    // Every item reachable from the catalog's `/Outlines`, read through `view` (a ledger put back over the disk).
    static Reading read(const ReferenceRepair::View& view);

    // ONE item by its number -- one an edit took out of the tree, and an undo puts back: its own fields, its links
    // ignored. Nullopt for a number that does not fetch as a dictionary.
    static std::optional<std::pair<core::OutlineItem, Facts>> readItem(const ReferenceRepair::View& view, long long number,
                                                                       const std::map<std::string, cos::CosObject>& named);

    // Each destination of `reading`'s outline placed: a page object's index from `indexOf` (-1 for one not there),
    // an integer as itself -- the model's view (`destination` and a GoTo's `action.destination`).
    static void resolve(Reading& reading, const std::map<long long, int>& indexOf);
    // The same for one item.
    static void resolveItem(core::OutlineItem& item, const Facts& facts, const std::map<long long, int>& indexOf);

    // THE AGREEMENT (D7-10): `engine`, PDFium's reading, against `resolved` -- this reading placed by the same tree.
    // Success, or `error.outline.readingsDisagree` naming the first item that differs and how.
    static core::Status agreesWith(const core::Outline& engine, const Reading& resolved,
                                   const std::map<long long, int>& indexOf);

    // WHAT ONE DICTIONARY'S TARGET IS -- `/Dest` before `/A`, as above -- an outline item's, or since Stage 7 T12 a
    // Link annotation's (D7-12): the action into `action`, the page object or integer it names into `facts`. `named`
    // fills a GoToNamed's view and engine page; an empty map leaves it the name alone.
    static void readTarget(const ReferenceRepair::View& view, const cos::CosObject& dictionary,
                           const std::map<std::string, cos::CosObject>& named, core::OutlineAction& action, Facts& facts);

    // The title as PDFium shows it: every character at or below U+0020 a space.
    static QString engineTitle(const QString& title);

    // A destination array's view -- its fit and its parameters -- as `core::Destination` carries it (PDFium's rules).
    static core::Destination viewOf(const cos::CosObject& array);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_OUTLINEREADER_H
