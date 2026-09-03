// pdf/cos/filters/RunLengthFilter -- `/RunLengthDecode`. ISO 32000-1 7.4.5. Stage 2 T7.
//
// A length byte then data: 0..127 means "the next n+1 bytes are literal", 129..255 means "the next
// byte, repeated 257-n times", and 128 is EOD. The encoding is worth stating precisely because the
// two ranges are asymmetric and the off-by-one goes the other way in each: a literal run of 1 is
// length byte 0, and a repeat of 2 is length byte 255.
//
// Lossless, both directions implemented, `encode`→`decode` is the identity. `encode` is not
// required to be optimal and is not: it emits a repeat only for runs of 3 or more, because a
// two-byte run costs the same either way and preferring literals keeps runs mergeable.

#ifndef RAWPAGE_PDF_COS_FILTERS_RUNLENGTHFILTER_H
#define RAWPAGE_PDF_COS_FILTERS_RUNLENGTHFILTER_H

#include "pdf/cos/filters/Filter.h"

namespace rawpage::pdf::cos::filters {

core::Result<Bytes> runLengthDecode(std::string_view input);
core::Result<Bytes> runLengthEncode(std::string_view input);

} // namespace rawpage::pdf::cos::filters

#endif // RAWPAGE_PDF_COS_FILTERS_RUNLENGTHFILTER_H
