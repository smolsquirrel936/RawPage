// tests/unit/CosBackend.h -- an `IDocumentBackend` built on the FIRST-PARTY reader only, for the headless
// suite. Stage 3 T4 (inside tst_document), shared from T5.
//
// WHY IT EXISTS: `rawpage_tests` may not link the engine (D2-2), and the journal, the commands and the
// reversibility property all need a hydrated `Document` and a REAL `pdf/ops/MutationSinkImpl` -- a
// recording fake sink would make "apply then revert leaves an empty delta" a property of the fake.
//
// WHAT IT IS NOT, and every suite using it says so at its assertion (convention 19): the ENGINE's page
// tree. Its tree is built from `pdf/ops/PageOps`' walk over the same bytes, so it agrees with the sink
// by construction; whether PDFium agrees is `tests/render/tst_pageopsengine.cpp` and
// `tst_backendedit.cpp`.
//
// `plantReading()` makes the next `open()` assess its permissions as if the first-party reader had
// found the document encrypted or unmappable -- how a headless case obtains a document the pre-flight
// refuses (guide case A-40) without an encrypted file the first-party reader cannot open at all.

#ifndef RAWPAGE_TESTS_UNIT_COSBACKEND_H
#define RAWPAGE_TESTS_UNIT_COSBACKEND_H

#include "core/doc/AnnotationStore.h"
#include "core/doc/Attachments.h"
#include "core/doc/Fdf.h"
#include "pdf/ops/AttachmentFiles.h"
#include "pdf/ops/FdfCodec.h"
#include "pdf/ops/EmbeddedFileReader.h"
#include "pdf/ops/AnnotationReader.h"
#include "core/doc/Destinations.h"
#include "core/doc/Metadata.h"
#include "core/doc/PageTransitions.h"
#include "core/doc/Outline.h"
#include "core/backend.h"
#include "core/doc/DocumentId.h"
#include "core/doc/PageTree.h"
#include "core/doc/Permissions.h"
#include "core/doc/RepairReport.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "pdf/ops/RevisionWriter.h"
#include "pdf/cos/XrefRepair.h"

#include <QFile>
#include <QFileInfo>

#include <map>
#include <memory>
#include <optional>

namespace rawpage::tests {

class CosBackend final : public core::IDocumentBackend {
public:
    void plantReading(core::CosReading reading) { planted_ = reading; }
    // Stage 6 T3: the next `open()` assesses its annotations as if the ENGINE had read one entry fewer on
    // page `page` -- how a headless case obtains a page D6-3 makes read-only, with no engine linked.
    void plantAnnotationDisagreement(int page) { plantedDisagreement_ = page; }

    std::array<bool, core::kBackendInterfaceCount> implementedInterfaces() const override
    {
        std::array<bool, core::kBackendInterfaceCount> implemented{};
        implemented[static_cast<std::size_t>(core::BackendInterface::DocumentBackend)] = true;
        implemented[static_cast<std::size_t>(core::BackendInterface::Mutation)] = true;
        return implemented;
    }

    core::Result<core::DocumentHandle> open(const QString& path, const core::PasswordCallback& password) override
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.open.notFound"), path};
        return openFrom(file.readAll(), path, password, false);
    }

    // S6 T11 (D6-17): `open()` over bytes, as the shipped backend does it -- no path, read-only, its sink kept so a
    // Save As writes it unchanged.
    core::Result<core::DocumentHandle> openBytes(const QByteArray& bytes, const QString& name,
                                                 const core::PasswordCallback& password) override
    {
        return openFrom(bytes, name, password, true);
    }

    core::Result<core::DocumentHandle> openFrom(const QByteArray& bytes, const QString& path, const core::PasswordCallback&,
                                                bool fromBytes)
    {
        auto entry = std::make_unique<Entry>();
        entry->bytes = bytes;
        auto sink = pdf::ops::MutationSinkImpl::open(
            std::string_view(entry->bytes.constData(), static_cast<std::size_t>(entry->bytes.size())));
        if (!sink)
            return sink.error();

        std::vector<core::Page> pages;
        std::vector<core::CosPageFacts> facts;
        int index = 0;
        for (const auto& walked : sink.value()->pages().pages) {
            core::PdfRect box{core::PdfPoint{0.0, 0.0}, core::PdfSize{612.0, 792.0}};
            if (walked.mediaBox) {
                const auto& b = *walked.mediaBox;
                box = core::PdfRect{core::PdfPoint{b[0], b[1]}, core::PdfSize{b[2] - b[0], b[3] - b[1]}};
            }
            core::Page page(index++, box);
            page.setRotation(walked.effectiveRotate());
            pages.push_back(page);
            facts.push_back(core::CosPageFacts{walked.effectiveRotate(), box.size});
        }
        entry->tree = core::PageTree(std::move(pages));
        // The first-party reader's repair report, as the shipped backend's PdfiumDocument produces it
        // (Stage 3 T8): a repaired file's first save must be a Save-As here too.
        if (auto opened = pdf::cos::openWithRepair(std::string_view(entry->bytes.constData(), static_cast<std::size_t>(entry->bytes.size()))))
            entry->repair = opened.value().report;
        entry->permissions = core::Permissions::assess(
            entry->tree, planted_.value_or(core::CosReading::Read), facts);
        planted_.reset();
        // S7 T2: as the shipped backend -- a page tree the walk noted anything about keeps its order.
        if (!sink.value()->pages().notes.empty())
            entry->permissions.setPageTreeDamaged(sink.value()->pages().notes.front().objectNumber);
        entry->id = fromBytes ? core::DocumentId::of(QString(), 0, entry->bytes)
                              : core::DocumentId::of(path, QFileInfo(path).lastModified().toMSecsSinceEpoch(), entry->bytes);   // the real time: SaveCoordinator compares it (T8)
        // S6 T11: the document-level files, off the same walk as the shipped backend's.
        entry->attachments = core::Attachments::fromReading(pdf::ops::EmbeddedFileReader::attachments(sink.value()->table()));
        // S6 T2 (D6-3): the annotations, from the same first-party walk as the shipped backend's.
        // There is no engine here, so no per-page agreement is assessed -- that half is
        // `tests/render/tst_annotreader.cpp`'s, where both readers are.
        entry->annotations = core::AnnotationStore::fromReading(
            pdf::ops::AnnotationReader::read(sink.value()->table(), sink.value()->pages()));
        if (plantedDisagreement_) {
            std::vector<std::vector<core::AnnotationFact>> engine = entry->annotations.listingFacts();
            const auto page = static_cast<std::size_t>(*plantedDisagreement_);
            if (page < engine.size() && !engine[page].empty()) engine[page].pop_back();
            else if (page < engine.size()) engine[page].push_back(core::AnnotationFact{});
            entry->permissions.assessAnnotations(engine, entry->annotations.listingFacts());
            plantedDisagreement_.reset();
        }
        if (fromBytes) entry->permissions = core::Permissions::readOnly("error.edit.attachmentCopy");
        if (entry->permissions.canEditPages() || fromBytes)
            entry->sink = std::move(sink).value();

        const core::DocumentHandle handle{next_++};
        entries_.emplace(handle.id, std::move(entry));
        return handle;
    }

    core::Status close(core::DocumentHandle handle) override
    {
        entries_.erase(handle.id);
        return {};
    }

    // S6 T11 (D6-17): the shipped backend's arrangement -- the sink's reading of the document as it stands, else a
    // table loaded for the call -- and the extract through `pdf/ops/AttachmentFiles`, the same write.
    const core::Attachments* attachments(core::DocumentHandle handle) const override
    {
        const auto* entry = find(handle);
        return entry ? &entry->attachments : nullptr;
    }
    core::Result<QByteArray> attachmentData(core::DocumentHandle handle, const core::AttachmentRef& attachment) override
    {
        auto* entry = findMutable(handle);
        if (entry == nullptr)
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.unknownDocument"), QString()};
        if (entry->sink) return entry->sink->attachmentData(attachment);
        auto opened = pdf::cos::openWithRepair(std::string_view(entry->bytes.constData(), static_cast<std::size_t>(entry->bytes.size())));
        if (!opened) return opened.error();
        return pdf::ops::AttachmentFiles::read(opened.value().table, attachment);
    }
    core::Status extractAttachment(core::DocumentHandle handle, const core::AttachmentRef& attachment, const QString& path,
                                   bool overwrite) override
    {
        core::Result<QByteArray> bytes = attachmentData(handle, attachment);
        if (!bytes) return bytes.error();
        return pdf::ops::AttachmentFiles::write(bytes.value(), path, overwrite);
    }
    // S6 T14 (D6-18): the shipped backend's arrangement -- the sink's samples, the document as it stands now.
    core::Result<QByteArray> soundSamples(core::DocumentHandle handle, const core::AnnotationId& annotation) override
    {
        auto* entry = findMutable(handle);
        if (entry == nullptr)
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.unknownDocument"), QString()};
        if (!entry->sink) return core::Error{core::ErrorCode::NotFound, QLatin1String("error.sound.noData"), annotation.toString()};
        return entry->sink->soundSamples(annotation);
    }
    // S6 T13 (D6-20): the shipped backend's arrangement -- `pdf/ops/FdfCodec` both ways, the bytes of an export's
    // FileAttachments from `attachmentData`, and the sink's own question for writability.
    core::Result<core::Fdf> readAnnotationExchange(const QString& path) override
    {
        return pdf::ops::FdfCodec::readFile(path);
    }
    core::Status writeAnnotationExchange(core::DocumentHandle handle, const core::Fdf& exchange, core::ExchangeFormat format,
                                         const QString& path, bool overwrite) override
    {
        core::Fdf carried = exchange;
        const core::Status filled = pdf::ops::FdfCodec::carryFiles(carried, [this, handle](const core::AnnotationId& id) {
            return attachmentData(handle, core::AttachmentRef::ofAnnotation(id));
        });
        if (!filled) return filled;
        const core::Status heard = pdf::ops::FdfCodec::carrySounds(carried, [this, handle](const core::AnnotationId& id) {
            return soundSamples(handle, id);
        });
        if (!heard) return heard;
        return pdf::ops::FdfCodec::writeFile(carried, format, path, overwrite);
    }
    core::Status annotationWritable(const core::Annotation& annotation) const override
    {
        return pdf::ops::MutationSinkImpl::canCreate(annotation);
    }
    const core::PageTree* pages(core::DocumentHandle handle) const override
    {
        const auto* entry = find(handle);
        return entry ? &entry->tree : nullptr;
    }
    const core::RepairReport* repairReport(core::DocumentHandle handle) const override
    {
        const auto* entry = find(handle);
        return entry ? &entry->repair : nullptr;
    }
    const core::Permissions* permissions(core::DocumentHandle handle) const override
    {
        const auto* entry = find(handle);
        return entry ? &entry->permissions : nullptr;
    }
    const core::DocumentId* documentId(core::DocumentHandle handle) const override
    {
        const auto* entry = find(handle);
        return entry ? &entry->id : nullptr;
    }
    // S4 T4 (D4-3, D4-4). This double reads no file, so it carries EMPTY models rather than null:
    // null means *not a handle of mine*, and empty means *this document has none*, which is the
    // distinction `core/backend.h` asks callers to make. A case that needs an outline fills these.
    const core::Outline* outline(core::DocumentHandle) const override { return &outline_; }
    const core::Destinations* destinations(core::DocumentHandle) const override
    {
        return &destinations_;
    }
    const core::Metadata* metadata(core::DocumentHandle) const override { return &metadata_; }

    // S4 T10 (D4-12): empty, which is the ordinary answer -- almost no document carries one.
    const core::PageTransitions* pageTransitions(core::DocumentHandle) const override
    {
        return &transitions_;
    }

    const core::AnnotationStore* annotations(core::DocumentHandle handle) const override
    {
        const auto* entry = find(handle);
        return entry ? &entry->annotations : nullptr;
    }

    // S4 T12 (D4-14): this fake has no engine and therefore no objects to bound. An explicit
    // not-implemented, never an empty rectangle -- `core/backend.h`'s rule, and the difference
    // between *this build does not do that* and *the page is blank*, which `FitPolicy` treats
    // differently only because the two are distinguishable here.
    core::Result<core::PdfRect> contentBox(core::DocumentHandle, int) override
    {
        return core::notImplementedError(core::BackendInterface::DocumentBackend, "contentBox");
    }
    // S5 T9: no staged open -- this fake opens in one call, and nothing that drives pieces uses it.
    core::Result<core::OpenProgress> beginOpen(const QString&, const core::PasswordCallback&) override
    {
        return core::notImplementedError(core::BackendInterface::DocumentBackend, "beginOpen");
    }
    core::Result<core::OpenProgress> continueOpen(core::OpeningId) override
    {
        return core::notImplementedError(core::BackendInterface::DocumentBackend, "continueOpen");
    }
    void abandonOpen(core::OpeningId) override {}
    // S5 T8 (D5-12): no engine, so no annotations or attachments to read -- said, never an empty list.
    core::Status searchUnits(core::DocumentHandle, int, std::vector<core::text::SearchUnit>&) override
    {
        return core::notImplementedError(core::BackendInterface::DocumentBackend, "searchUnits");
    }
    core::Status pageGeometry(core::DocumentHandle, int, core::PageGeometry&) override
    {
        return core::notImplementedError(core::BackendInterface::DocumentBackend, "pageGeometry");
    }

    core::Outline outline_;
    core::Destinations destinations_;
    core::Metadata metadata_;
    core::PageTransitions transitions_;


    // Stage 3 T8: the same `pdf/ops/RevisionWriter` the shipped backend calls, so a round trip through
    // this backend exercises the real write, re-read and adopt -- everything but the engine.
    core::Status writeRevision(core::DocumentHandle handle, const QString& temporaryPath) override
    {
        if (failWrite_)
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.save.writeFailed"), temporaryPath};
        auto* entry = findMutable(handle);
        if (entry == nullptr || !entry->sink)
            return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), QString()};
        return pdf::ops::RevisionWriter::write(*entry->sink, entry->repair.requiresSaveAs(), temporaryPath);
    }

    core::Status adoptRevision(core::DocumentHandle handle, const QString& path) override
    {
        if (failAdopt_)
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.save.adoptFailed"), path};
        auto* entry = findMutable(handle);
        if (entry == nullptr || !entry->sink)
            return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), QString()};
        auto adopted = pdf::ops::RevisionWriter::adopt(*entry->sink, path);
        if (!adopted)
            return adopted.error();
        entry->bytes = std::move(adopted.value().bytes);
        entry->repair = adopted.value().report;
        entry->id = core::DocumentId::of(path, QFileInfo(path).lastModified().toMSecsSinceEpoch(), entry->bytes);
        ++adoptions_;
        return core::Status{};
    }

    // Stage 6 T4 (D6-2): the shipped backend's compose and verify -- `pdf/ops/RevisionWriter`, the same
    // calls -- with no engine to hand the result to. What WOULD be drawn is kept: `shownUpdate()` is the
    // update after the newest revision on disk, so a headless case can compare it with what Save writes.
    core::Status showEdits(core::DocumentHandle handle, const std::vector<core::DocumentHandle>&) override
    {
        auto* entry = findMutable(handle);
        if (entry == nullptr)
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.backend.unknownDocument"), QString()};
        ++shows_;
        if (!entry->sink) {
            entry->shown = QByteArray();
            return {};
        }
        const auto notShown = [](const core::Error& error) {
            return core::Error{error.code, QLatin1String("error.edit.notShown"), QString(error.messageKey)};
        };
        auto composed = pdf::ops::RevisionWriter::compose(*entry->sink, entry->repair.requiresSaveAs());
        if (!composed) return notShown(composed.error());
        const std::string_view original = entry->sink->bytes();
        const std::string_view written(composed.value().bytes.constData(),
                                       static_cast<std::size_t>(composed.value().bytes.size()));
        if (const core::Status verified = pdf::ops::RevisionWriter::verify(original, written, *entry->sink); !verified)
            return notShown(verified.error());
        entry->shown = composed.value().bytes.mid(static_cast<qsizetype>(original.size()));
        return {};
    }
    QByteArray shownUpdate(core::DocumentHandle handle) const
    {
        const auto* entry = find(handle);
        return entry ? entry->shown : QByteArray();
    }
    int shows() const { return shows_; }

    // Test seams for `tst_savecoordinator`: make the next write or adopt fail, as a backend can.
    void failNextWrites(bool fail) { failWrite_ = fail; }
    void failNextAdopts(bool fail) { failAdopt_ = fail; }
    int adoptions() const { return adoptions_; }

    core::MutationSink* mutationSink(core::DocumentHandle handle) override
    {
        return sinkFor(handle);
    }
    // The concrete sink, for a case that inspects its delta.
    pdf::ops::MutationSinkImpl* sinkFor(core::DocumentHandle handle)
    {
        const auto found = entries_.find(handle.id);
        return found == entries_.end() ? nullptr : found->second->sink.get();
    }

    core::IPageRasterizer* rasterizer(core::DocumentHandle) override { return nullptr; }
    core::ITextExtractor* textExtractor(core::DocumentHandle) override { return nullptr; }
    core::ISigner* signer(core::DocumentHandle) override { return nullptr; }
    core::IOcrEngine* ocrEngine() override { return nullptr; }
    core::IIndexer* indexer() override { return nullptr; }

private:
    struct Entry {
        core::AnnotationStore annotations;     // S6 T2
        core::Attachments attachments;         // S6 T11
        QByteArray bytes;
        core::PageTree tree;
        core::RepairReport repair;
        core::Permissions permissions;
        core::DocumentId id;
        std::unique_ptr<pdf::ops::MutationSinkImpl> sink;
        QByteArray shown;                      // S6 T4: the update the last `showEdits` composed
    };
    Entry* findMutable(core::DocumentHandle handle)
    {
        const auto found = entries_.find(handle.id);
        return found == entries_.end() ? nullptr : found->second.get();
    }
    const Entry* find(core::DocumentHandle handle) const
    {
        const auto found = entries_.find(handle.id);
        return found == entries_.end() ? nullptr : found->second.get();
    }
    std::map<std::uint64_t, std::unique_ptr<Entry>> entries_;
    std::uint64_t next_ = 1;
    std::optional<core::CosReading> planted_;
    std::optional<int> plantedDisagreement_;
    bool failWrite_ = false;
    bool failAdopt_ = false;
    int adoptions_ = 0;
    int shows_ = 0;
};

} // namespace rawpage::tests

#endif // RAWPAGE_TESTS_UNIT_COSBACKEND_H
