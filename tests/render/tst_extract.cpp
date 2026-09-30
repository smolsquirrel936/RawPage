// tst_extract -- `pdf/render/PdfiumTextExtractor` over the corpus, and the engine thread's text job.
// Stage 5 T2, decisions D5-2 and D5-3; guide cases **A-17 … A-26**
// (`Stage5-TextSearch-Testing-Guide.md` §5.4).
//
// ---------------------------------------------------------------------------------------------
// THE TWO ORACLES, AND WHAT EACH IS NOT
// ---------------------------------------------------------------------------------------------
// **Text is graded against the GENERATOR** (the guide's rule 7), never against PDFium's own
// `FPDFText_GetText`, which would prove the engine agrees with itself. For the ten Stage 5 fixtures
// that is `corpus/labels/<name>.json`, written by `corpus/make-text.py` as it drew each run. For
// the older files A-17 names that carry no label -- `text-a4.pdf`, `a2-text-page.pdf` and
// `pages-900.pdf` -- it is **the runs the generator wrote into the content stream**, read back
// first-party through `pdf/cos/` (a stream decode and a small operator scanner in this file) and
// never through PDFium: every run there is `1 0 0 1 x y Tm (ascii) Tj` in base-14 Helvetica, so
// its text, its origin and -- from the Helvetica widths the generator itself uses -- its extent are
// all known without the engine.
//
// **Geometry is graded against the INK** (rule 6). A character's ink is found by scanning the
// rendered raster inside a window computed from the GENERATOR's position for that character, by
// code that never reads `PageTextModel` and, for the rotated pages, by a rotation written here
// rather than the product's `PageTransform`. The model's box then has to contain that ink.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS FILE MEASURED, rather than assumed (recorded in the guide's A-17 … A-23)
// ---------------------------------------------------------------------------------------------
//   * PDFium DECOMPOSES the presentation-form ligatures U+FB00…U+FB06: a glyph whose /ToUnicode
//     says U+FB01 arrives as `f` and `i`, both with the ligature's box. The expected text below
//     applies exactly that and nothing else (`expectedText`), named where it is used.
//   * A decomposed `e` + U+0301 arrives as TWO characters, as the content wrote it.
//   * A line-end hyphen arrives as U+0002; the extractor stores the hyphen-minus (A-20).
//   * A base-14 font reports weight 0; the extractor stores -1, "could not say" (A-19).
//   * Right-to-left text arrives in NEITHER order: each Hebrew word's letters are logical, and the
//     words are in left-to-right page order -- on the visual-order page and the logical-order page
//     alike (A-23). Reordering is `LayoutAnalyzer`'s (T4, D5-6).
//
// ---------------------------------------------------------------------------------------------
// WHAT IT DOES NOT COVER (convention 19)
// ---------------------------------------------------------------------------------------------
//   * Reading order across lines, blocks and columns -- `LayoutAnalyzer`'s, T4. A-17 compares
//     each LINE's characters in the model's order and places no claim on the order of lines.
//   * What a consumer does with an unmapped page -- find reporting it, copy producing nothing --
//     is T5's and T6's; A-21 here is the extractor's half.
//   * Which lane a selection or a search asks in: A-24 chooses its lanes itself.
//   * Every file is ours (D2-11, D5-23). None of this says anything about a real document.

#include <catch2/catch_test_macros.hpp>

#include "app/BackendRasterizer.h"
#include "app/BackendTextExtractor.h"
#include "core/backend.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/doc/Revision.h"
#include "core/render/DpiPolicy.h"
#include "core/render/PagePresentation.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/text/PageTextModel.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/cos/filters/Filter.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "tests/render/TextOracle.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace rawpage;
using core::DocumentHandle;
using core::PageRotation;
using core::PdfPoint;
using core::PdfRect;
using core::PdfSize;
using core::text::PageTextModel;
using core::text::TextChar;
using namespace rawpage::test_oracle;

namespace {

// =============================================================================================
// The raster, and the ink in it -- found without the model
// =============================================================================================

constexpr double kScale = 2.0;   // 144 dpi, the guide's A-18 resolution

core::RasterImage render(Opened& opened, int pageIndex, PageRotation view = PageRotation::None)
{
    const core::Page& page = opened.page(pageIndex);
    const PdfSize display = page.displaySize();
    const bool swap = core::swapsAxes(view);
    core::render::RenderRequest request;
    request.document = opened.handle;
    request.pageIndex = pageIndex;
    request.deviceScale = kScale;
    request.revision = core::Revision::initial();
    request.viewRotation = view;
    request.tile = core::render::TileRect{
        0, 0, static_cast<int>(std::ceil((swap ? display.height : display.width) * kScale)),
        static_cast<int>(std::ceil((swap ? display.width : display.height) * kScale))};
    core::IPageRasterizer* rasterizer = opened.backend.rasterizer(opened.handle);
    REQUIRE(rasterizer != nullptr);
    auto image = rasterizer->raster(request);
    REQUIRE(image);
    return std::move(image).value();
}

struct PixelBox {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;   // inclusive
    bool empty() const { return x1 < x0; }
};

struct DeviceBox {
    double left = 0, top = 0, right = 0, bottom = 0;
};

// Dark pixels -- ink -- inside a device window. The threshold keeps the faintest antialiasing
// fringe of a NEIGHBOURING glyph out of a window, and the 1 px tolerance of the containment below
// absorbs this glyph's own.
PixelBox inkIn(const core::RasterImage& image, const DeviceBox& window)
{
    PixelBox box;
    box.x0 = image.width;
    box.y0 = image.height;
    const int left = std::max(0, static_cast<int>(std::floor(window.left)));
    const int top = std::max(0, static_cast<int>(std::floor(window.top)));
    const int right = std::min(image.width, static_cast<int>(std::ceil(window.right)));
    const int bottom = std::min(image.height, static_cast<int>(std::ceil(window.bottom)));
    const auto* bytes = reinterpret_cast<const unsigned char*>(image.pixels.constData());
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const unsigned char* p = bytes + y * image.bytesPerLine + x * 4;
            if ((p[0] + p[1] + p[2]) / 3 >= 160) continue;
            box.x0 = std::min(box.x0, x);
            box.y0 = std::min(box.y0, y);
            box.x1 = std::max(box.x1, x);
            box.y1 = std::max(box.y1, y);
        }
    }
    return box;
}

bool containsWithin(const DeviceBox& outer, const PixelBox& ink, double tolerance)
{
    return ink.x0 >= outer.left - tolerance && ink.x1 + 1 <= outer.right + tolerance
           && ink.y0 >= outer.top - tolerance && ink.y1 + 1 <= outer.bottom + tolerance;
}

// THE TEST'S OWN page-to-raster mapping -- not `core::PageTransform`, which is what the product uses
// and what A-22 tests. Unrotated device space first (y down from the CropBox top), then the total
// clockwise rotation as a quarter-turn of the image.
DeviceBox oracleBox(const PdfRect& crop, int clockwise, double left, double bottom, double right,
                    double top)
{
    const double wu = crop.size.width * kScale;
    const double hu = crop.size.height * kScale;
    const auto map = [&](double x, double y) {
        const double u = (x - crop.left()) * kScale;
        const double v = (crop.top() - y) * kScale;
        switch (((clockwise % 360) + 360) % 360) {
        case 90: return std::pair<double, double>{hu - v, u};
        case 180: return std::pair<double, double>{wu - u, hu - v};
        case 270: return std::pair<double, double>{v, wu - u};
        default: return std::pair<double, double>{u, v};
        }
    };
    const auto a = map(left, bottom);
    const auto b = map(right, top);
    return DeviceBox{std::min(a.first, b.first), std::min(a.second, b.second),
                     std::max(a.first, b.first), std::max(a.second, b.second)};
}

DeviceBox shifted(const PdfRect& r, double dx, const PdfRect& crop, int clockwise)
{
    return oracleBox(crop, clockwise, r.left() + dx, r.bottom(), r.right() + dx, r.top());
}

// =============================================================================================
// The files the text cases run over
// =============================================================================================

struct LabelledFixture {
    const char* name;   // labels/<name>.json, ordinary/<name>.pdf
    std::vector<int> pages;
};

const std::vector<LabelledFixture>& labelledFixtures()
{
    static const std::vector<LabelledFixture> kFixtures = {
        {"columns-two", {0, 1, 2}},  {"columns-three", {0, 1}}, {"table-ruled", {0}},
        {"rtl-text", {0, 1}},        {"cjk-vertical", {0}},     {"cjk-readable", {0}},
        {"hyphenated", {0}},         {"search-classes", {0}},   {"text-tounicode", {0, 1}},
        {"text-900", {0, 1, 899}},
    };
    return kFixtures;
}

struct UnlabelledFixture {
    const char* path;
    std::vector<int> pages;
};

const std::vector<UnlabelledFixture>& contentFixtures()
{
    static const std::vector<UnlabelledFixture> kFixtures = {
        {"ordinary/text-a4.pdf", {0, 1, 2}},
        {"a2-text-page.pdf", {0}},
        {"ordinary/pages-900.pdf", {0, 899}},
    };
    return kFixtures;
}

// One page's text compared with its generator's lines. Returns a description of every mismatch,
// empty when the page is right.
std::vector<std::string> compareText(const PageTextModel& model, const std::vector<Line>& lines,
                                     const Claims& claims)
{
    std::vector<std::string> problems;
    for (std::size_t l = 0; l < lines.size(); ++l) {
        const QString expected = expectedText(lines[l].text);
        const QString got = textOf(model, claims.perLine[l]);
        const bool same = lines[l].direction == Direction::Rtl
                              ? sortedScalars(got) == sortedScalars(expected)
                              : got == expected;
        if (!same) {
            problems.push_back("line '" + expected.toStdString() + "' extracted as '"
                               + got.toStdString() + "'");
        }
    }
    for (const std::size_t i : claims.unclaimed) {
        const TextChar& c = model.chars()[i];
        char buffer[160];
        std::snprintf(buffer, sizeof buffer, "INVENTED U+%04X at (%.2f, %.2f), in no generated run",
                      static_cast<unsigned>(c.codepoint), c.origin.x, c.origin.y);
        problems.emplace_back(buffer);
    }
    for (const std::size_t i : claims.claimedTwice) {
        char buffer[120];
        std::snprintf(buffer, sizeof buffer, "U+%04X at (%.2f, %.2f) lies in two runs",
                      static_cast<unsigned>(model.chars()[i].codepoint), model.chars()[i].origin.x,
                      model.chars()[i].origin.y);
        problems.emplace_back(buffer);
    }
    return problems;
}

} // namespace

// =============================================================================================
// A-17 -- the text is the generator's text
// =============================================================================================

TEST_CASE("extraction returns the generator's text for every labelled page", "[render][text][A-17]")
{
    std::size_t lines = 0;
    std::size_t characters = 0;
    for (const LabelledFixture& fixture : labelledFixtures()) {
        const QJsonObject label = loadLabel(fixture.name);
        auto opened = openCorpus(std::string("ordinary/") + fixture.name + ".pdf");
        for (const int page : fixture.pages) {
            const std::vector<Line> generated = labelLines(label, page);
            INFO(fixture.name << " page " << page);
            REQUIRE_FALSE(generated.empty());
            const PageTextModel model = extract(*opened, page);
            const Claims claims = claim(model, generated);
            const std::vector<std::string> problems = compareText(model, generated, claims);
            for (const std::string& problem : problems) UNSCOPED_INFO(problem);
            CHECK(problems.empty());
            lines += generated.size();
            characters += model.shownCount();
        }
    }
    std::printf("A-17 labelled: %zu generated lines, %zu shown characters, every one accounted for\n",
                lines, characters);
    CHECK(lines > 500);
}

TEST_CASE("extraction returns the content's own runs for the unlabelled text files", "[render][text][A-17]")
{
    // text-a4, a2-text-page and pages-900 predate the labels. Their oracle is the runs the
    // generator wrote into the content stream, read first-party -- see the file header.
    std::size_t runs = 0;
    for (const UnlabelledFixture& fixture : contentFixtures()) {
        auto opened = openCorpus(fixture.path);
        for (const int page : fixture.pages) {
            const std::vector<Line> generated = contentLines(fixture.path, page);
            INFO(fixture.path << " page " << page);
            REQUIRE_FALSE(generated.empty());
            for (const Line& line : generated) REQUIRE(line.face != Face::Other);
            const PageTextModel model = extract(*opened, page);
            const Claims claims = claim(model, generated);
            const std::vector<std::string> problems = compareText(model, generated, claims);
            for (const std::string& problem : problems) UNSCOPED_INFO(problem);
            CHECK(problems.empty());
            runs += generated.size();
        }
    }
    std::printf("A-17 content runs: %zu, every one extracted as written\n", runs);
    CHECK(runs > 200);

    SECTION("THE CONTROL: a run the generator did not write is reported as invented")
    {
        // Rule 5. Drop one run from the oracle, and its characters must surface as unclaimed --
        // otherwise the comparison above could not have noticed an invented line either.
        auto opened = openCorpus("ordinary/text-a4.pdf");
        std::vector<Line> generated = contentLines("ordinary/text-a4.pdf", 0);
        generated.erase(generated.begin() + 3);
        const PageTextModel model = extract(*opened, 0);
        const Claims claims = claim(model, generated);
        CHECK_FALSE(claims.unclaimed.empty());
    }
}

// =============================================================================================
// A-18 -- every character's box contains its rendered ink
// =============================================================================================

namespace {

struct InkTally {
    std::size_t checked = 0;       // characters whose ink the oracle found
    std::size_t noInk = 0;         // non-space characters whose window held no dark pixel
    std::size_t outside = 0;       // ink found and NOT inside the model's box
    std::vector<std::string> examples;
};

// Per-character for Helvetica lines (the generator's per-glyph position is known); per-LINE for
// every other line, whose glyph advances the label does not carry. `dx` offsets the MODEL's boxes,
// which is how the control below proves the check can fail.
void checkInk(Opened& opened, int pageIndex, const std::vector<Line>& lines, double dx,
              InkTally& tally)
{
    const PageTextModel model = extract(opened, pageIndex);
    const Claims claims = claim(model, lines);
    const core::RasterImage image = render(opened, pageIndex);
    const PdfRect crop = opened.page(pageIndex).cropBox();

    for (std::size_t l = 0; l < lines.size(); ++l) {
        const Line& line = lines[l];
        const std::vector<std::size_t>& owned = claims.perLine[l];
        if (owned.empty()) continue;

        if (line.face != Face::Other && owned.size() == static_cast<std::size_t>(line.text.size())) {
            const std::vector<double> advances =
                helveticaAdvances(line.text, line.size, line.face == Face::HelveticaBold);
            double x = line.x0;
            for (int k = 0; k < line.text.size(); ++k) {
                const double w = advances[static_cast<std::size_t>(k)];
                const QChar ch = line.text.at(k);
                DeviceBox window = oracleBox(crop, 0, x, line.baseline - 0.25 * line.size, x + w,
                                             line.baseline + 0.80 * line.size);
                window.left += 1.0;    // a neighbour's antialiasing fringe is not this glyph's ink
                window.right -= 1.0;
                x += w;
                if (ch == QLatin1Char(' ') || window.right <= window.left) continue;
                const PixelBox ink = inkIn(image, window);
                if (ink.empty()) {
                    ++tally.noInk;
                    continue;
                }
                ++tally.checked;
                const TextChar& c = model.chars()[owned[static_cast<std::size_t>(k)]];
                if (!containsWithin(shifted(c.box, dx, crop, 0), ink, 1.0)) {
                    ++tally.outside;
                    if (tally.examples.size() < 8) {
                        tally.examples.push_back(opened.file + " p" + std::to_string(pageIndex) + " '"
                                                 + QString(ch).toStdString() + "' in '"
                                                 + line.text.left(24).toStdString() + "'");
                    }
                }
            }
            continue;
        }

        // Per line: the ink inside the generator's line box must lie inside the union of the
        // model's boxes for the characters it claimed.
        DeviceBox window = line.direction == Direction::Ttb
                               ? oracleBox(crop, 0, line.box[0] - 1, line.box[1] - 1, line.box[2] + 1,
                                           line.box[3] + 1)
                               : oracleBox(crop, 0, line.x0, line.baseline - 0.3 * line.size,
                                           line.x1, line.baseline + 1.0 * line.size);
        const PixelBox ink = inkIn(image, window);
        if (ink.empty()) {
            ++tally.noInk;
            continue;
        }
        ++tally.checked;
        DeviceBox united{1e9, 1e9, -1e9, -1e9};
        for (const std::size_t i : owned) {
            const DeviceBox b = shifted(model.chars()[i].box, dx, crop, 0);
            united.left = std::min(united.left, b.left);
            united.top = std::min(united.top, b.top);
            united.right = std::max(united.right, b.right);
            united.bottom = std::max(united.bottom, b.bottom);
        }
        if (!containsWithin(united, ink, 1.0)) {
            ++tally.outside;
            if (tally.examples.size() < 8)
                tally.examples.push_back(opened.file + " p" + std::to_string(pageIndex) + " line '"
                                         + line.text.left(24).toStdString() + "'");
        }
    }
}

void inkOverCorpus(double dx, InkTally& tally)
{
    for (const LabelledFixture& fixture : labelledFixtures()) {
        const QJsonObject label = loadLabel(fixture.name);
        auto opened = openCorpus(std::string("ordinary/") + fixture.name + ".pdf");
        for (const int page : fixture.pages) checkInk(*opened, page, labelLines(label, page), dx, tally);
    }
    for (const UnlabelledFixture& fixture : contentFixtures()) {
        auto opened = openCorpus(fixture.path);
        for (const int page : fixture.pages)
            checkInk(*opened, page, contentLines(fixture.path, page), dx, tally);
    }
}

} // namespace

TEST_CASE("every character's box contains its rendered ink", "[render][text][A-18]")
{
    InkTally tally;
    inkOverCorpus(0.0, tally);
    for (const std::string& example : tally.examples) UNSCOPED_INFO(example);
    std::printf("A-18: %zu glyphs and lines checked against their ink at 144 dpi, %zu outside the "
                "model's box, %zu with no ink the oracle could find\n",
                tally.checked, tally.outside, tally.noInk);
    CHECK(tally.checked > 10000);
    CHECK(tally.outside == 0);
    // The ORACLE's coverage, stated rather than hidden: a thin glyph ('.', 'i', 'l') whose ink is
    // all antialiasing can hold no pixel dark enough inside a window shrunk by a pixel each side.
    CHECK(tally.noInk * 100 <= tally.checked);

    SECTION("THE CONTROL: the same model moved 2 pt to the right fails")
    {
        InkTally moved;
        inkOverCorpus(2.0, moved);
        std::printf("A-18 control: %zu of %zu outside when every box is moved 2 pt\n", moved.outside,
                    moved.checked);
        CHECK(moved.outside * 10 >= moved.checked * 9);
    }
}

// =============================================================================================
// A-19 -- font identity, size and weight
// =============================================================================================

namespace {

struct ExpectedFont {
    const char* name;
    int weight;     // -1: the engine could not say (a base-14 font)
    bool bold;
    bool serif;     // the descriptor's serif bit, where the generator set one
};

void checkFonts(std::string_view path, int pageIndex, const std::map<std::string, ExpectedFont>& table)
{
    auto opened = openCorpus(path);
    const PageTextModel model = extract(*opened, pageIndex);
    const std::vector<Run> runs = contentRuns(path, pageIndex);
    std::size_t checked = 0;
    for (std::size_t r = 0; r < runs.size(); ++r) {
        const Run& run = runs[r];
        const auto expected = table.find(run.fontKey);
        INFO(path << " run " << r << " /" << run.fontKey);
        REQUIRE(expected != table.end());
        // The run's characters: its baseline, from its origin up to the next run on that baseline.
        double end = 1e9;
        for (const Run& other : runs)
            if (&other != &run && std::abs(other.y - run.y) < 0.5 && other.x > run.x) end = std::min(end, other.x);
        for (const TextChar& c : model.chars()) {
            if (c.isGenerated() || std::abs(c.origin.y - run.y) > 0.5 || c.origin.x < run.x - 0.05
                || c.origin.x >= end - 0.05)
                continue;
            const core::text::FontIdentity* font = model.fontOf(c);
            REQUIRE(font != nullptr);
            CHECK(font->name == QLatin1String(expected->second.name));
            CHECK(font->weight == expected->second.weight);
            CHECK(font->isBold() == expected->second.bold);
            CHECK(((font->flags & 2) != 0) == expected->second.serif);
            CHECK(std::abs(c.fontSize - run.size) < 0.01);
            ++checked;
        }
    }
    CHECK(checked > 0);
}

} // namespace

TEST_CASE("extraction reads each character's font name size and weight", "[render][text][A-19]")
{
    const ExpectedFont helvetica{"Helvetica", -1, false, false};
    const ExpectedFont helveticaBold{"Helvetica-Bold", -1, true, false};
    // Weight 400 is the engine's reading of the generator's /StemV 80 -- 80 x 5 -- for every
    // embedded or named font here; none is bold, and only the absent SERIF face sets the serif bit.
    checkFonts("ordinary/latin-embedded.pdf", 0,
               {{"F1", helvetica}, {"F2", helveticaBold},
                {"F3", {"RawPageCorpusLatin", 400, false, false}}});
    checkFonts("ordinary/font-not-embedded.pdf", 0,
               {{"F1", helvetica}, {"F2", helveticaBold},
                {"F3", {"RAWPGA+RawPageAbsentSerif", 400, false, true}},
                {"F4", {"RAWPGB+RawPageAbsentSans", 400, false, false}}});
    // text-tounicode's /F3 is base-14 Helvetica with a /Differences and a /ToUnicode; /F4 the
    // embedded Latin subset.
    checkFonts("ordinary/text-tounicode.pdf", 0,
               {{"F1", helvetica}, {"F2", helveticaBold}, {"F3", helvetica},
                {"F4", {"RawPageCorpusLatin", 400, false, false}}});
}

// =============================================================================================
// A-20 -- generated whitespace and soft hyphens
// =============================================================================================

TEST_CASE("extraction flags line-end hyphens and generated whitespace", "[render][text][A-20]")
{
    auto opened = openCorpus("ordinary/hyphenated.pdf");
    const PageTextModel model = extract(*opened, 0);
    const std::vector<Line> lines = labelLines(loadLabel("hyphenated"), 0);
    const Claims claims = claim(model, lines);

    // The label's `hyphenation` notes name lines 0, 3 and 5 of the body -- label line ids 3, 6 and 8,
    // after the header, the footer and the caption. Each ends in a hyphen the engine must flag.
    std::vector<std::size_t> flagged;
    for (std::size_t i = 0; i < model.chars().size(); ++i)
        if (model.chars()[i].isHyphen()) flagged.push_back(i);
    REQUIRE(flagged.size() == 3);
    for (const std::size_t i : flagged) {
        // U+0002 is what the engine reports; the model stores the hyphen the content showed.
        CHECK(model.chars()[i].codepoint == U'-');
        bool endsALine = false;
        for (std::size_t l = 0; l < lines.size(); ++l)
            if (!claims.perLine[l].empty() && claims.perLine[l].back() == i
                && lines[l].text.endsWith(QLatin1Char('-')))
                endsALine = true;
        CHECK(endsALine);
    }

    // THE CONTROL (rule 5): the mid-line hyphen of `well-known` is a real compound, not flagged.
    const QString compound = QStringLiteral("well-known");
    bool foundCompound = false;
    for (std::size_t l = 0; l < lines.size(); ++l) {
        const qsizetype at = lines[l].text.indexOf(compound);
        if (at < 0) continue;
        foundCompound = true;
        const std::size_t hyphen = claims.perLine[l][static_cast<std::size_t>(at + 4)];
        CHECK(model.chars()[hyphen].codepoint == U'-');
        CHECK_FALSE(model.chars()[hyphen].isHyphen());
    }
    CHECK(foundCompound);

    // Generated characters are whitespace and nothing else, and every shown space is one the
    // content drew -- A-17's claim, restated for the flag this case is about.
    std::size_t generatedCount = 0;
    for (const TextChar& c : model.chars()) {
        if (!c.isGenerated()) continue;
        ++generatedCount;
        CHECK((c.codepoint == U' ' || c.codepoint == U'\r' || c.codepoint == U'\n'));
    }
    CHECK(generatedCount > 0);
    std::size_t shownSpaces = 0;
    std::size_t generatorSpaces = 0;
    for (const TextChar& c : model.chars())
        if (!c.isGenerated() && c.codepoint == U' ') ++shownSpaces;
    for (const Line& line : lines) generatorSpaces += static_cast<std::size_t>(line.text.count(QLatin1Char(' ')));
    CHECK(shownSpaces == generatorSpaces);
}

// =============================================================================================
// A-21 -- unmappable text is REPORTED, not extracted
// =============================================================================================

TEST_CASE("extraction reports cjk-embedded's hanzi as unmapped and invents nothing", "[render][text][A-21]")
{
    // `cjk-embedded.pdf` draws nine hanzi from an embedded subset through Identity-H with NO
    // /ToUnicode: 9 + 9 + 27 + 45 = 90 glyphs. The engine's guess for each is its glyph id.
    auto opened = openCorpus("ordinary/cjk-embedded.pdf");
    const PageTextModel model = extract(*opened, 0);
    std::size_t cjk = 0;
    for (const TextChar& c : model.chars()) {
        const core::text::FontIdentity* font = model.fontOf(c);
        if (font == nullptr || font->name != QLatin1String("RawPageCorpusCJK")) {
            // The Helvetica captions on the same page ARE text, and must stay text.
            CHECK_FALSE(c.isUnmapped());
            continue;
        }
        ++cjk;
        CHECK(c.isUnmapped());
        CHECK(c.codepoint == core::text::kReplacementCharacter);
    }
    CHECK(cjk == 90);
    CHECK(model.unmappedCount() == 90);
    // The page still has extractable text -- its two Helvetica lines. What a consumer does with
    // the 90 is T5's and T6's: this is the extractor's half of A-21.
    CHECK(model.hasExtractableText());

    SECTION("THE CONTROL: the same glyphs WITH /ToUnicode extract as the right hanzi")
    {
        auto mapped = openCorpus("ordinary/text-tounicode.pdf");
        const PageTextModel page = extract(*mapped, 1);
        CHECK(page.unmappedCount() == 0);
        const QString shown = page.text(PageTextModel::Whitespace::ShownOnly);
        CHECK(shown.contains(QStringLiteral("一二三十口日田中目")));
        CHECK(shown.contains(QStringLiteral("十日中日口")));
    }
}

// =============================================================================================
// A-22 -- coordinates are in page space, whatever the rotation
// =============================================================================================

TEST_CASE("extracted boxes are in unrotated page space and land on the ink at every rotation",
          "[render][text][A-22]")
{
    // (a) PAGE SPACE. `rotated-pages.pdf` is one content stream under /Rotate 0, 90, 180 and 270, so
    // its four models must hold the same characters in the same boxes: a box that moved with /Rotate
    // would be in device space.
    //
    // As a SET, not a sequence -- MEASURED 2026-09-30: the engine returns a rotated page's characters
    // in a DIFFERENT ORDER (its line grouping follows the displayed orientation), while every box
    // stays in user space. Which order a reader reads is `LayoutAnalyzer`'s (T4), so the order is
    // reported here and the boxes are asserted.
    const auto shownSet = [](const PageTextModel& model) {
        std::vector<std::array<long long, 5>> out;
        for (const TextChar& c : model.chars()) {
            if (c.isGenerated()) continue;
            out.push_back({static_cast<long long>(c.codepoint), std::llround(c.box.left() * 100),
                           std::llround(c.box.bottom() * 100), std::llround(c.box.right() * 100),
                           std::llround(c.box.top() * 100)});
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    auto rotated = openCorpus("ordinary/rotated-pages.pdf");
    REQUIRE(rotated->pageCount() == 4);
    const PageTextModel first = extract(*rotated, 0);
    REQUIRE(first.shownCount() > 50);
    for (int page = 1; page < 4; ++page) {
        const PageTextModel other = extract(*rotated, page);
        INFO("page " << page);
        CHECK(shownSet(other) == shownSet(first));
        const bool sameOrder = other.text(PageTextModel::Whitespace::ShownOnly)
                               == first.text(PageTextModel::Whitespace::ShownOnly);
        std::printf("A-22: /Rotate %d -- the same %zu characters in the same boxes, in %s order\n",
                    page * 90, other.shownCount(), sameOrder ? "the same" : "a DIFFERENT");
    }

    // (b) THE HIGHLIGHT LANDS ON THE INK. Each character's model box through the PRODUCT's mapping
    // (`PagePresentation`'s layout, what a selection highlight will be drawn through) must contain
    // the ink the oracle finds through the TEST's own rotation, for every combination of the page's
    // /Rotate and the view rotation.
    std::size_t checked = 0;
    std::size_t outside = 0;
    std::size_t wrongRotationOutside = 0;
    std::size_t underStroke = 0;
    for (const char* path : {"ordinary/rotated-pages.pdf", "ordinary/rotate-inherited.pdf"}) {
        auto opened = openCorpus(path);
        for (int page = 0; page < opened->pageCount(); ++page) {
            const PageTextModel model = extract(*opened, page);
            const std::vector<Line> lines = contentLines(path, page);
            // Both fixtures draw an ARROW through their second line -- a 2 pt stroke at x = 300 from
            // y = 640 to 740 on `rotated-pages.pdf` -- across the `p` of "points". Its ink belongs to
            // no character, so a glyph whose window touches a stroke the generator drew is left out
            // of the oracle (found at T2: 16 misses, every one that `p`, before this rule).
            std::vector<PdfRect> strokes;
            contentRuns(path, page, &strokes);
            const Claims claims = claim(model, lines);
            const PdfRect crop = opened->page(page).cropBox();
            const int own = static_cast<int>(opened->page(page).rotation());
            for (const PageRotation view : {PageRotation::None, PageRotation::Clockwise90,
                                            PageRotation::Half, PageRotation::Clockwise270}) {
                const core::RasterImage image = render(*opened, page, view);
                core::render::Resolution resolution;
                resolution.dpi = 72.0 * kScale;
                const core::render::PageLayout layout = core::render::PagePresentation{}.layout(
                    *opened->backend.pages(opened->handle), core::render::LayoutMode::SingleContinuous,
                    resolution, page, view);
                const core::render::PageSlot* slot = layout.slotForPage(page);
                REQUIRE(slot != nullptr);
                const auto productBox = [&](const PdfRect& r, const core::render::PageLayout& with,
                                            const core::render::PageSlot& at) {
                    const auto a = with.toContent(page, PdfPoint{r.left(), r.bottom()});
                    const auto b = with.toContent(page, PdfPoint{r.right(), r.top()});
                    return DeviceBox{std::min(a->x, b->x) - at.rect.left(), std::min(a->y, b->y) - at.rect.top(),
                                     std::max(a->x, b->x) - at.rect.left(), std::max(a->y, b->y) - at.rect.top()};
                };
                // The CONTROL's mapping: the same layout a quarter-turn off.
                const PageRotation wrong = core::rotateBy(view, 1);
                const core::render::PageLayout wrongLayout = core::render::PagePresentation{}.layout(
                    *opened->backend.pages(opened->handle), core::render::LayoutMode::SingleContinuous,
                    resolution, page, wrong);
                const core::render::PageSlot* wrongSlot = wrongLayout.slotForPage(page);
                REQUIRE(wrongSlot != nullptr);

                for (std::size_t l = 0; l < lines.size(); ++l) {
                    const Line& line = lines[l];
                    if (line.face == Face::Other || claims.perLine[l].size() != static_cast<std::size_t>(line.text.size()))
                        continue;
                    const std::vector<double> advances =
                        helveticaAdvances(line.text, line.size, line.face == Face::HelveticaBold);
                    double x = line.x0;
                    for (int k = 0; k < line.text.size(); ++k) {
                        const double w = advances[static_cast<std::size_t>(k)];
                        const double x0 = x;
                        x += w;
                        if (line.text.at(k) == QLatin1Char(' ')) continue;
                        const PdfRect glyph{PdfPoint{x0, line.baseline - 0.25 * line.size},
                                            PdfSize{w, 1.05 * line.size}};
                        if (std::any_of(strokes.begin(), strokes.end(), [&](const PdfRect& stroke) {
                                return stroke.left() < glyph.right() && glyph.left() < stroke.right()
                                       && stroke.bottom() < glyph.top() && glyph.bottom() < stroke.top();
                            })) {
                            ++underStroke;
                            continue;
                        }
                        // The window shrunk by 0.5 pt along the advance, in PAGE space, so it stays
                        // off the neighbours whichever way the page is turned.
                        const DeviceBox window =
                            oracleBox(crop, own + static_cast<int>(view), x0 + 0.5,
                                      line.baseline - 0.25 * line.size, x0 + w - 0.5,
                                      line.baseline + 0.80 * line.size);
                        const PixelBox ink = inkIn(image, window);
                        if (ink.empty()) continue;
                        ++checked;
                        const PdfRect& box = model.chars()[claims.perLine[l][static_cast<std::size_t>(k)]].box;
                        if (!containsWithin(productBox(box, layout, *slot), ink, 1.0)) {
                            ++outside;
                            const DeviceBox p = productBox(box, layout, *slot);
                            std::printf("  MISS %s p%d own%d view%d '%c' in '%s': ink [%d %d %d %d] "
                                        "box [%.1f %.1f %.1f %.1f] window [%.1f %.1f %.1f %.1f]\n",
                                        path, page, own, static_cast<int>(view),
                                        line.text.at(k).toLatin1(), line.text.left(20).toLatin1().constData(),
                                        ink.x0, ink.y0, ink.x1, ink.y1, p.left, p.top, p.right, p.bottom,
                                        window.left, window.top, window.right, window.bottom);
                        }
                        if (!containsWithin(productBox(box, wrongLayout, *wrongSlot), ink, 1.0))
                            ++wrongRotationOutside;
                    }
                }
            }
        }
    }
    std::printf("A-22: %zu glyphs over 7 pages x 4 view rotations, %zu outside, %zu left out under a "
                "stroke; the control (a quarter-turn off) put %zu outside\n",
                checked, outside, underStroke, wrongRotationOutside);
    CHECK(checked > 1000);
    CHECK(outside == 0);
    CHECK(wrongRotationOutside * 10 >= checked * 9);
}

// =============================================================================================
// A-23 -- right-to-left order, MEASURED
// =============================================================================================

TEST_CASE("right-to-left order as the engine returns it is measured on both emissions",
          "[render][text][A-23]")
{
    // The measurement D5-6 asks for, asserted so that a PDFium upgrade that changes it fails here
    // rather than quietly changing what `LayoutAnalyzer` (T4) receives.
    //
    // MEASURED 2026-09-30: NEITHER logical NOR visual. Each Hebrew WORD's letters come back in
    // logical order, and the WORDS come back in left-to-right page order -- and identically for
    // the page whose content stream emits visual order and the page that emits logical order. So
    // the model does NOT hold logical order, and the analyzer reorders (D5-6's second branch).
    const QJsonObject label = loadLabel("rtl-text");
    auto opened = openCorpus("ordinary/rtl-text.pdf");
    for (const int page : {0, 1}) {
        const std::vector<Line> lines = labelLines(label, page);
        const PageTextModel model = extract(*opened, page);
        const Claims claims = claim(model, lines);
        std::size_t hebrew = 0;
        for (std::size_t l = 0; l < lines.size(); ++l) {
            if (lines[l].direction != Direction::Rtl) continue;
            ++hebrew;
            const QString logical = lines[l].text;
            // Words in left-to-right PAGE order, each in logical order: the logical words reversed.
            QStringList words = logical.split(QLatin1Char(' '));
            std::reverse(words.begin(), words.end());
            const QString engineOrder = words.join(QLatin1Char(' '));
            const QString got = textOf(model, claims.perLine[l]);
            INFO("page " << page << " line " << logical.toStdString());
            CHECK(got == engineOrder);
            // And it is NOT the logical order -- the analyzer's job is real.
            CHECK(got != logical);
        }
        CHECK(hebrew == 4);
    }
}

// =============================================================================================
// A-24 -- extraction happens on the engine thread and nowhere else
// =============================================================================================

namespace {

class RecordingExtractor final : public core::ITextExtractor {
public:
    RecordingExtractor(core::IDocumentBackend& backend, std::mutex& mutex,
                       std::set<std::thread::id>& threads, int& calls)
        : inner_(backend), mutex_(mutex), threads_(threads), calls_(calls)
    {
    }

    core::Status extractPage(DocumentHandle document, int pageIndex, PageTextModel& into) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            threads_.insert(std::this_thread::get_id());
            ++calls_;
        }
        return inner_.extractPage(document, pageIndex, into);
    }

private:
    app::BackendTextExtractor inner_;
    std::mutex& mutex_;
    std::set<std::thread::id>& threads_;
    int& calls_;
};

} // namespace

TEST_CASE("extraction through the shipped scheduler runs on its worker and delivers here",
          "[render][text][A-24]")
{
    // The ENGINE half of A-24: the real backend, the shipped `app/BackendTextExtractor`, the real
    // scheduler -- the assembly `app/CompositionRoot` builds -- with the adapter wrapped so the
    // thread of every call is recorded.
    pdf::DocumentBackendImpl backend;
    auto text = backend.open(corpusPath("ordinary/columns-two.pdf"), nullptr);
    REQUIRE(text);
    std::mutex mutex;
    std::set<std::thread::id> threads;
    int calls = 0;
    core::render::RenderScheduler scheduler(
        [&backend] { return std::unique_ptr<core::IPageRasterizer>(new app::BackendRasterizer(backend)); },
        [&] {
            return std::unique_ptr<core::ITextExtractor>(
                new RecordingExtractor(backend, mutex, threads, calls));
        },
        nullptr, core::render::RenderScheduler::Config{});

    std::map<int, QString> delivered;
    std::set<std::thread::id> deliveringThreads;
    scheduler.subscribeText([&](const core::render::TextRequest& request,
                                const core::render::TextResult& result) {
        deliveringThreads.insert(std::this_thread::get_id());
        REQUIRE(result);
        delivered[request.pageIndex] = result.value()->text(PageTextModel::Whitespace::ShownOnly);
    });

    // An index build's shape -- every page at Background -- and a selection's, one page at Visible.
    scheduler.enqueueText({text.value(), 0}, core::render::RenderPriority::Background);
    scheduler.enqueueText({text.value(), 1}, core::render::RenderPriority::Background);
    scheduler.enqueueText({text.value(), 2}, core::render::RenderPriority::Visible);
    REQUIRE(scheduler.waitForIdle(10000));
    CHECK(scheduler.deliverReady() == 3);

    CHECK(calls == 3);
    REQUIRE(threads.size() == 1);
    CHECK(*threads.begin() != std::this_thread::get_id());
    REQUIRE(deliveringThreads.size() == 1);
    CHECK(*deliveringThreads.begin() == std::this_thread::get_id());
    const auto stats = scheduler.stats();
    CHECK(stats.enqueuedBackground == 2);
    CHECK(stats.enqueuedVisible == 1);
    CHECK(stats.textDelivered == 3);

    // What came through the thread is what a direct call extracts -- the job changes WHERE, not what.
    REQUIRE(delivered.size() == 3);
    for (const auto& [page, shown] : delivered) {
        PageTextModel direct;
        REQUIRE(backend.textExtractor(text.value())->extractPage(text.value(), page, direct));
        CHECK(shown == direct.text(PageTextModel::Whitespace::ShownOnly));
    }
}

// =============================================================================================
// A-25 -- the backend implements ITextExtractor
// =============================================================================================

TEST_CASE("the backend implements ITextExtractor and refuses a foreign handle", "[render][text][A-25]")
{
    pdf::DocumentBackendImpl backend;
    CHECK(backend.implementedInterfaces()[static_cast<std::size_t>(core::BackendInterface::TextExtractor)]);
    const auto missing = core::unimplementedBackendInterfaces(backend);
    REQUIRE(missing.size() == 3);
    CHECK(missing[0] == "ISigner");
    CHECK(missing[1] == "IOcrEngine");
    CHECK(missing[2] == "IIndexer");

    auto a = backend.open(corpusPath("ordinary/text-a4.pdf"), nullptr);
    auto b = backend.open(corpusPath("ordinary/hyphenated.pdf"), nullptr);
    REQUIRE(a);
    REQUIRE(b);
    core::ITextExtractor* forA = backend.textExtractor(a.value());
    REQUIRE(forA != nullptr);
    CHECK(forA != backend.textExtractor(b.value()));

    // A FOREIGN HANDLE is refused, and the model is left exactly as it was -- an extractor that
    // answered with its own document's text would be a wrong answer with no error.
    PageTextModel untouched(42);
    TextChar marker;
    marker.codepoint = U'Z';
    untouched.append(marker);
    const core::Status foreign = forA->extractPage(b.value(), 0, untouched);
    REQUIRE_FALSE(foreign);
    CHECK(foreign.error().code == core::ErrorCode::InvalidArgument);
    CHECK(untouched.size() == 1);
    CHECK(untouched.pageIndex() == 42);

    // A page outside the TREE is refused the same way.
    const core::Status outside = forA->extractPage(a.value(), 999, untouched);
    REQUIRE_FALSE(outside);
    CHECK(outside.error().code == core::ErrorCode::InvalidArgument);
    CHECK(outside.error().messageKey == QLatin1String("error.render.noSuchPage"));
    CHECK(untouched.size() == 1);

    // And a handle this backend never opened, or has closed, gets no extractor at all.
    CHECK(backend.textExtractor(DocumentHandle{9999}) == nullptr);
    REQUIRE(backend.close(b.value()));
    CHECK(backend.textExtractor(b.value()) == nullptr);

    SECTION("THE CONTROL: the extractor answers for its own handle")
    {
        PageTextModel model;
        CHECK(forA->extractPage(a.value(), 0, model));
        CHECK(model.shownCount() > 0);
    }
}

// =============================================================================================
// A-26 -- the whole corpus: no crash, a bad page fails alone, nothing invented
// =============================================================================================

TEST_CASE("extraction over the whole corpus fails a page alone and never invents a control character",
          "[render][text][A-26]")
{
    std::size_t files = 0, refused = 0, pages = 0, failedPages = 0, characters = 0, unmapped = 0;
    std::size_t controls = 0, nonFinite = 0;
    std::vector<std::string> failures;
    const core::PasswordCallback password = [](int attempt) -> std::optional<QString> {
        // The encrypted corpus's public user password (`corpus/PROVENANCE.md`).
        if (attempt == 1) return QStringLiteral("user");
        return std::nullopt;
    };
    for (const auto& entry : std::filesystem::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        ++files;
        pdf::DocumentBackendImpl backend;
        auto handle = backend.open(QString::fromStdString(entry.path().string()), password);
        if (!handle) {
            ++refused;   // opening is not this case's subject: Stage 2's A-42 owns which files open
            continue;
        }
        core::ITextExtractor* extractor = backend.textExtractor(handle.value());
        REQUIRE(extractor != nullptr);
        const int count = backend.pages(handle.value())->count();
        for (int page = 0; page < count; ++page) {
            ++pages;
            PageTextModel model;
            const core::Status status = extractor->extractPage(handle.value(), page, model);
            if (!status) {
                // A page the engine cannot read is reported FOR THAT PAGE, by name and index, and
                // the loop goes on to its neighbours.
                ++failedPages;
                CHECK(status.error().messageKey == QLatin1String("error.text.extractFailed"));
                CHECK(status.error().context == QString::number(page));
                failures.push_back(entry.path().filename().string() + " p" + std::to_string(page));
                continue;
            }
            for (const TextChar& c : model.chars()) {
                if (!std::isfinite(c.box.left()) || !std::isfinite(c.box.top())) ++nonFinite;
                if (c.isGenerated()) continue;
                ++characters;
                if (c.isUnmapped()) ++unmapped;
                // U+0002 is what the engine gives a line-end hyphen (A-20). No SHOWN character may
                // be a C0 control: none is in any document here.
                if (c.codepoint < 0x20) ++controls;
            }
        }
    }
    for (const std::string& failure : failures) UNSCOPED_INFO("page failed alone: " << failure);
    std::printf("A-26: %zu files, %zu refused at open, %zu pages extracted (%zu failed alone), "
                "%zu shown characters, %zu unmapped, %zu control characters\n",
                files, refused, pages, failedPages, characters, unmapped, controls);
    CHECK(files >= 97);
    CHECK(pages > 2000);
    CHECK(controls == 0);
    CHECK(nonFinite == 0);
}
