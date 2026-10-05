// pdf/ops/MutationSinkImpl -- where a command's INTENT becomes an update to write. Stage 3 T3.
//
// The engine side of flow D (`Architecture-FileMap.md` §5): `RotatePageCommand::apply()` says
// "page 3 displays at 90 degrees" through `core::MutationSink`, and never touches bytes. This class
// decides which COS objects that means, and hands `pdf/cos/IncrementalWriter` a delta at save.
//
// -------------------------------------------------------------------------------------------
// DESIRED STATE, NOT A LOG (D3-5)
// -------------------------------------------------------------------------------------------
// It keeps, per page, the rotation the document should HAVE -- not the list of calls that got it
// there. `delta()` compares that desired state with the NEWEST REVISION ON DISK and emits only the
// pages that differ. Two properties follow, and guide cases A-31 and A-32 are each one of them:
//
//   * **An undo before a save writes nothing.** Rotate page 1 and rotate it back, or rotate it four
//     times: the desired rotation equals the one on disk, so the delta is empty -- not an update that
//     restates the original.
//   * **An undo after a save writes the page back.** `adopt()` moves the comparison onto the revision
//     just written; reverting then differs from THAT, so the delta holds the page's pre-rotation
//     dictionary. A sink that compared with the file as first opened would lose this undo on the next
//     save, silently.
//
// Which object and which dictionary is `pdf/ops/PageOps`' decision, including the rule that a
// rotation is never written onto an ancestor `/Pages` node.
//
// -------------------------------------------------------------------------------------------
// OWNERSHIP
// -------------------------------------------------------------------------------------------
// It owns the `XrefTable` of the revision it compares against and does NOT own the bytes that table
// points into -- the backend holds the opened bytes for the document's lifetime (D3-4), and a copy
// here would be a second 200 MB buffer for nothing. `adopt()` takes the next revision's bytes on the
// same terms. Not thread-safe: the journal calls it from one thread, and the save path is T8's.
//
// -------------------------------------------------------------------------------------------
// ANNOTATIONS -- Stage 6 T3 (D6-1, D6-4, D6-5): the same rule, over each page's `/Annots` listing
// -------------------------------------------------------------------------------------------
// It keeps, per page, the LISTING the page should have -- entry by entry, each either a reference the
// file already holds, a direct entry kept as written, or an annotation RawPage created -- and only for
// pages where that differs from the disk. A page whose listing is put back (add then undo; delete then
// undo) drops out, so an undo before a save writes nothing (guide case A-25). `delta()` writes, for
// each page that differs: the page's `/Annots` -- into its indirect array object when that object is
// the page's alone, else into the page dictionary, keeping every other key -- and, for each created
// annotation, its dictionary (`AnnotationOps::createdDictionary`) and its `/AP` normal appearance
// (`AppearanceStreamBuilder`). NOTHING ELSE: an annotation that is only re-listed or un-listed is not
// rewritten, so a deleted one's dictionary stays in the file exactly as it was, and an undone delete
// lists that same object again.
//
// IDENTITY ACROSS A SAVE. The model names a created annotation by its `/NM` for its whole life (D6-4);
// `adopt()` finds where the save put each one -- by `/NM`, on the page that lists it -- and from then
// on lists that object rather than writing a new one. A direct entry the model names by its page and
// position AT OPEN keeps that name across a save that moved it, and a removed one is kept here, by that
// name, so an undo after the save can list it again as it was written.
//
// WHAT IT DOES NOT DO. It refuses nothing on policy -- whether this document may be edited at all is
// `core/doc/Permissions` (D3-7, T4) -- and it writes nothing: the update's bytes, the temporary file
// and the replace are `IncrementalWriter`, `SaveCoordinator` and `platform::IDurableFiles`.

#ifndef RAWPAGE_PDF_OPS_MUTATIONSINKIMPL_H
#define RAWPAGE_PDF_OPS_MUTATIONSINKIMPL_H

#include "core/Result.h"
#include "core/backend.h"
#include "core/doc/Annotation.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/PageOps.h"

#include <map>
#include <memory>
#include <string_view>

namespace rawpage::pdf::ops {

class MutationSinkImpl final : public core::MutationSink {
public:
    // Reads the table and walks the page tree of `bytes`, which must outlive the sink (or the next
    // `adopt()`). Fails for a file with no usable cross-reference table or no page tree -- an
    // encrypted file is declined one layer down, by name (D2-6).
    static core::Result<std::unique_ptr<MutationSinkImpl>> open(std::string_view bytes);

    // core::MutationSink. `degrees` is normalized: -90 is 270 and 450 is 90. A value that is not a
    // multiple of 90, or a page index outside the document, is refused with nothing changed.
    core::Status setPageRotation(int pageIndex, int degrees) override;

    // The update the desired state needs, against the newest revision on disk. Empty when nothing
    // differs. The delta is built over `table()`, which is what `IncrementalWriter` must be handed
    // with it.
    core::Result<cos::ObjectDelta> delta();

    // After a save: compare against `bytes` from now on. The page count must be unchanged -- nothing
    // at Stage 3 adds or removes a page -- and desired rotations now equal to the disk are dropped. Every
    // annotation listing this sink changed must read back as it composed it (`reconcile`), or nothing
    // moves; after it, no annotation change is pending.
    core::Status adopt(std::string_view bytes);

    int pageCount() const noexcept { return static_cast<int>(walk_.pages.size()); }
    const PageWalk& pages() const noexcept { return walk_; }
    std::string_view bytes() const noexcept { return bytes_; }
    const cos::XrefTable& table() const noexcept { return table_; }
    // For `RevisionWriter::compose`, which hands it to `IncrementalWriter::protectOriginal` -- whose
    // walk fetches, and fetching caches.
    cos::XrefTable& table() noexcept { return table_; }

    // The rotation page `pageIndex` will display at once the desired state is written.
    int desiredRotation(int pageIndex) const;

    // --- Annotations (Stage 6 T3) --------------------------------------------------------------
    core::Status insertAnnotations(const std::vector<core::AnnotationPlacement>& at,
                                   const std::vector<core::Annotation>& annotations) override;
    core::Status removeAnnotations(const std::vector<core::AnnotationPlacement>& at) override;

    // One entry of a page's `/Annots`, as the sink holds it.
    struct ListedEntry {
        enum class Kind : std::uint8_t { Reference, Value, Created };
        Kind kind = Kind::Value;
        cos::CosRef reference;                  // Reference: an object the file holds
        cos::CosObject value;                   // Value: a direct entry, kept as written
        QString nm;                             // Created: an annotation RawPage writes
        std::optional<core::AnnotationId> id;   // the model's name for it; none for a non-annotation

        bool sameEntry(const ListedEntry& other) const;   // the same bytes would be listed
    };
    struct PageListing {
        std::vector<ListedEntry> entries;
        bool present = false;                   // the page dictionary has an /Annots key at all
        std::optional<long long> arrayObject;   // /Annots is a reference to this array object
    };

    // The listing page `page` will have once the desired state is written; empty past the last page.
    const std::vector<ListedEntry>& desiredListing(int page) const;
    // A created annotation not yet on disk, by its `/NM`; null for one that is not.
    const core::Annotation* createdAnnotation(const QString& nm) const;
    // Pages whose listing differs from the disk.
    std::vector<int> pagesWithAnnotationChanges() const;

    // Every page's `/Annots` as `table` holds it, entries identified as the reader at open names them.
    static std::vector<PageListing> readListings(cos::XrefTable& table, const PageWalk& walk);

    // THE CHECK `RevisionWriter::verify` AND `adopt()` SHARE: `written` is a file this sink composed, so
    // every page's listing in it must be the desired one -- a created annotation as a reference to a
    // dictionary with its `/NM`, its `/Subtype`, its page as `/P`, and an `/AP /N` stream that is
    // well-formed. Returns the listings as they are in `written`, each entry carrying the model's id.
    // `strict` (the save's verify): a page this sink did not change must read back unchanged too. Not
    // strict (adopt): such a page is taken as the file has it.
    struct Reconciled {
        std::vector<PageListing> listings;
        std::map<QString, cos::CosRef> created;   // by /NM: where `written` put each created annotation
    };
    core::Result<Reconciled> reconcile(cos::XrefTable& written, const PageWalk& walk, bool strict) const;

private:
    MutationSinkImpl(std::string_view bytes, cos::XrefTable table, PageWalk walk);

    std::vector<ListedEntry>& listingToChange(int page, std::map<int, std::vector<ListedEntry>>& scratch) const;
    void commitListings(std::map<int, std::vector<ListedEntry>> changed);
    core::Status composeAnnotations(cos::ObjectDelta& delta, std::map<long long, cos::CosObject>& pageDictionaries);
    long long referenceFloor();

    std::string_view bytes_;
    cos::XrefTable table_;
    PageWalk walk_;
    std::map<int, int> desired_;   // pageIndex -> rotation, only where it differs from the disk

    std::vector<PageListing> disk_;                              // every page's /Annots on disk
    std::map<int, std::vector<ListedEntry>> desiredListings_;   // only pages that differ from the disk
    std::map<QString, core::Annotation> created_;              // by /NM: listed, and not on disk
    std::map<QString, cos::CosRef> createdOnDisk_;             // by /NM: where a save put it
    std::map<core::AnnotationId, cos::CosObject> removedDirect_;   // removed direct entries, by name
    std::map<long long, int> arrayUsers_;                      // an /Annots array object -> its pages
    std::optional<long long> referenceFloor_;                  // largest number the file references
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_MUTATIONSINKIMPL_H
