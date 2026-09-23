// tst_contentbox -- the ENGINE half of Stage 4 T12: what `pdf/render/PdfiumDocument` gets out of a
// real page's objects, and what `core/render/FitPolicy` then makes of it. Guide cases **A-11**,
// **A-67** and **A-68** (`Stage4-ViewingSurface-Testing-Guide.md` §5.3 and §5.13); decision
// **D4-14**. **This is where Stage 2's I-28 is closed.**
//
// ---------------------------------------------------------------------------------------------
// WHY IT IS HERE, AND WHY IT COULD NOT HAVE BEEN ANYWHERE ELSE
//
// I-28's observable is a NUMBER read off a real file: over `corpus/ordinary/cropbox-margins.pdf`,
// Fit Page, Fit Visible and Actual Size must give three different zooms. Only this target links the
// engine, and it links no widgets -- which is exactly why D4-14 moved the fit arithmetic out of
// `ui/viewer/PageLayoutEngine` and into `core/render/FitPolicy`. Before that move the assertion
// below could not be written at all, and the case that existed instead asserted `fitMode()` and was
// green for a month.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS FILE DOES NOT COVER, named at the assertions rather than discovered (convention 19)
//
//   * **It is not a widget.** Nothing here shows a page, scrolls to a content box or presses
//     `Ctrl+3`. The chord and the scroll are `tests/ui/tst_properties.cpp`'s half.
//   * **The corpus is ours.** `cropbox-margins.pdf` is generated, and its ink is text drawn by our
//     own generator. What a producer's clipped image or annotation does to a content box is outside
//     this evidence entirely -- the D2-11 bound, in this task's subject.

#include <catch2/catch_test_macros.hpp>

#include "core/backend.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/render/FitPolicy.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/render/PdfiumDocument.h"

#include <filesystem>
#include <memory>
#include <string_view>

using rawpage::core::PdfRect;
using rawpage::core::render::FitMode;
using rawpage::core::render::PageBoxes;
using rawpage::core::render::PhysicalDpi;
using rawpage::core::render::Zoom;
using rawpage::pdf::DocumentBackendImpl;
using rawpage::pdf::render::PdfiumDocument;

namespace {

QString corpusPath(std::string_view relative)
{
    return QString::fromStdString((std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative).string());
}

std::unique_ptr<PdfiumDocument> openCorpus(std::string_view relative)
{
    auto opened = PdfiumDocument::open(corpusPath(relative), {});
    REQUIRE(opened);
    return std::move(opened).value();
}

bool strictlyInside(const PdfRect& inner, const PdfRect& outer, double byAtLeast)
{
    return inner.left() >= outer.left() + byAtLeast && inner.bottom() >= outer.bottom() + byAtLeast
           && inner.right() <= outer.right() - byAtLeast && inner.top() <= outer.top() - byAtLeast;
}

PageBoxes boxesOf(const PdfiumDocument& document, int pageIndex)
{
    const rawpage::core::Page* page = document.pages().at(pageIndex);
    REQUIRE(page != nullptr);

    PageBoxes boxes;
    boxes.media = page->mediaBox();
    boxes.crop = page->cropBox();
    if (const auto content = document.contentBox(pageIndex); content) boxes.content = content.value();
    return boxes;
}

} // namespace

// =============================================================================================
// A-11 -- the fixture separates the three rectangles, read through the accessor T12 built.
// =============================================================================================

TEST_CASE("cropbox-margins.pdf carries three distinct rectangles", "[contentbox]")
{
    const auto document = openCorpus("ordinary/cropbox-margins.pdf");
    const PageBoxes boxes = boxesOf(*document, 0);

    REQUIRE(boxes.content.has_value());

    // THE CASE T1 COULD NOT RUN. It measured these numbers out of the bytes and left its box
    // unticked, because `core/backend.h` had no box accessor of any kind until this task -- the
    // observable it was written against did not exist. This is that observable.
    //
    // 36 pt is what the case requires and the fixture carries 72 on every side, which is the slack
    // that keeps the claim from turning on a rounding difference in the engine's text metrics.
    CHECK(strictlyInside(boxes.crop, boxes.media, 36.0));
    CHECK(strictlyInside(*boxes.content, boxes.crop, 36.0));
}

// =============================================================================================
// A-67 -- the box is the union of the objects, it falls back, and it is cached.
// =============================================================================================

TEST_CASE("the content box is tight around the ink", "[contentbox]")
{
    const auto document = openCorpus("ordinary/cropbox-margins.pdf");
    const auto box = document->contentBox(0);
    REQUIRE(box);

    // TIGHT, not merely inside: the content occupies well under half the sheet's area. A
    // "content box" that had quietly come back as the CropBox would pass `strictlyInside` against
    // the MediaBox above and fail here, which is why both assertions exist.
    const rawpage::core::Page* page = document->pages().at(0);
    REQUIRE(page != nullptr);
    const double sheet = page->mediaBox().size.width * page->mediaBox().size.height;
    const double ink = box.value().size.width * box.value().size.height;
    CHECK(ink > 0.0);
    CHECK(ink < sheet * 0.6);
}

TEST_CASE("a page with no objects has no content box and the fit falls back", "[contentbox]")
{
    // **PAGE 2 OF `cropbox-margins.pdf` IS DELIBERATELY EMPTY** -- a well-formed page with a
    // zero-length content stream and therefore no page objects at all, carrying page 1's boxes
    // (`corpus/PROVENANCE.md`). T1 built it for exactly this half of D4-14. An empty page is
    // ORDINARY and is not a failure: `NotFound` is what `core/render/FitPolicy` falls back to the
    // CropBox for, and the fallback is exactly what `Ctrl+3` did for the whole of Stages 2 and 3.
    const auto document = openCorpus("ordinary/cropbox-margins.pdf");
    REQUIRE(document->pageCount() >= 2);

    const auto box = document->contentBox(1);
    REQUIRE_FALSE(box);
    CHECK(box.error().code == rawpage::core::ErrorCode::NotFound);

    // AND THE FIT STILL ANSWERS. This is the clause that matters: a page the engine cannot bound
    // must still be fittable, or `Ctrl+3` on it is a viewer that does nothing.
    PageBoxes boxes = boxesOf(*document, 1);
    REQUIRE_FALSE(boxes.content.has_value());
    CHECK(rawpage::core::render::fitRect(boxes, FitMode::Visible) == boxes.crop);

    // **AND IT FALLS BACK TO THE CROPBOX, NOT THE MEDIABOX**, which on this file is a visibly
    // different rectangle rather than something to infer -- 72 pt smaller on every side. That is
    // the reason the fixture puts the empty page in THIS document: on any other corpus file the
    // two fallbacks would be indistinguishable.
    CHECK(rawpage::core::render::fitRect(boxes, FitMode::Visible) != boxes.media);

    // The second ask answers the same way. An empty page must be CACHED as empty, or every fit on
    // it walks a page that has nothing to walk.
    const auto again = document->contentBox(1);
    REQUIRE_FALSE(again);
    CHECK(again.error().code == rawpage::core::ErrorCode::NotFound);
}

TEST_CASE("asking twice calls the engine once", "[contentbox]")
{
    const auto document = openCorpus("ordinary/cropbox-margins.pdf");

    const auto first = document->contentBox(0);
    const auto second = document->contentBox(0);
    REQUIRE(first);
    REQUIRE(second);

    // THE CACHE IS ASSERTED BY ITS RESULT, and this is the back door being named: there is no
    // counter on `FPDF_LoadPage` that a test may read, because the handle never leaves
    // `pdf/render/`. So what is proved here is that the second answer is IDENTICAL, which a
    // recomputation would also give -- the claim that the engine was not called again rests on the
    // code, which consults the map before the lock. **What this therefore does NOT cover** is a
    // cache that is written and never read; a per-page timing assertion would be a flake.
    CHECK(first.value() == second.value());

    // An index outside the tree is refused rather than cached, and refused as a CALLER error rather
    // than as an empty page -- a distinction a fit does not need and a batch runner does.
    const auto missing = document->contentBox(document->pageCount() + 5);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == rawpage::core::ErrorCode::InvalidArgument);
}

TEST_CASE("the content box reaches the backend interface", "[contentbox]")
{
    // Through `core::IDocumentBackend`, which is what `ui/` is handed and the only way it could
    // ever reach this: `ui/` may never name `pdf/`, so a box that existed only on the concrete
    // document would be a box no viewer could ask for. That is Stage 2's I-08, twice already, and
    // the reason this accessor was declared on the interface rather than on the implementation.
    DocumentBackendImpl backend;
    const auto handle = backend.open(corpusPath("ordinary/cropbox-margins.pdf"), {});
    REQUIRE(handle);

    const auto box = backend.contentBox(handle.value(), 0);
    REQUIRE(box);
    CHECK(box.value().size.width > 0.0);

    // A handle this backend did not open is a caller error and never an empty page.
    const auto alien = backend.contentBox(rawpage::core::DocumentHandle{9999}, 0);
    REQUIRE_FALSE(alien);
    CHECK(alien.error().code == rawpage::core::ErrorCode::InvalidArgument);

    REQUIRE(backend.close(handle.value()));
}

// =============================================================================================
// A-68 -- `Ctrl+3` produces the CONTENT's zoom. **Stage 2's I-28, closed.**
// =============================================================================================

TEST_CASE("the three fits over a real page give three different zooms", "[contentbox]")
{
    const auto document = openCorpus("ordinary/cropbox-margins.pdf");
    const PageBoxes boxes = boxesOf(*document, 0);
    REQUIRE(boxes.content.has_value());

    const rawpage::core::DeviceSize viewport{1200.0, 900.0};
    const PhysicalDpi dpi{96.0};
    constexpr double kMargin = 12.0;

    const Zoom page = rawpage::core::render::zoomToFit(
        FitMode::Page, rawpage::core::render::fitRect(boxes, FitMode::Page).size, viewport, kMargin,
        dpi, Zoom{1.0});
    const Zoom visible = rawpage::core::render::zoomToFit(
        FitMode::Visible, rawpage::core::render::fitRect(boxes, FitMode::Visible).size, viewport,
        kMargin, dpi, Zoom{1.0});
    const Zoom actual = Zoom::actualSize();

    // **THE ASSERTION IS ON THE ZOOM, NEVER ON `fitMode()`.** The case that was supposed to catch
    // this before asserted the MODE, and the mode was always right: `Ctrl+3` set `FitMode::Visible`
    // faithfully and then fitted the CropBox, so on every corpus file it was `Ctrl+0` under another
    // name. That case was green for a month (I-28, `Stage2-Rendering-Testing-Guide.md` §8).
    CHECK(visible.factor != page.factor);
    CHECK(visible.factor != actual.factor);
    CHECK(page.factor != actual.factor);

    // **AND IT IS THE CONTENT'S ZOOM, WHICH IS THE ONLY ASSERTION THAT DISCRIMINATES.** The first
    // version of this case checked `visible != page` and `visible > page` and PASSED with I-28
    // planted back in -- because on this file the CropBox is smaller than the sheet, so a Fit
    // Visible still fitting the CropBox is both different from Fit Page and larger than it. Three
    // different numbers is a necessary condition and not a sufficient one, and a control that does
    // not fail for the right reason is worse than none (convention 7).
    //
    // So the expectation is computed from the content box the engine reported, independently of
    // `fitRect`, and the two must be equal to the bit. The line below it is what makes that equality
    // mean something: the CropBox fit is a DIFFERENT number, so the assertion cannot be satisfied by
    // the defect it exists to catch.
    const Zoom fromContent = rawpage::core::render::zoomToFit(
        FitMode::Visible, boxes.content->size, viewport, kMargin, dpi, Zoom{1.0});
    const Zoom fromCrop = rawpage::core::render::zoomToFit(
        FitMode::Visible, boxes.crop.size, viewport, kMargin, dpi, Zoom{1.0});

    CHECK(visible.factor == fromContent.factor);
    REQUIRE(fromContent.factor != fromCrop.factor);   // the discrimination itself
    CHECK(visible.factor != fromCrop.factor);

    // And the whole-box fits run in the direction the geometry requires: a content box strictly
    // inside the sheet needs MORE magnification to fill the same viewport.
    CHECK(visible.factor > page.factor);

    // The control, on the same instrument: a page whose content fills its sheet makes the two
    // agree. `text-a4.pdf`'s ink does not reach its edges, so the control is built rather than
    // borrowed -- and building it here rather than in the corpus is deliberate, because a second
    // generated file would have to be regenerated byte-identically forever to prove one equality.
    PageBoxes filled;
    filled.media = boxes.media;
    filled.crop = boxes.media;
    filled.content = boxes.media;
    const Zoom filledVisible = rawpage::core::render::zoomToFit(
        FitMode::Visible, rawpage::core::render::fitRect(filled, FitMode::Visible).size, viewport,
        kMargin, dpi, Zoom{1.0});
    const Zoom filledPage = rawpage::core::render::zoomToFit(
        FitMode::Page, rawpage::core::render::fitRect(filled, FitMode::Page).size, viewport,
        kMargin, dpi, Zoom{1.0});
    CHECK(filledVisible.factor == filledPage.factor);
}

// =============================================================================================
// I-10 -- Fit Page over a REAL page fits the crop box and not the sheet.
// =============================================================================================

TEST_CASE("Fit Page over a real page fits the drawn page and not the sheet", "[contentbox]")
{
    // **THE PRODUCT HALF OF I-10**, on the file a person found it with. `cropbox-margins.pdf` is
    // the only corpus file whose CropBox is inset from its MediaBox, so it is the only one on which
    // this case can fail -- which is the whole reason T1 built it, and is why the same defect sat
    // under a green suite from Stage 2 until M-11 was walked on 2026-09-24.
    const auto document = openCorpus("ordinary/cropbox-margins.pdf");
    const PageBoxes boxes = boxesOf(*document, 0);
    REQUIRE(boxes.crop.size.height != boxes.media.size.height);   // the fixture, asserted

    const rawpage::core::DeviceSize viewport{1200.0, 900.0};
    const PhysicalDpi dpi{96.0};
    constexpr double kMargin = 12.0;

    const Zoom page = rawpage::core::render::zoomToFit(
        FitMode::Page, rawpage::core::render::fitRect(boxes, FitMode::Page).size, viewport, kMargin,
        dpi, Zoom{1.0});
    const Zoom fromCrop = rawpage::core::render::zoomToFit(FitMode::Page, boxes.crop.size, viewport,
                                                           kMargin, dpi, Zoom{1.0});
    const Zoom fromSheet = rawpage::core::render::zoomToFit(FitMode::Page, boxes.media.size,
                                                            viewport, kMargin, dpi, Zoom{1.0});

    REQUIRE(fromCrop.factor != fromSheet.factor);   // the discrimination itself
    CHECK(page.factor == fromCrop.factor);
    CHECK(page.factor != fromSheet.factor);

    // **AND THE DRAWN PAGE FILLS THE VIEWPORT IT WAS FITTED TO**, which is what "fit page" means
    // and what the defect broke: at the sheet's zoom the crop box -- the rectangle
    // `core::Page::displaySize()` returns and `PagePresentation` lays out -- covered only 83 % of
    // the height it was given, and a person saw the next page's top edge below it.
    const double available = 900.0 - 2.0 * kMargin;
    const double drawnAtPageZoom = boxes.crop.size.height * (96.0 / 72.0) * page.factor;
    const double drawnAtSheetZoom = boxes.crop.size.height * (96.0 / 72.0) * fromSheet.factor;
    CHECK(drawnAtPageZoom > available * 0.999);
    CHECK(drawnAtSheetZoom < available * 0.9);
}
