// pdf/cos/XrefTable -- object numbers to byte offsets. Stage 2 T7.
//
// SCOPE. `startxref`, the two cross-reference forms, the `/Prev` chain that links a file's
// revisions, the trailer, and fetching an object by number -- including one packed inside an object
// stream. What it does NOT do is repair: when the structures here are unusable, `pdf/cos/XrefRepair`
// (T8) rebuilds the map by scanning the file, and this class's job is to FAIL CLEANLY AND SAY WHY
// so that repair has something to act on.
//
// -------------------------------------------------------------------------------------------
// TWO FORMS, AND THE SECOND IS NOT OPTIONAL
// -------------------------------------------------------------------------------------------
// A classic table is `xref`, subsections of twenty-byte records, then `trailer`. A cross-reference
// STREAM is an ordinary indirect object with `/Type /XRef`, its rows packed into binary fields whose
// widths `/W` gives -- and it is **Flate-compressed in practice every time**, which is why
// `pdf/cos/filters/FlateFilter` is a dependency of the thing that finds objects rather than an image
// feature. Every PDF 1.5-or-later file written by a modern producer uses the stream form, so a
// reader with only the classic path cannot open most documents at all.
//
// A THIRD case is the hybrid file: a classic table whose trailer also carries `/XRefStm`, pointing
// at a stream that lists the objects the classic table cannot describe. Old readers see a valid
// classic table; new ones get the packed objects. Ignoring `/XRefStm` does not fail loudly -- it
// silently loses every object in an object stream, which surfaces as a missing page.
//
// -------------------------------------------------------------------------------------------
// NEWEST WINS, AND THAT IS THE WHOLE POINT OF THE CHAIN
// -------------------------------------------------------------------------------------------
// An incremental save appends a new section that overrides only what changed and points back with
// `/Prev`. Sections are walked newest first and an object number already seen is NOT overwritten --
// so generation three's object 6 wins over generation one's. Reversing that reads a document as it
// was before its last two saves, with no error anywhere.
// `corpus/ordinary/incremental-3gen.pdf` is the fixture, and guide case **A-20** walks it.
//
// LOOPS ARE A REAL FILE, NOT A HYPOTHETICAL. `corpus/malformed/circular-prev.pdf` has a `/Prev`
// chain that points back at itself; A-21 forbids a hang, so every offset already visited ends the
// walk with a note. The same applies to an object stream that claims to contain itself.

#ifndef RAWPAGE_PDF_COS_XREFTABLE_H
#define RAWPAGE_PDF_COS_XREFTABLE_H

#include "core/Result.h"
#include "pdf/cos/CosObject.h"
#include "pdf/cos/Parser.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace rawpage::pdf::cos {

enum class XrefEntryType : std::uint8_t {
    Free,             // on the free list; `field2` is the next free object number
    InFile,           // `field2` is a byte offset, `field3` the generation
    InObjectStream,   // `field2` is the object stream's NUMBER, `field3` the index within it
};

struct XrefEntry {
    XrefEntryType type = XrefEntryType::Free;
    long long field2 = 0;
    long long field3 = 0;

    // Which revision supplied this entry: 0 is the newest section, 1 its `/Prev`, and so on.
    // Carried because T8's `RepairReport` and Stage 3's incremental writer both need to know
    // whether an object came from the newest generation or an older one.
    int revision = 0;

    // A hybrid file's `/XRefStm` ALSO defines this number, and this entry -- read from the classic
    // table in front of that stream -- is the one that won. Carried because an update that hoists
    // `/XRefStm` into its own trailer moves the stream ahead of the section this entry came from,
    // which would silently hand the number back to the stream (Stage 3 guide I-17). The writer
    // re-lists exactly these.
    bool alsoInXrefStm = false;
};

enum class XrefNote : std::uint8_t {
    None,
    StartxrefMissing,        // no `startxref` anywhere in the file
    StartxrefOutOfRange,     // it names an offset past the end of the buffer
    StartxrefNotATable,      // the offset holds neither `xref` nor an /XRef stream object
    SubsectionMalformed,     // a classic subsection header that is not `<start> <count>`
    EntryMalformed,          // a twenty-byte record that is not `<10> <5> <n|f>`
    PrevLoop,                // a /Prev chain that revisits an offset -- circular-prev.pdf
    PrevOutOfRange,
    XrefStreamUndecodable,   // /Type /XRef whose stream would not decode
    XrefStreamFieldsBad,     // /W missing, negative, or summing to more than the row supplies
    TrailerMissing,          // a section with no trailer dictionary and no /XRef stream dict
    SizeDisagrees,           // /Size is smaller than the largest object number in the table
    ObjectStreamUnreadable,  // a type-2 entry whose container will not decode or parse
    ObjectOffsetWrong,       // the entry's offset does not hold the object it claims
};

struct XrefNoteRecord {
    XrefNote note = XrefNote::None;
    std::size_t offset = 0;
    long long detail = 0;
};

// One revision of the file, newest first in `sections()`.
struct XrefSection {
    std::size_t offset = 0;      // where its `xref` keyword or /XRef object begins
    bool isStream = false;       // a cross-reference stream rather than a classic table
    bool fromXrefStm = false;    // reached through a hybrid file's /XRefStm rather than /Prev
    CosObject trailer;
};

// The largest object number a file may use -- ISO 32000-1 Annex C, "8,388,607 indirect objects in a
// file". A number the file writes is a claim like any other, and this reader NEVER PLACES one above it:
// not from a classic subsection, an xref stream's /Index, an object stream's pair table or a repair
// scan (Stage 3 guide issue I-13, found by `fuzz_cos_roundtrip`'s soak). Past it, a number is not an
// object any conforming reader resolves -- and it is what let `largest + 1` overflow. One definition:
// `ObjectAllocator` uses the same limit for what an update may allocate.
inline constexpr long long kMaxObjectNumber = 8'388'607;

class XrefTable {
public:
    // Follows `startxref` and the whole `/Prev` chain. Fails only when there is no usable table at
    // all -- which is precisely the signal `XrefRepair` (T8) exists to act on, so the error names
    // what was wrong rather than saying the file is broken.
    static core::Result<XrefTable> load(std::string_view source);

    const XrefEntry* find(long long number) const;
    bool has(long long number) const { return find(number) != nullptr; }

    // `/Size` from the newest trailer that carries one. This is a CLAIM the file makes, not a
    // measurement: `corpus/malformed/size-too-small.pdf` lies about it, so `largestObjectNumber()`
    // is the one to trust when they disagree.
    long long declaredSize() const noexcept { return declaredSize_; }
    long long largestObjectNumber() const;
    std::size_t entryCount() const noexcept { return entries_.size(); }

    // The merged trailer: each of `/Root`, `/Info`, `/Encrypt`, `/ID` and `/Size` taken from the
    // NEWEST section that carries it. A caller reading only the newest section's trailer misses
    // `/Root` on any file whose last revision did not restate it, which is most of them.
    const CosObject& trailer() const noexcept { return trailer_; }

    const std::vector<XrefSection>& sections() const noexcept { return sections_; }
    const std::vector<XrefNoteRecord>& notes() const noexcept { return notes_; }
    std::size_t noteCount(XrefNote note) const;

    // D2-6: this stage DECLINES an encrypted document by name rather than half-parsing it. The
    // check lives here because `/Encrypt` is a trailer key, and the answer has to be available
    // before anything tries to read a string.
    bool isEncrypted() const;

    // Every entry, for `pdf/cos/XrefRepair` (T8), which starts from whatever this table managed to
    // read and replaces only the entries that do not verify. Exposed rather than duplicated,
    // because a repairer that rebuilt from nothing would throw away the good half of a file whose
    // damage is confined to its tail -- and that is most damaged files.
    const std::map<long long, XrefEntry>& entries() const noexcept { return entries_; }

    // Build a table from a reconstruction rather than from the file's own structures. The ONLY
    // caller is `XrefRepair`: `load()` is how a healthy file is read, and having two public ways
    // to make a table that mean different things is how a later stage ends up calling the wrong
    // one. `notes` carries what the failed load already discovered, so a repair report can say
    // what was wrong as well as what was rebuilt.
    static XrefTable rebuilt(std::string_view source, std::map<long long, XrefEntry> entries,
                             CosObject trailer, std::vector<XrefNoteRecord> notes = {});

    // Fetch by number, following a type-2 entry into its object stream and caching the container
    // so a 200-object stream is decoded once rather than 200 times. Not const: the cache is the
    // whole reason this is affordable.
    core::Result<IndirectObject> fetch(long long number);

    // The `/Length 9 0 R` resolver `pdf/cos/Parser` takes. Handing the parser this is what closes
    // the loop T6 deliberately left open -- and it is re-entrancy guarded, because a stream whose
    // `/Length` points at an object inside another stream would otherwise recurse without bound.
    LengthResolver lengthResolver();

    std::string_view source() const noexcept { return source_; }

private:
    struct SectionResult {
        CosObject trailer;
        bool ok = false;
    };

    void note(XrefNote which, std::size_t offset, long long detail = 0);
    void mergeEntry(long long number, const XrefEntry& entry);

    SectionResult readClassic(std::size_t offset, int revision);
    SectionResult readStream(std::size_t offset, int revision);
    SectionResult readSection(std::size_t offset, int revision, bool& isStream);

    const std::vector<IndirectObject>* objectStreamFor(long long number);

    std::string_view source_;
    std::map<long long, XrefEntry> entries_;
    std::vector<XrefSection> sections_;
    std::vector<XrefNoteRecord> notes_;
    CosObject trailer_;
    long long declaredSize_ = 0;

    // Set only while the section being read is one a trailer's `/XRefStm` named, so `mergeEntry`
    // can mark what that stream would have defined had the classic table not got there first.
    bool readingXrefStm_ = false;

    std::map<long long, std::vector<IndirectObject>> objectStreams_;
    std::vector<long long> objectStreamsInProgress_;
    bool resolvingLength_ = false;
};

// Where `startxref`'s number lives, or npos. Exposed because `XrefRepair` (T8) has to be able to
// say *"the startxref was fine and the table it named was not"*, which is a different repair from
// *"there is no startxref"*.
std::size_t findStartxref(std::string_view source, long long& offsetOut);

const char* xrefNoteName(XrefNote note) noexcept;

} // namespace rawpage::pdf::cos

#endif // RAWPAGE_PDF_COS_XREFTABLE_H
