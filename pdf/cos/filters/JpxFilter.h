// pdf/cos/filters/JpxFilter -- `/JPXDecode`. ISO 32000-1 7.4.9, ISO/IEC 15444-1. Stage 2 T7.
//
// OpenJPEG, not first-party. BSD-2-Clause, already in the tree through vcpkg's `codecs` feature,
// and on `Tech-Stack-Plan.md` §8.2's allowlist. Writing a JPEG 2000 decoder is a project, not a
// file: EBCOT, the MQ coder, tag trees, packet headers and five progression orders.
//
// DECODE ONLY, on the same terms as `DctFilter` and for the same reason: JPEG 2000 as PDF uses it
// is lossy, so guide case A-18's `encode`→`decode` identity is not a property the format has.
// `encodeFilter(FilterKind::Jpx, …)` returns `Unsupported` by name.
//
// -------------------------------------------------------------------------------------------
// TWO CONTAINERS, ONE FILTER
// -------------------------------------------------------------------------------------------
// `/JPXDecode` data is either a raw J2K codestream (starting `FF 4F FF 51`) or a JP2 box container
// (starting `00 00 00 0C 6A 50 20 20`). They need different OpenJPEG codecs, and handing the wrong
// one produces "not a valid codestream" on a perfectly good file. The magic decides.
//
// -------------------------------------------------------------------------------------------
// WHAT `corpus/ordinary/image-jpx.pdf` CAN AND CANNOT PROVE
// -------------------------------------------------------------------------------------------
// `corpus/jpxenc.py` implements the MQ coder and the EBCOT **cleanup pass only** — not
// significance propagation, not magnitude refinement — so the codestream it produces is
// *truncated* rather than malformed and a 0/255 source decodes back to roughly 0/128.
// `corpus/PROVENANCE.md` states this as a limitation of the CORPUS, and it is inherited here: the
// fixture proves this filter reaches OpenJPEG, hands it the right container and gets samples of
// the right shape back. **It is not a claim about JPEG 2000 fidelity**, and `tst_cos_filters`
// asserts the dimensions and the tone COUNT rather than the tone values, because asserting the
// values would be asserting the encoder's truncation.

#ifndef RAWPAGE_PDF_COS_FILTERS_JPXFILTER_H
#define RAWPAGE_PDF_COS_FILTERS_JPXFILTER_H

#include "pdf/cos/filters/Filter.h"

namespace rawpage::pdf::cos::filters {

struct JpxInfo {
    int width = 0;
    int height = 0;
    int components = 0;
    int bitsPerComponent = 0;
};

core::Result<Bytes> jpxDecode(std::string_view input);
core::Result<Bytes> jpxDecode(std::string_view input, JpxInfo& info);

} // namespace rawpage::pdf::cos::filters

#endif // RAWPAGE_PDF_COS_FILTERS_JPXFILTER_H
