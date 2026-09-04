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

#include "core/backend.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/doc/RepairReport.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace rawpage::uitest {

class FakeBackend final : public core::IDocumentBackend {
public:
    // What to answer for a given path. The default opens everything, so a harness that does not
    // care about opening writes nothing; a case that does sets this.
    std::function<bool(const QString&)> accepts = [](const QString&) { return true; };

    // Every path `open()` was asked about, in order.
    QStringList asked;
    int closes = 0;

    // How many A4 pages an accepted document has. Zero gives a document with a real handle and no
    // pages, which is a state the viewer must survive and not a state any real PDF reaches.
    int pagesPerDocument = 3;

    std::array<bool, core::kBackendInterfaceCount> implementedInterfaces() const override
    {
        // The shape the shipped backend reports after Stage 2, so a widget written against this
        // fake cannot come to depend on capabilities the real one does not have.
        return {true, true, false, false, false, false, false};
    }

    core::Result<core::DocumentHandle> open(const QString& path,
                                            const core::PasswordCallback&) override
    {
        asked << path;
        if (!accepts(path)) {
            return core::Error{core::ErrorCode::ParseFailure,
                               QLatin1String("error.open.notPdf"), path};
        }
        const core::DocumentHandle handle{static_cast<std::uint64_t>(asked.size())};

        std::vector<core::Page> built;
        built.reserve(static_cast<std::size_t>(pagesPerDocument));
        for (int i = 0; i < pagesPerDocument; ++i) {
            built.emplace_back(i, core::PdfRect{core::PdfPoint{0.0, 0.0},
                                                core::PdfSize{595.276, 841.89}});
        }
        trees[handle.id] = std::make_unique<core::PageTree>(std::move(built));

        return handle;
    }

    core::Status close(core::DocumentHandle handle) override
    {
        ++closes;
        trees.erase(handle.id);
        return {};
    }

    // A REAL `PageTree`, because the viewer's whole job is arithmetic over one -- a null here would
    // make every layout case assert that nothing was laid out. A4 at 595.276 x 841.89 points, which
    // is what `corpus/ordinary/` is generated at.
    const core::PageTree* pages(core::DocumentHandle handle) const override
    {
        const auto found = trees.find(handle.id);
        return found == trees.end() ? nullptr : found->second.get();
    }

    // A REAL, EMPTY report by default -- which is the state "this file needed no repair", and is
    // what makes "opening a clean file shows no bar" assertable. A case that wants the banner
    // fills `repairs` in, and one that wants a backend with no report at all clears it.
    const core::RepairReport* repairReport(core::DocumentHandle handle) const override
    {
        return trees.count(handle.id) != 0 ? &repairs : nullptr;
    }

    core::RepairReport repairs;

    core::IPageRasterizer* rasterizer(core::DocumentHandle) override { return nullptr; }
    core::ITextExtractor* textExtractor(core::DocumentHandle) override { return nullptr; }
    core::MutationSink* mutationSink(core::DocumentHandle) override { return nullptr; }
    core::ISigner* signer(core::DocumentHandle) override { return nullptr; }
    core::IOcrEngine* ocrEngine() override { return nullptr; }
    core::IIndexer* indexer() override { return nullptr; }

private:
    // `mutable` because `pages()` is const and `open()` is not: the map is the fake's storage, and
    // the constness belongs to the interface rather than to this object's bookkeeping.
    mutable std::map<std::uint64_t, std::unique_ptr<core::PageTree>> trees;
};

} // namespace rawpage::uitest

#endif // RAWPAGE_TESTS_UI_FAKEBACKEND_H
