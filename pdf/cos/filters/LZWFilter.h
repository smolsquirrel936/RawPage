// pdf/cos/filters/LZWFilter -- `/LZWDecode`. ISO 32000-1 7.4.4. Stage 2 T7.
//
// PDF's variant of TIFF LZW: MSB-first codes of 9 to 12 bits, 256 = clear table, 257 = EOD, first
// assignable code 258.
//
// -------------------------------------------------------------------------------------------
// THE `EarlyChange` OFF-BY-ONE, WHICH IS THE ONLY HARD PART -- AND WHICH WAS MEASURED
// -------------------------------------------------------------------------------------------
// `/EarlyChange` defaults to **1**, meaning the code width grows one code sooner than the table
// strictly requires: 9 bits become 10 when the next assignable code reaches **511**, not 512. Get
// it wrong and a stream decodes correctly for its first few hundred codes and then turns to noise
// — which reads like a corrupt file rather than like a decoder bug, and is why this comment
// carries the boundary rather than leaving it to be re-derived.
//
// The boundary here was **measured against `corpus/pdfbuild.py`'s encoder rather than reasoned
// about**, because the decoder is always one table entry BEHIND the encoder and that asymmetry
// makes the two conditions look different while being the same rule. Empirically, and asserted by
// `tst_cos_filters` over `corpus/ordinary/image-lzw-ascii85.pdf`:
//
//   decoder: after assigning an entry, grow when `nextCode + earlyChange >= (1 << width)`
//   encoder: after assigning an entry, grow when `nextCode + earlyChange >  (1 << width)`
//
// A decoder using the encoder's `>` fails on the corpus stream at the first boundary crossing;
// that was run, not assumed.
//
// Lossless, both directions implemented, `encode`→`decode` is the identity.

#ifndef RAWPAGE_PDF_COS_FILTERS_LZWFILTER_H
#define RAWPAGE_PDF_COS_FILTERS_LZWFILTER_H

#include "pdf/cos/filters/Filter.h"

namespace rawpage::pdf::cos::filters {

core::Result<Bytes> lzwDecode(std::string_view input, int earlyChange = 1);
core::Result<Bytes> lzwEncode(std::string_view input, int earlyChange = 1);

} // namespace rawpage::pdf::cos::filters

#endif // RAWPAGE_PDF_COS_FILTERS_LZWFILTER_H
