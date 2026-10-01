#include "pdf/ops/EmbeddedFileReader.h"

#include "pdf/cos/CosObject.h"

#include <optional>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;

// The header's bounds. Depth is far beyond any real name tree (a balanced tree of a million entries is
// a few levels deep); the node and entry caps are far above any real document and far below a number
// that takes noticeable time.
constexpr int kMaxDepth = 32;
constexpr int kMaxNodes = 20000;
constexpr std::size_t kMaxEntries = 100000;

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

void walk(cos::XrefTable& table, const CosObject& node, int depth, int& nodes, std::vector<EmbeddedFileEntry>& out)
{
    if (depth >= kMaxDepth || nodes >= kMaxNodes || out.size() >= kMaxEntries) return;
    ++nodes;

    // A LEAF: `/Names [key1 value1 key2 value2 ...]`, already in key order.
    std::optional<cos::IndirectObject> namesHolder;
    if (const CosObject* names = resolve(table, node.find("Names"), namesHolder); names != nullptr && names->isArray()) {
        const std::vector<CosObject>& items = names->items();
        for (std::size_t i = 0; i + 1 < items.size() && out.size() < kMaxEntries; i += 2) {
            std::optional<cos::IndirectObject> specHolder;
            const CosObject* spec = resolve(table, &items[i + 1], specHolder);
            if (spec == nullptr || !spec->isDictionary()) continue;
            EmbeddedFileEntry entry;
            // /UF, then /F: the engine's preference (the header) -- the name is read only to match.
            entry.name = textOf(table, spec->find("UF"));
            if (entry.name.isEmpty()) entry.name = textOf(table, spec->find("F"));
            entry.description = textOf(table, spec->find("Desc"));
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

} // namespace

std::vector<EmbeddedFileEntry> EmbeddedFileReader::read(cos::XrefTable& table)
{
    std::vector<EmbeddedFileEntry> out;
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
