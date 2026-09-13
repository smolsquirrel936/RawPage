// pdf/render/FontResolverBridge -- PDFium's font hook, answered by OUR substitution policy instead
// of by the engine's own. Stage 2 T16.
//
// ===============================================================================================
// WHAT IT IS FOR, IN ONE SENTENCE A SUPPORT CALL WOULD USE.
// ===============================================================================================
//
// A PDF may name a font and not embed it. Somebody has to decide what to draw instead, and until
// this file existed **PDFium decided, silently, and nothing in the product could say which face it
// had picked**. That is the failure guide case **A-66** is written against: substitution policy on
// the wrong side of the engine boundary is invisible until a document looks wrong on a customer's
// machine and no log line names the font that was used.
//
// So the chain is: PDFium asks -> this file translates the ask into a
// `platform::IFontResolver::Request` -> `platform/win/WinFontResolver` walks
// `core/render/FontSubstitution`'s ordered candidate list against what the machine has -> the
// chosen face comes back here as a family, a file path and a face index -> PDFium is handed the
// BYTES. Every substitution is logged and the last one is readable, which is what lets a test NAME
// the substitute rather than assert that rendering did not crash.
//
// ===============================================================================================
// WHY IT IS STATIC, AND WHY THAT IS NOT A SHORTCUT.
// ===============================================================================================
//
// `FPDF_SetSystemFontInfo` installs a hook into PDFium's PROCESS-GLOBAL state -- the same state
// `PdfiumInit` owns and the same state whose thread-unsafety was measured at T3 (**I-05**: what
// PDFium shares between threads is its font mapper and its glyph caches, which is precisely this).
// There is one engine per process and therefore exactly one of these. An instance per document
// would offer a second one, and the second would silently replace the first the way
// `core/Log.h`'s single sink does (convention 18).
//
// It also has to outlive every `FPDF_DOCUMENT`: PDFium keeps the pointer and calls back into it
// during rendering, so a bridge owned by a document would be a dangling callback the moment that
// tab closed. `uninstall()` exists for teardown and `app/CompositionRoot` calls it BEFORE the
// platform bundle that owns the resolver is destroyed -- an order that is stated at the call site
// as well as here, because getting it wrong is a use-after-free inside the engine.
//
// ===============================================================================================
// THE BASE-14 FONTS DO REACH THIS FILE, AND DECLINING THEM IS THE WHOLE OF ISSUE I-13.
// ===============================================================================================
//
// This section said the opposite when it was first written, on the reasoning that Helvetica, Times,
// Courier, Symbol and ZapfDingbats are required of every conforming reader and PDFium carries its
// own metric-compatible Foxit copies -- so a hook would never be asked about them. **PDFium asks
// about them.** The `FPDF_SYSFONTINFO` hook is consulted for EVERY unembedded font and the engine
// falls back to its own faces only when the hook declines. The first run of the control case in
// `tests/render/tst_fontbridge.cpp` therefore found the product substituting **Segoe UI for
// Helvetica** on every page of every corpus file -- a change to how every existing document renders,
// shipped by a task whose subject was documents that render *wrongly today*.
//
// So `bridgeMapFont` declines the standard fourteen by name and lets PDFium use its own. That is
// the right answer rather than merely the safe one: the base 14 are not missing fonts, PDFium's
// copies are metric-compatible where a system face is only approximately so, and they are identical
// on every machine -- which is what `tests/golden/` depends on at T17.
//
// The finding is also a lesson about the case that caught it. A-66 asks whether the bridge RUNS;
// the control asking whether it runs when it should NOT is what found the defect, and nothing in
// A-66's own wording called for it (convention 7).
//
// `corpus/ordinary/font-not-embedded.pdf` is the fixture for the positive half: two families that
// are on no machine anywhere, each with a `/FontDescriptor` and no `/FontFile`, which is the shape a
// real producer emits when the author's font was not licensed for embedding.
//
// ===============================================================================================
// THE LIMIT THIS FILE HAS, STATED WHERE IT BITES.
// ===============================================================================================
//
// PDFium asks for a face's data with a TABLE TAG or with 0 for the whole file. For a table tag the
// answer is exact, including inside a TrueType Collection, because the collection's directory is
// walked with the face index the resolver chose. For 0 -- which is what PDFium asks for in the
// ordinary path -- the whole file is handed over, and a `.ttc` therefore reaches the engine's
// FreeType as face 0. Every family `core/render/FontSubstitution` selects has its regular weight at
// index 0, so the two agree today; that is a property of the policy and not of the format, and it
// is written here rather than left for whoever first adds a candidate where it is untrue.

#ifndef RAWPAGE_PDF_RENDER_FONTRESOLVERBRIDGE_H
#define RAWPAGE_PDF_RENDER_FONTRESOLVERBRIDGE_H

#include "core/Result.h"
#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <QString>

namespace rawpage::pdf::render {

class GlyphCache;

class FontResolverBridge {
public:
    // FOUR OUTCOMES, NOT A BOOLEAN, and the two that hand nothing back are the reason.
    //
    // A row with an empty `chosen` has two completely different meanings -- *we declined, because
    // the engine's own copy of a standard face is better than a system one* and *nothing on this
    // machine could stand in* -- and a support reader who cannot tell them apart will chase the
    // wrong one. `exact` versus `substituted` is the other distinction, and it is the one that says
    // how faithful the page on screen is to what its author saw.
    enum class Outcome {
        Exact,             // the family the document named is installed here and was used
        Substituted,       // something else was chosen; `chosen` names it
        StandardDeclined,  // one of the base 14 -- handed back to PDFium's own metric-compatible
                           // copy on purpose (see FontResolverBridge.cpp, issue I-13)
        NoneAvailable,     // every candidate was absent; the engine's internal fallback took over
    };

    // One row per font PDFium asked us to map.
    struct Substitution {
        QString requested;   // the /BaseFont as PDFium passed it, before normalization
        QString normalized;  // what we actually looked up: subset tag and style suffixes removed
        QString chosen;      // the family handed back; empty for the two outcomes that hand nothing
        QString filePath;
        int     faceIndex = 0;
        Outcome outcome = Outcome::NoneAvailable;

        // The row's place among EVERY mapping this process performed, starting at 0 and never
        // reused -- which stays meaningful after older rows have been dropped (see `substitutions()`).
        std::uint64_t sequence = 0;
    };

    // Installs the hook. The resolver is borrowed, NOT owned: it belongs to the platform bundle,
    // and `uninstall()` must run before that bundle is destroyed.
    //
    // Calling it twice replaces the hook, and says so in the log rather than silently -- a second
    // installer means two things believe they own the process's font mapping, which is worth a line
    // in a support log even though the outcome is well defined.
    //
    // Fails with `Unavailable` if PDFium could not be initialized, because a hook installed into an
    // engine that is not running would be accepted and never called.
    static core::Status install(const platform::IFontResolver& resolver);

    // Removes the hook and drops the borrowed resolver. Safe when nothing was installed, and safe
    // twice. PDFium is told explicitly (`FPDF_SetSystemFontInfo(nullptr)`), because leaving a
    // pointer to freed memory in the engine's globals is exactly the crash this method exists for.
    static void uninstall();

    static bool isInstalled();

    // ------------------------------------------------------------------------------------------
    // WHAT A TEST -- AND A SUPPORT LOG -- CAN READ BACK.
    //
    // The whole point of moving this decision to our side of the boundary is that the answer is
    // now observable. `substitutions()` is the most recent `kLogCapacity` mappings, oldest first;
    // `lastSubstitution()` is the convenience for the common case.
    //
    // BOUNDED, and it was not until Stage 2 issue I-35. PDFium asks about every unembedded font a
    // document loads -- the base-14 faces it declines included -- so an append-only log gained a
    // row of four QStrings for every ordinary document opened, for the life of the process.
    //
    // A caller that wants only the rows ITS work produced takes `substitutionCount()` BEFORE the
    // work and reads `substitutionsSince()` after. Counting positions in `substitutions()` stops
    // working the moment the log is full -- every later window would read as empty, silently --
    // which is why the golden suite no longer does it.
    static constexpr std::size_t kLogCapacity = 256;
    static std::vector<Substitution> substitutions();
    static std::optional<Substitution> lastSubstitution();
    static void resetLog();

    // How many mappings this process has EVER recorded -- the next row's `sequence`. Not reset by
    // `resetLog()`, so a mark taken before a reset still means the same thing after it.
    static std::uint64_t substitutionCount();

    // The retained rows whose `sequence` is at least `mark`, oldest first. A window that produced
    // more than `kLogCapacity` rows returns only the newest of them; `substitutionCount() - mark`
    // says how many there were.
    static std::vector<Substitution> substitutionsSince(std::uint64_t mark);

    // How many times PDFium asked us to map a font. **A second tile of the same page adds
    // nothing**: PDFium resolves a font when it loads it, not when it draws it -- which is the half
    // of guide case A-67's claim that lives on this side of the boundary. See `GlyphCache.h` for
    // why the other half does not.
    static std::size_t mapFontCalls();

    // The face-data cache the hook reads through. Vended so the suite can read its counters; there
    // is no setter, because a second cache would mean two answers to "has this face been read".
    static GlyphCache& glyphCache();
};

} // namespace rawpage::pdf::render

#endif // RAWPAGE_PDF_RENDER_FONTRESOLVERBRIDGE_H
