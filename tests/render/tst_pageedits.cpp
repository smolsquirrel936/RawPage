// tst_pageedits -- Stage 7 T9 (`Stage7-Assembly-Plan.md` §6, D7-15, D7-32), the ENGINE's half: what PDFium reads back
// from a file whose boxes, content, labels and thumbnails RawPage changed, and the INK the changed pages leave
// (`Stage7-Assembly-Testing-Guide.md` §5.11 -- A-58, A-60, A-61, A-62, A-63). Every file is saved by the shipped backend
// through a journal and `SaveCoordinator`, and read by a FRESH engine document: `FPDFPage_Get*Box` (the page tree
// `PdfiumDocument` builds), `FPDF_GetPageLabel` (each page's label), `FPDFPage_GetThumbnailAsBitmap`
// (`PdfiumDocument::embeddedThumbnail`) and the raster (rule 6: the ink, never our own arithmetic).
//
// WHAT IT DOES NOT COVER (convention 19): the surfaces. Each command is constructed here; the dialogs, the crop tool and
// the actions that make them are `tests/ui`'s and `tests/composition`'s.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/doc/SaveCoordinator.h"
#include "core/render/RenderRequest.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/render/PdfiumDocument.h"
#include "platform/win/WinDurableFiles.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
using Catch::Approx;

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
    live->path = live->scratch.filePath(QStringLiteral("document.pdf"));
    REQUIRE(QFile::copy(corpusPath(relative), live->path));
    QFile::setPermissions(live->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    auto handle = live->backend.open(live->path, nullptr);
    INFO(relative);
    REQUIRE(handle);
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

// Saved, and a copy kept -- the copy is what a fresh engine reads. (`qpdf --check` reads the same edits from the
// headless suite's `written/`, `tests/unit/tst_pageedits`, on Linux where this suite does not build.)
QString save(Live& live, const char* name)
{
    auto result = live.coordinator->save(*live.journal, live.path);
    INFO("save " << name << ": " << (result ? std::string() : std::string(result.error().messageKey.data()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    const QString copy = live.scratch.filePath(QStringLiteral("saved-%1.pdf").arg(++live.saves));
    REQUIRE(QFile::copy(live.path, copy));
    return copy;
}

core::PdfRect rect(double left, double bottom, double right, double top)
{
    return core::PdfRect{core::PdfPoint{left, bottom}, core::PdfSize{right - left, top - bottom}};
}

// THE INK: the smallest rectangle, in the page's own points, holding every pixel that is not white -- the page drawn
// whole at `scale` by `backend`, unturned. `std::nullopt` for a page with no ink.
std::optional<core::PdfRect> inkOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int index, double scale)
{
    const core::Page* page = backend.pages(handle)->at(index);
    REQUIRE(page != nullptr);
    REQUIRE(page->rotation() == core::PageRotation::None);
    const core::PdfRect crop = page->cropBox();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = index;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::Annotations;
    const int width = static_cast<int>(std::ceil(crop.size.width * scale));
    const int height = static_cast<int>(std::ceil(crop.size.height * scale));
    request.tile = core::render::TileRect{0, 0, width, height};
    auto image = backend.rasterizer(handle)->raster(request);
    REQUIRE(image);
    int left = width, right = -1, top = height, bottom = -1;
    for (int y = 0; y < image.value().height; ++y) {
        const auto* row = reinterpret_cast<const unsigned char*>(image.value().pixels.constData()) + static_cast<std::ptrdiff_t>(y) * image.value().bytesPerLine;
        for (int x = 0; x < image.value().width; ++x) {
            const unsigned char* px = row + x * 4;
            if (px[0] == 255 && px[1] == 255 && px[2] == 255) continue;
            left = std::min(left, x);
            right = std::max(right, x + 1);
            top = std::min(top, y);
            bottom = std::max(bottom, y + 1);
        }
    }
    if (right < 0) return std::nullopt;
    return rect(crop.left() + left / scale, crop.top() - bottom / scale, crop.left() + right / scale, crop.top() - top / scale);
}

struct Fresh {
    pdf::DocumentBackendImpl backend;
    core::DocumentHandle handle;
    const core::Page& page(int index) const
    {
        const core::Page* found = backend.pages(handle)->at(index);
        REQUIRE(found != nullptr);
        return *found;
    }
};

std::unique_ptr<Fresh> openFresh(const QString& path)
{
    auto fresh = std::make_unique<Fresh>();
    auto handle = fresh->backend.open(path, nullptr);
    REQUIRE(handle);
    fresh->handle = handle.value();
    return fresh;
}

// PDFium reads floats: a box written to a ten-thousandth of a point reads back within a hundredth.
void requireSameBox(const core::PdfRect& got, const core::PdfRect& want)
{
    CHECK(got.left() == Approx(want.left()).margin(0.01));
    CHECK(got.bottom() == Approx(want.bottom()).margin(0.01));
    CHECK(got.right() == Approx(want.right()).margin(0.01));
    CHECK(got.top() == Approx(want.top()).margin(0.01));
}

} // namespace

// ==========================================================================================
// A-60 -- the five boxes, read back by PDFium
// ==========================================================================================

TEST_CASE("pageops.boxes: every box written is the box PDFium reads, and undo a crop shows the MediaBox", "[pageedits][boxes][A-60]")
{
    auto live = openLive("assembly/boxes.pdf");
    const core::PageBoxes asked{rect(0, 0, 612, 792), rect(50.25, 60, 560, 700.5), rect(40, 50, 570, 710),
                                rect(45, 55, 565, 705), rect(100, 100, 500, 600)};
    REQUIRE(live->journal->run(std::make_unique<core::CropPagesCommand>(std::vector<int>{0}, std::vector<core::PageBoxes>{asked})));
    // A CropBox reaching outside the MediaBox on page 2: written clipped.
    REQUIRE(live->journal->run(std::make_unique<core::CropPagesCommand>(
        std::vector<int>{1}, std::vector<core::PageBoxes>{core::PageBoxes{rect(0, 0, 595.276, 841.89), rect(-40, 200, 400, 900), {}, {}, {}}})));
    {
        auto fresh = openFresh(save(*live, "boxes"));
        const core::Page& page = fresh->page(0);
        requireSameBox(page.mediaBox(), asked.media);
        requireSameBox(page.cropBox(), *asked.crop);
        requireSameBox(page.bleedBox(), *asked.bleed);
        requireSameBox(page.trimBox(), *asked.trim);
        requireSameBox(page.artBox(), *asked.art);
        requireSameBox(fresh->page(1).cropBox(), rect(0, 200, 400, 841.89));
    }
    // UNDO A CROP (`Features.md` §5.1): every box removed but the MediaBox -- what the page shows is the MediaBox.
    REQUIRE(live->journal->run(std::make_unique<core::CropPagesCommand>(
        std::vector<int>{0}, std::vector<core::PageBoxes>{core::PageBoxes{asked.media, {}, {}, {}, {}}})));
    {
        auto fresh = openFresh(save(*live, "boxes-uncropped"));
        requireSameBox(fresh->page(0).cropBox(), asked.media);
        requireSameBox(fresh->page(0).artBox(), asked.media);
    }
    // And the live engine draws what it saved: the page's size as the tree the engine draws holds it.
    const core::Page* drawn = live->backend.pages(live->handle)->at(0);
    REQUIRE(drawn != nullptr);
    requireSameBox(drawn->cropBox(), asked.media);
}

// ==========================================================================================
// A-61 -- white margins removed to the CONTENT box, judged on the ink
// ==========================================================================================

TEST_CASE("crop.white_margins: the crop is the content box padded, and the ink touches each edge within the padding", "[pageedits][crop][A-61]")
{
    const double scale = 2.0;   // 144 dpi: one device pixel is half a point
    // WHAT THE CONTENT BOX ADDS TO THE INK (Stage 7 guide I-16): `boxes.pdf`'s page 3 is a FILLED rectangle, whose bounds
    // are its ink; `cropbox-margins.pdf`'s panel is STROKED 2 pt wide, and PDFium's object bounds -- the content box
    // Fit Visible and this crop share (D7-15) -- reach 2 pt past the path where the ink reaches 1 pt. Measured, and
    // graded as measured: a crop that cut ink would fail here either way.
    struct Fixture {
        const char* file;
        int page;
        double strokeAllowance;
    };
    for (const Fixture& fixture : {Fixture{"assembly/boxes.pdf", 2, 0.0}, Fixture{"ordinary/cropbox-margins.pdf", 0, 1.0}}) {
        const char* file = fixture.file;
        const int page = fixture.page;
        for (const double asked : {0.0, 9.0}) {
            const double padding = asked + fixture.strokeAllowance;
            INFO(file << " page " << page << ", padding " << asked << " + the engine's " << fixture.strokeAllowance);
            auto live = openLive(file);
            const auto content = live->backend.contentBox(live->handle, page);
            REQUIRE(content);
            const core::Page& before = *live->journal->document().pages().at(page);
            core::PageBoxes boxes = boxesOf(before);
            boxes.crop = core::whiteMarginBox(content.value(), asked, before.cropBox());
            REQUIRE(live->journal->run(std::make_unique<core::CropPagesCommand>(std::vector<int>{page}, std::vector<core::PageBoxes>{boxes})));
            auto fresh = openFresh(save(*live, "white-margins"));
            requireSameBox(fresh->page(page).cropBox(), *boxes.crop);   // the CONTENT box, padded
            const std::optional<core::PdfRect> ink = inkOf(fresh->backend, fresh->handle, page, scale);
            REQUIRE(ink);
            const core::PdfRect crop = fresh->page(page).cropBox();
            const double pixel = 1.0 / scale;
            UNSCOPED_INFO("ink " << ink->left() << " " << ink->bottom() << " " << ink->right() << " " << ink->top()
                                 << ", crop " << crop.left() << " " << crop.bottom() << " " << crop.right() << " " << crop.top());
            CHECK(ink->left() - crop.left() == Approx(padding).margin(pixel));
            CHECK(crop.right() - ink->right() == Approx(padding).margin(pixel));
            CHECK(ink->bottom() - crop.bottom() == Approx(padding).margin(pixel));
            CHECK(crop.top() - ink->top() == Approx(padding).margin(pixel));
        }
    }
    // THE CONTROL that the measure sees a crop through ink: the same panel cropped 3 pt INSIDE its content box leaves the
    // ink touching every edge -- no white at all -- which the padded crops above never do.
    auto live = openLive("ordinary/cropbox-margins.pdf");
    const auto content = live->backend.contentBox(live->handle, 0);
    REQUIRE(content);
    core::PageBoxes tight = boxesOf(*live->journal->document().pages().at(0));
    tight.crop = rect(content.value().left() + 3, content.value().bottom() + 3, content.value().right() - 3, content.value().top() - 3);
    REQUIRE(live->journal->run(std::make_unique<core::CropPagesCommand>(std::vector<int>{0}, std::vector<core::PageBoxes>{tight})));
    auto fresh = openFresh(save(*live, "white-margins-tight"));
    const std::optional<core::PdfRect> ink = inkOf(fresh->backend, fresh->handle, 0, scale);
    REQUIRE(ink);
    CHECK(ink->left() == Approx(tight.crop->left()).margin(0.5));
    CHECK(ink->top() == Approx(tight.crop->top()).margin(0.5));
}

// ==========================================================================================
// A-62 -- a resized page's content, scaled and placed as asked, measured on the ink
// ==========================================================================================

TEST_CASE("resize: the ink is scaled by the ratio asked and placed at its anchor, or does not move without scaling", "[pageedits][resize][A-62]")
{
    const double scale = 2.0;
    const double pixel = 1.0 / scale;
    SECTION("text-letter.pdf to A4, scaled and centred")
    {
        auto live = openLive("ordinary/text-letter.pdf");
        const core::Page letter = *live->journal->document().pages().at(0);
        // The reference at 288 dpi, a quarter of a point: what the scaled ink is predicted from.
        const std::optional<core::PdfRect> before = inkOf(live->backend, live->handle, 0, 4.0);
        REQUIRE(before);
        auto plan = core::planResize(letter, core::PdfSize{595.276, 841.89}, true, core::ResizeAnchor::Centre);
        REQUIRE(plan);
        const std::array<double, 6> m = plan.value().wrap->matrix;
        REQUIRE(live->journal->run(std::make_unique<core::ResizePagesCommand>(std::vector<int>{0}, core::PdfSize{595.276, 841.89}, true,
                                                                              core::ResizeAnchor::Centre)));
        auto fresh = openFresh(save(*live, "resize-a4"));
        requireSameBox(fresh->page(0).cropBox(), rect(0, 0, 595.276, 841.89));
        const std::optional<core::PdfRect> after = inkOf(fresh->backend, fresh->handle, 0, scale);
        REQUIRE(after);
        const double tolerance = pixel + 0.25 * m[0];   // one device pixel here, the reference's quarter point scaled
        CHECK(after->left() == Approx(m[0] * before->left() + m[4]).margin(tolerance));
        CHECK(after->right() == Approx(m[0] * before->right() + m[4]).margin(tolerance));
        CHECK(after->bottom() == Approx(m[3] * before->bottom() + m[5]).margin(tolerance));
        CHECK(after->top() == Approx(m[3] * before->top() + m[5]).margin(tolerance));
        // The live engine shows the page at its new size, and an undo puts the letter page back -- shown and saved.
        requireSameBox(live->backend.pages(live->handle)->at(0)->cropBox(), rect(0, 0, 595.276, 841.89));
        REQUIRE(live->journal->undo());
        auto undone = openFresh(save(*live, "resize-a4-undone"));
        requireSameBox(undone->page(0).cropBox(), letter.cropBox());
        const std::optional<core::PdfRect> back = inkOf(undone->backend, undone->handle, 0, 4.0);
        REQUIRE(back);
        CHECK(*back == *before);
    }
    SECTION("text-letter.pdf without scaling: the boxes change and the ink stays where it is")
    {
        auto live = openLive("ordinary/text-letter.pdf");
        const std::optional<core::PdfRect> before = inkOf(live->backend, live->handle, 0, scale);
        REQUIRE(before);
        REQUIRE(live->journal->run(std::make_unique<core::ResizePagesCommand>(std::vector<int>{0}, core::PdfSize{700, 900}, false,
                                                                              core::ResizeAnchor::Centre)));
        auto fresh = openFresh(save(*live, "resize-unscaled"));
        requireSameBox(fresh->page(0).cropBox(), rect(-44, -54, 656, 846));
        const std::optional<core::PdfRect> after = inkOf(fresh->backend, fresh->handle, 0, scale);
        REQUIRE(after);
        CHECK(after->left() == Approx(before->left()).margin(pixel));
        CHECK(after->right() == Approx(before->right()).margin(pixel));
        CHECK(after->bottom() == Approx(before->bottom()).margin(pixel));
        CHECK(after->top() == Approx(before->top()).margin(pixel));
    }
    SECTION("mixed-page-sizes.pdf: every page to US Letter, scaled, each at its anchor")
    {
        auto live = openLive("ordinary/mixed-page-sizes.pdf");
        const int count = live->journal->document().pages().count();
        std::vector<core::PdfRect> before;
        std::vector<std::array<double, 6>> matrices;
        std::vector<int> pages;
        for (int page = 0; page < count; ++page) {
            const core::Page& shown = *live->journal->document().pages().at(page);
            if (shown.rotation() != core::PageRotation::None) continue;   // the ink measure reads pages unturned
            const std::optional<core::PdfRect> ink = inkOf(live->backend, live->handle, page, 4.0);
            REQUIRE(ink);
            before.push_back(*ink);
            auto plan = core::planResize(shown, core::PdfSize{612, 792}, true, core::ResizeAnchor::BottomLeft);
            REQUIRE(plan);
            matrices.push_back(plan.value().wrap->matrix);
            pages.push_back(page);
        }
        REQUIRE(pages.size() >= 3);
        REQUIRE(live->journal->run(std::make_unique<core::ResizePagesCommand>(pages, core::PdfSize{612, 792}, true, core::ResizeAnchor::BottomLeft)));
        auto fresh = openFresh(save(*live, "resize-mixed"));
        for (std::size_t i = 0; i < pages.size(); ++i) {
            INFO("page " << pages[i]);
            requireSameBox(fresh->page(pages[i]).cropBox(), rect(0, 0, 612, 792));
            const std::optional<core::PdfRect> after = inkOf(fresh->backend, fresh->handle, pages[i], scale);
            REQUIRE(after);
            const std::array<double, 6>& m = matrices[i];
            const double tolerance = pixel + 0.25 * m[0];
            CHECK(after->left() == Approx(m[0] * before[i].left() + m[4]).margin(tolerance));
            CHECK(after->bottom() == Approx(m[3] * before[i].bottom() + m[5]).margin(tolerance));
            CHECK(after->right() == Approx(m[0] * before[i].right() + m[4]).margin(tolerance));
            CHECK(after->top() == Approx(m[3] * before[i].top() + m[5]).margin(tolerance));
        }
    }
}

// ==========================================================================================
// A-63 -- renumbering, read back by PDFium against hand-written tables
// ==========================================================================================

TEST_CASE("renumber: every style, a prefix and a start, per range, as PDFium labels the pages", "[pageedits][renumber][A-63]")
{
    auto live = openLive("assembly/odd-count-9.pdf");
    REQUIRE(live->journal->run(std::make_unique<core::RenumberPagesCommand>(std::vector<int>{0, 1, 2}, core::PageLabelStyle::RomanLower, QString(), 1)));
    REQUIRE(live->journal->run(std::make_unique<core::RenumberPagesCommand>(std::vector<int>{3, 4}, core::PageLabelStyle::LettersUpper,
                                                                            QStringLiteral("App-"), 26)));
    REQUIRE(live->journal->run(std::make_unique<core::RenumberPagesCommand>(std::vector<int>{6}, core::PageLabelStyle::RomanUpper, QString(), 14)));
    REQUIRE(live->journal->run(std::make_unique<core::RenumberPagesCommand>(std::vector<int>{7}, core::PageLabelStyle::LettersLower, QString(), 3)));
    REQUIRE(live->journal->run(std::make_unique<core::RenumberPagesCommand>(std::vector<int>{8}, core::PageLabelStyle::None, QStringLiteral("Cover"), 1)));
    const std::vector<QString> want{"i", "ii", "iii", "App-Z", "App-AA", "6", "XIV", "c", "Cover"};
    // The model says what the screen says, and the engine of a fresh open says the same.
    for (int page = 0; page < 9; ++page) CHECK(live->journal->document().pages().at(page)->label() == want[static_cast<std::size_t>(page)]);
    auto fresh = openFresh(save(*live, "renumber"));
    for (int page = 0; page < 9; ++page) {
        INFO("page " << page);
        CHECK(fresh->page(page).label() == want[static_cast<std::size_t>(page)]);
    }
    // Everything undone and saved: PDFium shows no label at all, as the file came.
    for (int step = 0; step < 5; ++step) REQUIRE(live->journal->undo());
    auto undone = openFresh(save(*live, "renumber-undone"));
    for (int page = 0; page < 9; ++page) CHECK(undone->page(page).label().isEmpty());
}

// ==========================================================================================
// A-58 -- embedded thumbnails, read back by PDFium
// ==========================================================================================

TEST_CASE("thumbnails.embed: PDFium decodes each /Thumb at the size and in the colours written, and none after removal", "[pageedits][thumbnails][A-58]")
{
    auto live = openLive("assembly/odd-count-3.pdf");
    std::vector<core::PageThumbnail> images;
    for (int page = 0; page < 3; ++page) {
        core::PageThumbnail image{20 + page, 26, QByteArray()};
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                image.rgb.append(static_cast<char>(x * 10));
                image.rgb.append(static_cast<char>(y * 9));
                image.rgb.append(static_cast<char>(60 * page));
            }
        }
        images.push_back(image);
    }
    REQUIRE(live->journal->run(std::make_unique<core::SetEmbeddedThumbnailsCommand>(std::vector<int>{0, 1, 2}, images)));
    {
        const QString saved = save(*live, "thumbnails");
        auto document = pdf::render::PdfiumDocument::open(saved, nullptr);
        REQUIRE(document);
        for (int page = 0; page < 3; ++page) {
            INFO("page " << page);
            auto thumbnail = document.value()->embeddedThumbnail(page);
            REQUIRE(thumbnail);
            const core::PageThumbnail& want = images[static_cast<std::size_t>(page)];
            REQUIRE(thumbnail.value().width == want.width);
            REQUIRE(thumbnail.value().height == want.height);
            int differing = 0;
            for (int y = 0; y < want.height; ++y) {
                for (int x = 0; x < want.width; ++x) {
                    const auto* got = reinterpret_cast<const unsigned char*>(thumbnail.value().pixels.constData()) + y * thumbnail.value().bytesPerLine + x * 4;
                    const auto* rgb = reinterpret_cast<const unsigned char*>(want.rgb.constData()) + (y * want.width + x) * 3;
                    if (got[2] != rgb[0] || got[1] != rgb[1] || got[0] != rgb[2]) ++differing;   // BGRx against RGB
                }
            }
            CHECK(differing == 0);
        }
    }
    REQUIRE(live->journal->run(std::make_unique<core::SetEmbeddedThumbnailsCommand>(std::vector<int>{0, 1, 2}, std::vector<core::PageThumbnail>{})));
    {
        auto document = pdf::render::PdfiumDocument::open(save(*live, "thumbnails-removed"), nullptr);
        REQUIRE(document);
        for (int page = 0; page < 3; ++page) {
            auto thumbnail = document.value()->embeddedThumbnail(page);
            REQUIRE_FALSE(thumbnail);
            CHECK(thumbnail.error().code == core::ErrorCode::NotFound);
        }
    }
    // THE CONTROL: the corpus file itself carries none -- what makes the first half's thumbnails ours.
    auto original = pdf::render::PdfiumDocument::open(corpusPath("assembly/odd-count-3.pdf"), nullptr);
    REQUIRE(original);
    CHECK_FALSE(original.value()->embeddedThumbnail(0));
}
