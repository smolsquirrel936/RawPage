// tst_watermarkengine -- Stage 7 T15 (`Stage7-Assembly-Plan.md` D7-7, D7-9, D7-45), the ENGINE's half: what PDFium draws
// from a file RawPage watermarked (`Stage7-Assembly-Testing-Guide.md` §3.8, §5.17 -- A-99's raster half, A-100's raster
// half, A-101). Every file is saved by the shipped backend through a journal and `SaveCoordinator`, and read by a FRESH
// engine document.
//
// WHAT IT DOES NOT COVER (convention 19): another READER. That Acrobat hides a print-only watermark on screen and prints
// it is M-09's; PDFium's `FPDF_PRINTING` flag is the print half here, measured on PDFium's raster, not on a printer. And
// the surfaces: every command is constructed here.

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/DecorationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/doc/SaveCoordinator.h"
#include "core/doc/Stamps.h"
#include "core/render/RenderRequest.h"
#include "pdf/DocumentBackendImpl.h"
#include "platform/win/WinDurableFiles.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
namespace fs = std::filesystem;
using core::render::RenderFlags;

namespace {

struct Live {
    QTemporaryDir scratch;
    pdf::DocumentBackendImpl backend;
    platform::win::WinDurableFiles durableFiles;
    QString path;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    std::unique_ptr<core::SaveCoordinator> coordinator;
    int saves = 0;
};

std::unique_ptr<Live> openLive(const std::string& relative)
{
    auto live = std::make_unique<Live>();
    REQUIRE(live->scratch.isValid());
    live->path = live->scratch.filePath(QString::fromStdString(fs::path(relative).filename().string()));
    REQUIRE(QFile::copy(corpusPath(relative), live->path));
    QFile::setPermissions(live->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    auto handle = live->backend.open(live->path, nullptr);
    if (!handle) return nullptr;
    live->handle = handle.value();
    core::Document document;
    REQUIRE(document.hydrate(live->backend, live->handle));
    live->journal = std::make_unique<core::CommandJournal>(std::move(document), live->backend.mutationSink(live->handle));
    Live* self = live.get();
    live->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); },
                                  [self] { return self->backend.settleEdits(self->handle); });
    live->coordinator = std::make_unique<core::SaveCoordinator>(live->backend, &live->durableFiles);
    return live;
}

core::Result<QString> saveLive(Live& live)
{
    auto result = live.coordinator->save(*live.journal, live.path);
    if (!result) return result.error();
    if (result.value().kind == core::SaveOutcome::Kind::SaveAsRequired) {
        const QString target = live.scratch.filePath(QStringLiteral("saved-as.pdf"));
        auto as = live.coordinator->saveAs(*live.journal, target);
        if (!as) return as.error();
        live.path = target;
    }
    const QString copy = live.scratch.filePath(QStringLiteral("saved-%1.pdf").arg(++live.saves));
    if (!QFile::copy(live.path, copy)) return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.save.copy"), copy};
    return copy;
}

QString saved(Live& live)
{
    auto result = saveLive(live);
    INFO((result ? std::string() : std::string(result.error().messageKey.data()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    return result.value();
}

struct Fresh {
    pdf::DocumentBackendImpl backend;
    core::DocumentHandle handle;
};

std::unique_ptr<Fresh> openFresh(const QString& path)
{
    auto fresh = std::make_unique<Fresh>();
    auto handle = fresh->backend.open(path, nullptr);
    REQUIRE(handle);
    fresh->handle = handle.value();
    return fresh;
}

core::RasterImage raster(core::IDocumentBackend& backend, core::DocumentHandle handle, int index, double scale,
                         RenderFlags flags = RenderFlags::Annotations)
{
    const core::Page* page = backend.pages(handle)->at(index);
    REQUIRE(page != nullptr);
    const core::PdfSize shown = page->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = index;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
    request.flags = flags;
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(shown.width * scale)), static_cast<int>(std::ceil(shown.height * scale))};
    auto image = backend.rasterizer(handle)->raster(request);
    REQUIRE(image);
    return std::move(image).value();
}

const unsigned char* pixel(const core::RasterImage& image, int x, int y)
{
    return reinterpret_cast<const unsigned char*>(image.pixels.constData()) + static_cast<std::ptrdiff_t>(y) * image.bytesPerLine + x * 4;
}

int differing(const core::RasterImage& a, const core::RasterImage& b)
{
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    int count = 0;
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            if (std::memcmp(pixel(a, x, y), pixel(b, x, y), 3) != 0) ++count;
        }
    }
    return count;
}

// The pixels inside an OPAQUE FILL of the page's own content: not white, and every pixel two around the same colour.
std::vector<std::pair<int, int>> fillInterior(const core::RasterImage& image)
{
    std::vector<std::pair<int, int>> out;
    for (int y = 2; y + 2 < image.height; ++y) {
        for (int x = 2; x + 2 < image.width; ++x) {
            const unsigned char* p = pixel(image, x, y);
            if (p[0] == 255 && p[1] == 255 && p[2] == 255) continue;
            bool same = true;
            for (int dy = -2; dy <= 2 && same; ++dy) {
                for (int dx = -2; dx <= 2 && same; ++dx) same = std::memcmp(pixel(image, x + dx, y + dy), p, 3) == 0;
            }
            if (same) out.emplace_back(x, y);
        }
    }
    return out;
}

core::StampImage solid(int side, unsigned char r, unsigned char g, unsigned char b)
{
    QByteArray rgb;
    for (int i = 0; i < side * side; ++i) {
        rgb.append(static_cast<char>(r));
        rgb.append(static_cast<char>(g));
        rgb.append(static_cast<char>(b));
    }
    auto image = core::stampImageFromPixels(side, side, rgb, QByteArray());
    REQUIRE(image);
    return image.value();
}

// A solid red picture tiled over the whole page with no gap, opaque.
core::WatermarkSpec coverWith(bool behind)
{
    core::WatermarkSpec spec;
    spec.source = core::WatermarkSource::Image;
    spec.image = solid(8, 255, 0, 0);
    spec.opacity = 1.0;
    spec.rotation = 0;
    spec.scaleMode = core::WatermarkScale::Absolute;
    spec.scale = 10;   // 60 points a copy
    spec.tile = true;
    spec.tileGap = 0;
    spec.behind = behind;
    return spec;
}

core::WatermarkSpec words(core::WatermarkUsage usage, double opacity = 0.5)
{
    core::WatermarkSpec spec;
    spec.text = QStringLiteral("WATERMARK");
    spec.size = 60;
    spec.colour = {0, 0, 0};
    spec.opacity = opacity;
    spec.usage = usage;
    spec.behind = false;
    return spec;
}

} // namespace

TEST_CASE("watermark.usage: print-only is drawn with PDFium's print flag and not without, screen-only the reverse, and always both", "[watermarkengine][A-101]")
{
    auto live = openLive("ordinary/text-letter.pdf");
    REQUIRE(live);
    const RenderFlags print = RenderFlags::Annotations | RenderFlags::Printing;
    const core::RasterImage screenBefore = raster(live->backend, live->handle, 0, 1.0);
    const core::RasterImage printBefore = raster(live->backend, live->handle, 0, 1.0, print);
    struct Row {
        core::WatermarkUsage usage;
        bool onScreen;
        bool printed;
    };
    for (const Row row : {Row{core::WatermarkUsage::PrintOnly, false, true}, Row{core::WatermarkUsage::ScreenOnly, true, false},
                          Row{core::WatermarkUsage::Always, true, true}}) {
        INFO("usage " << core::watermarkUsageName(row.usage).data());
        REQUIRE(live->journal->run(std::make_unique<core::ApplyWatermarkCommand>(words(row.usage))));
        auto fresh = openFresh(saved(*live));
        const int screen = differing(screenBefore, raster(fresh->backend, fresh->handle, 0, 1.0));
        const int printed = differing(printBefore, raster(fresh->backend, fresh->handle, 0, 1.0, print));
        // A 60-point word draws thousands of pixels; absent is EXACTLY none.
        if (row.onScreen) CHECK(screen > 2000);
        else CHECK(screen == 0);
        if (row.printed) CHECK(printed > 2000);
        else CHECK(printed == 0);
        REQUIRE(live->journal->undo());
    }
    // AND THE LIVE ENGINE -- the unsaved update `showEdits` hands it -- draws a print-only one as a fresh one does: not.
    REQUIRE(live->journal->run(std::make_unique<core::ApplyWatermarkCommand>(words(core::WatermarkUsage::PrintOnly))));
    REQUIRE(live->journal->settle());
    CHECK(differing(screenBefore, raster(live->backend, live->handle, 0, 1.0)) == 0);
    CHECK(differing(printBefore, raster(live->backend, live->handle, 0, 1.0, print)) > 2000);
}

TEST_CASE("watermark: behind is hidden where the page's own content is opaque and in front is not, and both show on the empty page", "[watermarkengine][A-99]")
{
    // `boxes.pdf` page 3 draws a filled panel: its interior is the page's own opaque content.
    auto live = openLive("assembly/boxes.pdf");
    REQUIRE(live);
    const core::RasterImage before = raster(live->backend, live->handle, 2, 1.0);
    const std::vector<std::pair<int, int>> interior = fillInterior(before);
    REQUIRE(interior.size() > 1000);
    for (const bool behind : {true, false}) {
        INFO((behind ? "behind" : "in front"));
        REQUIRE(live->journal->run(std::make_unique<core::ApplyWatermarkCommand>(coverWith(behind))));
        auto fresh = openFresh(saved(*live));
        const core::RasterImage after = raster(fresh->backend, fresh->handle, 2, 1.0);
        int changed = 0;
        for (const auto& [x, y] : interior) changed += std::memcmp(pixel(before, x, y), pixel(after, x, y), 3) != 0 ? 1 : 0;
        if (behind) CHECK(changed == 0);
        else CHECK(changed == static_cast<int>(interior.size()));
        // Where the page is EMPTY -- white before -- the red shows either way.
        int white = 0;
        int red = 0;
        for (int y = 0; y < before.height; y += 7) {
            for (int x = 0; x < before.width; x += 7) {
                const unsigned char* p = pixel(before, x, y);
                if (!(p[0] == 255 && p[1] == 255 && p[2] == 255)) continue;
                ++white;
                const unsigned char* q = pixel(after, x, y);
                red += (q[2] > 200 && q[1] < 40 && q[0] < 40) ? 1 : 0;   // BGRA: red is the third byte
            }
        }
        CHECK(white > 100);
        CHECK(red == white);
        REQUIRE(live->journal->undo());
    }
}

TEST_CASE("watermark: opacity is what the raster shows -- black at 50 percent over white is mid-grey", "[watermarkengine][A-99]")
{
    auto live = openLive("assembly/split-blanks.pdf");   // page 3 has no content at all
    REQUIRE(live);
    REQUIRE(live->journal->run(std::make_unique<core::ApplyWatermarkCommand>(words(core::WatermarkUsage::Always, 0.5))));
    auto fresh = openFresh(saved(*live));
    const core::RasterImage image = raster(fresh->backend, fresh->handle, 2, 2.0);
    int darkest = 255;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) darkest = std::min(darkest, static_cast<int>(pixel(image, x, y)[0]));
    }
    // A glyph's solid interior: black at half opacity over white, 127 or 128 -- never darker.
    CHECK(darkest >= 124);
    CHECK(darkest <= 131);
}

TEST_CASE("watermark: a PDF page as the source draws that page, its text read back from the watermarked page", "[watermarkengine][A-99]")
{
    auto source = openFresh(corpusPath("assembly/refs-every-kind.pdf"));
    const auto stamp = source->backend.stampPage(corpusPath("assembly/refs-every-kind.pdf"), 0);
    REQUIRE(stamp);
    core::ITextExtractor* sourceText = source->backend.textExtractor(source->handle);
    core::text::PageTextModel sourceModel;
    REQUIRE(sourceText->extractPage(source->handle, 0, sourceModel));
    QString identity;
    for (const core::text::TextChar& c : sourceModel.chars()) {
        if (c.codepoint > U' ') identity += QString::fromUcs4(&c.codepoint, 1);
    }
    REQUIRE(identity.size() > 3);

    auto live = openLive("assembly/split-blanks.pdf");
    REQUIRE(live);
    core::WatermarkSpec spec;
    spec.source = core::WatermarkSource::Page;
    spec.page = stamp.value();
    spec.rotation = 0;
    spec.opacity = 1.0;
    spec.scaleMode = core::WatermarkScale::Relative;
    spec.scale = 1.0;
    REQUIRE(live->journal->run(std::make_unique<core::ApplyWatermarkCommand>(spec)));
    auto fresh = openFresh(saved(*live));
    core::text::PageTextModel model;
    REQUIRE(fresh->backend.textExtractor(fresh->handle)->extractPage(fresh->handle, 2, model));
    QString drawn;
    for (const core::text::TextChar& c : model.chars()) {
        if (c.codepoint > U' ') drawn += QString::fromUcs4(&c.codepoint, 1);
    }
    CHECK(drawn.contains(identity.left(std::min<qsizetype>(identity.size(), 12))));
}

TEST_CASE("watermark: an image pasted as a page draws the image over the whole page", "[watermarkengine][A-100]")
{
    auto live = openLive("ordinary/text-letter.pdf");
    REQUIRE(live);
    auto page = live->backend.imagePage(solid(40, 0, 128, 255));
    REQUIRE(page);
    const int count = live->journal->document().pages().count();
    REQUIRE(live->journal->run(std::make_unique<core::InsertPagesCommand>(count, page.value())));
    auto fresh = openFresh(saved(*live));
    REQUIRE(fresh->backend.pages(fresh->handle)->count() == count + 1);
    CHECK(fresh->backend.pages(fresh->handle)->at(count)->displaySize().width == 30.0);
    const core::RasterImage image = raster(fresh->backend, fresh->handle, count, 2.0);
    for (const auto& [x, y] : {std::pair{1, 1}, std::pair{image.width / 2, image.height / 2}, std::pair{image.width - 2, image.height - 2}}) {
        const unsigned char* p = pixel(image, x, y);
        CHECK(static_cast<int>(p[0]) == 255);   // BGRA
        CHECK(static_cast<int>(p[1]) == 128);
        CHECK(static_cast<int>(p[2]) == 0);
    }
}

TEST_CASE("decoration.roundtrip: every editable corpus file's first and last page rasterize to 0 differing pixels after watermarks applied, saved, removed, saved", "[watermarkengine][A-100]")
{
    // Stage 7 guide I-14: the first unknown-font substitution of a process is taken before the sweep, as A-91's is.
    {
        pdf::DocumentBackendImpl warm;
        auto opened = warm.open(corpusPath("malformed/bad-name-hex-escape.pdf"), nullptr);
        REQUIRE(opened);
        for (int page = 0; page < warm.pages(opened.value())->count(); ++page) (void)raster(warm, opened.value(), page, 1.0);
    }
    std::size_t files = 0;
    std::vector<std::string> failures;
    for (const auto& entry : fs::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        const std::string relative = fs::relative(entry.path(), RAWPAGE_CORPUS_DIR).generic_string();
        if (relative.rfind("encrypted/", 0) == 0 || relative == "malformed/pages-loop.pdf") continue;
        auto live = openLive(relative);
        if (!live || live->backend.mutationSink(live->handle) == nullptr
            || !live->journal->document().permissions().may(core::Operation::EditPages))
            continue;
        const int count = live->journal->document().pages().count();
        std::vector<int> probe{0};
        if (count > 1) probe.push_back(count - 1);
        std::vector<core::RasterImage> before;
        for (const int page : probe) before.push_back(raster(live->backend, live->handle, page, 1.0));
        const auto step = [&](const char* what, bool ok) {
            if (!ok) failures.push_back(relative + ": " + what);
            return ok;
        };
        if (!step("text", static_cast<bool>(live->journal->run(std::make_unique<core::ApplyWatermarkCommand>(words(core::WatermarkUsage::PrintOnly))))))
            continue;
        ++files;
        if (!step("save", saveLive(*live).hasValue())) continue;
        core::WatermarkSpec cover = coverWith(false);
        cover.opacity = 0.3;
        if (!step("image", static_cast<bool>(live->journal->run(std::make_unique<core::ApplyWatermarkCommand>(cover))))) continue;
        if (!step("save image", saveLive(*live).hasValue())) continue;
        if (!step("remove", static_cast<bool>(live->journal->run(std::make_unique<core::RemoveWatermarkCommand>())))) continue;
        auto last = saveLive(*live);
        if (!step("save removed", last.hasValue())) continue;
        auto fresh = openFresh(last.value());
        for (std::size_t k = 0; k < probe.size(); ++k) {
            const int d = differing(before[k], raster(fresh->backend, fresh->handle, probe[k], 1.0));
            if (d != 0) failures.push_back(relative + " page " + std::to_string(probe[k] + 1) + ": " + std::to_string(d) + " pixels differ");
        }
    }
    UNSCOPED_INFO(files << " files watermarked and cleared");
    for (const std::string& f : failures) UNSCOPED_INFO(f);
    CHECK(failures.empty());
    CHECK(files >= 100);
}
