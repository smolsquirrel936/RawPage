// The Windows `IFontResolver` -- what face stands in for a font a document did not embed, and
// which file on this machine backs it. Stage 2 T16.
//
// ---------------------------------------------------------------------------------------------
// THIS CLASS ANSWERS *WHAT IS INSTALLED*. IT DOES NOT DECIDE *WHAT TO PREFER*.
//
// `platform/win/` makes no policy decisions, and font substitution is mostly policy. So the
// ordered list of families to try lives in `core/render/FontSubstitution` -- pure, headless, and
// the same judgement on every operating system -- and this class walks that list against
// DirectWrite's system font collection, returning the first one the machine actually has.
//
// The consequence is the property guide case A-66 is about: the substitute a document gets is
// chosen by a table `tests/unit` can read with no display and no Windows, and this file only
// reports whether the chosen name exists here and what file it lives in.
//
// ---------------------------------------------------------------------------------------------
// WHY DIRECTWRITE AND NOT GDI, AND NOT QFontDatabase.
//
// `FontMatch::filePath` is the load-bearing field: PDFium's font hook is handed BYTES, so a match
// that cannot name a readable file is a match nobody can use. GDI can enumerate families and can
// hand back font data through a device context, but it cannot tell you which FILE a face came from
// -- so the caller would have to hold an HDC and an HFONT for the lifetime of the match, which is
// a per-request OS resource on the render thread. DirectWrite's `IDWriteLocalFontFileLoader`
// answers the path directly.
//
// `QFontDatabase` is out for a harder reason: it is QtGui. `platform/platform.h` is included by
// `core/`, and `core/` may not name a QtGui type (convention 3) -- and this boundary exists
// precisely so that the domain layer never acquires a display dependency. It is also the wrong
// database: Qt's answers are about the FAMILIES available to draw the user interface, and this is
// the different question of substituting for a face a PDF's author had and we do not.
//
// ---------------------------------------------------------------------------------------------
// WHAT A TrueType COLLECTION COSTS, said out loud rather than left to be found.
//
// Every CJK face on Windows is a `.ttc` -- MSJH.TTC, YuGothM.ttc, SIMSUN.TTC -- and a collection
// holds several faces in one file. `FontMatch::faceIndex` carries the index DirectWrite chose, and
// a caller that reads the whole file and hands it to a rasterizer WITHOUT that index gets face 0.
// For every family this project's policy selects, the regular weight IS face 0, so the two agree
// today -- but that is a property of the policy rather than of the format, and
// `pdf/render/FontResolverBridge` states the same limitation at the point where it matters.
//
// ---------------------------------------------------------------------------------------------
// THREAD SAFETY: `resolve()` IS CALLED FROM THE RENDER WORKER.
//
// PDFium asks for a substitute in the middle of rasterizing a page, which happens on the
// scheduler's worker thread and not on the UI thread. The DirectWrite factory is created
// `DWRITE_FACTORY_TYPE_SHARED` and its objects are documented as thread-safe, but the system font
// collection is REFRESHED here (a font may be installed while we run), and that refresh is a
// mutation. One mutex covers both, which costs nothing: this is called once per missing face per
// document, not once per glyph.

#ifndef RAWPAGE_PLATFORM_WIN_WINFONTRESOLVER_H
#define RAWPAGE_PLATFORM_WIN_WINFONTRESOLVER_H

#include "platform/platform.h"

#include <memory>
#include <mutex>

namespace rawpage::platform::win {

class WinFontResolver final : public IFontResolver {
public:
    WinFontResolver();
    ~WinFontResolver() override;

    // Exact family first; then `core::render::substitutionCandidates()` in order. `exact` is true
    // only for the first of those, and it is the field a caller reports on -- a document rendered
    // with a substitute looks fine and is not what its author saw.
    //
    // Fails with `NotFound` only when NOTHING in the candidate list is installed, which on a
    // Windows machine means Arial is missing. That is worth an error rather than an empty match:
    // the caller's alternative is PDFium's own internal fallback, and it needs to know it is
    // taking it.
    Result<FontMatch> resolve(const Request&) const override;

    // Every family in the system collection, sorted, for a diagnostic and for the case that proves
    // the collection was actually queried. Not used by the substitution path -- walking a
    // candidate list is a lookup, not a search.
    Result<QStringList> installedFamilies() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::mutex    mutex_;
};

} // namespace rawpage::platform::win

#endif // RAWPAGE_PLATFORM_WIN_WINFONTRESOLVER_H
