// pdf/cos/IncrementalWriter -- implementation. Stage 3 T2. See the header for what an update holds.

#include "pdf/cos/IncrementalWriter.h"

#include "pdf/cos/XrefTable.h"
#include "pdf/cos/filters/Filter.h"

#include <QByteArray>
#include <QCryptographicHash>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <set>

namespace rawpage::pdf::cos {
namespace {

core::Error writeError(core::ErrorCode code, const char* key, long long detail = -1)
{
    return core::Error{code, QLatin1String(key),
                       detail < 0 ? QString() : QString::number(static_cast<qlonglong>(detail))};
}

// --------------------------------------------------------------------------------------------
// Serialization
// --------------------------------------------------------------------------------------------

bool isRegularNameByte(unsigned char byte)
{
    if (byte < 0x21 || byte > 0x7E)
        return false;
    switch (byte) {
    case '(': case ')': case '<': case '>': case '[': case ']':
    case '{': case '}': case '/': case '%': case '#':
        return false;
    default:
        return true;
    }
}

void appendHexByte(std::string& out, unsigned char byte)
{
    static const char digits[] = "0123456789ABCDEF";
    out += digits[byte >> 4];
    out += digits[byte & 0x0F];
}

void appendInteger(std::string& out, long long value)
{
    char buffer[24];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    out.append(buffer, result.ptr);
}

// Zero-padded to `width` digits. A value too wide for the field is written whole rather than
// truncated -- a wrong offset is worse than a long record, which the reader tolerates.
void appendPadded(std::string& out, long long value, std::size_t width)
{
    std::string digits;
    appendInteger(digits, value < 0 ? 0 : value);
    if (digits.size() < width)
        out.append(width - digits.size(), '0');
    out += digits;
}

void appendReal(std::string& out, double value)
{
    // A parsed real is always finite (the lexer never produces anything else), but a constructed
    // one might not be, and PDF has no spelling for either -- 0 is the least surprising value.
    if (!std::isfinite(value))
        value = 0.0;
    // Shortest round-trip, FIXED notation: PDF has no exponent syntax, so `1e-05` would be read as
    // garbage. And the type is kept: a real whose shortest spelling has no point gets `.0`, or a
    // re-read would hand back an Integer where a Real was written.
    char buffer[400];
    const auto result =
        std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed);
    const std::string_view text(buffer, static_cast<std::size_t>(result.ptr - buffer));
    out += text;
    if (text.find('.') == std::string_view::npos)
        out += ".0";
}

void appendString(std::string& out, const CosObject& object)
{
    const std::string& bytes = object.text();
    if (object.stringForm() == StringForm::Hex) {
        out += '<';
        for (unsigned char byte : bytes)
            appendHexByte(out, byte);
        out += '>';
        return;
    }
    // A literal, with every byte a reader could misparse escaped. Unbalanced parentheses are legal
    // when escaped and ambiguous when not, so all three delimiters are always escaped; control and
    // high bytes are octal so the output is 7-bit and survives any tool that looks at it as text.
    out += '(';
    for (unsigned char byte : bytes) {
        switch (byte) {
        case '(': out += "\\("; break;
        case ')': out += "\\)"; break;
        case '\\': out += "\\\\"; break;
        case '\r': out += "\\r"; break;
        case '\n': out += "\\n"; break;
        default:
            if (byte < 0x20 || byte > 0x7E) {
                out += '\\';
                out += static_cast<char>('0' + ((byte >> 6) & 7));
                out += static_cast<char>('0' + ((byte >> 3) & 7));
                out += static_cast<char>('0' + (byte & 7));
            } else {
                out += static_cast<char>(byte);
            }
        }
    }
    out += ')';
}

void appendName(std::string& out, const std::string& name)
{
    out += '/';
    for (unsigned char byte : name) {
        if (isRegularNameByte(byte)) {
            out += static_cast<char>(byte);
        } else {
            out += '#';
            appendHexByte(out, byte);
        }
    }
}

void appendObject(std::string& out, const CosObject& object)
{
    switch (object.type()) {
    case CosType::Null:
        out += "null";
        return;
    case CosType::Boolean:
        out += object.booleanValue() ? "true" : "false";
        return;
    case CosType::Integer:
        appendInteger(out, object.integerValue());
        return;
    case CosType::Real:
        appendReal(out, object.numberValue());
        return;
    case CosType::String:
        appendString(out, object);
        return;
    case CosType::Name:
        appendName(out, object.text());
        return;
    case CosType::Array: {
        out += '[';
        bool first = true;
        for (const CosObject& item : object.items()) {
            if (!first)
                out += ' ';
            appendObject(out, item);
            first = false;
        }
        out += ']';
        return;
    }
    case CosType::Dictionary:
    case CosType::Stream: {
        // Source order and duplicates kept, as `CosObject` keeps them: a rewritten dictionary that
        // reordered or deduplicated its keys would differ from what the reader built for no reason
        // anyone asked for, and `pdf/ops/` compares the two.
        out += "<<";
        for (const CosObject::Entry& entry : object.entries()) {
            appendName(out, entry.key);
            out += ' ';
            appendObject(out, entry.value);
        }
        out += ">>";
        return;
    }
    case CosType::Reference:
        appendInteger(out, object.reference().number);
        out += ' ';
        appendInteger(out, object.reference().generation);
        out += " R";
        return;
    }
}

// One indirect object, envelope included. A stream's `/Length` is REPLACED by the length of the
// data actually written -- a stale `/Length` copied from a parsed dictionary is how a writer
// produces exactly the damage `corpus/malformed/length-lies-short.pdf` is made of.
void appendIndirect(std::string& out, long long number, long long generation,
                    const CosObject& object, const std::optional<std::string>& streamData)
{
    appendInteger(out, number);
    out += ' ';
    appendInteger(out, generation);
    out += " obj\n";
    if (object.isStream()) {
        std::vector<CosObject::Entry> entries;
        entries.reserve(object.entries().size() + 1);
        for (const CosObject::Entry& entry : object.entries()) {
            if (entry.key != "Length")
                entries.push_back(entry);
        }
        entries.push_back({"Length", CosObject::makeInteger(
                                         static_cast<long long>(streamData->size()))});
        appendObject(out, CosObject::makeDictionary(std::move(entries)));
        out += "\nstream\n";
        out += *streamData;
        out += "\nendstream";
    } else {
        appendObject(out, object);
    }
    out += "\nendobj\n";
}

// --------------------------------------------------------------------------------------------
// Sections
// --------------------------------------------------------------------------------------------

struct Row {
    XrefEntryType type = XrefEntryType::InFile;
    long long field2 = 0;
    long long field3 = 0;
};

// Contiguous runs of object numbers, each written as one subsection. Small sections are the point
// of an incremental update, and a reader that mishandles more than one subsection is a real bug
// that `corpus/ordinary/incremental-3gen.pdf` already exercises on the reading side.
std::vector<std::vector<std::pair<long long, Row>>> runsOf(const std::map<long long, Row>& rows)
{
    std::vector<std::vector<std::pair<long long, Row>>> runs;
    for (const auto& [number, row] : rows) {
        if (runs.empty() || runs.back().back().first + 1 != number)
            runs.emplace_back();
        runs.back().emplace_back(number, row);
    }
    return runs;
}

std::string classicSection(const std::map<long long, Row>& rows)
{
    std::string out = "xref\n";
    for (const auto& run : runsOf(rows)) {
        appendInteger(out, run.front().first);
        out += ' ';
        appendInteger(out, static_cast<long long>(run.size()));
        out += '\n';
        for (const auto& [number, row] : run) {
            (void)number;
            // Exactly twenty bytes, CR LF included (ISO 32000-1 7.5.4). The reader lexes these as
            // tokens and would forgive a short one; another reader might not. Built by hand rather
            // than with `snprintf("%010lld")`, whose `ll` MinGW's msvcrt format checking disputes.
            const bool free = row.type == XrefEntryType::Free;
            appendPadded(out, free ? 0 : row.field2, 10);
            out += ' ';
            appendPadded(out, row.field3, 5);
            out += free ? " f\r\n" : " n\r\n";
        }
    }
    return out;
}

int bytesFor(long long value)
{
    int bytes = 1;
    while (bytes < 8 && (static_cast<unsigned long long>(value) >> (8 * bytes)) != 0)
        ++bytes;
    return bytes;
}

// The raw rows of a cross-reference stream, big-endian, with the widths chosen to fit.
std::string streamRows(const std::map<long long, Row>& rows, int width2, int width3)
{
    std::string out;
    auto put = [&out](long long value, int width) {
        for (int byte = width - 1; byte >= 0; --byte)
            out += static_cast<char>((static_cast<unsigned long long>(value) >> (8 * byte)) & 0xFF);
    };
    for (const auto& [number, row] : rows) {
        (void)number;
        const int type = row.type == XrefEntryType::Free ? 0
                         : row.type == XrefEntryType::InFile ? 1 : 2;
        put(type, 1);
        put(row.field2, width2);
        put(row.field3, width3);
    }
    return out;
}

QByteArray hashOf(std::string_view first, const std::string& body)
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(QByteArrayView(first.data(), static_cast<qsizetype>(first.size())));
    hash.addData(QByteArrayView(body.data(), static_cast<qsizetype>(body.size())));
    return hash.result();
}

enum class Mode { Chained, Complete };

core::Result<WrittenUpdate> write(std::string_view original, const XrefTable& table,
                                  const ObjectDelta& delta, Mode mode)
{
    if (table.source().data() != original.data() || table.source().size() != original.size())
        return writeError(core::ErrorCode::InvalidArgument, "error.pdf.writeWrongBuffer");
    if (table.isEncrypted())
        return writeError(core::ErrorCode::Unsupported, "error.pdf.encrypted");

    const CosObject& root = table.trailer().get("Root");
    if (!root.isReference())
        return writeError(core::ErrorCode::ParseFailure, "error.pdf.writeNoRoot");

    std::size_t previousSection = 0;
    SectionKind kind = SectionKind::Classic;
    const CosObject* hybridStream = nullptr;
    if (mode == Mode::Chained) {
        if (table.sections().empty())
            return writeError(core::ErrorCode::Unavailable, "error.pdf.writeNoRevision");
        if (delta.isEmpty())
            return writeError(core::ErrorCode::Unavailable, "error.pdf.writeNothing");
        // `sections()` is newest first, and a hybrid file's classic table precedes the stream its
        // `/XRefStm` names -- so "the newest section's kind" is classic there, as D3-5 requires.
        previousSection = table.sections().front().offset;
        kind = table.sections().front().isStream ? SectionKind::Stream : SectionKind::Classic;
        // A HYBRID file's newest trailer names its cross-reference stream, and our update's trailer
        // names it again (Stage 3 guide I-14). Acrobat reads `/XRefStm` from the NEWEST trailer only:
        // without it the packed objects -- a page tree, often -- are gone and the file is "damaged",
        // while PDFium and `XrefTable` follow `/Prev` to the older trailer and never notice. The
        // offset is into the original, which the update never moves.
        if (kind == SectionKind::Classic) {
            const CosObject* named = table.sections().front().trailer.find("XRefStm");
            if (named != nullptr && named->isInteger())
                hybridStream = named;
        }
    }

    // --- Validate the whole delta before producing a byte. A refusal half-way through would have
    // nothing to undo, but a caller must never see a partial update even in memory. -------------
    const ObjectAllocator& allocator = delta.allocator();
    std::set<long long> seen;
    for (const ObjectChange& change : delta.changes()) {
        if (!seen.insert(change.number).second)
            return writeError(core::ErrorCode::InvalidArgument, "error.pdf.writeDuplicateObject",
                              change.number);
        const bool added = change.number >= allocator.firstNumber()
                           && change.number < allocator.nextNumber();
        const XrefEntry* entry = table.find(change.number);
        const bool present = entry != nullptr && entry->type != XrefEntryType::Free;
        if (change.number <= 0 || (!added && !present))
            return writeError(core::ErrorCode::NotFound, "error.pdf.writeObjectMissing",
                              change.number);
        if (change.object.isStream() && !change.streamData)
            return writeError(core::ErrorCode::InvalidArgument,
                              "error.pdf.writeStreamDataMissing", change.number);
    }

    // --- The objects. ---------------------------------------------------------------------------
    WrittenUpdate update;
    std::string& out = update.bytes;
    const bool endsWithEol =
        !original.empty() && (original.back() == '\n' || original.back() == '\r');
    if (!endsWithEol)
        out += '\n';   // `%%EOF` then our first object on the same line would be a comment

    std::vector<const ObjectChange*> ordered;
    for (const ObjectChange& change : delta.changes())
        ordered.push_back(&change);
    std::sort(ordered.begin(), ordered.end(),
              [](const ObjectChange* a, const ObjectChange* b) { return a->number < b->number; });

    std::map<long long, Row> rows;
    for (const ObjectChange* change : ordered) {
        // A replaced object keeps its generation; one that lived in an object stream had
        // generation 0 by definition (ISO 32000-1 7.5.7), and so does a new one.
        const XrefEntry* entry = table.find(change->number);
        const long long generation =
            entry != nullptr && entry->type == XrefEntryType::InFile ? entry->field3 : 0;
        rows[change->number] = Row{XrefEntryType::InFile,
                                   static_cast<long long>(original.size() + out.size()),
                                   generation};
        appendIndirect(out, change->number, generation, change->object, change->streamData);
        update.objectsWritten.push_back(change->number);
    }

    // --- Which rows the section lists. ---------------------------------------------------------
    long long size = std::max({table.declaredSize(), table.largestObjectNumber() + 1,
                               allocator.nextNumber()});
    if (mode == Mode::Complete) {
        // Every object the table places, at the offset it verified -- except those this update
        // replaces, whose new rows are already in `rows` and win.
        for (const auto& [number, entry] : table.entries()) {
            if (entry.type == XrefEntryType::Free || rows.count(number) != 0)
                continue;
            rows[number] = Row{entry.type, entry.field2, entry.field3};
            if (entry.type == XrefEntryType::InObjectStream)
                kind = SectionKind::Stream;   // a classic table cannot say "inside object stream N"
        }
        rows[0] = Row{XrefEntryType::Free, 0, 65535};
    }

    // The ID: first element kept, second derived. Hashed over everything the update writes that
    // does not itself contain the ID, so it is reproducible and differs whenever the update does.
    std::string firstId;
    const CosObject& id = table.trailer().get("ID");
    if (id.isArray() && !id.items().empty() && id.items()[0].isString())
        firstId = id.items()[0].text();

    CosObject rootValue = root;
    const CosObject* info = table.trailer().find("Info");

    auto trailerEntries = [&](const QByteArray& digest) {
        std::vector<CosObject::Entry> entries;
        entries.push_back({"Size", CosObject::makeInteger(size)});
        entries.push_back({"Root", rootValue});
        if (info != nullptr && info->isReference())
            entries.push_back({"Info", *info});
        const std::string second(digest.constData(), static_cast<std::size_t>(digest.size()));
        entries.push_back({"ID", CosObject::makeArray(
                                     {CosObject::makeString(firstId.empty() ? second : firstId,
                                                            StringForm::Hex),
                                      CosObject::makeString(second, StringForm::Hex)})});
        if (mode == Mode::Chained)
            entries.push_back({"Prev", CosObject::makeInteger(
                                           static_cast<long long>(previousSection))});
        if (hybridStream != nullptr)
            entries.push_back({"XRefStm", *hybridStream});
        return entries;
    };

    if (kind == SectionKind::Classic) {
        update.sectionOffset = original.size() + out.size();
        out += classicSection(rows);
        const QByteArray digest = hashOf(firstId, out);
        out += "trailer\n";
        appendObject(out, CosObject::makeDictionary(trailerEntries(digest)));
        out += '\n';
    } else {
        // The stream lists itself, so its number and offset are rows like any other.
        ObjectAllocator streamAllocator = allocator;
        update.xrefStreamNumber = streamAllocator.allocate();
        size = std::max(size, streamAllocator.nextNumber());
        update.sectionOffset = original.size() + out.size();
        rows[update.xrefStreamNumber] =
            Row{XrefEntryType::InFile, static_cast<long long>(update.sectionOffset), 0};

        long long widest2 = 0;
        long long widest3 = 0;
        for (const auto& [number, row] : rows) {
            (void)number;
            widest2 = std::max(widest2, row.field2);
            widest3 = std::max(widest3, row.field3);
        }
        const int width2 = bytesFor(widest2);
        const int width3 = std::max(2, bytesFor(widest3));
        const std::string raw = streamRows(rows, width2, width3);

        auto encoded = encodeFilter(FilterKind::Flate, raw, DecodeParms{});
        if (!encoded)
            return writeError(core::ErrorCode::Unknown, "error.pdf.writeEncodeFailed");
        const std::string data(encoded.value().begin(), encoded.value().end());

        std::vector<CosObject> index;
        for (const auto& run : runsOf(rows)) {
            index.push_back(CosObject::makeInteger(run.front().first));
            index.push_back(CosObject::makeInteger(static_cast<long long>(run.size())));
        }

        const QByteArray digest = hashOf(firstId, out + raw);
        std::vector<CosObject::Entry> entries;
        entries.push_back({"Type", CosObject::makeName("XRef")});
        for (CosObject::Entry& entry : trailerEntries(digest))
            entries.push_back(std::move(entry));
        entries.push_back({"W", CosObject::makeArray({CosObject::makeInteger(1),
                                                      CosObject::makeInteger(width2),
                                                      CosObject::makeInteger(width3)})});
        entries.push_back({"Index", CosObject::makeArray(std::move(index))});
        entries.push_back({"Filter", CosObject::makeName("FlateDecode")});
        appendIndirect(out, update.xrefStreamNumber, 0,
                       CosObject::makeStream(std::move(entries), CosStreamData{}), data);
    }

    out += "startxref\n";
    appendInteger(out, static_cast<long long>(update.sectionOffset));
    out += "\n%%EOF\n";

    update.kind = kind;
    update.size = size;
    return update;
}

} // namespace

std::string serializeObject(const CosObject& object)
{
    std::string out;
    appendObject(out, object);
    return out;
}

ObjectDelta::ObjectDelta(const XrefTable& table) noexcept
    : allocator_(ObjectAllocator::forTable(table))
{
}

void ObjectDelta::replace(long long number, CosObject object, std::optional<std::string> streamData)
{
    changes_.push_back(ObjectChange{number, std::move(object), std::move(streamData)});
}

long long ObjectDelta::add(CosObject object, std::optional<std::string> streamData)
{
    const long long number = allocator_.allocate();
    changes_.push_back(ObjectChange{number, std::move(object), std::move(streamData)});
    return number;
}

bool ObjectDelta::touches(long long number) const noexcept
{
    return std::any_of(changes_.begin(), changes_.end(),
                       [number](const ObjectChange& change) { return change.number == number; });
}

void IncrementalWriter::protectOriginal(XrefTable& table, ObjectDelta& delta)
{
    const std::string_view source = table.source();
    const std::size_t size = source.size();

    // A snapshot of the numbers: `fetch` fills caches, and a loop over the live map while it does is
    // one refactor away from iterator invalidation.
    std::vector<std::pair<long long, XrefEntryType>> placed;
    placed.reserve(table.entryCount());
    for (const auto& [number, entry] : table.entries()) {
        if (entry.type != XrefEntryType::Free && number > 0)
            placed.emplace_back(number, entry.type);
    }

    long long largestReferenced = -1;
    std::vector<const CosObject*> stack;
    for (const auto& [number, type] : placed) {
        auto fetched = table.fetch(number);
        if (!fetched || fetched.value().number != number)
            continue;
        const IndirectObject& indirect = fetched.value();

        // Every reference, iteratively -- the tree is input, and `pdf/cos/Parser` bounds its depth,
        // but a recursive walk here would still be the one place that trusted it.
        stack.assign(1, &indirect.object);
        while (!stack.empty()) {
            const CosObject* value = stack.back();
            stack.pop_back();
            if (value->isReference())
                largestReferenced = std::max(largestReferenced, value->reference().number);
            for (const CosObject& item : value->items())
                stack.push_back(&item);
            for (const CosObject::Entry& entry : value->entries())
                stack.push_back(&entry.value);
        }

        // Open-ended: only a loose object can be -- a packed one's span is into a decoded payload, and
        // its container is itself a loose object that this loop meets.
        if (type != XrefEntryType::InFile || delta.touches(number))
            continue;
        const CosSpan& span = indirect.span;
        const bool objectReachesEnd = span.valid && span.offset <= size && span.length >= size - span.offset;
        bool dataReachesEnd = false;
        bool dataInSource = false;
        if (indirect.object.isStream()) {
            const CosStreamData& data = indirect.object.streamData();
            dataInSource = data.dataOffset <= size && data.dataLength <= size - data.dataOffset;
            dataReachesEnd = dataInSource && data.dataLength == size - data.dataOffset;
        }
        if (!objectReachesEnd && !dataReachesEnd)
            continue;

        if (indirect.object.isStream() && dataInSource) {
            const CosStreamData& data = indirect.object.streamData();
            delta.replace(number, indirect.object, std::string(source.substr(data.dataOffset, data.dataLength)));
        } else if (indirect.object.isStream()) {
            // A stream whose data is not in this buffer has nothing a copy could carry; its dictionary
            // is what every reader of it has ever had.
            std::vector<CosObject::Entry> entries(indirect.object.entries().begin(),
                                                  indirect.object.entries().end());
            delta.replace(number, CosObject::makeDictionary(std::move(entries)));
        } else {
            delta.replace(number, indirect.object);
        }
    }
    delta.reserveNumbersThrough(largestReferenced);
}

core::Result<WrittenUpdate> IncrementalWriter::append(std::string_view original,
                                                      const XrefTable& table,
                                                      const ObjectDelta& delta)
{
    return write(original, table, delta, Mode::Chained);
}

core::Result<WrittenUpdate> IncrementalWriter::appendComplete(std::string_view original,
                                                              const XrefTable& table,
                                                              const ObjectDelta& delta)
{
    return write(original, table, delta, Mode::Complete);
}

} // namespace rawpage::pdf::cos
