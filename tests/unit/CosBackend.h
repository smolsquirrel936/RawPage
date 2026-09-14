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

#include "core/backend.h"
#include "core/doc/DocumentId.h"
#include "core/doc/PageTree.h"
#include "core/doc/Permissions.h"
#include "core/doc/RepairReport.h"
#include "pdf/ops/MutationSinkImpl.h"

#include <QFile>

#include <map>
#include <memory>
#include <optional>

namespace rawpage::tests {

class CosBackend final : public core::IDocumentBackend {
public:
    void plantReading(core::CosReading reading) { planted_ = reading; }

    std::array<bool, core::kBackendInterfaceCount> implementedInterfaces() const override
    {
        std::array<bool, core::kBackendInterfaceCount> implemented{};
        implemented[static_cast<std::size_t>(core::BackendInterface::DocumentBackend)] = true;
        implemented[static_cast<std::size_t>(core::BackendInterface::Mutation)] = true;
        return implemented;
    }

    core::Result<core::DocumentHandle> open(const QString& path, const core::PasswordCallback&) override
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.open.notFound"), path};
        auto entry = std::make_unique<Entry>();
        entry->bytes = file.readAll();
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
        entry->permissions = core::Permissions::assess(
            entry->tree, planted_.value_or(core::CosReading::Read), facts);
        planted_.reset();
        entry->id = core::DocumentId::of(path, 1'700'000'000'000LL, entry->bytes);
        if (entry->permissions.canEditPages())
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
        QByteArray bytes;
        core::PageTree tree;
        core::RepairReport repair;
        core::Permissions permissions;
        core::DocumentId id;
        std::unique_ptr<pdf::ops::MutationSinkImpl> sink;
    };
    const Entry* find(core::DocumentHandle handle) const
    {
        const auto found = entries_.find(handle.id);
        return found == entries_.end() ? nullptr : found->second.get();
    }
    std::map<std::uint64_t, std::unique_ptr<Entry>> entries_;
    std::uint64_t next_ = 1;
    std::optional<core::CosReading> planted_;
};

} // namespace rawpage::tests

#endif // RAWPAGE_TESTS_UNIT_COSBACKEND_H
