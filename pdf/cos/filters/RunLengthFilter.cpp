#include "pdf/cos/filters/RunLengthFilter.h"

#include <algorithm>

namespace rawpage::pdf::cos::filters {

core::Result<Bytes> runLengthDecode(std::string_view input)
{
    Bytes out;
    out.reserve(input.size() * 2);

    std::size_t at = 0;
    while (at < input.size()) {
        const auto length = static_cast<std::uint8_t>(input[at++]);
        if (length == 128)
            break;                                  // EOD

        if (length < 128) {
            // n+1 literal bytes. A truncated tail is TOLERATED and copied as far as it goes --
            // `corpus/malformed/truncated-mid-stream.pdf` is what a reader is actually handed, and
            // losing the bytes that ARE there would be a worse answer than a short result.
            const std::size_t count =
                std::min(static_cast<std::size_t>(length) + 1, input.size() - at);
            out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(at),
                       input.begin() + static_cast<std::ptrdiff_t>(at + count));
            at += count;
        } else {
            if (at >= input.size())
                break;                              // a repeat count with nothing to repeat
            const auto byte = static_cast<std::uint8_t>(input[at++]);
            out.insert(out.end(), 257u - length, byte);
        }
    }
    return out;
}

core::Result<Bytes> runLengthEncode(std::string_view input)
{
    Bytes out;
    out.reserve(input.size() + input.size() / 64 + 2);

    std::size_t at = 0;
    while (at < input.size()) {
        // How long is the run starting here? Capped at 128, which is the largest a single repeat
        // record can express (257 - 129).
        std::size_t run = 1;
        while (at + run < input.size() && input[at + run] == input[at] && run < 128)
            ++run;

        if (run >= 3) {
            out.push_back(static_cast<std::uint8_t>(257 - run));
            out.push_back(static_cast<std::uint8_t>(input[at]));
            at += run;
            continue;
        }

        // A literal run, ending where a run of three or more begins -- that lookahead is what
        // keeps a long repeat from being swallowed into the literal that precedes it.
        const std::size_t start = at;
        while (at < input.size() && at - start < 128) {
            std::size_t ahead = 1;
            while (at + ahead < input.size() && input[at + ahead] == input[at] && ahead < 3)
                ++ahead;
            if (ahead >= 3)
                break;
            ++at;
        }
        const std::size_t count = at - start;
        out.push_back(static_cast<std::uint8_t>(count - 1));
        out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(start),
                   input.begin() + static_cast<std::ptrdiff_t>(at));
    }

    out.push_back(std::uint8_t{128});               // EOD
    return out;
}

} // namespace rawpage::pdf::cos::filters
