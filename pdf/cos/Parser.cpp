// pdf/cos/Parser -- implementation. Stage 2 T6. See the header for the design and for why the
// /Length recovery is shaped the way it is.

#include "pdf/cos/Parser.h"

#include <QString>

#include <algorithm>
#include <iterator>
#include <utility>

namespace rawpage::pdf::cos {

namespace {

// The keyword whose position ends a stream. Named once so the scan and the skip past it cannot
// disagree about its length -- an off-by-one there would silently move every recovered stream's
// end by nine bytes.
constexpr std::string_view kEndstream = "endstream";

std::string_view viewOf(const std::uint8_t* data, std::size_t size) noexcept
{
    if (data == nullptr || size == 0)
        return {};
    return std::string_view(reinterpret_cast<const char*>(data), size);
}

TokenType openerFor(TokenType closer) noexcept
{
    switch (closer) {
    case TokenType::ArrayClose: return TokenType::ArrayOpen;
    case TokenType::DictClose:  return TokenType::DictOpen;
    default:                    return TokenType::BraceOpen;
    }
}

} // namespace

Parser::Parser(const std::uint8_t* data, std::size_t size) noexcept
    : data_(data), size_(data == nullptr ? 0 : size), lexer_(data, data == nullptr ? 0 : size)
{
}

Parser::Parser(std::string_view bytes) noexcept
    : Parser(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())
{
}

void Parser::setLengthResolver(LengthResolver resolver)
{
    lengthResolver_ = std::move(resolver);
}

std::string_view Parser::source() const noexcept
{
    return viewOf(data_, size_);
}

std::size_t Parser::position() const noexcept
{
    // A pushed-back token has not been consumed, so the logical position is where IT starts --
    // not where the lexer happens to have got to. Everything between the two is whitespace and
    // comments that were skipped on the way, which no caller can act on.
    return pushbackCount_ > 0 ? pushback_[0].offset : lexer_.position();
}

void Parser::seek(std::size_t offset) noexcept
{
    pushbackCount_ = 0;
    lexer_.seek(offset);
}

std::size_t Parser::noteCount(ParseNote which) const noexcept
{
    std::size_t count = 0;
    for (const ParseNoteRecord& record : notes_) {
        if (record.note == which)
            ++count;
    }
    return count;
}

void Parser::note(ParseNote which, std::size_t offset, long long detail)
{
    notes_.push_back(ParseNoteRecord{which, offset, detail});
}

void Parser::noteToken(const Token& token)
{
    if (token.note != LexNote::None)
        note(ParseNote::LexerNote, token.offset, static_cast<long long>(token.note));
}

// ---------------------------------------------------------------------------------------------
// Token supply
// ---------------------------------------------------------------------------------------------

Token Parser::nextToken()
{
    if (pushbackCount_ > 0) {
        Token token = pushback_[0];
        pushback_[0] = pushback_[1];
        --pushbackCount_;
        return token;
    }
    Token token = lexer_.nextSignificant();
    // Recorded HERE, at the single point where a token is produced, which is what makes the note
    // list a census: every token is lexed exactly once (that is what the pushback buys) and
    // therefore noted at most once.
    noteToken(token);
    return token;
}

void Parser::pushBack(const Token& token)
{
    if (pushbackCount_ >= 2)
        return;  // unreachable by construction: nothing looks more than two tokens ahead
    pushback_[1] = pushback_[0];
    pushback_[0] = token;
    ++pushbackCount_;
}

Token Parser::peekToken()
{
    Token token = nextToken();
    pushBack(token);
    return token;
}

// ---------------------------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------------------------

CosObject Parser::parseObject()
{
    return parseValue(0);
}

CosObject Parser::parseValue(int depth)
{
    const Token token = nextToken();

    switch (token.type) {
    case TokenType::End:
        // No span: there are no bytes here. A Null carrying a span means "these bytes are a
        // null", and the end of the buffer is not that.
        return CosObject::makeNull();

    case TokenType::Integer: {
        // The only place this grammar needs lookahead: `12 0 R` is three tokens and its first two
        // are indistinguishable from a pair of ordinary integers in an array.
        const Token second = nextToken();
        if (second.type == TokenType::Integer) {
            const Token third = nextToken();
            if (third.isKeyword("R")) {
                CosObject object = CosObject::makeReference(token.integer, second.integer);
                object.setSpan(token.offset, third.offset + third.length - token.offset);
                return object;
            }
            pushBack(third);
        }
        pushBack(second);
        CosObject object = CosObject::makeInteger(token.integer);
        object.setSpan(token.offset, token.length);
        return object;
    }

    case TokenType::Real: {
        CosObject object = CosObject::makeReal(token.real);
        object.setSpan(token.offset, token.length);
        return object;
    }

    case TokenType::LiteralString: {
        CosObject object = CosObject::makeString(token.text, StringForm::Literal);
        object.setSpan(token.offset, token.length);
        return object;
    }

    case TokenType::HexString: {
        CosObject object = CosObject::makeString(token.text, StringForm::Hex);
        object.setSpan(token.offset, token.length);
        return object;
    }

    case TokenType::Name: {
        CosObject object = CosObject::makeName(token.text);
        object.setSpan(token.offset, token.length);
        return object;
    }

    case TokenType::Keyword: {
        CosObject object;
        if (token.text == "true")
            object = CosObject::makeBoolean(true);
        else if (token.text == "false")
            object = CosObject::makeBoolean(false);
        else if (token.text != "null")
            note(ParseNote::UnexpectedToken, token.offset);
        object.setSpan(token.offset, token.length);
        return object;
    }

    case TokenType::ArrayOpen:
        return parseArray(token, depth);

    case TokenType::DictOpen:
        return parseDictionaryOrStream(token, depth);

    case TokenType::BraceOpen: {
        // A type 4 (PostScript calculator) function body. It is legal PDF and it is not part of
        // the file structure this stage reads, so it is consumed as a unit and reported rather
        // than silently dropped: leaving its contents to be read as top-level junk would put a
        // stray `}` into someone else's diagnostics.
        note(ParseNote::UnexpectedToken, token.offset);
        skipBalanced(TokenType::BraceClose);
        CosObject object = CosObject::makeNull();
        object.setSpan(token.offset, position() - token.offset);
        return object;
    }

    case TokenType::ArrayClose:
    case TokenType::DictClose:
    case TokenType::BraceClose:
    case TokenType::Junk:
    case TokenType::Comment:
        // Comment is unreachable -- `nextSignificant` filters it -- and is listed so that the
        // switch stays exhaustive under -Werror rather than needing a `default` that would hide
        // the next enumerator somebody adds.
        note(ParseNote::UnexpectedToken, token.offset);
        {
            CosObject object = CosObject::makeNull();
            object.setSpan(token.offset, token.length);
            return object;
        }
    }

    return CosObject::makeNull();
}

CosObject Parser::parseArray(const Token& open, int depth)
{
    if (depth >= kMaxDepth) {
        note(ParseNote::DepthLimitExceeded, open.offset, depth);
        skipBalanced(TokenType::ArrayClose);
        CosObject object = CosObject::makeNull();
        object.setSpan(open.offset, position() - open.offset);
        return object;
    }

    CosObject array = CosObject::makeArray({});
    std::size_t end = position();

    for (;;) {
        const Token look = peekToken();
        if (look.type == TokenType::End) {
            note(ParseNote::UnterminatedArray, open.offset);
            end = position();
            break;
        }
        if (look.type == TokenType::ArrayClose) {
            nextToken();
            end = look.offset + look.length;
            break;
        }
        // Progress: `parseValue` consumes at least one token on every path except End, and End is
        // handled above. That is what keeps this loop bounded on hostile input.
        array.append(parseValue(depth + 1));
    }

    // Last, because `append` clears the span -- an object that has been changed can no longer be
    // round-tripped by copying its original bytes, and `CosObject.h` explains why that matters.
    array.setSpan(open.offset, end - open.offset);
    return array;
}

CosObject Parser::parseDictionaryOrStream(const Token& open, int depth)
{
    if (depth >= kMaxDepth) {
        note(ParseNote::DepthLimitExceeded, open.offset, depth);
        skipBalanced(TokenType::DictClose);
        CosObject object = CosObject::makeNull();
        object.setSpan(open.offset, position() - open.offset);
        return object;
    }

    std::vector<CosObject::Entry> entries;
    std::size_t end = position();

    for (;;) {
        const Token look = peekToken();
        if (look.type == TokenType::End) {
            note(ParseNote::UnterminatedDictionary, open.offset);
            end = position();
            break;
        }
        if (look.type == TokenType::DictClose) {
            nextToken();
            end = look.offset + look.length;
            break;
        }

        const Token key = nextToken();
        if (key.type != TokenType::Name) {
            // The key is CONSUMED rather than pushed back, which is both what keeps the loop
            // making progress and the tolerant reading: `<< 1 2 /A 3 >>` still yields /A.
            note(ParseNote::DictionaryKeyNotAName, key.offset);
            continue;
        }

        const Token valueLook = peekToken();
        if (valueLook.type == TokenType::End || valueLook.type == TokenType::DictClose) {
            note(ParseNote::DictionaryValueMissing, key.offset);
            continue;
        }

        CosObject value = parseValue(depth + 1);

        // Both copies are kept -- see `CosObject::Entry` -- so this is a report, not a drop.
        const bool duplicate = std::any_of(
            entries.begin(), entries.end(),
            [&key](const CosObject::Entry& entry) { return entry.key == key.text; });
        if (duplicate)
            note(ParseNote::DuplicateDictionaryKey, key.offset);

        entries.push_back(CosObject::Entry{key.text, std::move(value)});
    }

    CosObject dictionary = CosObject::makeDictionary(std::move(entries));
    dictionary.setSpan(open.offset, end - open.offset);

    // A stream can only follow an indirect object's top-level dictionary (ISO 32000-1 7.3.8.1),
    // and depth 0 is exactly that position -- `parseIndirectObject` parses the body at depth 0.
    // Checking at every depth would let a nested dictionary followed by the letters `stream` in
    // a content stream swallow the rest of the file.
    if (depth == 0) {
        const Token look = peekToken();
        if (look.isKeyword("stream")) {
            nextToken();
            return makeStreamFrom(std::move(dictionary), look);
        }
    }

    return dictionary;
}

std::optional<long long> Parser::declaredStreamLength(const CosObject& dictionary,
                                                      std::size_t dictionaryOffset)
{
    const CosObject* length = dictionary.find("Length");
    if (length == nullptr) {
        note(ParseNote::StreamLengthMissing, dictionaryOffset);
        return std::nullopt;
    }
    if (length->isInteger())
        return length->integerValue();
    if (length->isReference()) {
        if (lengthResolver_) {
            const std::optional<long long> resolved =
                lengthResolver_(length->reference().number, length->reference().generation);
            if (resolved)
                return resolved;
        }
        note(ParseNote::StreamLengthUnresolved, dictionaryOffset, length->reference().number);
        return std::nullopt;
    }
    note(ParseNote::StreamLengthNotAnInteger, dictionaryOffset);
    return std::nullopt;
}

CosObject Parser::makeStreamFrom(CosObject dictionary, const Token& streamKeyword)
{
    // ISO 32000-1 7.3.8.1: the `stream` keyword is followed by CRLF or a single LF, and
    // explicitly NOT by a CR alone. Producers emit all three, plus trailing spaces, so all three
    // are accepted and the two non-conforming spellings are noted -- getting this wrong shifts
    // every stream in the file by one byte, which decodes as corruption rather than as a
    // structural error.
    std::size_t pos = streamKeyword.offset + streamKeyword.length;
    while (pos < size_ && (byteAt(pos) == ' ' || byteAt(pos) == '\t'))
        ++pos;

    if (pos + 1 < size_ && byteAt(pos) == '\r' && byteAt(pos + 1) == '\n') {
        pos += 2;
    } else if (pos < size_ && byteAt(pos) == '\n') {
        ++pos;
    } else if (pos < size_ && byteAt(pos) == '\r') {
        note(ParseNote::StreamKeywordEolMissing, pos);
        ++pos;
    } else {
        note(ParseNote::StreamKeywordEolMissing, pos);
    }

    const std::size_t dataStart = pos;
    const std::size_t available = size_ - dataStart;

    CosStreamData data;
    data.dataOffset = dataStart;

    const std::optional<long long> declared =
        declaredStreamLength(dictionary, dictionary.span().offset);
    data.declaredLength = declared ? *declared : -1;

    // TRUST /Length ONLY WHEN IT LANDS ON `endstream`. Everything else -- absent, negative, past
    // the end of the buffer, an unresolvable reference, or simply wrong -- takes the scan.
    bool accepted = false;
    if (declared && *declared >= 0
        && static_cast<unsigned long long>(*declared) <= static_cast<unsigned long long>(available)) {
        const std::size_t declaredEnd = dataStart + static_cast<std::size_t>(*declared);
        Lexer probe(data_, size_);
        probe.seek(declaredEnd);
        const Token after = probe.nextSignificant();
        if (after.isKeyword("endstream")) {
            accepted = true;
            data.dataLength = declaredEnd - dataStart;
            seek(after.offset + after.length);
        }
    }

    if (!accepted) {
        const std::string_view bytes = source();
        const std::size_t found = bytes.find(kEndstream, dataStart);
        if (found == std::string_view::npos) {
            // `corpus/malformed/truncated-mid-stream.pdf`: an interrupted download. The data runs
            // to the end of what we were given, which is the most useful thing that can be said
            // about it, and the note is what T8 reports.
            note(ParseNote::StreamEndstreamMissing, dataStart);
            data.dataLength = available;
            seek(size_);
        } else {
            // One EOL immediately before `endstream` belongs to the file's layout rather than to
            // the stream. Exactly one: trimming greedily would eat a trailing newline that is
            // genuinely part of a text stream's content.
            std::size_t dataEnd = found;
            if (dataEnd > dataStart && byteAt(dataEnd - 1) == '\n') {
                --dataEnd;
                if (dataEnd > dataStart && byteAt(dataEnd - 1) == '\r')
                    --dataEnd;
            } else if (dataEnd > dataStart && byteAt(dataEnd - 1) == '\r') {
                --dataEnd;
            }
            data.dataLength = dataEnd - dataStart;
            seek(found + kEndstream.size());
        }
        data.lengthRecovered = true;
        note(ParseNote::StreamLengthRecovered, dataStart, data.declaredLength);
    }

    const std::size_t dictionaryOffset = dictionary.span().offset;
    CosObject stream = CosObject::makeStream(std::move(dictionary), data);
    // The span covers the WHOLE construct -- `<<...>> stream ... endstream` -- because that is
    // what Stage 3 copies to leave an untouched stream byte identical. `position()` is just past
    // `endstream`, or at the end of the buffer when there was not one.
    stream.setSpan(dictionaryOffset, position() - dictionaryOffset);
    return stream;
}

void Parser::skipBalanced(TokenType closer)
{
    // Iterative, and that is the whole point: this is the path a construct nested past kMaxDepth
    // takes, so recursing here would reintroduce the stack overflow the limit exists to prevent.
    // Only the MATCHING pair is counted -- a `>>` inside an array being discarded is not this
    // loop's business, and counting every delimiter would make an unbalanced file run to the end.
    const TokenType opener = openerFor(closer);
    int depth = 1;
    while (depth > 0) {
        const Token token = nextToken();
        if (token.type == TokenType::End)
            return;
        if (token.type == opener)
            ++depth;
        else if (token.type == closer)
            --depth;
    }
}

// ---------------------------------------------------------------------------------------------
// Indirect objects
// ---------------------------------------------------------------------------------------------

core::Result<IndirectObject> Parser::parseIndirectObject()
{
    const std::size_t origin = position();

    const Token number = nextToken();
    const Token generation = nextToken();
    const Token keyword = nextToken();

    if (number.type != TokenType::Integer || generation.type != TokenType::Integer
        || !keyword.isKeyword("obj")) {
        // Rewound, so a caller that guessed an offset (T7's xref, T8's repair scan) can try
        // another one without having to remember where it was. A failed probe that also moved the
        // cursor is how a repair pass loses its place.
        seek(origin);
        return core::Error{core::ErrorCode::ParseFailure,
                           QLatin1String("error.pdf.badObjectHeader"),
                           QString::number(static_cast<qulonglong>(origin))};
    }

    IndirectObject result;
    result.number = number.integer;
    result.generation = generation.integer;
    result.object = parseValue(0);

    // Everything past the header is TOLERATED rather than refused: an object whose body is
    // damaged is precisely what T8 has to report on, and returning an error here would discard
    // the part that parsed.
    std::size_t end = position();
    const Token look = peekToken();
    if (look.isKeyword("endobj")) {
        nextToken();
        end = look.offset + look.length;
    } else {
        // `corpus/malformed/missing-endobj.pdf`. The next object header is still found, because
        // nothing here requires `endobj` to continue.
        note(ParseNote::MissingEndobj, number.offset);
    }

    result.span = CosSpan{number.offset, end - number.offset, true};
    return result;
}

core::Result<IndirectObject> Parser::parseIndirectObjectAt(std::size_t offset)
{
    seek(offset);
    return parseIndirectObject();
}

// ---------------------------------------------------------------------------------------------
// Object streams
// ---------------------------------------------------------------------------------------------

core::Result<std::vector<IndirectObject>> Parser::parseObjectStream(
    std::string_view payload, long long count, long long first,
    std::vector<ParseNoteRecord>* notes)
{
    auto record = [notes](ParseNote which, std::size_t offset, long long detail) {
        if (notes != nullptr)
            notes->push_back(ParseNoteRecord{which, offset, detail});
    };

    if (count < 0 || first < 0
        || static_cast<unsigned long long>(first) > payload.size()) {
        return core::Error{core::ErrorCode::ParseFailure,
                           QLatin1String("error.pdf.badObjectStream"),
                           QString::number(static_cast<qlonglong>(count)) + QLatin1String("/")
                               + QString::number(static_cast<qlonglong>(first))};
    }

    // A pair table entry is at least four bytes -- two digits and two separators -- so an `/N`
    // larger than a quarter of the payload cannot be honest. Clamped rather than refused, because
    // the objects that ARE there are still worth recovering, and reported because T8 has to say
    // so. This is also what keeps a `/N 4294967295` from reserving anything.
    long long entries = count;
    const long long ceiling = static_cast<long long>(payload.size() / 4);
    if (entries > ceiling) {
        record(ParseNote::ObjectStreamCountBad, 0, count);
        entries = ceiling;
    }

    std::vector<std::pair<long long, long long>> pairs;
    pairs.reserve(static_cast<std::size_t>(entries));

    Lexer table(payload);
    for (long long index = 0; index < entries; ++index) {
        const Token objectNumber = table.nextSignificant();
        const Token relativeOffset = table.nextSignificant();
        if (objectNumber.type != TokenType::Integer
            || relativeOffset.type != TokenType::Integer) {
            record(ParseNote::ObjectStreamCountBad, objectNumber.offset, index);
            break;
        }
        // The table must live entirely before `/First`. A table that runs past it is the
        // signature of a wrong `/N` or a wrong `/First`, which is what
        // `corpus/malformed/objstm-corrupt.pdf` is built to be.
        if (objectNumber.offset >= static_cast<std::size_t>(first)) {
            record(ParseNote::ObjectStreamCountBad, objectNumber.offset, index);
            break;
        }
        pairs.emplace_back(objectNumber.integer, relativeOffset.integer);
    }

    // Each pair's absolute offset, or none when it is out of range.
    std::vector<std::optional<std::size_t>> starts;
    starts.reserve(pairs.size());
    for (const auto& pair : pairs) {
        // RANGE-CHECKED BEFORE IT IS ADDED, never after: `first + pair.second` with an offset near
        // LLONG_MAX overflows, and a check on the sum is a check the compiler may delete (Stage 2
        // issue I-38, found by `fuzz_cos_parser`). `first` is already known to be within the
        // payload, so the subtraction below cannot wrap.
        if (pair.second < 0
            || static_cast<unsigned long long>(pair.second)
                   > payload.size() - static_cast<std::size_t>(first)) {
            starts.push_back(std::nullopt);
            continue;
        }
        starts.push_back(static_cast<std::size_t>(first + pair.second));
    }

    // EACH PACKED OBJECT ENDS WHERE THE NEXT ONE BEGINS, and until Stage 5 guide I-23 nothing said
    // so: every object was parsed from its offset with the whole rest of the payload in view. An
    // honest object stops on its own, but in a damaged payload -- an unterminated string, array or
    // dictionary -- each object read on to the end, so the work was the object count times the
    // payload, and a 149 KB payload of 934 objects parsed 18.4 MB of span and a million notes, 300 ms
    // per parse, fourteen parses per round trip: a `fuzz_cos_roundtrip` timeout under ASan. Now an
    // object sees only the bytes up to the next larger offset the table names -- the next larger, not
    // the next in table order, because nothing makes a damaged table sorted -- and each distinct
    // offset is parsed once, so the work is linear in the payload. A pair naming an offset an earlier
    // pair already took is dropped and reported: two objects cannot occupy one place, and a deep copy
    // of the first for each later claimant would make the memory quadratic instead of the time.
    std::vector<std::size_t> boundaries;
    boundaries.reserve(starts.size());
    for (const auto& start : starts) {
        if (start)
            boundaries.push_back(*start);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    std::vector<bool> taken(boundaries.size(), false);

    std::vector<IndirectObject> objects;
    objects.reserve(boundaries.size());

    for (std::size_t index = 0; index < pairs.size(); ++index) {
        const long long number = pairs[index].first;
        if (!starts[index]) {
            record(ParseNote::ObjectStreamOffsetBad, 0, number);
            continue;
        }
        const std::size_t absolute = *starts[index];
        const auto at = std::lower_bound(boundaries.begin(), boundaries.end(), absolute);
        const auto slot = static_cast<std::size_t>(at - boundaries.begin());
        if (taken[slot]) {
            record(ParseNote::ObjectStreamOffsetBad, absolute, number);
            continue;
        }
        taken[slot] = true;
        const std::size_t end = std::next(at) != boundaries.end() ? *std::next(at) : payload.size();

        // The view keeps the payload's START, so every span and note offset stays relative to
        // `payload`, as the header promises; only its end moves in.
        Parser inner(payload.substr(0, end));
        inner.seek(absolute);
        IndirectObject entry;
        entry.number = number;
        // Zero by definition: ISO 32000-1 7.5.7 forbids a non-zero generation in an object
        // stream, so this is the specification rather than an assumption about the file.
        entry.generation = 0;
        entry.object = inner.parseObject();
        entry.span = entry.object.span();
        objects.push_back(std::move(entry));

        // Per object, and copied only when asked for. A caller that passes no list -- `XrefTable` and
        // `XrefRepair` both -- has each object's notes freed with its parser, where one parser for
        // the whole stream held a million of them.
        if (notes != nullptr) {
            const std::vector<ParseNoteRecord>& innerNotes = inner.notes();
            notes->insert(notes->end(), innerNotes.begin(), innerNotes.end());
        }
    }

    return objects;
}

// No `default:` label, so a new enumerator is a -Wswitch error rather than a "?" in a report.
const char* parseNoteName(ParseNote which) noexcept
{
    switch (which) {
    case ParseNote::None:                    return "None";
    case ParseNote::UnexpectedToken:         return "UnexpectedToken";
    case ParseNote::UnterminatedArray:       return "UnterminatedArray";
    case ParseNote::UnterminatedDictionary:  return "UnterminatedDictionary";
    case ParseNote::DictionaryKeyNotAName:   return "DictionaryKeyNotAName";
    case ParseNote::DictionaryValueMissing:  return "DictionaryValueMissing";
    case ParseNote::DuplicateDictionaryKey:  return "DuplicateDictionaryKey";
    case ParseNote::DepthLimitExceeded:      return "DepthLimitExceeded";
    case ParseNote::MissingEndobj:           return "MissingEndobj";
    case ParseNote::StreamLengthMissing:     return "StreamLengthMissing";
    case ParseNote::StreamLengthNotAnInteger:return "StreamLengthNotAnInteger";
    case ParseNote::StreamLengthUnresolved:  return "StreamLengthUnresolved";
    case ParseNote::StreamLengthRecovered:   return "StreamLengthRecovered";
    case ParseNote::StreamEndstreamMissing:  return "StreamEndstreamMissing";
    case ParseNote::StreamKeywordEolMissing: return "StreamKeywordEolMissing";
    case ParseNote::ObjectStreamCountBad:    return "ObjectStreamCountBad";
    case ParseNote::ObjectStreamOffsetBad:   return "ObjectStreamOffsetBad";
    case ParseNote::LexerNote:               return "LexerNote";
    }
    return "?";
}

} // namespace rawpage::pdf::cos
