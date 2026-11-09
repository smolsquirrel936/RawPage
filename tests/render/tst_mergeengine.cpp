// tst_mergeengine -- Stage 7 T10 (`Stage7-Assembly-Plan.md` §6, D7-16): a merge through the SHIPPED backend and its
// engine -- guide cases A-66 and A-67, and A-68's engine half (`Stage7-Assembly-Testing-Guide.md` §5.12).
//
// THE ORACLES ARE THE ENGINE AND THE LABELS (D7-18), never the merger: the merged document is written to a file and
// opened by PDFium as any file is -- with NO repair -- and
//   * a BOOKMARK is graded by the identity PDFium extracts from the page PDFium resolves it to: a file's bookmark
//     lands on that file's first page, and each source bookmark -- titled by the generator with the identity of the
//     page it targets (D7-17) -- lands on a page showing its title;
//   * a LINK, read from the merged bytes by this file's own small reader (PDFium's public API reads links nowhere this
//     product reaches -- `tst_reference_repair`'s reason), lands on the page showing its label's `targetIdentity`;
//     every comment of every source is on the page showing its label's `pageIdentity`, a reply's `/IRT` and a note's
//     `/Popup` naming annotations of the same page;
//   * the FIELDS are what PDFium's form layer reads (`IDocumentBackend::searchUnits`, `FPDFAnnot_GetFormFieldName` and
//     `...Value`), against the labels with the renames the policy implies, or one field per name with merging;
//   * every merged page DRAWS as the source page it came from, with its annotations and widgets, at 72 dpi: 0
//     differing bytes (§3.4's bar), renamed fields included.
//
// WHAT IT DOES NOT COVER (convention 19): the merged-on-purpose widgets' DRAWING -- each shows the surviving field's
// appearance, which is not its source's, and is A-68's headless half (one appearance per field); `qpdf --check`
// (portable-asan, over `tst_merge`'s files); Acrobat and Firefox (M-02).

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/backend.h"
#include "core/doc/Merge.h"
#include "core/doc/Outline.h"
#include "core/render/RenderRequest.h"
#include "core/text/SearchUnit.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using rawpage::test_oracle::corpusPath;
using core::text::PageTextModel;
using pdf::cos::CosObject;

namespace {

QJsonObject label(const QString& name)
{
    QFile file(corpusPath("labels/" + name.toStdString()));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

std::string identityOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page)
{
    core::ITextExtractor* extractor = backend.textExtractor(handle);
    REQUIRE(extractor != nullptr);
    PageTextModel model;
    REQUIRE(extractor->extractPage(handle, page, model));
    static const QRegularExpression pattern(QStringLiteral("(?<![A-Za-z0-9-])[A-Z][A-Z0-9]*-[0-9]{4}(?![A-Za-z0-9-])"));
    const QRegularExpressionMatch match = pattern.match(model.text());
    return match.hasMatch() ? match.captured(0).toStdString() : std::string("(none)");
}

std::string digestOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page)
{
    const core::Page* entry = backend.pages(handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfSize size = entry->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = 1.0;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::Annotations;
    request.tile = core::render::TileRect{0, 0, std::max(1, static_cast<int>(std::ceil(size.width))),
                                          std::max(1, static_cast<int>(std::ceil(size.height)))};
    auto image = backend.rasterizer(handle)->raster(request);
    REQUIRE(image);
    QByteArray bytes;
    bytes.append(QByteArray::number(image.value().width)).append('x').append(QByteArray::number(image.value().height)).append(':');
    for (int row = 0; row < image.value().height; ++row)
        bytes.append(image.value().pixels.constData() + static_cast<qsizetype>(row) * image.value().bytesPerLine, image.value().width * 4);
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

// The merged document, written and opened by the engine from its path -- with no repair -- and read first-party too.
struct Merged {
    QTemporaryDir scratch;
    pdf::DocumentBackendImpl backend;
    core::DocumentHandle handle;
    core::MergeReport report;
    QByteArray bytes;
    std::optional<pdf::cos::XrefTable> table;
    std::map<long long, int> indexOf;   // page object -> its place, by following /Kids from the root
    std::vector<std::string> identity;

    CosObject object(long long number)
    {
        auto fetched = table->fetch(number);
        REQUIRE(fetched);
        CosObject value = fetched.value().object;
        value.clearSpansRecursively();
        return value;
    }
    CosObject resolve(const CosObject& value) { return value.isReference() ? object(value.reference().number) : value; }
    int count() const { return static_cast<int>(identity.size()); }
};

std::unique_ptr<Merged> mergeAndOpen(const std::vector<std::string>& relatives, const core::MergeOptions& options)
{
    auto merged = std::make_unique<Merged>();
    std::vector<core::MergeInput> inputs;
    for (const std::string& relative : relatives) inputs.push_back(core::MergeInput{corpusPath(relative), {}});
    auto result = merged->backend.mergeFiles(inputs, options, {});
    INFO((result ? std::string() : std::string(result.error().messageKey.data()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    merged->report = result.value().report;
    merged->bytes = result.value().bytes;
    const QString path = merged->scratch.filePath(QStringLiteral("merged.pdf"));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(merged->bytes) == merged->bytes.size());
    file.close();
    auto handle = merged->backend.open(path, nullptr);
    REQUIRE(handle);
    merged->handle = handle.value();
    CHECK(merged->backend.repairReport(merged->handle)->isEmpty());
    for (int page = 0; page < merged->backend.pages(merged->handle)->count(); ++page)
        merged->identity.push_back(identityOf(merged->backend, merged->handle, page));

    auto opened = pdf::cos::openWithRepair(std::string_view(merged->bytes.constData(), static_cast<std::size_t>(merged->bytes.size())));
    REQUIRE(opened);
    merged->table.emplace(std::move(opened.value().table));
    const CosObject catalog = merged->object(merged->table->trailer().get("Root").reference().number);
    std::vector<long long> stack{catalog.get("Pages").reference().number};
    while (!stack.empty()) {
        const long long number = stack.back();
        stack.pop_back();
        const CosObject node = merged->object(number);
        if (!node.get("Kids").isArray()) {
            merged->indexOf.emplace(number, static_cast<int>(merged->indexOf.size()));
            continue;
        }
        const auto& kids = node.get("Kids").items();
        for (auto kid = kids.rbegin(); kid != kids.rend(); ++kid) stack.push_back(kid->reference().number);
    }
    REQUIRE(static_cast<int>(merged->indexOf.size()) == merged->count());
    return merged;
}

const std::vector<std::string> kSources{"assembly/merge-a.pdf", "assembly/merge-b.pdf", "assembly/merge-c.pdf"};
const std::vector<QString> kLabels{QStringLiteral("assembly-merge-a.json"), QStringLiteral("assembly-merge-b.json"),
                                   QStringLiteral("assembly-merge-c.json")};

} // namespace

TEST_CASE("merge.outline every bookmark lands on the page it names, by what that page shows", "[render][merge][merge.outline][A-66]")
{
    for (const core::MergeBookmarks mode : core::kMergeBookmarks) {
        const std::string modeName(core::mergeBookmarksName(mode));
        INFO(modeName);
        core::MergeOptions options;
        options.bookmarks = mode;
        auto merged = mergeAndOpen(kSources, options);
        const core::Outline* outline = merged->backend.outline(merged->handle);
        REQUIRE(outline != nullptr);
        int landed = 0;
        // A source bookmark: titled with the identity it targets (D7-17), so it lands where that identity shows.
        std::function<void(const std::vector<core::OutlineItem>&)> grade = [&](const std::vector<core::OutlineItem>& items) {
            for (const core::OutlineItem& item : items) {
                INFO(item.title.toStdString());
                CHECK(item.destination.pageIndex >= 0);
                if (item.destination.pageIndex >= 0) CHECK(merged->identity[static_cast<std::size_t>(item.destination.pageIndex)] == item.title.toStdString());
                ++landed;
                grade(item.children);
            }
        };
        if (mode == core::MergeBookmarks::None) {
            grade(outline->roots());
        } else {
            REQUIRE(outline->roots().size() == 3);
            for (std::size_t k = 0; k < 3; ++k) {
                const core::OutlineItem& file = outline->roots()[k];
                const QJsonObject source = label(kLabels[k]);
                const QString want = mode == core::MergeBookmarks::Title ? source.value(QStringLiteral("title")).toString()
                                                                         : QFileInfo(QString::fromStdString(kSources[k])).completeBaseName();
                CHECK(file.title == want);
                // ... landing on that file's FIRST page, by the identity it shows.
                REQUIRE(file.destination.pageIndex >= 0);
                CHECK(merged->identity[static_cast<std::size_t>(file.destination.pageIndex)]
                      == source.value(QStringLiteral("pages")).toArray().first().toObject().value(QStringLiteral("identity")).toString().toStdString());
                CHECK_FALSE(file.openByDefault);
                ++landed;
                grade(file.children);
            }
        }
        std::cout << "A-66 " << modeName << ": " << landed << " bookmarks landed by identity" << std::endl;
    }
}

TEST_CASE("merge.links links and comments come with their pages, by what the pages show", "[render][merge][merge.links][A-67]")
{
    std::vector<std::string> sources = kSources;
    sources.push_back("assembly/annots-on-pages.pdf");
    std::vector<QString> labels = kLabels;
    labels.push_back(QStringLiteral("assembly-annots-on-pages.json"));
    auto merged = mergeAndOpen(sources, core::MergeOptions{});

    // Every annotation of the merged document, by its page, as the file lists it.
    std::map<int, std::vector<std::pair<long long, CosObject>>> annotations;
    for (const auto& [number, index] : merged->indexOf) {
        for (const CosObject listed = merged->resolve(merged->object(number).get("Annots")); const CosObject& item : listed.items())
            annotations[index].emplace_back(item.reference().number, merged->resolve(item));
    }
    const auto pageShowing = [&](const std::string& identity) {
        const auto at = std::find(merged->identity.begin(), merged->identity.end(), identity);
        REQUIRE(at != merged->identity.end());
        return static_cast<int>(at - merged->identity.begin());
    };
    const auto subtypeOf = [](const CosObject& a) { return a.get("Subtype").isName() ? a.get("Subtype").text() : std::string(); };
    int links = 0;
    int comments = 0;
    for (std::size_t k = 0; k < sources.size(); ++k) {
        const QJsonObject source = label(labels[k]);
        const QJsonArray pages = source.value(QStringLiteral("pages")).toArray();
        // THE LINKS: each lands on the page showing its target. The merge-* labels list them by source page; the
        // annots-on-pages label per page, each a link to the next.
        std::vector<std::pair<std::string, std::string>> wanted;   // (the page it is on, the identity it targets)
        for (const QJsonValue value : source.value(QStringLiteral("links")).toArray()) {
            const QJsonObject link = value.toObject();
            wanted.emplace_back(pages[link.value(QStringLiteral("page")).toInt()].toObject().value(QStringLiteral("identity")).toString().toStdString(),
                                link.value(QStringLiteral("targetIdentity")).toString().toStdString());
        }
        for (const QJsonValue value : pages) {
            for (const QJsonValue annotation : value.toObject().value(QStringLiteral("annots")).toArray()) {
                if (annotation.toObject().value(QStringLiteral("role")).toString() == QLatin1String("link"))
                    wanted.emplace_back(value.toObject().value(QStringLiteral("identity")).toString().toStdString(),
                                        annotation.toObject().value(QStringLiteral("targetIdentity")).toString().toStdString());
            }
        }
        for (const auto& [on, target] : wanted) {
            INFO(on << " -> " << target);
            const int page = pageShowing(on);
            std::vector<std::string> landed;
            for (const auto& [number, annotation] : annotations[page]) {
                if (subtypeOf(annotation) != "Link") continue;
                const CosObject dest = annotation.has("Dest") ? merged->resolve(annotation.get("Dest"))
                                                              : merged->resolve(merged->resolve(annotation.get("A")).get("D"));
                REQUIRE(dest.isArray());
                const auto found = merged->indexOf.find(dest.items().front().reference().number);
                REQUIRE(found != merged->indexOf.end());
                landed.push_back(merged->identity[static_cast<std::size_t>(found->second)]);
            }
            CHECK(std::count(landed.begin(), landed.end(), target) == 1);
            ++links;
        }
        // THE COMMENTS: every one on the page showing its label's page, by its /NM; a thread whole on that page.
        std::vector<std::pair<std::string, std::string>> notes;   // (page identity, /NM)
        for (const QJsonValue value : source.value(QStringLiteral("comments")).toArray()) {
            const QJsonObject comment = value.toObject();
            const QString nm = QStringLiteral("merge-%1-note").arg(QChar(QLatin1Char('a' + static_cast<char>(k))));
            notes.emplace_back(comment.value(QStringLiteral("pageIdentity")).toString().toStdString(), nm.toStdString());
        }
        for (const QJsonValue value : pages) {
            for (const QJsonValue annotation : value.toObject().value(QStringLiteral("annots")).toArray()) {
                const QString nm = annotation.toObject().value(QStringLiteral("nm")).toString();
                if (!nm.isEmpty()) notes.emplace_back(value.toObject().value(QStringLiteral("identity")).toString().toStdString(), nm.toStdString());
            }
        }
        for (const auto& [on, nm] : notes) {
            INFO(on << " " << nm);
            const int page = pageShowing(on);
            std::set<long long> here;
            for (const auto& [number, annotation] : annotations[page]) here.insert(number);
            int found = 0;
            for (const auto& [number, annotation] : annotations[page]) {
                if (!annotation.get("NM").isString() || annotation.get("NM").text() != nm) continue;
                ++found;
                for (const char* key : {"IRT", "Popup"}) {
                    if (annotation.get(key).isReference()) CHECK(here.count(annotation.get(key).reference().number) == 1);
                }
            }
            CHECK(found == 1);
            ++comments;
        }
    }
    std::cout << "A-67: " << links << " links landed by identity, " << comments << " comments on their pages" << std::endl;
    CHECK(links == 3 + 4);
}

TEST_CASE("merge.fields PDFium's form layer reads every field as the merge reported it", "[render][merge][merge.fields][A-68]")
{
    const auto read = [](Merged& merged) {
        std::map<QString, std::multiset<QString>> fields;   // fully qualified name -> each widget's value
        for (int page = 0; page < merged.count(); ++page) {
            std::vector<core::text::SearchUnit> units;
            REQUIRE(merged.backend.searchUnits(merged.handle, page, units));
            for (const core::text::SearchUnit& unit : units) {
                if (unit.field == core::text::SearchUnit::Field::FieldValue) fields[unit.locator].insert(unit.text);
            }
        }
        return fields;
    };
    SECTION("renamed")
    {
        core::MergeOptions options;
        options.renameCollidingFields = true;
        auto merged = mergeAndOpen(kSources, options);
        const auto fields = read(*merged);
        std::map<QString, std::multiset<QString>> want{
            {QStringLiteral("name"), {QStringLiteral("Ana Lima")}},
            {QStringLiteral("date"), {QStringLiteral("2026-10-01")}},
            {QStringLiteral("a-only"), {QStringLiteral("first source")}},
            {QStringLiteral("name_2"), {QStringLiteral("Ben Okafor")}},
            {QStringLiteral("date_2"), {QStringLiteral("2026-10-02")}},
            {QStringLiteral("address.street"), {QStringLiteral("1 Long Road")}},
            {QStringLiteral("b-only"), {QStringLiteral("second source")}},
            {QStringLiteral("name_3"), {QStringLiteral("Chen Wei")}},
            {QStringLiteral("date_3"), {QStringLiteral("2026-10-03")}},
            {QStringLiteral("address_2.city"), {QStringLiteral("Taipei")}},
            {QStringLiteral("c-only"), {QStringLiteral("third source")}}};
        CHECK(fields == want);
        // Each name PDFium reads is one the report states: the renames are exactly the report's.
        std::set<QString> renamed;
        for (const core::FieldCollision& collision : merged->report.fields) renamed.insert(collision.renamedTo);
        CHECK(renamed == std::set<QString>{QStringLiteral("name_2"), QStringLiteral("date_2"), QStringLiteral("name_3"), QStringLiteral("date_3"),
                                           QStringLiteral("address_2")});
        // ... and every merged page draws as the page it came from, widgets and comments included.
        int compared = 0;
        int at = 0;
        for (const std::string& relative : kSources) {
            pdf::DocumentBackendImpl sourceBackend;
            auto source = sourceBackend.open(corpusPath(relative), nullptr);
            REQUIRE(source);
            for (int page = 0; page < sourceBackend.pages(source.value())->count(); ++page, ++at) {
                INFO(relative << " page " << page);
                CHECK(digestOf(merged->backend, merged->handle, at) == digestOf(sourceBackend, source.value(), page));
                ++compared;
            }
            (void)sourceBackend.close(source.value());
        }
        std::cout << "A-68 renamed through the engine: " << fields.size() << " fields, " << compared << " pages compared at 72 dpi, 0 differ"
                  << std::endl;
    }
    SECTION("merged on purpose")
    {
        core::MergeOptions options;
        options.renameCollidingFields = false;
        auto merged = mergeAndOpen(kSources, options);
        const auto fields = read(*merged);
        std::map<QString, std::multiset<QString>> want{
            {QStringLiteral("name"), {QStringLiteral("Ana Lima"), QStringLiteral("Ana Lima"), QStringLiteral("Ana Lima")}},
            {QStringLiteral("date"), {QStringLiteral("2026-10-01"), QStringLiteral("2026-10-01"), QStringLiteral("2026-10-01")}},
            {QStringLiteral("a-only"), {QStringLiteral("first source")}},
            {QStringLiteral("address.street"), {QStringLiteral("1 Long Road")}},
            {QStringLiteral("b-only"), {QStringLiteral("second source")}},
            {QStringLiteral("address.city"), {QStringLiteral("Taipei")}},
            {QStringLiteral("c-only"), {QStringLiteral("third source")}}};
        CHECK(fields == want);
        std::cout << "A-68 merged through the engine: " << fields.size() << " fields, one value per field" << std::endl;
    }
}
