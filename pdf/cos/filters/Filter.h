// pdf/cos/filters/Filter -- what every stream filter has in common. Stage 2 T7.
//
// SCOPE. The filter KIND vocabulary, the `/DecodeParms` a filter can be given, the predictor that
// two of them share, and the registry that turns a `/Filter` entry into a call. The nine filters
// themselves are one file each, exactly as `Architecture-FileMap.md` §3.9 lists them.
//
// NINE, NOT TEN. `CryptFilter` is Stage 10's, with the `StandardSecurityHandler` whose key it needs
// (D2-4) -- a crypt filter built here would have nothing to decrypt with, could not be tested, and
// would be rewritten. Guide case **A-19** asserts its absence rather than trusting the comment.
//
// -------------------------------------------------------------------------------------------
// WHY FREE FUNCTIONS AND NOT A CLASS HIERARCHY
// -------------------------------------------------------------------------------------------
// A filter is a pure function from bytes to bytes. It has no state to hold between calls, nothing
// to configure at construction, and exactly one implementation each -- so a virtual interface would
// buy a vtable and an ownership question in exchange for nothing. `FilterKind` plus a dispatch in
// `Filter.cpp` gives the registry its one place to fail on an unknown name, and keeps each filter
// testable as the pure function it is.
//
// -------------------------------------------------------------------------------------------
// WHAT "ROUND-TRIPS" MEANS PER FILTER, because it is NOT the same for all nine
// -------------------------------------------------------------------------------------------
// Guide case A-18 asks that each filter's `encode` → `decode` return the input exactly. That is
// satisfiable for the six **lossless** filters and is not a property the three image codecs have:
//
//   * `AsciiHex`, `Ascii85`, `RunLength`, `LZW`, `Flate` -- fully reversible, both directions
//     implemented, `encode`→`decode` is the identity. These are the STRUCTURAL filters: object
//     streams, cross-reference streams and content streams are compressed with them, so the reader
//     cannot open a modern PDF without them.
//   * `Ccitt` -- lossless for bilevel data, but this stage implements DECODE only; see its header.
//   * `Dct` and `Jpx` -- **lossy by construction**. An `encode`→`decode` identity is not merely
//     unimplemented here, it is not a property JPEG or JPEG 2000 has, and a case asserting it would
//     be asserting something false. What IS asserted is the other half of A-18: a decode of a
//     corpus-sourced stream matches its expected bytes.
//   * `Jbig2` -- see its own header. It is the one filter with neither a permissible library nor a
//     corpus fixture, and it says so rather than pretending.
//
// That divergence is recorded as a finding against A-18 rather than resolved by narrowing what the
// case asks (`Stage2-Rendering-Plan.md` §12's rule about overrun).
//
// WHY std AND NOT Qt: `pdf/cos/` is portable BY RULE, and these are byte pipelines. `core::Result`
// appears here because a filter CAN fail -- a truncated Flate stream is a real outcome a caller
// acts on -- which is exactly the boundary `pdf/cos/Lexer.h` predicted it would appear at.

#ifndef RAWPAGE_PDF_COS_FILTERS_FILTER_H
#define RAWPAGE_PDF_COS_FILTERS_FILTER_H

#include "core/Result.h"
#include "pdf/cos/CosObject.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rawpage::pdf::cos {

using Bytes = std::vector<std::uint8_t>;

enum class FilterKind : std::uint8_t {
    AsciiHex,
    Ascii85,
    LZW,
    Flate,
    RunLength,
    Ccitt,
    Dct,
    Jpx,
    Jbig2,
};

// `/DecodeParms`, flattened. A plain struct rather than a `CosObject`, so every filter is testable
// without building a dictionary -- and so a filter cannot reach for a key nobody documented.
// `decodeParmsFrom()` below is the one place that knows the spelling of each key.
struct DecodeParms {
    // Predictor, shared by Flate and LZW (ISO 32000-1 table 8).
    // 1 = none, 2 = TIFF horizontal differencing, 10..15 = PNG. Any value ≥ 10 selects PNG, whose
    // per-row filter type byte decides the rest -- which is why 10..15 are not distinguished here.
    int predictor = 1;
    int colors = 1;
    int bitsPerComponent = 8;
    int columns = 1;

    // LZW only. 1 = increase the code width one code early, which is the default and what almost
    // every producer emits; 0 is the Adobe-documented alternative.
    int earlyChange = 1;

    // CCITTFaxDecode (ISO 32000-1 table 11).
    int k = 0;                  // < 0 pure two-dimensional (G4), 0 pure one-dimensional (G3 1D),
                                // > 0 mixed, one-dimensional lines with optional 2-D lines
    int columnsCcitt = 1728;    // /Columns defaults to 1728 for CCITT, NOT to the predictor's 1
    int rows = 0;               // 0 means "until EOD"
    bool blackIs1 = false;
    bool byteAlign = false;     // /EncodedByteAlign
    bool endOfBlock = true;
};

// The spelling from a stream's `/DecodeParms`, which may be a dictionary, a null, or -- when
// `/Filter` is an array -- an array with one entry per filter. `index` picks the entry.
DecodeParms decodeParmsFrom(const CosObject& parms, std::size_t index = 0);

// Both the full names and the ISO 32000-1 table 6 abbreviations, because a producer that writes
// `/Fl` instead of `/FlateDecode` is producing a legal file. Returns nothing for an unknown name --
// including `/Crypt`, which is a real filter this stage does not have (D2-4), and the registry
// reports that by name rather than as "unknown".
std::optional<FilterKind> filterKindFromName(std::string_view name);

// Stable, ASCII, non-localized. Diagnostics only.
const char* filterName(FilterKind kind) noexcept;

// True when `encode` is implemented and `encode`→`decode` is the identity. Guide case A-18 is
// asserted over exactly this predicate rather than over a list a test keeps in its own head.
bool isLossless(FilterKind kind) noexcept;

core::Result<Bytes> decodeFilter(FilterKind kind, std::string_view input, const DecodeParms& parms);
core::Result<Bytes> encodeFilter(FilterKind kind, std::string_view input, const DecodeParms& parms);

// The whole chain, which is what every caller above this layer actually wants: read `/Filter` and
// `/DecodeParms` off the stream dictionary, apply each in order, and hand back the bytes.
// `source` is the buffer the stream was parsed from -- a stream is an offset and a length, never a
// copy, so the bytes have to come from somewhere.
core::Result<Bytes> decodeStream(const CosObject& stream, std::string_view source);

// The PNG and TIFF predictors, undone. Exposed because Flate and LZW share them exactly and a
// second copy is a second place to get the row stride wrong.
core::Result<Bytes> undoPredictor(const Bytes& input, const DecodeParms& parms);

} // namespace rawpage::pdf::cos

#endif // RAWPAGE_PDF_COS_FILTERS_FILTER_H
