// pdf/ops/ReferenceRepair -- what names a page, and what a page command does to it. Stage 7 T2, decision
// D7-2 (`Stage7-Assembly-Plan.md` §3).
//
// -------------------------------------------------------------------------------------------
// WHAT NEEDS NO REPAIR, AND WHY THIS FILE DOES NOT TOUCH IT
// -------------------------------------------------------------------------------------------
// A page is its page OBJECT. A move reorders `/Kids` and never copies a page, so everything that names a
// page by REFERENCE -- an outline item's or a link's `/Dest`, a named destination, a structure element's
// `/Pg`, a bead's `/P`, `/OpenAction` -- follows it with no write at all. Code that "repaired" those would
// be how a correct reference is made wrong; the guide's control for A-19 plants exactly that repair and
// watches every moved reference fail.
//
// -------------------------------------------------------------------------------------------
// WHAT THIS FILE DOES
// -------------------------------------------------------------------------------------------
// It finds every reference to a page -- `scan` -- and, for a set of pages the document no longer has
// (`gone`), treats each reference to one as D7-2 says, in the same command:
//   * an outline item keeps its title and loses its destination (`/Dest` and `/A` removed);
//   * a link on a page that stays keeps its rectangle and loses its action;
//   * a named destination to it is removed -- from `/Names /Dests` (a leaf's `/Names`, its `/Limits`
//     redrawn) or the catalog's `/Dests` -- and an item or link naming it by NAME is cleared too, since
//     the name no longer resolves;
//   * a structure element's content items on it are removed -- its marked-content numbers when its `/Pg`
//     is the page, any `/MCR` or `/OBJR` whose own `/Pg` is -- and the `/Pg` itself;
//   * a bead on it is unlinked from its thread's ring, and a thread left with no bead leaves `/Threads`;
//   * `/OpenAction` to it is removed.
// And a REMOTE `GoToR`, which names a page NUMBER in another file, and a local destination naming its page
// by INTEGER -- a producer's error PDFium reads leniently (Stage 7 guide A-15) -- are REPORTED on every page
// command and never touched (guide A-22).
//
// -------------------------------------------------------------------------------------------
// DESIRED STATE, AND THE LEDGER THAT MAKES AN UNDO AFTER A SAVE EXACT
// -------------------------------------------------------------------------------------------
// The sink composes from the desired state on every show and every save (D3-5), so the repair is a
// FUNCTION of (the references as written, the pages gone) and not a log of edits: delete a page and undo
// before a save, and nothing is gone, so nothing is written. AFTER a save the disk holds the repaired
// objects, and the references the repair cleared are no longer there to scan. So every value the repair
// changed in a saved revision is kept in a LEDGER -- per object, per key, the value before the repair --
// and both the scan and the repair read each object THROUGH it (`View`): the ledger's values put back over
// the disk's. An undo after a save then sees the bookmark's original destination again, finds its page
// back in the document, and writes the destination back. The ledger is the sink's to keep (`adopt`).
//
// A key of `""` names the whole value of an object that is not a dictionary -- an indirect `/K` array, an
// indirect `/Threads` array.
//
// BOUNDED: every walk has a visited set and a cap (`kMaxReferences`); a cycle or a million-item outline is
// a walk that stops, never one that does not.

#ifndef RAWPAGE_PDF_OPS_REFERENCEREPAIR_H
#define RAWPAGE_PDF_OPS_REFERENCEREPAIR_H

#include "core/Result.h"
#include "core/doc/PageMap.h"
#include "pdf/cos/CosObject.h"

#include <QString>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace rawpage::pdf::cos {
class XrefTable;
}

namespace rawpage::pdf::ops {

struct PageWalk;

class ReferenceRepair {
public:
    static constexpr std::size_t kMaxReferences = 1'000'000;

    // Per object, per key: the value it had before the repair changed it (nullopt: the key was absent).
    using Ledger = std::map<long long, std::map<std::string, std::optional<cos::CosObject>>>;

    // How the scan and the repair read an object: the disk's, with the ledger put back over it.
    class View {
    public:
        View(cos::XrefTable& table, const Ledger& ledger) : table_(table), ledger_(ledger) {}
        // The object, spans cleared, or nullopt when it does not fetch.
        std::optional<cos::CosObject> object(long long number) const;
        // A value that may be a reference, resolved once.
        std::optional<cos::CosObject> resolve(const cos::CosObject& value) const;
        long long catalogNumber() const;
        cos::XrefTable& table() const noexcept { return table_; }

    private:
        cos::XrefTable& table_;
        const Ledger& ledger_;
    };

    // Where a reference points.
    enum class Target : std::uint8_t { None, Page, Named, Index, Remote };

    struct Record {
        core::PageReference::Kind kind = core::PageReference::Kind::OutlineItem;
        long long holder = -1;       // the object holding it (the catalog for /OpenAction)
        Target target = Target::None;
        long long page = -1;         // Target::Page: the page object
        std::string name;            // Target::Named: the destination's name, as bytes; a named destination's own name
        int index = -1;              // Target::Index / Remote: the page number as written
        long long onPage = -1;       // a link: the page object whose /Annots lists it
        QString words;               // what a person knows it by
    };

    struct Scan {
        std::vector<Record> records;
        std::map<std::string, long long> named;   // every named destination -> its page object (or -1)
    };

    // Every reference to a page the document holds, read through `view`. `pages`: the page objects whose
    // `/Annots` hold the links to read.
    static Scan scan(const View& view, const std::vector<long long>& pages);

    // What removing `gone` does, in the person's words, for a command whose pages had `indexOf` BEFORE it --
    // plus, when `byIndex`, every remote and integer destination, reported and left alone.
    static core::PageCommandReport report(const Scan& scan, const std::set<long long>& gone,
                                          const std::map<long long, int>& indexOf, bool byIndex);

    // THE REPAIR, as the desired state needs it: every object the scan found whose references to `gone`
    // must change, rewritten from its value in `pending` (another part of the compose already rewrote it)
    // or else from the view -- and every ledger value put back where its page has returned. Writes each
    // object that now differs from the DISK into `pending`, and returns per object and key the value it has
    // ON DISK -- what the sink's ledger keeps after a save, where it does not already hold an older value.
    static core::Result<Ledger> compose(const View& view, const Scan& scan, const std::set<long long>& gone,
                                        std::map<long long, cos::CosObject>& pending);

    // A Link or outline item dictionary with its destination removed when it targets `gone` -- the rule a
    // copied link of a duplicated page also meets. Nullopt when it needs no change.
    static std::optional<cos::CosObject> clearedTarget(const View& view, const cos::CosObject& holder,
                                                       const std::set<long long>& gone,
                                                       const std::map<std::string, long long>& named);

    // Stage 7 T5 (D7-4): every named destination -- `/Names /Dests` and the catalog's `/Dests`, read as `scan` reads
    // them -- as the EXPLICIT array it stands for, references and a dictionary's `/D` followed. A copied link that
    // named one is written with the array: the name means nothing in the document the copy lands in.
    static std::map<std::string, cos::CosObject> namedArrays(const View& view);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_REFERENCEREPAIR_H
