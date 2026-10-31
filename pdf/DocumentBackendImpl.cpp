#include "pdf/DocumentBackendImpl.h"

#include "core/Log.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/Attachments.h"
#include "core/doc/CopiedPages.h"
#include "core/doc/DocumentId.h"
#include "core/doc/Fdf.h"
#include "core/doc/Permissions.h"
#include "core/text/SearchUnit.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AttachmentFiles.h"
#include "pdf/ops/FdfCodec.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/EmbeddedFileReader.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/TransitionReader.h"
#include "pdf/ops/RevisionWriter.h"
#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumRasterizer.h"
#include "pdf/render/PdfiumTextExtractor.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace rawpage::pdf {
namespace {

// D6-2's refusal: the failure's own key as the context of `error.edit.notShown`.
core::Error notShown(const core::Error& error)
{
    return core::Error{error.code, QLatin1String("error.edit.notShown"),
                       error.context.isEmpty() ? QString(error.messageKey)
                                               : QString(error.messageKey) + QLatin1Char(':') + error.context};
}

// S6 T2 (D6-3): what the annotation reading met, said once at open. The count of each kind of note,
// every unplaceable annotation by object number -- the case guide A-19 looks for in the log -- and
// every page the two readers disagree on, which is a page whose comments cannot be changed.
void logAnnotationReading(const QString& path, const core::AnnotationStore& store,
                          const core::Permissions& permissions)
{
    if (store.size() == 0 && store.notes().empty()) return;
    std::map<core::AnnotationNoteKind, int> counts;
    for (const core::AnnotationNote& note : store.notes()) {
        ++counts[note.kind];
        if (note.kind == core::AnnotationNoteKind::Unlisted) {
            RAWPAGE_LOG_INFO(core::LogSubsystem::Render)
                << "annotations:" << path << "object" << note.object
                << "is an annotation no page lists; it has no page and is ignored";
        }
    }
    QStringList summary;
    for (const auto& [kind, count] : counts)
        summary << QStringLiteral("%1 %2").arg(count).arg(QLatin1String(core::annotationNoteName(kind)));
    RAWPAGE_LOG_INFO(core::LogSubsystem::Render)
        << "annotations:" << path << "--" << static_cast<int>(store.size()) << "read"
        << (summary.isEmpty() ? QString() : QStringLiteral("(") + summary.join(QStringLiteral(", ")) + QLatin1Char(')'));
    for (const int page : permissions.annotationDisagreements()) {
        RAWPAGE_LOG_WARNING(core::LogSubsystem::Render)
            << "annotations:" << path << "page" << page + 1
            << "-- the engine and the first-party reader read its /Annots differently; read-only";
    }
}

// S7 T3 (D7-3): for each page the sink's desired state draws, its index in the tree read at open -- the sink's
// order read through `diskFrom`, which places each page of the newest revision on disk in that tree. With no
// sink, nothing has moved: the identity over the open tree.
std::vector<int> drawnFrom(const ops::MutationSinkImpl* sink, const std::vector<int>& diskFrom, int openCount)
{
    std::vector<int> from;
    if (sink == nullptr) {
        from.resize(static_cast<std::size_t>(openCount));
        for (std::size_t i = 0; i < from.size(); ++i) from[i] = static_cast<int>(i);
        return from;
    }
    for (const int onDisk : sink->desiredDiskIndices())
        from.push_back(onDisk >= 0 && onDisk < static_cast<int>(diskFrom.size())
                           ? diskFrom[static_cast<std::size_t>(onDisk)] : -1);
    return from;
}

// ... and whether each is the open page OBJECT itself: a page on disk that was one, not a copy the sink is making.
std::vector<bool> drawnOriginals(const ops::MutationSinkImpl* sink, const std::vector<bool>& diskOriginal, int openCount)
{
    if (sink == nullptr) return std::vector<bool>(static_cast<std::size_t>(openCount), true);
    std::vector<bool> originals;
    const std::vector<int> onDisk = sink->desiredDiskIndices();
    for (std::size_t i = 0; i < onDisk.size(); ++i) {
        const int d = onDisk[i];
        originals.push_back(!sink->isNewPage(static_cast<int>(i)) && d >= 0 && d < static_cast<int>(diskOriginal.size())
                            && diskOriginal[static_cast<std::size_t>(d)]);
    }
    return originals;
}

// A destination read at open, carried to where its page OBJECT is drawn now; -1 -- unresolved -- for a page no
// longer drawn, which is what `ReferenceRepair` makes of a destination to a deleted page (D7-2). A copy a
// duplicate made shows the same page and is not what a destination names.
core::Destination carried(core::Destination destination, const std::vector<int>& drawnIndexOf)
{
    if (destination.pageIndex >= 0)
        destination.pageIndex = destination.pageIndex < static_cast<int>(drawnIndexOf.size())
                                    ? drawnIndexOf[static_cast<std::size_t>(destination.pageIndex)] : -1;
    return destination;
}

void carryItems(std::vector<core::OutlineItem>& items, const std::vector<int>& drawnIndexOf)
{
    for (core::OutlineItem& item : items) {
        item.destination = carried(item.destination, drawnIndexOf);
        carryItems(item.children, drawnIndexOf);
    }
}

// The outline and the named destinations PDFium read at open, as they name pages in the order drawn now.
void carryReaders(const render::PdfiumDocument& document, const std::vector<int>& from,
                  const std::vector<bool>& originals, core::Outline& outline, core::Destinations& destinations)
{
    std::vector<int> drawnIndexOf(static_cast<std::size_t>(document.pages().count()), -1);
    for (std::size_t i = 0; i < from.size(); ++i) {
        const int open = from[i];
        if (open >= 0 && open < static_cast<int>(drawnIndexOf.size()) && i < originals.size() && originals[i])
            drawnIndexOf[static_cast<std::size_t>(open)] = static_cast<int>(i);
    }
    core::Outline carriedOutline = document.outline();
    carryItems(carriedOutline.roots(), drawnIndexOf);
    core::Destinations carriedDestinations;
    for (core::NamedDestination entry : document.destinations().all()) {
        entry.destination = carried(entry.destination, drawnIndexOf);
        carriedDestinations.add(std::move(entry));
    }
    outline = std::move(carriedOutline);
    destinations = std::move(carriedDestinations);
}

} // namespace

// One open document and the rasterizer and text extractor that serve it. Both hold the document by
// reference, so declaration order here is the destruction contract: they are destroyed before
// `document` because members are destroyed in reverse order of declaration.
struct DocumentBackendImpl::Open {
    std::unique_ptr<render::PdfiumDocument> document;
    std::unique_ptr<render::PdfiumRasterizer> rasterizer;
    // Stage 5 T2 (D5-2). Made once the handle exists, in `open()`, because it refuses every handle
    // but its own (guide case A-25).
    std::unique_ptr<render::PdfiumTextExtractor> extractor;

    // Stage 3 T4. The bytes the engine loaded, held by this entry so the first-party sink's views into
    // them stay valid: a `QByteArray` copy shares the engine's buffer rather than duplicating it, and
    // nothing here ever writes to it, so its data pointer does not move.
    QByteArray bytes;
    core::DocumentId id;
    core::Permissions permissions;

    // S4 T10 (D4-12). Read from the FIRST-PARTY walk below, because PDFium exposes `/Trans`
    // nowhere -- see `pdf/ops/TransitionReader.h`. Empty for almost every document.
    core::PageTransitions transitions;
    // S5 T8 (D5-12). Each embedded file's name and /Desc, likewise first-party and for the same reason:
    // the engine reads attachments' names and bytes and never their descriptions
    // (`pdf/ops/EmbeddedFileReader.h`). Empty for almost every document, and for an encrypted one.
    std::vector<std::pair<QString, QString>> attachmentDescriptions;
    // S6 T2 (D6-3). Every annotation, read first-party off the same walk; refused, with the reason, for
    // a document that walk could not read.
    core::AnnotationStore annotations;
    // S6 T11 (D6-17). The files attached to the document as a whole, read off the same walk -- never their bytes.
    core::Attachments attachments;
    // S6 T11: opened from bytes (`openBytes`) -- an attached PDF, read-only until it is saved as a file of its own.
    bool fromBytes = false;
    // Null unless `permissions` allows editing: a sink for a document this build refuses to edit
    // would be a way to edit it (D3-7).
    std::unique_ptr<ops::MutationSinkImpl> sink;

    // Stage 3 T8. Set once a save has been adopted: the repair report of the file NOW on disk, which
    // after a correct write is empty. The engine's report describes the bytes it loaded, and those
    // stop being the newest revision at the first save -- a repaired document must not be forced to
    // Save-As a second time because the engine still remembers the first file.
    std::optional<core::RepairReport> savedReport;

    // Stage 6 T4 (D6-2): the last `showEdits`' compose and verify, timed on its caller's thread, and the
    // verify alone. S7 T4: the compose alone, on the caller's thread, and the verify on its own -- written there,
    // so read whenever.
    std::chrono::nanoseconds lastCompose{0};
    std::atomic<long long> lastVerifyNs{0};
    std::size_t lastObjectsWritten = 0;
    std::size_t lastObjectsReused = 0;

    // Stage 7 T4 (`Stage7-Assembly-Plan.md` Q4): THE VERIFY OF THE LAST `showEdits`, on a thread of its own, and what
    // to put back if it refuses -- each engine's tree, order and readers as that show found them. Joined before
    // anything touches the sink again (`awaitVerify`), and before the entry goes.
    struct Replaced {
        core::DocumentHandle target;
        core::PageTree drawn;
        std::vector<int> drawnFrom;
        std::vector<bool> drawnOriginal;
        std::vector<std::optional<core::Page>> drawnFacts;
        core::Outline outline;
        core::Destinations destinations;
    };
    std::thread verifying;
    core::Status verdict;                         // the thread's, read after it is joined
    bool unsettled = false;                       // a verdict `settleEdits` has not answered
    std::vector<core::DocumentHandle> gated;      // every engine the show held behind its gate
    std::vector<Replaced> replaced;
    std::mutex settledMutex;                      // `settled` is called, and replaced, under it
    std::function<void()> settled;
    ~Open()
    {
        if (verifying.joinable()) verifying.join();
    }

    // Stage 7 T3 (D7-3): WHAT THE ENGINE DRAWS, page by page -- the tree `pages()` hands out, assigned IN PLACE
    // at every `showEdits` (a view holds a pointer to it). Each page is the open tree's page it shows, so a
    // page carries its boxes and its rotation as read at open, wherever it moves. `diskFrom[i]` is the index in
    // the tree read at open of page `i` of the NEWEST REVISION ON DISK -- the identity until a save writes an
    // order of its own -- which is what reads the sink's order (`MutationSinkImpl::desiredDiskIndices`) back
    // to the open tree. Read and written on the UI thread only; the rasterizer reads the engine's own copy.
    core::PageTree drawn;
    std::vector<int> drawnFrom;   // `drawn`'s page i is the open tree's page `drawnFrom[i]`
    std::vector<int> diskFrom;
    // ... and whether each is the open tree's page OBJECT itself, or a copy a duplicate made of it -- per page drawn
    // and per page of the newest revision on disk. What names a page names the original, never its copy (D7-2).
    std::vector<bool> drawnOriginal;
    std::vector<bool> diskOriginal;
    // S7 T5 (D7-4): per page drawn, the page as it was first drawn when the engine never opened it -- inserted from
    // another document, or replaced by a page of one (`MutationSinkImpl::drawnFacts`); nullopt for every other.
    std::vector<std::optional<core::Page>> drawnFacts;

    // THE OUTLINE AND THE NAMED DESTINATIONS AS THEY NOW NAME PAGES (S7 T3): PDFium's reading at open, each
    // destination's page carried to where that page OBJECT is drawn now, or to *unresolved* (-1) where a command
    // removed it -- which is what `ReferenceRepair` makes of it in the file (D7-2). `outline()` and
    // `destinations()` hand these out; assigned under `mutex_`, because a search's engine task copies the outline
    // from the render thread (`searchUnits`).
    core::Outline outline;
    core::Destinations destinations;
};

DocumentBackendImpl::DocumentBackendImpl() = default;
DocumentBackendImpl::~DocumentBackendImpl() = default;

std::array<bool, core::kBackendInterfaceCount> DocumentBackendImpl::implementedInterfaces() const
{
    std::array<bool, core::kBackendInterfaceCount> implemented{};
    implemented.fill(false);
    implemented[static_cast<std::size_t>(core::BackendInterface::DocumentBackend)] = true;
    implemented[static_cast<std::size_t>(core::BackendInterface::PageRasterizer)] = true;
    // Stage 3 T4: `pdf/ops/MutationSinkImpl`, handed out per document by `mutationSink()` -- and only
    // for a document `Permissions` lets this build edit.
    implemented[static_cast<std::size_t>(core::BackendInterface::Mutation)] = true;
    // Stage 5 T2 (D5-2): `pdf/render/PdfiumTextExtractor`, per document like the rasterizer.
    implemented[static_cast<std::size_t>(core::BackendInterface::TextExtractor)] = true;
    // The other three stay false and their accessors stay null. Convention 6: a stub that silently
    // succeeds surfaces stages later as data nobody produced.
    return implemented;
}

// Both `find` overloads require `mutex_` to be held; every public entry point below takes it.
DocumentBackendImpl::Open* DocumentBackendImpl::find(core::DocumentHandle document)
{
    const auto found = open_.find(document.id);
    return found == open_.end() ? nullptr : found->second.get();
}

const DocumentBackendImpl::Open* DocumentBackendImpl::find(core::DocumentHandle document) const
{
    const auto found = open_.find(document.id);
    return found == open_.end() ? nullptr : found->second.get();
}

// AN OPEN IN PROGRESS (S5 T9). Its pieces in order: READ the file, the engine's opening -- whose
// pieces `PdfiumDocument::Opening` defines, the last of them first-party -- and ASSEMBLE. The engine's
// opening exists from the read on, because it owns the bytes.
struct DocumentBackendImpl::Opening {
    enum class Stage { Read, Engine, Assemble };
    QString path;
    core::PasswordCallback password;
    Stage stage = Stage::Read;
    std::unique_ptr<render::PdfiumDocument::Opening> engine;
    std::unique_ptr<render::PdfiumDocument> document;   // taken from `engine` when it is done
    // S6 T11: `openBytes` -- the bytes are given, so the read is no read, and the document names no file.
    std::optional<QByteArray> bytes;
    bool fromBytes = false;
};

core::Result<core::DocumentHandle> DocumentBackendImpl::open(const QString& path,
                                                             const core::PasswordCallback& password)
{
    // THE PIECES BELOW, RUN TO THE END IN ONE CALL (S5 T9): a window's open and a folder search's are
    // the same open, and the cases that cover one cover the other.
    const core::Result<core::OpenProgress> begun = beginOpen(path, password);
    if (!begun) return begun.error();
    core::OpenProgress progress = begun.value();
    while (!progress.done) {
        const core::Result<core::OpenProgress> next = continueOpen(progress.opening);
        if (!next) return next.error();
        progress = next.value();
    }
    return progress.handle;
}

core::Result<core::OpenProgress> DocumentBackendImpl::beginOpen(const QString& path,
                                                                const core::PasswordCallback& password)
{
    auto opening = std::make_unique<Opening>();
    opening->path = path;
    opening->password = password;
    const std::lock_guard<std::mutex> guard(mutex_);
    const core::OpeningId id = nextOpening_++;
    openings_.emplace(id, std::move(opening));
    core::OpenProgress progress;
    progress.opening = id;
    progress.nextNeedsEngine = false;   // the read
    return progress;
}

void DocumentBackendImpl::abandonOpen(core::OpeningId opening)
{
    std::unique_ptr<Opening> dropped;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        const auto found = openings_.find(opening);
        if (found == openings_.end()) return;
        dropped = std::move(found->second);
        openings_.erase(found);
    }
    // DESTROYED OUTSIDE THE MAP'S LOCK: a half-opened engine document closes under the ENGINE lock, and
    // the map's must never be held inside that one (`contentBox`'s rule).
}

core::Result<core::OpenProgress> DocumentBackendImpl::continueOpen(core::OpeningId id)
{
    Opening* opening = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        const auto found = openings_.find(id);
        if (found == openings_.end()) {
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.unknownDocument"),
                               QString::number(static_cast<qulonglong>(id))};
        }
        opening = found->second.get();
    }

    // A refusal ends the opening: it is dropped here, so a caller holds no opening it cannot use.
    const auto refuse = [this, id](core::Error error) -> core::Result<core::OpenProgress> {
        abandonOpen(id);
        return error;
    };

    core::OpenProgress progress;
    progress.opening = id;

    switch (opening->stage) {
    case Opening::Stage::Read: {
        if (opening->bytes) {
            // S6 T11: bytes already in hand -- an attached PDF -- and `path` is the name it is shown by.
            opening->engine = std::make_unique<render::PdfiumDocument::Opening>(std::move(*opening->bytes), opening->path,
                                                                                opening->password);
            opening->bytes.reset();
            opening->stage = Opening::Stage::Engine;
            progress.nextNeedsEngine = opening->engine->nextNeedsEngine();
            return progress;
        }
        const QFileInfo info(opening->path);
        if (!info.exists() || !info.isFile())
            return refuse(core::Error{core::ErrorCode::NotFound, QLatin1String("error.open.notFound"), opening->path});
        // Read through QFile, which takes a QString and gets the Windows encoding right by
        // construction. Handing PDFium a `const char*` path is convention 1's trap one layer over --
        // this machine's code page 950 turned an emoji filename into `report-?????.pdf`.
        QFile file(opening->path);
        if (!file.open(QIODevice::ReadOnly))
            return refuse(core::Error{core::ErrorCode::IoFailure, QLatin1String("error.open.notFound"), info.fileName()});
        QByteArray bytes = file.readAll();
        file.close();
        opening->engine = std::make_unique<render::PdfiumDocument::Opening>(std::move(bytes), opening->path,
                                                                            opening->password);
        opening->stage = Opening::Stage::Engine;
        progress.nextNeedsEngine = opening->engine->nextNeedsEngine();
        return progress;
    }

    case Opening::Stage::Engine: {
        // WHETHER THE BYTES ARE A PDF IS ANSWERED HERE, and this is the whole point of guide case
        // A-47. Stage 1 decided a file by its suffix and said so at the time (its case A-58, which
        // asserted the opposite and was correct then under D1-2). A `.dat` that is a PDF opens; a
        // `.pdf` that is not is refused with a named reason.
        if (const core::Status piece = opening->engine->step(); !piece) return refuse(piece.error());
        if (!opening->engine->done()) {
            progress.nextNeedsEngine = opening->engine->nextNeedsEngine();
            return progress;
        }
        opening->document = opening->engine->take();
        opening->engine.reset();
        opening->stage = Opening::Stage::Assemble;
        progress.nextNeedsEngine = false;
        return progress;
    }

    case Opening::Stage::Assemble: {
        const core::Result<core::DocumentHandle> handle = assemble(*opening);
        if (!handle) return refuse(handle.error());
        abandonOpen(id);   // done with: the document now belongs to its entry
        progress.done = true;
        progress.handle = handle.value();
        return progress;
    }
    }
    return refuse(core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.unknownDocument")});
}

core::Result<core::DocumentHandle> DocumentBackendImpl::openBytes(const QByteArray& bytes, const QString& name,
                                                                  const core::PasswordCallback& password)
{
    // `open()`'s pieces, the read skipped: one open, as `open()` itself is (S5 T9).
    auto opening = std::make_unique<Opening>();
    opening->path = name;
    opening->password = password;
    opening->bytes = bytes;
    opening->fromBytes = true;
    core::OpeningId id = 0;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        id = nextOpening_++;
        openings_.emplace(id, std::move(opening));
    }
    core::OpenProgress progress;
    progress.opening = id;
    while (!progress.done) {
        const core::Result<core::OpenProgress> next = continueOpen(id);
        if (!next) return next.error();
        progress = next.value();
    }
    return progress.handle;
}

core::Result<core::DocumentHandle> DocumentBackendImpl::assemble(Opening& opening)
{
    const QString& path = opening.path;
    auto entry = std::make_unique<Open>();
    entry->fromBytes = opening.fromBytes;
    entry->document = std::move(opening.document);
    entry->rasterizer = std::make_unique<render::PdfiumRasterizer>(*entry->document);
    // S7 T3: what the engine draws is, until a page command, what it opened.
    entry->drawn = entry->document->pages();
    entry->diskFrom.resize(static_cast<std::size_t>(entry->drawn.count()));
    for (std::size_t i = 0; i < entry->diskFrom.size(); ++i) entry->diskFrom[i] = static_cast<int>(i);
    entry->drawnFrom = entry->diskFrom;
    entry->diskOriginal.assign(entry->diskFrom.size(), true);
    entry->drawnOriginal = entry->diskOriginal;
    entry->outline = entry->document->outline();
    entry->destinations = entry->document->destinations();

    // --- Stage 3 T4: identity, the second reading, and the pre-flight (D3-7, D3-16) -----------------
    // The identity is taken from the bytes the ENGINE loaded, not from a second read of the file, so
    // the hash is of the document the user is looking at. The modification time is a separate stat,
    // and a file rewritten between the read and the stat would carry the new time with the old hash --
    // which makes the pair disagree with the disk, the conservative direction for D3-16's check.
    entry->bytes = entry->document->bytes();
    entry->id = entry->fromBytes ? core::DocumentId::of(QString(), 0, entry->bytes)
                                 : core::DocumentId::of(path, QFileInfo(path).lastModified().toMSecsSinceEpoch(), entry->bytes);

    // The FIRST-PARTY reading of the same bytes, through the sink that would carry any edit. Its page
    // walk and the engine's page tree must agree before editing is allowed: a rotation lands on the
    // object the walk calls page n and is drawn on the engine's page n.
    const std::string_view view(entry->bytes.constData(),
                                static_cast<std::size_t>(entry->bytes.size()));
    auto sink = ops::MutationSinkImpl::open(view);
    core::CosReading reading = core::CosReading::Read;
    std::vector<core::CosPageFacts> facts;
    if (!sink) {
        reading = sink.error().messageKey == QLatin1String("error.pdf.encrypted")
                      ? core::CosReading::Encrypted
                      : core::CosReading::NoPageMap;
    } else {
        for (const ops::PageEntry& page : sink.value()->pages().pages) {
            core::CosPageFacts fact;
            fact.rotation = page.effectiveRotate();
            if (page.mediaBox) {
                // The DRAWN box, as `core::CosPageFacts` says: media intersected with crop, each own or
                // inherited (Stage 7 guide I-01). Edges are normalized first, as the engine's are.
                const auto rectOf = [](const std::array<double, 4>& box) {
                    const double x0 = std::min(box[0], box[2]);
                    const double y0 = std::min(box[1], box[3]);
                    return core::PdfRect{core::PdfPoint{x0, y0},
                                         core::PdfSize{std::max(box[0], box[2]) - x0, std::max(box[1], box[3]) - y0}};
                };
                core::PdfRect drawn = rectOf(*page.mediaBox);
                if (page.cropBox)
                    drawn = core::intersect(drawn, rectOf(*page.cropBox));
                fact.drawnSize = drawn.size;
            }
            facts.push_back(fact);
        }

        // S4 T10 (D4-12): `/Trans`, `/Dur` and `/PageMode`, off the walk that just ran. **This was
        // the first read in the product that does not go through PDFium** (S5 T8's attachment
        // descriptions, below, are the second), and it is here rather than in `pdf/render/` because
        // the engine's C API exposes page transitions nowhere at all. It re-reads dictionaries this walk already fetched, so it costs no second parse --
        // which is why it is worth doing for every document when almost none carries one.
        ops::TransitionReader::read(sink.value()->table(), sink.value()->pages(), entry->transitions);
        if (const int unsupported = entry->transitions.unsupportedCount(); unsupported > 0) {
            RAWPAGE_LOG_INFO(core::LogSubsystem::Render)
                << "backend:" << unsupported
                << "page transition(s) name a style this build does not draw; they will cut";
        }

        // S5 T8 (D5-12): the embedded files' descriptions, off the same walk -- one dictionary lookup for
        // a document that has none.
        for (ops::EmbeddedFileEntry& file : ops::EmbeddedFileReader::read(sink.value()->table()))
            entry->attachmentDescriptions.emplace_back(std::move(file.name), std::move(file.description));
        // S6 T11 (D6-17): the same tree as the model holds it -- names, types, sizes and dates, never the bytes.
        entry->attachments = core::Attachments::fromReading(ops::EmbeddedFileReader::attachments(sink.value()->table()));
    }
    if (!sink) {
        // S6 T11: a document the first-party reader declines has no attachments to list -- and says why.
        entry->attachments = core::Attachments::refused(core::Error{
            core::ErrorCode::PermissionDenied,
            reading == core::CosReading::Encrypted ? QLatin1String("error.edit.encrypted") : QLatin1String("error.edit.noPageMap"),
            QString()});
    }
    entry->permissions = core::Permissions::assess(entry->document->pages(), reading, facts);
    // S7 T2 (D7-21): a tree the walk noted anything about keeps its page order -- the sink refuses it too.
    if (sink && !sink.value()->pages().notes.empty())
        entry->permissions.setPageTreeDamaged(sink.value()->pages().notes.front().objectNumber);

    // S6 T2 (D6-3): the annotations, first-party, and their agreement with the engine page by page.
    // A page whose /Annots the two read differently is read-only for annotations, and says which.
    if (sink) {
        entry->annotations = core::AnnotationStore::fromReading(
            ops::AnnotationReader::read(sink.value()->table(), sink.value()->pages()));
        entry->permissions.assessAnnotations(entry->document->annotationFacts(),
                                             entry->annotations.listingFacts());
        logAnnotationReading(path, entry->annotations, entry->permissions);
    } else {
        entry->annotations = core::AnnotationStore::refused(core::Error{
            core::ErrorCode::PermissionDenied,
            reading == core::CosReading::Encrypted ? QLatin1String("error.edit.encrypted")
                                                    : QLatin1String("error.edit.noPageMap"),
            QString()});
    }
    // S6 T11: a document from bytes is never edited -- its permissions say so by key -- and keeps its sink all the
    // same, so a Save As writes it, unchanged, to a file of its own (`RevisionWriter`: a save of an unchanged file
    // is a copy).
    if (entry->fromBytes) entry->permissions = core::Permissions::readOnly("error.edit.attachmentCopy");
    if (sink && (entry->permissions.canEditPages() || entry->fromBytes))
        entry->sink = std::move(sink).value();

    const std::lock_guard<std::mutex> guard(mutex_);
    const core::DocumentHandle handle{nextHandle_++};
    entry->extractor = std::make_unique<render::PdfiumTextExtractor>(*entry->document, handle);
    RAWPAGE_LOG_INFO(core::LogSubsystem::Render)
        << "backend: opened" << path << "as handle" << static_cast<int>(handle.id) << "with"
        << entry->document->pageCount() << "pages;"
        << (entry->permissions.canEditPages()
                ? QStringLiteral("editable")
                : QStringLiteral("not editable (%1 %2)")
                      .arg(entry->permissions.refusalKey(), entry->permissions.refusalContext()));
    open_.emplace(handle.id, std::move(entry));
    return handle;
}

const core::AnnotationStore* DocumentBackendImpl::annotations(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry != nullptr ? &entry->annotations : nullptr;
}

const core::Attachments* DocumentBackendImpl::attachments(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry != nullptr ? &entry->attachments : nullptr;
}

// THE LOCK IS HELD ONLY TO FIND THE ENTRY, `writeRevision`'s arrangement: decoding a large attachment is the
// caller's thread's work, and the entry is destroyed only by `close()`, which the same thread calls.
core::Result<QByteArray> DocumentBackendImpl::attachmentData(core::DocumentHandle document,
                                                             const core::AttachmentRef& attachment)
{
    Open* entry = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        entry = find(document);
    }
    if (entry == nullptr) {
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.unknownDocument"),
                           QString::number(static_cast<qulonglong>(document.id))};
    }
    // With a sink, the document as it stands now -- an attachment not yet saved included. S7 T4: once a verify reading
    // the sink is over.
    awaitVerify(*entry);
    if (entry->sink) return entry->sink->attachmentData(attachment);
    // Without one, the bytes it was opened from, through a table loaded for the call: the first-party reader is
    // what reads an embedded file's stream, and a document it declines (encrypted) has none to give.
    auto opened = cos::openWithRepair(std::string_view(entry->bytes.constData(), static_cast<std::size_t>(entry->bytes.size())));
    if (!opened) return opened.error();
    return ops::AttachmentFiles::read(opened.value().table, attachment);
}

// Stage 6 T14: as `attachmentData` -- the lock held only to find the entry. A document with no sink holds no sound the
// model reads (its annotations are not read: D2-6), so it has none to give.
core::Result<QByteArray> DocumentBackendImpl::soundSamples(core::DocumentHandle document, const core::AnnotationId& annotation)
{
    Open* entry = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        entry = find(document);
    }
    if (entry == nullptr) {
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.unknownDocument"),
                           QString::number(static_cast<qulonglong>(document.id))};
    }
    if (!entry->sink) return core::Error{core::ErrorCode::NotFound, QLatin1String("error.sound.noData"), annotation.toString()};
    awaitVerify(*entry);   // S7 T4: as `attachmentData`
    return entry->sink->soundSamples(annotation);
}

core::Result<core::Fdf> DocumentBackendImpl::readAnnotationExchange(const QString& path)
{
    core::Result<core::Fdf> read = ops::FdfCodec::readFile(path);
    if (read) {
        RAWPAGE_LOG_INFO(core::LogSubsystem::Save)
            << "backend: read" << static_cast<int>(read.value().annotations.size()) << "annotation(s) from" << path << "("
            << static_cast<int>(read.value().notes.size()) << "note(s))";
    }
    return read;
}

core::Status DocumentBackendImpl::writeAnnotationExchange(core::DocumentHandle document, const core::Fdf& exchange,
                                                          core::ExchangeFormat format, const QString& path, bool overwrite)
{
    core::Fdf carried = exchange;
    const core::Status filled = ops::FdfCodec::carryFiles(carried, [this, document](const core::AnnotationId& id) {
        return attachmentData(document, core::AttachmentRef::ofAnnotation(id));
    });
    if (!filled) return filled;
    // Stage 6 T14: and each sound's samples.
    const core::Status heard = ops::FdfCodec::carrySounds(carried, [this, document](const core::AnnotationId& id) {
        return soundSamples(document, id);
    });
    if (!heard) return heard;
    if (const core::Status written = ops::FdfCodec::writeFile(carried, format, path, overwrite); !written) return written;
    RAWPAGE_LOG_INFO(core::LogSubsystem::Save)
        << "backend: wrote" << static_cast<int>(carried.annotations.size()) << "annotation(s) of handle"
        << static_cast<int>(document.id) << "to" << path;
    return core::Status{};
}

core::Status DocumentBackendImpl::annotationWritable(const core::Annotation& annotation) const
{
    return ops::MutationSinkImpl::canCreate(annotation);
}

// Stage 7 T5 (D7-4, D7-6): read once, here, through the first-party reader that writes the copy -- the engine never
// sees the source. Nothing is changed and no document is registered.
core::Result<core::CopiedPages> DocumentBackendImpl::copyPages(const QString& path, const std::vector<int>& pages)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.open.notFound"), path};
    const QByteArray bytes = file.readAll();
    file.close();
    auto copied = ops::ObjectGraphCopier::copyFile(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                                                   pages);
    if (!copied) return copied.error();
    RAWPAGE_LOG_INFO(core::LogSubsystem::Save)
        << "backend: copied" << copied.value().pageCount << "page(s) of" << path << "--" << copied.value().graph.size()
        << "bytes," << static_cast<int>(copied.value().report.references.size()) << "reported";
    return copied;
}

core::Result<core::CopiedPages> DocumentBackendImpl::blankPages(core::PdfSize size, int count)
{
    return ops::ObjectGraphCopier::blankPages(size, count);
}

core::Status DocumentBackendImpl::extractAttachment(core::DocumentHandle document, const core::AttachmentRef& attachment,
                                                    const QString& path, bool overwrite)
{
    core::Result<QByteArray> bytes = attachmentData(document, attachment);
    if (!bytes) return bytes.error();
    if (const core::Status written = ops::AttachmentFiles::write(bytes.value(), path, overwrite); !written)
        return written;
    RAWPAGE_LOG_INFO(core::LogSubsystem::Save)
        << "backend: wrote an attachment of handle" << static_cast<int>(document.id) << "to" << path << "("
        << bytes.value().size() << "bytes)";
    return core::Status{};
}

// THE CALLER'S PROTOCOL, because a mutex cannot express it: before closing, cancel the document's
// outstanding render jobs and let the scheduler go idle. `rasterizer()` hands out a pointer into
// the entry this call destroys, and a worker holding one across the destruction is a use-after-free
// inside PDFium. `ui/shell/DocumentWindow`'s destructor is the one caller and it does exactly that.
core::Status DocumentBackendImpl::close(core::DocumentHandle document)
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const auto found = open_.find(document.id);
    if (found == open_.end()) {
        // Closing a handle that is not open is a caller error worth naming, not a silent success:
        // it usually means two owners believe they own the same document, and the second close
        // would otherwise look like it worked.
        return core::Error{core::ErrorCode::NotFound, QLatin1String("error.open.notFound"),
                           QString::number(static_cast<qulonglong>(document.id))};
    }
    open_.erase(found);
    // The handle is NOT returned to a pool. `nextHandle_` only goes up, so a token held across
    // this call misses forever rather than starting to name a different document.
    return {};
}

core::IPageRasterizer* DocumentBackendImpl::rasterizer(core::DocumentHandle document)
{
    // CALLED FROM THE RENDER THREAD. See the mutex's note in the header.
    const std::lock_guard<std::mutex> guard(mutex_);
    Open* entry = find(document);
    return entry ? entry->rasterizer.get() : nullptr;
}

// THE LOCK IS HELD ONLY TO FIND THE ENTRY. A save of a large file takes seconds, and `rasterizer()`
// is asked from the render thread under the same mutex; holding it through the write would stall
// every tile behind the disk. The entry itself is only destroyed by `close()`, which the UI thread --
// the same thread that saves -- calls.
core::Status DocumentBackendImpl::writeRevision(core::DocumentHandle document, const QString& temporaryPath)
{
    Open* entry = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        entry = find(document);
    }
    if (entry == nullptr || !entry->sink)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), temporaryPath};
    awaitVerify(*entry);   // S7 T4: the journal settled before the save; this is the sink's own guard
    const bool complete = entry->savedReport ? entry->savedReport->requiresSaveAs()
                                             : entry->document->repairReport().requiresSaveAs();
    return ops::RevisionWriter::write(*entry->sink, complete, temporaryPath);
}

core::Status DocumentBackendImpl::adoptRevision(core::DocumentHandle document, const QString& path)
{
    Open* entry = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        entry = find(document);
    }
    if (entry == nullptr || !entry->sink)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), path};

    awaitVerify(*entry);   // S7 T4
    // THE ORDER JUST WRITTEN, read before the sink adopts it (S7 T3): the saved revision's page i is the open
    // tree's page `written[i]`, which is what the next page command's order is read through.
    std::vector<int> written = drawnFrom(entry->sink.get(), entry->diskFrom, entry->document->pages().count());
    std::vector<bool> writtenOriginals =
        drawnOriginals(entry->sink.get(), entry->diskOriginal, entry->document->pages().count());
    auto adopted = ops::RevisionWriter::adopt(*entry->sink, path);
    if (!adopted)
        return adopted.error();
    entry->diskFrom = std::move(written);
    entry->diskOriginal = std::move(writtenOriginals);
    // The sink now holds views into `adopted.bytes`; this assignment keeps that buffer and releases the
    // previous revision's (the engine keeps its own copy of the bytes it loaded).
    entry->bytes = std::move(adopted.value().bytes);
    entry->savedReport = adopted.value().report;
    entry->id = core::DocumentId::of(path, QFileInfo(path).lastModified().toMSecsSinceEpoch(), entry->bytes);
    RAWPAGE_LOG_INFO(core::LogSubsystem::Save)
        << "backend: handle" << static_cast<int>(document.id) << "adopted the revision saved at" << path
        << "(" << entry->bytes.size() << "bytes)";
    return core::Status{};
}

// THE LOCK IS HELD ONLY TO FIND THE ENTRIES, as `writeRevision` holds it: composing a large file is the UI thread's
// work, and `rasterizer()` is asked from the render thread under the same mutex. The entries are destroyed only by
// `close()`, which the same thread calls.
//
// STAGE 7 T4 (`Stage7-Assembly-Plan.md` Q4, `DW-4`'s second cause): THE VERIFY RUNS ON A THREAD OF ITS OWN. The
// compose is Save's (`RevisionWriter::composeUpdate`) and stays here, on the caller's thread -- a compose that fails
// is refused at once, as before -- and since T4 it costs what CHANGED (`MutationSinkImpl`'s header). The verify is
// Save's too, over the same whole file (`RevisionWriter::verifyUpdate`), but it read every page of the document on
// the UI thread after every edit -- 3.4 ms on `text-900.pdf` with nothing unsaved, 21 ms after 1,500 unsaved
// highlights (measured) -- so it now runs beside it, and every engine is handed the update BEHIND A GATE that only
// the verify opens (`render::ShowGate`): a job that needs the engine waits for it, and a verify that refuses leaves
// every engine drawing what it drew. The sink is not touched again until the thread is joined (`awaitVerify`): the
// journal settles before its next command (`CommandJournal::settle`), and every entry point here that reads the
// sink joins first.
void DocumentBackendImpl::awaitVerify(Open& entry)
{
    if (entry.verifying.joinable()) entry.verifying.join();
}

core::Status DocumentBackendImpl::showEdits(core::DocumentHandle document,
                                            const std::vector<core::DocumentHandle>& viewers)
{
    Open* entry = nullptr;
    std::vector<std::pair<core::DocumentHandle, Open*>> shown;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        entry = find(document);
        if (entry == nullptr) {
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.unknownDocument"),
                               QString::number(static_cast<qulonglong>(document.id))};
        }
        shown.emplace_back(document, entry);
        for (const core::DocumentHandle viewer : viewers) {
            Open* other = find(viewer);
            if (other == nullptr) {
                return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.unknownDocument"),
                                   QString::number(static_cast<qulonglong>(viewer.id))};
            }
            if (other != entry) shown.emplace_back(viewer, other);
        }
    }
    // The verify of the show before this one is over before anything reads the sink again; its verdict is kept for
    // `settleEdits`, which the journal asks before it runs anything else.
    awaitVerify(*entry);

    // NO SINK, NO UPDATE: each engine back on the bytes it opened (no work when it never left them), and so is
    // its tree (S7 T3).
    if (!entry->sink) {
        for (const auto& [handle, target] : shown) {
            const std::vector<int> identity = drawnFrom(nullptr, {}, target->document->pages().count());
            if (target->drawnFrom != identity || !target->drawnFacts.empty()) {
                target->drawn = target->document->pages();
                target->drawnFrom = identity;
                target->drawnOriginal.assign(identity.size(), true);
                target->drawnFacts.clear();
                const std::lock_guard<std::mutex> guard(mutex_);
                target->outline = target->document->outline();
                target->destinations = target->document->destinations();
            }
            target->document->show(target->document->bytes(), QByteArray());
        }
        return core::Status{};
    }

    // SAVE'S OWN COMPOSE, so what is drawn is what Save would write. What the engines are handed is the newest
    // revision on disk -- the buffer this entry already holds -- and the update alone.
    const auto began = std::chrono::steady_clock::now();
    const bool complete = entry->savedReport ? entry->savedReport->requiresSaveAs()
                                             : entry->document->repairReport().requiresSaveAs();
    auto composed = ops::RevisionWriter::composeUpdate(*entry->sink, complete);
    if (!composed) return notShown(composed.error());
    const QByteArray update = std::move(composed.value().update);
    entry->lastCompose = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began);
    entry->lastObjectsWritten = composed.value().objectsWritten.size();
    entry->lastObjectsReused = composed.value().objectsReused;

    // AND THE PAGES IN THE ORDER THE UPDATE DRAWS THEM (S7 T3, D7-3): each engine is told the order with the
    // bytes and swaps its own tree in with them; `pages()` answers it from now on, on this thread. A companion
    // opened the same file, so the order read through its own open tree is the same pages.
    // REASSIGNED ONLY WHEN THE ORDER CHANGED: an assignment moves the tree's storage, and a caller may hold a
    // page of it across an edit that moved no page -- every annotation edit of Stage 6 is one. S7 T4: what each
    // replaced is kept, to put back if the verify refuses.
    const std::vector<int> from = drawnFrom(entry->sink.get(), entry->diskFrom, entry->document->pages().count());
    const std::vector<bool> originals =
        drawnOriginals(entry->sink.get(), entry->diskOriginal, entry->document->pages().count());
    // S7 T5: and each page the engine never opened, as it was first drawn.
    std::vector<std::optional<core::Page>> facts = entry->sink->drawnFacts();
    if (std::none_of(facts.begin(), facts.end(), [](const std::optional<core::Page>& page) { return page.has_value(); }))
        facts.clear();
    std::vector<Open::Replaced> replaced;
    std::vector<core::DocumentHandle> gated;
    const auto gate = std::make_shared<render::ShowGate>();
    for (const auto& [handle, target] : shown) {
        if (target->drawnFrom != from || target->drawnOriginal != originals || target->drawnFacts != facts) {
            Open::Replaced before;
            before.target = handle;
            before.drawn = target->drawn;
            before.drawnFrom = target->drawnFrom;
            before.drawnOriginal = target->drawnOriginal;
            before.drawnFacts = target->drawnFacts;
            target->drawn = target->document->pages().reordered(from, facts);
            target->drawnFrom = from;
            target->drawnOriginal = originals;
            target->drawnFacts = facts;
            // THE OUTLINE AND THE NAMED DESTINATIONS follow the pages they name (S7 T3): the bookmarks pane, the
            // destinations pane and a bookmark found by a search open the page, not the index it had.
            core::Outline outline;
            core::Destinations destinations;
            carryReaders(*target->document, from, originals, outline, destinations);
            const std::lock_guard<std::mutex> guard(mutex_);
            before.outline = std::move(target->outline);
            before.destinations = std::move(target->destinations);
            target->outline = std::move(outline);
            target->destinations = std::move(destinations);
            replaced.push_back(std::move(before));
        }
        target->document->show(entry->bytes, update, from, gate, facts);
        gated.push_back(handle);
    }

    // THE VERIFY, on its own thread: the whole file put together there, against the seam as it stands NOW.
    entry->verdict = core::Status{};
    entry->unsettled = true;
    entry->gated = std::move(gated);
    entry->replaced = std::move(replaced);
    Open* self = entry;
    entry->verifying = std::thread([self, gate, update, fault = ops::RevisionWriter::composeFault()] {
        const auto verifying = std::chrono::steady_clock::now();
        const ops::MutationSinkImpl& sink = *self->sink;
        core::Status verified = ops::RevisionWriter::verifyUpdate(sink.bytes(), update, sink, fault);
        self->lastVerifyNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - verifying).count());
        self->verdict = std::move(verified);
        gate->resolve(static_cast<bool>(self->verdict));
        const std::lock_guard<std::mutex> guard(self->settledMutex);
        if (self->settled) self->settled();
    });
    return core::Status{};
}

core::Status DocumentBackendImpl::settleEdits(core::DocumentHandle document)
{
    Open* entry = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        entry = find(document);
    }
    if (entry == nullptr) return core::Status{};
    awaitVerify(*entry);
    if (!entry->unsettled) return core::Status{};
    entry->unsettled = false;
    std::vector<Open::Replaced> replaced = std::move(entry->replaced);
    std::vector<core::DocumentHandle> gated = std::move(entry->gated);
    entry->replaced.clear();
    entry->gated.clear();
    if (entry->verdict) return core::Status{};

    // REFUSED: every engine kept what it drew, so its tree, its order and its readers go back to what they were, and
    // its refusal ends -- the journal takes the operation back next, and asks again for what it shows.
    for (Open::Replaced& before : replaced) {
        Open* target = nullptr;
        {
            const std::lock_guard<std::mutex> guard(mutex_);
            target = find(before.target);
        }
        if (target == nullptr) continue;
        target->drawn = std::move(before.drawn);
        target->drawnFrom = std::move(before.drawnFrom);
        target->drawnOriginal = std::move(before.drawnOriginal);
        target->drawnFacts = std::move(before.drawnFacts);
        const std::lock_guard<std::mutex> guard(mutex_);
        target->outline = std::move(before.outline);
        target->destinations = std::move(before.destinations);
    }
    for (const core::DocumentHandle handle : gated) {
        Open* target = nullptr;
        {
            const std::lock_guard<std::mutex> guard(mutex_);
            target = find(handle);
        }
        if (target != nullptr) target->document->clearRefusal();
    }
    RAWPAGE_LOG_WARNING(core::LogSubsystem::Save)
        << "backend: handle" << static_cast<int>(document.id) << "-- the verify refused its last edit:"
        << entry->verdict.error().messageKey << entry->verdict.error().context;
    return notShown(entry->verdict.error());
}

void DocumentBackendImpl::setEditsSettled(core::DocumentHandle document, std::function<void()> settled)
{
    Open* entry = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        entry = find(document);
    }
    if (entry == nullptr) return;
    const std::lock_guard<std::mutex> guard(entry->settledMutex);
    entry->settled = std::move(settled);
}

std::optional<DocumentBackendImpl::ReloadRecord> DocumentBackendImpl::reloadRecord(core::DocumentHandle document) const
{
    const render::PdfiumDocument* found = nullptr;
    std::chrono::nanoseconds compose{0};
    std::chrono::nanoseconds verify{0};
    std::size_t written = 0;
    std::size_t reused = 0;
    const void* onDisk = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        const Open* entry = find(document);
        if (entry == nullptr) return std::nullopt;
        found = entry->document.get();
        compose = entry->lastCompose;
        verify = std::chrono::nanoseconds(entry->lastVerifyNs.load());
        written = entry->lastObjectsWritten;
        reused = entry->lastObjectsReused;
        onDisk = entry->bytes.constData();
    }
    const render::PdfiumDocument::ReloadStats stats = found->reloadStats();
    ReloadRecord record;
    record.reloads = stats.reloads;
    record.failures = stats.failures;
    record.coalesced = stats.coalesced;
    record.lastLoad = stats.lastLoad;
    record.lastThread = stats.lastThread;
    record.pendingGeneration = stats.pendingGeneration;
    record.shownGeneration = stats.shownGeneration;
    record.lastCompose = compose;
    record.lastVerify = verify;
    record.lastObjectsWritten = written;
    record.lastObjectsReused = reused;
    record.engineBase = stats.shownBase;
    record.newestOnDisk = onDisk;
    return record;
}

// CALLED FROM THE RENDER THREAD, as a text job (D5-3) -- `rasterizer()`'s arrangement exactly, and
// `close()`'s protocol covers it the same way: a document's text jobs are cancelled and drained with
// its tiles before the entry this pointer points into is destroyed.
core::ITextExtractor* DocumentBackendImpl::textExtractor(core::DocumentHandle document)
{
    const std::lock_guard<std::mutex> guard(mutex_);
    Open* entry = find(document);
    return entry ? entry->extractor.get() : nullptr;
}

core::MutationSink* DocumentBackendImpl::mutationSink(core::DocumentHandle document)
{
    // Called from the UI thread -- by `CommandJournal`, through a command's `apply()` -- but under
    // the same lock as `rasterizer()`, because `open()` and `close()` change the map it reads.
    const std::lock_guard<std::mutex> guard(mutex_);
    Open* entry = find(document);
    return entry ? entry->sink.get() : nullptr;
}

core::ISigner* DocumentBackendImpl::signer(core::DocumentHandle)
{
    return nullptr;   // Stage 11
}

core::IOcrEngine* DocumentBackendImpl::ocrEngine()
{
    return nullptr;   // Stage 16
}

core::IIndexer* DocumentBackendImpl::indexer()
{
    return nullptr;   // Stage 12
}

const core::PageTree* DocumentBackendImpl::pages(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry ? &entry->drawn : nullptr;   // S7 T3: what the engine draws, not what it opened
}

const core::RepairReport* DocumentBackendImpl::repairReport(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    if (entry == nullptr)
        return nullptr;
    return entry->savedReport ? &*entry->savedReport : &entry->document->repairReport();
}

const core::Permissions* DocumentBackendImpl::permissions(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry ? &entry->permissions : nullptr;
}

const core::DocumentId* DocumentBackendImpl::documentId(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry ? &entry->id : nullptr;
}

const core::Outline* DocumentBackendImpl::outline(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry ? &entry->outline : nullptr;   // S7 T3: as it names pages now
}

const core::Destinations* DocumentBackendImpl::destinations(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry ? &entry->destinations : nullptr;   // S7 T3: as they name pages now
}

const core::Metadata* DocumentBackendImpl::metadata(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry ? &entry->document->metadata() : nullptr;
}

const core::PageTransitions* DocumentBackendImpl::pageTransitions(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry != nullptr ? &entry->transitions : nullptr;
}

core::Result<core::PdfRect> DocumentBackendImpl::contentBox(core::DocumentHandle document,
                                                            int pageIndex)
{
    // THE MAP'S LOCK IS RELEASED BEFORE THE ENGINE IS ASKED. `mutex_` guards `open_` against the
    // render thread calling `rasterizer()` while the UI thread opens or closes (the reasoning is on
    // the member itself); holding it across a page load would serialize the UI thread behind
    // whatever PDFium is doing. The document object outlives the lock because only `close()`
    // destroys one, and `close()`'s own protocol requires the caller to have drained this
    // document's work first.
    render::PdfiumDocument* found = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        Open* entry = find(document);
        if (entry == nullptr) {
            return core::Error{core::ErrorCode::InvalidArgument,
                               QLatin1String("error.backend.unknownDocument")};
        }
        found = entry->document.get();
    }
    return found->contentBox(pageIndex);
}

// `contentBox()`'s arrangement: the map's lock only to find the entry, the engine asked outside it. The
// descriptions are read at open and never written again, so the reference handed on is stable while the
// entry lives -- which `close()`'s protocol guarantees across this call.
core::Status DocumentBackendImpl::searchUnits(core::DocumentHandle document, int part,
                                              std::vector<core::text::SearchUnit>& into)
{
    render::PdfiumDocument* found = nullptr;
    const std::vector<std::pair<QString, QString>>* descriptions = nullptr;
    core::Outline outline;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        Open* entry = find(document);
        if (entry == nullptr) {
            return core::Error{core::ErrorCode::InvalidArgument,
                               QLatin1String("error.backend.unknownDocument"),
                               QString::number(static_cast<qulonglong>(document.id))};
        }
        found = entry->document.get();
        descriptions = &entry->attachmentDescriptions;
        // THE OUTLINE AS IT NAMES PAGES NOW (S7 T3), copied under the lock it is written under: a bookmark found by
        // a search goes to the page, not to the index it had at open.
        if (part == core::text::kOutlinePart) outline = entry->outline;
    }
    return found->searchUnits(part, *descriptions, into, part == core::text::kOutlinePart ? &outline : nullptr);
}

core::Status DocumentBackendImpl::pageGeometry(core::DocumentHandle document, int pageIndex, core::PageGeometry& into)
{
    render::PdfiumDocument* found = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        Open* entry = find(document);
        if (entry == nullptr) {
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.unknownDocument"),
                               QString::number(static_cast<qulonglong>(document.id))};
        }
        found = entry->document.get();
    }
    return found->pageGeometry(pageIndex, into);
}

} // namespace rawpage::pdf
