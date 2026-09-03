#include "pdf/cos/filters/AsciiHexFilter.h"

#include "pdf/cos/Lexer.h"

namespace rawpage::pdf::cos::filters {

core::Result<Bytes> asciiHexDecode(std::string_view input)
{
    Bytes out;
    out.reserve(input.size() / 2 + 1);

    int high = -1;
    for (const char raw : input) {
        const auto byte = static_cast<std::uint8_t>(raw);
        if (byte == '>')
            break;                                  // EOD
        if (Lexer::isWhitespace(byte))
            continue;

        const int value = Lexer::hexValue(byte);
        if (value < 0)
            continue;                               // tolerated and skipped, as in the lexer

        if (high < 0) {
            high = value;
        } else {
            out.push_back(static_cast<std::uint8_t>((high << 4) | value));
            high = -1;
        }
    }

    // ISO 32000-1 7.4.2: "If the filter encounters the EOD marker after reading an odd number of
    // hexadecimal digits, it shall behave as if a 0 followed the last digit." Not a kindness --
    // the specification says so, and `pdf/cos/Lexer` applies the same rule to `<41A>`.
    if (high >= 0)
        out.push_back(static_cast<std::uint8_t>(high << 4));

    return out;
}

core::Result<Bytes> asciiHexEncode(std::string_view input)
{
    static const char kDigits[] = "0123456789ABCDEF";

    Bytes out;
    out.reserve(input.size() * 2 + input.size() / 32 + 2);

    std::size_t column = 0;
    for (const char raw : input) {
        const auto byte = static_cast<std::uint8_t>(raw);
        out.push_back(static_cast<std::uint8_t>(kDigits[byte >> 4]));
        out.push_back(static_cast<std::uint8_t>(kDigits[byte & 0x0F]));
        column += 2;
        if (column >= 64) {
            out.push_back(static_cast<std::uint8_t>('\n'));
            column = 0;
        }
    }
    out.push_back(static_cast<std::uint8_t>('>'));
    return out;
}

} // namespace rawpage::pdf::cos::filters
