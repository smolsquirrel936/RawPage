// A `core::IDocumentBackend` for the widget suite. Stage 2 T9.
//
// WHY A FAKE RATHER THAN THE REAL ONE, and it is not a convenience. `tests/ui` links
// `rawpage_core` and `rawpage_ui` and nothing else (D1-4); the real backend lives in
// `rawpage_pdfium`, which this target must not link for the same reason `tests/unit` must not.
// More importantly, **guide case A-47 is about the SHELL'S POLICY, not about PDFium**: the claim
// is that `openFiles()` asks the backend rather than the filename. A fake is what lets a case
// decide, precisely and without a real PDF, whether the shell consulted it at all.
//
// It records every path it was asked about, which is the half that makes the assertion possible:
// "the file was refused" and "the backend was never asked" look identical from outside, and only
// the second is the bug A-47 exists to catch.

#ifndef RAWPAGE_TESTS_UI_FAKEBACKEND_H
#define RAWPAGE_TESTS_UI_FAKEBACKEND_H

#include "core/doc/AnnotationStore.h"
#include "core/doc/Attachments.h"
#include "core/doc/Destinations.h"
#include "core/doc/Fdf.h"
#include "core/doc/Metadata.h"
#include "core/doc/PageTransitions.h"
#include "core/doc/XfdfCodec.h"
#include "core/doc/Outline.h"
#include "core/backend.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/doc/DocumentId.h"
#include "core/doc/Permissions.h"
#include "core/doc/RepairReport.h"
#include "core/text/SearchUnit.h"

#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rawpage::uitest {

// Stage 3 T10: a sink that accepts every rotation and records the last one per page. What it wrote is
// `tests/unit`'s and `tests/render`'s to judge; the widget suite needs a document it may edit.
class FakeSink final : public core::MutationSink {
public:
    core::Status setPageRotation(int pageIndex, int degrees) override
    {
        rotations[pageIndex] = ((degrees % 360) + 360) % 360;
        ++calls;
        return {};
    }
    // Stage 6 T3: accepted and counted, as a rotation is. What the real sink writes is `tests/unit`'s.
    core::Status insertAnnotations(const std::vector<core::AnnotationPlacement>& at,
                                   const std::vector<core::Annotation>&) override
    {
        annotationInserts += static_cast<int>(at.size());
        ++calls;
        return {};
    }
    core::Status removeAnnotations(const std::vector<core::AnnotationPlacement>& at) override
    {
        annotationRemovals += static_cast<int>(at.size());
        ++calls;
        return {};
    }
    // Stage 6 T5: an edit, accepted and counted -- the last one kept, for a case that asks what it was.
    core::Status replaceAnnotations(const std::vector<core::Annotation>&, const std::vector<core::Annotation>& after) override
    {
        annotationEdits += static_cast<int>(after.size());
        lastEdit = after;
        ++calls;
        return {};
    }
    // Stage 6 T11: the attachment intents, accepted and counted -- the keys kept, for a case that asks.
    core::Status insertAttachments(const std::vector<core::DocumentAttachment>& files) override
    {
        for (const core::DocumentAttachment& file : files) attachmentInserts.push_back(file.key);
        ++calls;
        return {};
    }
    core::Status removeAttachments(const std::vector<QString>& keys) override
    {
        for (const QString& key : keys) attachmentRemovals.push_back(key);
        ++calls;
        return {};
    }
    // Stage 7 T2: the page intents, accepted and counted. A duplicate's labels are empty, as a document with
    // no /PageLabels gives them.
    core::Result<core::PageCommandReport> reorderPages(const core::PageMap&) override
    {
        ++pageCommands;
        ++calls;
        return core::PageCommandReport{};
    }
    core::Result<core::PageCommandReport> removePages(const std::vector<int>&, const QString&) override
    {
        ++pageCommands;
        ++calls;
        return core::PageCommandReport{};
    }
    core::Status restorePages(const std::vector<int>&, const QString&) override
    {
        ++pageCommands;
        ++calls;
        return {};
    }
    core::Result<core::DuplicatedPages> duplicatePages(const std::vector<int>& pages, const QString&,
                                                       const std::vector<std::vector<core::AnnotationCopy>>&) override
    {
        ++pageCommands;
        ++calls;
        core::DuplicatedPages out;
        out.labels.resize(pages.size());
        return out;
    }
    int pageCommands = 0;
    std::vector<QString> attachmentInserts;
    std::vector<QString> attachmentRemovals;
    std::map<int, int> rotations;
    int annotationInserts = 0;
    int annotationRemovals = 0;
    int annotationEdits = 0;
    std::vector<core::Annotation> lastEdit;
    int calls = 0;
};

class FakeBackend final : public core::IDocumentBackend {
public:
    // STAGE 3 T10 -- EDITING. Off by default, which keeps every Stage 2 case's document read-only exactly
    // as before. On, a document is assessed as the first-party reader would assess `editableReading`
    // (`Read` edits; `Encrypted` refuses by key, as the shipped backend refuses the encrypted corpus),
    // takes its identity from the file on disk, and saves by writing the opened bytes plus a marker line.
    bool editable = false;
    core::CosReading editableReading = core::CosReading::Read;
    bool failWrites = false;     // a save's write step fails by key -- `IoFailure`, THE PLACE (Save-As is offered)
    bool failVerify = false;     // the composed bytes fail their own re-read -- `Corrupted`, OUR BYTES (refused)
    int writes = 0;
    int adoptions = 0;
    bool failShows = false;      // S6 T4: `showEdits` refuses
    std::vector<std::vector<core::DocumentHandle>> shows;   // S6 T4: every `showEdits`, document first

    // What to answer for a given path. The default opens everything, so a harness that does not
    // care about opening writes nothing; a case that does sets this.
    std::function<bool(const QString&)> accepts = [](const QString&) { return true; };

    // What a refusal says, for a case that needs a particular one -- a cancelled prompt must not
    // raise the could-not-open banner that a damaged file must (Stage 2 issue I-32).
    core::ErrorCode refusalCode = core::ErrorCode::ParseFailure;
    const char* refusalKey = "error.open.notPdf";

    // Every path `open()` was asked about, in order.
    QStringList asked;

    // Called at the top of `open()`, before anything is decided -- which is the moment the REAL
    // backend would invoke the password callback and put a modal prompt on screen. A case that
    // needs to know the state of the window at that moment sets it (Stage 2 issue I-30).
    std::function<void(const QString&)> onOpen;
    int closes = 0;

    // How many A4 pages an accepted document has. Zero gives a document with a real handle and no
    // pages -- a state the viewer must survive, and one the SHIPPED backend never hands over: it
    // refuses a document with no readable page (Stage 2 issue I-31). This comment used to say no
    // real PDF reaches it; `corpus/malformed/objstm-corrupt.pdf` did, for as long as nothing refused.
    int pagesPerDocument = 3;

    // S4 T12. Page index -> logical label, applied at `open()`. A document with roman front matter
    // has two page 1s, and this is how a widget case gets one without an engine.
    std::map<int, QString> pageLabels;

    // S4 I-11. Page index -> MediaBox, applied at `open()`; a page with no entry is A4 portrait. A
    // document of MIXED sizes is what `corpus/ordinary/mixed-page-sizes.pdf` is, and what a fit that
    // measures the wrong page gets wrong -- a uniform document gives every page the same zoom.
    std::map<int, core::PdfRect> pageBoxes;

    std::array<bool, core::kBackendInterfaceCount> implementedInterfaces() const override
    {
        // The shape the shipped backend reports after Stage 2, so a widget written against this
        // fake cannot come to depend on capabilities the real one does not have.
        return {true, true, false, false, false, false, false};
    }

    core::Result<core::DocumentHandle> open(const QString& path,
                                            const core::PasswordCallback&) override
    {
        const std::lock_guard<std::recursive_mutex> state(stateMutex);
        asked << path;
        if (onOpen) onOpen(path);
        if (!accepts(path)) {
            // The FILE NAME as context, as `PdfiumDocument` gives it -- not the path.
            return core::Error{refusalCode, QLatin1String(refusalKey), QFileInfo(path).fileName()};
        }
        const core::DocumentHandle handle{static_cast<std::uint64_t>(asked.size())};

        std::vector<core::Page> built;
        built.reserve(static_cast<std::size_t>(pagesPerDocument));
        for (int i = 0; i < pagesPerDocument; ++i) {
            const auto sized = pageBoxes.find(i);
            built.emplace_back(i, sized != pageBoxes.end()
                                      ? sized->second
                                      : core::PdfRect{core::PdfPoint{0.0, 0.0},
                                                      core::PdfSize{595.276, 841.89}});
            // S4 T12: `/PageLabels`, for the cases about the page box and the status bar. Empty is
            // the ordinary state and is what every case that does not set one gets -- the same
            // shape a document with no `/PageLabels` has, which is most of them.
            const auto labelled = pageLabels.find(i);
            if (labelled != pageLabels.end()) built.back().setLabel(labelled->second);
        }
        trees[handle.id] = std::make_unique<core::PageTree>(std::move(built));
        pathOf[handle.id] = path;
        if (editable) {
            auto entry = std::make_unique<EditState>();
            std::vector<core::CosPageFacts> facts;
            for (const core::Page& page : trees[handle.id]->pages())
                facts.push_back(core::CosPageFacts{static_cast<int>(page.rotation()), page.mediaBox().size});
            entry->permissions = core::Permissions::assess(*trees[handle.id], editableReading, facts);
            entry->sourcePath = path;
            entry->id = identityOf(path);
            edits[handle.id] = std::move(entry);
        }

        return handle;
    }

    core::Status close(core::DocumentHandle handle) override
    {
        const std::lock_guard<std::recursive_mutex> state(stateMutex);
        ++closes;
        trees.erase(handle.id);
        edits.erase(handle.id);
        return {};
    }

    // A REAL `PageTree`, because the viewer's whole job is arithmetic over one -- a null here would
    // make every layout case assert that nothing was laid out. A4 at 595.276 x 841.89 points, which
    // is what `corpus/ordinary/` is generated at.
    const core::PageTree* pages(core::DocumentHandle handle) const override
    {
        const std::lock_guard<std::recursive_mutex> state(stateMutex);
        const auto found = trees.find(handle.id);
        return found == trees.end() ? nullptr : found->second.get();
    }

    // A REAL, EMPTY report by default -- which is the state "this file needed no repair", and is
    // what makes "opening a clean file shows no bar" assertable. A case that wants the banner
    // fills `repairs` in, and one that wants a backend with no report at all clears it.
    const core::RepairReport* repairReport(core::DocumentHandle handle) const override
    {
        const std::lock_guard<std::recursive_mutex> state(stateMutex);
        return trees.count(handle.id) != 0 ? &repairs : nullptr;
    }

    core::RepairReport repairs;

    // Null for both unless `editable` (Stage 3 T10). A null permissions pointer hydrates to "not assessed",
    // which refuses editing -- the safe reading for a fake whose pages come from nowhere.
    const core::Permissions* permissions(core::DocumentHandle handle) const override
    {
        const std::lock_guard<std::recursive_mutex> state(stateMutex);
        const auto found = edits.find(handle.id);
        return found == edits.end() ? nullptr : &found->second->permissions;
    }
    const core::DocumentId* documentId(core::DocumentHandle handle) const override
    {
        const std::lock_guard<std::recursive_mutex> state(stateMutex);
        const auto found = edits.find(handle.id);
        return found == edits.end() ? nullptr : &found->second->id;
    }
    // S4 T4 (D4-3, D4-4). This double reads no file, so it carries EMPTY models rather than null:
    // null means *not a handle of mine*, and empty means *this document has none*, which is the
    // distinction `core/backend.h` asks callers to make. A case that needs an outline fills these.
    const core::Outline* outline(core::DocumentHandle) const override
    {
        ++outlineReads;
        return &outline_;
    }
    const core::Destinations* destinations(core::DocumentHandle) const override
    {
        ++destinationReads;
        return &destinations_;
    }
    // S5 T9: a file NAME may carry its own Info (`metadataByName`) -- the folder search's filter cases need
    // documents that differ; every other file answers `metadata_`, as before.
    const core::Metadata* metadata(core::DocumentHandle handle) const override
    {
        const std::lock_guard<std::recursive_mutex> state(stateMutex);
        const auto path = pathOf.find(handle.id);
        if (path != pathOf.end()) {
            const auto named = metadataByName.find(QFileInfo(path->second).fileName());
            if (named != metadataByName.end()) return &named->second;
        }
        return &metadata_;
    }
    std::map<QString, core::Metadata> metadataByName;
    std::map<std::uint64_t, QString> pathOf;
    // S5 T9: `open()` runs on the render worker for a folder search's staged open while the widget thread
    // reads `pages()` and the rest -- so the maps they share are read and written under this.
    mutable std::recursive_mutex stateMutex;

    // S4 T10 (D4-12). PUBLIC and settable, unlike the three above, because the presentation
    // cases drive a document that asks for a per-page transition and this fake is where that
    // request comes from -- there is no engine here to read a real `/Trans` from.
    const core::PageTransitions* pageTransitions(core::DocumentHandle) const override
    {
        return &transitions_;
    }
    core::PageTransitions& transitions() noexcept { return transitions_; }

    // S6 T2 (D6-3): an EMPTY store that was read -- this fake has no file to read annotations from,
    // and *read, and there are none* is what its documents are. Public and settable as the
    // transitions are, for the cases from T5 that need a document with comments.
    const core::AnnotationStore* annotations(core::DocumentHandle) const override
    {
        return &annotations_;
    }
    core::AnnotationStore annotations_ = core::AnnotationStore::fromReading({});

    // S6 T11 (D6-17): the document-level files every document of this fake has -- none, read -- and the bytes
    // `attachmentData` hands out, by key (a document file) or by annotation id string (`obj:12`, `nm:...`).
    // PUBLIC and settable, as the store is. `extractAttachment` WRITES the bytes (`tests/` may write), so a case
    // can read what an open or an extract put on disk; every write is recorded.
    const core::Attachments* attachments(core::DocumentHandle) const override { return &attachments_; }
    core::Attachments attachments_ = core::Attachments::fromReading({});
    std::map<QString, QByteArray> attachmentBytes;
    QStringList extracted;
    core::Result<QByteArray> attachmentData(core::DocumentHandle, const core::AttachmentRef& attachment) override
    {
        const QString name = attachment.kind == core::AttachmentRef::Kind::Document ? attachment.key
                                                                                   : attachment.annotation.toString();
        const auto found = attachmentBytes.find(name);
        if (found == attachmentBytes.end())
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.attachment.unknown"), name};
        return found->second;
    }
    // S6 T14 (D6-18): the samples `soundSamples` hands out, by annotation id string -- settable, as the bytes above.
    std::map<QString, QByteArray> soundBytes;
    core::Result<QByteArray> soundSamples(core::DocumentHandle, const core::AnnotationId& annotation) override
    {
        const auto found = soundBytes.find(annotation.toString());
        if (found == soundBytes.end())
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.sound.noData"), annotation.toString()};
        return found->second;
    }
    core::Status extractAttachment(core::DocumentHandle handle, const core::AttachmentRef& attachment, const QString& path,
                                   bool overwrite) override
    {
        core::Result<QByteArray> bytes = attachmentData(handle, attachment);
        if (!bytes) return bytes.error();
        if (QFileInfo::exists(path) && !overwrite)
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.attachment.exists"), path};
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes.value()) != bytes.value().size() || !file.commit())
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.attachment.writeFailed"), path};
        extracted << path;
        return {};
    }
    // S6 T13 (D6-20): an exchange READ is XFDF only (`core/doc/XfdfCodec` -- an FDF needs `pdf/`, which this target
    // does not link); a WRITE is recorded, the exchange kept as it was handed over, and XFDF written to the path (an
    // FDF asked for is written as XFDF too: what the real backend writes is `tests/unit`'s to judge). Writability is
    // `writable`'s, everything by default.
    QStringList exchangesRead;
    struct WrittenExchange {
        QString path;
        core::ExchangeFormat format;
        core::Fdf exchange;
    };
    std::vector<WrittenExchange> exchangesWritten;
    std::function<core::Status(const core::Annotation&)> writable;
    core::Result<core::Fdf> readAnnotationExchange(const QString& path) override
    {
        exchangesRead << path;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.exchange.unreadable"), path};
        return core::XfdfCodec::read(file.readAll());
    }
    core::Status writeAnnotationExchange(core::DocumentHandle, const core::Fdf& exchange, core::ExchangeFormat format,
                                         const QString& path, bool overwrite) override
    {
        if (QFileInfo::exists(path) && !overwrite)
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.exchange.exists"), path};
        QSaveFile file(path);
        const QByteArray bytes = core::XfdfCodec::write(exchange);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.exchange.writeFailed"), path};
        exchangesWritten.push_back(WrittenExchange{path, format, exchange});
        return {};
    }
    core::Status annotationWritable(const core::Annotation& annotation) const override
    {
        return writable ? writable(annotation) : core::Status{};
    }

    // `openBytes` opens as `open()` does, under the name, read-only: the bytes are recorded, not read.
    QList<QByteArray> openedBytes;
    core::Result<core::DocumentHandle> openBytes(const QByteArray& bytes, const QString& name,
                                                 const core::PasswordCallback& password) override
    {
        openedBytes << bytes;
        const bool wasEditable = editable;
        editable = false;
        core::Result<core::DocumentHandle> handle = open(name, password);
        editable = wasEditable;
        return handle;
    }

    // S4 T12 (D4-14). PUBLIC and settable for the same reason as the transitions above: there is no
    // engine here to union any objects, and the fit cases need a document whose content box is a
    // KNOWN rectangle strictly inside its CropBox so that Fit Visible and Fit Page can be told
    // apart by their zooms (guide case A-68's widget half).
    //
    // A page with no entry answers `NotFound`, which is the empty-page answer the real backend
    // gives and the one `core/render/FitPolicy` falls back to the CropBox for -- so the fallback is
    // reachable from here as well, which is what makes its control a case rather than a claim.
    core::Result<core::PdfRect> contentBox(core::DocumentHandle handle, int pageIndex) override
    {
        const std::lock_guard<std::recursive_mutex> state(stateMutex);
        ++contentBoxReads;
        if (trees.find(handle.id) == trees.end()) {
            return core::Error{core::ErrorCode::InvalidArgument,
                               QLatin1String("error.backend.badHandle")};
        }
        const auto found = contentBoxes_.find(pageIndex);
        if (found == contentBoxes_.end()) {
            return core::Error{core::ErrorCode::NotFound,
                               QLatin1String("error.backend.noPageContent")};
        }
        return found->second;
    }

    // S5 T9. THE STAGED OPEN, in one ENGINE piece: `beginOpen` remembers, the one `continueOpen` is
    // `open()` -- so a folder search over this fake runs an engine task per file, and `openThreads`
    // says which thread it ran on (the render worker, never the UI thread, guide A-88).
    struct PendingOpen {
        QString path;
        core::PasswordCallback password;
    };
    std::map<core::OpeningId, PendingOpen> openings;
    core::OpeningId nextOpening = 1;
    std::mutex openMutex;
    std::vector<std::thread::id> openThreads;
    int abandoned = 0;

    core::Result<core::OpenProgress> beginOpen(const QString& path, const core::PasswordCallback& password) override
    {
        const std::lock_guard<std::mutex> lock(openMutex);
        const core::OpeningId id = nextOpening++;
        openings[id] = PendingOpen{path, password};
        core::OpenProgress progress;
        progress.opening = id;
        progress.nextNeedsEngine = true;
        return progress;
    }
    core::Result<core::OpenProgress> continueOpen(core::OpeningId id) override
    {
        PendingOpen pending;
        {
            const std::lock_guard<std::mutex> lock(openMutex);
            const auto found = openings.find(id);
            if (found == openings.end())
                return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.badHandle")};
            pending = found->second;
            openings.erase(found);
            openThreads.push_back(std::this_thread::get_id());
        }
        // ON THE RENDER WORKER: `open()` takes `stateMutex`, as every accessor the widget thread calls does.
        const core::Result<core::DocumentHandle> opened = open(pending.path, pending.password);
        if (!opened) return opened.error();
        core::OpenProgress progress;
        progress.opening = id;
        progress.done = true;
        progress.handle = opened.value();
        return progress;
    }
    void abandonOpen(core::OpeningId id) override
    {
        const std::lock_guard<std::mutex> lock(openMutex);
        if (openings.erase(id) != 0) ++abandoned;
    }

    // S5 T8 (D5-12). The search units every document of this fake answers, by part -- a page index,
    // `kOutlinePart` or `kAttachmentsPart`; a part with no entry is an empty success, as the shipped reader
    // answers a page with no annotation. CALLED ON THE ENGINE THREAD, so the count is atomic and the units
    // are set before a case searches.
    std::map<int, std::vector<core::text::SearchUnit>> unitParts;
    std::atomic<int> searchUnitCalls{0};

    // The handle is not looked up: `trees` is the UI thread's, and this runs on the engine's.
    core::Status searchUnits(core::DocumentHandle, int part, std::vector<core::text::SearchUnit>& into) override
    {
        ++searchUnitCalls;
        const auto found = unitParts.find(part);
        into = found == unitParts.end() ? std::vector<core::text::SearchUnit>{} : found->second;
        return {};
    }

    // S6 T12: a page's paths, as a case sets them -- the segments of `pagePaths[page]`, or none. Counted, so a case
    // can say the second request asked nothing (guide A-91).
    std::map<int, std::vector<core::PathSegment>> pagePaths;
    std::atomic<int> geometryCalls{0};
    core::Status pageGeometry(core::DocumentHandle, int pageIndex, core::PageGeometry& into) override
    {
        ++geometryCalls;
        const auto found = pagePaths.find(pageIndex);
        into = core::PageGeometry(pageIndex, found == pagePaths.end() ? std::vector<core::PathSegment>{} : found->second);
        return {};
    }

    void setContentBox(int pageIndex, core::PdfRect box) { contentBoxes_[pageIndex] = box; }
    void clearContentBoxes() { contentBoxes_.clear(); }

    // How many times a caller asked. Guide case A-67's *asking twice calls the engine once* is the
    // BACKEND's claim and is asserted against the real one; this counter is what lets a widget case
    // say how often the widget asked, which is a different question and worth its own number.
    mutable int contentBoxReads = 0;

    // Keyed by PAGE INDEX and not by handle: every case that sets one drives a single document, and
    // a second key would be two things to get right in a fixture whose whole job is to be obvious.
    std::map<int, core::PdfRect> contentBoxes_;

    // S4 T6: how many times a caller ASKED for each model. Guide case A-30 claims the readers are
    // hydrated once at open and that showing a pane re-reads nothing; the `pdf/render/` half of
    // that is met by construction (the `FPDF_DOCUMENT` never leaves that directory), and this
    // counter is what lets the WIDGET half be asserted rather than argued -- a pane that rebuilt
    // its tree on every show would be visible here as a growing number. `mutable` because the
    // accessors are const by interface, which is itself the point: fetching a model is a read of
    // something already in memory.
    mutable int outlineReads = 0;
    mutable int destinationReads = 0;

    core::Outline outline_;
    core::Destinations destinations_;
    core::Metadata metadata_;
    core::PageTransitions transitions_;


    // Stage 3 T10: the opened file's bytes plus a line naming the save, so a saved file differs from the one
    // before it -- enough for `SaveCoordinator`'s identity check and for the chrome's dirty marker.
    core::Status writeRevision(core::DocumentHandle handle, const QString& temporaryPath) override
    {
        const auto found = edits.find(handle.id);
        if (found == edits.end() || !found->second->permissions.canEditPages())
            return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), QString()};
        if (failWrites)
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.save.writeFailed"), temporaryPath};
        // What `pdf/ops/RevisionWriter` returns when the file it wrote does not read back as the document
        // it composed: no other path would take those bytes either, so this one is never a Save-As offer.
        if (failVerify)
            return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.save.verifyFailed"),
                               QStringLiteral("pages")};
        QFile source(found->second->sourcePath);
        QByteArray bytes = source.open(QIODevice::ReadOnly) ? source.readAll() : QByteArray();
        bytes += "\n% saved revision " + QByteArray::number(++writes) + "\n";
        QFile out(temporaryPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size())
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.save.writeFailed"), temporaryPath};
        return {};
    }
    core::Status adoptRevision(core::DocumentHandle handle, const QString& path) override
    {
        const auto found = edits.find(handle.id);
        if (found == edits.end())
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.open.notFound"), path};
        found->second->sourcePath = path;
        found->second->id = identityOf(path);
        repairs = core::RepairReport{};          // a saved file needs no repair
        ++adoptions;
        return {};
    }

    // S6 T4 (D6-2): no engine to reload -- every call is recorded, as `[document, viewers...]`, so a case
    // can see who asked to be shown what; `failShows` refuses as a compose that fails verify would.
    core::Status showEdits(core::DocumentHandle document, const std::vector<core::DocumentHandle>& viewers) override
    {
        if (failShows)
            return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.edit.notShown"),
                               QStringLiteral("error.save.verifyFailed")};
        std::vector<core::DocumentHandle> call{document};
        call.insert(call.end(), viewers.begin(), viewers.end());
        shows.push_back(std::move(call));
        return {};
    }

    core::IPageRasterizer* rasterizer(core::DocumentHandle) override { return nullptr; }
    core::ITextExtractor* textExtractor(core::DocumentHandle) override { return nullptr; }
    core::MutationSink* mutationSink(core::DocumentHandle handle) override
    {
        const auto found = edits.find(handle.id);
        return found != edits.end() && found->second->permissions.canEditPages() ? &found->second->sink : nullptr;
    }
    FakeSink* sinkFor(core::DocumentHandle handle)
    {
        const auto found = edits.find(handle.id);
        return found == edits.end() ? nullptr : &found->second->sink;
    }
    core::ISigner* signer(core::DocumentHandle) override { return nullptr; }
    core::IOcrEngine* ocrEngine() override { return nullptr; }
    core::IIndexer* indexer() override { return nullptr; }

private:
    struct EditState {
        core::Permissions permissions;
        core::DocumentId id;
        FakeSink sink;
        QString sourcePath;
    };
    static core::DocumentId identityOf(const QString& path)
    {
        QFile file(path);
        const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("fake");
        return core::DocumentId::of(path, QFileInfo(path).lastModified().toMSecsSinceEpoch(), bytes);
    }
    std::map<std::uint64_t, std::unique_ptr<EditState>> edits;
    // `mutable` because `pages()` is const and `open()` is not: the map is the fake's storage, and
    // the constness belongs to the interface rather than to this object's bookkeeping.
    mutable std::map<std::uint64_t, std::unique_ptr<core::PageTree>> trees;
};

} // namespace rawpage::uitest

#endif // RAWPAGE_TESTS_UI_FAKEBACKEND_H
