// core/render/FontSubstitution -- WHICH face stands in for a font a document names and does not
// embed. Stage 2 T16.
//
// ---------------------------------------------------------------------------------------------
// WHY THE POLICY IS HERE AND NOT IN platform/win/.
//
// `platform/win/` "implements platform/platform.h and nothing else -- it makes NO policy
// decisions" (tools/layer-lint/rules.json, and Tech-Stack-Plan.md 4.3). *Is Times New Roman
// installed, and what file backs it* is an OS question. *A missing serif Traditional-Chinese face
// should be tried as PMingLiU, then MingLiU, then Microsoft JhengHei* is a judgement about
// typography, and it is the same judgement on every operating system this product will ever run
// on. Putting it in `WinFontResolver` would make the policy invisible to `tests/unit`, untestable
// without a display, and quietly different the day a second platform appears.
//
// So this file is the ordered candidate LIST and `platform::IFontResolver` walks it against what
// the machine actually has. That split is also what makes guide case A-66's claim -- *our* policy
// rather than PDFium's -- something a test can name rather than infer.
//
// ---------------------------------------------------------------------------------------------
// WHY A LIST AND NOT A FUNCTION RETURNING ONE NAME.
//
// Every candidate can be absent. A Windows N edition ships without the CJK faces; a stripped
// container image has neither Times New Roman nor Arial; a server SKU may have no CJK support
// installed at all. A resolver that computed one answer and failed would substitute nothing on
// exactly the machines where substitution matters most. The list ends with faces present in every
// Windows install since Vista, and `lastResortFamily()` names the final one separately so a caller
// can say *"we fell all the way through"* rather than *"we chose Arial"*, which are different
// facts about the document.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS IS NOT.
//
// It is NOT metric compatibility. Substituting Arial for Helvetica happens to be close; Arial for
// Garamond is not, and no table of family names fixes that -- the page reflows. PDF's answer to
// that problem is the /FontDescriptor's widths, which PDFium already honours by scaling the
// substitute's glyphs to the declared /Widths. What this file decides is which OUTLINES appear;
// the metrics come from the document.
//
// It is also NOT the base-14 story, and **that half was measured wrong first**. Helvetica, Times,
// Courier, Symbol and ZapfDingbats are required of every conforming reader and PDFium carries its
// own metric-compatible Foxit copies of all fourteen -- so the assumption written here at first was
// that a document naming Helvetica would never reach a font hook. **It does.** PDFium asks the
// installed hook for EVERY unembedded font, standard or not, and only falls back to its own faces
// when the hook declines. The first run of `tst_fontbridge` therefore found the product
// substituting Segoe UI for Helvetica on every page of every corpus file (issue **I-13**).
//
// `isStandardFontName()` below is what declines them, and declining is the right answer rather
// than merely the conservative one: the base 14 are not missing fonts -- they are fonts the reader
// is OBLIGED to have -- and PDFium's copies are metric-compatible and identical on every machine,
// which is what `tests/golden/` will depend on at T17.

#ifndef RAWPAGE_CORE_RENDER_FONTSUBSTITUTION_H
#define RAWPAGE_CORE_RENDER_FONTSUBSTITUTION_H

// **THE FIRST FILE IN `core/` TO INCLUDE THIS HEADER**, which is worth noticing even though the
// rule has always allowed it: `core/`'s permitted outward look is its two interface headers,
// `core/backend.h` and this one, and `tools/layer-lint`'s `core` layer has listed
// `platform/platform.h` in `mayInclude` since Stage 0. It compiles on Linux for the
// `portable-asan` preset because `platform/platform.h` is interfaces only and names nothing but
// `core/` types and QtCore -- a property that file has its own layer entry, its own OS-header
// deny list and its own fixture to keep true (issue I-07 is what happens when it slips).
//
// It is included for exactly one type: `IFontResolver::Request`. Declaring a parallel request
// struct here would be the two-lists shape `core/settings/SettingsKeys.def`'s rule 2 exists to
// prevent, for a struct whose every field would have to be copied across at every call.
#include "platform/platform.h"

#include <QString>
#include <QStringList>

namespace rawpage::core::render {

// The families to try, in order, for a font the document names and does not embed.
//
// Always non-empty, and always ends with `lastResortFamily()`. Those two are the guarantees a
// caller may rely on, and the second is why a third one that looks obvious is deliberately absent:
// **the requested family is not excluded from its own candidate list.** Excluding it was written
// here first, on the reasoning that asking for it is the resolver's first move and repeating it
// makes "did we substitute" ambiguous -- and it conflicts with the guarantee above the moment a
// document asks for Arial on a machine that does not have it, where the filter would remove the
// last resort and leave nothing to try. Keeping it costs one lookup that is already known to miss;
// dropping it costs the page. (Found by the case in `tests/unit/tst_fontsubstitution.cpp`, which
// was written to assert the invariant and measured that it does not hold.)
//
// The candidate list is a function of the request's ATTRIBUTES and not of its name -- serif, pitch,
// symbolic, language -- so it says nothing about the family that was asked for either way.
QStringList substitutionCandidates(const platform::IFontResolver::Request& request);

// The face tried when everything else is absent. Arial: present in every Windows install since
// Windows 95, and the one name that has never not been there.
QString lastResortFamily();

// Whether this is one of ISO 32000-1's fourteen standard faces, which a conforming reader must
// have and which therefore must NOT be substituted with a system font. Takes the NORMALIZED name:
// "Times-Bold" is Times and "Helvetica-Oblique" is Helvetica, and both are standard.
//
// The check is on the family rather than on all fourteen full names, because the style half has
// already been stripped by the time anybody asks -- and because a producer writing "Times-Semibold"
// means Times with a weight, not a fifteenth face.
bool isStandardFontName(const QString& normalizedFamily);

// A PDF `/BaseFont` name as it appears in the file, reduced to a family a font database can be
// asked about. Three transformations, each of which a real producer emits:
//
//   "ABCDEF+Garamond-Bold"  ->  "Garamond"     a subset tag, six capitals and a plus
//   "TimesNewRomanPSMT"     ->  "TimesNewRoman" a PostScript name with its MT/PS suffixes
//   "Arial,BoldItalic"      ->  "Arial"        the comma form TrueType producers use
//
// The style words are stripped rather than parsed, because the weight and the slant reach us
// separately through the /FontDescriptor -- a name that says "Bold" and a descriptor that says 400
// disagree, and the descriptor is the one the document's author set deliberately.
QString normalizeBaseFontName(const QString& baseFont);

} // namespace rawpage::core::render

#endif // RAWPAGE_CORE_RENDER_FONTSUBSTITUTION_H
