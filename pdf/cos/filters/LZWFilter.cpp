#include "pdf/cos/filters/LZWFilter.h"

#include <array>
#include <map>
#include <string>

namespace rawpage::pdf::cos::filters {
namespace {

constexpr unsigned kClear = 256;
constexpr unsigned kEod = 257;
constexpr unsigned kFirstFree = 258;
constexpr unsigned kMaxCode = 4096;
constexpr int kMinWidth = 9;
constexpr int kMaxWidth = 12;

core::Error malformed(const char* what)
{
    return core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.pdf.filterFailed"),
                       QString::fromLatin1(what)};
}

// MSB-first bit reader. A code that runs past the end yields "no more codes" rather than reading
// whatever follows the buffer -- a truncated stream is an ordinary input here.
class BitReader {
public:
    explicit BitReader(std::string_view bytes) : bytes_(bytes) {}

    bool read(int width, unsigned& code)
    {
        while (bits_ < width) {
            if (at_ >= bytes_.size())
                return false;
            accumulator_ = (accumulator_ << 8) | static_cast<std::uint8_t>(bytes_[at_++]);
            bits_ += 8;
        }
        bits_ -= width;
        code = static_cast<unsigned>((accumulator_ >> bits_) & ((1u << width) - 1u));
        accumulator_ &= (1ull << bits_) - 1ull;
        return true;
    }

private:
    std::string_view bytes_;
    std::size_t at_ = 0;
    std::uint64_t accumulator_ = 0;
    int bits_ = 0;
};

class BitWriter {
public:
    void write(unsigned code, int width)
    {
        accumulator_ = (accumulator_ << width) | code;
        bits_ += width;
        while (bits_ >= 8) {
            bits_ -= 8;
            out_.push_back(static_cast<std::uint8_t>((accumulator_ >> bits_) & 0xFFu));
        }
    }

    Bytes finish()
    {
        if (bits_ > 0)
            out_.push_back(static_cast<std::uint8_t>((accumulator_ << (8 - bits_)) & 0xFFu));
        bits_ = 0;
        return std::move(out_);
    }

private:
    Bytes out_;
    std::uint64_t accumulator_ = 0;
    int bits_ = 0;
};

} // namespace

core::Result<Bytes> lzwDecode(std::string_view input, int earlyChange)
{
    Bytes out;
    out.reserve(input.size() * 3);

    // The table holds only the ASSIGNED entries, 258 upward; codes below 256 are their own byte and
    // 256/257 are control. Storing the 256 singletons would be 256 allocations per clear code, and
    // a stream that clears often would spend more time rebuilding the table than decoding.
    std::vector<std::string> table;
    table.reserve(kMaxCode - kFirstFree);

    int width = kMinWidth;
    std::string previous;

    auto entryFor = [&table](unsigned code, std::string& out_entry) {
        if (code < 256) {
            out_entry.assign(1, static_cast<char>(code));
            return true;
        }
        if (code < kFirstFree)
            return false;                            // 256/257 are control, never data
        const std::size_t index = code - kFirstFree;
        if (index >= table.size())
            return false;
        out_entry = table[index];
        return true;
    };

    BitReader reader(input);
    unsigned code = 0;
    std::string entry;

    while (reader.read(width, code)) {
        if (code == kEod)
            break;

        if (code == kClear) {
            table.clear();
            width = kMinWidth;
            previous.clear();
            continue;
        }

        if (!entryFor(code, entry)) {
            // The one legal forward reference in LZW: a code equal to the next code to be
            // assigned means "the previous entry plus its own first byte". Anything beyond that is
            // a malformed stream.
            const unsigned next = kFirstFree + static_cast<unsigned>(table.size());
            if (code != next || previous.empty())
                return malformed("lzw: code outside the table");
            entry = previous + previous.front();
        }

        out.insert(out.end(), entry.begin(), entry.end());

        if (!previous.empty() && kFirstFree + table.size() < kMaxCode) {
            table.push_back(previous + entry.front());
            // MEASURED, not derived -- see the header. The decoder is one entry behind the
            // encoder, so its boundary is `>=` where the encoder's is `>`.
            const auto nextCode = static_cast<unsigned>(kFirstFree + table.size());
            if (width < kMaxWidth
                && nextCode + static_cast<unsigned>(earlyChange) >= (1u << width))
                ++width;
        }
        previous = entry;
    }

    return out;
}

core::Result<Bytes> lzwEncode(std::string_view input, int earlyChange)
{
    BitWriter writer;
    std::map<std::string, unsigned> table;
    unsigned nextCode = kFirstFree;
    int width = kMinWidth;

    auto reset = [&table, &nextCode, &width]() {
        table.clear();
        nextCode = kFirstFree;
        width = kMinWidth;
    };

    writer.write(kClear, width);
    reset();

    std::string current;
    for (const char raw : input) {
        std::string candidate = current + raw;
        const bool known = candidate.size() == 1 || table.count(candidate) != 0;
        if (known) {
            current = std::move(candidate);
            continue;
        }

        writer.write(current.size() == 1 ? static_cast<unsigned>(
                                               static_cast<std::uint8_t>(current.front()))
                                         : table.at(current),
                     width);
        table.emplace(std::move(candidate), nextCode);
        ++nextCode;

        if (width < kMaxWidth && nextCode + static_cast<unsigned>(earlyChange) > (1u << width)) {
            ++width;
        } else if (nextCode + static_cast<unsigned>(earlyChange) > kMaxCode) {
            // The table is full. A clear costs one code and is the only way to carry on; a decoder
            // that never saw one would run past 4,095 and desynchronize.
            writer.write(kClear, width);
            reset();
        }
        current.assign(1, raw);
    }

    if (!current.empty()) {
        writer.write(current.size() == 1
                         ? static_cast<unsigned>(static_cast<std::uint8_t>(current.front()))
                         : table.at(current),
                     width);
    }
    writer.write(kEod, width);
    return writer.finish();
}

} // namespace rawpage::pdf::cos::filters
