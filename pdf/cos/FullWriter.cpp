// pdf/cos/FullWriter -- implementation. Stage 7 T6. The header says what is written, what is dropped and why a
// dropped number keeps its generation.

#include "pdf/cos/FullWriter.h"

#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefTable.h"

#include <QByteArrayView>
#include <QCryptographicHash>

#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace rawpage::pdf::cos {
namespace {

core::Error writeError(core::ErrorCode code, const char* key, long long detail = -1)
{
    return core::Error{code, QLatin1String(key),
                       detail >= 0 ? QString::number(static_cast<qlonglong>(detail)) : QString()};
}

bool inUse(const XrefEntry* entry) noexcept
{
    return entry != nullptr && entry->type != XrefEntryType::Free;
}

// Every reference inside `value`, the stream's `/Length` excepted (the header says why). The parser bounds a
// value's depth (`Parser::kMaxDepth`), so the recursion is bounded too.
void referencesIn(const CosObject& value, std::vector<long long>& out)
{
    switch (value.type()) {
    case CosType::Reference:
        out.push_back(value.reference().number);
        return;
    case CosType::Array:
        for (const CosObject& item : value.items()) referencesIn(item, out);
        return;
    case CosType::Dictionary:
    case CosType::Stream:
        for (const CosObject::Entry& entry : value.entries()) {
            if (value.isStream() && entry.key == "Length") continue;
            referencesIn(entry.value, out);
        }
        return;
    default:
        return;
    }
}

// The walk, and the fetched objects it met, so the rewrite does not fetch each object twice.
std::map<long long, IndirectObject> reachableObjects(XrefTable& table)
{
    std::map<long long, IndirectObject> reached;
    std::vector<long long> pending;
    for (const char* key : {"Root", "Info"}) {
        const CosObject* named = table.trailer().find(key);
        if (named != nullptr && named->isReference()) pending.push_back(named->reference().number);
    }
    std::vector<long long> found;
    while (!pending.empty()) {
        const long long number = pending.back();
        pending.pop_back();
        if (number <= 0 || reached.count(number) != 0 || !inUse(table.find(number))) continue;
        auto fetched = table.fetch(number);
        if (!fetched) continue;   // nothing a reader resolves either: not reached
        found.clear();
        referencesIn(fetched.value().object, found);
        reached.emplace(number, std::move(fetched).value());
        for (const long long next : found) {
            if (reached.count(next) == 0) pending.push_back(next);
        }
    }
    return reached;
}

// `%PDF-1.x` from the file's first kilobyte, which is where ISO 32000-1 7.5.2 lets a reader look for it.
std::string headerOf(std::string_view source)
{
    const std::size_t at = source.substr(0, 1024).find("%PDF-");
    if (at == std::string_view::npos) return "%PDF-1.7";
    std::size_t end = at + 5;
    while (end < source.size() && end < at + 16
           && ((source[end] >= '0' && source[end] <= '9') || source[end] == '.'))
        ++end;
    return end > at + 5 ? std::string(source.substr(at, end - at)) : std::string("%PDF-1.7");
}

void appendPadded(std::string& out, long long value, std::size_t width)
{
    std::string digits = std::to_string(value);
    if (digits.size() < width) out.append(width - digits.size(), '0');
    out += digits;
}

struct Row {
    bool free = true;
    long long offset = 0;       // in use: where its header begins
    long long generation = 0;
    long long nextFree = 0;     // free: the next free number
};

} // namespace

std::vector<long long> FullWriter::reachable(XrefTable& table)
{
    std::vector<long long> out;
    for (const auto& [number, object] : reachableObjects(table)) out.push_back(number);
    return out;
}

core::Result<FullRewrite> FullWriter::rewrite(std::string_view source, XrefTable& table)
{
    if (table.source().data() != source.data() || table.source().size() != source.size())
        return writeError(core::ErrorCode::InvalidArgument, "error.pdf.writeWrongBuffer");
    if (table.isEncrypted())
        return writeError(core::ErrorCode::Unsupported, "error.pdf.encrypted");
    const CosObject& root = table.trailer().get("Root");
    if (!root.isReference())
        return writeError(core::ErrorCode::ParseFailure, "error.pdf.writeNoRoot");

    std::map<long long, IndirectObject> reached = reachableObjects(table);
    if (reached.count(root.reference().number) == 0)
        return writeError(core::ErrorCode::NotFound, "error.pdf.objectMissing", root.reference().number);

    FullRewrite out;
    std::string& bytes = out.bytes;
    bytes = headerOf(source);
    bytes += "\n%\xE2\xE3\xCF\xD3\n";

    // The objects, in number order, each at the number and generation it has.
    std::map<long long, Row> rows;
    for (auto& [number, fetched] : reached) {
        const XrefEntry* entry = table.find(number);
        const long long generation = entry->type == XrefEntryType::InFile ? entry->field3 : 0;
        CosObject value = std::move(fetched.object);
        value.clearSpansRecursively();
        std::optional<std::string> data;
        if (value.isStream()) data = std::string(value.rawStreamData(source));
        rows[number] = Row{false, static_cast<long long>(bytes.size()), generation, 0};
        bytes += serializeIndirect(number, generation, value, data);
        out.kept.push_back(number);
    }

    // `/Size`: one past the largest number in use, dropped ones included (the header: a number an undo may revive
    // is never one a later edit allocates). The rows: 0, every number in use, every number the table listed free.
    long long largest = 0;
    for (const auto& [number, entry] : table.entries()) {
        if (number <= 0 || number > kMaxObjectNumber) continue;
        if (entry.type != XrefEntryType::Free) {
            largest = std::max(largest, number);
            if (rows.count(number) == 0) {
                rows[number] = Row{true, 0, entry.type == XrefEntryType::InFile ? entry.field3 : 0, 0};
                out.dropped.push_back(number);
            }
        }
    }
    out.size = largest + 1;
    for (const auto& [number, entry] : table.entries()) {
        if (number <= 0 || number >= out.size || entry.type != XrefEntryType::Free || rows.count(number) != 0) continue;
        rows[number] = Row{true, 0, std::min<long long>(entry.field3, 65535), 0};
    }
    rows[0] = Row{true, 0, 65535, 0};
    // The free list, ascending, ending back at 0 (ISO 32000-1 7.5.4).
    {
        long long previous = 0;
        for (auto& [number, row] : rows) {
            if (!row.free || number == 0) continue;
            rows[previous].nextFree = number;
            previous = number;
        }
        rows[previous].nextFree = 0;
    }

    const std::size_t sectionOffset = bytes.size();
    std::string section = "xref\n";
    for (auto it = rows.begin(); it != rows.end();) {
        auto end = it;
        long long count = 0;
        while (end != rows.end() && end->first == it->first + count) {
            ++end;
            ++count;
        }
        section += std::to_string(it->first) + ' ' + std::to_string(count) + '\n';
        for (; it != end; ++it) {
            const Row& row = it->second;
            appendPadded(section, row.free ? row.nextFree : row.offset, 10);
            section += ' ';
            appendPadded(section, row.generation, 5);
            section += row.free ? " f\r\n" : " n\r\n";
        }
    }
    bytes += section;

    // The `/ID`: its first element kept, its second a hash of everything written before the trailer.
    std::string firstId;
    const CosObject& id = table.trailer().get("ID");
    if (id.isArray() && !id.items().empty() && id.items()[0].isString()) firstId = id.items()[0].text();
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(QByteArrayView(firstId.data(), static_cast<qsizetype>(firstId.size())));
    hash.addData(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())));
    const QByteArray digest = hash.result();
    const std::string second(digest.constData(), static_cast<std::size_t>(digest.size()));

    std::vector<CosObject::Entry> trailer;
    trailer.push_back({"Size", CosObject::makeInteger(out.size)});
    trailer.push_back({"Root", root});
    const CosObject* info = table.trailer().find("Info");
    if (info != nullptr && info->isReference() && rows.count(info->reference().number) != 0
        && !rows[info->reference().number].free)
        trailer.push_back({"Info", *info});
    trailer.push_back({"ID", CosObject::makeArray({CosObject::makeString(firstId.empty() ? second : firstId, StringForm::Hex),
                                                   CosObject::makeString(second, StringForm::Hex)})});
    bytes += "trailer\n";
    bytes += serializeObject(CosObject::makeDictionary(std::move(trailer)));
    bytes += "\nstartxref\n" + std::to_string(sectionOffset) + "\n%%EOF\n";
    return out;
}

} // namespace rawpage::pdf::cos
