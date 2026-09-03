// core/doc/RepairReport -- what had to be reconstructed to open this document. Stage 2 T8.
//
// TWO CONSUMERS, AND ONLY ONE OF THEM EXISTS YET:
//
//   * **T14's `ui/shell/MessageBar`** says the file was repaired and offers the detail.
//   * **Stage 3** forces a repaired document's first save to Save-As (`CLAUDE.md`, standing
//     conventions). That is why this is not a boolean.
//
// -------------------------------------------------------------------------------------------
// WHY A BOOLEAN WOULD NOT DO -- guide case A-22, and it is the whole design
// -------------------------------------------------------------------------------------------
// `Stage2-Rendering-Plan.md` §5.4 puts it in one line: *"reconstructed 3 objects of 4,000"* and
// *"reconstructed the entire xref"* are **different risks**. The first is a file with a damaged
// tail that will save back almost unchanged; the second is a document whose entire structure is
// our guess, where an incremental save would append a new revision on top of a reconstruction and
// silently discard whatever the original bytes really said. Stage 3 has to be able to tell them
// apart, and a `bool wasRepaired` cannot.
//
// So the report carries per-object entries with a severity, plus the two numbers that make the
// ratio meaningful: how many objects were reconstructed and how many there are.
//
// -------------------------------------------------------------------------------------------
// THE SUMMARY IS A CATALOG KEY, NOT A SENTENCE
// -------------------------------------------------------------------------------------------
// `core/` holds no user-facing English (`core/Result.h`, CLAUDE.md convention 5) and has no
// `tr()`. A-22 asks for *"a summary a message bar can render in one line"*, and the honest form of
// that in this layer is a **key plus its arguments** -- `ui/` resolves it. Returning an English
// sentence here would be the one thing that makes the product unshippable in `zh-TW`, and it would
// look completely reasonable in review.

#ifndef RAWPAGE_CORE_DOC_REPAIRREPORT_H
#define RAWPAGE_CORE_DOC_REPAIRREPORT_H

#include "core/Result.h"

// QStringList, not merely forward-declared: `summaryArguments()` RETURNS one by value, so every
// caller needs the complete type. core/Result.h brings in QString and stops there.
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawpage::core {

// How bad one finding is. Ordered: a later enumerator is worse, so `worstSeverity()` is a max.
enum class RepairSeverity : std::uint8_t {
    Info,        // noticed and cost nothing -- a lying /Length that was recovered exactly
    Recovered,   // reconstructed, and the result is believed complete
    Degraded,    // reconstructed, and something was lost or guessed
    Lost,        // could not be recovered at all; that object is gone
};

// WHAT was wrong. Fine-grained where the REPAIR differs and coarse where it does not -- D2-11
// spent a decision arguing against "something was odd in object 12", and this enum is where that
// argument is honoured or lost.
enum class RepairKind : std::uint8_t {
    None,
    StartxrefMissing,        // no `startxref` at all; the whole table is a reconstruction
    StartxrefUnusable,       // it pointed somewhere that is not a table
    XrefSectionUnreadable,   // a section in the /Prev chain could not be read
    XrefPrevLoop,            // the revision chain pointed back at itself
    ObjectOffsetWrong,       // the table's offset did not hold the object it named
    ObjectRecoveredByScan,   // found by scanning the file rather than through the table
    ObjectMissing,           // named by the table or by a reference, and not in the file
    ObjectStreamUnreadable,  // a compressed object container would not decode
    StreamLengthRecovered,   // a stream's /Length lied and `endstream` was scanned for
    StreamEndstreamMissing,  // a stream ran to the end of the file
    TrailerMissing,          // no trailer; /Root had to be found by looking for a /Catalog
    TrailerRootMissing,      // there is a trailer and it names no usable /Root
    SizeDisagrees,           // /Size is smaller than the largest object actually present
};

struct RepairEntry {
    RepairKind kind = RepairKind::None;
    RepairSeverity severity = RepairSeverity::Info;

    // -1 when the finding is not about one object (a missing startxref, a bad trailer).
    long long objectNumber = -1;

    // Where in the file, for a diagnostic that can be checked against the bytes.
    std::size_t offset = 0;

    // Kind-specific, documented at each enumerator's use site -- the declared length for
    // `StreamLengthRecovered`, the offset that was wrong for `ObjectOffsetWrong`.
    long long detail = 0;
};

class RepairReport {
public:
    void add(const RepairEntry& entry);

    // How many objects the file is believed to contain, set by the repairer once it knows. The
    // DENOMINATOR of A-22's ratio: without it, "3 objects reconstructed" says nothing about
    // whether the document is nearly intact or nearly invented.
    void setObjectsTotal(std::size_t total) noexcept { objectsTotal_ = total; }

    // Set when the cross-reference table was rebuilt from scratch rather than patched. This is
    // the *"reconstructed the entire xref"* case, and it is recorded explicitly rather than
    // inferred from the ratio -- a two-object document whose xref was entirely rebuilt has a
    // ratio of 1.0 and so does a two-object document with two bad offsets, and they are not the
    // same risk.
    void setXrefRebuiltFromScratch(bool rebuilt) noexcept { xrefRebuilt_ = rebuilt; }

    const std::vector<RepairEntry>& entries() const noexcept { return entries_; }
    bool isEmpty() const noexcept { return entries_.empty(); }

    // The question Stage 3 asks before allowing a plain Save. Note that it is NOT `!isEmpty()`:
    // an `Info` finding -- a `/Length` recovered exactly, which is the commonest thing in any real
    // PDF -- is worth reporting and is not a reason to force Save-As. Forcing it on every
    // slightly-imperfect file would train users to ignore the prompt.
    bool requiresSaveAs() const noexcept;

    RepairSeverity worstSeverity() const noexcept;
    std::size_t objectsReconstructed() const noexcept { return objectsReconstructed_; }
    std::size_t objectsTotal() const noexcept { return objectsTotal_; }
    bool xrefRebuiltFromScratch() const noexcept { return xrefRebuilt_; }
    std::size_t count(RepairKind kind) const noexcept;

    // A-22's one-line summary, as a CATALOG KEY plus its arguments -- never a sentence, because
    // `core/` holds no user-facing English. `ui/` resolves the key and interpolates the arguments.
    //
    //   error.repair.none              -- nothing to say
    //   error.repair.minor             -- %1 findings, none of which cost anything
    //   error.repair.objects           -- reconstructed %1 of %2 objects
    //   error.repair.wholeXref         -- the entire cross-reference table was rebuilt (%1 objects)
    QLatin1String summaryKey() const noexcept;
    QStringList summaryArguments() const;

private:
    std::vector<RepairEntry> entries_;
    std::size_t objectsReconstructed_ = 0;
    std::size_t objectsTotal_ = 0;
    bool xrefRebuilt_ = false;
};

// Stable, ASCII, non-localized. Diagnostics, the log and test failure messages. A "?" here is a
// bug report nobody can act on, which is why `tst_xrefrepair` asserts completeness.
const char* repairKindName(RepairKind kind) noexcept;
const char* repairSeverityName(RepairSeverity severity) noexcept;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_REPAIRREPORT_H
