// pdf/cos/filters/Filter -- the registry, the parameters, the predictor. Stage 2 T7.

#include "pdf/cos/filters/Filter.h"

#include "pdf/cos/filters/Ascii85Filter.h"
#include "pdf/cos/filters/AsciiHexFilter.h"
#include "pdf/cos/filters/CcittFilter.h"
#include "pdf/cos/filters/DctFilter.h"
#include "pdf/cos/filters/FlateFilter.h"
#include "pdf/cos/filters/Jbig2Filter.h"
#include "pdf/cos/filters/JpxFilter.h"
#include "pdf/cos/filters/LZWFilter.h"
#include "pdf/cos/filters/RunLengthFilter.h"

#include <algorithm>
#include <cstdlib>

namespace rawpage::pdf::cos {
namespace {

core::Error filterError(core::ErrorCode code, const char* messageKey, const QString& context)
{
    return core::Error{code, QLatin1String(messageKey), context};
}

int intFrom(const CosObject& dictionary, std::string_view key, int fallback)
{
    const CosObject* value = dictionary.find(key);
    if (value == nullptr || !value->isNumber())
        return fallback;
    const double number = value->numberValue();
    // Clamped rather than cast: `/Columns 99999999999` is a producer bug and a `int` cast of it is
    // undefined, which is a worse outcome than a refusal one layer up.
    if (number < 0.0)
        return fallback;
    if (number > 1'000'000.0)
        return 1'000'000;
    return static_cast<int>(number);
}

bool boolFrom(const CosObject& dictionary, std::string_view key, bool fallback)
{
    const CosObject* value = dictionary.find(key);
    return value != nullptr && value->isBoolean() ? value->booleanValue() : fallback;
}

} // namespace

DecodeParms decodeParmsFrom(const CosObject& parms, std::size_t index)
{
    // `/DecodeParms` mirrors `/Filter`: a dictionary when there is one filter, an array of
    // dictionaries (any of them null) when there are several. A caller that forgot the array case
    // would apply the first filter's parameters to all of them.
    const CosObject* entry = &parms;
    if (parms.isArray()) {
        if (index >= parms.items().size())
            return DecodeParms{};
        entry = &parms.items()[index];
    } else if (index != 0) {
        return DecodeParms{};
    }
    if (!entry->isDictionary())
        return DecodeParms{};

    DecodeParms out;
    out.predictor = intFrom(*entry, "Predictor", 1);
    out.colors = std::max(1, intFrom(*entry, "Colors", 1));
    out.bitsPerComponent = intFrom(*entry, "BitsPerComponent", 8);
    out.columns = std::max(1, intFrom(*entry, "Columns", 1));
    out.earlyChange = intFrom(*entry, "EarlyChange", 1);

    out.k = intFrom(*entry, "K", 0);
    // `/K` is the one signed field here, and `intFrom` refuses negatives -- so G4's `K -1` is read
    // directly rather than through it. Missing this makes every Group 4 image decode as Group 3.
    if (const CosObject* k = entry->find("K"); k != nullptr && k->isNumber())
        out.k = static_cast<int>(k->numberValue());

    // /Columns means 1728 here and 1 for the predictor -- two different defaults on one key name,
    // which is why DecodeParms carries them as two fields rather than one.
    out.columnsCcitt = std::max(1, intFrom(*entry, "Columns", 1728));
    out.rows = intFrom(*entry, "Rows", 0);
    out.blackIs1 = boolFrom(*entry, "BlackIs1", false);
    out.byteAlign = boolFrom(*entry, "EncodedByteAlign", false);
    out.endOfBlock = boolFrom(*entry, "EndOfBlock", true);
    return out;
}

std::optional<FilterKind> filterKindFromName(std::string_view name)
{
    // Full names and the ISO 32000-1 table 6 abbreviations. The abbreviations are legal in any
    // stream, not only in inline images, and a producer that uses them is producing a legal file.
    if (name == "ASCIIHexDecode" || name == "AHx") return FilterKind::AsciiHex;
    if (name == "ASCII85Decode" || name == "A85")  return FilterKind::Ascii85;
    if (name == "LZWDecode" || name == "LZW")      return FilterKind::LZW;
    if (name == "FlateDecode" || name == "Fl")     return FilterKind::Flate;
    if (name == "RunLengthDecode" || name == "RL") return FilterKind::RunLength;
    if (name == "CCITTFaxDecode" || name == "CCF") return FilterKind::Ccitt;
    if (name == "DCTDecode" || name == "DCT")      return FilterKind::Dct;
    if (name == "JPXDecode")                       return FilterKind::Jpx;
    if (name == "JBIG2Decode")                     return FilterKind::Jbig2;
    // `/Crypt` is deliberately absent: it is a real filter this stage does not have (D2-4), and
    // `decodeStream` names it specifically rather than calling it unknown.
    return std::nullopt;
}

const char* filterName(FilterKind kind) noexcept
{
    switch (kind) {
    case FilterKind::AsciiHex:  return "ASCIIHexDecode";
    case FilterKind::Ascii85:   return "ASCII85Decode";
    case FilterKind::LZW:       return "LZWDecode";
    case FilterKind::Flate:     return "FlateDecode";
    case FilterKind::RunLength: return "RunLengthDecode";
    case FilterKind::Ccitt:     return "CCITTFaxDecode";
    case FilterKind::Dct:       return "DCTDecode";
    case FilterKind::Jpx:       return "JPXDecode";
    case FilterKind::Jbig2:     return "JBIG2Decode";
    }
    return "?";
}

bool isLossless(FilterKind kind) noexcept
{
    // The predicate guide case A-18 is asserted over, rather than a list a test keeps in its own
    // head. JPEG and JPEG 2000 are lossy BY CONSTRUCTION -- `encode`→`decode` is not a property
    // they have -- and CCITT and JBIG2 are decode-only here; see each filter's own header.
    switch (kind) {
    case FilterKind::AsciiHex:
    case FilterKind::Ascii85:
    case FilterKind::LZW:
    case FilterKind::Flate:
    case FilterKind::RunLength:
        return true;
    case FilterKind::Ccitt:
    case FilterKind::Dct:
    case FilterKind::Jpx:
    case FilterKind::Jbig2:
        return false;
    }
    return false;
}

core::Result<Bytes> undoPredictor(const Bytes& input, const DecodeParms& parms)
{
    if (parms.predictor <= 1)
        return input;

    const long long colors = parms.colors;
    const long long bpc = parms.bitsPerComponent;
    const long long columns = parms.columns;
    if (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8 && bpc != 16) {
        return filterError(core::ErrorCode::Unsupported, "error.pdf.filterFailed",
                           QStringLiteral("predictor BitsPerComponent %1").arg(bpc));
    }

    // Bytes per row, rounded up: a 1-bit 3-column row is one byte, not three eighths of one.
    const long long rowBits = colors * bpc * columns;
    const auto rowBytes = static_cast<std::size_t>((rowBits + 7) / 8);
    if (rowBytes == 0)
        return input;

    if (parms.predictor == 2) {
        // TIFF horizontal differencing. Only the 8- and 16-bit forms are implemented: sub-byte
        // TIFF prediction exists in the specification and appears in no file this project has, and
        // a decoder written against no fixture is the shape D2-4 rejected for CryptFilter.
        if (bpc != 8 && bpc != 16) {
            return filterError(core::ErrorCode::Unsupported, "error.pdf.filterFailed",
                               QStringLiteral("TIFF predictor at %1 bits per component").arg(bpc));
        }
        Bytes out = input;
        const auto stride = static_cast<std::size_t>(colors * (bpc / 8));
        for (std::size_t row = 0; row + rowBytes <= out.size(); row += rowBytes) {
            for (std::size_t at = stride; at < rowBytes; ++at) {
                out[row + at] = static_cast<std::uint8_t>(out[row + at] + out[row + at - stride]);
            }
        }
        return out;
    }

    // PNG predictors (10..15). Every row is prefixed by a filter TYPE byte, so the encoded row is
    // one byte longer than the decoded one -- getting that wrong shifts every row after the first.
    const auto pixelBytes = static_cast<std::size_t>(std::max<long long>(1, colors * bpc / 8));
    const std::size_t encodedRow = rowBytes + 1;

    Bytes out;
    out.reserve(input.size());
    std::vector<std::uint8_t> previous(rowBytes, 0);
    std::vector<std::uint8_t> current(rowBytes, 0);

    for (std::size_t at = 0; at + 1 <= input.size(); at += encodedRow) {
        const std::uint8_t type = input[at];
        const std::size_t available = std::min(rowBytes, input.size() - at - 1);
        if (available == 0)
            break;

        std::fill(current.begin(), current.end(), std::uint8_t{0});
        std::copy(input.begin() + static_cast<std::ptrdiff_t>(at + 1),
                  input.begin() + static_cast<std::ptrdiff_t>(at + 1 + available),
                  current.begin());

        for (std::size_t index = 0; index < rowBytes; ++index) {
            const unsigned raw = current[index];
            const unsigned left = index >= pixelBytes ? current[index - pixelBytes] : 0u;
            const unsigned up = previous[index];
            const unsigned upLeft = index >= pixelBytes ? previous[index - pixelBytes] : 0u;

            unsigned value = raw;
            switch (type) {
            case 0: break;                                        // None
            case 1: value = raw + left; break;                     // Sub
            case 2: value = raw + up; break;                       // Up
            case 3: value = raw + ((left + up) >> 1); break;        // Average
            case 4: {                                              // Paeth
                const int estimate = static_cast<int>(left + up - upLeft);
                const int distLeft = std::abs(estimate - static_cast<int>(left));
                const int distUp = std::abs(estimate - static_cast<int>(up));
                const int distUpLeft = std::abs(estimate - static_cast<int>(upLeft));
                unsigned nearest = upLeft;
                if (distLeft <= distUp && distLeft <= distUpLeft)
                    nearest = left;
                else if (distUp <= distUpLeft)
                    nearest = up;
                value = raw + nearest;
                break;
            }
            default:
                // An unknown row type is not a stream-level failure: PNG predictors are per ROW,
                // so one bad type byte costs one row rather than the image. Treated as None.
                break;
            }
            current[index] = static_cast<std::uint8_t>(value & 0xFFu);
        }

        out.insert(out.end(), current.begin(), current.end());
        previous = current;
    }

    return out;
}

core::Result<Bytes> decodeFilter(FilterKind kind, std::string_view input, const DecodeParms& parms)
{
    switch (kind) {
    case FilterKind::AsciiHex:
        return filters::asciiHexDecode(input);
    case FilterKind::Ascii85:
        return filters::ascii85Decode(input);
    case FilterKind::RunLength:
        return filters::runLengthDecode(input);
    case FilterKind::LZW: {
        auto decoded = filters::lzwDecode(input, parms.earlyChange);
        if (!decoded)
            return decoded.error();
        return undoPredictor(std::move(decoded).value(), parms);
    }
    case FilterKind::Flate: {
        auto decoded = filters::flateDecode(input);
        if (!decoded)
            return decoded.error();
        return undoPredictor(std::move(decoded).value(), parms);
    }
    case FilterKind::Ccitt:
        return filters::ccittDecode(input, parms);
    case FilterKind::Dct:
        return filters::dctDecode(input);
    case FilterKind::Jpx:
        return filters::jpxDecode(input);
    case FilterKind::Jbig2:
        return filters::jbig2Decode(input, {});
    }
    return filterError(core::ErrorCode::Unsupported, "error.pdf.filterUnsupported",
                       QStringLiteral("?"));
}

core::Result<Bytes> encodeFilter(FilterKind kind, std::string_view input, const DecodeParms& parms)
{
    switch (kind) {
    case FilterKind::AsciiHex:
        return filters::asciiHexEncode(input);
    case FilterKind::Ascii85:
        return filters::ascii85Encode(input);
    case FilterKind::RunLength:
        return filters::runLengthEncode(input);
    case FilterKind::LZW:
        return filters::lzwEncode(input, parms.earlyChange);
    case FilterKind::Flate:
        return filters::flateEncode(input);
    case FilterKind::Ccitt:
    case FilterKind::Dct:
    case FilterKind::Jpx:
    case FilterKind::Jbig2:
        // Not "not written yet": Stage 2 is a READER (D2-4), and for DCT and JPX an
        // `encode`→`decode` identity is not a property the format has. An encoder here would be
        // untested infrastructure with no caller -- exactly the shape D2-4 rejected for
        // CryptFilter. It says so by name rather than returning an empty buffer.
        return filterError(core::ErrorCode::Unsupported, "error.pdf.filterUnsupported",
                           QString::fromLatin1(filterName(kind)));
    }
    return filterError(core::ErrorCode::Unsupported, "error.pdf.filterUnsupported",
                       QStringLiteral("?"));
}

core::Result<Bytes> decodeStream(const CosObject& stream, std::string_view source)
{
    Bytes data;
    const std::string_view raw = stream.rawStreamData(source);
    data.assign(raw.begin(), raw.end());

    const CosObject* filter = stream.find("Filter");
    if (filter == nullptr || filter->isNull())
        return data;

    const CosObject* parms = stream.find("DecodeParms");
    const CosObject empty;

    // One name or an array of them, applied in order. The array order is the ENCODING order, so
    // decoding walks it forwards -- `[/ASCII85Decode /LZWDecode]` means ASCII85 first.
    std::vector<std::string> names;
    if (filter->isName()) {
        names.push_back(filter->text());
    } else if (filter->isArray()) {
        for (const CosObject& item : filter->items()) {
            if (item.isName())
                names.push_back(item.text());
        }
    }

    for (std::size_t index = 0; index < names.size(); ++index) {
        const std::optional<FilterKind> kind = filterKindFromName(names[index]);
        if (!kind) {
            // `/Crypt` is named specifically: it is a real filter that arrives at Stage 10 with the
            // security handler, and reporting it as "unknown" would send a reader looking for a
            // typo instead of for the stage.
            const bool isCrypt = names[index] == "Crypt";
            return filterError(core::ErrorCode::Unsupported,
                               isCrypt ? "error.pdf.filterCrypt" : "error.pdf.filterUnsupported",
                               QString::fromStdString(names[index]));
        }
        const DecodeParms decodeParms =
            decodeParmsFrom(parms != nullptr ? *parms : empty, index);

        auto decoded = decodeFilter(
            *kind, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()),
            decodeParms);
        if (!decoded)
            return decoded.error();
        data = std::move(decoded).value();
    }

    return data;
}

} // namespace rawpage::pdf::cos
