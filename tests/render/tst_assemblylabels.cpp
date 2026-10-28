// tst_assemblylabels -- Stage 7's corpus, as the ENGINE reads it. Stage 7 T1, guide case A-15's engine
// half (`Stage7-Assembly-Testing-Guide.md` §5.3), decisions D7-17 and D7-18; and the regression case for
// Stage 7 guide I-01.
//
// WHY THE LABELS ARE CHECKED HERE AS WELL AS IN `corpus/fixturecheck.py`. Every later Stage 7 case grades
// a page by the identity PDFium extracts from it (the guide's rule 7). `--check-faults` proves each label
// describes the BYTES; only the engine can prove the identity a label names is the text a reader gets
// back from that page -- one Helvetica-Bold run of a tag, a hyphen and four digits, and no other token on
// the page that looks like one. A label that was true of the bytes and false of the extraction would make
// every later grade wrong in the same direction as the code under test.
//
// WHAT IT DOES NOT COVER (convention 19): the links, the structure tree, the threads, the labels and the
// open action of `refs-every-kind.pdf` are not read through the engine here -- the product has no engine
// reading of any of them yet (Stage 7 T2 and T12 add the first) -- so their targets are held to the label
// by `fixturecheck.py` alone. The outline and the named destinations ARE read here, through the shipped
// backend's PDFium reading (D4-3).

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/doc/Destinations.h"
#include "core/doc/Outline.h"
#include "core/doc/Permissions.h"

#include <QDir>
#include <QRegularExpression>

#include <map>
#include <string>
#include <vector>

using namespace rawpage::test_oracle;

namespace {

// The label count `corpus/make-assembly.py` writes. A fixed number rather than "whatever is there", so a
// fixture dropped from the generator is a failure here and not one fewer file looked at.
constexpr int kAssemblyLabels = 21;

std::vector<QString> assemblyLabels()
{
    const QDir labels(corpusPath("labels"));
    std::vector<QString> names;
    for (const QString& file : labels.entryList({QStringLiteral("assembly-*.json")}, QDir::Files, QDir::Name))
        names.push_back(file.chopped(5));
    return names;
}

// Every token on the page that reads as an identity, in the order extracted.
std::vector<std::string> identitiesOn(const PageTextModel& model)
{
    static const QRegularExpression pattern(QStringLiteral("(?<![A-Za-z0-9-])[A-Z][A-Z0-9]*-[0-9]{4}(?![A-Za-z0-9-])"));
    std::vector<std::string> found;
    auto matches = pattern.globalMatch(model.text());
    while (matches.hasNext())
        found.push_back(matches.next().captured(0).toStdString());
    return found;
}

// What the label says page `index` shows: its identity, or the identity drawn in white on the one page of
// `split-blanks.pdf` that is blank to the eye (the engine still extracts it).
std::vector<std::string> expectedOn(const QJsonObject& page)
{
    const QJsonValue drawn = page.contains(QStringLiteral("drawnIdentity")) ? page.value(QStringLiteral("drawnIdentity"))
                                                                            : page.value(QStringLiteral("identity"));
    if (drawn.isNull())
        return {};
    return {drawn.toString().toStdString()};
}

// The pages whose extracted identities differ from the label's, as "index: found / expected" -- empty when
// the label is true. Separate from the assertion so the control below can drive it with a false label.
std::vector<std::string> mismatches(const QJsonObject& label, const std::vector<std::vector<std::string>>& extracted)
{
    std::vector<std::string> out;
    const QJsonArray pages = label.value(QStringLiteral("pages")).toArray();
    for (qsizetype at = 0; at < pages.size(); ++at) {
        const std::vector<std::string> expected = expectedOn(pages.at(at).toObject());
        const std::vector<std::string>& found = extracted.at(static_cast<std::size_t>(at));
        if (found != expected) {
            std::string line = std::to_string(at) + ": found";
            for (const std::string& token : found)
                line += " " + token;
            line += " / expected";
            for (const std::string& token : expected)
                line += " " + token;
            out.push_back(line);
        }
    }
    return out;
}

std::string identityOfPage(const QJsonObject& label, int index)
{
    const QJsonArray pages = label.value(QStringLiteral("pages")).toArray();
    if (index < 0 || index >= pages.size())
        return "(no page " + std::to_string(index) + ")";
    const QJsonValue identity = pages.at(index).toObject().value(QStringLiteral("identity"));
    return identity.isNull() ? std::string("(no identity)") : identity.toString().toStdString();
}

} // namespace

TEST_CASE("assembly labels: every page shows PDFium the identity its label gives", "[render][assembly][A-15]")
{
    const std::vector<QString> names = assemblyLabels();
    REQUIRE(static_cast<int>(names.size()) == kAssemblyLabels);

    int pagesRead = 0;
    for (const QString& name : names) {
        const QJsonObject label = loadLabel(name.toStdString());
        const std::string file = label.value(QStringLiteral("file")).toString().toStdString();
        INFO(file);
        auto opened = openCorpus(file);
        const QJsonArray pages = label.value(QStringLiteral("pages")).toArray();
        REQUIRE(opened->pageCount() == pages.size());

        std::vector<std::vector<std::string>> extracted;
        for (int index = 0; index < opened->pageCount(); ++index)
            extracted.push_back(identitiesOn(extract(*opened, index)));
        const std::vector<std::string> wrong = mismatches(label, extracted);
        for (const std::string& line : wrong)
            UNSCOPED_INFO(line);
        CHECK(wrong.empty());
        pagesRead += opened->pageCount();

        // THE CONTROL, on the first file only (convention 7): the same extraction against a label naming a
        // wrong identity for one page must be reported. Without it, a `mismatches` that compared nothing
        // would pass every file above.
        if (name == names.front()) {
            QJsonObject planted = label;
            QJsonArray plantedPages = planted.value(QStringLiteral("pages")).toArray();
            QJsonObject first = plantedPages.at(0).toObject();
            first.insert(QStringLiteral("identity"), QStringLiteral("ZZ-9999"));
            plantedPages.replace(0, first);
            planted.insert(QStringLiteral("pages"), plantedPages);
            const std::vector<std::string> caught = mismatches(planted, extracted);
            REQUIRE(caught.size() == 1);
            CHECK(caught.front().rfind("0: found", 0) == 0);
        }
    }
    // 12 + 12 + 6 + 5 + 4 + 3 + 2 + 4 + 2 + 3 + 3 + 9 + 3 + 2 + (1 + 2 + 3 + 5 + 7 + 9 + 17).
    CHECK(pagesRead == 114);
}

TEST_CASE("assembly labels: the engine's outline and named destinations land on the pages labelled", "[render][assembly][A-15]")
{
    // The outline items and named destinations each label lists, resolved by PDFium (D4-3), and the page
    // each resolves to named by its IDENTITY -- the label's claim, held to a reader that is not ours.
    int itemsChecked = 0;
    int namesChecked = 0;
    for (const QString& name : assemblyLabels()) {
        const QJsonObject label = loadLabel(name.toStdString());
        if (!label.contains(QStringLiteral("outline")) && !label.contains(QStringLiteral("namedDestinations")))
            continue;
        const std::string file = label.value(QStringLiteral("file")).toString().toStdString();
        INFO(file);
        auto opened = openCorpus(file);

        if (label.contains(QStringLiteral("outline"))) {
            const rawpage::core::Outline* outline = opened->backend.outline(opened->handle);
            REQUIRE(outline != nullptr);
            // The two trees walked side by side: same titles in the same shape, and every item the label
            // gives a target lands on that target's page.
            std::vector<std::pair<const std::vector<rawpage::core::OutlineItem>*, QJsonArray>> stack{
                {&outline->roots(), label.value(QStringLiteral("outline")).toArray()}};
            while (!stack.empty()) {
                const auto [items, expected] = stack.back();
                stack.pop_back();
                REQUIRE(items->size() == static_cast<std::size_t>(expected.size()));
                for (qsizetype at = 0; at < expected.size(); ++at) {
                    const QJsonObject entry = expected.at(at).toObject();
                    const rawpage::core::OutlineItem& item = items->at(static_cast<std::size_t>(at));
                    const std::string title = entry.value(QStringLiteral("title")).toString().toStdString();
                    INFO("outline item " << title);
                    CHECK(item.title.toStdString() == title);
                    const QJsonValue target = entry.value(QStringLiteral("targetIdentity"));
                    if (target.isNull()) {
                        // `refs-by-index.pdf`'s integer destination: no target is claimed. What PDFium makes
                        // of it is recorded rather than graded -- a repair must leave it as written.
                        WARN(file << ": the engine resolves the outline item '" << title << "' to page index "
                                  << item.destination.pageIndex);
                    } else {
                        CHECK(identityOfPage(label, item.destination.pageIndex) == target.toString().toStdString());
                        ++itemsChecked;
                    }
                    stack.push_back({&item.children, entry.value(QStringLiteral("children")).toArray()});
                }
            }
        }

        for (const QJsonValue value : label.value(QStringLiteral("namedDestinations")).toArray()) {
            const QJsonObject entry = value.toObject();
            const QString destination = entry.value(QStringLiteral("name")).toString();
            INFO("named destination " << destination.toStdString());
            const rawpage::core::Destinations* destinations = opened->backend.destinations(opened->handle);
            REQUIRE(destinations != nullptr);
            const rawpage::core::NamedDestination* found = destinations->find(destination);
            REQUIRE(found != nullptr);
            CHECK(identityOfPage(label, found->destination.pageIndex)
                  == entry.value(QStringLiteral("targetIdentity")).toString().toStdString());
            ++namesChecked;
        }
    }
    // refs-every-kind's seven items and refs-by-index's control, merge-a/b/c's 3 + 2 + 4, split-blanks' 3;
    // refs-every-kind's five names.
    CHECK(itemsChecked == 7 + 1 + 3 + 2 + 4 + 3);
    CHECK(namesChecked == 5);
}

TEST_CASE("assembly labels: a page shows the boxes and rotation it inherits (I-01)", "[render][assembly][I-01]")
{
    // STAGE 7 GUIDE I-01's regression. `FPDFPage_GetMediaBox` and `FPDFPage_GetCropBox` read the page
    // dictionary alone, so until T1 a page inheriting its boxes from a /Pages node was laid out as US Letter,
    // uncropped, while PDFium drew it as the file says -- and the two readers' disagreement on its size
    // refused editing for the whole document. `inherit-resources.pdf` inherits both boxes on every page.
    //
    // Graded on the DRAWN box (media intersected with crop), which is what the label's resolved `cropBox`
    // is and what the page model must show. The media box of a page inheriting both is NOT graded: PDFium
    // exposes no inherited media box apart from the crop, and `pdf/render/PdfiumDocument.cpp` says so.
    const QJsonObject label = loadLabel("assembly-inherit-resources");
    auto opened = openCorpus("assembly/inherit-resources.pdf");
    const QJsonArray pages = label.value(QStringLiteral("pages")).toArray();
    REQUIRE(opened->pageCount() == pages.size());
    for (int index = 0; index < opened->pageCount(); ++index) {
        INFO("page " << index);
        const QJsonObject expected = pages.at(index).toObject();
        const QJsonArray crop = expected.value(QStringLiteral("cropBox")).toArray();
        const rawpage::core::Page& page = opened->page(index);
        const PdfRect drawn = rawpage::core::intersect(page.mediaBox(), page.cropBox());
        CHECK(std::abs(drawn.origin.x - crop.at(0).toDouble()) < 0.01);
        CHECK(std::abs(drawn.origin.y - crop.at(1).toDouble()) < 0.01);
        CHECK(std::abs(drawn.size.width - (crop.at(2).toDouble() - crop.at(0).toDouble())) < 0.01);
        CHECK(std::abs(drawn.size.height - (crop.at(3).toDouble() - crop.at(1).toDouble())) < 0.01);
        CHECK(static_cast<int>(page.rotation()) == expected.value(QStringLiteral("rotate")).toInt());
    }

    // The editing half: both readers now state the same drawn box, so the document is editable.
    const rawpage::core::Permissions* permissions = opened->backend.permissions(opened->handle);
    REQUIRE(permissions != nullptr);
    INFO("refusal: " << std::string(permissions->refusalKey().data(), static_cast<std::size_t>(permissions->refusalKey().size()))
                      << " " << permissions->refusalContext().toStdString());
    CHECK(permissions->canEditPages());

    // THE CONTROL (convention 22): a page that states its own boxes is unchanged by the fallback -- `boxes.pdf`'s
    // first page carries all five, and its media box is its own, not the drawn one.
    auto own = openCorpus("assembly/boxes.pdf");
    const rawpage::core::Page& first = own->page(0);
    CHECK(first.mediaBox().size.width == 612.0);
    CHECK(first.mediaBox().size.height == 792.0);
    CHECK(std::abs(first.cropBox().size.width - 576.0) < 0.01);
}
