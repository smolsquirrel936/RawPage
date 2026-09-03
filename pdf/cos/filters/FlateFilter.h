// pdf/cos/filters/FlateFilter -- `/FlateDecode`. ISO 32000-1 7.4.4. Stage 2 T7.
//
// THE MOST IMPORTANT FILTER IN THE READER, and not because of images. Cross-reference streams and
// object streams are Flate-compressed in every PDF 1.5-or-later file, so **the reader cannot find a
// single object in a modern document without this one**. `pdf/cos/XrefTable` calls it directly.
//
// zlib, not first-party. It is the one codec in the nine where writing our own would be pure
// risk: `inflate` is the most-tested decompressor in existence, it is already in the tree (vcpkg's
// `codecs` feature, `Zlib` licence, on `Tech-Stack-Plan.md` §8.2's allowlist), and PDFium links it
// too. What IS first-party is everything around it -- the tolerance for a truncated stream, and
// the predictor.
//
// TWO TOLERANCES, both measured against real producers rather than imagined:
//
//   1. A TRUNCATED STREAM YIELDS WHAT IT HAD. `Z_BUF_ERROR` / `Z_DATA_ERROR` partway through is not
//      an empty result: `corpus/malformed/truncated-mid-stream.pdf` is an interrupted download, and
//      the page that decodes from the first 90 % of its content stream is worth more than nothing.
//      A stream that produced NO output at all is a real failure and is reported as one.
//   2. A MISSING zlib HEADER IS RETRIED RAW. Some producers emit a bare deflate stream with no
//      two-byte zlib wrapper. `inflateInit2` with a negative window bit count reads exactly that,
//      so the retry costs one call and rescues a file class that would otherwise be blank.
//
// Lossless, both directions implemented, `encode`→`decode` is the identity.

#ifndef RAWPAGE_PDF_COS_FILTERS_FLATEFILTER_H
#define RAWPAGE_PDF_COS_FILTERS_FLATEFILTER_H

#include "pdf/cos/filters/Filter.h"

namespace rawpage::pdf::cos::filters {

// The predictor is NOT applied here. `/DecodeParms` belongs to the filter chain rather than to the
// compressor, and `decodeFilter()` in `Filter.cpp` applies it afterwards -- which is also what lets
// LZW share exactly the same code path.
core::Result<Bytes> flateDecode(std::string_view input);
core::Result<Bytes> flateEncode(std::string_view input);

} // namespace rawpage::pdf::cos::filters

#endif // RAWPAGE_PDF_COS_FILTERS_FLATEFILTER_H
