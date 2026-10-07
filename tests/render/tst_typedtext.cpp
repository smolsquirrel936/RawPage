// tst_typedtext (render) -- Stage 6 T7: the claims typed text rests on, held to the ENGINE
// (`Stage6-Annotations-Testing-Guide.md` §5.9, A-56; D6-7, D6-34).
//
// `core/doc/TypedText` makes two claims a headless case can only state:
//
//   * **Every character the coverage table lists can be shown in its collection's face.** The table is built
//     from each collection's legacy character set (`tools/cjk-coverage/make-cjk-coverage.py`) -- a PROXY.
//     Here every one of them is written, in the font objects the appearance builder writes
//     (`AppearanceStreamBuilder::fontObject`) and the codes it writes (`encodeText`), onto pages opened by
//     the SHIPPED backend with the product's font hook installed; the engine must read each back AS THE SAME
//     CHARACTER, in order, and draw ink inside its box. A character that comes back as another, or as
//     nothing, or draws nothing, is a claim the table may not make -- narrowed in the generator, never here.
//   * **Helvetica's advances are the AFM's, and every reader sets typed text on them.** Each WinAnsi code is
//     written ten times, followed by a marker; where the engine puts the marker gives the advance. In a
//     Helvetica with NO `/Widths` the engine sets it on ITS OWN metrics -- Foxit Sans, PDFium's stand-in --
//     which agree with the table on every code but FOUR, where Foxit Sans is not Helvetica (the euro, `‹`,
//     `«`, `»`: Helvetica's AFM and Arial, measured at T7, both say what the table says). That set is
//     asserted EXACTLY, so a fifth would show. In the font the appearance builder writes, WITH its `/Widths`,
//     the engine sets every code on the table's advance -- which is what makes a line wrap the same in it.
//
// WHAT IT DOES NOT COVER (convention 19): Acrobat and Firefox, which draw a non-embedded CJK font with their own
// faces (M-06); whether a glyph drawn is the RIGHT glyph rather than some ink -- a missing glyph drawn as a
// box would pass the ink half, and only the read-back half, which names the CID's character, closes that.

#include <catch2/catch_test_macros.hpp>

#include "core/backend.h"
#include "core/doc/PageTree.h"
#include "core/doc/TypedText.h"
#include "core/render/RenderRequest.h"
#include "core/text/PageTextModel.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/render/FontResolverBridge.h"
#include "platform/win/WinFontResolver.h"

#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace rawpage;
using core::TypedFont;
using pdf::cos::CosObject;

namespace {

// The product's assembly: the font hook installed, as `app/CompositionRoot` installs it.
struct FontBridge {
    FontBridge() { REQUIRE(pdf::render::FontResolverBridge::install(resolver)); }
    ~FontBridge() { pdf::render::FontResolverBridge::uninstall(); }
    platform::win::WinFontResolver resolver;
};

// A PDF of `pages` content streams over the objects `objects` collected -- numbered from 3, after the
// catalog and the page tree -- each page's /Resources naming `fonts`.
std::string pdfOf(const pdf::ops::CollectedAppearanceObjects& objects, const std::string& fonts,
                  const std::vector<std::string>& pages)
{
    std::vector<std::string> bodies;
    bodies.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    bodies.push_back(std::string());   // the page tree, written when the pages are numbered
    for (const pdf::ops::CollectedAppearanceObjects::Entry& entry : objects.entries()) {
        REQUIRE(entry.at.number == static_cast<long long>(bodies.size()) + 1);
        bodies.push_back(pdf::cos::serializeObject(entry.object));
    }
    std::string kids;
    for (const std::string& content : pages) {
        const int stream = static_cast<int>(bodies.size()) + 1;
        bodies.push_back("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream");
        const int page = static_cast<int>(bodies.size()) + 1;
        bodies.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents " + std::to_string(stream)
                         + " 0 R /Resources << /Font << " + fonts + " >> >> >>");
        kids += std::to_string(page) + " 0 R ";
    }
    bodies[1] = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages.size()) + " >>";

    std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + bodies[i] + "\nendobj\n";
    }
    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(bodies.size() + 1) + "\n0000000000 65535 f \n";
    for (const std::size_t offset : offsets) {
        char line[24];
        std::snprintf(line, sizeof line, "%010zu 00000 n \n", offset);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(bodies.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
           + std::to_string(xref) + "\n%%EOF\n";
    return out;
}

std::string hex(const std::string& bytes)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : bytes) {
        out += kHex[static_cast<unsigned char>(c) >> 4];
        out += kHex[static_cast<unsigned char>(c) & 0x0F];
    }
    return out;
}

struct Opened {
    QTemporaryDir directory;
    pdf::DocumentBackendImpl backend;
    core::DocumentHandle handle;
};

std::unique_ptr<Opened> openBytes(const std::string& bytes, const char* name)
{
    auto opened = std::make_unique<Opened>();
    REQUIRE(opened->directory.isValid());
    const QString path = opened->directory.filePath(QString::fromLatin1(name));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()));
    file.close();
    auto handle = opened->backend.open(path, nullptr);
    INFO(name);
    REQUIRE(handle);
    opened->handle = handle.value();
    return opened;
}

core::text::PageTextModel extract(Opened& opened, int page)
{
    core::ITextExtractor* extractor = opened.backend.textExtractor(opened.handle);
    REQUIRE(extractor != nullptr);
    core::text::PageTextModel model;
    REQUIRE(extractor->extractPage(opened.handle, page, model));
    return model;
}

core::RasterImage render(Opened& opened, int page, double scale)
{
    const core::Page* entry = opened.backend.pages(opened.handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfSize size = entry->displaySize();
    core::render::RenderRequest request;
    request.document = opened.handle;
    request.pageIndex = page;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::Annotations;
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(size.width * scale)),
                                          static_cast<int>(std::ceil(size.height * scale))};
    core::IPageRasterizer* rasterizer = opened.backend.rasterizer(opened.handle);
    REQUIRE(rasterizer != nullptr);
    auto image = rasterizer->raster(request);
    REQUIRE(image);
    return image.value();
}

// Pixels with any ink inside a page-space box, on a page rendered at `scale` (the page's origin bottom-left):
// any channel below 224 -- a hairline (a box-drawing stroke, a vertical bar) is antialiased to light grey at
// 24 pixels an em, and a threshold of half intensity read it as no ink (measured at T7: eight such glyphs).
int inkIn(const core::RasterImage& image, const core::PdfRect& box, double scale, double pageHeight)
{
    const int x0 = std::max(0, static_cast<int>(std::floor(box.left() * scale)));
    const int x1 = std::min(image.width, static_cast<int>(std::ceil(box.right() * scale)));
    const int y0 = std::max(0, static_cast<int>(std::floor((pageHeight - box.top()) * scale)));
    const int y1 = std::min(image.height, static_cast<int>(std::ceil((pageHeight - box.bottom()) * scale)));
    int dark = 0;
    for (int y = y0; y < y1; ++y) {
        const auto* row = reinterpret_cast<const unsigned char*>(image.pixels.constData()) + static_cast<qsizetype>(y) * image.bytesPerLine;
        for (int x = x0; x < x1; ++x) {
            const unsigned char* p = row + x * 4;
            if (p[0] < 224 || p[1] < 224 || p[2] < 224) ++dark;
        }
    }
    return dark;
}

QString hexOf(char32_t c)
{
    return QStringLiteral("U+%1").arg(static_cast<qulonglong>(c), 4, 16, QLatin1Char('0')).toUpper();
}

} // namespace

TEST_CASE("every character the coverage table claims is read back and drawn by the engine", "[render][typedtext][A-56]")
{
    FontBridge fonts;
    constexpr double kSize = 12.0;
    constexpr int kPerLine = 40;
    constexpr int kLinesPerPage = 50;
    constexpr double kScale = 2.0;
    for (const TypedFont face : {TypedFont::Simplified, TypedFont::Traditional, TypedFont::Japanese, TypedFont::Korean}) {
        const core::TypedFontSpec& spec = core::typedFontSpec(face);
        INFO(spec.baseFont);
        std::vector<char32_t> claimed;
        for (char32_t c = 0x2000; c <= 0xFFFF; ++c) {
            if (core::cjkCovers(face, c)) claimed.push_back(c);
        }
        REQUIRE(claimed.size() > 5000u);

        pdf::ops::CollectedAppearanceObjects objects(3);
        const pdf::cos::CosRef font = pdf::ops::AppearanceStreamBuilder::fontObject(face, objects);
        const std::string fontEntry = std::string("/F ") + std::to_string(font.number) + " 0 R";
        std::vector<std::string> pages;
        std::vector<std::vector<char32_t>> written;
        for (std::size_t at = 0; at < claimed.size();) {
            std::string content = "BT /F " + std::to_string(static_cast<int>(kSize)) + " Tf\n";
            std::vector<char32_t> onPage;
            for (int line = 0; line < kLinesPerPage && at < claimed.size(); ++line) {
                std::string codes;
                for (int k = 0; k < kPerLine && at < claimed.size(); ++k, ++at) {
                    const QString one = QString(QChar(static_cast<char16_t>(claimed[at])));
                    codes += pdf::ops::AppearanceStreamBuilder::encodeText(face, one);
                    onPage.push_back(claimed[at]);
                }
                content += "1 0 0 1 36 " + std::to_string(800 - line * 15) + " Tm <" + hex(codes) + "> Tj\n";
            }
            content += "ET";
            pages.push_back(std::move(content));
            written.push_back(std::move(onPage));
        }
        auto opened = openBytes(pdfOf(objects, fontEntry, pages), "coverage.pdf");
        REQUIRE(opened->backend.pages(opened->handle)->count() == static_cast<int>(pages.size()));

        QStringList misread;
        QStringList inkless;
        std::size_t checked = 0;
        for (std::size_t p = 0; p < pages.size(); ++p) {
            const core::text::PageTextModel model = extract(*opened, static_cast<int>(p));
            // The engine's characters, the spaces and line ends it supplies between lines left out.
            std::vector<const core::text::TextChar*> shown;
            for (const core::text::TextChar& c : model.chars()) {
                if (c.codepoint == U' ' || c.codepoint == U'\r' || c.codepoint == U'\n') continue;
                shown.push_back(&c);
            }
            const core::RasterImage image = render(*opened, static_cast<int>(p), kScale);
            const std::vector<char32_t>& want = written[p];
            for (std::size_t k = 0; k < want.size(); ++k) {
                ++checked;
                if (k >= shown.size() || shown[k]->codepoint != want[k]) {
                    if (misread.size() < 40)
                        misread << hexOf(want[k]) + QStringLiteral("->") + (k < shown.size() ? hexOf(shown[k]->codepoint) : QStringLiteral("none"));
                    else if (misread.size() == 40)
                        misread << QStringLiteral("...");
                    continue;
                }
                // Two characters are BLANK by definition: the ideographic space and the Hangul filler.
                if (want[k] == 0x3000 || want[k] == 0x3164) continue;
                if (inkIn(image, shown[k]->box, kScale, 842.0) == 0) {
                    if (inkless.size() < 40) inkless << hexOf(want[k]);
                }
            }
            if (shown.size() != want.size() && misread.size() < 40)
                misread << QStringLiteral("page %1: %2 shown for %3 written").arg(p + 1).arg(shown.size()).arg(want.size());
        }
        INFO("misread: " << misread.join(QLatin1Char(' ')).toStdString());
        INFO("drew no ink: " << inkless.join(QLatin1Char(' ')).toStdString());
        CHECK(checked == claimed.size());
        CHECK(misread.isEmpty());
        CHECK(inkless.isEmpty());
    }
}

namespace {

// Each WinAnsi code's advance as the engine sets it in the font `objects` holds at object 3 -- or the codes
// it could not measure, in `problems`.
std::map<int, double> engineAdvances(const pdf::ops::CollectedAppearanceObjects& objects, QStringList& problems)
{
    constexpr double kSize = 20.0;
    constexpr int kRepeat = 10;
    std::vector<int> codes;
    for (int code = 32; code <= 255; ++code) {
        if (core::helveticaWidth(static_cast<std::uint8_t>(code)) > 0 && code != '|') codes.push_back(code);
    }
    REQUIRE(codes.size() == 217u);   // 224 codes, six WinAnsi leaves undefined, and the marker itself
    std::vector<std::string> pages;
    for (std::size_t at = 0; at < codes.size();) {
        std::string content = "BT /F 20 Tf\n";
        for (int line = 0; line < 30 && at < codes.size(); ++line, ++at) {
            std::string run(kRepeat, static_cast<char>(codes[at]));
            run += '|';
            content += "1 0 0 1 36 " + std::to_string(800 - line * 25) + " Tm <" + hex(run) + "> Tj\n";
        }
        content += "ET";
        pages.push_back(std::move(content));
    }
    auto opened = openBytes(pdfOf(objects, "/F 3 0 R", pages), "helvetica.pdf");

    std::map<int, double> out;
    std::size_t at = 0;
    for (std::size_t p = 0; p < pages.size(); ++p) {
        const core::text::PageTextModel model = extract(*opened, static_cast<int>(p));
        // Each line's marker, by its baseline.
        std::map<int, double> marker;   // baseline y -> the marker's origin x
        for (const core::text::TextChar& c : model.chars()) {
            if (c.codepoint == U'|') marker[static_cast<int>(std::lround(c.origin.y))] = c.origin.x;
        }
        for (int line = 0; line < 30 && at < codes.size(); ++line, ++at) {
            const auto found = marker.find(800 - line * 25);
            if (found == marker.end()) {
                problems << QStringLiteral("%1: no marker").arg(codes[at]);
                continue;
            }
            out[codes[at]] = (found->second - 36.0) / kRepeat * 1000.0 / kSize;
        }
    }
    if (at != codes.size()) problems << QStringLiteral("only %1 of %2 codes laid out").arg(at).arg(codes.size());
    return out;
}

// The codes whose advance differs from the table by more than half a unit, as `code: engine`.
std::map<int, int> differing(const std::map<int, double>& advances)
{
    std::map<int, int> out;
    for (const auto& [code, advance] : advances) {
        if (std::abs(advance - core::helveticaWidth(static_cast<std::uint8_t>(code))) > 0.5)
            out[code] = static_cast<int>(std::lround(advance));
    }
    return out;
}

} // namespace

TEST_CASE("Helvetica's advances are the AFM's and the engine sets typed text on them", "[render][typedtext][A-56]")
{
    FontBridge fonts;
    // NO /Widths: the engine's own metrics, Foxit Sans -- the AFM's but for four codes (see the header).
    pdf::ops::CollectedAppearanceObjects bare(3);
    (void)bare.shared("helv", CosObject::makeDictionary({
        {"Type", CosObject::makeName("Font")},
        {"Subtype", CosObject::makeName("Type1")},
        {"BaseFont", CosObject::makeName("Helvetica")},
        {"Encoding", CosObject::makeName("WinAnsiEncoding")},
    }));
    QStringList problems;
    const std::map<int, double> own = engineAdvances(bare, problems);
    INFO(problems.join(QStringLiteral("; ")).toStdString());
    REQUIRE(problems.isEmpty());
    REQUIRE(own.size() == 217u);   // 224 codes, six WinAnsi leaves undefined, and the marker itself
    CHECK(differing(own) == std::map<int, int>{{128, 667}, {155, 366}, {171, 612}, {187, 612}});

    // THE FONT THE BUILDER WRITES, its /Widths with it: every code on the table's advance.
    pdf::ops::CollectedAppearanceObjects written(3);
    REQUIRE(pdf::ops::AppearanceStreamBuilder::fontObject(TypedFont::Helvetica, written).number == 3);
    const std::map<int, double> set = engineAdvances(written, problems);
    REQUIRE(problems.isEmpty());
    REQUIRE(set.size() == 217u);
    CHECK(differing(set).empty());
}
