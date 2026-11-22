// tst_decorationengine -- Stage 7 T14 (`Stage7-Assembly-Plan.md` D7-7, D7-14, D7-44), the ENGINE's half: what PDFium reads
// and draws from a file whose pages RawPage decorated (`Stage7-Assembly-Testing-Guide.md` §3.8, §5.16 -- A-91's raster
// half, A-92, A-93, A-95's raster half, A-96). Every file is saved by the shipped backend through a journal and
// `SaveCoordinator`, and read by a FRESH engine document: its text extracted (rule 6 -- what the page shows, never our
// own arithmetic) and its pages rasterized.
//
// WHAT IT DOES NOT COVER (convention 19): the surfaces. Each command is constructed here; the dialogs and the actions
// that make them are `tests/ui`'s and `tests/composition`'s. And another READER: Acrobat and Firefox are M-07's.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/DecorationCommands.h"
#include "core/doc/BatesAllocator.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/doc/SaveCoordinator.h"
#include "core/render/RenderRequest.h"
#include "pdf/DocumentBackendImpl.h"
#include "platform/win/WinDurableFiles.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cstring>
#include <set>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
using Catch::Approx;
namespace fs = std::filesystem;

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
    INFO(relative);
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

// Saved -- as a Save As where the document's first save must be one (a repaired file) -- and a copy kept, which is what a
// fresh engine reads.
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
    int pageCount() const { return backend.pages(handle)->count(); }
    const core::Page& page(int index) const { return *backend.pages(handle)->at(index); }
};

std::unique_ptr<Fresh> openFresh(const QString& path)
{
    auto fresh = std::make_unique<Fresh>();
    auto handle = fresh->backend.open(path, nullptr);
    REQUIRE(handle);
    fresh->handle = handle.value();
    return fresh;
}

// The page's text as PDFium extracts it, every space and line break taken out -- what a reader's search finds.
QString pageText(Fresh& fresh, int index)
{
    core::ITextExtractor* extractor = fresh.backend.textExtractor(fresh.handle);
    REQUIRE(extractor != nullptr);
    core::text::PageTextModel model;
    REQUIRE(extractor->extractPage(fresh.handle, index, model));
    QString out;
    for (const core::text::TextChar& c : model.chars()) {
        if (c.codepoint == U' ' || c.codepoint == U'\n' || c.codepoint == U'\r' || c.codepoint == U'\t') continue;
        out += QString::fromUcs4(&c.codepoint, 1);
    }
    return out;
}

QString squeezed(const QString& text)
{
    QString out = text;
    out.remove(QLatin1Char(' '));
    return out;
}

core::RasterImage raster(core::IDocumentBackend& backend, core::DocumentHandle handle, int index, double scale)
{
    const core::Page* page = backend.pages(handle)->at(index);
    REQUIRE(page != nullptr);
    const core::PdfSize shown = page->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = index;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::Annotations;
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(shown.width * scale)), static_cast<int>(std::ceil(shown.height * scale))};
    auto image = backend.rasterizer(handle)->raster(request);
    REQUIRE(image);
    return std::move(image).value();
}

// Pixels whose colour differs, inside `box` (device pixels, half-open) or everywhere.
struct DeviceBox {
    int left = 0, top = 0, right = 0, bottom = 0;
};
int differing(const core::RasterImage& a, const core::RasterImage& b, std::optional<DeviceBox> box = std::nullopt)
{
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    const DeviceBox at = box.value_or(DeviceBox{0, 0, a.width, a.height});
    int count = 0;
    for (int y = std::max(0, at.top); y < std::min(a.height, at.bottom); ++y) {
        const auto* ra = reinterpret_cast<const unsigned char*>(a.pixels.constData()) + static_cast<std::ptrdiff_t>(y) * a.bytesPerLine;
        const auto* rb = reinterpret_cast<const unsigned char*>(b.pixels.constData()) + static_cast<std::ptrdiff_t>(y) * b.bytesPerLine;
        for (int x = std::max(0, at.left); x < std::min(a.width, at.right); ++x) {
            if (std::memcmp(ra + x * 4, rb + x * 4, 3) != 0) ++count;
        }
    }
    return count;
}

// The smallest box holding every pixel that is not white, in device pixels; none for a page with no ink.
std::optional<DeviceBox> inkBox(const core::RasterImage& image)
{
    DeviceBox box{image.width, image.height, -1, -1};
    for (int y = 0; y < image.height; ++y) {
        const auto* row = reinterpret_cast<const unsigned char*>(image.pixels.constData()) + static_cast<std::ptrdiff_t>(y) * image.bytesPerLine;
        for (int x = 0; x < image.width; ++x) {
            const unsigned char* px = row + x * 4;
            if (px[0] == 255 && px[1] == 255 && px[2] == 255) continue;
            box.left = std::min(box.left, x);
            box.right = std::max(box.right, x + 1);
            box.top = std::min(box.top, y);
            box.bottom = std::max(box.bottom, y + 1);
        }
    }
    if (box.right < 0) return std::nullopt;
    return box;
}

core::FieldValues fieldValues(const QString& file = QStringLiteral("merge-a.pdf"))
{
    return core::FieldValues{QDate(2026, 11, 23), QTime(16, 45), file, QStringLiteral("D:/matter/") + file};
}

core::HeaderFooterSpec batesSpec(long long start)
{
    core::HeaderFooterSpec spec;
    spec.areas[5] = QStringLiteral("<<bates>>");
    spec.size = 9;
    spec.bottom = 14;
    spec.right = 18;
    spec.bates = core::BatesFormat{QStringLiteral("CASE-"), QString(), 6, start};
    return spec;
}

} // namespace

// ==========================================================================================
// A-92 -- every field, on every page, as PDFium extracts it from the drawn page
// ==========================================================================================

TEST_CASE("headerfooter.fields: every field expanded on every page is the text PDFium extracts from it", "[decorationengine][A-92]")
{
    auto live = openLive("ordinary/outline-labels.pdf");   // labelled pages: `<<label>>` is not the page's number
    REQUIRE(live);
    core::HeaderFooterSpec spec;
    spec.areas = {QStringLiteral("<<file>>"), QStringLiteral("<<date>> <<time>>"), QStringLiteral("<<label>>"),
                  QStringLiteral("Page <<page>> of <<count>>"), QStringLiteral("Draft"), QStringLiteral("<<path>>")};
    spec.size = 8;
    spec.top = 10;
    spec.bottom = 10;
    spec.left = 10;
    spec.right = 10;
    const core::FieldValues values = fieldValues(QStringLiteral("outline-labels.pdf"));
    REQUIRE(live->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(spec, values)));
    const core::PageTree& pages = live->journal->document().pages();
    std::vector<QString> labels;
    for (int i = 0; i < pages.count(); ++i) labels.push_back(pages.at(i)->label());
    auto fresh = openFresh(saved(*live));
    REQUIRE(fresh->pageCount() == pages.count());
    bool anyLabelNotNumber = false;
    for (int i = 0; i < fresh->pageCount(); ++i) {
        INFO("page " << i + 1 << " label " << labels[static_cast<std::size_t>(i)].toStdString());
        const QString text = pageText(*fresh, i);
        const core::FieldPage field{i, pages.count(), labels[static_cast<std::size_t>(i)], std::nullopt};
        for (const QString& area : spec.areas) {
            const QString want = squeezed(core::expandFields(area, values, field, spec.bates));
            INFO(want.toStdString());
            CHECK(text.contains(want));
        }
        // Hand values the expansion must be: not only the engine agreeing with itself.
        CHECK(text.contains(QStringLiteral("Page%1of%2").arg(i + 1).arg(pages.count())));
        CHECK(text.contains(QStringLiteral("2026-11-2316:45")));
        CHECK(text.contains(QStringLiteral("D:/matter/outline-labels.pdf")));
        if (!labels[static_cast<std::size_t>(i)].isEmpty() && labels[static_cast<std::size_t>(i)] != QString::number(i + 1))
            anyLabelNotNumber = true;
    }
    CHECK(anyLabelNotNumber);   // the fixture's labels are not its numbers, so `<<label>>` was tested as a label
}

TEST_CASE("headerfooter.fields: a header reads across the top of the page AS SHOWN at every rotation", "[decorationengine][A-92]")
{
    auto live = openLive("ordinary/rotated-pages.pdf");
    REQUIRE(live);
    core::HeaderFooterSpec spec;
    spec.areas[1] = QStringLiteral("TOPMARK");
    spec.areas[3] = QStringLiteral("FOOTMARK");
    spec.size = 12;
    spec.top = 20;
    spec.bottom = 20;
    spec.left = 30;
    REQUIRE(live->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(spec, fieldValues())));
    auto fresh = openFresh(saved(*live));
    std::set<int> rotations;
    for (int i = 0; i < fresh->pageCount(); ++i) {
        const core::Page& page = fresh->page(i);
        rotations.insert(static_cast<int>(page.rotation()));
        INFO("page " << i + 1 << " rotated " << static_cast<int>(page.rotation()));
        core::text::PageTextModel model;
        REQUIRE(fresh->backend.textExtractor(fresh->handle)->extractPage(fresh->handle, i, model));
        // Each mark's characters, mapped back to the page as shown: the header's along the top, centred; the footer's
        // along the bottom from the left margin -- read from where PDFium put each glyph's origin.
        const core::PdfPoint o = core::shownToPage(page, 0, 0);
        const core::PdfPoint ex = core::shownToPage(page, 1, 0);
        const core::PdfPoint ey = core::shownToPage(page, 0, 1);
        const auto shown = [&](core::PdfPoint p) {
            const double ax = ex.x - o.x, bx = ex.y - o.y, ay = ey.x - o.x, by = ey.y - o.y;
            const double px = p.x - o.x, py = p.y - o.y;
            const double det = ax * by - ay * bx;
            return core::PdfPoint{(px * by - py * ay) / det, (ax * py - bx * px) / det};
        };
        const auto find = [&](const QString& word) -> std::vector<core::PdfPoint> {
            const auto& chars = model.chars();
            for (std::size_t at = 0; at + static_cast<std::size_t>(word.size()) <= chars.size(); ++at) {
                bool match = true;
                for (qsizetype k = 0; k < word.size() && match; ++k)
                    match = chars[at + static_cast<std::size_t>(k)].codepoint == static_cast<char32_t>(word.at(k).unicode());
                if (!match) continue;
                std::vector<core::PdfPoint> out;
                for (qsizetype k = 0; k < word.size(); ++k) out.push_back(shown(chars[at + static_cast<std::size_t>(k)].origin));
                return out;
            }
            return {};
        };
        const core::PdfSize size = page.displaySize();
        const auto top = find(QStringLiteral("TOPMARK"));
        REQUIRE(top.size() == 7);
        CHECK(top.front().y == Approx(size.height - 20 - core::kTypedAscent * 12).margin(1.0));
        CHECK(top.back().x > top.front().x);   // reading left to right on the page as shown
        CHECK((top.front().x + top.back().x) / 2 == Approx(size.width / 2).margin(12.0));
        const auto foot = find(QStringLiteral("FOOTMARK"));
        REQUIRE(foot.size() == 8);
        CHECK(foot.front().y == Approx(20 + (core::kTypedLeading - core::kTypedAscent) * 12).margin(1.0));
        CHECK(foot.front().x == Approx(30).margin(1.0));
    }
    CHECK(rotations.size() >= 3);   // the fixture turns its pages, so the rotations were tested
}

// ==========================================================================================
// A-93 -- Bates across three open documents is one run
// ==========================================================================================

TEST_CASE("bates.across: three open documents numbered as one run in a chosen order, each page showing its number, each undo its own", "[decorationengine][A-93]")
{
    // The order chosen: b, a, c -- not the files' own.
    const std::vector<std::string> order{"assembly/merge-b.pdf", "assembly/merge-a.pdf", "assembly/merge-c.pdf"};
    std::vector<std::unique_ptr<Live>> lives;
    for (const std::string& file : order) {
        lives.push_back(openLive(file));
        REQUIRE(lives.back());
    }
    core::BatesAllocator run(core::BatesFormat{QStringLiteral("CASE-"), QString(), 6, 1});
    for (std::size_t d = 0; d < lives.size(); ++d) {
        const int pages = lives[d]->journal->document().pages().count();
        auto share = run.take(QString::fromStdString(order[d]), pages);
        REQUIRE(share);
        REQUIRE(lives[d]->journal->run(std::make_unique<core::ApplyBatesCommand>(batesSpec(share.value().first), fieldValues())));
    }
    std::vector<QString> savedPaths;
    for (auto& live : lives) savedPaths.push_back(saved(*live));

    // Every page shows its number, consecutively across the three in the chosen order; the report's first and last are
    // what each document's first and last pages show.
    long long expected = 1;
    for (std::size_t d = 0; d < lives.size(); ++d) {
        auto fresh = openFresh(savedPaths[d]);
        const core::BatesShare& share = run.report()[d];
        INFO(order[d]);
        CHECK(share.first == expected);
        for (int i = 0; i < fresh->pageCount(); ++i) {
            const QString number = core::formatBates(core::BatesFormat{QStringLiteral("CASE-"), QString(), 6, 1}, expected + i);
            INFO("page " << i + 1 << " " << number.toStdString());
            CHECK(pageText(*fresh, i).contains(number));
        }
        CHECK(pageText(*fresh, 0).contains(core::formatBates(run.format(), share.first)));
        CHECK(pageText(*fresh, fresh->pageCount() - 1).contains(core::formatBates(run.format(), share.last)));
        expected = share.last + 1;
    }
    CHECK(run.report().size() == 3);

    // The middle document's undo removes ITS numbers only.
    REQUIRE(lives[1]->journal->undo());
    const QString undone = saved(*lives[1]);
    auto middle = openFresh(undone);
    for (int i = 0; i < middle->pageCount(); ++i) CHECK_FALSE(pageText(*middle, i).contains(QStringLiteral("CASE-")));
    for (const std::size_t d : {std::size_t{0}, std::size_t{2}}) {
        auto fresh = openFresh(savedPaths[d]);
        CHECK(pageText(*fresh, 0).contains(core::formatBates(run.format(), run.report()[d].first)));
    }
}

// ==========================================================================================
// A-95 -- shrinking the content to clear the header (raster half)
// ==========================================================================================

TEST_CASE("headerfooter.shrink: the shrunk page's ink and the header's do not overlap on the raster, and the control's do", "[decorationengine][A-95]")
{
    // `split-blanks.pdf`: page 1 opens with a heading near its top, page 3 is blank and the same size. The header drawn on
    // the blank page is the header's ink alone, and inside ITS box the decorated content page must draw the same pixels.
    constexpr double kScale = 2.0;
    const auto measure = [&](bool shrink) {
        auto live = openLive("assembly/split-blanks.pdf");
        REQUIRE(live);
        const core::RasterImage plain = raster(live->backend, live->handle, 0, kScale);
        const std::optional<DeviceBox> ink = inkBox(plain);
        REQUIRE(ink);
        core::HeaderFooterSpec spec;
        spec.areas[1] = QStringLiteral("SHRINK HEADER");
        spec.areas[4] = QStringLiteral("SHRINK FOOTER");
        spec.size = 18;
        // The header's line OVER the content's first ink: unshrunk, they must overlap (the control).
        spec.top = std::max(0.0, ink->top / kScale - 6.0);
        spec.bottom = std::max(0.0, (plain.height - ink->bottom) / kScale - 6.0);
        spec.shrink = shrink;
        spec.range = QStringLiteral("1,3");
        REQUIRE(live->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(spec, fieldValues())));
        auto fresh = openFresh(saved(*live));
        const core::RasterImage decorated = raster(fresh->backend, fresh->handle, 0, kScale);
        const core::RasterImage headerOnly = raster(fresh->backend, fresh->handle, 2, kScale);
        const std::optional<DeviceBox> bands = inkBox(headerOnly);
        REQUIRE(bands);
        // The header's band and the footer's: the ink rows of the header-only page, split where they are not contiguous.
        std::vector<DeviceBox> lines;
        for (int y = 0; y < headerOnly.height; ++y) {
            DeviceBox row{headerOnly.width, y, -1, y + 1};
            const auto* r = reinterpret_cast<const unsigned char*>(headerOnly.pixels.constData()) + static_cast<std::ptrdiff_t>(y) * headerOnly.bytesPerLine;
            for (int x = 0; x < headerOnly.width; ++x) {
                if (r[x * 4] != 255 || r[x * 4 + 1] != 255 || r[x * 4 + 2] != 255) {
                    row.left = std::min(row.left, x);
                    row.right = std::max(row.right, x + 1);
                }
            }
            if (row.right < 0) continue;
            if (!lines.empty() && lines.back().bottom == y) {
                lines.back().bottom = y + 1;
                lines.back().left = std::min(lines.back().left, row.left);
                lines.back().right = std::max(lines.back().right, row.right);
            } else {
                lines.push_back(row);
            }
        }
        REQUIRE(lines.size() == 2);
        int overlap = 0;
        for (const DeviceBox& line : lines)
            overlap += differing(decorated, headerOnly, DeviceBox{line.left - 1, line.top - 1, line.right + 1, line.bottom + 1});
        // AND BETWEEN THEM (planted control P7 at T14: content moved below the footer cleared both bands and stayed green):
        // every pixel the content adds -- where the decorated page differs from the header alone -- lies below the
        // header's line and above the footer's.
        int outside = 0;
        for (int y = 0; y < decorated.height; ++y) {
            if (y >= lines[0].bottom && y < lines[1].top) continue;
            outside += differing(decorated, headerOnly, DeviceBox{0, y, decorated.width, y + 1});
        }
        return std::pair{overlap, outside};
    };
    const auto [shrunkOverlap, shrunkOutside] = measure(true);
    CHECK(shrunkOverlap == 0);    // shrunk: inside each band, the decorated page draws the header alone
    CHECK(shrunkOutside == 0);    // and the content is between the bands
    const auto [plainOverlap, plainOutside] = measure(false);
    CHECK(plainOverlap > 50);     // the CONTROL (convention 22): unshrunk, the content's ink is inside the header's
    CHECK(plainOutside > 50);     // and reaches past the bands
}

// ==========================================================================================
// A-96 -- drawn text outside the fonts is refused by name; Latin and CJK read back as typed
// ==========================================================================================

TEST_CASE("decoration.characters: Latin and CJK drawn and read back by PDFium as typed, a Cyrillic letter refused with nothing applied", "[decorationengine][A-96]")
{
    auto live = openLive("ordinary/text-a4.pdf");
    REQUIRE(live);
    core::HeaderFooterSpec cyrillic;
    cyrillic.areas[0] = QStringLiteral("Глава 1");
    const core::Status refused = live->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(cyrillic, fieldValues()));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.decorate.characterRefused"));
    CHECK_FALSE(live->journal->canUndo());

    core::HeaderFooterSpec spec;
    spec.areas[0] = QStringLiteral("Résumé àéîõü ß");
    spec.areas[2] = QStringLiteral("第一章 概要");                  // Han, in the Simplified collection
    spec.areas[3] = QStringLiteral("ページ 한국어");                // kana and Hangul
    spec.areas[5] = QStringLiteral("€ “quotes” — dash");
    spec.size = 11;
    REQUIRE(live->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(spec, fieldValues())));
    auto fresh = openFresh(saved(*live));
    const QString text = pageText(*fresh, 0);
    for (const QString& area : spec.areas) {
        if (area.isEmpty()) continue;
        INFO(area.toStdString());
        CHECK(text.contains(squeezed(area)));
    }
}

// ==========================================================================================
// A-91 -- apply, then remove: 0 differing pixels (§3.8, raster half)
// ==========================================================================================

TEST_CASE("decoration.roundtrip: every editable corpus file's first and last page rasterize to 0 differing pixels after apply, save, remove, save", "[decorationengine][A-91]")
{
    // THE FIRST UNKNOWN FONT PDFium SUBSTITUTES IN A PROCESS DRAWS UNLIKE EVERY LATER ONE (Stage 7 guide I-14): found again
    // here, `malformed/bad-name-hex-escape.pdf` drawn before and after with ~54,000 pixels apart on each page and its
    // objects equal in value (`tst_decoration`). So the first substitution is TAKEN before the sweep, as
    // `tst_fullwriterengine`'s I-13 case takes it, and this case does NOT cover what the first document of a session shows.
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
        core::HeaderFooterSpec spec;
        spec.areas = {QStringLiteral("<<file>>"), {}, QStringLiteral("<<date>>"), {}, QStringLiteral("<<page>> / <<count>>"), {}};
        spec.size = 8;
        spec.top = spec.bottom = spec.left = spec.right = 6;
        core::Status applied = live->journal->run(std::make_unique<core::ApplyHeaderFooterCommand>(spec, fieldValues()));
        if (!applied) {
            failures.push_back(relative + ": apply refused, " + applied.error().messageKey.data());
            continue;
        }
        ++files;
        const auto step = [&](const char* what, bool ok) {
            if (!ok) failures.push_back(relative + ": " + what);
            return ok;
        };
        if (!step("save", saveLive(*live).hasValue())) continue;
        if (!step("bates", static_cast<bool>(live->journal->run(std::make_unique<core::ApplyBatesCommand>(batesSpec(1), fieldValues())))))
            continue;
        if (!step("save bates", saveLive(*live).hasValue())) continue;
        if (!step("remove header", static_cast<bool>(live->journal->run(std::make_unique<core::RemoveHeaderFooterCommand>())))) continue;
        if (!step("remove bates", static_cast<bool>(live->journal->run(std::make_unique<core::RemoveBatesCommand>())))) continue;
        auto last = saveLive(*live);
        if (!step("save removed", last.hasValue())) continue;
        auto fresh = openFresh(last.value());
        for (std::size_t k = 0; k < probe.size(); ++k) {
            const int d = differing(before[k], raster(fresh->backend, fresh->handle, probe[k], 1.0));
            if (d != 0) failures.push_back(relative + " page " + std::to_string(probe[k] + 1) + ": " + std::to_string(d) + " pixels differ");
        }
    }
    UNSCOPED_INFO(files << " files decorated and taken off");
    for (const std::string& f : failures) UNSCOPED_INFO(f);
    CHECK(failures.empty());
    CHECK(files >= 100);
}
