// core/doc/PageTransitions -- the document's own presentation instructions, read. Stage 4 T10;
// decision **D4-12** (`Stage4-ViewingSurface-Plan.md` §3).
//
// ===========================================================================================
// THE FIRST READ IN THIS PROJECT THAT PDFIUM CANNOT DO, AND THAT IS WHY IT IS RECORDED HERE.
// ===========================================================================================
// **D4-3 settled that the outline, the destinations and the Info dictionary are read THROUGH
// PDFIUM** -- *"we read through PDFium and write through our own code"* -- because `FPDFBookmark_*`,
// `FPDFDest_*` and `FPDF_GetMetaText` already resolve the inherited and indirect cases, and a
// second first-party walk would be a second reader to keep in agreement.
//
// **There is no `FPDF` call for `/Trans` at all.** The engine's public C API exposes page
// transitions nowhere (`FPDFPage_HasTransparency` is a different subject with a similar name), so
// this model is hydrated from the FIRST-PARTY page walk instead -- the one
// `pdf/DocumentBackendImpl::open` already runs on every document, editable or not, to check that
// our reading of the page tree agrees with the engine's. **It therefore costs no second parse.**
//
// That is a genuine deviation from D4-3 and it is narrow: the rule is *read through PDFium*, and
// the exception is *unless PDFium cannot*. It does not open the door to re-reading the outline
// first-party, because PDFium can read that.
//
// ===========================================================================================
// A DOCUMENT'S REQUEST, NOT THE PRODUCT'S SETTING.
// ===========================================================================================
// Everything here is what the FILE asks for. `fullScreen/transition` and
// `fullScreen/autoAdvanceSec` are what the PERSON asks for, and `ui/viewer/FullScreenController`
// is what resolves the two -- per-page first, preference second, which is the override
// `Features.md` §1.2 promises and guide case A-61 asserts. Nothing here reads a setting, so this
// model says the same thing on every machine.
//
// ===========================================================================================
// EVERY FIELD IS OPTIONAL BECAUSE ABSENCE IS THE COMMON CASE AND IS NOT A DEFAULT.
// ===========================================================================================
// Almost no PDF carries a `/Trans`, and a page without one must be distinguishable from a page
// that explicitly asks for `/S /R`: the first falls through to the preference, the second means
// *cut, whatever the preference says*. A model that stored `TransitionStyle::None` for both would
// make the override unimplementable -- which is why `transition` is a `std::optional` and its
// `style` is not.
//
// `corpus/ordinary/transitions.pdf` is built for exactly this: three pages carry a `/Trans`, two a
// `/Dur`, and **page 4 carries neither**, which is the control the override assertion needs.

#ifndef RAWPAGE_CORE_DOC_PAGETRANSITIONS_H
#define RAWPAGE_CORE_DOC_PAGETRANSITIONS_H

#include "core/Transition.h"

#include <QString>

#include <optional>
#include <vector>

namespace rawpage::core {

// One page's `/Trans`, as the file wrote it.
struct PageTransition {
    // What we will DRAW. A `/S` this product has no animation for reads as `None` -- see
    // `core/Transition.h` for why that is reported rather than approximated.
    TransitionStyle style = TransitionStyle::None;

    // What the FILE said, verbatim and without the slash: "Wipe", "Split", "Glitter". Kept beside
    // `style` so that a page whose style we cannot draw can be named in the log and counted by a
    // later stage, rather than being indistinguishable from a page that asked for a cut.
    // **Empty only for a `/Trans` with no `/S` at all**, which ISO 32000-1 12.4.4 says defaults to
    // `/R`.
    QString rawStyle;

    // `/D`, the transition's duration in SECONDS -- how long the animation takes. Absent means the
    // file did not say; ISO's default is 1. Not to be confused with `PageTiming::duration`, which
    // is how long the page is SHOWN: `/D` and `/Dur` are different keys in different dictionaries
    // and confusing them makes a one-second fade into a one-second slide.
    std::optional<double> durationSeconds;

    // `/Dm` (`/H` or `/V`) and `/M` (`/I` or `/O`) and `/Di` (a number, or `/None`). Carried as
    // read because only `Wipe` and `Split` use them and the controller decides what to do with
    // them; a model that normalized them would be deciding for it.
    QString dimension;
    QString motion;
    QString direction;

    // Whether this page's style is one we can draw. `false` means `style` is `None` because we
    // have no animation for `rawStyle`, NOT because the file asked for a cut.
    bool supported = true;
};

// One page's `/Dur` -- how long the page is displayed before auto-advance moves on.
struct PageTiming {
    double seconds = 0.0;
};

class PageTransitions {
public:
    // How many pages this model was hydrated over. Every accessor below takes a page INDEX and
    // answers `nullopt` for one outside it, so a caller never has to range-check first.
    int pageCount() const noexcept { return static_cast<int>(transitions_.size()); }

    // This page's `/Trans`, or nothing. **Nothing is not `None`** -- see the header.
    const PageTransition* at(int pageIndex) const noexcept;

    // This page's `/Dur`, or nothing. A page with a `/Dur` auto-advances after that many seconds
    // EVEN WHEN `fullScreen/autoAdvanceSec` differs, which is the same override shape as `/Trans`.
    const PageTiming* timingAt(int pageIndex) const noexcept;

    // `/PageMode /FullScreen` in the catalog: the document asks to be OPENED as a presentation.
    // Read and reported; **acting on it is not Stage 4's** -- opening a file full-screen because
    // it said so is a decision about a person's window that needs a preference to refuse it, and
    // no declared key holds one. `Features.md` lists no such capability either.
    bool opensFullScreen() const noexcept { return opensFullScreen_; }

    // How many pages carried a `/Trans` whose `/S` this product cannot draw. Zero for almost every
    // document; non-zero is what a log line and guide case A-61 report.
    int unsupportedCount() const noexcept;

    // ---- Hydration, called by `pdf/` and by tests ---------------------------------------------
    void setPageCount(int count);
    void setTransition(int pageIndex, const PageTransition& transition);
    void setTiming(int pageIndex, const PageTiming& timing);
    void setOpensFullScreen(bool value) noexcept { opensFullScreen_ = value; }

    bool isEmpty() const noexcept;

private:
    std::vector<std::optional<PageTransition>> transitions_;
    std::vector<std::optional<PageTiming>>     timings_;
    bool                                       opensFullScreen_ = false;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_PAGETRANSITIONS_H
