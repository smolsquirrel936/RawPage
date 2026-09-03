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

#include <QString>
#include <QStringList>

#include <functional>

namespace rawpage::uitest {

class FakeBackend final : public core::IDocumentBackend {
public:
    // What to answer for a given path. The default opens everything, so a harness that does not
    // care about opening writes nothing; a case that does sets this.
    std::function<bool(const QString&)> accepts = [](const QString&) { return true; };

    // Every path `open()` was asked about, in order.
    QStringList asked;
    int closes = 0;

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
        return core::DocumentHandle{static_cast<std::uint64_t>(asked.size())};
    }

    core::Status close(core::DocumentHandle) override
    {
        ++closes;
        return {};
    }

    core::IPageRasterizer* rasterizer(core::DocumentHandle) override { return nullptr; }
    core::ITextExtractor* textExtractor(core::DocumentHandle) override { return nullptr; }
    core::MutationSink* mutationSink(core::DocumentHandle) override { return nullptr; }
    core::ISigner* signer(core::DocumentHandle) override { return nullptr; }
    core::IOcrEngine* ocrEngine() override { return nullptr; }
    core::IIndexer* indexer() override { return nullptr; }
};

} // namespace rawpage::uitest

#endif // RAWPAGE_TESTS_UI_FAKEBACKEND_H
