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
#include <algorithm>
#include <cstring>
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

// ==========================================================================================
// The VIEW rotation -- Stage 2 T14. `FPDF_RenderPageBitmap`'s `rotate` argument.
// ==========================================================================================

TEST_CASE("a rotated view produces different pixels for the same tile", "[render][rotation]")
{
    // THE HALF WITH NO SYMPTOM IF IT IS WRONG. `RenderRequest::viewRotation` is part of the cache
    // key, so a rasterizer that ignored it would be handed a rotation, return the unrotated tile,
    // and have it cached under the rotated key -- the rotate command appearing to do nothing, with
    // no error and no failing test anywhere else.
    auto document = openOrFail("ordinary/text-a4.pdf");
    PdfiumRasterizer rasterizer(*document);

    RenderRequest upright = requestFor(0, TileRect{0, 0, 300, 300}, 1.0);
    RenderRequest turned = upright;
    turned.viewRotation = rawpage::core::PageRotation::Clockwise90;

    // …AND THE KEYS DIFFER, which is what makes the two reach the engine at all rather than one
    // being served for the other.
    CHECK(upright != turned);

    const auto a = rasterizer.raster(upright);
    const auto b = rasterizer.raster(turned);
    REQUIRE(a);
    REQUIRE(b);

    // Same buffer size -- the TILE did not change shape, the page under it did.
    REQUIRE(a.value().width == b.value().width);
    REQUIRE(a.value().height == b.value().height);

    // Different content. Both have ink on them; if they were identical the `rotate` argument was
    // not passed, which is the failure this case exists for.
    CHECK(distinctBytes(a.value()) > 2);
    CHECK(distinctBytes(b.value()) > 2);
    CHECK(a.value().pixels != b.value().pixels);
}

TEST_CASE("a rotated page is rasterized against its rotated extent", "[render][rotation]")
{
    // A4 is 595x842 points. At 1.0 device pixels per point a tile at x=700 is off the right edge of
    // the upright page and INSIDE the rotated one -- so the request that must be refused and the
    // request that must succeed are the same request with one field changed. An implementation that
    // computed the extent from the unrotated size would refuse both.
    auto document = openOrFail("ordinary/text-a4.pdf");
    PdfiumRasterizer rasterizer(*document);

    RenderRequest offRight = requestFor(0, TileRect{700, 0, 64, 64}, 1.0);
    CHECK_FALSE(rasterizer.raster(offRight));

    offRight.viewRotation = rawpage::core::PageRotation::Clockwise90;
    CHECK(rasterizer.raster(offRight));
}


// ==========================================================================================
// The rendering flags -- Stage 2 T15, guide case A-65's engine half.
// ==========================================================================================

namespace {

// How many pixels differ between two rasters of the same tile. A COUNT rather than a boolean,
// because "these differ" and "these differ in four pixels out of ninety thousand" are different
// claims and only the second says a flag did something a person could see.
std::size_t differingPixels(const RasterImage& a, const RasterImage& b)
{
    if (a.width != b.width || a.height != b.height) return static_cast<std::size_t>(-1);

    std::size_t differing = 0;
    for (int y = 0; y < a.height; ++y) {
        const auto* rowA = reinterpret_cast<const unsigned char*>(a.pixels.constData())
                           + static_cast<std::size_t>(y) * a.bytesPerLine;
        const auto* rowB = reinterpret_cast<const unsigned char*>(b.pixels.constData())
                           + static_cast<std::size_t>(y) * b.bytesPerLine;
        for (int x = 0; x < a.width; ++x)
            if (std::memcmp(rowA + x * 4, rowB + x * 4, 3) != 0) ++differing;
    }
    return differing;
}

// How many pixels are darker than `threshold` on every channel.
//
// TWO THRESHOLDS ARE USED BELOW AND THE DIFFERENCE MATTERS. Counting every non-white pixel
// (`0xf0`) counts the ANTI-ALIASING FRINGE, so turning smoothing off reduces it -- which looks
// like a thinner line and is not one. Counting only SOLID ink (`0x80`) counts the line's core,
// and that is what a true-width hairline would actually reduce: a sub-pixel line has no fully
// dark pixel at all, only grey ones.
std::size_t darkerThan(const RasterImage& image, int threshold)
{
    std::size_t ink = 0;
    for (int y = 0; y < image.height; ++y) {
        const auto* row = reinterpret_cast<const unsigned char*>(image.pixels.constData())
                          + static_cast<std::size_t>(y) * image.bytesPerLine;
        for (int x = 0; x < image.width; ++x) {
            if (row[x * 4] < threshold && row[x * 4 + 1] < threshold
                && row[x * 4 + 2] < threshold) {
                ++ink;
            }
        }
    }
    return ink;
}

} // namespace

TEST_CASE("each anti-aliasing flag measurably changes the raster", "[render][flags]")
{
    // A-65's first three, and the whole point of the case is that the answer is a NUMBER. A flag
    // that reached the engine and did nothing would satisfy "the setting round-trips" (A-63) and be
    // exactly the defect Stage 1's I-38 was -- `general/touchMode`, stored and read by nothing,
    // which only a person eventually noticed.
    auto document = openOrFail("ordinary/text-a4.pdf");
    PdfiumRasterizer rasterizer(*document);

    const TileRect tile{0, 0, 400, 400};
    RenderRequest smooth = requestFor(0, tile, 1.0);
    const auto base = rasterizer.raster(smooth);
    REQUIRE(base);

    // TEXT. `text-a4.pdf` is body copy, so disabling text anti-aliasing has thousands of edge
    // pixels to change.
    RenderRequest noText = smooth;
    noText.flags = noText.flags | rawpage::core::render::RenderFlags::NoSmoothText;
    const auto withoutText = rasterizer.raster(noText);
    REQUIRE(withoutText);
    const std::size_t textDelta = differingPixels(base.value(), withoutText.value());
    WARN("antialiasText off changes " << textDelta << " pixels");
    CHECK(textDelta > 100);

    // LINE ART, on the file that has some. A hairline page has far fewer edge pixels than a text
    // page, which is why this uses a different fixture rather than reusing the one above and
    // hoping.
    auto vectors = openOrFail("ordinary/vector-hairlines.pdf");
    PdfiumRasterizer vectorRasterizer(*vectors);
    RenderRequest paths = requestFor(0, tile, 1.0);
    const auto pathsSmooth = vectorRasterizer.raster(paths);
    REQUIRE(pathsSmooth);

    paths.flags = paths.flags | rawpage::core::render::RenderFlags::NoSmoothPath;
    const auto pathsHard = vectorRasterizer.raster(paths);
    REQUIRE(pathsHard);
    const std::size_t pathDelta = differingPixels(pathsSmooth.value(), pathsHard.value());
    WARN("antialiasLineArt off changes " << pathDelta << " pixels");
    CHECK(pathDelta > 10);

    // IMAGES, AND THE SCALE IS THE POINT. Image smoothing only bites where a source pixel is being
    // STRETCHED or shrunk, so at `deviceScale` 1.0 this fixture's image is drawn near enough to
    // 1:1 that the flag changes **zero** pixels -- measured, and the reason this case scales up
    // rather than reusing the tile above and hoping. A case that asserted a difference at 1.0
    // would have failed for a reason that is about the corpus rather than about the flag.

    // THE WHOLE PAGE, not the top-left tile. Measured while writing this case: a 400x400 tile at
    // the page's origin contains no part of this fixture's image, so the flag changed zero pixels
    // and the case would have been asserting where the image ISN'T. `A4 is 595x842 points`, so one
    // tile that size at 1.0 covers all of it.
    const TileRect wholePage{0, 0, 600, 850};

    std::size_t bestImageDelta = 0;
    for (const char* fixture : {"ordinary/image-dct-rgb.pdf", "ordinary/image-dct-gray.pdf",
                                "ordinary/image-indexed-stencil.pdf",
                                "ordinary/image-flate-predictor.pdf",
                                "ordinary/image-ccitt-g4.pdf"}) {
        auto file = openOrFail(fixture);
        PdfiumRasterizer each(*file);
        for (const double scale : {0.5, 1.0, 4.0}) {
            RenderRequest smoothRequest = requestFor(0, wholePage, scale);
            const auto withSmoothing = each.raster(smoothRequest);
            if (!withSmoothing) continue;

            RenderRequest hardRequest = smoothRequest;
            hardRequest.flags =
                hardRequest.flags | rawpage::core::render::RenderFlags::NoSmoothImage;
            const auto without = each.raster(hardRequest);
            if (!without) continue;

            const std::size_t delta = differingPixels(withSmoothing.value(), without.value());
            if (delta != static_cast<std::size_t>(-1)) bestImageDelta = std::max(bestImageDelta, delta);
        }
    }

    // THE SWEEP IS THE CASE, and it is a sweep because the first two ways of writing this measured
    // ZERO and would have been reported as a finding about PDFium.
    //
    // `image-dct-rgb.pdf` at `deviceScale` 1.0 over a 400x400 tile at the page origin: 0 pixels --
    // the tile contains no part of that file's image. The same file over the whole page at 1.0 and
    // at 4.0: 0 again. Image smoothing only bites where a source pixel is being STRETCHED or shrunk
    // hard, and which fixture at which scale does that is a property of the corpus rather than of
    // the flag -- so a case that picked one and asserted a floor would have been asserting where
    // the resampling isn't, and a case that picked one and asserted ZERO would have declared a
    // working flag broken.
    //
    // Sweeping every image fixture at three scales finds it: **23,928 pixels**. The floor below is
    // two orders of magnitude under that, because what is being established is that the flag
    // reaches the engine and does something, not that this particular number is stable.
    WARN("antialiasImages off changes at most " << bestImageDelta
                                                << " pixels across every corpus image fixture");
    CHECK(bestImageDelta > 100);
}

TEST_CASE("NO PDFium flag can draw a hairline at its true width -- issue I-11",
          "[render][flags]")
{
    // THIS CASE IS THE MEASUREMENT BEHIND A SCOPE FINDING, and it is a case rather than a paragraph
    // because "I read the header" is weaker evidence than "I tried them".
    //
    // `Features.md` 1.2 defines Line Weight as *"draw hairlines at their true width instead of a
    // minimum of one pixel"*, and `pageDisplay/lineWeight` is in `Implementation-Plan.md`'s Stage 2
    // key list. PDFium always enforces the one-pixel minimum and its `FPDF_*` render flags have no
    // control over it -- so the setting's ON state is a thing this engine cannot be asked for.
    //
    // D3 says we reach the engine through the `FPDF_*` C API and nothing else, so this is a
    // CAPABILITY GAP rather than a defect: the same shape as **I-06**, where `/JBIG2Decode` is
    // declared and decodes nothing because the only usable library is AGPL-3.0.
    auto vectors = openOrFail("ordinary/vector-hairlines.pdf");
    PdfiumRasterizer rasterizer(*vectors);

    const TileRect tile{0, 0, 400, 400};
    const RenderRequest plain = requestFor(0, tile, 1.0);
    const auto base = rasterizer.raster(plain);
    REQUIRE(base);

    const std::size_t fringeBefore = darkerThan(base.value(), 0xf0);
    const std::size_t solidBefore = darkerThan(base.value(), 0x80);
    WARN("hairlines: " << fringeBefore << " pixels of ink, " << solidBefore << " of them solid");
    REQUIRE(solidBefore > 0);   // the fixture has hairlines on it at all

    // Every flag `core::render::RenderFlags` can express. None of them thins a line: the smoothers
    // change EDGE softness, `Grayscale` changes colour, `NoNativeText` is about text. There is no
    // fourth option, and that absence is the finding.
    for (const auto flag : {rawpage::core::render::RenderFlags::NoSmoothPath,
                            rawpage::core::render::RenderFlags::NoSmoothImage,
                            rawpage::core::render::RenderFlags::NoNativeText}) {
        RenderRequest request = plain;
        request.flags = request.flags | flag;
        const auto rendered = rasterizer.raster(request);
        REQUIRE(rendered);

        const std::size_t fringeAfter = darkerThan(rendered.value(), 0xf0);
        const std::size_t solidAfter = darkerThan(rendered.value(), 0x80);
        WARN("  with a flag: " << fringeAfter << " ink, " << solidAfter << " solid");

        // THE SOLID CORE DOES NOT MEANINGFULLY SHRINK, and the assertion is on the solid count
        // rather than the total precisely so the anti-aliasing fringe cannot be mistaken for the
        // thing being looked for: `NoSmoothPath` drops total ink by a third -- 12,598 to 8,528,
        // measured -- and that is the fringe going away, not a thinner line. Its SOLID count moves
        // from 7,013 to 6,926, which is 1.2 %; the other two flags move it by nothing at all.
        //
        // A flag that drew hairlines at their true width would drive this toward ZERO, because a
        // sub-pixel line has no fully dark pixel at all. Ten per cent is far outside the noise
        // these flags produce and far inside what the feature would.
        CHECK(solidAfter * 10 >= solidBefore * 9);
    }

    // `pageDisplay/lineWeight` is therefore DECLARED AND INERT, and `ViewSettings::lineWeight`
    // carries it so that the day an engine can honour it there is a value to honour. Guide issue
    // I-11 records it, and A-65's criterion is corrected there rather than quietly failed -- exactly
    // as A-18's was at T7, when "encode then decode returns the input" turned out not to be a
    // property JPEG has.
    SUCCEED();
}

TEST_CASE("the transparency flag changes the bitmap format and leaves the page unpainted",
          "[render][flags]")
{
    // Stage 2 T15. `pageDisplay/transparencyGrid` only works if the RASTER carries alpha: a
    // checkerboard painted behind an opaque page is invisible, because the rasterizer fills its
    // bitmap with white before drawing and a transparent region of the page comes back white.
    //
    // The first implementation of that option was a viewer-side paint with no flag, and it showed
    // nothing -- a feature that reads back as enabled and does nothing, which is Stage 1's I-38.
    auto document = openOrFail("ordinary/text-a4.pdf");
    PdfiumRasterizer rasterizer(*document);

    const TileRect tile{0, 0, 300, 300};
    const RenderRequest opaque = requestFor(0, tile, 1.0);
    const auto onWhite = rasterizer.raster(opaque);
    REQUIRE(onWhite);

    // NO ALPHA CHANNEL AT ALL in the ordinary case: `Bgrx8888`'s fourth byte is padding, which is
    // what `core/backend.h` says and why asking for transparency has to change the FORMAT and not
    // only the fill colour.
    CHECK(onWhite.value().format == rawpage::core::PixelFormat::Bgrx8888);

    RenderRequest transparent = opaque;
    transparent.flags = transparent.flags
                        | rawpage::core::render::RenderFlags::TransparentBackground;
    CHECK(opaque != transparent);   // and therefore a different cache entry

    const auto onNothing = rasterizer.raster(transparent);
    REQUIRE(onNothing);
    CHECK(onNothing.value().format == rawpage::core::PixelFormat::Bgra8888);

    // AND THE PAGE IS ACTUALLY TRANSPARENT WHERE IT HAS NO CONTENT. A page of body copy is mostly
    // empty, so most of this tile should have an alpha of zero -- which is exactly the region a
    // checkerboard is meant to show through.
    std::size_t clear = 0;
    const RasterImage& image = onNothing.value();
    for (int y = 0; y < image.height; ++y) {
        const auto* row = reinterpret_cast<const unsigned char*>(image.pixels.constData())
                          + static_cast<std::size_t>(y) * image.bytesPerLine;
        for (int x = 0; x < image.width; ++x)
            if (row[x * 4 + 3] == 0x00) ++clear;
    }
    const std::size_t total = static_cast<std::size_t>(image.width) * image.height;
    WARN("transparent pixels: " << clear << " of " << total);
    CHECK(clear > total / 2);
}
