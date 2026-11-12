// core/doc/Destinations -- where a link, a bookmark or a named destination POINTS, and the
// document's table of named ones. Stage 4 T4; decision D4-3 (`Stage4-ViewingSurface-Plan.md` §3). The
// EDITING half Stage 7 T12 (D7-10, D7-35, `Stage7-Assembly-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// TWO READINGS, HELD TO AGREE (D7-10)
// ---------------------------------------------------------------------------------------------
// *"We read through PDFium and write through our own code"* (`CLAUDE.md`, D3). Both forms a PDF may
// carry -- the PDF 1.2 `/Names /Dests` name TREE and the PDF 1.1 flat `/Dests` DICTIONARY -- are
// resolved by `FPDFDest_*` behind the facade, including the inherited and indirect cases, for the panes.
// Editing needs to know which table an entry is in and what it was written as, which PDFium does not
// say, so since T12 `pdf/ops/DestinationReader` reads both first-party too, and the backend holds the
// two readings to agree -- the same names, each to the same page -- or the table is READ-ONLY AND SAYS
// WHY (`status()`), the engine's reading shown (D6-3's rule), exactly as the outline is.
//
// This file is the vocabulary only: no `FPDF_` name appears here, and `layer-lint` would say so.
//
// ---------------------------------------------------------------------------------------------
// AN ENTRY KEEPS ITS TABLE (D7-35)
// ---------------------------------------------------------------------------------------------
// A name in the `/Dests` dictionary is a PDF NAME and a link reaches it as `/Dest /name`; one in the
// name tree is a STRING and a link reaches it as `/Dest (name)`. ISO 32000-1 §12.3.2.3 keeps the two
// apart and a strict reader looks a name up only in its own table, so an entry stays in the table it
// was read from -- a rename too -- and one RawPage creates goes in the tree, the form PDF 1.2 made the
// standard one.
//
// ---------------------------------------------------------------------------------------------
// AN UNRESOLVED DESTINATION IS A VALUE, NOT AN ERROR
// ---------------------------------------------------------------------------------------------
// A named destination whose page is gone is what deleting a page leaves behind, and
// `corpus/ordinary/dests-named.pdf` carries one on purpose (`ghost-page`, pointing at an object
// that does not exist). It must be **reported** rather than silently dropped -- a pane that omits
// it tells the user their document has four destinations when it has five, and the one that is
// broken is the one they were looking for. So `pageIndex` is `-1` and `isResolved()` is false, and
// the entry stays in the table. A page command REMAPS the indices (D7-3) and leaves an entry to a page
// it removed unresolved, as the outline does -- the model keeps what the entry meant, and what
// `ReferenceRepair` writes is the file's business.

#ifndef RAWPAGE_CORE_DOC_DESTINATIONS_H
#define RAWPAGE_CORE_DOC_DESTINATIONS_H

#include "core/Result.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawpage::core {

class PageMap;

// Where to go, and how to frame it once there. The fit modes are ISO 32000-1 §12.3.2.2's, named
// rather than numbered, because a `/FitH` with no top is a different thing from a `/FitH` with one
// and a bare integer could not say so.
struct Destination {
    enum class Fit {
        Unknown,   // the destination named no view, or the engine could not say
        XYZ,       // a point and a zoom, any of which may be null -- see the `has*` flags
        Fit,       // fit the whole page
        FitH,      // fit the width, at a given top
        FitV,      // fit the height, at a given left
        FitR,      // fit a rectangle
        FitB,      // fit the bounding box of the page's content
        FitBH,
        FitBV,
    };

    int  pageIndex = -1;         // -1 means UNRESOLVED, which is a value (see the header)
    Fit  fit = Fit::Unknown;

    // `/XYZ null null null` is legal and common -- it means *keep the current value* for each null.
    // A reader that treated a missing coordinate as 0 would scroll every such link to the page's
    // bottom-left corner, so the three are carried separately from their values.
    bool   hasX = false;
    bool   hasY = false;
    bool   hasZoom = false;
    double x = 0.0;
    double y = 0.0;
    double zoom = 0.0;

    bool isResolved() const noexcept { return pageIndex >= 0; }

    friend bool operator==(const Destination& a, const Destination& b)
    {
        return a.pageIndex == b.pageIndex && a.fit == b.fit && a.hasX == b.hasX && a.hasY == b.hasY
               && a.hasZoom == b.hasZoom && a.x == b.x && a.y == b.y && a.zoom == b.zoom;
    }
};

// ASCII, stable, and the token any file or log holds -- the same arrangement `layoutModeName` and
// `fitModeName` use, so a surface cannot display a value the enum lacks.
const char* destinationFitName(Destination::Fit fit) noexcept;

// THE BOUNDS a command, a reader and a replay share. A name is far shorter than any real one needs and a
// table far larger than any real document's; both far below what takes noticeable time on the UI thread.
inline constexpr qsizetype kMaxDestinationNameLength = 1024;
inline constexpr std::size_t kMaxNamedDestinations = 100000;

struct NamedDestination {
    enum class Form : std::uint8_t {
        NameTree,     // `/Names /Dests`, keyed by a string (PDF 1.2)
        Dictionary,   // the catalog's `/Dests`, keyed by a name (PDF 1.1)
    };

    QString     name;
    Destination destination;
    Form        form = Form::NameTree;

    friend bool operator==(const NamedDestination& a, const NamedDestination& b)
    {
        return a.name == b.name && a.destination == b.destination && a.form == b.form;
    }

    // `{"name", "form", "dest"}` -- the form a command carries. `fromJson` is a parser of hostile input (a
    // journal line), bounded as `kMaxDestinationNameLength` says.
    QJsonObject toJson() const;
    static Result<NamedDestination> fromJson(const QJsonObject& json);
};

// `nameTree` or `dictionary`.
const char* namedDestinationFormName(NamedDestination::Form form) noexcept;

// WHETHER A COMMAND MAY GIVE AN ENTRY THIS NAME (D7-35): 1 ... `kMaxDestinationNameLength` characters of printable
// ASCII, not beginning or ending with a space. A name READ from a file may be anything its producer wrote and is kept
// as written; one a person types is held to what every reader and both tables spell the same way.
bool isWritableDestinationName(const QString& name);

// The document's named destinations, in the order the engine enumerates them. **Both PDF forms land
// here as one list**: a caller asking for `chapter-one` does not care which table it came from, and
// keeping two lists would make *"which one wins"* a question every caller had to answer. Each entry
// says which table it is in (`form`); a name is unique across both.
class Destinations {
public:
    void add(NamedDestination entry) { entries_.push_back(std::move(entry)); }

    const std::vector<NamedDestination>& all() const noexcept { return entries_; }
    bool isEmpty() const noexcept { return entries_.empty(); }
    std::size_t size() const noexcept { return entries_.size(); }

    // Null when no destination carries that name. Exact match: PDF destination names are byte
    // strings and are case-SENSITIVE, so folding them here would make two distinct destinations
    // one.
    const NamedDestination* find(const QString& name) const;

    // How many entries name a page that is not there. Zero on every well-formed document, which is
    // why a pane can show it without shouting.
    std::size_t unresolvedCount() const;

    // ---- The editing half (Stage 7 T12) ----------------------------------------------------------------
    // WHETHER IT MAY BE EDITED (D7-10): success, or the refusal -- the two readings disagree, or nothing read
    // it. A default table is an assessed, empty one; `refused` is one that says why it is read-only.
    const Status& status() const noexcept { return status_; }
    static Destinations refused(Error why, std::vector<NamedDestination> entries = {});
    void setStatus(Status status) { status_ = std::move(status); }

    // A new entry, at the end. Refused, changing nothing: a name already here, or a name a command may not write.
    Status insert(NamedDestination entry);
    // Removes `name` and returns it with where it stood; refused, changing nothing, for a name not here.
    Result<std::pair<NamedDestination, std::size_t>> remove(const QString& name);
    // Puts `entry` back at `at` (0 ... size) -- what an undone delete does.
    Status restore(NamedDestination entry, std::size_t at);
    // `from` called `to`, in its place and its table. Refused for a name not here, a `to` already here, or a `to` a
    // command may not write.
    Status rename(const QString& from, const QString& to);

    // D7-3: every destination carried through `map` -- unresolved where its page was removed (see the header).
    void applyPageMap(const PageMap& map);

    // The bounds, and every name unique. What a command checks a table it computed against.
    Status validate() const;

    // The entries ordered by name (code point by code point, so `B` before `a`, as the bytes compare) or by page
    // and then name, an unresolved entry last -- what the pane's two sort orders show.
    std::vector<const NamedDestination*> sortedByName() const;
    std::vector<const NamedDestination*> sortedByPage() const;

    // Every entry, every field, in order, and whether it may be edited -- `Document::modelHash`'s input.
    QByteArray digest() const;

private:
    std::vector<NamedDestination> entries_;
    Status status_;
};

} // namespace rawpage::core

// Declared here rather than in each `ui/` file that needs it (Stage 4 T6). A `Destination` is a
// value type that travels in a Qt signal and rides in a `QVariant` on a tree item, and
// `Q_DECLARE_METATYPE` must be seen exactly once for a type -- two UI headers each declaring it
// would be an ODR question nobody wants to answer at link time. It is `QMetaType`, which is
// QtCore: `core/` may name QtCore by the documented exception and may not name a QtGui type
// (convention 3), and this is the former. Present since Qt 5, so the portable presets' Qt 6.4
// builds it unchanged (convention 24).
Q_DECLARE_METATYPE(rawpage::core::Destination)

#endif // RAWPAGE_CORE_DOC_DESTINATIONS_H
