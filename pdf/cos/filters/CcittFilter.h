// pdf/cos/filters/CcittFilter -- `/CCITTFaxDecode`. ISO 32000-1 7.4.6, ITU-T T.4 and T.6.
// Stage 2 T7.
//
// FIRST-PARTY, AND THAT IS FORCED RATHER THAN CHOSEN. The obvious library is libtiff, which is
// already in the tree -- but its fax decoder is reachable only through a `TIFF*` handle over a real
// or fabricated TIFF file, not as a function from bytes to bytes, and fabricating a TIFF header
// around every PDF image stream to borrow a decoder is more code than the decoder. So T.4 and T.6
// are implemented here, against the same code tables `corpus/ccitt.py` encodes with -- which is why
// the corpus generator carries an INDEPENDENT decoder of its own for its self-check: two
// implementations that agree because one was written by inverting the other prove nothing
// (CLAUDE.md convention 7).
//
// A scanned page is the most common thing a PDF reader is handed that is not text, so this is not
// an exotic path.
//
// -------------------------------------------------------------------------------------------
// THE THREE CODINGS `/K` SELECTS, and which have a corpus fixture
// -------------------------------------------------------------------------------------------
//   K  < 0   Group 4 (T.6, Modified Modified READ). Every row coded against the row above in pass,
//            horizontal and vertical modes. What real fax and scanner output uses.
//            → `corpus/ordinary/image-ccitt-g4.pdf`
//   K == 0   Group 3 one-dimensional (T.4, Modified Huffman). Every row independent.
//            → `corpus/ordinary/image-ccitt-g3.pdf`
//   K  > 0   Group 3 mixed: a one-dimensional row, then up to K-1 rows that are each 1-D or 2-D
//            according to a flag bit after the EOL. **NO CORPUS FIXTURE EXISTS** for this one. It
//            is implemented because it is the same machinery as the other two plus one bit, and it
//            is named here as untested-by-fixture rather than left for a reader to assume covered
//            (convention 19). The honest fix is a `K > 0` corpus file, which is T7's own finding.
//
// -------------------------------------------------------------------------------------------
// `/BlackIs1` IS A POLARITY, AND ITS DEFAULT IS THE SURPRISING ONE
// -------------------------------------------------------------------------------------------
// The default is **false**, which means a BLACK pixel comes out as a **0** bit and white as 1 --
// the opposite of what "1 means ink" intuition suggests, and the opposite of what
// `corpus/ordinary/image-ccitt-g4.pdf` asks for (it sets `/BlackIs1 true`). Getting this backwards
// produces a perfect photographic negative, which looks like a colour-space bug and is not one.
//
// OUTPUT. One bit per pixel, packed MSB-first, **each row padded to a byte boundary** -- so a
// 1728-pixel row is 216 bytes and a 100-pixel row is 13, not 12.5 rounded somewhere later.
//
// DECODE ONLY. CCITT is lossless, so an encoder COULD be held to A-18's round-trip -- but Stage 2
// is a reader (D2-4), the corpus already has an encoder in Python for generating fixtures, and a
// second one here would have no caller until Stage 12's export. `encodeFilter` says so by name.

#ifndef RAWPAGE_PDF_COS_FILTERS_CCITTFILTER_H
#define RAWPAGE_PDF_COS_FILTERS_CCITTFILTER_H

#include "pdf/cos/filters/Filter.h"

namespace rawpage::pdf::cos::filters {

// Uses `parms.k`, `parms.columnsCcitt`, `parms.rows`, `parms.blackIs1` and `parms.byteAlign`.
//
// TOLERANT, in the same sense the rest of `pdf/cos/` is: a row that cannot be decoded ends the
// image rather than failing it, because the rows that DID decode are worth more than nothing --
// which is the whole argument `FlateFilter` makes about truncation, and the reason a damaged scan
// still shows its top half.
core::Result<Bytes> ccittDecode(std::string_view input, const DecodeParms& parms);

} // namespace rawpage::pdf::cos::filters

#endif // RAWPAGE_PDF_COS_FILTERS_CCITTFILTER_H
