#include "pdf/cos/filters/Ascii85Filter.h"

#include "pdf/cos/Lexer.h"

namespace rawpage::pdf::cos::filters {
namespace {

core::Error malformed(const char* what)
{
    return core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.pdf.filterFailed"),
                       QString::fromLatin1(what)};
}

} // namespace

core::Result<Bytes> ascii85Decode(std::string_view input)
{
    Bytes out;
    out.reserve(input.size() * 4 / 5 + 4);

    // Adobe's PostScript convention, not ISO 32000-1's -- but producers emit it, and a reader that
    // treated `<` as data would corrupt the first four bytes of the stream.
    std::size_t at = 0;
    if (input.size() >= 2 && input[0] == '<' && input[1] == '~')
        at = 2;

    std::uint32_t group = 0;
    int count = 0;
    bool sawEod = false;

    for (; at < input.size(); ++at) {
        const auto byte = static_cast<std::uint8_t>(input[at]);
        if (Lexer::isWhitespace(byte))
            continue;

        if (byte == '~') {
            sawEod = true;
            break;                                  // `~>`; the `>` is not required to be present
        }

        if (byte == 'z') {
            // Only at a group boundary. Inside a partial group it is malformed, and accepting it
            // would silently shift every byte after it.
            if (count != 0)
                return malformed("ascii85: 'z' inside a group");
            out.insert(out.end(), 4, std::uint8_t{0});
            continue;
        }

        if (byte < '!' || byte > 'u')
            return malformed("ascii85: character outside '!'..'u'");

        group = group * 85u + static_cast<std::uint32_t>(byte - '!');
        if (++count == 5) {
            out.push_back(static_cast<std::uint8_t>(group >> 24));
            out.push_back(static_cast<std::uint8_t>(group >> 16));
            out.push_back(static_cast<std::uint8_t>(group >> 8));
            out.push_back(static_cast<std::uint8_t>(group));
            group = 0;
            count = 0;
        }
    }

    if (count > 0) {
        if (count == 1)
            return malformed("ascii85: a final group of one character carries no byte");
        // Pad to five with 'u' (84), then keep count-1 bytes. Padding with anything else, or
        // keeping `count` bytes, corrupts the tail of every stream whose length is not a multiple
        // of four -- which is most of them.
        for (int pad = count; pad < 5; ++pad)
            group = group * 85u + 84u;
        for (int index = 0; index < count - 1; ++index)
            out.push_back(static_cast<std::uint8_t>(group >> (24 - 8 * index)));
    }

    (void)sawEod;   // a missing `~>` is tolerated: truncation is T8's report, not a decode failure
    return out;
}

core::Result<Bytes> ascii85Encode(std::string_view input)
{
    Bytes out;
    out.reserve(input.size() * 5 / 4 + input.size() / 60 + 4);

    std::size_t column = 0;
    // NOT named `emit`: QtCore defines that as an empty macro, and `core/Result.h` pulls QtCore in
    // through QString -- so a lambda called `emit` silently becomes `= [&out, &column](...)`.
    auto put = [&out, &column](std::uint8_t byte) {
        out.push_back(byte);
        if (++column >= 75) {
            out.push_back(static_cast<std::uint8_t>('\n'));
            column = 0;
        }
    };

    std::size_t at = 0;
    for (; at + 4 <= input.size(); at += 4) {
        std::uint32_t group = 0;
        for (int index = 0; index < 4; ++index)
            group = (group << 8) | static_cast<std::uint8_t>(input[at + index]);

        if (group == 0) {
            put(static_cast<std::uint8_t>('z'));
            continue;
        }
        std::uint8_t digits[5];
        for (int index = 4; index >= 0; --index) {
            digits[index] = static_cast<std::uint8_t>('!' + group % 85u);
            group /= 85u;
        }
        for (const std::uint8_t digit : digits)
            put(digit);
    }

    const std::size_t remaining = input.size() - at;
    if (remaining > 0) {
        std::uint32_t group = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            const auto byte = index < remaining ? static_cast<std::uint8_t>(input[at + index])
                                                : std::uint8_t{0};
            group = (group << 8) | byte;
        }
        std::uint8_t digits[5];
        for (int index = 4; index >= 0; --index) {
            digits[index] = static_cast<std::uint8_t>('!' + group % 85u);
            group /= 85u;
        }
        // remaining+1 characters, never the `z` shorthand: a partial group of zeros is not four
        // zero bytes, and emitting `z` here would lengthen the decoded output.
        for (std::size_t index = 0; index <= remaining; ++index)
            put(digits[index]);
    }

    out.push_back(static_cast<std::uint8_t>('~'));
    out.push_back(static_cast<std::uint8_t>('>'));
    return out;
}

} // namespace rawpage::pdf::cos::filters
