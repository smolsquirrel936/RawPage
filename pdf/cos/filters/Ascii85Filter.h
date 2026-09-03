// pdf/cos/filters/Ascii85Filter -- `/ASCII85Decode`. ISO 32000-1 7.4.3. Stage 2 T7.
//
// Base-85 in groups of four bytes to five characters, `z` as shorthand for four zero bytes, `~>`
// as the terminator. Three things are easy to get wrong and each has a case:
//
//   1. THE PARTIAL FINAL GROUP. A group of n < 5 characters is padded to five with `u` (84) and
//      then n-1 bytes are kept. Padding with anything else, or keeping n bytes, corrupts the last
//      one to three bytes of every stream whose length is not a multiple of four -- which is most
//      of them, and which is invisible until someone looks at the end of a decoded image.
//   2. `z` IS ONLY LEGAL AT A GROUP BOUNDARY. Inside a partial group it is a malformed stream, and
//      accepting it silently shifts everything after it.
//   3. THE LEADING `<~`. The specification does not have it; Adobe's own PostScript convention
//      does, and producers emit it. Skipped when present.
//
// Lossless, both directions implemented, `encode`→`decode` is the identity.

#ifndef RAWPAGE_PDF_COS_FILTERS_ASCII85FILTER_H
#define RAWPAGE_PDF_COS_FILTERS_ASCII85FILTER_H

#include "pdf/cos/filters/Filter.h"

namespace rawpage::pdf::cos::filters {

core::Result<Bytes> ascii85Decode(std::string_view input);
core::Result<Bytes> ascii85Encode(std::string_view input);

} // namespace rawpage::pdf::cos::filters

#endif // RAWPAGE_PDF_COS_FILTERS_ASCII85FILTER_H
