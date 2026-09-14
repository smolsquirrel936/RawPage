#include "pdf/BackendFactory.h"

#include "core/Log.h"
#include "pdf/DocumentBackendImpl.h"

#include <QStringList>

#include <string_view>
#include <vector>

namespace rawpage::pdf {

// T9 REPLACED THE NULL BACKEND WITH THE REAL ONE, and the file MOVED from `pdf/` to `pdf/render/`
// in the same change. Both halves are consequences of the same fact: a backend that opens documents
// IS the engine.
//
// Until T9 this returned a backend implementing NONE of the seven interfaces -- every method
// answering `notImplementedError` and naming itself -- which was the honest implementation of
// "declared, not built yet" (D2-7, convention 6) rather than scaffolding. That class is gone
// because `DocumentBackendImpl` now answers two of the seven for real, and keeping a second
// backend nobody constructs is the placeholder-that-outlives-its-purpose shape Stage 2 T1 deleted
// `PdfModule.cpp` for.
//
// **The compilation unit had to leave `rawpage_cos`**: `tests/unit` links that library under
// `rawpage_assert_headless()`, which forbids `rawpage_pdfium` BY NAME (D2-2), and the real backend
// needs PDFium. So `pdf/BackendFactory.h` still declares the one exported symbol and this
// definition lives beside the engine. On the portable presets there is now no `createBackend()` at
// all, which is correct: `app/` is not built there, and `app/CompositionRoot` is its only caller.
//
// What that costs is stated rather than absorbed -- see `pdf/DocumentBackendImpl.h`: the concrete
// backend's census is no longer asserted under ASan. `core/backend.h` itself still is, over a test
// backend defined in the headless suite.
std::unique_ptr<core::IDocumentBackend> createBackend()
{
    auto backend = std::make_unique<DocumentBackendImpl>();

    // THE CENSUS LINE, and it is not decoration. `app/CompositionRoot` documents that this factory
    // logs its own interface census the way the Windows platform bundle logs its, and a build that
    // cannot do something should say so in its log rather than leave a caller to find out from an
    // empty result (D2-7, convention 6). It read "0 of 7" from T1 until T9, 2 until Stage 3 T4 and 3 from
    // then (`MutationSink`); the names of the ones missing are what makes "not built yet" printable.
    //
    // It was briefly LOST when T9 replaced the null backend -- the line lived in that class's
    // constructor -- and restored when a smoke run of `--dump-ui-strings` showed the platform
    // bundle's census with nothing beside it.
    const std::vector<std::string_view> missing = core::unimplementedBackendInterfaces(*backend);
    const std::size_t implemented = core::kBackendInterfaceCount - missing.size();

    QStringList names;
    names.reserve(static_cast<qsizetype>(missing.size()));
    for (const std::string_view name : missing)
        names << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));

    RAWPAGE_LOG_INFO(core::LogSubsystem::Render)
        << "backend: assembled --" << static_cast<int>(implemented) << "of"
        << static_cast<int>(core::kBackendInterfaceCount) << "interfaces implemented; missing:"
        << names.join(QLatin1String(", "));

    return backend;
}

} // namespace rawpage::pdf
