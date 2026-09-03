// pdf/cos/filters/AsciiHexFilter -- `/ASCIIHexDecode`. ISO 32000-1 7.4.2. Stage 2 T7.
//
// The simplest filter there is, and the one whose two tolerances matter: whitespace is legal
// ANYWHERE inside the data (producers wrap at 80 columns), and an ODD number of digits before the
// `>` terminator is padded with a trailing zero rather than being an error. That padding is in the
// specification, not a kindness -- and it is the same rule `pdf/cos/Lexer` applies to `<41A>`, one
// layer down, which is why both cite 7.3.4.3's wording.
//
// Lossless, both directions implemented, `encode`→`decode` is the identity.

#ifndef RAWPAGE_PDF_COS_FILTERS_ASCIIHEXFILTER_H
#define RAWPAGE_PDF_COS_FILTERS_ASCIIHEXFILTER_H

#include "pdf/cos/filters/Filter.h"

namespace rawpage::pdf::cos::filters {

// Never fails on a stray byte -- a non-hex, non-whitespace character before `>` is skipped, the
// way `pdf/cos/Lexer` skips one inside a hex string. It fails only on nothing at all to work with,
// which cannot happen from a stream and can from a caller.
core::Result<Bytes> asciiHexDecode(std::string_view input);

// Wraps at 64 characters and terminates with `>`, which is what every producer does and what makes
// the output diffable. The wrapping is not significant to the decoder.
core::Result<Bytes> asciiHexEncode(std::string_view input);

} // namespace rawpage::pdf::cos::filters

#endif // RAWPAGE_PDF_COS_FILTERS_ASCIIHEXFILTER_H
