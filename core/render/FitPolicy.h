// core/render/FitPolicy -- WHICH RECTANGLE A FIT MEASURES, and the zoom that satisfies it.
// Stage 4 T12; decision **D4-14** (`Stage4-ViewingSurface-Plan.md` §3). Closes Stage 2's **I-28**.
//
// ---------------------------------------------------------------------------------------------
// THE DEFECT THIS FILE EXISTS TO END
// ---------------------------------------------------------------------------------------------
// `Features.md` §1.2 defines Fit Visible as *fit the content bounding box, ignoring white margins*.
// From Stage 2 T13 until this task it fitted the **CropBox** -- so on every document whose CropBox
// is its drawable page, `Ctrl+3` was `Ctrl+0` under another name, and the one case asserting it
// checked `fitMode()` and was green for a month. That is I-28 (`Stage2-Rendering-Testing-Guide.md`
// §8), and the lesson it carries is in this header rather than only in that log: **a fit is a
// ZOOM, and a case that asserts the mode asserts nothing**.
//
// ---------------------------------------------------------------------------------------------
// WHY THE ARITHMETIC IS HERE AND NOT IN `ui/viewer/PageLayoutEngine`, WHERE IT LIVED
// ---------------------------------------------------------------------------------------------
// D4-14 says `pdf/render/` computes the content box and `core/render/` decides the fit from it,
// and the reason is a test rather than a taxonomy. The observable I-28 names is a NUMBER read off
// a real file: three different zooms over `corpus/ordinary/cropbox-margins.pdf`, whose three
// rectangles are 72 pt apart. Only `rawpage_render_tests` links the engine, and it links no
// widgets -- so an arithmetic that lived in a `QWidget`'s helper could not be reached by the one
// suite that can open the file. Moving it here is what makes guide case **A-68** assertable at all.
//
// It is also the shape the rest of `core/render/` already has: `DpiPolicy` resolves a zoom into a
// resolution, `PagePresentation` lays slots out, and neither needs a display. This is the third
// pure step and is tested the same way (guide case A-67's fallbacks, over synthetic geometry).
//
// ---------------------------------------------------------------------------------------------
// THE FALLBACK IS THE CROPBOX, AND IT IS NOT AN ERROR PATH
// ---------------------------------------------------------------------------------------------
// A page with no objects has no content bounding box. An engine that cannot answer has none
// either. **Both fall back to the CropBox**, which is what the product did for the whole of Stages
// 2 and 3 -- so the degradation is exactly the old behaviour and never a blank viewport or a
// division by zero. `contentBox` is therefore a `std::optional`, and its absence is a value:
// `fitRect()` takes it and needs no second opinion about whether the engine was asked.

#ifndef RAWPAGE_CORE_RENDER_FITPOLICY_H
#define RAWPAGE_CORE_RENDER_FITPOLICY_H

#include "core/Geometry.h"
#include "core/render/DpiPolicy.h"

#include <optional>

namespace rawpage::core::render {

// One page's three rectangles, in PDF points, BEFORE any rotation. `media` and `crop` come from
// `core::Page` (which resolves the inheritance chain and intersects with the MediaBox); `content`
// comes from `IDocumentBackend::contentBox()` and is absent for a page with no objects, for a page
// nobody has asked about yet, and for an engine that declined.
//
// A STRUCT RATHER THAN THREE PARAMETERS, for `RenderRequest`'s reason: three `PdfRect`s standing
// next to each other in a signature is two of them transposed compiling silently, and the
// transposition here is invisible on every document whose CropBox is its MediaBox -- which is most
// of them, and is precisely how I-28 survived a month.
struct PageBoxes {
    PdfRect media;
    PdfRect crop;
    std::optional<PdfRect> content;
};

// WHICH RECTANGLE `fit` MEASURES.
//
//   Page     -- the MEDIA box: the sheet, including any bleed and crop marks.
//   Visible  -- the CONTENT box, falling back to the CropBox when there is none (see the header).
//   Width,
//   Height,
//   None     -- the CROP box, which is what a reader draws.
//
// The rectangle rather than its size, because Fit Visible has to be SCROLLED to as well as zoomed
// to: a content box in the middle of a sheet that has been zoomed to fit is not on screen until
// somebody moves the viewport to it, and the caller needs the origin to do that.
PdfRect fitRect(const PageBoxes& boxes, FitMode fit) noexcept;

// The zoom that puts `box` -- already rotated into the orientation it will be presented in -- inside
// a viewport of `viewport` DEVICE pixels, allowing `margin` device pixels of gutter on each side.
//
// `physical` is the monitor's real DPI and not a nominal 96: that is the whole reason `DpiPolicy`
// takes three inputs, and a fit computed against 96 on a 150 % panel is wrong by exactly the scale
// factor -- which is Stage 2's **I-20** in a second place.
//
// Returns `current` unchanged for `FitMode::None`, for a viewport or a box with no area, and for
// any arithmetic that does not come out finite and positive. A viewer that divided by zero here
// would be a black window on a monitor we cannot obtain.
Zoom zoomToFit(FitMode fit, PdfSize box, DeviceSize viewport, double margin, PhysicalDpi physical,
               Zoom current) noexcept;

} // namespace rawpage::core::render

#endif // RAWPAGE_CORE_RENDER_FITPOLICY_H
