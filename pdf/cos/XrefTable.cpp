// pdf/cos/XrefTable -- implementation. Stage 2 T7. See the header for the design.

#include "pdf/cos/XrefTable.h"

#include "pdf/cos/filters/Filter.h"

#include <algorithm>
#include <set>

namespace rawpage::pdf::cos {
namespace {

core::Error xrefError(const char* messageKey, const QString& context)
{
    return core::Error{core::ErrorCode::ParseFailure, QLatin1String(messageKey), context};
}

// Every key a merged trailer takes from the newest section that has it. `/Prev` and `/XRefStm` are
// deliberately absent: they are chain plumbing, and carrying them into the merged view would make
// a caller think the merged trailer still had a previous revision to walk.
constexpr std::string_view kTrailerKeys[] = {"Root", "Info", "Encrypt", "ID", "Size"};

} // namespace

std::size_t findStartxref(std::string_view source, long long& offsetOut)
{
    offsetOut = -1;
    const std::size_t at = source.rfind("startxref");
    if (at == std::string_view::npos)
        return std::string_view::npos;

    // ISO 32000-1 says `startxref` sits within the last 1,024 bytes. It is found by searching the
    // WHOLE file from the end instead, because `corpus/malformed/no-eof-marker.pdf` has trailing
    // junk after it and a reader that only looked in the last kilobyte would call that file
    // unopenable when its table is perfectly good.
    Lexer lexer(source);
    lexer.seek(at + 9);
    const Token number = lexer.nextSignificant();
    if (number.type == TokenType::Integer)
        offsetOut = number.integer;
    return at;
}

const char* xrefNoteName(XrefNote note) noexcept
{
    switch (note) {
    case XrefNote::None:                   return "None";
    case XrefNote::StartxrefMissing:       return "StartxrefMissing";
    case XrefNote::StartxrefOutOfRange:    return "StartxrefOutOfRange";
    case XrefNote::StartxrefNotATable:     return "StartxrefNotATable";
    case XrefNote::SubsectionMalformed:    return "SubsectionMalformed";
    case XrefNote::EntryMalformed:         return "EntryMalformed";
    case XrefNote::PrevLoop:               return "PrevLoop";
    case XrefNote::PrevOutOfRange:         return "PrevOutOfRange";
    case XrefNote::XrefStreamUndecodable:  return "XrefStreamUndecodable";
    case XrefNote::XrefStreamFieldsBad:    return "XrefStreamFieldsBad";
    case XrefNote::TrailerMissing:         return "TrailerMissing";
    case XrefNote::SizeDisagrees:          return "SizeDisagrees";
    case XrefNote::ObjectStreamUnreadable: return "ObjectStreamUnreadable";
    case XrefNote::ObjectOffsetWrong:      return "ObjectOffsetWrong";
    }
    return "?";
}

void XrefTable::note(XrefNote which, std::size_t offset, long long detail)
{
    notes_.push_back(XrefNoteRecord{which, offset, detail});
}

std::size_t XrefTable::noteCount(XrefNote which) const
{
    std::size_t count = 0;
    for (const XrefNoteRecord& record : notes_) {
        if (record.note == which)
            ++count;
    }
    return count;
}

void XrefTable::mergeEntry(long long number, const XrefEntry& entry)
{
    // NEWEST WINS. Sections are walked newest first, so an object number already present came from
    // a newer revision and is not overwritten. Reversing this reads the document as it was two
    // saves ago, with no error anywhere -- which is why the rule lives in one place.
    // The backstop for kMaxObjectNumber (XrefTable.h, Stage 3 guide I-13). Each caller also checks
    // BEFORE it adds, because the addition is where the overflow was.
    if (number < 0 || number > kMaxObjectNumber)
        return;
    entries_.emplace(number, entry);
}

const XrefEntry* XrefTable::find(long long number) const
{
    const auto found = entries_.find(number);
    return found == entries_.end() ? nullptr : &found->second;
}

long long XrefTable::largestObjectNumber() const
{
    return entries_.empty() ? -1 : entries_.rbegin()->first;
}

bool XrefTable::isEncrypted() const
{
    const CosObject* encrypt = trailer_.find("Encrypt");
    return encrypt != nullptr && !encrypt->isNull();
}

// --------------------------------------------------------------------------------------------
// Classic tables
// --------------------------------------------------------------------------------------------

XrefTable::SectionResult XrefTable::readClassic(std::size_t offset, int revision)
{
    SectionResult result;
    Lexer lexer(source_);
    lexer.seek(offset);

    const Token keyword = lexer.nextSignificant();
    if (!keyword.isKeyword("xref"))
        return result;

    for (;;) {
        const std::size_t subsectionAt = lexer.position();
        const Token first = lexer.peekSignificant();
        if (first.isKeyword("trailer") || first.isEnd())
            break;

        const Token start = lexer.nextSignificant();
        const Token count = lexer.nextSignificant();
        if (start.type != TokenType::Integer || count.type != TokenType::Integer) {
            note(XrefNote::SubsectionMalformed, subsectionAt);
            break;
        }
        // A start past the limit is refused before `start + index` is ever computed: near
        // LLONG_MAX that sum overflows (Stage 3 guide I-13, found by `fuzz_cos_roundtrip`'s soak).
        if (count.integer < 0 || start.integer < 0 || start.integer > kMaxObjectNumber) {
            note(XrefNote::SubsectionMalformed, subsectionAt, count.integer);
            break;
        }

        for (long long index = 0; index < count.integer; ++index) {
            // Read the three fields as TOKENS rather than by seeking twenty bytes at a time. The
            // record is specified as exactly twenty bytes, and producers get that wrong constantly
            // -- one byte of line ending instead of two is the classic -- so lexing is what makes
            // `corpus/malformed/xref-offsets-short.pdf` readable at all.
            const std::size_t entryAt = lexer.position();
            const Token field2 = lexer.nextSignificant();
            const Token field3 = lexer.nextSignificant();
            const Token kind = lexer.nextSignificant();
            if (field2.type != TokenType::Integer || field3.type != TokenType::Integer
                || kind.type != TokenType::Keyword) {
                note(XrefNote::EntryMalformed, entryAt, start.integer + index);
                index = count.integer;   // this subsection is unusable past here
                break;
            }

            // `start` is at most kMaxObjectNumber and `index` counts records actually read from the
            // buffer, so the sum cannot overflow; past the limit, the rest of the subsection names
            // nothing a reader resolves.
            if (index > kMaxObjectNumber - start.integer) {
                note(XrefNote::SubsectionMalformed, subsectionAt, start.integer);
                break;
            }

            XrefEntry entry;
            entry.revision = revision;
            entry.field2 = field2.integer;
            entry.field3 = field3.integer;
            entry.type = kind.text == "n" ? XrefEntryType::InFile : XrefEntryType::Free;
            if (kind.text != "n" && kind.text != "f")
                note(XrefNote::EntryMalformed, entryAt, start.integer + index);

            // Free entries are merged too, and deliberately: a free entry in a NEWER revision is
            // what deletes an object, so dropping it here would resurrect a deleted object from
            // an older section further down the /Prev chain.
            mergeEntry(start.integer + index, entry);
        }
    }

    const Token trailerKeyword = lexer.nextSignificant();
    if (!trailerKeyword.isKeyword("trailer")) {
        note(XrefNote::TrailerMissing, offset);
        result.ok = true;             // the ENTRIES are still usable; only the trailer is missing
        return result;
    }

    Parser parser(source_);
    parser.seek(lexer.position());
    result.trailer = parser.parseObject();
    if (!result.trailer.isDictionary()) {
        note(XrefNote::TrailerMissing, lexer.position());
        result.trailer = CosObject::makeDictionary({});
    }
    result.ok = true;
    return result;
}

// --------------------------------------------------------------------------------------------
// Cross-reference streams
// --------------------------------------------------------------------------------------------

XrefTable::SectionResult XrefTable::readStream(std::size_t offset, int revision)
{
    SectionResult result;

    Parser parser(source_);
    auto object = parser.parseIndirectObjectAt(offset);
    if (!object)
        return result;

    const CosObject& stream = object.value().object;
    if (!stream.isStream())
        return result;
    if (stream.get("Type").text() != "XRef")
        return result;

    auto decoded = decodeStream(stream, source_);
    if (!decoded) {
        note(XrefNote::XrefStreamUndecodable, offset);
        return result;
    }
    const Bytes& rows = decoded.value();

    // `/W` gives the byte width of each of the three fields. A zero width means "use the default",
    // which is 1 for the type field and 0 for the others -- so `/W [0 4 2]` is a legal table whose
    // every entry is type 1, and a reader that treated a missing width as a malformed table would
    // reject it.
    const CosObject& widths = stream.get("W");
    if (!widths.isArray() || widths.items().size() < 3) {
        note(XrefNote::XrefStreamFieldsBad, offset);
        return result;
    }
    int width[3] = {0, 0, 0};
    for (int index = 0; index < 3; ++index) {
        const long long value = widths.items()[static_cast<std::size_t>(index)].integerValue(-1);
        if (value < 0 || value > 8) {
            note(XrefNote::XrefStreamFieldsBad, offset, value);
            return result;
        }
        width[index] = static_cast<int>(value);
    }
    const std::size_t rowBytes =
        static_cast<std::size_t>(width[0]) + width[1] + width[2];
    if (rowBytes == 0) {
        note(XrefNote::XrefStreamFieldsBad, offset);
        return result;
    }

    // `/Index` is pairs of (first object number, count); its default is `[0 /Size]`.
    std::vector<std::pair<long long, long long>> ranges;
    const CosObject& index = stream.get("Index");
    if (index.isArray() && index.items().size() >= 2) {
        for (std::size_t at = 0; at + 1 < index.items().size(); at += 2) {
            ranges.emplace_back(index.items()[at].integerValue(),
                                index.items()[at + 1].integerValue());
        }
    } else {
        ranges.emplace_back(0LL, stream.get("Size").integerValue());
    }

    std::size_t cursor = 0;
    // Accumulated UNSIGNED: an eight-byte field (`/W` allows 8) shifts a signed value's high bits out
    // of range, which is undefined (Stage 3 guide I-13). The conversion back is modular in C++20.
    auto readField = [&rows, &cursor](int bytes) -> long long {
        unsigned long long value = 0;
        for (int byte = 0; byte < bytes; ++byte) {
            value = (value << 8) | static_cast<unsigned long long>(rows[cursor]);
            ++cursor;
        }
        return static_cast<long long>(value);
    };

    for (const auto& [firstNumber, count] : ranges) {
        if (count < 0)
            continue;
        for (long long item = 0; item < count; ++item) {
            if (cursor + rowBytes > rows.size()) {
                // A truncated table is not a failure: the rows that ARE there still name objects,
                // and T8 reports the shortfall. Failing here would lose a document over its tail.
                note(XrefNote::XrefStreamFieldsBad, offset,
                     static_cast<long long>(rows.size()));
                break;
            }
            const long long type = width[0] == 0 ? 1 : readField(width[0]);
            const long long field2 = readField(width[1]);
            const long long field3 = readField(width[2]);

            XrefEntry entry;
            entry.revision = revision;
            entry.field2 = field2;
            entry.field3 = field3;
            switch (type) {
            case 0:  entry.type = XrefEntryType::Free; break;
            case 1:  entry.type = XrefEntryType::InFile; break;
            case 2:  entry.type = XrefEntryType::InObjectStream; break;
            default: entry.type = XrefEntryType::Free; break;   // reserved; treated as absent
            }
            // Read the row either way, so the cursor stays on the next one; place it only when the
            // number is one a file may use -- checked before `firstNumber + item` is computed
            // (Stage 3 guide I-13).
            if (firstNumber < 0 || firstNumber > kMaxObjectNumber || item > kMaxObjectNumber - firstNumber) {
                note(XrefNote::XrefStreamFieldsBad, offset, firstNumber);
                continue;
            }
            mergeEntry(firstNumber + item, entry);
        }
    }

    // A cross-reference stream's own dictionary IS the trailer -- there is no separate `trailer`
    // keyword in a 1.5-style file, and a reader looking for one finds nothing and loses `/Root`.
    result.trailer = CosObject::makeDictionary(
        std::vector<CosObject::Entry>(stream.entries()));
    result.ok = true;
    return result;
}

XrefTable::SectionResult XrefTable::readSection(std::size_t offset, int revision, bool& isStream)
{
    isStream = false;
    SectionResult classic = readClassic(offset, revision);
    if (classic.ok)
        return classic;

    isStream = true;
    return readStream(offset, revision);
}

// --------------------------------------------------------------------------------------------
// Loading
// --------------------------------------------------------------------------------------------

core::Result<XrefTable> XrefTable::load(std::string_view source)
{
    XrefTable table;
    table.source_ = source;

    long long start = -1;
    const std::size_t startxrefAt = findStartxref(source, start);
    if (startxrefAt == std::string_view::npos) {
        // `corpus/malformed/xref-missing.pdf`. Named rather than generic, because "there is no
        // startxref" and "the table it names is broken" are DIFFERENT repairs at T8.
        table.note(XrefNote::StartxrefMissing, 0);
        return xrefError("error.pdf.noXref", QStringLiteral("startxref"));
    }
    if (start < 0 || static_cast<unsigned long long>(start) >= source.size()) {
        // `corpus/malformed/startxref-past-eof.pdf`.
        table.note(XrefNote::StartxrefOutOfRange, startxrefAt, start);
        return xrefError("error.pdf.noXref", QString::number(static_cast<qlonglong>(start)));
    }

    // Every offset already visited, so a `/Prev` that points back at itself ends the walk instead
    // of hanging. `corpus/malformed/circular-prev.pdf` is the fixture and A-21 forbids the hang.
    std::set<std::size_t> visited;
    std::vector<std::size_t> pending{static_cast<std::size_t>(start)};
    int revision = 0;

    while (!pending.empty()) {
        const std::size_t offset = pending.front();
        pending.erase(pending.begin());

        if (!visited.insert(offset).second) {
            table.note(XrefNote::PrevLoop, offset);
            continue;
        }
        if (offset >= source.size()) {
            table.note(XrefNote::PrevOutOfRange, offset);
            continue;
        }

        bool isStream = false;
        SectionResult section = table.readSection(offset, revision, isStream);
        if (!section.ok) {
            // `corpus/malformed/startxref-into-object.pdf`: the offset is inside the file and holds
            // something that is not a table.
            table.note(XrefNote::StartxrefNotATable, offset);
            continue;
        }

        XrefSection record;
        record.offset = offset;
        record.isStream = isStream;
        record.trailer = section.trailer;
        table.sections_.push_back(std::move(record));

        // A hybrid file: an old-style table whose trailer also names a cross-reference stream for
        // the objects the classic form cannot describe. Read BEFORE `/Prev` so it is still newer
        // than the previous revision, and after the classic table so the classic entries win --
        // which is the precedence ISO 32000-1 7.5.8.4 specifies.
        if (const CosObject* xrefStm = section.trailer.find("XRefStm");
            xrefStm != nullptr && xrefStm->isInteger()) {
            const long long hybrid = xrefStm->integerValue();
            if (hybrid >= 0 && static_cast<unsigned long long>(hybrid) < source.size()
                && visited.count(static_cast<std::size_t>(hybrid)) == 0) {
                bool hybridIsStream = false;
                SectionResult extra =
                    table.readSection(static_cast<std::size_t>(hybrid), revision, hybridIsStream);
                visited.insert(static_cast<std::size_t>(hybrid));
                if (extra.ok) {
                    XrefSection hybridRecord;
                    hybridRecord.offset = static_cast<std::size_t>(hybrid);
                    hybridRecord.isStream = hybridIsStream;
                    hybridRecord.fromXrefStm = true;
                    hybridRecord.trailer = extra.trailer;
                    table.sections_.push_back(std::move(hybridRecord));
                }
            }
        }

        if (const CosObject* prev = section.trailer.find("Prev");
            prev != nullptr && prev->isInteger()) {
            const long long previous = prev->integerValue();
            if (previous < 0 || static_cast<unsigned long long>(previous) >= source.size())
                table.note(XrefNote::PrevOutOfRange, offset, previous);
            else
                pending.push_back(static_cast<std::size_t>(previous));
        }
        ++revision;
    }

    if (table.sections_.empty())
        return xrefError("error.pdf.noXref", QStringLiteral("no usable section"));

    // The merged trailer: each key from the NEWEST section that carries it. A caller reading only
    // the newest section misses `/Root` on any file whose last revision did not restate it.
    std::vector<CosObject::Entry> merged;
    for (const std::string_view key : kTrailerKeys) {
        for (const XrefSection& section : table.sections_) {
            if (const CosObject* value = section.trailer.find(key); value != nullptr) {
                merged.push_back(CosObject::Entry{std::string(key), *value});
                break;
            }
        }
    }
    table.trailer_ = CosObject::makeDictionary(std::move(merged));
    table.declaredSize_ = table.trailer_.get("Size").integerValue();

    // `corpus/malformed/size-too-small.pdf`: `/Size` is a CLAIM, and this is where the claim meets
    // the table. Noted rather than corrected -- Stage 3's writer needs to know the file lied.
    if (table.declaredSize_ > 0 && table.largestObjectNumber() >= table.declaredSize_) {
        table.note(XrefNote::SizeDisagrees, 0, table.largestObjectNumber());
    }

    return table;
}

XrefTable XrefTable::rebuilt(std::string_view source, std::map<long long, XrefEntry> entries,
                             CosObject trailer, std::vector<XrefNoteRecord> notes)
{
    XrefTable table;
    table.source_ = source;
    table.entries_ = std::move(entries);
    table.trailer_ = std::move(trailer);
    table.notes_ = std::move(notes);
    table.declaredSize_ = table.trailer_.get("Size").integerValue();
    // No `sections_`: a reconstruction has no revisions, and reporting one would tell Stage 3's
    // incremental writer there was a /Prev chain to append to when there is not.
    return table;
}

// --------------------------------------------------------------------------------------------
// Fetching
// --------------------------------------------------------------------------------------------

const std::vector<IndirectObject>* XrefTable::objectStreamFor(long long number)
{
    if (const auto cached = objectStreams_.find(number); cached != objectStreams_.end())
        return &cached->second;

    // A container that claims to contain itself, or two containers naming each other, would
    // recurse without bound. The in-progress list is small by construction -- nesting deeper than
    // one is already malformed -- so a linear scan is the right shape.
    if (std::find(objectStreamsInProgress_.begin(), objectStreamsInProgress_.end(), number)
        != objectStreamsInProgress_.end()) {
        note(XrefNote::ObjectStreamUnreadable, 0, number);
        return nullptr;
    }
    objectStreamsInProgress_.push_back(number);
    struct Pop {
        std::vector<long long>& list;
        ~Pop() { list.pop_back(); }
    } pop{objectStreamsInProgress_};

    const XrefEntry* entry = find(number);
    if (entry == nullptr || entry->type != XrefEntryType::InFile) {
        note(XrefNote::ObjectStreamUnreadable, 0, number);
        return nullptr;
    }

    Parser parser(source_);
    parser.setLengthResolver(lengthResolver());
    auto container = parser.parseIndirectObjectAt(static_cast<std::size_t>(entry->field2));
    if (!container || !container.value().object.isStream()) {
        note(XrefNote::ObjectStreamUnreadable, static_cast<std::size_t>(entry->field2), number);
        return nullptr;
    }

    const CosObject& stream = container.value().object;
    auto payload = decodeStream(stream, source_);
    if (!payload) {
        // The commonest reason to land here at T7 is a filter this build does not have -- JBIG2,
        // or `/Crypt` on an encrypted file (D2-6). Both say so by name one layer down.
        note(XrefNote::ObjectStreamUnreadable, static_cast<std::size_t>(entry->field2), number);
        return nullptr;
    }

    const Bytes& bytes = payload.value();
    auto packed = Parser::parseObjectStream(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
        stream.get("N").integerValue(), stream.get("First").integerValue());
    if (!packed) {
        note(XrefNote::ObjectStreamUnreadable, static_cast<std::size_t>(entry->field2), number);
        return nullptr;
    }

    // The decoded payload dies with this call, and the packed objects' spans are measured INTO it,
    // not into the file. `CosObject::raw()` takes the buffer as an argument, so a caller handing it
    // the FILE would get a slice of the file from the wrong place -- not a crash, which is worse. A
    // packed object is re-serialized rather than copied, which is correct: it was never a byte range
    // in the file to begin with.
    //
    // RECURSIVELY, and until Stage 3 T2 it was not: only the top-level object's span was cleared, so
    // every nested value of a packed page dictionary still carried a payload-relative span into a
    // buffer that no longer existed -- while this comment said the spans were cleared. Stage 3 guide
    // issue I-04, found by reading this function before the incremental writer was built on it.
    std::vector<IndirectObject> objects = std::move(packed).value();
    for (IndirectObject& item : objects) {
        item.object.clearSpansRecursively();
        item.span = CosSpan{};
    }

    return &objectStreams_.emplace(number, std::move(objects)).first->second;
}

core::Result<IndirectObject> XrefTable::fetch(long long number)
{
    const XrefEntry* entry = find(number);
    if (entry == nullptr) {
        return core::Error{core::ErrorCode::NotFound, QLatin1String("error.pdf.objectMissing"),
                           QString::number(static_cast<qlonglong>(number))};
    }

    if (entry->type == XrefEntryType::Free) {
        return core::Error{core::ErrorCode::NotFound, QLatin1String("error.pdf.objectFree"),
                           QString::number(static_cast<qlonglong>(number))};
    }

    if (entry->type == XrefEntryType::InObjectStream) {
        const std::vector<IndirectObject>* packed = objectStreamFor(entry->field2);
        if (packed == nullptr) {
            return core::Error{core::ErrorCode::ParseFailure,
                               QLatin1String("error.pdf.objectStreamUnreadable"),
                               QString::number(static_cast<qlonglong>(entry->field2))};
        }
        // The index is a hint, not a promise: `corpus/malformed/objstm-corrupt.pdf` shifts every
        // packed object, so the NUMBER is checked and the index is only a starting guess.
        if (entry->field3 >= 0
            && static_cast<std::size_t>(entry->field3) < packed->size()
            && (*packed)[static_cast<std::size_t>(entry->field3)].number == number) {
            return (*packed)[static_cast<std::size_t>(entry->field3)];
        }
        for (const IndirectObject& item : *packed) {
            if (item.number == number)
                return item;
        }
        return core::Error{core::ErrorCode::NotFound, QLatin1String("error.pdf.objectMissing"),
                           QString::number(static_cast<qlonglong>(number))};
    }

    Parser parser(source_);
    parser.setLengthResolver(lengthResolver());
    auto object = parser.parseIndirectObjectAt(static_cast<std::size_t>(entry->field2));
    if (!object) {
        // A wrong offset has TWO failure shapes and both are the same finding. It can land
        // mid-object and still parse as some other object -- caught below -- or it can land
        // somewhere that is not an object header at all, which lands here. Noting only the first
        // was measured wrong: `corpus/malformed/xref-offsets-short.pdf` produced eight unusable
        // objects and zero notes, so T8 would have had nothing to report on the commonest
        // damaged-xref file there is.
        note(XrefNote::ObjectOffsetWrong, static_cast<std::size_t>(entry->field2), number);
        return object.error();
    }

    // THE OFFSET IS A CLAIM AND THIS IS WHERE IT IS CHECKED.
    // `corpus/malformed/xref-offsets-short.pdf` has every offset twelve bytes short from object 4
    // onward, so each one lands mid-object and parses as SOMETHING. Comparing the number that came
    // back against the number asked for is what turns that into a report instead of silently
    // returning the wrong object -- which is the failure mode nobody notices until a page renders
    // as a font descriptor.
    if (object.value().number != number) {
        note(XrefNote::ObjectOffsetWrong, static_cast<std::size_t>(entry->field2), number);
        return core::Error{core::ErrorCode::ParseFailure,
                           QLatin1String("error.pdf.objectOffsetWrong"),
                           QString::number(static_cast<qlonglong>(number))};
    }
    return object;
}

LengthResolver XrefTable::lengthResolver()
{
    return [this](long long number, long long) -> std::optional<long long> {
        // Re-entrancy guard: a stream whose `/Length` names an object that itself lives in a
        // stream would recurse. One level is all any legal file needs.
        if (resolvingLength_)
            return std::nullopt;
        resolvingLength_ = true;
        struct Reset {
            bool& flag;
            ~Reset() { flag = false; }
        } reset{resolvingLength_};

        const XrefEntry* entry = find(number);
        if (entry == nullptr || entry->type != XrefEntryType::InFile)
            return std::nullopt;

        Parser parser(source_);
        auto object = parser.parseIndirectObjectAt(static_cast<std::size_t>(entry->field2));
        if (!object || object.value().number != number)
            return std::nullopt;
        if (!object.value().object.isInteger())
            return std::nullopt;
        return object.value().object.integerValue();
    };
}

} // namespace rawpage::pdf::cos
