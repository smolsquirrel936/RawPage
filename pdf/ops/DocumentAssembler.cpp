// pdf/ops/DocumentAssembler -- implementation. Stage 7 T7. The header says what is built, what writes it and what the
// verify proves.

#include "pdf/ops/DocumentAssembler.h"

#include "pdf/cos/FullWriter.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefTable.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <utility>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;

core::Error assemblyError(const QString& what)
{
    return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.assembly.verifyFailed"), what};
}

core::Error writeFailed(const QString& path)
{
    return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.assembly.writeFailed"), path};
}

std::mutex& faultMutex()
{
    static std::mutex mutex;
    return mutex;
}

DocumentAssembler::WriteFault& fault()
{
    static DocumentAssembler::WriteFault installed;
    return installed;
}

void applyFault(std::string& bytes)
{
    const std::lock_guard<std::mutex> guard(faultMutex());
    if (fault()) fault()(bytes);
}

// `dictionary` with `key` set to `value` -- replaced in place where it is, appended where it is not.
CosObject withEntry(const CosObject& dictionary, const std::string& key, CosObject value)
{
    std::vector<CosObject::Entry> entries;
    bool set = false;
    for (const CosObject::Entry& entry : dictionary.entries()) {
        if (entry.key == key) {
            if (!set) entries.push_back({key, value});
            set = true;
            continue;
        }
        entries.push_back(entry);
    }
    if (!set) entries.push_back({key, std::move(value)});
    return dictionary.isStream() ? CosObject::makeStream(std::move(entries), cos::CosStreamData{})
                                 : CosObject::makeDictionary(std::move(entries));
}

CosObject without(const CosObject& dictionary, std::string_view key)
{
    std::vector<CosObject::Entry> entries;
    for (const CosObject::Entry& entry : dictionary.entries()) {
        if (entry.key != key) entries.push_back(entry);
    }
    return CosObject::makeDictionary(std::move(entries));
}

// The objects as values, the stream's data beside it -- what the build writes.
struct Value {
    CosObject object;
    std::optional<std::string> data;
};

bool sameObject(const cos::IndirectObject& a, std::string_view aSource, const cos::IndirectObject& b, std::string_view bSource)
{
    if (a.object.isStream() != b.object.isStream()) return false;
    if (!a.object.isStream()) return a.object == b.object;
    return without(a.object, "Length") == without(b.object, "Length")
           && a.object.rawStreamData(aSource) == b.object.rawStreamData(bSource);
}

// The kids a payload's one node lists, by number.
std::vector<long long> pageRefsOf(const CosObject& kids)
{
    std::vector<long long> out;
    for (const CosObject& kid : kids.items()) {
        if (kid.isReference()) out.push_back(kid.reference().number);
    }
    return out;
}

} // namespace

void DocumentAssembler::setWriteFaultForTesting(WriteFault installed)
{
    const std::lock_guard<std::mutex> guard(faultMutex());
    fault() = std::move(installed);
}

core::Result<std::string> DocumentAssembler::build(std::string_view payload, std::string_view idSeed)
{
    auto opened = cos::openWithRepair(payload);
    if (!opened) return opened.error();
    if (!opened.value().usedOriginalXref || opened.value().report.requiresSaveAs()) return assemblyError(QStringLiteral("payload"));
    cos::XrefTable& table = opened.value().table;
    const CosObject& rootRef = table.trailer().get("Root");
    if (!rootRef.isReference()) return assemblyError(QStringLiteral("payload"));
    auto catalog = table.fetch(rootRef.reference().number);
    if (!catalog || !catalog.value().object.isDictionary()) return assemblyError(QStringLiteral("payload"));
    const CosObject& nodeRef = catalog.value().object.get("Pages");
    if (!nodeRef.isReference()) return assemblyError(QStringLiteral("payload"));
    const long long rootNode = nodeRef.reference().number;
    auto node = table.fetch(rootNode);
    if (!node || !node.value().object.isDictionary()) return assemblyError(QStringLiteral("payload"));
    const std::vector<long long> pages = pageRefsOf(node.value().object.get("Kids"));
    if (pages.empty()) return assemblyError(QStringLiteral("payload"));

    // Every object of the payload, as values.
    std::map<long long, Value> objects;
    long long largest = 0;
    for (const auto& [number, entry] : table.entries()) {
        if (number <= 0 || entry.type == cos::XrefEntryType::Free) continue;
        auto fetched = table.fetch(number);
        if (!fetched) return assemblyError(QStringLiteral("object:%1").arg(number));
        Value value{std::move(fetched.value().object), std::nullopt};
        if (value.object.isStream()) value.data = std::string(value.object.rawStreamData(table.source()));
        value.object.clearSpansRecursively();
        largest = std::max(largest, number);
        objects.emplace(number, std::move(value));
    }

    // THE TREE: pages dealt into leaves of at most `kMaxKids`, as evenly as the count divides, and those into nodes
    // above them, until the root (the payload's node, at its number) holds at most `kMaxKids`.
    struct Node {
        long long number = 0;
        std::vector<long long> kids;
        long long count = 0;
    };
    std::map<long long, long long> parentOf;   // a page or a node -> the node that lists it
    std::map<long long, long long> leavesOf;   // a node -> the pages beneath it
    std::vector<Node> made;
    std::vector<long long> level = pages;
    long long next = largest + 1;
    const auto leaves = [&](long long number) {
        const auto found = leavesOf.find(number);
        return found != leavesOf.end() ? found->second : 1LL;
    };
    while (level.size() > static_cast<std::size_t>(kMaxKids)) {
        const std::size_t groups = (level.size() + kMaxKids - 1) / kMaxKids;
        const std::size_t base = level.size() / groups;
        const std::size_t extra = level.size() % groups;
        std::vector<long long> above;
        std::size_t at = 0;
        for (std::size_t group = 0; group < groups; ++group) {
            const std::size_t size = base + (group < extra ? 1 : 0);
            Node made_node{next++, {}, 0};
            for (std::size_t i = 0; i < size; ++i, ++at) {
                made_node.kids.push_back(level[at]);
                made_node.count += leaves(level[at]);
                parentOf[level[at]] = made_node.number;
            }
            leavesOf[made_node.number] = made_node.count;
            above.push_back(made_node.number);
            made.push_back(std::move(made_node));
        }
        level = std::move(above);
    }
    for (const long long kid : level) parentOf[kid] = rootNode;
    made.push_back(Node{rootNode, level, static_cast<long long>(pages.size())});

    for (const Node& n : made) {
        std::vector<CosObject> kids;
        kids.reserve(n.kids.size());
        for (const long long kid : n.kids) kids.push_back(CosObject::makeReference(kid, 0));
        std::vector<CosObject::Entry> entries{{"Type", CosObject::makeName("Pages")},
                                              {"Kids", CosObject::makeArray(std::move(kids))},
                                              {"Count", CosObject::makeInteger(n.count)}};
        if (n.number != rootNode) entries.push_back({"Parent", CosObject::makeReference(parentOf.at(n.number), 0)});
        objects[n.number] = Value{CosObject::makeDictionary(std::move(entries)), std::nullopt};
    }
    for (const long long page : pages) {
        auto found = objects.find(page);
        if (found == objects.end() || !found->second.object.isDictionary()) return assemblyError(QStringLiteral("page:%1").arg(page));
        found->second.object = withEntry(found->second.object, "Parent", CosObject::makeReference(parentOf.at(page), 0));
    }

    // THE FRESH `/ID`: the payload and the caller's seed, hashed -- never the source file's.
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(QByteArrayView(idSeed.data(), static_cast<qsizetype>(idSeed.size())));
    hash.addData(QByteArrayView(payload.data(), static_cast<qsizetype>(payload.size())));
    const QByteArray digest = hash.result();
    const std::string id(digest.constData(), static_cast<std::size_t>(digest.size()));

    // Built as one classic section, then WRITTEN by `FullWriter` (D7-25).
    std::string built = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::map<long long, std::size_t> offsets;
    for (const auto& [number, value] : objects) {
        offsets[number] = built.size();
        built += cos::serializeIndirect(number, 0, value.object, value.data);
    }
    const long long size = objects.rbegin()->first + 1;
    const std::size_t xref = built.size();
    built += "xref\n0 " + std::to_string(size) + "\n0000000000 65535 f\r\n";
    char row[24];
    for (long long number = 1; number < size; ++number) {
        const auto found = offsets.find(number);
        if (found != offsets.end())
            std::snprintf(row, sizeof row, "%010llu 00000 n\r\n", static_cast<unsigned long long>(found->second));
        else
            std::snprintf(row, sizeof row, "%010d 00000 f\r\n", 0);
        built += row;
    }
    built += "trailer\n";
    built += cos::serializeObject(CosObject::makeDictionary(
        {{"Size", CosObject::makeInteger(size)},
         {"Root", rootRef},
         {"ID", CosObject::makeArray({CosObject::makeString(id, cos::StringForm::Hex), CosObject::makeString(id, cos::StringForm::Hex)})}}));
    built += "\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";

    auto loaded = cos::XrefTable::load(built);
    if (!loaded) return assemblyError(QStringLiteral("built"));
    auto written = cos::FullWriter::rewrite(built, loaded.value());
    if (!written) return written.error();
    return std::move(written.value().bytes);
}

core::Status DocumentAssembler::verify(std::string_view written, std::string_view payload)
{
    auto opened = cos::openWithRepair(written);
    if (!opened) return assemblyError(QStringLiteral("parse"));
    if (!opened.value().usedOriginalXref || opened.value().report.requiresSaveAs()) return assemblyError(QStringLiteral("repair"));
    cos::XrefTable& table = opened.value().table;
    if (table.sections().size() != 1 || table.sections().front().trailer.has("Prev")) return assemblyError(QStringLiteral("revisions"));
    if (table.isEncrypted()) return assemblyError(QStringLiteral("encrypted"));
    const CosObject& id = table.trailer().get("ID");
    if (!id.isArray() || id.items().size() != 2 || !id.items()[0].isString() || !id.items()[1].isString())
        return assemblyError(QStringLiteral("id"));

    std::vector<long long> held;
    for (const auto& [number, entry] : table.entries()) {
        if (number > 0 && entry.type != cos::XrefEntryType::Free) held.push_back(number);
    }
    if (cos::FullWriter::reachable(table) != held) return assemblyError(QStringLiteral("unreachable"));

    auto source = cos::openWithRepair(payload);
    if (!source) return assemblyError(QStringLiteral("payload"));
    cos::XrefTable& from = source.value().table;
    auto walk = PageOps::walk(table);
    auto planned = PageOps::walk(from);
    if (!walk || !planned || !walk.value().notes.empty()) return assemblyError(QStringLiteral("pages"));
    if (walk.value().pages.size() != planned.value().pages.size()) return assemblyError(QStringLiteral("pageCount"));

    // Each page the payload's page, in order, value for value but `/Parent`.
    std::set<long long> pageNumbers;
    for (std::size_t at = 0; at < walk.value().pages.size(); ++at) {
        const long long got = walk.value().pages[at].number;
        const long long want = planned.value().pages[at].number;
        if (got != want) return assemblyError(QStringLiteral("pageOrder:%1").arg(at + 1));
        auto a = table.fetch(got);
        auto b = from.fetch(want);
        if (!a || !b || without(a.value().object, "Parent") != without(b.value().object, "Parent"))
            return assemblyError(QStringLiteral("page:%1").arg(at + 1));
        pageNumbers.insert(got);
    }
    // Every other object the payload reached, at its number, value for value -- its catalog's keys but `/Pages`.
    const CosObject& root = from.trailer().get("Root");
    const long long catalog = root.isReference() ? root.reference().number : -1;
    std::set<long long> nodes;
    if (auto c = from.fetch(catalog); c && c.value().object.get("Pages").isReference()) nodes.insert(c.value().object.get("Pages").reference().number);
    for (const long long number : cos::FullWriter::reachable(from)) {
        if (pageNumbers.count(number) != 0 || nodes.count(number) != 0) continue;
        auto a = table.fetch(number);
        auto b = from.fetch(number);
        if (!a || !b) return assemblyError(QStringLiteral("object:%1").arg(number));
        if (number == catalog) {
            if (without(a.value().object, "Pages") != without(b.value().object, "Pages"))
                return assemblyError(QStringLiteral("catalog"));
            continue;
        }
        if (!sameObject(a.value(), table.source(), b.value(), from.source()))
            return assemblyError(QStringLiteral("object:%1").arg(number));
    }
    return core::Status{};
}

// --- Source --------------------------------------------------------------------------------------

core::Result<std::unique_ptr<DocumentAssembler::Source>> DocumentAssembler::Source::open(std::string bytes)
{
    std::unique_ptr<Source> source(new Source());
    source->bytes_ = std::move(bytes);
    auto opened = cos::openWithRepair(source->bytes_);
    if (!opened) return opened.error();
    if (opened.value().table.isEncrypted())
        return core::Error{core::ErrorCode::PermissionDenied, QLatin1String("error.pdf.encrypted"), QString()};
    source->opened_.emplace(std::move(opened).value());
    auto walk = PageOps::walk(source->opened_->table);
    if (!walk) return walk.error();
    if (!walk.value().notes.empty())
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.pdf.pageTreeDamaged"),
                           QString::number(walk.value().notes.front().objectNumber)};
    source->walk_ = std::move(walk).value();
    return source;
}

core::Result<DocumentAssembler::Assembled> DocumentAssembler::Source::make(const std::vector<int>& pages)
{
    auto copied = ObjectGraphCopier::copy(opened_->table, walk_, pages);
    if (!copied) return copied.error();
    // The seed: this process's clock, this source's count and the pages -- unique per document made, so two extracts
    // of the same pages are two files with two identifiers.
    std::string seed = std::to_string(QDateTime::currentMSecsSinceEpoch()) + ':' + std::to_string(++made_);
    for (const int page : pages) seed += ',' + std::to_string(page);
    auto built = build(copied.value().pdf, seed);
    if (!built) return built.error();
    if (core::Status verified = verify(built.value(), copied.value().pdf); !verified) return verified.error();
    Assembled out;
    out.bytes = std::move(built).value();
    out.payload = std::move(copied.value().pdf);
    out.pageCount = copied.value().pageCount;
    out.objects = copied.value().objects;
    out.report = std::move(copied.value().report);
    return out;
}

core::Result<core::AssembledDocument> DocumentAssembler::Source::assemble(const std::vector<int>& pages)
{
    auto made = make(pages);
    if (!made) return made.error();
    core::AssembledDocument out;
    out.bytes = QByteArray(made.value().bytes.data(), static_cast<qsizetype>(made.value().bytes.size()));
    out.pageCount = made.value().pageCount;
    out.report = std::move(made.value().report);
    return out;
}

core::Result<std::vector<core::WrittenDocument>> DocumentAssembler::Source::write(const std::vector<core::AssemblyOutput>& outputs,
                                                                                  const QString& folder)
{
    // EVERY NAME FIRST, before a byte is written: a bare file name, once, and nothing the folder holds.
    if (folder.isEmpty()) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.assembly.badName"), folder};
    const QDir directory(QFileInfo(folder).absoluteFilePath());
    std::set<QString> names;
    std::vector<QString> targets;
    for (const core::AssemblyOutput& output : outputs) {
        const QString& name = output.fileName;
        if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..") || name.contains(QLatin1Char('/'))
            || name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char(':')) || QFileInfo(name).fileName() != name)
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.assembly.badName"), name};
        if (!names.insert(name.toCaseFolded()).second)   // Windows' names are case-blind
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.assembly.duplicateName"), name};
        const QString target = directory.filePath(name);
        if (QFileInfo::exists(target)) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.assembly.exists"), target};
        targets.push_back(target);
    }
    if (outputs.empty()) return std::vector<core::WrittenDocument>{};
    if (QFileInfo(directory.absolutePath()).exists() && !QFileInfo(directory.absolutePath()).isDir())
        return writeFailed(directory.absolutePath());
    if (!QDir().mkpath(directory.absolutePath())) return writeFailed(directory.absolutePath());

    std::vector<core::WrittenDocument> written;
    const auto takeBack = [&written] {
        for (const core::WrittenDocument& file : written) QFile::remove(file.path);
    };
    for (std::size_t at = 0; at < outputs.size(); ++at) {
        auto made = make(outputs[at].pages);
        if (!made) {
            takeBack();
            return made.error();
        }
        std::string bytes = std::move(made.value().bytes);
        applyFault(bytes);
        const QString& target = targets[at];
        // Beside the target, under a name nothing holds (`NewOnly`), then MOVED onto the target -- which a rename does
        // only when nothing is there: a file that appeared since the names were checked is refused, never replaced.
        const QString temporary = target + QStringLiteral(".rawpage-%1.tmp").arg(QDateTime::currentMSecsSinceEpoch());
        QFile file(temporary);
        bool ok = file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                  && file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size());
        file.close();
        ok = ok && file.error() == QFileDevice::NoError;
        if (!ok) {
            QFile::remove(temporary);
            takeBack();
            return writeFailed(target);
        }
        if (QFileInfo::exists(target) || !QFile::rename(temporary, target)) {
            QFile::remove(temporary);
            takeBack();
            return QFileInfo::exists(target)
                       ? core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.assembly.exists"), target}
                       : writeFailed(target);
        }
        written.push_back(core::WrittenDocument{target, made.value().pageCount, static_cast<qint64>(bytes.size()),
                                                std::move(made.value().report)});
        // READ BACK FROM THE PATH and verified there: the claim is about the file on disk.
        QFile back(target);
        if (!back.open(QIODevice::ReadOnly)) {
            takeBack();
            return writeFailed(target);
        }
        const QByteArray onDisk = back.readAll();
        back.close();
        if (core::Status verified = verify(std::string_view(onDisk.constData(), static_cast<std::size_t>(onDisk.size())), made.value().payload);
            !verified) {
            takeBack();
            return verified.error();
        }
    }
    return written;
}

} // namespace rawpage::pdf::ops
