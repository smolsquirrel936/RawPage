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
// EDITS IN PLACE -- Stage 6 T5 (D6-5): `replaceAnnotations` keeps, per annotation the file holds, the
// model as it is ON DISK and as it should BE -- only where the two differ, so an edit undone before a save
// writes nothing. `delta()` rewrites each such annotation from its dictionary on disk with only the changed
// modelled keys (`AnnotationOps::editedDictionary`), and redraws its `/AP` only where what it draws changed:
// an annotation carried into a new rectangle and otherwise drawn the same keeps the appearance it has,
// because ISO 32000-1 §12.5.5 maps that appearance into the rectangle (`core::isMappedCopy`). A change it
// cannot draw is refused when it is asked for, not at save. A DIRECT dictionary is written as a new object
// the first time it is edited and its page lists that object (D6-4) -- keeping, across the save, the
// (page, position) name the model gives it. A created annotation not yet saved is simply drawn anew.
//
// REFERENCES BETWEEN ANNOTATIONS -- Stage 6 T6. A note and its pop-up name each other (`/Popup`, `/Parent`),
// and a replace edit's strikeout names its caret (`/IRT`), before either has an object number. So a compose
// RESERVES a number for every created annotation it will write before it writes any of them, and every
// reference -- in a created dictionary and in an edited one -- is resolved through `resolveReference`: an
// object the file holds, a created annotation this delta writes, or one an earlier save wrote. A created
// target listed nowhere any more is written by nothing, and the key is left out rather than point at nothing.
// A `Popup` and a state reply (T9) are written with no `/AP` (`AppearanceStreamBuilder::drawsNothing`). Verify reads every created
// reference back as the object its target was written at.
//
// AN UNDO RETURNS THE DICTIONARY, NOT A REDRAWING OF IT. Every dictionary an edited annotation has had ON
// DISK is remembered beside the model it read as, from the first edit on; when an edit's result IS one of
// those models -- an undo after a save, a change put back -- that dictionary is written again verbatim, its
// producer's `/AP` with it. Found by guide case A-35 at T5: a highlight from the file recoloured, saved and
// undone was drawn by our builder in its old colour, not as the file had drawn it.
//
// ATTACHMENTS -- Stage 6 T11 (D6-17): the same rule over the catalog's `/Names /EmbeddedFiles` name tree. It keeps
// the tree the document should have -- entry by entry, each a pair the file holds kept AS WRITTEN or a file RawPage
// attaches -- only while that differs from the disk. `delta()` writes a new flat tree (one `/Names` array, sorted by
// the bytes of its keys, ISO 32000-1 §7.9.6) and points the catalog's `/Names` at it -- into the names dictionary's
// own object when it has one, else into the catalog -- plus, for each attached file, its `/EmbeddedFile` stream
// (`/FlateDecode`, compressed ONCE when it is attached, not at every compose: D6-2 composes after every command,
// and a 50 MB file recompressed for each highlight after it would be seconds of the UI thread each time) and its
// file specification. A file removed stays in the file, listed nowhere; one removed and put back -- an undo,
// before or after a save -- is listed again as the very pair it was, AT THE PLACE IT HAD, so a tree its producer
// wrote out of order is put back exactly. A `FileAttachment` annotation RawPage creates gets the same stream and
// specification as its `/FS`.
//
// WHAT IT DOES NOT DO. It refuses nothing on policy -- whether this document may be edited at all is
// `core/doc/Permissions` (D3-7, T4) -- and it writes nothing: the update's bytes, the temporary file
// and the replace are `IncrementalWriter`, `SaveCoordinator` and `platform::IDurableFiles`.

#ifndef RAWPAGE_PDF_OPS_MUTATIONSINKIMPL_H
#define RAWPAGE_PDF_OPS_MUTATIONSINKIMPL_H

#include "core/Result.h"
#include "core/backend.h"
#include "core/doc/Annotation.h"
#include "core/doc/Attachments.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
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
    // Stage 6 T5 -- see the header.
    core::Status replaceAnnotations(const std::vector<core::Annotation>& before,
                                    const std::vector<core::Annotation>& after) override;

    // --- Attachments (Stage 6 T11) -- see the header --------------------------------------------------
    core::Status insertAttachments(const std::vector<core::DocumentAttachment>& files) override;
    core::Status removeAttachments(const std::vector<QString>& keys) override;

    // One entry of the name tree, as the sink holds it.
    struct ListedFile {
        enum class Kind : std::uint8_t { Disk, Created };
        Kind kind = Kind::Disk;
        cos::CosObject keyObject;     // Disk: the key as written
        cos::CosObject value;         // Disk: the value as written (a reference, or a direct specification)
        QString key;                  // the model's name for it: the key decoded; empty for a key that is no string
        QByteArray keyBytes;          // what it sorts by: the key's bytes as written, or as RawPage writes them
        core::DocumentAttachment created;   // Created: the file, its bytes carried
        bool sameEntry(const ListedFile& other) const;
    };
    // The tree the document will have once the desired state is written, in order.
    const std::vector<ListedFile>& desiredFiles() const;
    bool hasAttachmentChanges() const noexcept { return desiredFiles_.has_value(); }

    // An attachment's bytes AS THE DOCUMENT NOW STANDS (`IDocumentBackend::attachmentData`): a file attached and
    // not saved from the edit that attached it, one the file holds from its stream. A `FileAttachment` annotation
    // likewise -- created, or one the file lists. `NotFound` for one the document does not hold.
    core::Result<QByteArray> attachmentData(const core::AttachmentRef& attachment);

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

    // An annotation the file holds, changed in place (T5): as on disk, as it will be written, and whether
    // its appearance is drawn anew. Null for one that is not edited.
    struct Edit {
        core::Annotation disk;
        core::Annotation now;
        bool redraw = false;
        std::optional<cos::CosObject> restore;   // `now` is a version the file has held: this dictionary
    };
    const Edit* editOf(const core::AnnotationId& id) const;

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
    // T5: an edited annotation must read back edited -- its `/Subtype`, its `/Rect`, and a well-formed `/AP`
    // where it was redrawn -- and a direct one first edited must be listed as the object it became.
    core::Result<Reconciled> reconcile(cos::XrefTable& written, const PageWalk& walk, bool strict) const;

    // T11: `written`'s name tree is the desired one -- every pair the sink kept, the same pair, and every attached
    // file a specification whose stream holds exactly the bytes compressed when it was attached. Returns the tree
    // as `written` holds it. `strict` (the save's verify): an unchanged tree must read back unchanged too.
    core::Result<std::vector<ListedFile>> reconcileFiles(cos::XrefTable& written, bool strict) const;

private:
    MutationSinkImpl(std::string_view bytes, cos::XrefTable table, PageWalk walk);

    std::vector<ListedEntry>& listingToChange(int page, std::map<int, std::vector<ListedEntry>>& scratch) const;
    void commitListings(std::map<int, std::vector<ListedEntry>> changed);
    core::Status composeAnnotations(cos::ObjectDelta& delta, std::map<long long, cos::CosObject>& pageDictionaries);
    long long referenceFloor();
    // T5: the pages whose /Annots the next delta writes -- those whose listing changed, and those listing a
    // direct dictionary that is edited (it becomes an object, and the page lists that object).
    std::vector<int> pagesToRewrite() const;
    // T5: an edited annotation's dictionary as it will be written, from `onDisk`; its appearance added to
    // `delta` when it is redrawn.
    // T7: its appearance's shared objects (a font) through `shared`, one per compose.
    core::Result<cos::CosObject> editedFrom(const cos::CosObject& onDisk, const Edit& edit, cos::ObjectDelta& delta,
                                            const std::map<QString, long long>& written, AppearanceObjects& shared);
    // T6: where a reference to `id` is written -- see the header. `written` is this compose's created numbers.
    std::optional<cos::CosRef> resolveReference(const core::AnnotationId& id,
                                                const std::map<QString, long long>& written) const;

    std::string_view bytes_;
    cos::XrefTable table_;
    PageWalk walk_;
    std::map<int, int> desired_;   // pageIndex -> rotation, only where it differs from the disk

    std::vector<PageListing> disk_;                              // every page's /Annots on disk
    std::map<int, std::vector<ListedEntry>> desiredListings_;   // only pages that differ from the disk
    std::map<QString, core::Annotation> created_;              // by /NM: listed, and not on disk
    std::map<QString, cos::CosRef> createdOnDisk_;             // by /NM: where a save put it
    // Entries removed that no object number names -- a direct dictionary, or one a save promoted -- by the
    // model's name, so an undo lists them again as they were (T3; T5 holds the entry, not just a value).
    std::map<core::AnnotationId, ListedEntry> removedEntries_;
    std::map<core::AnnotationId, Edit> edits_;                 // T5: annotations on disk edited in place
    // T5: every dictionary an edited annotation has had on disk, with the model it read as -- see the header.
    std::map<core::AnnotationId, std::vector<std::pair<core::Annotation, cos::CosObject>>> versions_;
    std::optional<cos::CosObject> dictionaryOnDisk(const core::AnnotationId& id);
    std::map<long long, int> arrayUsers_;                      // an /Annots array object -> its pages
    std::optional<long long> referenceFloor_;                  // largest number the file references

    // T11: the name tree on disk and as it should be (only while they differ), and every pair removed from it,
    // by key, with where it was and the order it went in -- an undo, before a save or after one, lists it again
    // there (see the header). Kept across a save, as `removedEntries_` is.
    struct RemovedFile {
        ListedFile entry;
        std::size_t position = 0;
        long long sequence = 0;
    };
    static std::vector<ListedFile> readFiles(cos::XrefTable& table);
    void commitFiles(std::vector<ListedFile> files);
    core::Status composeFiles(cos::ObjectDelta& delta);
    // The `/EmbeddedFile` stream and the file specification for `file`, added to `delta`; the reference to the
    // specification. `compressed` is the stream's data, `/FlateDecode`d once (`compressedFor`).
    static cos::CosRef addFileSpecification(cos::ObjectDelta& delta, const core::EmbeddedFile& file,
                                            std::string compressed);
    // Null when the bytes will not compress -- which zlib does not do short of running out of memory.
    const std::string* compressedFor(const QString& cacheKey, const QByteArray& data);
    std::vector<ListedFile> diskFiles_;
    std::optional<std::vector<ListedFile>> desiredFiles_;
    std::map<QString, RemovedFile> removedFiles_;
    long long removalSequence_ = 0;
    // `/FlateDecode` of each carried file, compressed once: `key:<key>` for a document-level file, `nm:<nm>` for a
    // FileAttachment's. Dropped when nothing desired names it any more.
    struct Compressed {
        QByteArray data;          // what was compressed -- a new file under an old key is compressed anew
        std::string flate;
    };
    std::map<QString, Compressed> compressed_;
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_MUTATIONSINKIMPL_H
