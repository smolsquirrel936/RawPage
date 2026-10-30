// pdf/cos/IncrementalWriter -- appends a revision to a PDF and never touches what is already there.
// Stage 3 T2.
//
// The riskiest first-party code in the project (`Implementation-Plan.md` § Stage 3), because it has
// one job that cannot be approximated: after it runs, every byte of the file as it was opened must
// still be there, unchanged, at the same offset. A signature covers a byte range; an unrelated
// edit that moves one byte inside it breaks the signature silently (risk A4, `Tech-Stack-Plan.md`
// §3.3). Guide cases A-19 … A-28 (`Stage3-Mutation-Testing-Guide.md` §5.4) are this file's.
//
// -------------------------------------------------------------------------------------------
// IT RETURNS THE UPDATE, NOT THE FILE -- so it cannot rewrite the original even by mistake
// -------------------------------------------------------------------------------------------
// `Implementation-Plan.md` sketched `append(QIODevice&, const ObjectDelta&)`. What exists instead
// takes the original bytes READ-ONLY and returns only the bytes to put after them. Three reasons:
//
//   * **The prefix property becomes structural.** A function that is never handed a writable copy
//     of the original cannot alter it; the caller writes `original` then `update.bytes`, which is
//     exactly D3-4's "the opened bytes, copied verbatim, followed by the update"
//     (`Stage3-Mutation-Plan.md` §3). Every byte-identity assertion is still made against bytes
//     read back from disk (guide rule 3), because this argument proves the function and not the
//     save.
//   * **`pdf/cos/` is portable std C++ by rule** -- `portable-asan` and `portable-fuzz` build it on
//     Linux, and `fuzz_cos_roundtrip` (D3-17) will call this per iteration. A `QIODevice` would put
//     a Qt I/O class at the centre of the one component those jobs exist to exercise.
//   * **The device is D3-4's to choose.** The temporary file, its flush and its replace belong to
//     `core/doc/SaveCoordinator` and `platform::IDurableFiles` (T7, T8), not to the serializer.
//
// -------------------------------------------------------------------------------------------
// WHAT GOES IN THE UPDATE (D3-5), and what does not
// -------------------------------------------------------------------------------------------
//   * **Exactly the objects in the delta**, each written as an ordinary indirect object -- including
//     one that lived inside an `/ObjStm`, whose new type-1 entry supersedes its old type-2 entry.
//     The object stream itself is never rewritten. An untouched object is never copied: it is the
//     original, at its original offset, reached through `/Prev`.
//   * **A cross-reference section matching the NEWEST section's kind**: a classic table after a
//     classic table, a stream after a stream, and a classic table after a hybrid `/XRefStm` file
//     (whose newest section is its classic table). A stream's own number comes from
//     `ObjectAllocator`, never from the free list.
//   * **A trailer** with `/Size` = the larger of the old claim, one past the largest present object,
//     and the allocator's next number; `/Root` and `/Info` carried from the merged trailer; `/Prev`
//     = the newest section's offset; and an `/ID` whose FIRST element is kept and whose SECOND is a
//     hash of the update. A deterministic `/ID` is what lets an expected update be a committed
//     fixture (guide case A-24). **After a hybrid file, `/XRefStm` too**, the newest trailer's value
//     unchanged: Acrobat reads the key from the newest trailer only, and reports a file whose packed
//     objects it cannot reach as damaged -- a defect our reader and PDFium cannot see, because both
//     follow `/Prev` to the older trailer (Stage 3 guide I-14, found by M-02's walk). **And with it,
//     a row for every number the section at `/Prev` defined that the stream defines too**: naming the
//     stream in our trailer puts it in FRONT of that section, so without those rows the second save
//     of a hybrid file hands each of them back to the stream -- which is the first save, reverted
//     (Stage 3 guide I-17, found by `fuzz_cos_roundtrip`'s soak).
//   * **Never** `/Info` or XMP changes, and never an `/Encrypt`: an encrypted document is refused,
//     because every string and stream written into it would have to be encrypted (D3-7; Stage 10).
//
// `appendComplete()` is D3-6's other shape, for a REPAIRED document's forced Save-As: the same
// objects, but a section listing EVERY object at the offset `XrefRepair` verified, and no `/Prev`,
// so the new file opens without repair. It keeps the original bytes, damage included.
//
// -------------------------------------------------------------------------------------------
// SERIALIZATION IGNORES SPANS, on purpose
// -------------------------------------------------------------------------------------------
// `CosObject::raw()` copies an object's exact source bytes, and the writer could use it for the
// untouched VALUES inside a changed dictionary. It does not: a value's span is only meaningful
// against the buffer it was parsed from, and a page dictionary unpacked from an object stream was
// parsed from a decompressed payload that no longer exists (issue I-04). A changed object is written
// whole from its values, which is byte identity's actual subject -- an object not in the update.
//
// -------------------------------------------------------------------------------------------
// AN UPDATE COMPOSED AGAIN AND AGAIN -- Stage 7 T4 (`Stage7-Assembly-Plan.md` Q4, `DW-4`'s second cause)
// -------------------------------------------------------------------------------------------
// D6-2 composes the whole unsaved update after EVERY command, and until T4 each compose numbered its new
// objects from the allocator in the order it met them and serialized every one -- so a one-page move after
// 1,500 unsaved highlights renumbered and re-wrote 3,701 objects (25 ms of the UI thread, measured). Three
// things here let a caller pay only for what changed:
//
//   * **`NumberBook`** -- a new object's number by a NAME the caller gives it (`"annot:<NM>"`), kept from one
//     compose to the next until the caller drops the book (at a save). A move no longer renumbers what it
//     did not touch. A name no compose asks for any more gives its number back: the highest is simply
//     un-given, and one below it is an UNUSED number, which the section writes as a FREE row -- so every
//     number under `/Size` has a row (the user's choice, 2026-10-31). The update's numbering therefore
//     depends on the order things were made in; what it MEANS does not.
//   * **`WrittenObject`** -- an object as this writer wrote it, envelope included, with the MD5 of its bytes.
//     A change carrying one is copied verbatim; the caller keeps them from the last compose. And the
//     `/ID`'s second element is hashed over each object's DIGEST rather than over every byte again.
//   * **`Protection`** -- `protectOriginal`'s walk, made ONCE per revision and applied to every delta.

#ifndef RAWPAGE_PDF_COS_INCREMENTALWRITER_H
#define RAWPAGE_PDF_COS_INCREMENTALWRITER_H

#include "core/Result.h"
#include "pdf/cos/CosObject.h"
#include "pdf/cos/ObjectAllocator.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rawpage::pdf::cos {

class XrefTable;

// One indirect object as `IncrementalWriter` wrote it -- `N G obj ... endobj` and the line end after it --
// and the MD5 of exactly those bytes (16 bytes). Stage 7 T4; see the header.
struct WrittenObject {
    long long number = 0;
    std::string bytes;
    std::string digest;
};

// One object the update writes. `streamData` is the ENCODED payload and is required when `object`
// is a Stream: a stream parsed from a file describes its data by offsets into that file, and a
// constructed one has none at all, so the writer is never left to guess where the bytes are.
// S7 T4: a change with `written` set is that object as an earlier compose wrote it, copied verbatim;
// `object` and `streamData` are then empty.
struct ObjectChange {
    long long number = 0;
    CosObject object;
    std::optional<std::string> streamData;
    std::shared_ptr<const WrittenObject> written;
};

// NUMBERS BY NAME, ACROSS COMPOSES (Stage 7 T4) -- see the header. A ROUND is one compose: `beginRound`,
// any number of `numberFor`, then `endRound` once every name is asked and BEFORE the update is written,
// which gives back every name the round did not ask for -- so `/Size` and the free rows are the ones the
// next compose of the same state writes (guide A-128 found them otherwise). A round that fails is simply
// begun again; what it named is kept until a round that ends does not ask for it. Numbers start at `first` -- above every number the file defines or
// references, which the caller works out once per revision -- and an unused number is given to the next
// new name before anything higher is.
class NumberBook {
public:
    explicit NumberBook(long long first) noexcept : first_(first), next_(first) {}

    void beginRound() noexcept { ++round_; }
    long long numberFor(const std::string& name);
    void endRound();
    // Every name asked for while `into` is set is appended to it -- for a caller that writes the same objects again
    // later and must ask for the same names then. Null stops it.
    void trace(std::vector<std::string>* into) noexcept { trace_ = into; }

    long long firstNumber() const noexcept { return first_; }
    long long nextNumber() const noexcept { return next_; }
    bool gave(long long number) const noexcept { return number >= first_ && number < next_; }
    // Below `nextNumber()` and given to no name: each is written as a free row.
    const std::set<long long>& unused() const noexcept { return unused_; }

private:
    struct Given {
        long long number = 0;
        std::uint64_t round = 0;
    };
    long long first_;
    long long next_;
    std::uint64_t round_ = 0;
    std::unordered_map<std::string, Given> names_;
    std::set<long long> unused_;
    std::vector<std::string>* trace_ = nullptr;
};

// What an update will contain, built against the table of the file it will be appended to.
// Desired state, not a log: replacing an object twice in one delta is refused by the writer rather
// than resolved, because which of two intents wins is `pdf/ops/MutationSinkImpl`'s decision (T3).
class ObjectDelta {
public:
    explicit ObjectDelta(const XrefTable& table) noexcept;
    // S7 T4: new objects numbered by `book`, by name -- which must outlive the delta and must already be above
    // every number the file references (`Protection::largestReferenced`).
    ObjectDelta(const XrefTable& table, NumberBook& book) noexcept;

    // An existing object's new value. Its number must name an object the table has.
    void replace(long long number, CosObject object, std::optional<std::string> streamData = {});

    // A new object, numbered by the allocator. Returns the number, so a caller can reference it.
    long long add(CosObject object, std::optional<std::string> streamData = {});
    // S7 T4: a new object numbered by the book under `name` -- the allocator's next without a book.
    long long add(const std::string& name, CosObject object, std::optional<std::string> streamData = {});

    // A NEW OBJECT'S NUMBER BEFORE ITS VALUE (Stage 6 T6): two new objects that name EACH OTHER -- a
    // note and its pop-up, `/Popup` one way and `/Parent` the other -- cannot both be added first. A
    // reserved number is the allocator's next, and `define` gives it its value exactly once. A number
    // reserved and never defined is a caller's defect, and `IncrementalWriter` would write no row for
    // it; the one caller (`MutationSinkImpl`) defines every number it reserves or discards the delta.
    long long reserve() noexcept { return allocator_.allocate(); }
    long long reserve(const std::string& name);   // S7 T4: by the book, as `add(name, ...)`
    // False -- nothing recorded -- for a number this delta did not reserve, or one already defined.
    bool define(long long number, CosObject object, std::optional<std::string> streamData = {});

    // S7 T4: an object an earlier compose wrote, again -- a new one (its number the book's) or a replaced one.
    // False, nothing recorded, for a number this delta already writes.
    bool reuse(std::shared_ptr<const WrittenObject> written);

    // True when `number` is already replaced or added by this delta.
    bool touches(long long number) const noexcept;

    // See `ObjectAllocator::reserveThrough`. Called by `IncrementalWriter::protectOriginal`. With a book it
    // changes nothing: the book starts above every reference already.
    void reserveNumbersThrough(long long number) noexcept { allocator_.reserveThrough(number); }

    const std::vector<ObjectChange>& changes() const noexcept { return changes_; }
    void reserveChanges(std::size_t count) { changes_.reserve(count); }   // S7 T4: a caller that knows roughly
    bool isEmpty() const noexcept { return changes_.empty(); }
    const ObjectAllocator& allocator() const noexcept { return allocator_; }
    // A number this delta's allocator or book gave out -- a NEW object, not one the file has.
    bool isNew(long long number) const noexcept;
    // The first number nothing here has given out: the `/Size` the update must declare at least.
    long long nextNumber() const noexcept;
    const NumberBook* book() const noexcept { return book_; }

private:
    ObjectAllocator allocator_;
    NumberBook* book_ = nullptr;
    std::vector<ObjectChange> changes_;
    // Which numbers this delta writes: a bit per number for the numbers a file has (S7 T4 -- a node per change
    // was most of the cost of a delta of thousands), a set past them.
    void mark(long long number);
    std::vector<bool> touchedBits_;
    std::set<long long> touchedFar_;
};

enum class SectionKind { Classic, Stream };

struct WrittenUpdate {
    std::string bytes;             // to be written IMMEDIATELY after the original, verbatim
    std::size_t sectionOffset = 0; // absolute offset of the new section: the new `startxref`
    SectionKind kind = SectionKind::Classic;
    long long size = 0;            // the `/Size` the new trailer declares
    long long xrefStreamNumber = -1;
    std::vector<long long> objectsWritten;   // ascending; excludes the xref stream's own number
    // S7 T4: each of them as written, in the same order -- the ones a change carried, the same pointers.
    std::vector<std::shared_ptr<const WrittenObject>> objects;
    std::size_t objectsReused = 0;   // S7 T4: how many of them a change carried as written
};

// `protectOriginal`'s walk, once (Stage 7 T4): the objects it would re-write and the largest number any
// object references. Pure data about one revision; `apply` puts it into a delta.
struct Protection {
    std::vector<ObjectChange> rewrites;
    long long largestReferenced = -1;
};

class IncrementalWriter {
public:
    // An ordinary update, `/Prev`-chained to the newest section. `table` must have been loaded over
    // exactly `original` -- the same buffer, not a copy -- because every offset it holds is an
    // offset into it. Refused, with nothing produced: an encrypted document; a table with no
    // revision to chain to (a repaired one -- use `appendComplete`); an empty delta; a change naming
    // an object the file does not have, or one twice; a stream change with no data; a document
    // with no `/Root`.
    static core::Result<WrittenUpdate> append(std::string_view original, const XrefTable& table,
                                              const ObjectDelta& delta);

    // D3-6: a complete section and no `/Prev`, over a table `XrefRepair` rebuilt (or any table). An
    // empty delta is allowed -- the complete table is itself the point of a repaired file's Save-As.
    static core::Result<WrittenUpdate> appendComplete(std::string_view original,
                                                      const XrefTable& table,
                                                      const ObjectDelta& delta);

    // BEFORE EITHER OF THEM, whenever anything will be appended: make `delta` safe to put after
    // `table`'s bytes. Stage 3 guide issue I-12, found by `fuzz_cos_roundtrip` (D3-17) -- the prefix
    // property says every original BYTE survives, and two things let an append change what those
    // bytes MEAN anyway:
    //
    //   * **An object that runs to the end of the file.** An unterminated dictionary, array or string,
    //     or a stream with no `endstream`, is parsed up to the last byte -- and after an append, into
    //     the update, taking its keys or its data for its own. Every such object is re-written into
    //     the update exactly as it was READ, so the new table places the copy whose meaning is fixed
    //     and the original bytes, kept verbatim, are no longer what any reader resolves.
    //   * **A number the file references without defining.** A dangling `9 0 R` means null; the
    //     allocator's floor (`/Size`, the largest defined number) cannot see it, and the xref stream
    //     a complete section writes would take 9 and give that reference a meaning. The delta's
    //     allocator is raised past the largest number any object references.
    //
    // It walks every object the table places, once -- the cost of a save of a file, not of a page,
    // which `rawpage_bench_save` measures. It never fails: an object that will not fetch is one no
    // reader resolves either, and is left where it is. Not const, because `XrefTable::fetch` caches.
    static void protectOriginal(XrefTable& table, ObjectDelta& delta);
    // The same in two halves (Stage 7 T4): the walk, which a caller composing after every command makes
    // once per revision, and its result put into one delta -- `protectOriginal` is exactly the two.
    static Protection protection(XrefTable& table);
    static void protect(const Protection& protection, ObjectDelta& delta);

    // The largest object number any object `table` places REFERENCES -- defined or not; -1 for none.
    // The second half of `protectOriginal`, alone, for a caller that ADDS objects to a delta and so must
    // raise the allocator's floor before the first number is handed out (Stage 6 T3: an annotation and
    // its appearance). The same walk over every object, so a caller computes it once per revision.
    static long long largestReference(XrefTable& table);
};

// A direct object in PDF syntax, from its VALUES (spans ignored -- see the note above). A Stream
// serializes as its dictionary only; the writer adds `/Length`, the keyword and the data.
std::string serializeObject(const CosObject& object);

} // namespace rawpage::pdf::cos

#endif // RAWPAGE_PDF_COS_INCREMENTALWRITER_H
