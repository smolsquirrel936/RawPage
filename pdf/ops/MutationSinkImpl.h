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
// REDACTION MARKS AND SOUNDS -- Stage 6 T14 (D6-25, D6-18). A mark RawPage creates, or redraws, is written with TWO
// forms: its `/AP /N` and its `/RO` (`AppearanceStreamBuilder::redactionOverlay`) -- and nothing else: a mark is an
// annotation like any other, and no page's content is read, let alone written (guide case A-100). A Sound RawPage
// creates gets its `/Sound` stream -- `/R`, `/C`, `/B`, `/E` and the samples, `/FlateDecode`d ONCE as an attached
// file is, for the same reason -- and verify compares that stream byte for byte with what was compressed.
//
// PAGES -- Stage 7 T2 (D7-2, D7-3, D7-24): the same rule over the page ORDER. The sink keeps every per-page
// fact -- the page's walk entry, its `/Annots` on disk and as desired, its rotation, its label -- in the order
// the document should have, and a page command PERMUTES them as its `PageMap` says, so every index-keyed rule
// above keeps comparing a page with ITSELF on disk. `delta()` lists that order in the page tree
// (`PageOps::composeTree`: only the nodes whose `/Kids` changed, a page's `/Parent` only where its node did,
// and each of the four inheritable keys a new parent would change made the page's own), writes `/PageLabels`
// back so each page keeps its label (`PageOps::labelTree`), and repairs what names a page the document no
// longer has (`ReferenceRepair`). An order put back -- a move undone, a delete restored -- equals the disk, and
// writes nothing. A DELETED page and everything the sink held for it is KEPT under the command's key, so a
// restore is exact before a save and after one; the values the repair changed in a saved revision are kept in
// a LEDGER, so an undo after the save writes them back (`ReferenceRepair` says how). A DUPLICATE is a new page
// object, numbered NEGATIVE until a compose gives it a number -- its dictionary the source's as it is now, its
// annotations COPIES written from the sources' dictionaries (`copies_`), its label the next of its range.
// The page tree is changed only where the walk read it without a note (`error.pdf.pageTreeDamaged`).
//
// THE COMPOSE KEEPS WHAT IT COMPOSED -- Stage 7 T4 (`Stage7-Assembly-Plan.md` Q4, `DW-4`'s second cause). D6-2
// composes the whole unsaved update after every command, so before T4 a one-page move after 1,500 unsaved
// highlights rebuilt and re-serialized 3,701 objects (25 ms of the UI thread; verify another 21). Two rules end
// that, and neither changes WHAT is written:
//   * **Every new object is numbered by NAME** (`cos::NumberBook`): `annot:<NM>`, `ap:<NM>`, `page:<n>`,
//     `shared:<key>`, ... -- kept from one compose to the next until a save, so a move renumbers nothing it did
//     not touch. A name no compose asks for any more gives its number back (see `cos::NumberBook`).
//   * **A UNIT whose inputs are what they were is written again as it was** (`cos::WrittenObject`, copied
//     verbatim): a created annotation -- its model, its page, the references it resolves, the shared objects
//     its appearance named -- an annotation edited in place, and a page's `/Annots` listing written into its
//     own array or a page dictionary nothing else changes. The inputs are COMPARED, every compose, rather
//     than tracked by the mutators: a comparison that is wrong rebuilds, where a missed mark would write a
//     stale object. Everything else -- the page tree, labels, repairs, the name tree, a copy on a duplicated
//     page -- is built again each time, as before; none of it grows with the unsaved annotations.
// `composed()` is what `RevisionWriter::compose` tells it once the writer has written the update: the round
// ends and each unit built in it keeps the bytes it was written as. `adopt()` drops all of it -- the next
// revision's numbers start above what the save wrote.
//
// PAGES FROM ELSEWHERE -- Stage 7 T5 (D7-4, D7-6). An insert or a replace hands the sink a GRAPH: the payload
// `ObjectGraphCopier` wrote, a small PDF holding the copied pages and everything they reach. The sink parses it
// once, keeps it by the command's key (`graphs_`) and takes its objects from it at every compose -- each written
// under the book name `g:<key>:<number>`, so a compose renumbers nothing it did not touch, and written again as
// it was, as ONE unit, when nothing it maps to has changed. A graph is written only while a page of the document
// draws from it, and after a save that wrote it, never again (`Graph::onDisk`): a page put back by a later undo
// points at the objects the file now holds.
//   * An INSERTED page is a new page, numbered negative until a compose gives it a number, as a duplicate is --
//     its dictionary the payload page's, its annotations COPIES written from the payload's dictionaries
//     (`copies_`, the same path a duplicate's copies take), its label the next of its range.
//   * A REPLACED page KEEPS ITS OBJECT (the user's choice, 2026-10-31): `replaced_` says which payload page it
//     draws, and the compose writes its dictionary with every key that DRAWS -- content, resources, the five
//     boxes, rotation, group, user unit -- taken from it and every other key its own; the payload page's
//     annotations are listed after its own. An undo puts back what it drew; after a save, from the dictionary
//     it had (`restored_`), so the undo is exact on either side of one.
// `drawnAs_` holds, per page object, the page as it was drawn when it arrived -- what the backend's page tree
// shows for a page the engine did not open (`drawnFacts`).
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
#include "core/doc/CopiedPages.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/ReferenceRepair.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

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
    // with it. S7 T4: its new objects are numbered by this sink's book, which the delta points at.
    core::Result<cos::ObjectDelta> delta();
    // S7 T4: `IncrementalWriter::protectOriginal` for `delta`, from a walk made once per revision.
    void protect(cos::ObjectDelta& delta);
    // S7 T4: the writer wrote `delta()`'s update as `update` -- every unit built in the compose keeps the objects it
    // was written as (see the header); the book's round ended with `delta()`. Not called for a compose that failed.
    void composed(const cos::WrittenUpdate& update);
    // S7 T4: the same for a compose that wrote nothing at all.
    void composedNothing();
    // S7 T4's TEST SEAM (guide A-128): everything composed is forgotten -- every unit, the annotations' block -- and the
    // book KEPT, so the next compose builds every object from nothing at the numbers it has. What it writes must be
    // byte for byte what a compose that kept them writes.
    void forgetComposedForTesting();

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

    // --- Pages (Stage 7 T2) -- see the header ----------------------------------------------------
    core::Result<core::PageCommandReport> reorderPages(const core::PageMap& map) override;
    core::Result<core::PageCommandReport> removePages(const std::vector<int>& pages, const QString& key) override;
    core::Status restorePages(const std::vector<int>& pages, const QString& key) override;
    core::Result<core::DuplicatedPages> duplicatePages(const std::vector<int>& pages, const QString& key,
                                                       const std::vector<std::vector<core::AnnotationCopy>>& copies) override;
    // Whether page `pageIndex` is a NEW page no save has written yet -- verify's question.
    bool isNewPage(int pageIndex) const;
    // S7 T3 (D7-3): for each page of the desired order, the index in the NEWEST REVISION ON DISK of the page
    // it shows -- itself, or for a page a duplicate made, the page it copies; -1 for one it cannot name. What
    // the engine draws at each index once `showEdits` hands it this state, which is what the backend's page
    // tree follows.
    std::vector<int> desiredDiskIndices() const;
    // Each page's label once the desired state is written; empty for a document with no /PageLabels.
    QString pageLabel(int pageIndex) const;

    // --- Pages from elsewhere (Stage 7 T5) -- see the header ----------------------------------------
    core::Result<core::InsertedPages> insertPages(int at, const QString& key, const QByteArray& graph) override;
    core::Result<core::InsertedPages> replacePages(const std::vector<int>& pages, const QString& key,
                                                   const QByteArray& graph) override;
    core::Status restoreReplacedPages(const std::vector<int>& pages, const QString& key) override;
    // For each page of the desired order: the page as it was DRAWN when it arrived, for a page the engine did not
    // open -- inserted, replaced, or a duplicate of one -- and nullopt for every other. What the backend's page tree
    // holds for it (D7-3's tree, in which a page carries the facts it was first drawn with).
    std::vector<std::optional<core::Page>> drawnFacts() const;

    // --- Annotations (Stage 6 T3) --------------------------------------------------------------
    // Whether a CREATED annotation can be written new: a kind this build draws (D6-6) or one with no appearance at
    // all, and a FileAttachment carrying its file. What `insertAnnotations` asks of each, and -- Stage 6 T13 -- what
    // `IDocumentBackend::annotationWritable` answers before a command exists.
    static core::Status canCreate(const core::Annotation& annotation);
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

    // T14: a Sound's samples AS THE DOCUMENT NOW STANDS (`IDocumentBackend::soundSamples`): one created and not saved
    // from the command that carries them, one the file lists from its `/Sound` stream, its filters undone.
    // `NotFound` (`error.sound.noData`) for an annotation that is no Sound with a stream.
    core::Result<QByteArray> soundSamples(const core::AnnotationId& annotation);

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
    // S7 T4: `name` prefixes the book names of the appearance objects it adds (`<name>:ap`, `<name>:ro`).
    core::Result<cos::CosObject> editedFrom(const cos::CosObject& onDisk, const Edit& edit, cos::ObjectDelta& delta,
                                            const std::map<QString, long long>& written, AppearanceObjects& shared,
                                            const std::string& name);
    // T6: where a reference to `id` is written -- see the header. `written` is this compose's created numbers.
    std::optional<cos::CosRef> resolveReference(const core::AnnotationId& id,
                                                const std::map<QString, long long>& written) const;

    std::string_view bytes_;
    cos::XrefTable table_;
    PageWalk walk_;                // S7 T2: the DESIRED order -- a new page's number negative (see the header)
    std::map<int, int> desired_;   // pageIndex -> rotation, only where it differs from the disk

    // --- Pages (Stage 7 T2) ----------------------------------------------------------------------
    // One page and everything the sink holds for it, as a page command takes it out and puts it back.
    struct PageSlot {
        PageEntry entry;
        PageListing disk;
        std::optional<int> rotation;
        std::optional<std::vector<ListedEntry>> listing;
        std::optional<PageLabelRange> label;
        std::map<QString, core::Annotation> created;   // created annotations its listing names
    };
    std::vector<PageSlot> takeSlots(const core::PageMap& map) const;
    void applyOrder(const core::PageMap& map, std::vector<PageSlot> inserted);
    core::Status mayRestructure() const;   // the tree read with no note
    std::map<long long, int> indexOfPages() const;   // page object -> index, the real pages
    const ReferenceRepair::Scan& referenceScan();
    std::set<long long> gonePages() const;
    long long objectOf(int pageIndex) const;   // a new page's number in THIS compose
    core::Status composePageOrder(cos::ObjectDelta& delta, std::map<long long, cos::CosObject>& pending,
                                  std::set<int>& handled);
    core::Status composeLabels(std::map<long long, cos::CosObject>& pending);
    core::Status composeRepairs(std::map<long long, cos::CosObject>& pending);
    core::Result<cos::CosObject> copiedDictionary(const QString& nm, const cos::CosRef& page, cos::ObjectDelta& delta,
                                                  const std::map<QString, long long>& written, AppearanceObjects& shared);

    PageWalk diskWalk_;                                   // the newest revision on disk, in its order
    std::vector<PageLabelRange> labels_;                  // desired, by index; empty: no /PageLabels
    // S7 T5: a page of a graph -- the command's key and the page's number in the payload.
    struct GraphPage {
        QString key;
        long long local = 0;
        bool operator==(const GraphPage& other) const { return key == other.key && local == other.local; }
    };
    struct NewPage {
        long long source = 0;                             // the page on disk it copies; 0 for one from a graph
        std::optional<GraphPage> graph;                   // S7 T5: the payload page it draws
        bool duplicate = false;                           // S7 T5: a duplicate of it, not the inserted page itself
    };
    std::map<long long, NewPage> newPages_;               // by its negative number
    long long nextNewPage_ = -1;
    std::map<QString, std::vector<PageSlot>> stash_;      // what each removal took, by its command's key
    // An annotation COPIED onto a new page: what it copies, its dictionary as it was when copied (and the
    // edit pending on it then), and the copy as the model holds it -- its references.
    struct CopySource {
        cos::CosObject dictionary;
        std::optional<Edit> edit;
        core::Annotation model;
        QString graph;   // S7 T5: the payload `dictionary` is read from -- its references are the payload's; empty: the file's
    };
    std::map<QString, CopySource> copies_;                // by the copy's /NM; kept until a save writes it

    std::set<long long> knownPages_;                      // every page object a revision on disk listed
    ReferenceRepair::Ledger ledger_;                      // values the repair changed in a saved revision
    ReferenceRepair::Ledger composedLedger_;              // what the last compose's repair changed
    std::optional<ReferenceRepair::Scan> scan_;           // through the ledger; dropped by adopt
    std::optional<PageTreeShape> shape_;                  // of the disk; dropped by adopt
    std::map<long long, long long> composeNumbers_;       // a new page -> the number the current compose gave it

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
    core::Status composeFiles(cos::ObjectDelta& delta, std::map<long long, cos::CosObject>& pending);
    // The `/EmbeddedFile` stream and the file specification for `file`, added to `delta`; the reference to the
    // specification. `compressed` is the stream's data, `/FlateDecode`d once (`compressedFor`).
    // S7 T4: numbered by the book as `<name>:stream` and `<name>:spec`.
    static cos::CosRef addFileSpecification(cos::ObjectDelta& delta, const std::string& name, const core::EmbeddedFile& file,
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

    // --- The compose that keeps what it composed (Stage 7 T4) -- see the header ---------------------
    cos::NumberBook& book();
    const cos::Protection& protection();
    std::optional<cos::NumberBook> book_;            // dropped by adopt
    std::optional<cos::Protection> protection_;      // dropped by adopt
    std::uint64_t round_ = 0;                        // the compose in progress, or the last
    std::size_t lastWritten_ = 0;                    // objects the last compose wrote
    using Written = std::shared_ptr<const cos::WrittenObject>;
    // ONE UNIT's objects: their book names and numbers, the shared appearance objects it named, and -- once the
    // writer has written them -- each object as written. `round` is the compose that last used it.
    struct UnitObjects {
        std::vector<std::string> names;
        std::vector<long long> numbers;
        std::vector<std::string> shared;
        std::vector<Written> written;
        std::uint64_t round = 0;
        bool complete() const noexcept { return !numbers.empty() && written.size() == numbers.size(); }
    };
    // A created annotation, as the last compose that built it built it.
    struct CreatedUnit {
        core::Annotation annotation;
        cos::CosRef page;
        std::vector<std::optional<cos::CosRef>> references;   // what its references resolved to
        UnitObjects objects;                                  // the dictionary first
    };
    std::map<QString, CreatedUnit> createdUnits_;            // by /NM
    // An annotation on disk edited in place: its edit, and the references it resolved.
    struct EditUnit {
        Edit edit;
        std::vector<std::optional<cos::CosRef>> references;
        UnitObjects objects;                                  // the replaced object first
    };
    std::map<long long, EditUnit> editUnits_;                // by its object number
    // A page's `/Annots` listing written into its own array object, or into a page dictionary nothing else
    // in the compose changed: what was listed, and the object as written.
    struct ListingUnit {
        std::vector<cos::CosObject> items;
        bool intoPage = false;
        UnitObjects objects;
    };
    std::map<long long, ListingUnit> listingUnits_;          // by the number it is written at
    // A shared appearance object (a font, an image): what it is, and as written.
    struct SharedUnit {
        cos::CosObject object;
        std::optional<std::string> data;
        UnitObjects objects;
    };
    std::map<std::string, SharedUnit> sharedUnits_;          // by key
    // EVERY object the last compose wrote for the annotations, for a compose after which nothing about them changed
    // -- `annotationEpoch_`, moved by each command that can change them -- to write again whole (`composeAnnotations`).
    struct AnnotationBlock {
        std::uint64_t epoch = 0;
        std::uint64_t round = 0;
        std::vector<std::string> names;      // every book name the annotations asked for
        std::vector<long long> numbers;      // every object, ascending
        std::vector<long long> intoPages;    // the page dictionaries among them, which carry nothing but a listing
        std::vector<Written> written;
        bool complete() const noexcept { return !numbers.empty() && written.size() == numbers.size(); }
    };
    AnnotationBlock block_;
    std::uint64_t annotationEpoch_ = 0;
    class DeltaObjects;                                      // the compose's `AppearanceObjects`
    // Re-lists `objects` in `delta` as written, marking each name used; false -- the unit is to be built
    // again -- when one is not written, its name now has another number, or a shared object cannot follow.
    bool reuseUnit(UnitObjects& objects, cos::ObjectDelta& delta, DeltaObjects& shared);

    // --- Pages from elsewhere (Stage 7 T5) -- see the header ----------------------------------------
    struct Graph {
        QByteArray bytes;                                 // the payload, which `table` views
        cos::XrefTable table;
        PageWalk walk;
        std::set<long long> pageObjects;                  // its pages, by their number in the payload
        std::set<long long> annotationObjects;            // the annotations they list -- written as copies
        std::vector<long long> objects;                   // every other object it holds, ascending
        std::map<long long, long long> onDisk;            // after a save that wrote them: each one's number there
        std::map<long long, long long> composedAt;        // the last compose's numbers for them
        // ONE unit (S7 T4's rule): its objects as the last compose wrote them, and what that compose mapped
        // every payload number to -- the same mapping writes the same bytes.
        std::vector<long long> mapping;
        UnitObjects unit;
        // Stage 7 guide I-07: the optional-content groups its pages use, as the copier narrowed the source's
        // `/OCProperties` -- payload numbers; none when it uses none.
        std::optional<cos::CosObject> layers;
        std::set<long long> layerGroups;
    };
    // The payload `graph`, parsed and checked: refused by key for one that is not a payload this sink can take.
    static core::Result<Graph> parseGraph(const QByteArray& graph);
    // The graph under `key` -- parsed once, kept for the session (a page put back by an undo draws from it).
    core::Result<Graph*> graphFor(const QString& key, const QByteArray& graph);
    // `graph`'s page `index` as it draws: its boxes and rotation, from the payload's own dictionary.
    static core::Page pageFactsOf(Graph& graph, int index);
    // Every annotation the payload's pages list, as COPIES named `<key>-0`, `<key>-1`, ... over the pages in order --
    // the model's listings (page `i`'s copies owned by `targets[i]`), the sink's, and each copy's source. Changes
    // nothing: the caller keeps what it takes. A copy a save already wrote is listed as the object it is.
    struct Copies {
        std::vector<core::AnnotationStore::PageListing> model;
        std::vector<std::vector<ListedEntry>> listings;
        std::vector<std::pair<QString, CopySource>> sources;
    };
    Copies copiesOf(const QString& key, Graph& graph, const std::vector<int>& targets) const;
    // Each new page's number for this compose, by name: `g:<key>:<n>` for an inserted page, `page:<n>` for
    // any other (S7 T4). Before anything else in the compose, so a graph can name the pages it points at.
    void numberNewPages(cos::ObjectDelta& delta);
    std::string newPageName(long long number) const;
    // Every graph a page of the document draws from, written -- or written again as it was, or not at all once a
    // save wrote it -- and `graphMaps_` filled: each payload number's number in this compose, -1 for none.
    core::Status composeGraphs(cos::ObjectDelta& delta);
    // A value of `key`'s payload with every reference renumbered through `graphMaps_` -- null for what this
    // compose does not write.
    cos::CosObject mapped(const QString& key, const cos::CosObject& value) const;
    // A graph page's dictionary as a page of THIS document draws it: renumbered, its tree and listing keys gone.
    core::Result<cos::CosObject> graphPage(const GraphPage& page) const;
    // Each replaced page's dictionary, and each restored one's (see the header), into `pending`.
    core::Status composeReplacements(std::map<long long, cos::CosObject>& pending, std::set<int>& handled);
    // Stage 7 guide I-07: the document's `/OCProperties` as the pages it draws need it -- the disk's, every group a graph
    // added that no page draws from now taken out, and every live graph's groups added with their default states, so a
    // group its source hid stays hidden. Into `pending` (the catalog, or `/OCProperties`' own object).
    core::Status composeLayers(std::map<long long, cos::CosObject>& pending);
    std::set<QString> liveGraphs() const;
    std::map<QString, Graph> graphs_;                     // by the command's key
    std::map<QString, std::map<long long, long long>> graphMaps_;   // this compose's, by key
    std::map<long long, GraphPage> replaced_;             // a page on disk -> the payload page it draws
    std::map<long long, cos::CosObject> restored_;        // a page on disk -> the dictionary an undo writes back
    std::map<long long, core::Page> drawnAs_;             // by page object (negative for a new one)
    // A page ON DISK that draws a graph's page -- inserted, or replaced, and saved -- by the graph's key: what keeps that
    // graph's layers in the document after the save (`liveGraphs`).
    std::map<long long, QString> graphOfPage_;
    // What a replace took, per page, so its undo puts it back.
    struct ReplacedSlot {
        int index = 0;
        long long number = 0;
        PageEntry entry;                                  // the walk's entry before
        int rotation = 0;                                 // the rotation it displayed at before
        std::vector<ListedEntry> listing;                 // its listing before
        std::optional<GraphPage> replacement;             // what it drew from before, if anything
        std::optional<NewPage> newPage;                   // a new page: what it was before
        std::optional<cos::CosObject> restored;           // its `restored_` entry before
        std::optional<cos::CosObject> onDisk;             // a page on disk: its dictionary when replaced
        std::optional<core::Page> drawnAs;
        std::optional<QString> graphOf;                   // its `graphOfPage_` entry before
    };
    std::map<QString, std::vector<ReplacedSlot>> replacedStash_;
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_MUTATIONSINKIMPL_H
