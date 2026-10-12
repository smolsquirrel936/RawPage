#include "pdf/ops/EmbeddedFileReader.h"

#include "pdf/cos/CosObject.h"
#include "pdf/cos/filters/Filter.h"

#include <optional>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;

// The header's bounds. Depth is far beyond any real name tree (a balanced tree of a million entries is
// a few levels deep); the node and entry caps are far above any real document and far below a number
// that takes noticeable time.
constexpr int kMaxDepth = 32;
constexpr int kMaxNodes = 20000;
constexpr std::size_t kMaxEntries = EmbeddedFileReader::kMaxEntries;

// PDFDocEncoding's code points where it differs from ISO-8859-1 (ISO 32000-1 Annex D, Table D.2). 0 is an
// undefined code, which is decoded as U+FFFD rather than guessed.
char16_t pdfDocEncoding(unsigned char byte)
{
    static constexpr char16_t kLow[8] = {0x02D8, 0x02C7, 0x02C6, 0x02D9, 0x02DD, 0x02DB, 0x02DA, 0x02DC};
    static constexpr char16_t kHigh[33] = {
        0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x0192, 0x2044, 0x2039, 0x203A, 0x2212,
        0x2030, 0x201E, 0x201C, 0x201D, 0x2018, 0x2019, 0x201A, 0x2122, 0xFB01, 0xFB02, 0x0141,
        0x0152, 0x0160, 0x0178, 0x017D, 0x0131, 0x0142, 0x0153, 0x0161, 0x017E, 0x0000, 0x20AC,
    };
    if (byte >= 0x18 && byte <= 0x1F) return kLow[byte - 0x18];
    if (byte >= 0x80 && byte <= 0xA0) {
        const char16_t mapped = kHigh[byte - 0x80];
        return mapped == 0 ? char16_t{0xFFFD} : mapped;
    }
    if (byte == 0xAD) return 0xFFFD;
    return byte;
}

// An object, with a reference followed once. `holder` keeps a fetched object alive for the caller.
const CosObject* resolve(cos::XrefTable& table, const CosObject* object, std::optional<cos::IndirectObject>& holder)
{
    if (object == nullptr || !object->isReference()) return object;
    auto fetched = table.fetch(object->reference().number);
    if (!fetched) return nullptr;
    holder = std::move(fetched).value();
    return &holder->object;
}

QString textOf(cos::XrefTable& table, const CosObject* object)
{
    std::optional<cos::IndirectObject> holder;
    const CosObject* value = resolve(table, object, holder);
    if (value == nullptr || !value->isString()) return {};
    return EmbeddedFileReader::decodeTextString(value->text());
}

void walk(cos::XrefTable& table, const CosObject& node, int depth, int& nodes, std::vector<NameTreeEntry>& out)
{
    if (depth >= kMaxDepth || nodes >= kMaxNodes || out.size() >= kMaxEntries) return;
    ++nodes;

    // A LEAF: `/Names [key1 value1 key2 value2 ...]`, already in key order.
    std::optional<cos::IndirectObject> namesHolder;
    if (const CosObject* names = resolve(table, node.find("Names"), namesHolder); names != nullptr && names->isArray()) {
        const std::vector<CosObject>& items = names->items();
        for (std::size_t i = 0; i + 1 < items.size() && out.size() < kMaxEntries; i += 2) {
            // Every pair, whatever its key: the engine enumerates by position, and `read` is joined to it by
            // position (`PdfiumDocument::searchUnits`) -- a pair skipped here would shift every file after it.
            NameTreeEntry entry{items[i], items[i + 1]};
            entry.key.clearSpansRecursively();
            entry.value.clearSpansRecursively();
            out.push_back(std::move(entry));
        }
    }

    // AN INTERMEDIATE NODE: `/Kids [refs]`, each a subtree in key order.
    std::optional<cos::IndirectObject> kidsHolder;
    if (const CosObject* kids = resolve(table, node.find("Kids"), kidsHolder); kids != nullptr && kids->isArray()) {
        for (const CosObject& kid : kids->items()) {
            std::optional<cos::IndirectObject> kidHolder;
            const CosObject* child = resolve(table, &kid, kidHolder);
            if (child == nullptr || !child->isDictionary()) continue;
            walk(table, *child, depth + 1, nodes, out);
        }
    }
}

// The `/EF` stream of a specification -- `/F`, else `/UF` (ISO 32000-1 Table 44 lists both; a producer writes one
// or both, the same stream) -- fetched into `holder`; null when there is none.
const CosObject* embeddedStream(cos::XrefTable& table, const CosObject& specification,
                                std::optional<cos::IndirectObject>& efHolder, std::optional<cos::IndirectObject>& holder)
{
    const CosObject* ef = resolve(table, specification.find("EF"), efHolder);
    if (ef == nullptr || !ef->isDictionary()) return nullptr;
    for (const char* key : {"F", "UF"}) {
        const CosObject* value = ef->find(key);
        if (value == nullptr || !value->isReference()) continue;   // a stream is always indirect
        const CosObject* stream = resolve(table, value, holder);
        if (stream != nullptr && stream->isStream()) return stream;
    }
    return nullptr;
}

core::PdfDate dateOf(cos::XrefTable& table, const CosObject* object)
{
    const QString raw = textOf(table, object);
    return raw.isEmpty() ? core::PdfDate{} : core::PdfDate::parse(raw);
}

} // namespace

std::vector<NameTreeEntry> EmbeddedFileReader::entries(cos::XrefTable& table)
{
    std::vector<NameTreeEntry> out;
    std::optional<cos::IndirectObject> catalogHolder;
    const CosObject* catalog = resolve(table, &table.trailer().get("Root"), catalogHolder);
    if (catalog == nullptr || !catalog->isDictionary()) return out;

    std::optional<cos::IndirectObject> namesHolder;
    const CosObject* names = resolve(table, catalog->find("Names"), namesHolder);
    if (names == nullptr || !names->isDictionary()) return out;

    std::optional<cos::IndirectObject> rootHolder;
    const CosObject* root = resolve(table, names->find("EmbeddedFiles"), rootHolder);
    if (root == nullptr || !root->isDictionary()) return out;

    int nodes = 0;
    walk(table, *root, 0, nodes, out);
    return out;
}

std::vector<EmbeddedFileEntry> EmbeddedFileReader::read(cos::XrefTable& table)
{
    std::vector<EmbeddedFileEntry> out;
    for (const NameTreeEntry& entry : entries(table)) {
        std::optional<cos::IndirectObject> specHolder;
        const CosObject* spec = resolve(table, &entry.value, specHolder);
        if (spec == nullptr || !spec->isDictionary()) continue;
        EmbeddedFileEntry file;
        // /UF, then /F: the engine's preference (the header) -- the name is read only to match.
        file.name = textOf(table, spec->find("UF"));
        if (file.name.isEmpty()) file.name = textOf(table, spec->find("F"));
        file.description = textOf(table, spec->find("Desc"));
        out.push_back(std::move(file));
    }
    return out;
}

std::vector<core::DocumentAttachment> EmbeddedFileReader::attachments(cos::XrefTable& table)
{
    std::vector<core::DocumentAttachment> out;
    for (const NameTreeEntry& entry : entries(table)) {
        std::optional<core::EmbeddedFile> file = fileOf(table, &entry.value);
        if (!file) continue;
        if (!entry.key.isString()) continue;   // a key nobody can name, so not a file the model can hold
        core::DocumentAttachment attachment;
        attachment.key = decodeTextString(entry.key.text());
        if (attachment.key.isEmpty()) continue;
        attachment.file = std::move(*file);
        if (attachment.file.name.isEmpty()) attachment.file.name = attachment.key;   // the key names it, then
        out.push_back(std::move(attachment));
    }
    return out;
}

std::optional<core::EmbeddedFile> EmbeddedFileReader::fileOf(cos::XrefTable& table, const cos::CosObject* specification)
{
    std::optional<cos::IndirectObject> specHolder;
    const CosObject* spec = resolve(table, specification, specHolder);
    if (spec == nullptr) return std::nullopt;
    core::EmbeddedFile file;
    if (spec->isString()) {
        file.name = decodeTextString(spec->text());
        return file;
    }
    if (!spec->isDictionary()) return std::nullopt;

    file.name = textOf(table, spec->find("UF"));
    if (file.name.isEmpty()) file.name = textOf(table, spec->find("F"));
    file.description = textOf(table, spec->find("Desc"));

    std::optional<cos::IndirectObject> efHolder, streamHolder;
    const CosObject* stream = embeddedStream(table, *spec, efHolder, streamHolder);
    if (stream == nullptr) return file;
    if (const CosObject* subtype = stream->find("Subtype"); subtype != nullptr && subtype->isName())
        file.mime = QString::fromStdString(subtype->text());   // `text/plain`: a name's `#2F` already decoded

    std::optional<cos::IndirectObject> paramsHolder;
    const CosObject* params = resolve(table, stream->find("Params"), paramsHolder);
    if (params != nullptr && params->isDictionary()) {
        std::optional<cos::IndirectObject> sizeHolder;
        const CosObject* size = resolve(table, params->find("Size"), sizeHolder);
        if (size != nullptr && size->isNumber() && size->numberValue() >= 0.0 && size->numberValue() <= 9.0e15)
            file.size = static_cast<long long>(size->numberValue());
        file.created = dateOf(table, params->find("CreationDate"));
        file.modified = dateOf(table, params->find("ModDate"));
        std::optional<cos::IndirectObject> sumHolder;
        const CosObject* sum = resolve(table, params->find("CheckSum"), sumHolder);
        if (sum != nullptr && sum->isString() && sum->text().size() == 16)
            file.checksum = QByteArray(sum->text().data(), 16);
    }
    return file;
}

core::Result<QByteArray> EmbeddedFileReader::dataOf(cos::XrefTable& table, const cos::CosObject* specification)
{
    std::optional<cos::IndirectObject> specHolder;
    const CosObject* spec = resolve(table, specification, specHolder);
    std::optional<cos::IndirectObject> efHolder, streamHolder;
    const CosObject* stream = (spec != nullptr && spec->isDictionary()) ? embeddedStream(table, *spec, efHolder, streamHolder) : nullptr;
    if (stream == nullptr)
        return core::Error{core::ErrorCode::NotFound, QLatin1String("error.attachment.notEmbedded"), QString()};

    // The stream's bytes lie in the buffer its table was loaded over: a stream is never packed in an object stream.
    auto decoded = cos::decodeStream(*stream, table.source());
    if (!decoded)
        return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.attachment.undecodable"),
                           QString(decoded.error().messageKey)};
    QByteArray bytes(reinterpret_cast<const char*>(decoded.value().data()), static_cast<qsizetype>(decoded.value().size()));
    if (const std::optional<core::EmbeddedFile> file = fileOf(table, specification);
        file && file->size >= 0 && file->size != static_cast<long long>(bytes.size()))
        return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.attachment.undecodable"),
                           QStringLiteral("size %1 != %2").arg(file->size).arg(bytes.size())};
    return bytes;
}

QString EmbeddedFileReader::decodeTextString(const std::string& bytes)
{
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    if (bytes.size() >= 2 && byte(0) == 0xFE && byte(1) == 0xFF) {
        QString out;
        out.reserve(static_cast<qsizetype>((bytes.size() - 2) / 2));
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2)
            out.append(QChar(static_cast<char16_t>((byte(i) << 8) | byte(i + 1))));
        return out;
    }
    if (bytes.size() >= 3 && byte(0) == 0xEF && byte(1) == 0xBB && byte(2) == 0xBF)
        return QString::fromUtf8(bytes.data() + 3, static_cast<qsizetype>(bytes.size() - 3));

    QString out;
    out.reserve(static_cast<qsizetype>(bytes.size()));
    for (std::size_t i = 0; i < bytes.size(); ++i) out.append(QChar(pdfDocEncoding(byte(i))));
    return out;
}

} // namespace rawpage::pdf::ops
