// pdf/cos/filters/DctFilter -- `/DCTDecode`. ISO 32000-1 7.4.8, ITU-T T.81. Stage 2 T7.
//
// libjpeg-turbo, not first-party. BSD-3-Clause, already in the tree through vcpkg's `codecs`
// feature, and on `Tech-Stack-Plan.md` §8.2's allowlist.
//
// DECODE ONLY, AND THAT IS A DECISION RATHER THAN AN OMISSION. Stage 2's `pdf/cos/` is a READER
// (D2-4). More to the point, guide case A-18's *"each `encode`→`decode` returns the input exactly"*
// **is not a property JPEG has** — the transform is lossy by construction, so an encoder here could
// not be held to that criterion by any test, and writing one would be untested infrastructure with
// no caller. `encodeFilter(FilterKind::Dct, …)` returns `Unsupported` **by name**, never an empty
// buffer. The divergence is recorded as a finding against A-18 rather than by quietly narrowing it.
//
// WHAT COMES OUT. The component samples, interleaved, 8 bits each, top row first — which is what
// `/DCTDecode` is defined to produce and what the image dictionary's `/ColorSpace` then interprets.
// No colour management, no `/Decode` array: those belong to the image pipeline, and this filter's
// job ends at the samples.
//
// THREE THINGS THAT ARE EASY TO GET WRONG, each handled here:
//
//   1. **libjpeg reports errors by longjmp**, and its default handler calls `exit()`. A PDF reader
//      that exits on a malformed image is not a reader. The error manager below replaces both.
//   2. **CMYK JPEGs written by Adobe are stored INVERTED.** The APP14 marker says so, and a
//      decoder that ignores it produces a photographic negative — a bug that looks like a colour
//      management problem and is not one.
//   3. **Progressive JPEGs need the whole buffer**, which we have: a stream is an offset and a
//      length, so `jpeg_mem_src` gets everything at once and there is no suspending source to get
//      wrong.

#ifndef RAWPAGE_PDF_COS_FILTERS_DCTFILTER_H
#define RAWPAGE_PDF_COS_FILTERS_DCTFILTER_H

#include "pdf/cos/filters/Filter.h"

namespace rawpage::pdf::cos::filters {

// What the codestream said about itself. Returned alongside the samples because a caller cannot
// interpret them without it, and re-parsing the header to find out would decode the image twice.
struct DctInfo {
    int width = 0;
    int height = 0;
    int components = 0;
    bool adobeInverted = false;   // CMYK stored inverted, per the APP14 marker
};

core::Result<Bytes> dctDecode(std::string_view input);
core::Result<Bytes> dctDecode(std::string_view input, DctInfo& info);

// The header alone, without decoding the image. `pdf/render/` never needs this -- PDFium rasterizes
// -- but Stage 8's image export does, and reading 30 bytes to answer "how big is it" beats
// decompressing forty megapixels.
core::Result<DctInfo> dctInspect(std::string_view input);

} // namespace rawpage::pdf::cos::filters

#endif // RAWPAGE_PDF_COS_FILTERS_DCTFILTER_H
