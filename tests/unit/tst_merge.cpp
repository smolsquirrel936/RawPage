// tst_merge -- Stage 7 T10 (`Stage7-Assembly-Plan.md` §6, D7-16, D7-33): the merge's headless half -- guide cases A-68,
// A-69, A-70 and A-71, and A-66's and A-67's structure (`Stage7-Assembly-Testing-Guide.md` §5.12).
//
// Every merged document is READ BACK through the first-party reader and graded against the CORPUS LABELS
// (`corpus/labels/assembly-merge-*.json`, `assembly-layers.json`) -- the fields each source holds, its values, its
// layers and which are on -- never against what the merger computed. Each one is kept in RAWPAGE_WRITTEN_DIR as
// `assembly-merge-*.pdf`, where the portable-asan job's `qpdf --check` reads it (D7-18's second oracle).
//
// WHAT IT DOES NOT COVER (convention 19): where a bookmark or a link LANDS by what the page shows, the field names as
// PDFium's form layer reads them, and how a merged page draws -- `tests/render/tst_mergeengine.cpp` (A-66, A-67, A-68's
// engine half); the dialog and the run off the UI thread -- `tests/ui/tst_mergedialog.cpp` and
// `tests/composition/tst_mergeproduct.cpp` (A-72).

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Merge.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/DocumentMerger.h"
#include "pdf/ops/EmbeddedFileReader.h"
#include "pdf/ops/PageOps.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <tuple>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using pdf::cos::CosObject;
using pdf::ops::DocumentMerger;

namespace {

QString corpus(const char* relative)
{
    return QDir(QStringLiteral(RAWPAGE_CORPUS_DIR)).filePath(QString::fromUtf8(relative));
}

QJsonObject label(const char* name)
{
    QFile file(corpus("labels/") + QString::fromUtf8(name));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

std::vector<core::MergeInput> inputsOf(std::initializer_list<const char*> relatives)
{
    std::vector<core::MergeInput> out;
    for (const char* relative : relatives) out.push_back(core::MergeInput{corpus(relative), {}});
    return out;
}

void keep(const QByteArray& bytes, const QString& name)
{
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    QDir().mkpath(dir);
    QFile file(QDir(dir).filePath(name));
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(bytes) == bytes.size());
}

core::MergedDocument merged(const std::vector<core::MergeInput>& inputs, const core::MergeOptions& options, const QString& keepAs = {})
{
    auto result = DocumentMerger::mergeFiles(inputs, options, {});
    INFO((result ? std::string() : std::string(result.error().messageKey.data()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    if (!keepAs.isEmpty()) keep(result.value().bytes, keepAs);
    return std::move(result).value();
}

// A merged file read back, first-party, over bytes it keeps.
struct Read {
    explicit Read(const QByteArray& data) : bytes(data.constData(), static_cast<std::size_t>(data.size()))
    {
        auto opened = pdf::cos::openWithRepair(bytes);
        REQUIRE(opened);
        CHECK(opened.value().usedOriginalXref);
        CHECK_FALSE(opened.value().report.requiresSaveAs());
        table.emplace(std::move(opened.value().table));
        CHECK(table->sections().size() == 1);
        auto walked = pdf::ops::PageOps::walk(*table);
        REQUIRE(walked);
        REQUIRE(walked.value().notes.empty());
        for (const pdf::ops::PageEntry& page : walked.value().pages) pages.push_back(page.number);
    }
    CosObject object(long long number)
    {
        auto fetched = table->fetch(number);
        REQUIRE(fetched);
        CosObject value = fetched.value().object;
        value.clearSpansRecursively();
        return value;
    }
    CosObject resolve(const CosObject& value) { return value.isReference() ? object(value.reference().number) : value; }
    CosObject catalog() { return object(table->trailer().get("Root").reference().number); }
    int pageIndexOf(const CosObject& reference)
    {
        if (!reference.isReference()) return -1;
        const auto at = std::find(pages.begin(), pages.end(), reference.reference().number);
        return at == pages.end() ? -2 : static_cast<int>(at - pages.begin());
    }
    static QString text(const CosObject& value) { return value.isString() ? pdf::ops::EmbeddedFileReader::decodeTextString(value.text()) : QString(); }

    std::string bytes;
    std::optional<pdf::cos::XrefTable> table;
    std::vector<long long> pages;
};

// Every TERMINAL field the merged form holds, by its fully qualified name: its value (inherited down the chain), and
// its widgets by number -- with every `/Parent` checked against the `/Kids` that lists it on the way down.
struct Field {
    QString value;
    std::vector<long long> widgets;
    QString type;
};

std::map<QString, Field> fieldsOf(Read& read, std::vector<std::string>& broken)
{
    std::map<QString, Field> out;
    const CosObject form = read.resolve(read.catalog().get("AcroForm"));
    std::function<void(const CosObject&, long long, const QString&, const QString&, const QString&, int)> visit =
        [&](const CosObject& reference, long long parent, const QString& prefix, const QString& inheritedValue, const QString& inheritedType,
            int depth) {
            REQUIRE(depth < 16);
            REQUIRE(reference.isReference());
            const long long number = reference.reference().number;
            const CosObject node = read.object(number);
            const CosObject& up = node.get("Parent");
            if (parent < 0 ? up.isReference() : (!up.isReference() || up.reference().number != parent))
                broken.push_back("object " + std::to_string(number) + "'s /Parent does not name the node that lists it");
            const QString partial = Read::text(node.get("T"));
            const QString name = partial.isEmpty() ? prefix : (prefix.isEmpty() ? partial : prefix + QLatin1Char('.') + partial);
            const QString value = node.has("V") ? Read::text(node.get("V")) : inheritedValue;
            const QString type = node.get("FT").isName() ? QString::fromStdString(node.get("FT").text()) : inheritedType;
            std::vector<CosObject> kids = read.resolve(node.get("Kids")).items();
            const bool namedKids = std::any_of(kids.begin(), kids.end(), [&](const CosObject& k) { return read.object(k.reference().number).has("T"); });
            if (!namedKids) {
                Field& field = out[name];
                field.value = value;
                field.type = type;
                if (node.get("Subtype").isName() && node.get("Subtype").text() == "Widget") field.widgets.push_back(number);
                for (const CosObject& kid : kids) {
                    const CosObject widget = read.object(kid.reference().number);
                    if (!widget.get("Parent").isReference() || widget.get("Parent").reference().number != number)
                        broken.push_back("widget " + std::to_string(kid.reference().number) + " does not name its field");
                    field.widgets.push_back(kid.reference().number);
                }
                return;
            }
            for (const CosObject& kid : kids) visit(kid, number, name, value, type, depth + 1);
        };
    for (const CosObject held166 = read.resolve(form.get("Fields")); const CosObject& root : held166.items()) visit(root, -1, QString(), QString(), QString(), 0);
    return out;
}

// What the labels say each source's fields are: fully qualified name -> value.
std::map<QString, QString> labelledFields(const char* name)
{
    std::map<QString, QString> out;
    for (const QJsonValue value : label(name).value(QStringLiteral("fields")).toArray()) {
        const QJsonObject field = value.toObject();
        const QString top = field.value(QStringLiteral("name")).toString();
        const QJsonArray kids = field.value(QStringLiteral("kids")).toArray();
        if (kids.isEmpty()) out[top] = field.value(QStringLiteral("value")).toString();
        for (const QJsonValue kid : kids) out[top + QLatin1Char('.') + kid.toObject().value(QStringLiteral("name")).toString()] = field.value(QStringLiteral("value")).toString();
    }
    return out;
}

// The name a field of a later source carries once its top-level name was renamed `top_n`.
QString renamedTop(const QString& qualified, const QString& to)
{
    const qsizetype dot = qualified.indexOf(QLatin1Char('.'));
    return dot < 0 ? to : to + qualified.mid(dot);
}

struct OutlineNode {
    QString title;
    int page = -1;   // the merged page index its destination names; -1 none
    bool open = false;
    std::vector<OutlineNode> children;
};

std::vector<OutlineNode> outlineOf(Read& read, std::vector<std::string>& broken)
{
    std::function<std::vector<OutlineNode>(const CosObject&, long long, int)> chain = [&](const CosObject& first, long long parent, int depth) {
        std::vector<OutlineNode> out;
        REQUIRE(depth < 16);
        CosObject at = first;
        long long previous = -1;
        while (at.isReference()) {
            const long long number = at.reference().number;
            const CosObject item = read.object(number);
            if (!item.get("Parent").isReference() || item.get("Parent").reference().number != parent)
                broken.push_back("outline item " + std::to_string(number) + " names the wrong parent");
            if (previous >= 0 && (!item.get("Prev").isReference() || item.get("Prev").reference().number != previous))
                broken.push_back("outline item " + std::to_string(number) + " names the wrong previous item");
            OutlineNode node;
            node.title = Read::text(item.get("Title"));
            const CosObject dest = item.has("Dest") ? read.resolve(item.get("Dest")) : read.resolve(read.resolve(item.get("A")).get("D"));
            if (dest.isArray() && !dest.items().empty()) node.page = read.pageIndexOf(dest.items().front());
            node.open = item.get("Count").integerValue() > 0;
            node.children = chain(item.get("First"), number, depth + 1);
            out.push_back(std::move(node));
            previous = number;
            at = item.get("Next");
        }
        return out;
    };
    const CosObject root = read.resolve(read.catalog().get("Outlines"));
    if (!root.isDictionary()) return {};
    return chain(root.get("First"), read.catalog().get("Outlines").reference().number, 0);
}

} // namespace

TEST_CASE("merge.fields renamed: every field present and the colliding top-level names suffixed", "[merge][merge.fields][A-68]")
{
    core::MergeOptions options;
    options.renameCollidingFields = true;
    const core::MergedDocument done = merged(inputsOf({"assembly/merge-a.pdf", "assembly/merge-b.pdf", "assembly/merge-c.pdf"}), options,
                                             QStringLiteral("assembly-merge-fields-renamed.pdf"));
    Read read(done.bytes);
    std::vector<std::string> broken;
    const std::map<QString, Field> fields = fieldsOf(read, broken);
    for (const std::string& line : broken) FAIL_CHECK(line);

    // The labels, with the renames the policy implies: a later source's top-level name an earlier source used becomes
    // `name_n`, n the first number free.
    std::map<QString, QString> expected;
    std::set<QString> tops;
    std::vector<core::FieldCollision> collisions;
    int input = 0;
    for (const char* name : {"assembly-merge-a.json", "assembly-merge-b.json", "assembly-merge-c.json"}) {
        std::map<QString, QString> renames;
        for (const auto& [qualified, value] : labelledFields(name)) {
            const QString top = qualified.section(QLatin1Char('.'), 0, 0);
            if (renames.count(top) == 0) {
                QString to = top;
                for (int n = 2; tops.count(to) != 0; ++n) to = top + QLatin1Char('_') + QString::number(n);
                renames[top] = to;
            }
        }
        for (const auto& [top, to] : renames) {
            tops.insert(to);
            if (to != top) collisions.push_back(core::FieldCollision{top, to, input});
        }
        for (const auto& [qualified, value] : labelledFields(name))
            expected[renamedTop(qualified, renames[qualified.section(QLatin1Char('.'), 0, 0)])] = value;
        ++input;
    }
    std::map<QString, QString> got;
    for (const auto& [name, field] : fields) {
        got[name] = field.value;
        CHECK(field.widgets.size() == 1);
    }
    CHECK(got == expected);
    // EITHER WAY THE REPORT LISTS EVERY COLLISION -- here, each a rename, as the labels imply.
    std::sort(collisions.begin(), collisions.end(), [](const auto& a, const auto& b) { return std::tie(a.input, a.name) < std::tie(b.input, b.name); });
    std::vector<core::FieldCollision> reported = done.report.fields;
    std::sort(reported.begin(), reported.end(), [](const auto& a, const auto& b) { return std::tie(a.input, a.name) < std::tie(b.input, b.name); });
    REQUIRE(reported.size() == collisions.size());
    for (std::size_t i = 0; i < reported.size(); ++i) {
        CHECK(reported[i].name == collisions[i].name);
        CHECK(reported[i].renamedTo == collisions[i].renamedTo);
        CHECK(reported[i].input == collisions[i].input);
    }
    std::cout << "A-68 renamed: " << fields.size() << " fields, " << reported.size() << " collisions renamed" << std::endl;
}

TEST_CASE("merge.fields merged on purpose: one field per colliding name, its widgets sharing its value", "[merge][merge.fields][A-68]")
{
    core::MergeOptions options;
    options.renameCollidingFields = false;
    const core::MergedDocument done = merged(inputsOf({"assembly/merge-a.pdf", "assembly/merge-b.pdf", "assembly/merge-c.pdf"}), options,
                                             QStringLiteral("assembly-merge-fields-merged.pdf"));
    Read read(done.bytes);
    std::vector<std::string> broken;
    const std::map<QString, Field> fields = fieldsOf(read, broken);
    for (const std::string& line : broken) FAIL_CHECK(line);

    // One field per fully qualified name in ANY source, its value the FIRST source's, its widgets one per source that
    // has it.
    std::map<QString, std::pair<QString, int>> expected;
    for (const char* name : {"assembly-merge-a.json", "assembly-merge-b.json", "assembly-merge-c.json"}) {
        for (const auto& [qualified, value] : labelledFields(name)) {
            auto [at, fresh] = expected.emplace(qualified, std::make_pair(value, 0));
            ++at->second.second;
            (void)fresh;
        }
    }
    REQUIRE(fields.size() == expected.size());
    for (const auto& [name, want] : expected) {
        INFO(name.toStdString());
        REQUIRE(fields.count(name) == 1);
        const Field& field = fields.at(name);
        CHECK(field.value == want.first);
        CHECK(static_cast<int>(field.widgets.size()) == want.second);
        // ... and every widget shows the one value: the surviving field's appearance on each.
        std::set<std::string> appearances;
        for (const long long widget : field.widgets) appearances.insert(pdf::cos::serializeObject(read.object(widget).get("AP")));
        CHECK(appearances.size() == 1);
    }
    CHECK(read.resolve(read.catalog().get("AcroForm")).get("NeedAppearances").booleanValue());
    // The collisions: the top-level names a later source shared with an earlier one, each MERGED.
    std::set<std::pair<int, QString>> want{{1, QStringLiteral("name")}, {1, QStringLiteral("date")}, {2, QStringLiteral("name")},
                                           {2, QStringLiteral("date")}, {2, QStringLiteral("address")}};
    std::set<std::pair<int, QString>> reported;
    for (const core::FieldCollision& collision : done.report.fields) {
        CHECK(collision.merged());
        reported.emplace(collision.input, collision.name);
    }
    CHECK(reported == want);
    std::cout << "A-68 merged: " << fields.size() << " fields, " << done.report.fields.size() << " collisions merged" << std::endl;
}

TEST_CASE("merge.layers every input's groups unioned, each input's order under its own label", "[merge][merge.layers][A-69]")
{
    const core::MergedDocument done = merged(inputsOf({"assembly/layers.pdf", "assembly/merge-a.pdf"}), core::MergeOptions{},
                                             QStringLiteral("assembly-merge-layers.pdf"));
    Read read(done.bytes);
    const CosObject properties = read.resolve(read.catalog().get("OCProperties"));
    REQUIRE(properties.isDictionary());
    const auto nameOf = [&](const CosObject& group) { return Read::text(read.object(group.reference().number).get("Name")); };
    const auto names = [&](const CosObject& list) {
        std::multiset<QString> out;
        for (const CosObject held341 = read.resolve(list); const CosObject& item : held341.items()) {
            if (item.isReference()) out.insert(nameOf(item));
        }
        return out;
    };

    // `/OCGs`: every group of both inputs, a name two inputs share kept twice.
    std::multiset<QString> all;
    std::multiset<QString> off;
    std::multiset<QString> locked;
    for (const QJsonValue value : label("assembly-layers.json").value(QStringLiteral("groups")).toArray()) {
        const QJsonObject group = value.toObject();
        all.insert(group.value(QStringLiteral("name")).toString());
        if (!group.value(QStringLiteral("defaultOn")).toBool()) off.insert(group.value(QStringLiteral("name")).toString());
        if (group.value(QStringLiteral("locked")).toBool()) locked.insert(group.value(QStringLiteral("name")).toString());
    }
    for (const QJsonValue value : label("assembly-merge-a.json").value(QStringLiteral("layers")).toArray())
        all.insert(value.toObject().value(QStringLiteral("name")).toString());
    CHECK(names(properties.get("OCGs")) == all);
    const CosObject d = read.resolve(properties.get("D"));
    CHECK(names(d.get("OFF")) == off);
    CHECK(names(d.get("Locked")) == locked);
    // `/Order`: one labelled array per input, in order, holding every group of that input.
    const std::vector<CosObject> order = read.resolve(d.get("Order")).items();
    REQUIRE(order.size() == 2);
    const auto groupsUnder = [&](const CosObject& value) {
        std::multiset<QString> out;
        std::function<void(const CosObject&)> walk = [&](const CosObject& v) {
            for (const CosObject held369 = read.resolve(v); const CosObject& item : held369.items()) {
                if (item.isReference()) out.insert(nameOf(item));
                else if (item.isArray()) walk(item);
            }
        };
        walk(value);
        return out;
    };
    CHECK(Read::text(order[0].items().front()) == QStringLiteral("layers"));
    CHECK(Read::text(order[1].items().front()) == QStringLiteral("merge-a"));
    CHECK(groupsUnder(order[1]) == std::multiset<QString>{QStringLiteral("Draft"), QStringLiteral("Source A notes")});
    CHECK(groupsUnder(order[0]).size() == 8);
    // The radio pair and the usage come too.
    const std::vector<CosObject> radios = read.resolve(d.get("RBGroups")).items();
    REQUIRE(radios.size() == 1);
    CHECK(names(radios.front()) == std::multiset<QString>{QStringLiteral("English"), QStringLiteral("French")});
    CHECK_FALSE(read.resolve(d.get("AS")).items().empty());
}

TEST_CASE("merge.skipped inputs are reported with their reason and the others merged", "[merge][merge.skipped][A-70]")
{
    std::vector<core::MergeInput> inputs = inputsOf({"assembly/merge-a.pdf", "encrypted/aes-128.pdf", "assembly/merge-not-a-pdf.pdf",
                                                     "malformed/truncated-mid-stream.pdf", "assembly/merge-b.pdf"});
    inputs.insert(inputs.begin() + 2, core::MergeInput{corpus("assembly/no-such-file.pdf"), {}});
    const core::MergedDocument done = merged(inputs, core::MergeOptions{}, QStringLiteral("assembly-merge-skipped.pdf"));
    std::map<int, std::string> reasons;
    for (const core::MergeSkip& skip : done.report.skipped) {
        reasons[skip.input] = std::string(skip.reasonKey.toStdString());
        CHECK(skip.path == inputs[static_cast<std::size_t>(skip.input)].path);
    }
    CHECK(reasons == std::map<int, std::string>{{1, "error.pdf.encrypted"},
                                               {2, "error.open.notFound"},
                                               {3, "error.open.notPdf"},
                                               {4, "error.merge.repairedWithLoss"}});
    CHECK(done.report.inputsMerged == 2);
    CHECK(done.report.pageCount == 3 + 2);
    Read read(done.bytes);
    CHECK(read.pages.size() == 5);

    // NOTHING TO MERGE is refused whole, by key.
    auto none = DocumentMerger::mergeFiles(inputsOf({"encrypted/aes-128.pdf", "assembly/merge-not-a-pdf.pdf"}), core::MergeOptions{}, {});
    REQUIRE_FALSE(none);
    CHECK(none.error().messageKey == QLatin1String("error.merge.nothingMerged"));
}

TEST_CASE("merge.separator one blank page between inputs at the size of the page before it", "[merge][merge.separator][A-71]")
{
    core::MergeOptions options;
    options.separatorPages = true;
    const core::MergedDocument done = merged(inputsOf({"assembly/merge-a.pdf", "assembly/merge-b.pdf", "assembly/merge-c.pdf"}), options,
                                             QStringLiteral("assembly-merge-separator.pdf"));
    Read read(done.bytes);
    REQUIRE(read.pages.size() == 3 + 1 + 2 + 1 + 4);
    CHECK(done.report.separators == 2);
    // The page before each separator, from the labels: MA-0003 and MB-0002.
    const auto boxOf = [&](std::size_t index) {
        std::vector<double> out;
        for (const CosObject held426 = read.resolve(read.object(read.pages[index]).get("MediaBox")); const CosObject& n : held426.items()) out.push_back(n.numberValue());
        return out;
    };
    const auto labelled = [](const char* name, int page) {
        std::vector<double> out;
        for (const QJsonValue n : label(name).value(QStringLiteral("pages")).toArray()[page].toObject().value(QStringLiteral("mediaBox")).toArray())
            out.push_back(n.toDouble());
        return out;
    };
    for (const auto& [index, want] : {std::make_pair(std::size_t{3}, labelled("assembly-merge-a.json", 2)),
                                      std::make_pair(std::size_t{6}, labelled("assembly-merge-b.json", 1))}) {
        const CosObject page = read.object(read.pages[index]);
        CHECK_FALSE(page.has("Contents"));
        CHECK_FALSE(page.has("Annots"));
        const std::vector<double> box = boxOf(index);
        REQUIRE(box.size() == 4);
        for (std::size_t i = 0; i < 4; ++i) CHECK(std::abs(box[i] - want[i]) < 1e-3);
    }
    // NONE AFTER THE LAST: the merged document ends on MC-0004's page, which has content.
    CHECK(read.object(read.pages.back()).has("Contents"));

    // THE PAGE BEFORE, not the input's first: merge-c's first page is A5, its last A4 -- the separator after it is A4.
    const core::MergedDocument after = merged(inputsOf({"assembly/merge-c.pdf", "assembly/merge-a.pdf"}), options);
    Read sized(after.bytes);
    REQUIRE(sized.pages.size() == 4 + 1 + 3);
    std::vector<double> box;
    for (const CosObject held = sized.resolve(sized.object(sized.pages[4]).get("MediaBox")); const CosObject& n : held.items())
        box.push_back(n.numberValue());
    const std::vector<double> want = labelled("assembly-merge-c.json", 3);
    REQUIRE(box.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) CHECK(std::abs(box[i] - want[i]) < 1e-3);

    const core::MergedDocument plain = merged(inputsOf({"assembly/merge-a.pdf", "assembly/merge-b.pdf", "assembly/merge-c.pdf"}), core::MergeOptions{});
    CHECK(Read(plain.bytes).pages.size() == 9);
    CHECK(plain.report.separators == 0);
}

TEST_CASE("merge.outline a bookmark per file, the sources' outlines beneath, as the setting says", "[merge][merge.outline][A-66]")
{
    const auto inputs = inputsOf({"assembly/merge-a.pdf", "assembly/merge-b.pdf", "assembly/merge-c.pdf"});
    const std::vector<int> firstPage{0, 3, 5};
    // Each source's own outline, from its label: titles, nested.
    std::vector<std::vector<QString>> sourceTitles;
    for (const char* name : {"assembly-merge-a.json", "assembly-merge-b.json", "assembly-merge-c.json"}) {
        std::vector<QString> titles;
        std::function<void(const QJsonArray&)> walk = [&](const QJsonArray& items) {
            for (const QJsonValue item : items) {
                titles.push_back(item.toObject().value(QStringLiteral("title")).toString());
                walk(item.toObject().value(QStringLiteral("children")).toArray());
            }
        };
        walk(label(name).value(QStringLiteral("outline")).toArray());
        sourceTitles.push_back(titles);
    }
    const auto flatten = [](const std::vector<OutlineNode>& nodes) {
        std::vector<QString> out;
        std::function<void(const std::vector<OutlineNode>&)> walk = [&](const std::vector<OutlineNode>& level) {
            for (const OutlineNode& node : level) {
                out.push_back(node.title);
                walk(node.children);
            }
        };
        walk(nodes);
        return out;
    };

    for (const core::MergeBookmarks mode : core::kMergeBookmarks) {
        INFO(std::string(core::mergeBookmarksName(mode)));
        core::MergeOptions options;
        options.bookmarks = mode;
        const core::MergedDocument done =
            merged(inputs, options, QStringLiteral("assembly-merge-outline-%1.pdf").arg(QString::fromUtf8(core::mergeBookmarksName(mode).data())));
        Read read(done.bytes);
        std::vector<std::string> broken;
        const std::vector<OutlineNode> top = outlineOf(read, broken);
        for (const std::string& line : broken) FAIL_CHECK(line);
        if (mode == core::MergeBookmarks::None) {
            std::vector<QString> want;
            for (const auto& titles : sourceTitles) want.insert(want.end(), titles.begin(), titles.end());
            CHECK(flatten(top) == want);
            continue;
        }
        REQUIRE(top.size() == 3);
        for (std::size_t k = 0; k < 3; ++k) {
            const QString want = mode == core::MergeBookmarks::Title
                                     ? label(std::vector<const char*>{"assembly-merge-a.json", "assembly-merge-b.json", "assembly-merge-c.json"}[k])
                                           .value(QStringLiteral("title"))
                                           .toString()
                                     : QStringLiteral("merge-%1").arg(QChar(QLatin1Char('a' + static_cast<char>(k))));
            CHECK(top[k].title == want);
            CHECK(top[k].page == firstPage[k]);
            CHECK_FALSE(top[k].open);   // closed: one bookmark per input on opening
            CHECK(flatten(top[k].children) == sourceTitles[k]);
        }
    }
}

TEST_CASE("merge.outline a range leaves a bookmark whose page it left out, cleared and reported", "[merge][merge.outline][merge.links][A-66][A-67]")
{
    // merge-a's pages 1 and 3: its outline names all three, its link on page 1 names page 3.
    std::vector<core::MergeInput> inputs = inputsOf({"assembly/merge-a.pdf", "assembly/merge-b.pdf"});
    inputs[0].pages = {0, 2};
    const core::MergedDocument done = merged(inputs, core::MergeOptions{}, QStringLiteral("assembly-merge-range.pdf"));
    Read read(done.bytes);
    REQUIRE(read.pages.size() == 2 + 2);
    std::vector<std::string> broken;
    const std::vector<OutlineNode> top = outlineOf(read, broken);
    for (const std::string& line : broken) FAIL_CHECK(line);
    REQUIRE(top.size() == 2);
    REQUIRE(top[0].children.size() == 1);
    const OutlineNode& source = top[0].children.front();   // MA-0001, whose children are MA-0002 and MA-0003
    CHECK(source.page == 0);
    REQUIRE(source.children.size() == 2);
    CHECK(source.children[0].title == QStringLiteral("MA-0002"));
    CHECK(source.children[0].page == -1);   // its page left out: the item kept, its destination gone (D7-2)
    CHECK(source.children[1].page == 1);
    // ... and reported, by the input it came from.
    REQUIRE(done.report.references.size() == 2);
    const auto& reported = done.report.references[0].references;
    REQUIRE(reported.size() == 1);
    CHECK(reported.front().kind == core::PageReference::Kind::OutlineItem);
    CHECK(reported.front().treatment == core::PageReference::Treatment::Cleared);
    CHECK(reported.front().name == QStringLiteral("MA-0002"));
    // The link on MA-0001 to MA-0003 still lands on the copy of MA-0003.
    const std::vector<CosObject> annots = read.resolve(read.object(read.pages[0]).get("Annots")).items();
    int links = 0;
    for (const CosObject& annotation : annots) {
        const CosObject value = read.resolve(annotation);
        if (!value.get("Subtype").isName() || value.get("Subtype").text() != "Link") continue;
        ++links;
        CHECK(read.pageIndexOf(read.resolve(value.get("Dest")).items().front()) == 1);
    }
    CHECK(links == 1);
}

TEST_CASE("merge.cancel a progress that answers false stops the merge, by key", "[merge][merge.cancel][A-72]")
{
    // Asked before each input (0, 1), before the build (2) and before the verify (3); the last call (4) only tells.
    for (const int at : {0, 1, 2, 3}) {
        int calls = 0;
        auto result = DocumentMerger::mergeFiles(inputsOf({"assembly/merge-a.pdf", "assembly/merge-b.pdf"}), core::MergeOptions{},
                                                 [&](int done, int total) {
                                                     CHECK(total == 4);
                                                     ++calls;
                                                     return done < at;
                                                 });
        INFO("stopped at step " << at);
        REQUIRE_FALSE(result);
        CHECK(result.error().messageKey == QLatin1String("error.merge.cancelled"));
    }
    // ... and a progress that always answers true sees every step, the last `total` of `total`.
    std::vector<int> seen;
    auto result = DocumentMerger::mergeFiles(inputsOf({"assembly/merge-a.pdf", "assembly/merge-b.pdf"}), core::MergeOptions{},
                                             [&](int done, int) {
                                                 seen.push_back(done);
                                                 return true;
                                             });
    REQUIRE(result);
    CHECK(seen == std::vector<int>{0, 1, 2, 3, 4});
}

TEST_CASE("merge.renamed a colliding name takes the first number free", "[merge][merge.fields]")
{
    CHECK(DocumentMerger::renamed(QStringLiteral("name"), 2) == QStringLiteral("name_2"));
    // A file merged with itself three times: every top-level name of the second and third renamed, in order.
    const core::MergedDocument done =
        merged(inputsOf({"assembly/merge-b.pdf", "assembly/merge-b.pdf", "assembly/merge-b.pdf"}), core::MergeOptions{});
    std::set<QString> renamedTo;
    for (const core::FieldCollision& collision : done.report.fields) renamedTo.insert(collision.renamedTo);
    CHECK(renamedTo == std::set<QString>{QStringLiteral("name_2"), QStringLiteral("date_2"), QStringLiteral("address_2"), QStringLiteral("b-only_2"),
                                         QStringLiteral("name_3"), QStringLiteral("date_3"), QStringLiteral("address_3"), QStringLiteral("b-only_3")});
}
