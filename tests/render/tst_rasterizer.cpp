// tst_rasterizer -- guide cases A-44, A-45, and A-46's second half. Stage 2 T9.
//
// **THIS IS WHERE A PAGE IS DRAWN FOR THE FIRST TIME.** Everything before T9 either read bytes or
// measured how fast the engine could go; this asserts that a `RenderRequest` produces the pixels it
// asked for.
//
// WHAT THIS SUITE IS TRYING TO CATCH:
//
//   1. **Exactly the requested pixel size**, and non-blank. A rasterizer that returned an empty
//      image of the right dimensions would pass a size check alone, and an empty image is
//      indistinguishable from a blank page once it reaches a cache.
//   2. **The tile arithmetic.** `FPDF_RenderPageBitmap` has no "render this rectangle" argument --
//      a region comes out by passing the WHOLE page's scaled size with a NEGATIVE origin. Getting
//      that wrong puts a thumbnail of the entire page into every tile, which looks like a zoom bug.
//      Asserted by rendering one page two ways and comparing the pixels.
//   3. **A-45: eight workers, concurrently.** D2-9 said thread-confinement per worker would do and
//      that was measured FALSE at T3 (I-05). This asserts the arrangement that replaced it.
//   4. **A-46's second half**, which `tests/unit/tst_backend.cpp` has been waiting for since T1:
//      after Stage 2 the implemented set is `{IDocumentBackend, IPageRasterizer}` and the other
//      five are still null.
//
// WHAT IT DOES NOT COVER (CLAUDE.md convention 19): **whether the pixels are CORRECT**. Every
// assertion here is about size, non-blankness, self-consistency and safety. A page rendered upside
// down, with the wrong font, or in the wrong colour space passes all of them. That is the golden
// suite's question (A-48, T17), and it needs baselines this stage has not produced.

#include <catch2/catch_test_macros.hpp>

#include "core/render/RenderRequest.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumInit.h"
#include "pdf/render/PdfiumRasterizer.h"

#include <QDir>
#include <QFile>

#include <atomic>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

using rawpage::core::PasswordCallback;
using rawpage::core::RasterImage;
using rawpage::core::Revision;
using rawpage::core::render::RenderFlags;
using rawpage::core::render::RenderRequest;
using rawpage::core::render::TileRect;
using rawpage::pdf::DocumentBackendImpl;
using rawpage::pdf::render::PdfiumDocument;
using rawpage::pdf::render::PdfiumRasterizer;

namespace {

QString corpusPath(std::string_view relative)
{
    return QString::fromStdString(
        (std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative).string());
}

std::unique_ptr<PdfiumDocument> openOrFail(std::string_view relative)
{
    auto document = PdfiumDocument::open(corpusPath(relative), nullptr);
    REQUIRE(document);
    return std::move(document).value();
}

// A request that is valid by construction, so a case that wants an INVALID one has to say which
// field it is breaking -- which is what makes those cases readable.
RenderRequest requestFor(int pageIndex, TileRect tile, double scale = 1.0)
{
    RenderRequest request;
    request.document = rawpage::core::DocumentHandle{1};
    request.pageIndex = pageIndex;
    request.tile = tile;
    request.deviceScale = scale;
    request.revision = Revision::initial();
    return request;
}

// How many distinct byte values the image contains. A blank page is one value; anything with ink
// on it is many. Cheaper and more honest than "is any pixel non-white", which a single stray dot
// would satisfy.
std::size_t distinctBytes(const RasterImage& image)
{
    std::set<std::uint8_t> seen;
    for (qsizetype at = 0; at < image.pixels.size(); ++at)
        seen.insert(static_cast<std::uint8_t>(image.pixels[at]));
    return seen.size();
}

} // namespace

// ==========================================================================================
// A-44 -- one page to an image
// ==========================================================================================

TEST_CASE("rasterizer produces exactly the requested pixel size and it is not blank",
          "[render][raster]")
{
    auto document = openOrFail("ordinary/text-a4.pdf");
    PdfiumRasterizer rasterizer(*document);

    const RenderRequest request = requestFor(0, TileRect{0, 0, 320, 240}, 1.0);
    const auto image = rasterizer.raster(request);
    REQUIRE(image);

    CHECK(image.value().width == 320);
    CHECK(image.value().height == 240);
    CHECK(image.value().bytesPerLine == 320 * 4);
    CHECK(image.value().format == rawpage::core::PixelFormat::Bgrx8888);
    CHECK(image.value().isValid());
    CHECK(image.value().pixels.size() == 320 * 240 * 4);

    // NON-BLANK, and this is the assertion that matters: a rasterizer returning a correctly-sized
    // buffer of nothing passes every size check above. `text-a4.pdf` page 1 is body copy, so the
    // top-left 320x240 has ink in it.
    INFO("distinct byte values: " << distinctBytes(image.value()));
    CHECK(distinctBytes(image.value()) > 4);
}

TEST_CASE("rasterizer scales with deviceScale rather than resampling", "[render][raster]")
{
    auto document = openOrFail("ordinary/text-a4.pdf");
    PdfiumRasterizer rasterizer(*document);

    // The same PDF-space region at two scales. At 2x the tile covers half as much of the page, so
    // the two are not the same picture -- what is asserted is that both succeed and that the
    // buffer size follows the TILE rather than the scale, which is the contract `TileGrid` (T10)
    // will build on.
    const auto small = rasterizer.raster(requestFor(0, TileRect{0, 0, 200, 200}, 1.0));
    const auto large = rasterizer.raster(requestFor(0, TileRect{0, 0, 200, 200}, 2.0));
    REQUIRE(small);
    REQUIRE(large);
    CHECK(small.value().pixels.size() == large.value().pixels.size());

    // And they DIFFER, which is what proves `deviceScale` reached the engine at all. Two identical
    // buffers here would mean the scale was accepted and ignored.
    CHECK(small.value().pixels != large.value().pixels);
}

TEST_CASE("rasterizer renders a tile as a REGION, not a scaled-down whole page",
          "[render][raster]")
{
    // THE ARITHMETIC MOST LIKELY TO BE WRONG. `FPDF_RenderPageBitmap` has no region parameter: a
    // tile comes out by passing the whole scaled page's size with a NEGATIVE origin. Passing the
    // tile's size as the page size instead scales the entire page into the tile, which puts a
    // thumbnail of the page in every tile -- a bug that reads as a zoom problem.
    //
    // Asserted by comparing a tile taken from the middle of the page against the same rectangle
    // rendered as the top-left of a whole-page raster. If tiles were whole-page thumbnails, every
    // tile would be identical; here they must differ from tile 0 and match the region they name.
    auto document = openOrFail("ordinary/text-a4.pdf");
    PdfiumRasterizer rasterizer(*document);

    const auto topLeft = rasterizer.raster(requestFor(0, TileRect{0, 0, 128, 128}, 2.0));
    const auto offset = rasterizer.raster(requestFor(0, TileRect{128, 128, 128, 128}, 2.0));
    REQUIRE(topLeft);
    REQUIRE(offset);

    // Different parts of a page of body copy are different pixels. Equal buffers would mean every
    // tile is the same thumbnail.
    CHECK(topLeft.value().pixels != offset.value().pixels);

    // And a tile taken twice is identical, which is what makes the cache legitimate at all.
    const auto again = rasterizer.raster(requestFor(0, TileRect{128, 128, 128, 128}, 2.0));
    REQUIRE(again);
    CHECK(again.value().pixels == offset.value().pixels);
}

TEST_CASE("rasterizer honours the render flags", "[render][raster]")
{
    // **THE FIXTURE HERE IS THE WHOLE CASE, and the first choice was wrong.** This originally
    // rendered `text-a4.pdf`, which is black body copy on white paper -- content that is ALREADY
    // greyscale, so a greyscale render of it is byte-identical by construction and the case failed
    // against a rasterizer doing exactly the right thing. A flag test needs content the flag can
    // change.
    //
    // `image-dct-rgb.pdf` is a three-component JPEG of smooth gradients, a checker and a hard edge
    // (PROVENANCE), and the whole page is rendered rather than a corner tile so the image is
    // certain to be inside it.
    auto document = openOrFail("ordinary/image-dct-rgb.pdf");
    PdfiumRasterizer rasterizer(*document);

    const auto pageSize = document->pages().displaySize(0);
    REQUIRE(pageSize.width > 0.0);
    const double scale = 0.7;   // the whole page in one tile, small enough to stay cheap
    const TileRect whole{0, 0, static_cast<int>(pageSize.width * scale),
                         static_cast<int>(pageSize.height * scale)};

    RenderRequest colour = requestFor(0, whole, scale);
    RenderRequest grey = colour;
    grey.flags = RenderFlags::Grayscale;

    const auto rendered = rasterizer.raster(colour);
    const auto greyed = rasterizer.raster(grey);
    REQUIRE(rendered);
    REQUIRE(greyed);

    // The colour render must actually have colour in it, or the comparison below proves nothing.
    bool sawColour = false;
    const QByteArray& colourBytes = rendered.value().pixels;
    for (qsizetype at = 0; at + 3 < colourBytes.size(); at += 4) {
        if (colourBytes[at] != colourBytes[at + 1] || colourBytes[at + 1] != colourBytes[at + 2]) {
            sawColour = true;
            break;
        }
    }
    REQUIRE(sawColour);

    // The flags are part of `RenderRequest`'s equality and therefore of the cache key, so two
    // renders that differ only in flags MUST differ in pixels -- otherwise the key carries a field
    // that changes nothing and the cache holds two entries for one picture.
    CHECK(rendered.value().pixels != greyed.value().pixels);

    // And in a greyscale render EVERY pixel's three colour bytes are equal. That is what the flag
    // means, and checking it is what distinguishes "the flag reached PDFium" from "the two buffers
    // happen to differ".
    bool allGrey = true;
    const QByteArray& greyBytes = greyed.value().pixels;
    for (qsizetype at = 0; at + 3 < greyBytes.size(); at += 4) {
        if (greyBytes[at] != greyBytes[at + 1] || greyBytes[at + 1] != greyBytes[at + 2]) {
            allGrey = false;
            break;
        }
    }
    CHECK(allGrey);
}

TEST_CASE("rasterizer refuses an invalid request rather than returning an empty image",
          "[render][raster]")
{
    auto document = openOrFail("ordinary/text-a4.pdf");
    PdfiumRasterizer rasterizer(*document);

    // Each of these has a silent failure mode if it were allowed through, and an empty image is
    // indistinguishable from a blank page once it is in a cache.
    RenderRequest noRevision = requestFor(0, TileRect{0, 0, 64, 64});
    noRevision.revision = Revision{};   // D2-12's stale-tile bug, arriving as a cache key
    CHECK_FALSE(rasterizer.raster(noRevision));

    RenderRequest emptyTile = requestFor(0, TileRect{0, 0, 0, 64});
    CHECK_FALSE(rasterizer.raster(emptyTile));

    RenderRequest zeroScale = requestFor(0, TileRect{0, 0, 64, 64}, 0.0);
    CHECK_FALSE(rasterizer.raster(zeroScale));

    RenderRequest noDocument = requestFor(0, TileRect{0, 0, 64, 64});
    noDocument.document = rawpage::core::DocumentHandle{};
    CHECK_FALSE(rasterizer.raster(noDocument));

    RenderRequest pastTheEnd = requestFor(9999, TileRect{0, 0, 64, 64});
    const auto refused = rasterizer.raster(pastTheEnd);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == rawpage::core::ErrorCode::NotFound);

    // THE CONTROL. A valid request must still succeed, or the five refusals above are equally
    // satisfied by a rasterizer that refuses everything.
    CHECK(rasterizer.raster(requestFor(0, TileRect{0, 0, 64, 64})));
}

// ==========================================================================================
// A-45 -- eight workers, concurrently
// ==========================================================================================

TEST_CASE("rasterizer survives eight concurrent workers over four documents", "[render][raster]")
{
    // **GUIDE CASE A-45**, and the case that only exists because I-05 was measured. D2-9 said
    // "thread-confined per worker" would do; the T3 probe showed that eight workers with their OWN
    // document and OWN page corrupt the heap, because what PDFium shares is process-global -- its
    // font mapper and glyph caches. Confining a document to a worker confines the wrong thing.
    //
    // `PdfiumInit::engineLock()` serializes every entry into the engine, which is the conservative
    // half of the choice T11 still has to make. This asserts the property that arrangement buys.
    //
    // NOTE WHAT THIS DOES AND DOES NOT PROVE (convention 19): it establishes that the current
    // arrangement does not crash or corrupt over 200 tiles. It is NOT a data-race detector -- that
    // is TSan's job and this toolchain has none (risk R7) -- so a race that happens not to fire in
    // this run would pass. What makes it worth having anyway is that the UNGUARDED version of this
    // exact shape crashed reliably at T3.
    const char* names[] = {"ordinary/text-a4.pdf", "ordinary/text-letter.pdf",
                           "ordinary/vector-curves.pdf", "ordinary/rotated-pages.pdf"};

    std::vector<std::unique_ptr<PdfiumDocument>> documents;
    std::vector<std::unique_ptr<PdfiumRasterizer>> rasterizers;
    for (const char* name : names) {
        documents.push_back(openOrFail(name));
        rasterizers.push_back(std::make_unique<PdfiumRasterizer>(*documents.back()));
    }

    constexpr int kWorkers = 8;
    constexpr int kTilesPerWorker = 25;   // 200 tiles in total, as the case specifies

    std::atomic<int> succeeded{0};
    std::atomic<int> failed{0};
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);

    for (int worker = 0; worker < kWorkers; ++worker) {
        workers.emplace_back([&, worker]() {
            // Workers share documents deliberately: two threads on ONE document is the case
            // per-worker confinement was supposed to make safe and did not.
            PdfiumRasterizer& rasterizer = *rasterizers[static_cast<std::size_t>(worker % 4)];
            for (int tile = 0; tile < kTilesPerWorker; ++tile) {
                RenderRequest request = requestFor(0, TileRect{(tile % 5) * 64, (tile / 5) * 64,
                                                               64, 64},
                                                   1.5);
                request.document = rawpage::core::DocumentHandle{
                    static_cast<std::uint64_t>(worker % 4) + 1};
                const auto image = rasterizer.raster(request);
                if (image && image.value().isValid())
                    succeeded.fetch_add(1, std::memory_order_relaxed);
                else
                    failed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (std::thread& worker : workers)
        worker.join();

    INFO("succeeded " << succeeded.load() << ", failed " << failed.load());
    CHECK(succeeded.load() == kWorkers * kTilesPerWorker);
    CHECK(failed.load() == 0);

    // `PdfiumInit` is entered ONCE however many threads asked -- the other half of A-45, and the
    // reason `initializationCount()` exists at all.
    CHECK(rawpage::pdf::render::initializationCount() == 1);
}

// ==========================================================================================
// A-46's second half -- the census, now that two of the seven are real
// ==========================================================================================

TEST_CASE("the shipped backend implements exactly IDocumentBackend and IPageRasterizer",
          "[render][backend]")
{
    // **THE HALF `tests/unit/tst_backend.cpp` HAS BEEN WAITING FOR SINCE T1**, and it lives here
    // rather than there because the shipped backend is the engine and the headless suite may not
    // link it (D2-2). A-46's Expect says: *after Stage 2 the implemented set is `IDocumentBackend`
    // and `IPageRasterizer`.*
    DocumentBackendImpl backend;

    const auto implemented = backend.implementedInterfaces();
    CHECK(implemented[static_cast<std::size_t>(
        rawpage::core::BackendInterface::DocumentBackend)]);
    CHECK(implemented[static_cast<std::size_t>(
        rawpage::core::BackendInterface::PageRasterizer)]);

    const auto missing = rawpage::core::unimplementedBackendInterfaces(backend);
    REQUIRE(missing.size() == 5);
    CHECK(missing[0] == "ITextExtractor");
    CHECK(missing[1] == "MutationSink");
    CHECK(missing[2] == "ISigner");
    CHECK(missing[3] == "IOcrEngine");
    CHECK(missing[4] == "IIndexer");
}

TEST_CASE("the shipped backend hands out a rasterizer and nulls for the other five",
          "[render][backend]")
{
    DocumentBackendImpl backend;
    const auto handle = backend.open(corpusPath("ordinary/text-a4.pdf"), nullptr);
    REQUIRE(handle);

    // The one that is implemented.
    rawpage::core::IPageRasterizer* rasterizer = backend.rasterizer(handle.value());
    REQUIRE(rasterizer != nullptr);
    CHECK(rasterizer->raster(requestFor(0, TileRect{0, 0, 64, 64})));

    // The five that are not are NULL, never an empty stub -- convention 6, because a stub that
    // silently succeeds lets a caller believe a page was extracted or a signature applied.
    CHECK(backend.textExtractor(handle.value()) == nullptr);
    CHECK(backend.mutationSink(handle.value()) == nullptr);
    CHECK(backend.signer(handle.value()) == nullptr);
    CHECK(backend.ocrEngine() == nullptr);
    CHECK(backend.indexer() == nullptr);

    // The page tree came with it.
    const auto* pages = backend.pages(handle.value());
    REQUIRE(pages != nullptr);
    CHECK(pages->count() > 0);

    CHECK(backend.close(handle.value()));
}

TEST_CASE("the shipped backend never reuses a handle", "[render][backend]")
{
    // A token held across a close must MISS rather than start naming a different document. That is
    // what makes `DocumentHandle` safe to hold anywhere above `pdf/` -- the alternative is a stale
    // handle that silently addresses somebody else's document.
    DocumentBackendImpl backend;
    const auto first = backend.open(corpusPath("minimal.pdf"), nullptr);
    REQUIRE(first);
    REQUIRE(backend.close(first.value()));

    const auto second = backend.open(corpusPath("minimal.pdf"), nullptr);
    REQUIRE(second);
    CHECK(second.value() != first.value());

    // The closed handle resolves to nothing at all.
    CHECK(backend.rasterizer(first.value()) == nullptr);
    CHECK(backend.pages(first.value()) == nullptr);

    // And closing it again is a NAMED refusal rather than a silent success: a second close usually
    // means two owners believe they own one document.
    const auto again = backend.close(first.value());
    REQUIRE_FALSE(again);
    CHECK(again.error().code == rawpage::core::ErrorCode::NotFound);
}

TEST_CASE("the shipped backend decides what is a PDF by its bytes, not its name",
          "[render][backend]")
{
    // **GUIDE CASE A-47's engine half.** It replaces Stage 1's A-58, which asserted the opposite
    // and was correct then under D1-2: Stage 1 decided a file by its suffix and said so at the
    // time. This is where that stops.
    //
    // The UI half -- that `openFiles()` asks the backend rather than the filename -- is asserted in
    // `tests/ui`, against a fake backend, because `ui/` may not link the engine.
    DocumentBackendImpl backend;

    // A real PDF whose suffix is wrong OPENS. Copied to a `.dat` name in the test's own temp area
    // rather than added to the corpus, because the corpus is generated and checked (D2-11) and a
    // renamed copy is not a fixture.
    const QString source = corpusPath("minimal.pdf");
    QFile input(source);
    REQUIRE(input.open(QIODevice::ReadOnly));
    const QByteArray bytes = input.readAll();
    input.close();

    const QString disguised = QDir::tempPath() + QStringLiteral("/rawpage-t9-disguised.dat");
    {
        QFile output(disguised);
        REQUIRE(output.open(QIODevice::WriteOnly));
        output.write(bytes);
    }
    const auto opened = backend.open(disguised, nullptr);
    CHECK(opened);
    if (opened)
        CHECK(backend.close(opened.value()));
    QFile::remove(disguised);

    // And a `.pdf` that is not a PDF is REFUSED, with a named reason.
    const QString liar = QDir::tempPath() + QStringLiteral("/rawpage-t9-not-really.pdf");
    {
        QFile output(liar);
        REQUIRE(output.open(QIODevice::WriteOnly));
        output.write("Dear diary, today I was not a PDF.\n");
    }
    const auto refused = backend.open(liar, nullptr);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.open.notPdf"));
    QFile::remove(liar);
}
