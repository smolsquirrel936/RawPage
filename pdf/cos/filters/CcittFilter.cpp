#include "pdf/cos/filters/CcittFilter.h"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace rawpage::pdf::cos::filters {
namespace {

// ITU-T T.4 Tables 1, 2 and 3. Transcribed from the specification, and deliberately kept in the
// same shape as `corpus/ccitt.py`'s so the two can be diffed by eye when a run decodes wrong.
struct Code {
    int bits;
    unsigned value;
    int run;
};

// clang-format off
const Code kWhiteCodes[] = {
    {8,0x35,0},{6,0x07,1},{4,0x07,2},{4,0x08,3},{4,0x0B,4},{4,0x0C,5},{4,0x0E,6},{4,0x0F,7},
    {5,0x13,8},{5,0x14,9},{5,0x07,10},{5,0x08,11},{6,0x08,12},{6,0x03,13},{6,0x34,14},{6,0x35,15},
    {6,0x2A,16},{6,0x2B,17},{7,0x27,18},{7,0x0C,19},{7,0x08,20},{7,0x17,21},{7,0x03,22},{7,0x04,23},
    {7,0x28,24},{7,0x2B,25},{7,0x13,26},{7,0x24,27},{7,0x18,28},{8,0x02,29},{8,0x03,30},{8,0x1A,31},
    {8,0x1B,32},{8,0x12,33},{8,0x13,34},{8,0x14,35},{8,0x15,36},{8,0x16,37},{8,0x17,38},{8,0x28,39},
    {8,0x29,40},{8,0x2A,41},{8,0x2B,42},{8,0x2C,43},{8,0x2D,44},{8,0x04,45},{8,0x05,46},{8,0x0A,47},
    {8,0x0B,48},{8,0x52,49},{8,0x53,50},{8,0x54,51},{8,0x55,52},{8,0x24,53},{8,0x25,54},{8,0x58,55},
    {8,0x59,56},{8,0x5A,57},{8,0x5B,58},{8,0x4A,59},{8,0x4B,60},{8,0x32,61},{8,0x33,62},{8,0x34,63},
    // Make-up codes, white.
    {5,0x1B,64},{5,0x12,128},{6,0x17,192},{7,0x37,256},{8,0x36,320},{8,0x37,384},{8,0x64,448},
    {8,0x65,512},{8,0x68,576},{8,0x67,640},{9,0xCC,704},{9,0xCD,768},{9,0xD2,832},{9,0xD3,896},
    {9,0xD4,960},{9,0xD5,1024},{9,0xD6,1088},{9,0xD7,1152},{9,0xD8,1216},{9,0xD9,1280},
    {9,0xDA,1344},{9,0xDB,1408},{9,0x98,1472},{9,0x99,1536},{9,0x9A,1600},{6,0x18,1664},
    {9,0x9B,1728},
};

const Code kBlackCodes[] = {
    {10,0x37,0},{3,0x02,1},{2,0x03,2},{2,0x02,3},{3,0x03,4},{4,0x03,5},{4,0x02,6},{5,0x03,7},
    {6,0x05,8},{6,0x04,9},{7,0x04,10},{7,0x05,11},{7,0x07,12},{8,0x04,13},{8,0x07,14},{9,0x18,15},
    {10,0x17,16},{10,0x18,17},{10,0x08,18},{11,0x67,19},{11,0x68,20},{11,0x6C,21},{11,0x37,22},
    {11,0x28,23},{11,0x17,24},{11,0x18,25},{12,0xCA,26},{12,0xCB,27},{12,0xCC,28},{12,0xCD,29},
    {12,0x68,30},{12,0x69,31},{12,0x6A,32},{12,0x6B,33},{12,0xD2,34},{12,0xD3,35},{12,0xD4,36},
    {12,0xD5,37},{12,0xD6,38},{12,0xD7,39},{12,0x6C,40},{12,0x6D,41},{12,0xDA,42},{12,0xDB,43},
    {12,0x54,44},{12,0x55,45},{12,0x56,46},{12,0x57,47},{12,0x64,48},{12,0x65,49},{12,0x52,50},
    {12,0x53,51},{12,0x24,52},{12,0x37,53},{12,0x38,54},{12,0x27,55},{12,0x28,56},{12,0x58,57},
    {12,0x59,58},{12,0x2B,59},{12,0x2C,60},{12,0x5A,61},{12,0x66,62},{12,0x67,63},
    // Make-up codes, black.
    {10,0x0F,64},{12,0xC8,128},{12,0xC9,192},{12,0x5B,256},{12,0x33,320},{12,0x34,384},
    {12,0x35,448},{13,0x6C,512},{13,0x6D,576},{13,0x4A,640},{13,0x4B,704},{13,0x4C,768},
    {13,0x4D,832},{13,0x72,896},{13,0x73,960},{13,0x74,1024},{13,0x75,1088},{13,0x76,1152},
    {13,0x77,1216},{13,0x52,1280},{13,0x53,1344},{13,0x54,1408},{13,0x55,1472},{13,0x5A,1536},
    {13,0x5B,1600},{13,0x64,1664},{13,0x65,1728},
};

// T.4 Table 3, shared by both colours.
const Code kExtendedCodes[] = {
    {11,0x08,1792},{11,0x0C,1856},{11,0x0D,1920},{12,0x12,1984},{12,0x13,2048},{12,0x14,2112},
    {12,0x15,2176},{12,0x16,2240},{12,0x17,2304},{12,0x1C,2368},{12,0x1D,2432},{12,0x1E,2496},
    {12,0x1F,2560},
};
// clang-format on

using CodeMap = std::map<std::pair<int, unsigned>, int>;

const CodeMap& whiteMap()
{
    static const CodeMap map = [] {
        CodeMap built;
        for (const Code& code : kWhiteCodes)
            built[{code.bits, code.value}] = code.run;
        for (const Code& code : kExtendedCodes)
            built[{code.bits, code.value}] = code.run;
        return built;
    }();
    return map;
}

const CodeMap& blackMap()
{
    static const CodeMap map = [] {
        CodeMap built;
        for (const Code& code : kBlackCodes)
            built[{code.bits, code.value}] = code.run;
        for (const Code& code : kExtendedCodes)
            built[{code.bits, code.value}] = code.run;
        return built;
    }();
    return map;
}

// MSB-first bit reader over the encoded stream.
class BitReader {
public:
    explicit BitReader(std::string_view bytes) : bytes_(bytes) {}

    bool atEnd() const noexcept { return at_ >= bytes_.size() * 8; }
    std::size_t position() const noexcept { return at_; }
    void seek(std::size_t bit) noexcept { at_ = bit; }

    int bit()
    {
        if (atEnd())
            return -1;
        const std::size_t byte = at_ >> 3;
        const int shift = 7 - static_cast<int>(at_ & 7u);
        ++at_;
        return (static_cast<std::uint8_t>(bytes_[byte]) >> shift) & 1;
    }

    // Look at up to `count` bits without consuming them. Used for the EOL sniff, which has to be
    // able to change its mind.
    unsigned peek(int count) const
    {
        unsigned value = 0;
        for (int index = 0; index < count; ++index) {
            const std::size_t position = at_ + static_cast<std::size_t>(index);
            int one = 0;
            if (position < bytes_.size() * 8) {
                const std::size_t byte = position >> 3;
                const int shift = 7 - static_cast<int>(position & 7u);
                one = (static_cast<std::uint8_t>(bytes_[byte]) >> shift) & 1;
            }
            value = (value << 1) | static_cast<unsigned>(one);
        }
        return value;
    }

    void alignToByte() noexcept { at_ = (at_ + 7u) & ~std::size_t{7u}; }

private:
    std::string_view bytes_;
    std::size_t at_ = 0;
};

// One run length: zero or more make-up codes then one terminating code. Returns -1 at end of data
// or on a code that is in no table -- both of which end the image rather than failing it.
int readRun(BitReader& reader, bool white)
{
    const CodeMap& table = white ? whiteMap() : blackMap();
    int total = 0;
    for (;;) {
        unsigned value = 0;
        int bits = 0;
        int run = -1;
        // 14 is the longest code in T.4 (13 bits for black make-up) plus one, so a code that has
        // not matched by then is not a code.
        while (bits < 14) {
            const int one = reader.bit();
            if (one < 0)
                return -1;
            value = (value << 1) | static_cast<unsigned>(one);
            ++bits;
            const auto found = table.find({bits, value});
            if (found != table.end()) {
                run = found->second;
                break;
            }
        }
        if (run < 0)
            return -1;
        total += run;
        // A make-up code (a multiple of 64 and at least 64) is always followed by another code;
        // a terminating code (0..63) ends the run. Missing that is how a 2,000-pixel white margin
        // becomes a 1,728-pixel one.
        if (run < 64 || (run % 64) != 0)
            return total;
    }
}

// The first changing element on the reference line strictly right of `a0` whose colour change is
// the one `colour` is looking for. `reference` holds transition positions in order, so index
// parity IS the colour: even entries are white→black, odd are black→white.
std::size_t findB1(const std::vector<int>& reference, int a0, int colour)
{
    std::size_t index = 0;
    while (index < reference.size()
           && (reference[index] <= a0 || (index % 2) != static_cast<std::size_t>(colour))) {
        ++index;
    }
    return index;
}

int at(const std::vector<int>& reference, std::size_t index, int width)
{
    return index < reference.size() ? reference[index] : width;
}

// Skip an EOL (`000000000001`) and any fill bits before it. Returns true when one was consumed.
bool consumeEol(BitReader& reader)
{
    const std::size_t saved = reader.position();
    int zeros = 0;
    for (;;) {
        const int one = reader.bit();
        if (one < 0) {
            reader.seek(saved);
            return false;
        }
        if (one == 0) {
            ++zeros;
            if (zeros > 64) {          // fill can be long, but not unbounded
                reader.seek(saved);
                return false;
            }
            continue;
        }
        if (zeros >= 11)
            return true;
        reader.seek(saved);
        return false;
    }
}

} // namespace

core::Result<Bytes> ccittDecode(std::string_view input, const DecodeParms& parms)
{
    const int width = std::max(1, parms.columnsCcitt);
    const int maxRows = parms.rows > 0 ? parms.rows : 0;

    BitReader reader(input);

    // The reference line for the first row is an imaginary all-white line, which T.6 specifies and
    // which is why an empty vector is the right starting value rather than a special case.
    std::vector<int> reference;
    std::vector<int> current;
    Bytes out;

    const auto rowBytes = static_cast<std::size_t>((width + 7) / 8);
    std::vector<std::uint8_t> row(rowBytes);

    int rowsDone = 0;
    bool twoDimensional = parms.k < 0;

    while (!reader.atEnd() && (maxRows == 0 || rowsDone < maxRows)) {
        if (parms.byteAlign && parms.k >= 0)
            reader.alignToByte();

        // G3 rows may be preceded by an EOL; G4 rows are not, but an EOFB at the end is an EOL
        // pair and consuming it here is what stops the loop cleanly.
        const bool hadEol = consumeEol(reader);
        if (hadEol && parms.k > 0) {
            // Mixed mode: one flag bit after the EOL says whether THIS row is 1-D or 2-D.
            const int flag = reader.bit();
            if (flag < 0)
                break;
            twoDimensional = flag == 0;
        }
        if (reader.atEnd())
            break;
        if (parms.byteAlign && parms.k < 0)
            reader.alignToByte();

        current.clear();
        int a0 = -1;
        int colour = 0;                  // 0 = white, 1 = black; every row starts white
        bool rowFailed = false;

        while (a0 < width) {
            if (twoDimensional) {
                // T.6 Table 4. Read the mode code one bit at a time; the codes are a prefix set,
                // so the first match is the only match.
                int mode = -99;
                unsigned value = 0;
                int bits = 0;
                while (bits < 7) {
                    const int one = reader.bit();
                    if (one < 0) { rowFailed = true; break; }
                    value = (value << 1) | static_cast<unsigned>(one);
                    ++bits;
                    if (bits == 1 && value == 0b1)        { mode = 0;   break; }   // V0
                    if (bits == 3 && value == 0b011)      { mode = 1;   break; }   // VR1
                    if (bits == 3 && value == 0b010)      { mode = -1;  break; }   // VL1
                    if (bits == 3 && value == 0b001)      { mode = 100; break; }   // Horizontal
                    if (bits == 4 && value == 0b0001)     { mode = 200; break; }   // Pass
                    if (bits == 6 && value == 0b000011)   { mode = 2;   break; }   // VR2
                    if (bits == 6 && value == 0b000010)   { mode = -2;  break; }   // VL2
                    if (bits == 7 && value == 0b0000011)  { mode = 3;   break; }   // VR3
                    if (bits == 7 && value == 0b0000010)  { mode = -3;  break; }   // VL3
                }
                if (rowFailed || mode == -99) { rowFailed = true; break; }

                const std::size_t b1index = findB1(reference, a0, colour);
                const int b1 = at(reference, b1index, width);
                const int b2 = at(reference, b1index + 1, width);

                if (mode == 200) {
                    // Pass: the run continues past b2 without a colour change here.
                    a0 = b2;
                } else if (mode == 100) {
                    // Horizontal: two runs in the current colour then its opposite.
                    const int first = readRun(reader, colour == 0);
                    const int second = readRun(reader, colour != 0);
                    if (first < 0 || second < 0) { rowFailed = true; break; }
                    const int start = a0 < 0 ? 0 : a0;
                    const int a1 = std::min(start + first, width);
                    const int a2 = std::min(a1 + second, width);
                    current.push_back(a1);
                    current.push_back(a2);
                    a0 = a2;
                } else {
                    const int a1 = std::clamp(b1 + mode, 0, width);
                    current.push_back(a1);
                    a0 = a1;
                    colour ^= 1;
                }
            } else {
                const int run = readRun(reader, colour == 0);
                if (run < 0) { rowFailed = true; break; }
                const int start = a0 < 0 ? 0 : a0;
                const int a1 = std::min(start + run, width);
                current.push_back(a1);
                a0 = a1;
                colour ^= 1;
            }
        }

        if (rowFailed && current.empty())
            break;                       // nothing decoded on this row: the image ends here

        // Transitions must be non-decreasing and inside the row; a damaged stream can produce
        // neither, and an unsorted list would make `findB1` walk off the end on the NEXT row.
        std::sort(current.begin(), current.end());
        for (int& position : current)
            position = std::clamp(position, 0, width);

        // Paint. `/BlackIs1 false` (the default) means a BLACK pixel is a 0 bit -- the opposite of
        // the intuitive reading, and getting it backwards produces a perfect negative.
        const std::uint8_t whiteBit = parms.blackIs1 ? 0u : 1u;
        std::fill(row.begin(), row.end(), static_cast<std::uint8_t>(whiteBit ? 0xFF : 0x00));

        int position = 0;
        int paint = 0;                    // 0 = white, 1 = black
        for (const int boundary : current) {
            if (paint == 1) {
                for (int x = position; x < boundary; ++x) {
                    const std::uint8_t mask = static_cast<std::uint8_t>(0x80u >> (x & 7));
                    if (whiteBit)
                        row[static_cast<std::size_t>(x >> 3)] &= static_cast<std::uint8_t>(~mask);
                    else
                        row[static_cast<std::size_t>(x >> 3)] |= mask;
                }
            }
            position = boundary;
            paint ^= 1;
        }
        if (paint == 1) {
            for (int x = position; x < width; ++x) {
                const std::uint8_t mask = static_cast<std::uint8_t>(0x80u >> (x & 7));
                if (whiteBit)
                    row[static_cast<std::size_t>(x >> 3)] &= static_cast<std::uint8_t>(~mask);
                else
                    row[static_cast<std::size_t>(x >> 3)] |= mask;
            }
        }

        out.insert(out.end(), row.begin(), row.end());
        ++rowsDone;

        reference = current;
        if (rowFailed)
            break;
    }

    if (out.empty()) {
        return core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.pdf.filterFailed"),
                           QStringLiteral("CCITTFaxDecode: no row decoded")};
    }
    return out;
}

} // namespace rawpage::pdf::cos::filters
