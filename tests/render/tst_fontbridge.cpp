// tst_fontbridge -- guide cases A-66 and A-67. Stage 2 T16.
//
// **THIS IS THE ONLY SUITE THAT DRIVES THE WHOLE SUBSTITUTION CHAIN**, and it is here rather than
// in `tests/unit` or `tests/platform` because that chain has three links in three libraries:
//
//     PDFium  ->  pdf/render/FontResolverBridge  ->  platform/win/WinFontResolver
//                                                        -> core/render/FontSubstitution
//
// `tests/unit` may link neither the engine nor the OS layer (`rawpage_assert_headless()`, D2-2) and
// `tests/platform` may not link the engine, so each of them can drive one link and infer the rest.
// That is issue **I-09**'s shape exactly -- the arrangement the product actually runs being the one
// arrangement no suite reaches -- and it is why `rawpage_render_tests` gained `rawpage_platform` at
// T16, with the same argument the target's own CMakeLists records for why it exists at all.
//
// WHAT EACH LAYER'S OWN SUITE STILL OWNS, so this file does not restate it:
//
//   `tests/unit/tst_fontsubstitution.cpp`  the POLICY -- which families, in which order, and why a
//                                          Traditional-Chinese hint is not a Simplified one. Runs
//                                          with no display, no Windows and no engine.
//   `tests/platform/tst_fontresolver.cpp`  the OS QUERY -- whether a family is installed here and
//                                          which file backs it, plus its own substitution walk.
//
// WHAT THIS SUITE DOES NOT COVER (convention 19): **whether the substituted page LOOKS right.**
// Every assertion below is about which face was chosen and how many times it was fetched. A page
// rendered in the correct substitute at the wrong size, or with the document's /Widths ignored,
// passes all of them. That is the golden suite's question (A-48, T17).

#include <catch2/catch_test_macros.hpp>

#include "core/render/RenderRequest.h"
#include "pdf/render/FontResolverBridge.h"
#include "pdf/render/GlyphCache.h"
#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumInit.h"
#include "pdf/render/PdfiumRasterizer.h"
#include "platform/win/WinFontResolver.h"

#include <QString>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <string_view>

using rawpage::core::RasterImage;
using rawpage::core::Revision;
using rawpage::core::render::RenderRequest;
using rawpage::core::render::TileRect;
using rawpage::pdf::render::FontResolverBridge;
using rawpage::pdf::render::PdfiumDocument;
using rawpage::pdf::render::PdfiumRasterizer;
using rawpage::platform::win::WinFontResolver;

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

std::size_t distinctBytes(const RasterImage& image)
{
    std::set<std::uint8_t> seen;
    for (qsizetype at = 0; at < image.pixels.size(); ++at)
        seen.insert(static_cast<std::uint8_t>(image.pixels[at]));
    return seen.size();
}

// Installs the real resolver behind the real hook, and takes it out again -- so that a case which
// throws does not leave the process's font mapping pointing at a destroyed object. Every case in
// this file needs exactly this, and a case that forgot the teardown would fail a LATER case in a
// way that reads as a defect in the later one.
class InstalledBridge {
public:
    InstalledBridge()
    {
        FontResolverBridge::resetLog();
        REQUIRE(FontResolverBridge::install(resolver_));
    }
    ~InstalledBridge() { FontResolverBridge::uninstall(); }

    InstalledBridge(const InstalledBridge&) = delete;
    InstalledBridge& operator=(const InstalledBridge&) = delete;

private:
    WinFontResolver resolver_;
};

} // namespace

// ==========================================================================================
// A-66 -- a missing embedded font substitutes through OUR policy, and the case NAMES the face
// ==========================================================================================

TEST_CASE("a font the document does not embed is substituted by a face this project chose",
          "[render][font]")
{
    InstalledBridge bridge;

    auto document = openOrFail("ordinary/font-not-embedded.pdf");
    PdfiumRasterizer rasterizer(*document);

    const auto image = rasterizer.raster(requestFor(0, TileRect{0, 0, 600, 800}, 1.0));
    REQUIRE(image);

    // The page rendered and has ink on it. Without this the rest of the case could pass over a
    // blank sheet: a bridge that was asked and answered, on a page nothing drew.
    CHECK(distinctBytes(image.value()) > 4);

    const auto rows = FontResolverBridge::substitutions();

    // THE CLAIM THE CASE EXISTS FOR: the hook ran at all. Before T16, PDFium answered this question
    // internally and nothing in the product could say what it had chosen -- so an empty log here is
    // not "no fonts were missing", it is "we are back to the engine's own policy".
    REQUIRE_FALSE(rows.empty());

    // Both of the file's absent fonts reached us, and neither was matched exactly -- they cannot
    // be, because `RawPageAbsentSerif` and `RawPageAbsentSans` are ours and are installed nowhere.
    const auto serif = std::find_if(rows.begin(), rows.end(), [](const auto& row) {
        return row.normalized == QLatin1String("RawPageAbsentSerif");
    });
    const auto sans = std::find_if(rows.begin(), rows.end(), [](const auto& row) {
        return row.normalized == QLatin1String("RawPageAbsentSans");
    });
    REQUIRE(serif != rows.end());
    REQUIRE(sans != rows.end());
    CHECK(serif->outcome == FontResolverBridge::Outcome::Substituted);
    CHECK(sans->outcome == FontResolverBridge::Outcome::Substituted);

    // THE SUBSTITUTE IS NAMED, which is A-66's own wording. On any Windows machine both families
    // resolve, so an empty `chosen` here means the resolver fell through every candidate -- a
    // different and much larger finding than a wrong choice.
    INFO("serif substitute: " << serif->chosen.toStdString() << "  <- " << serif->requested.toStdString());
    INFO("sans  substitute: " << sans->chosen.toStdString() << "  <- " << sans->requested.toStdString());
    CHECK_FALSE(serif->chosen.isEmpty());
    CHECK_FALSE(sans->chosen.isEmpty());

    // The file it came from is a real file on this machine. Without this the chain could be
    // satisfied by a family name we invented, and PDFium would be drawing with its own face while
    // our log claimed otherwise.
    CHECK(serif->filePath.endsWith(QLatin1String(".ttf"), Qt::CaseInsensitive));

    // AND THE TWO ANSWERS DIFFER. This is the assertion that proves the DESCRIPTOR was read rather
    // than a single default returned twice: the two fonts in the fixture are identical except for
    // the serif bit in /Flags, so a resolver that ignored it -- or a hook that never reached one --
    // gives the same family for both and passes every check above.
    CHECK(serif->chosen != sans->chosen);

    // THE SUBSET TAG NEVER REACHES US, AND MEASURING THAT IS WORTH A LINE. The document says
    // `RAWPGA+RawPageAbsentSerif`; PDFium strips the six-capital tag before it calls the hook, so
    // `requested` is already clean. `normalizeBaseFontName` is therefore belt-and-braces for this
    // producer shape and load-bearing for the two it does NOT strip -- the "Arial,Bold" comma form
    // and the "TimesNewRomanPSMT" suffixes -- which is where `tests/unit` exercises it.
    CHECK_FALSE(serif->requested.contains(QLatin1Char('+')));
    CHECK(serif->normalized == serif->requested);
}

TEST_CASE("a base-14 font is DECLINED rather than substituted -- the control that found I-13",
          "[render][font]")
{
    InstalledBridge bridge;

    // `text-a4.pdf` is three pages of Helvetica, one of the fourteen faces every conforming reader
    // must have and which PDFium carries its own metric-compatible copies of.
    //
    // THIS CASE IS WHY THE BRIDGE HAS A DECLINE BRANCH. It was written expecting `mapFontCalls()
    // == 0` -- the header of every file in this chain said the hook would never be asked about a
    // standard face -- and it measured 2, with Helvetica resolved to **Segoe UI**. PDFium consults
    // the hook for EVERY unembedded font and uses its own faces only when the hook says no, so
    // without the decline, T16 silently changed how every page of every existing document renders
    // (issue **I-13**).
    //
    // Nothing in A-66's own wording called for this case. It exists because a case that proves a
    // mechanism runs is only half of proving it runs *when it should* (convention 7).
    auto document = openOrFail("ordinary/text-a4.pdf");
    PdfiumRasterizer rasterizer(*document);

    const auto image = rasterizer.raster(requestFor(0, TileRect{0, 0, 600, 800}, 1.0));
    REQUIRE(image);
    REQUIRE(distinctBytes(image.value()) > 4);   // the page really did draw text

    const auto rows = FontResolverBridge::substitutions();
    REQUIRE_FALSE(rows.empty());   // the control's control: we WERE asked, so the checks below are
                                   // about the answer rather than about the question never arriving

    for (const auto& row : rows) {
        INFO("row: " << row.normalized.toStdString() << " -> [" << row.chosen.toStdString() << "]");
        CHECK(row.outcome == FontResolverBridge::Outcome::StandardDeclined);
        CHECK(row.chosen.isEmpty());
    }
}

TEST_CASE("an embedded font never reaches the bridge either", "[render][font]")
{
    InstalledBridge bridge;

    // The second control, and it is a different mechanism from the one above rather than a repeat:
    // `latin-embedded.pdf` carries its outlines in a /FontFile2, so the question of what to
    // substitute never arises. Between the two controls, the only documents that may reach the hook
    // are those that name a font and neither embed it nor rely on the base-14 set.
    auto document = openOrFail("ordinary/latin-embedded.pdf");
    PdfiumRasterizer rasterizer(*document);

    REQUIRE(rasterizer.raster(requestFor(0, TileRect{0, 0, 600, 800}, 1.0)));

    // The embedded face itself never appears. What DOES appear is the page's base-14 caption text,
    // declined by the branch the case above is about -- so the assertion is that no row names the
    // embedded family rather than that no row exists at all.
    for (const auto& row : FontResolverBridge::substitutions()) {
        INFO("row: " << row.normalized.toStdString());
        CHECK_FALSE(row.normalized.contains(QLatin1String("RawPageCorpusLatin")));
    }
}

TEST_CASE("without the hook installed the engine substitutes silently -- the T16 before-picture",
          "[render][font]")
{
    // Deliberately NOT using `InstalledBridge`: this is the state the product was in for every
    // task before T16, and asserting it is what makes the case above a claim about our code rather
    // than about PDFium's willingness to render.
    FontResolverBridge::uninstall();
    FontResolverBridge::resetLog();

    auto document = openOrFail("ordinary/font-not-embedded.pdf");
    PdfiumRasterizer rasterizer(*document);

    const auto image = rasterizer.raster(requestFor(0, TileRect{0, 0, 600, 800}, 1.0));
    REQUIRE(image);

    // The page still renders -- PDFium picks something. What is absent is any record of what.
    CHECK(distinctBytes(image.value()) > 4);
    CHECK(FontResolverBridge::mapFontCalls() == 0);
    CHECK_FALSE(FontResolverBridge::isInstalled());
}

// ==========================================================================================
// A-67 -- the face is resolved once and read once, across tiles and across pages
//
// **THE CRITERION AS WRITTEN COULD NOT BE MET AND IS CORRECTED RATHER THAN NARROWED**, which is
// the call T7 made about A-18 and T15 made about A-65. The guide says *"rendering two adjacent
// tiles of one text page rasterizes each distinct glyph once"*. Glyph rasterization and glyph
// caching are PDFium's, inside process-global state D2-9 confines to one file; nothing on our side
// of the boundary ever sees a glyph bitmap, so nothing here can count them without inventing the
// number. What IS ours is the step before: the font is MAPPED once and its bytes are READ once, and
// both of those are measured below.
// ==========================================================================================

// WHICH OF THE TWO A-67 CASES CATCHES WHICH HALF, measured by planting the controls rather than
// reasoned about (convention 7). Disabling `GlyphCache`'s hit path reddens the SECOND case below and
// not this one -- because a second tile of the same page performs no font mapping, so it performs no
// `GetFontData` either and the cache is never asked. That is the correct outcome and it means this
// case's cache assertion is IMPLIED by its mapping assertion rather than independent of it. The
// document case is the one that exercises the cache.
TEST_CASE("a second tile of the same page maps no font and reads no face", "[render][font]")
{
    InstalledBridge bridge;

    auto document = openOrFail("ordinary/font-not-embedded.pdf");
    PdfiumRasterizer rasterizer(*document);

    REQUIRE(rasterizer.raster(requestFor(0, TileRect{0, 0, 300, 400}, 1.0)));

    const std::size_t mappedAfterFirstTile = FontResolverBridge::mapFontCalls();
    const std::size_t readsAfterFirstTile = FontResolverBridge::glyphCache().requests()
                                          - FontResolverBridge::glyphCache().hits();
    REQUIRE(mappedAfterFirstTile > 0);   // the control: something WAS mapped, so zero below means
                                         // "not again" rather than "never"

    // The adjacent tile, over the same page. Both of the fixture's text lines sit inside the first
    // 400 points, so this tile draws the same two faces again.
    REQUIRE(rasterizer.raster(requestFor(0, TileRect{300, 0, 300, 400}, 1.0)));

    CHECK(FontResolverBridge::mapFontCalls() == mappedAfterFirstTile);
    CHECK(FontResolverBridge::glyphCache().requests() - FontResolverBridge::glyphCache().hits()
          == readsAfterFirstTile);
}

TEST_CASE("a second document naming the same missing font re-maps it and still reads no face",
          "[render][font]")
{
    InstalledBridge bridge;

    auto first = openOrFail("ordinary/font-not-embedded.pdf");
    PdfiumRasterizer firstRaster(*first);
    REQUIRE(firstRaster.raster(requestFor(0, TileRect{0, 0, 600, 800}, 1.0)));

    const std::size_t mappedAfterFirst = FontResolverBridge::mapFontCalls();
    const std::size_t readsAfterFirst = FontResolverBridge::glyphCache().requests()
                                      - FontResolverBridge::glyphCache().hits();
    REQUIRE(mappedAfterFirst > 0);

    // A SECOND OPEN OF THE SAME FILE IS A SECOND DOCUMENT to the engine: font mapping is per
    // document, so this count MUST rise. That is the half the cache cannot help with, and saying so
    // is what keeps the next assertion from looking like it proves more than it does.
    auto second = openOrFail("ordinary/font-not-embedded.pdf");
    PdfiumRasterizer secondRaster(*second);
    REQUIRE(secondRaster.raster(requestFor(0, TileRect{0, 0, 600, 800}, 1.0)));

    CHECK(FontResolverBridge::mapFontCalls() > mappedAfterFirst);

    // AND THE DISK IS NOT TOUCHED AGAIN. This is the whole of what `GlyphCache` buys: the second
    // document re-resolves the family and is handed the same bytes, which for a CJK collection is
    // 18 MB of file I/O that does not happen on the render thread.
    CHECK(FontResolverBridge::glyphCache().requests() - FontResolverBridge::glyphCache().hits()
          == readsAfterFirst);
    CHECK(FontResolverBridge::glyphCache().hits() > 0);
}

TEST_CASE("the glyph cache's counters add up and its faces are bounded", "[render][font]")
{
    rawpage::pdf::render::GlyphCache cache;

    // A path that cannot be read is a request, is not a hit, and is not retained -- so a font
    // installed a moment later is found rather than remembered as absent.
    CHECK(cache.faceData(QStringLiteral("Z:/no/such/face.ttf")).empty());
    CHECK(cache.requests() == 1);
    CHECK(cache.hits() == 0);
    CHECK(cache.facesHeld() == 0);

    // An empty path is the other absent case and must not reach the filesystem at all.
    CHECK(cache.faceData(QString()).empty());
    CHECK(cache.facesHeld() == 0);
}

