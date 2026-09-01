#include "pdf/cos/Lexer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rawpage::pdf::cos {
namespace {

// ISO 32000-1 table 1. NUL is whitespace in PDF, which surprises people and matters: a file padded
// with NULs between objects is well-formed, and treating them as regular characters glues the
// padding onto the next keyword.
constexpr bool whitespaceTable(std::uint8_t byte) noexcept
{
    return byte == 0x00 || byte == 0x09 || byte == 0x0A
        || byte == 0x0C || byte == 0x0D || byte == 0x20;
}

} // namespace

bool Lexer::isWhitespace(std::uint8_t byte) noexcept { return whitespaceTable(byte); }

bool Lexer::isDelimiter(std::uint8_t byte) noexcept
{
    // ISO 32000-1 table 2. Note '%' is a delimiter, so `12%comment` ends the number at the percent.
    return byte == '(' || byte == ')' || byte == '<' || byte == '>' || byte == '['
        || byte == ']' || byte == '{' || byte == '}' || byte == '/' || byte == '%';
}

int Lexer::hexValue(std::uint8_t byte) noexcept
{
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

Lexer::Lexer(const std::uint8_t* data, std::size_t size) noexcept
    : data_(data), size_(data ? size : 0)
{
    // A null pointer with a non-zero length is a caller bug that would otherwise become a
    // segfault on the first read. Treating it as empty is not papering over it -- the lexer's
    // contract is that it never reads out of bounds, and this is the one case where the bound
    // itself is the lie.
}

Lexer::Lexer(std::string_view bytes) noexcept
    : Lexer(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())
{
}

void Lexer::seek(std::size_t offset) noexcept
{
    position_ = std::min(offset, size_);
}

void Lexer::skipWhitespace() noexcept
{
    while (position_ < size_ && whitespaceTable(at(position_)))
        ++position_;
}

Token Lexer::make(TokenType type, std::size_t start, LexNote note)
{
    Token token;
    token.type = type;
    token.note = note;
    token.offset = start;
    token.length = position_ - start;
    if (note != LexNote::None)
        ++noteCount_;
    return token;
}

Token Lexer::peek()
{
    const std::size_t saved = position_;
    const std::size_t savedNotes = noteCount_;
    Token token = next();
    position_ = saved;
    // A peek must not change the note count either, or a caller that peeks before every next()
    // doubles every note and `noteCount()` stops meaning anything.
    noteCount_ = savedNotes;
    return token;
}

Token Lexer::peekSignificant()
{
    const std::size_t saved = position_;
    const std::size_t savedNotes = noteCount_;
    Token token = nextSignificant();
    position_ = saved;
    noteCount_ = savedNotes;
    return token;
}

Token Lexer::nextSignificant()
{
    for (;;) {
        Token token = next();
        if (token.type != TokenType::Comment)
            return token;
    }
}

Token Lexer::next()
{
    skipWhitespace();
    if (position_ >= size_) {
        Token token;
        token.type = TokenType::End;
        token.offset = size_;
        return token;
    }

    const std::size_t start = position_;
    const std::uint8_t byte = at(position_);

    switch (byte) {
    case '[': ++position_; return make(TokenType::ArrayOpen, start);
    case ']': ++position_; return make(TokenType::ArrayClose, start);
    case '{': ++position_; return make(TokenType::BraceOpen, start);
    case '}': ++position_; return make(TokenType::BraceClose, start);
    case '(': return lexLiteralString();
    case '/': return lexName();
    case '%': return lexComment();

    case '<':
        // `<<` before `<hex>`: a dictionary opens with two of the same delimiter, and checking the
        // shorter form first would lex every dictionary as an empty hex string followed by junk.
        if (position_ + 1 < size_ && at(position_ + 1) == '<') {
            position_ += 2;
            return make(TokenType::DictOpen, start);
        }
        return lexHexString();

    case '>':
        if (position_ + 1 < size_ && at(position_ + 1) == '>') {
            position_ += 2;
            return make(TokenType::DictClose, start);
        }
        // A lone '>' cannot begin anything. Consume it so the caller advances -- returning End
        // here would be a hang, and returning it repeatedly would be a slower one.
        ++position_;
        return make(TokenType::Junk, start, LexNote::StrayDelimiter);

    case ')':
        // An unmatched close paren. Same reasoning as the lone '>': one byte, one note, keep going.
        ++position_;
        return make(TokenType::Junk, start, LexNote::StrayDelimiter);

    default:
        break;
    }

    if ((byte >= '0' && byte <= '9') || byte == '+' || byte == '-' || byte == '.')
        return lexNumber();

    return lexKeyword();
}

Token Lexer::lexComment()
{
    const std::size_t start = position_;
    ++position_;                                   // the '%'
    const std::size_t bodyStart = position_;
    while (position_ < size_ && at(position_) != 0x0A && at(position_) != 0x0D)
        ++position_;
    Token token = make(TokenType::Comment, start);
    token.text.assign(reinterpret_cast<const char*>(data_ + bodyStart), position_ - bodyStart);
    // The end-of-line is left unconsumed: it is whitespace, and the next call skips it. Eating it
    // here would make a comment's `length` depend on whether the file uses CR, LF or CRLF, and
    // `offset + length` is a byte position Stage 3 copies from.
    return token;
}

Token Lexer::lexNumber()
{
    const std::size_t start = position_;
    LexNote note = LexNote::None;

    // Take the whole regular-character run first, then interpret it. Scanning and validating in one
    // pass is what produces a lexer that stops halfway through `--5` and leaves the tail to be
    // lexed as a second, different token -- and real files contain `--5`.
    while (position_ < size_ && isRegular(at(position_)))
        ++position_;

    bool negative = false;
    bool seenSign = false;
    bool seenPoint = false;
    bool overflow = false;
    bool anyDigit = false;
    unsigned long long magnitude = 0;
    double fraction = 0.0;
    double scale = 1.0;

    for (std::size_t index = start; index < position_; ++index) {
        const std::uint8_t character = at(index);
        if (character == '+' || character == '-') {
            // Only a leading sign is meaningful. A second one, or one after a digit, is a
            // malformation we record and step over -- Acrobat reads `--5` as -5 and so do we.
            if (index == start && !seenSign) {
                negative = character == '-';
                seenSign = true;
            } else {
                note = LexNote::MalformedNumber;
            }
            continue;
        }
        if (character == '.') {
            if (seenPoint)
                note = LexNote::MalformedNumber;   // 1.2.3 -- everything after the second point is
            seenPoint = true;                      // still absorbed below, at diminishing scale
            continue;
        }
        if (character >= '0' && character <= '9') {
            anyDigit = true;
            const int digit = character - '0';
            if (seenPoint) {
                scale *= 0.1;
                fraction += digit * scale;
            } else if (magnitude > (std::numeric_limits<unsigned long long>::max() - digit) / 10) {
                overflow = true;
            } else {
                magnitude = magnitude * 10 + static_cast<unsigned long long>(digit);
            }
            continue;
        }
        // A letter or other regular byte inside what began as a number: `0000000000n` from a
        // damaged xref line, or `12pt`. Noted, and the numeric prefix still yields a value.
        note = LexNote::MalformedNumber;
    }

    if (!anyDigit)
        note = LexNote::MalformedNumber;            // a lone '-', '+', or '.'

    const double magnitudeReal = static_cast<double>(magnitude) + fraction;
    Token token = make(seenPoint ? TokenType::Real : TokenType::Integer, start, note);
    token.real = negative ? -magnitudeReal : magnitudeReal;

    if (!seenPoint) {
        constexpr auto kMax = static_cast<unsigned long long>(std::numeric_limits<long long>::max());
        if (overflow || magnitude > (negative ? kMax + 1 : kMax)) {
            // Clamp rather than wrap. `huge-length-claim.pdf` is a corpus file precisely because a
            // wrapped length becomes a negative one, and a negative length is a size_t away from an
            // allocation the size of the address space.
            token.integer = negative ? std::numeric_limits<long long>::min()
                                     : std::numeric_limits<long long>::max();
            if (token.note == LexNote::None)
                ++noteCount_;
            token.note = LexNote::NumberOutOfRange;
        } else if (negative) {
            token.integer = magnitude == kMax + 1 ? std::numeric_limits<long long>::min()
                                                  : -static_cast<long long>(magnitude);
        } else {
            token.integer = static_cast<long long>(magnitude);
        }
    } else {
        // A Real still reports an integer, truncated, so a caller reading `integer` off a `4.0`
        // that a producer wrote where an integer belonged gets 4 rather than 0.
        const double truncated = std::trunc(token.real);
        if (truncated >= static_cast<double>(std::numeric_limits<long long>::min())
            && truncated <= static_cast<double>(std::numeric_limits<long long>::max()))
            token.integer = static_cast<long long>(truncated);
    }

    return token;
}

Token Lexer::lexName()
{
    const std::size_t start = position_;
    ++position_;                                   // the '/'
    LexNote note = LexNote::None;
    std::string decoded;

    while (position_ < size_ && isRegular(at(position_))) {
        const std::uint8_t character = at(position_);
        if (character != '#') {
            decoded.push_back(static_cast<char>(character));
            ++position_;
            continue;
        }
        // '#' escape. Both guards matter and both are one-byte-from-the-end cases a fuzzer finds
        // immediately: '#' as the final byte of the buffer, and '#4' as the final two.
        const int high = position_ + 1 < size_ ? hexValue(at(position_ + 1)) : -1;
        const int low = position_ + 2 < size_ ? hexValue(at(position_ + 2)) : -1;
        if (high < 0 || low < 0) {
            // `/A#GG`. ISO 32000 does not say what to do; every reader invents an answer, and ours
            // is a NAMED one (corpus/malformed/bad-name-hex-escape.pdf): keep the '#' literally.
            note = LexNote::InvalidNameEscape;
            decoded.push_back('#');
            ++position_;
            continue;
        }
        decoded.push_back(static_cast<char>(high * 16 + low));
        position_ += 3;
    }

    Token token = make(TokenType::Name, start, note);
    token.text = std::move(decoded);
    return token;
}

Token Lexer::lexLiteralString()
{
    const std::size_t start = position_;
    ++position_;                                   // the '('
    int depth = 1;
    std::string decoded;
    LexNote note = LexNote::None;

    while (position_ < size_) {
        const std::uint8_t character = at(position_);

        if (character == '\\') {
            ++position_;
            if (position_ >= size_) {
                // A backslash as the final byte. Nothing to escape; the string is unterminated,
                // which the loop exit below reports.
                break;
            }
            const std::uint8_t escaped = at(position_);
            switch (escaped) {
            case 'n': decoded.push_back('\n'); ++position_; break;
            case 'r': decoded.push_back('\r'); ++position_; break;
            case 't': decoded.push_back('\t'); ++position_; break;
            case 'b': decoded.push_back('\b'); ++position_; break;
            case 'f': decoded.push_back('\f'); ++position_; break;
            case '(': decoded.push_back('('); ++position_; break;
            case ')': decoded.push_back(')'); ++position_; break;
            case '\\': decoded.push_back('\\'); ++position_; break;
            case 0x0D:
                // Line continuation: backslash then EOL contributes nothing. CRLF counts as one.
                ++position_;
                if (position_ < size_ && at(position_) == 0x0A)
                    ++position_;
                break;
            case 0x0A:
                ++position_;
                break;
            default:
                if (escaped >= '0' && escaped <= '7') {
                    // \ddd -- one to three octal digits, taken mod 256 (ISO 32000-1 7.3.4.2).
                    int value = 0;
                    int digits = 0;
                    while (digits < 3 && position_ < size_
                           && at(position_) >= '0' && at(position_) <= '7') {
                        value = value * 8 + (at(position_) - '0');
                        ++position_;
                        ++digits;
                    }
                    decoded.push_back(static_cast<char>(value & 0xFF));
                } else {
                    // "the backslash is ignored" -- the escaped byte stands for itself.
                    decoded.push_back(static_cast<char>(escaped));
                    ++position_;
                }
                break;
            }
            continue;
        }

        if (character == '(') {
            ++depth;
            decoded.push_back('(');
            ++position_;
            continue;
        }
        if (character == ')') {
            --depth;
            ++position_;
            if (depth == 0) {
                Token token = make(TokenType::LiteralString, start);
                token.text = std::move(decoded);
                return token;
            }
            decoded.push_back(')');
            continue;
        }
        if (character == 0x0D) {
            // An end-of-line inside a literal string is a single LF whatever it was written as
            // (ISO 32000-1 7.3.4.2). A reader that passed CRLF through would make the same
            // document hash differently depending on the producer's platform.
            decoded.push_back('\n');
            ++position_;
            if (position_ < size_ && at(position_) == 0x0A)
                ++position_;
            continue;
        }

        decoded.push_back(static_cast<char>(character));
        ++position_;
    }

    note = LexNote::UnterminatedLiteralString;
    Token token = make(TokenType::LiteralString, start, note);
    token.text = std::move(decoded);
    return token;
}

Token Lexer::lexHexString()
{
    const std::size_t start = position_;
    ++position_;                                   // the '<'
    std::string decoded;
    LexNote note = LexNote::None;
    int pending = -1;
    bool terminated = false;

    while (position_ < size_) {
        const std::uint8_t character = at(position_);
        if (character == '>') {
            ++position_;
            terminated = true;
            break;
        }
        ++position_;
        if (whitespaceTable(character))
            continue;                              // whitespace inside <...> is ignored entirely
        const int value = hexValue(character);
        if (value < 0) {
            // Not hex and not whitespace. Skipped, and noted once -- a run of junk should not
            // produce one note per byte, because `noteCount()` is meant to be a signal rather than
            // a length measurement.
            if (note == LexNote::None)
                note = LexNote::InvalidHexDigit;
            continue;
        }
        if (pending < 0) {
            pending = value;
        } else {
            decoded.push_back(static_cast<char>(pending * 16 + value));
            pending = -1;
        }
    }

    if (pending >= 0) {
        // "If the final digit is missing ... it shall be assumed to be 0" -- ISO 32000-1 7.3.4.3.
        decoded.push_back(static_cast<char>(pending * 16));
        if (note == LexNote::None)
            note = LexNote::OddHexDigitCount;
    }
    if (!terminated)
        note = LexNote::UnterminatedHexString;     // outranks the two above: the file ended

    Token token = make(TokenType::HexString, start, note);
    token.text = std::move(decoded);
    return token;
}

Token Lexer::lexKeyword()
{
    const std::size_t start = position_;
    while (position_ < size_ && isRegular(at(position_)))
        ++position_;

    if (position_ == start) {
        // Defensive: `next()` only reaches here for a regular byte, so the run is at least one
        // long. If that ever stops being true this consumes a byte anyway -- the progress
        // guarantee is worth more than the unreachable branch is worth deleting.
        ++position_;
        return make(TokenType::Junk, start, LexNote::StrayDelimiter);
    }

    Token token = make(TokenType::Keyword, start);
    token.text.assign(reinterpret_cast<const char*>(data_ + start), position_ - start);
    return token;
}

const char* tokenTypeName(TokenType type) noexcept
{
    switch (type) {
    case TokenType::End:           return "End";
    case TokenType::Integer:       return "Integer";
    case TokenType::Real:          return "Real";
    case TokenType::LiteralString: return "LiteralString";
    case TokenType::HexString:     return "HexString";
    case TokenType::Name:          return "Name";
    case TokenType::Keyword:       return "Keyword";
    case TokenType::ArrayOpen:     return "ArrayOpen";
    case TokenType::ArrayClose:    return "ArrayClose";
    case TokenType::DictOpen:      return "DictOpen";
    case TokenType::DictClose:     return "DictClose";
    case TokenType::BraceOpen:     return "BraceOpen";
    case TokenType::BraceClose:    return "BraceClose";
    case TokenType::Comment:       return "Comment";
    case TokenType::Junk:          return "Junk";
    }
    return "?";                                    // unreachable: the switch is exhaustive and the
}                                                  // build is -Werror, so adding an enumerator here
                                                   // fails to compile rather than reaching this.
const char* lexNoteName(LexNote note) noexcept
{
    switch (note) {
    case LexNote::None:                      return "None";
    case LexNote::UnterminatedLiteralString: return "UnterminatedLiteralString";
    case LexNote::UnterminatedHexString:     return "UnterminatedHexString";
    case LexNote::OddHexDigitCount:          return "OddHexDigitCount";
    case LexNote::InvalidHexDigit:           return "InvalidHexDigit";
    case LexNote::InvalidNameEscape:         return "InvalidNameEscape";
    case LexNote::MalformedNumber:           return "MalformedNumber";
    case LexNote::NumberOutOfRange:          return "NumberOutOfRange";
    case LexNote::StrayDelimiter:            return "StrayDelimiter";
    }
    return "?";
}

} // namespace rawpage::pdf::cos
