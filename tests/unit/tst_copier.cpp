// tst_copier -- Stage 7 T5 (`Stage7-Assembly-Plan.md` §6, D7-4, D7-6): the copier's headless half, guide cases A-39 ...
// A-43 and A-45 (`Stage7-Assembly-Testing-Guide.md` §5.7), and an insert and a replace each saved, undone and redone.
//
// `pdf/ops/ObjectGraphCopier` copies pages with everything they reach into a payload that is a PDF of its own, and the
// sink takes its objects from that payload at every compose. Every check here reads the PAYLOAD or the SAVED FILE
// back through the first-party reader -- objects, references, keys -- against the SOURCE read the same way, never
// against what the copier computed. WHAT IT DOES NOT COVER (convention 19): how any of it DRAWS -- a copied page
// against its source, pixel for pixel, is `tests/render/tst_copierengine.cpp`'s (A-38), which is also where a link
// is followed to the page it shows.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "tests/fuzz/AssemblyFuzzBody.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/CopiedPages.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/RevisionWriter.h"

#include <QDir>
#include <QFile>

#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using pdf::cos::CosObject;

namespace {

core::CopiedPages copyOf(const char* relative, const std::vector<int>& pages)
{
    const QByteArray bytes = tests::readAllBytes(tests::corpusFile(relative));
    auto copied = pdf::ops::ObjectGraphCopier::copyFile(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                                                        pages);
    INFO(relative << ": " << (copied ? "" : copied.error().messageKey.data()));
    REQUIRE(copied);
    return std::move(copied).value();
}

// A file read back, first-party: its table over bytes it keeps, and its pages.
struct Read {
    QByteArray bytes;
    pdf::cos::XrefTable table;
    pdf::ops::PageWalk walk;

    CosObject object(long long number)
    {
        auto fetched = table.fetch(number);
        REQUIRE(fetched);
        return fetched.value().object;
    }
    CosObject resolved(const CosObject& value)
    {
        return value.isReference() ? object(value.reference().number) : value;
    }
    CosObject page(int index) { return object(walk.pages[static_cast<std::size_t>(index)].number); }
};

std::unique_ptr<Read> readBytes(QByteArray bytes)
{
    auto out = std::make_unique<Read>();
    out->bytes = std::move(bytes);
    auto opened = pdf::cos::openWithRepair(std::string_view(out->bytes.constData(), static_cast<std::size_t>(out->bytes.size())));
    REQUIRE(opened);
    out->table = std::move(opened.value().table);
    auto walk = pdf::ops::PageOps::walk(out->table);
    REQUIRE(walk);
    out->walk = std::move(walk).value();
    return out;
}

std::unique_ptr<Read> readCorpus(const char* relative)
{
    return readBytes(tests::readAllBytes(tests::corpusFile(relative)));
}

// A source page's value for an inheritable key: its own, else up its /Parent chain -- the reading the copier does NOT
// use (it walks down), so the two must agree independently.
std::optional<CosObject> inheritedUp(Read& file, int index, const char* key)
{
    CosObject at = file.page(index);
    for (int depth = 0; depth < 32; ++depth) {
        if (const CosObject* own = at.find(key)) return *own;
        const CosObject& parent = at.get("Parent");
        if (!parent.isReference()) return std::nullopt;
        at = file.object(parent.reference().number);
    }
    return std::nullopt;
}

std::vector<double> numbers(Read& file, const CosObject& value)
{
    std::vector<double> out;
    const CosObject array = file.resolved(value);
    for (const CosObject& item : array.items()) out.push_back(file.resolved(item).numberValue());
    return out;
}

// Every resource a page names, by category and name, resolved to the object it is: `Font/F1` -> 12.
std::map<std::string, long long> resourceObjects(Read& file, const CosObject& resources)
{
    std::map<std::string, long long> out;
    const CosObject dictionary = file.resolved(resources);
    for (const CosObject::Entry& category : dictionary.entries()) {
        const CosObject inner = file.resolved(category.value);
        if (!inner.isDictionary()) continue;
        for (const CosObject::Entry& entry : inner.entries()) {
            if (entry.value.isReference()) out[category.key + "/" + entry.key] = entry.value.reference().number;
        }
    }
    return out;
}

std::string typeOf(const CosObject& object)
{
    const CosObject& type = object.get("Type");
    return type.isName() ? type.text() : std::string();
}

std::string subtypeOf(const CosObject& object)
{
    const CosObject& subtype = object.get("Subtype");
    return subtype.isName() ? subtype.text() : std::string();
}

int countKind(const core::PageCommandReport& report, core::PageReference::Kind kind)
{
    int n = 0;
    for (const core::PageReference& reference : report.references) n += reference.kind == kind ? 1 : 0;
    return n;
}

// Saved through the coordinator and KEPT in RAWPAGE_WRITTEN_DIR as `copies-<line>.pdf`, where the `portable-asan` job's
// `check-qpdf.py` step reads every file the suite writes (Stage 7 guide A-25, Stage 6's A-31 mechanism).
bool saveKept(tests::SaveSession& session, int line)
{
    if (!session.coordinator->save(*session.journal, session.path)) return false;
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    if (!QDir().mkpath(dir)) return false;
    const QString target = QDir(dir).filePath(QStringLiteral("copies-%1.pdf").arg(line));   // the call's line: one name per save, across processes
    QFile::remove(target);
    return QFile::copy(session.path, target);
}

} // namespace

// ==========================================================================================
// A-39 -- what a source shares, the copy shares
// ==========================================================================================

TEST_CASE("copier shared: what two pages share in the source they share in the copy", "[copier][shared]")
{
    for (const char* relative : {"assembly/inherit-resources.pdf", "ordinary/pages-900.pdf"}) {
        auto source = readCorpus(relative);
        std::vector<int> all;
        for (int i = 0; i < static_cast<int>(source->walk.pages.size()); ++i) all.push_back(i);
        const core::CopiedPages copied = copyOf(relative, all);
        auto copy = readBytes(copied.graph);
        REQUIRE(copy->walk.pages.size() == source->walk.pages.size());

        // For every pair of pages, a resource is the same object in the copy exactly when it is in the source.
        std::vector<std::map<std::string, long long>> before;
        std::vector<std::map<std::string, long long>> after;
        for (int i = 0; i < static_cast<int>(all.size()); ++i) {
            const std::optional<CosObject> resources = inheritedUp(*source, i, "Resources");
            before.push_back(resources ? resourceObjects(*source, *resources) : std::map<std::string, long long>{});
            after.push_back(resourceObjects(*copy, copy->page(i).get("Resources")));
            INFO(relative << " page " << i);
            REQUIRE(before.back().size() == after.back().size());
        }
        int shared = 0;
        for (std::size_t a = 0; a < all.size(); ++a) {
            for (std::size_t b = a + 1; b < all.size() && b < a + 40; ++b) {
                for (const auto& [name, object] : before[a]) {
                    const auto other = before[b].find(name);
                    if (other == before[b].end()) continue;
                    INFO(relative << " pages " << a << " and " << b << ", " << name);
                    CHECK((object == other->second) == (after[a].at(name) == after[b].at(name)));
                    shared += object == other->second ? 1 : 0;
                }
            }
        }
        CHECK(shared > 0);   // the fixture's claim, and the reason it is the fixture: something IS shared

        // No object twice: the copy holds no more fonts and images than the source has.
        const auto census = [](Read& file) {
            std::map<std::string, int> out;
            for (const auto& [number, entry] : file.table.entries()) {
                if (entry.type == pdf::cos::XrefEntryType::Free || number == 0) continue;
                auto fetched = file.table.fetch(number);
                if (!fetched || !fetched.value().object.isDictionary()) continue;
                const CosObject& o = fetched.value().object;
                if (typeOf(o) == "Font") ++out["font"];
                if (subtypeOf(o) == "Image") ++out["image"];
            }
            return out;
        };
        const auto sourceCensus = census(*source);
        const auto copyCensus = census(*copy);
        for (const auto& [kind, count] : copyCensus) {
            INFO(relative << " " << kind << " in the copy: " << count);
            CHECK(count <= sourceCensus.at(kind));
        }
        std::cout << "copier shared: " << relative << " -- " << all.size() << " pages, " << copy->table.entryCount()
                  << " objects in the copy, " << shared << " shared resource pairs, " << copied.graph.size() << " bytes\n";
    }
}

// ==========================================================================================
// A-40 -- inherited attributes are materialized
// ==========================================================================================

TEST_CASE("copier inherit: every copied page carries its four inheritable keys", "[copier][inherit]")
{
    int inheritedSomething = 0;
    for (const char* relative : {"assembly/inherit-resources.pdf", "ordinary/rotate-inherited.pdf"}) {
        auto source = readCorpus(relative);
        std::vector<int> all;
        for (int i = 0; i < static_cast<int>(source->walk.pages.size()); ++i) all.push_back(i);
        auto copy = readBytes(copyOf(relative, all).graph);
        for (int i = 0; i < static_cast<int>(all.size()); ++i) {
            INFO(relative << " page " << i);
            const CosObject sourcePage = source->page(i);
            const CosObject page = copy->page(i);
            REQUIRE(page.has("Resources"));
            REQUIRE(page.has("MediaBox"));
            REQUIRE(page.has("Rotate"));
            for (const char* key : {"Resources", "MediaBox", "CropBox", "Rotate"}) inheritedSomething += sourcePage.has(key) ? 0 : 1;
            const pdf::ops::PageEntry& entry = source->walk.pages[static_cast<std::size_t>(i)];
            CHECK(copy->resolved(page.get("Rotate")).integerValue() == entry.effectiveRotate());
            REQUIRE(entry.mediaBox);
            CHECK(numbers(*copy, page.get("MediaBox")) == std::vector<double>(entry.mediaBox->begin(), entry.mediaBox->end()));
            CHECK(page.has("CropBox") == entry.cropBox.has_value());
            if (entry.cropBox)
                CHECK(numbers(*copy, page.get("CropBox")) == std::vector<double>(entry.cropBox->begin(), entry.cropBox->end()));
            const std::optional<CosObject> resources = inheritedUp(*source, i, "Resources");
            REQUIRE(resources);
            std::set<std::string> want;
            for (const auto& [name, object] : resourceObjects(*source, *resources)) want.insert(name);
            std::set<std::string> got;
            for (const auto& [name, object] : resourceObjects(*copy, page.get("Resources"))) got.insert(name);
            CHECK(got == want);
            CHECK_FALSE(page.has("StructParents"));
        }
    }
    CHECK(inheritedSomething > 0);   // the control: these fixtures' pages DO inherit, or this case proves nothing
}

// ==========================================================================================
// A-41 -- the copy stops at the page tree, and at its budget
// ==========================================================================================

TEST_CASE("copier bounds: no page tree, no catalog, no page left behind, and a budget refused by key", "[copier][bounds]")
{
    const core::CopiedPages copied = copyOf("assembly/refs-every-kind.pdf", {3});
    auto copy = readBytes(copied.graph);
    REQUIRE(copy->walk.pages.size() == 1);
    int catalogs = 0;
    int nodes = 0;
    int pages = 0;
    for (const auto& [number, entry] : copy->table.entries()) {
        if (entry.type == pdf::cos::XrefEntryType::Free || number == 0) continue;
        const CosObject o = copy->object(number);
        catalogs += typeOf(o) == "Catalog" ? 1 : 0;
        nodes += typeOf(o) == "Pages" ? 1 : 0;
        pages += typeOf(o) == "Page" ? 1 : 0;
        CHECK_FALSE(o.has("Outlines"));
        CHECK_FALSE(o.has("StructTreeRoot"));
        CHECK_FALSE(o.has("StructParent"));
    }
    // The payload's own catalog and node, and the one page: nothing of the source's tree came along.
    CHECK(catalogs == 1);
    CHECK(nodes == 1);
    CHECK(pages == 1);
    // The source has hundreds of objects; a copy that followed /Parent would hold most of them.
    auto source = readCorpus("assembly/refs-every-kind.pdf");
    CHECK(copy->table.entryCount() * 4 < source->table.entryCount());

    // A page whose RESOURCES name the catalog, the tree and another page -- what no corpus file does, and what a copy
    // that followed every reference would bring along whole.
    {
        tests::TinyPdf pdf;
        const int catalog = pdf.reserve();
        const int root = pdf.reserve();
        const int page = pdf.reserve();
        const int other = pdf.reserve();
        pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(root) + " 0 R >>");
        pdf.set(root, "<< /Type /Pages /Kids [" + std::to_string(page) + " 0 R " + std::to_string(other) + " 0 R] /Count 2 /MediaBox [0 0 200 200] >>");
        pdf.set(page, "<< /Type /Page /Parent " + std::to_string(root) + " 0 R /Resources << /Root " + std::to_string(catalog)
                          + " 0 R /Tree " + std::to_string(root) + " 0 R /Other " + std::to_string(other) + " 0 R >> >>");
        pdf.set(other, "<< /Type /Page /Parent " + std::to_string(root) + " 0 R >>");
        const std::string bytes = pdf.bytes(catalog);
        auto hostile = pdf::ops::ObjectGraphCopier::copyFile(bytes, {0});
        REQUIRE(hostile);
        auto read = readBytes(hostile.value().graph);
        int types[3] = {0, 0, 0};
        for (const auto& [number, entry] : read->table.entries()) {
            if (entry.type == pdf::cos::XrefEntryType::Free || number == 0) continue;
            const std::string type = typeOf(read->object(number));
            types[0] += type == "Catalog" ? 1 : 0;
            types[1] += type == "Pages" ? 1 : 0;
            types[2] += type == "Page" ? 1 : 0;
        }
        CHECK(types[0] == 1);
        CHECK(types[1] == 1);
        CHECK(types[2] == 1);
        const CosObject resources = read->resolved(read->page(0).get("Resources"));
        for (const char* key : {"Root", "Tree", "Other"}) CHECK(resources.get(key).isNull());
    }

    const QByteArray bytes = tests::readAllBytes(tests::corpusFile("ordinary/pages-900.pdf"));
    auto opened = pdf::cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    REQUIRE(opened);
    auto walk = pdf::ops::PageOps::walk(opened.value().table);
    REQUIRE(walk);
    pdf::ops::ObjectGraphCopier::Options small;
    small.maxObjects = 5;
    std::vector<int> ten;
    for (int i = 0; i < 10; ++i) ten.push_back(i);
    auto refused = pdf::ops::ObjectGraphCopier::copy(opened.value().table, walk.value(), ten, small);
    REQUIRE_FALSE(refused);
    CHECK(std::string(refused.error().messageKey.data()) == "error.copy.tooLarge");
    // ... and the control: the same pages within the default budget.
    CHECK(pdf::ops::ObjectGraphCopier::copy(opened.value().table, walk.value(), ten));
    // A set that is not one.
    for (const std::vector<int>& bad : {std::vector<int>{}, std::vector<int>{3, 2}, std::vector<int>{900}, std::vector<int>{-1}}) {
        auto no = pdf::ops::ObjectGraphCopier::copy(opened.value().table, walk.value(), bad);
        REQUIRE_FALSE(no);
        CHECK(std::string(no.error().messageKey.data()) == "error.pdf.pageOutOfRange");
    }
}

// ==========================================================================================
// A-42 -- annotations come with their page, and only what can follow them
// ==========================================================================================

TEST_CASE("copier annots: a page's annotations come with it, a widget and a link to a page left behind do not", "[copier][annots]")
{
    auto source = readCorpus("assembly/annots-on-pages.pdf");
    const core::CopiedPages copied = copyOf("assembly/annots-on-pages.pdf", {0, 1});
    auto copy = readBytes(copied.graph);
    REQUIRE(copy->walk.pages.size() == 2);
    const long long first = copy->walk.pages[0].number;
    const long long second = copy->walk.pages[1].number;

    // Each page's annotations, by role.
    for (int index : {0, 1}) {
        INFO("page " << index);
        const CosObject page = copy->page(index);
        const CosObject annots = copy->resolved(page.get("Annots"));
        std::map<std::string, std::vector<CosObject>> bySubtype;
        std::map<long long, std::string> roleOf;
        for (const CosObject& item : annots.items()) {
            REQUIRE(item.isReference());   // every copied annotation is an object of its own
            const CosObject a = copy->object(item.reference().number);
            bySubtype[subtypeOf(a)].push_back(a);
            roleOf[item.reference().number] = subtypeOf(a);
            REQUIRE(a.get("P").isReference());
            CHECK(a.get("P").reference().number == copy->walk.pages[static_cast<std::size_t>(index)].number);
            CHECK_FALSE(a.has("StructParent"));
        }
        CHECK(bySubtype.count("Widget") == 0);   // left with its field (the user's choice for T5)
        REQUIRE(bySubtype["Popup"].size() == 1);
        REQUIRE(bySubtype["Link"].size() == 1);
        // The note and its pop-up name each other; the reply names the note.
        const CosObject& popup = bySubtype["Popup"].front();
        REQUIRE(popup.get("Parent").isReference());
        CHECK(roleOf.at(popup.get("Parent").reference().number) == "Text");
        int replies = 0;
        for (const CosObject& note : bySubtype["Text"]) {
            if (!note.has("IRT")) {
                REQUIRE(note.get("Popup").isReference());
                CHECK(roleOf.at(note.get("Popup").reference().number) == "Popup");
                continue;
            }
            ++replies;
            REQUIRE(note.get("IRT").isReference());
            CHECK(roleOf.at(note.get("IRT").reference().number) == "Text");
        }
        CHECK(replies == 1);
        // Page 0's link goes to page 1, copied: it points at the COPY. Page 1's goes to page 2, left behind: it keeps its
        // rectangle and border, with no action at all.
        const CosObject& link = bySubtype["Link"].front();
        CHECK(link.has("Rect"));
        if (index == 0) {
            CosObject dest = link.has("Dest") ? copy->resolved(link.get("Dest")) : copy->resolved(copy->resolved(link.get("A")).get("D"));
            REQUIRE(dest.isArray());
            REQUIRE(dest.items().front().isReference());
            CHECK(dest.items().front().reference().number == second);
        } else {
            CHECK_FALSE(link.has("Dest"));
            CHECK_FALSE(link.has("A"));
        }
    }
    CHECK(first != second);
    CHECK(countKind(copied.report, core::PageReference::Kind::WidgetNotCopied) == 2);
    CHECK(countKind(copied.report, core::PageReference::Kind::Link) == 1);
    for (const core::PageReference& r : copied.report.references) {
        if (r.kind == core::PageReference::Kind::Link) {
            CHECK(r.treatment == core::PageReference::Treatment::Cleared);
            CHECK(r.page == 1);
        }
    }
    // The control: page 1 copied with page 2, its link is kept.
    const core::CopiedPages both = copyOf("assembly/annots-on-pages.pdf", {1, 2});
    CHECK(countKind(both.report, core::PageReference::Kind::Link) == 1);   // page 2's, to page 3 -- not page 1's
    (void)source;
}

// ==========================================================================================
// A-43 -- an encrypted source is refused
// ==========================================================================================

TEST_CASE("copier encrypted: an encrypted source is refused by key and nothing is copied", "[copier][encrypted]")
{
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(RAWPAGE_CORPUS_DIR) / "encrypted")) {
        if (entry.path().extension() != ".pdf") continue;
        ++files;
        const QByteArray bytes = tests::readAllBytes(QString::fromStdString(entry.path().string()));
        auto copied = pdf::ops::ObjectGraphCopier::copyFile(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), {0});
        INFO(entry.path().filename().string());
        REQUIRE_FALSE(copied);
        CHECK(std::string(copied.error().messageKey.data()) == "error.pdf.encrypted");
    }
    CHECK(files >= 1);
}

// ==========================================================================================
// A-45 -- blank pages of a chosen size
// ==========================================================================================

TEST_CASE("pageops blank: blank pages are inserted at the size given, with no content", "[pageops][blank][copier]")
{
    auto session = tests::openCopy("assembly/refs-none.pdf");
    REQUIRE(session);
    const int count = session->journal->document().pages().count();
    auto blank = session->backend.blankPages(core::PdfSize{595.0, 842.0}, 2);
    REQUIRE(blank);
    REQUIRE(session->journal->run(std::make_unique<core::InsertPagesCommand>(1, blank.value())));
    REQUIRE(session->journal->document().pages().count() == count + 2);
    CHECK(session->journal->document().pages().at(1)->displaySize() == core::PdfSize{595.0, 842.0});
    REQUIRE(saveKept(*session, __LINE__));
    auto saved = readBytes(tests::readAllBytes(session->path));
    REQUIRE(static_cast<int>(saved->walk.pages.size()) == count + 2);
    for (int index : {1, 2}) {
        const CosObject page = saved->page(index);
        CHECK(numbers(*saved, page.get("MediaBox")) == std::vector<double>{0, 0, 595, 842});
        CHECK_FALSE(page.has("Contents"));
        CHECK(saved->resolved(page.get("Resources")).entries().empty());
    }
    // Refused for a size no page may have (ISO 32000-1 Annex C).
    for (const core::PdfSize bad : {core::PdfSize{2.0, 842.0}, core::PdfSize{595.0, 14401.0}}) {
        auto no = session->backend.blankPages(bad, 1);
        REQUIRE_FALSE(no);
        CHECK(std::string(no.error().messageKey.data()) == "error.pdf.pageSizeOutOfRange");
    }
    CHECK_FALSE(session->backend.blankPages(core::PdfSize{595.0, 842.0}, 0));
}

// ==========================================================================================
// Insert and replace, saved, undone and redone -- the same objects, and an undo exact on either side of a save
// ==========================================================================================

TEST_CASE("pageops insert: an insert saved, undone and redone writes the same pages", "[pageops][insert][revert:InsertPagesCommand]")
{
    auto session = tests::openCopy("assembly/refs-none.pdf");
    REQUIRE(session);
    const int count = session->journal->document().pages().count();
    const core::CopiedPages copied = copyOf("assembly/annots-on-pages.pdf", {0, 1});
    auto command = std::make_unique<core::InsertPagesCommand>(1, copied);
    const QString key = command->key();
    REQUIRE(session->journal->run(std::move(command)));

    // The model: two pages more, each with its copied annotations (no widget), named from the key.
    const core::Document& document = session->journal->document();
    REQUIRE(document.pages().count() == count + 2);
    for (int index : {1, 2}) {
        const std::vector<std::size_t> owned = document.annotations().onPage(index);
        CHECK(owned.size() == 4);   // a note, its pop-up, a reply and a link
        for (const std::size_t at : owned) {
            CHECK(document.annotations().at(at).id.kind == core::AnnotationId::Kind::Created);
            CHECK(document.annotations().at(at).nm.startsWith(key + QLatin1Char('-')));
        }
    }

    REQUIRE(saveKept(*session, __LINE__));
    auto first = readBytes(tests::readAllBytes(session->path));
    REQUIRE(static_cast<int>(first->walk.pages.size()) == count + 2);
    const long long one = first->walk.pages[1].number;
    const long long two = first->walk.pages[2].number;
    for (int index : {1, 2}) {
        const CosObject annots = first->resolved(first->page(index).get("Annots"));
        CHECK(annots.items().size() == 4);
    }
    // The link on the first inserted page goes to the second -- in THIS file.
    {
        const CosObject annots = first->resolved(first->page(1).get("Annots"));
        bool found = false;
        for (const CosObject& item : annots.items()) {
            const CosObject a = first->resolved(item);
            if (subtypeOf(a) != "Link") continue;
            const CosObject dest = a.has("Dest") ? first->resolved(a.get("Dest")) : first->resolved(first->resolved(a.get("A")).get("D"));
            REQUIRE(dest.isArray());
            CHECK(dest.items().front().reference().number == two);
            found = true;
        }
        CHECK(found);
    }

    REQUIRE(session->journal->undo());
    REQUIRE(saveKept(*session, __LINE__));
    auto undone = readBytes(tests::readAllBytes(session->path));
    REQUIRE(static_cast<int>(undone->walk.pages.size()) == count);
    for (const pdf::ops::PageEntry& page : undone->walk.pages) {
        CHECK(page.number != one);
        CHECK(page.number != two);
    }

    REQUIRE(session->journal->redo());
    REQUIRE(saveKept(*session, __LINE__));
    auto redone = readBytes(tests::readAllBytes(session->path));
    REQUIRE(static_cast<int>(redone->walk.pages.size()) == count + 2);
    CHECK(redone->walk.pages[1].number == one);   // THE SAME pages, not new ones
    CHECK(redone->walk.pages[2].number == two);
    CHECK(redone->resolved(redone->page(1).get("Annots")).items().size() == 4);

    // And an undo BEFORE a save writes nothing: the document as saved, then an insert and its undo.
    REQUIRE(session->journal->run(std::make_unique<core::InsertPagesCommand>(0, copied)));
    REQUIRE(session->journal->undo());
    auto delta = session->sink()->delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());
}

TEST_CASE("pageops replace: a replaced page keeps its object, and an undo after a save writes it back", "[pageops][replace][revert:ReplacePagesCommand]")
{
    auto session = tests::openCopy("assembly/refs-every-kind.pdf");
    REQUIRE(session);
    auto original = readBytes(session->openedBytes);
    const int count = session->journal->document().pages().count();
    const long long number = original->walk.pages[3].number;
    const CosObject before = original->page(3);
    const std::size_t ownAnnotations = session->journal->document().annotations().listing(3).size();

    const core::CopiedPages copied = copyOf("assembly/annots-on-pages.pdf", {2});
    REQUIRE(session->journal->run(std::make_unique<core::ReplacePagesCommand>(std::vector<int>{3}, copied)));
    REQUIRE(session->journal->document().pages().count() == count);
    CHECK(session->journal->document().annotations().listing(3).size() == ownAnnotations + 4);
    REQUIRE(saveKept(*session, __LINE__));

    auto replaced = readBytes(tests::readAllBytes(session->path));
    REQUIRE(static_cast<int>(replaced->walk.pages.size()) == count);
    CHECK(replaced->walk.pages[3].number == number);   // the page OBJECT kept: what names it still lands on it
    const CosObject now = replaced->page(3);
    auto source = readCorpus("assembly/annots-on-pages.pdf");
    CHECK(numbers(*replaced, now.get("MediaBox")) == numbers(*source, source->page(2).get("MediaBox")));
    CHECK(now.get("Contents") != before.get("Contents"));
    CHECK_FALSE(now.has("StructParents"));
    CHECK(replaced->resolved(now.get("Annots")).items().size() == ownAnnotations + 4);
    // Every other page is the page it was.
    for (int i = 0; i < count; ++i) CHECK(replaced->walk.pages[static_cast<std::size_t>(i)].number == original->walk.pages[static_cast<std::size_t>(i)].number);

    REQUIRE(session->journal->undo());
    REQUIRE(saveKept(*session, __LINE__));
    auto undone = readBytes(tests::readAllBytes(session->path));
    CosObject back = undone->page(3);
    CosObject want = before;
    back.clearSpansRecursively();
    want.clearSpansRecursively();
    INFO("back: " << pdf::cos::serializeObject(back) << "\nwant: " << pdf::cos::serializeObject(want));
    CHECK(back == want);   // the dictionary it had, value for value
    CHECK(session->journal->document().annotations().listing(3).size() == ownAnnotations);

    REQUIRE(session->journal->redo());
    REQUIRE(saveKept(*session, __LINE__));
    auto redone = readBytes(tests::readAllBytes(session->path));
    CosObject again = redone->page(3);
    CosObject first = now;
    again.clearSpansRecursively();
    first.clearSpansRecursively();
    CHECK(again == first);   // the same replacement, the same objects

    // A page no save has written is not replaced: it is deleted and inserted.
    REQUIRE(session->journal->run(std::make_unique<core::InsertPagesCommand>(0, copied)));
    const core::Status refused = session->journal->run(std::make_unique<core::ReplacePagesCommand>(std::vector<int>{0}, copied));
    REQUIRE_FALSE(refused);
    const bool byKey = refused.error().context.contains(QLatin1String("error.edit.pageNotSaved"))
                       || std::string(refused.error().messageKey.data()) == "error.edit.pageNotSaved";
    INFO(refused.error().messageKey.data() << " " << refused.error().context.toStdString());
    CHECK(byKey);
    // And the payload must hold as many pages as it replaces.
    const core::Status mismatched = session->journal->run(std::make_unique<core::ReplacePagesCommand>(std::vector<int>{4, 5}, copied));
    REQUIRE_FALSE(mismatched);
}

// ==========================================================================================
// A-46 -- fuzz_assembly's body, replayed here over the corpus and over hostile graphs
// ==========================================================================================

TEST_CASE("fuzz.assembly_replay: the copier and the insert over the corpus, its cuts and hostile graphs", "[copier][fuzz]")
{
    // WHAT IT DOES NOT COVER (convention 19): coverage-guided search, and the sanitizers -- libFuzzer and ASan are
    // the Linux jobs' (`portable-fuzz`, `portable-asan`), whose logs are read for A-46. This is the same BODY.
    std::vector<std::pair<std::string, std::string>> inputs;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        const QByteArray bytes = tests::readAllBytes(QString::fromStdString(entry.path().string()));
        const std::string relative = std::filesystem::relative(entry.path(), RAWPAGE_CORPUS_DIR).generic_string();
        inputs.emplace_back(relative, std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        if (relative.rfind("assembly/", 0) == 0) {   // and its cuts: a file that stops part-way, damaged where it stops
            for (const std::size_t fraction : {2u, 3u, 5u}) {
                const std::string& whole = inputs.back().second;
                inputs.emplace_back(relative + " cut at 1/" + std::to_string(fraction), whole.substr(0, whole.size() / fraction));
            }
        }
    }
    // HOSTILE GRAPHS (D7-23): a /Resources naming the page tree and the catalog; an inheritance chain three nodes deep
    // whose middle node is listed twice; a page whose content names itself.
    {
        tests::TinyPdf pdf;
        const int catalog = pdf.reserve();
        const int root = pdf.reserve();
        const int page = pdf.reserve();
        pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(root) + " 0 R >>");
        pdf.set(root, "<< /Type /Pages /Kids [" + std::to_string(page) + " 0 R] /Count 1 /MediaBox [0 0 200 200] >>");
        pdf.set(page, "<< /Type /Page /Parent " + std::to_string(root) + " 0 R /Resources << /Root " + std::to_string(catalog)
                          + " 0 R /Tree " + std::to_string(root) + " 0 R /Self " + std::to_string(page) + " 0 R >> >>");
        inputs.emplace_back("hostile: resources naming the tree", pdf.bytes(catalog));
    }
    {
        tests::TinyPdf pdf;
        const int catalog = pdf.reserve();
        const int root = pdf.reserve();
        const int middle = pdf.reserve();
        const int page = pdf.reserve();
        pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(root) + " 0 R >>");
        pdf.set(root, "<< /Type /Pages /Kids [" + std::to_string(middle) + " 0 R " + std::to_string(middle) + " 0 R] /Count 2 /Rotate 90 >>");
        pdf.set(middle, "<< /Type /Pages /Parent " + std::to_string(root) + " 0 R /Kids [" + std::to_string(page)
                            + " 0 R] /Count 1 /MediaBox [0 0 300 300] >>");
        pdf.set(page, "<< /Type /Page /Parent " + std::to_string(middle) + " 0 R >>");
        inputs.emplace_back("hostile: a node listed twice", pdf.bytes(catalog));
    }
    {
        tests::TinyPdf pdf;
        const int catalog = pdf.reserve();
        const int root = pdf.reserve();
        const int page = pdf.reserve();
        const int content = pdf.reserve();
        pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(root) + " 0 R >>");
        pdf.set(root, "<< /Type /Pages /Kids [" + std::to_string(page) + " 0 R] /Count 1 >>");
        pdf.set(page, "<< /Type /Page /Parent " + std::to_string(root) + " 0 R /MediaBox [0 0 100 100] /Contents "
                          + std::to_string(content) + " 0 R >>");
        pdf.set(content, "<< /Length 5 /Loop " + std::to_string(content) + " 0 R /Page " + std::to_string(page) + " 0 R >>\nstream\nq Q\n\nendstream");
        inputs.emplace_back("hostile: content naming itself and its page", pdf.bytes(catalog));
    }

    int copied = 0;
    for (const auto& [label, bytes] : inputs) {
        const rawpage::fuzz::AssemblyVerdict verdict =
            rawpage::fuzz::assemblyFuzzOnce(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        INFO(label << ": " << verdict.failure);
        CHECK(verdict.ok);
        copied += verdict.copied ? 1 : 0;
    }
    std::cout << "fuzz.assembly_replay: " << inputs.size() << " inputs, " << copied << " reached the copy\n";
    CHECK(copied > static_cast<int>(inputs.size()) / 2);   // a body that copies nothing passes every input
}

// ==========================================================================================
// Stage 7 guide I-07 -- an inserted page's hidden layers stay hidden
// ==========================================================================================

TEST_CASE("pageops layers: the groups an inserted page uses join the document, hidden ones hidden, and leave on undo", "[pageops][insert][layers][I-07]")
{
    const auto groupsOf = [](Read& file) {
        CosObject catalog = file.object(file.table.trailer().get("Root").reference().number);
        std::pair<int, int> out{-1, -1};   // (groups, hidden); -1: no /OCProperties at all
        if (!catalog.has("OCProperties")) return out;
        const CosObject properties = file.resolved(catalog.get("OCProperties"));
        const CosObject defaults = file.resolved(properties.get("D"));
        out.first = static_cast<int>(file.resolved(properties.get("OCGs")).items().size());
        out.second = static_cast<int>(file.resolved(defaults.get("OFF")).items().size());
        return out;
    };
    // `layers.pdf` page 2 draws through four groups -- `en`, `fr`, `pr`, `sc` -- of which `fr` and `pr` are off.
    const core::CopiedPages copied = copyOf("assembly/layers.pdf", {1});
    {
        auto payload = readBytes(copied.graph);
        CHECK(groupsOf(*payload) == std::pair<int, int>{4, 2});
    }
    // Into a document with no optional content: the four arrive, the two still off.
    auto session = tests::openCopy("assembly/refs-none.pdf");
    REQUIRE(session);
    REQUIRE(session->journal->run(std::make_unique<core::InsertPagesCommand>(0, copied)));
    REQUIRE(saveKept(*session, __LINE__));
    {
        auto saved = readBytes(tests::readAllBytes(session->path));
        CHECK(groupsOf(*saved) == std::pair<int, int>{4, 2});
    }
    REQUIRE(session->journal->undo());
    REQUIRE(saveKept(*session, __LINE__));
    {
        auto saved = readBytes(tests::readAllBytes(session->path));
        CHECK(groupsOf(*saved) == std::pair<int, int>{-1, -1});   // no optional content left, as there was none
    }
    REQUIRE(session->journal->redo());
    REQUIRE(saveKept(*session, __LINE__));
    {
        auto saved = readBytes(tests::readAllBytes(session->path));
        CHECK(groupsOf(*saved) == std::pair<int, int>{4, 2});
    }
    // Into a document that has its own: united, its own kept.
    auto layered = tests::openCopy("assembly/layers.pdf");
    REQUIRE(layered);
    {
        auto before = readBytes(layered->openedBytes);
        REQUIRE(groupsOf(*before) == std::pair<int, int>{8, 2});
    }
    REQUIRE(layered->journal->run(std::make_unique<core::InsertPagesCommand>(2, copied)));
    REQUIRE(saveKept(*layered, __LINE__));
    auto united = readBytes(tests::readAllBytes(layered->path));
    CHECK(groupsOf(*united) == std::pair<int, int>{12, 4});
    // THE CONTROL: an insert and its undo before a save write nothing -- the catalog included.
    REQUIRE(layered->journal->run(std::make_unique<core::InsertPagesCommand>(0, copied)));
    REQUIRE(layered->journal->undo());
    auto delta = layered->sink()->delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());
}

// ==========================================================================================
// A copy's objects written again as they were -- only while what they point at is the same (S7 T4's rule, at T5)
// ==========================================================================================

TEST_CASE("pageops memo: a payload's objects are written again as they were only while what they map to is the same", "[pageops][insert][memo]")
{
    // A-128's oracle for the payload's one unit: the update the show composed, a second compose and a compose with
    // everything composed FORGOTTEN are the same bytes. The payload's form XObject names the OTHER copied page (`/Next`),
    // so deleting that copy changes what the form must be written as -- the reuse that ignored the mapping (P12) writes
    // it pointing at a page no longer written.
    tests::TinyPdf pdf;
    const int catalog = pdf.reserve();
    const int root = pdf.reserve();
    const int first = pdf.reserve();
    const int second = pdf.reserve();
    const int form = pdf.reserve();
    pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(root) + " 0 R >>");
    pdf.set(root, "<< /Type /Pages /Kids [" + std::to_string(first) + " 0 R " + std::to_string(second) + " 0 R] /Count 2 >>");
    pdf.set(first, "<< /Type /Page /Parent " + std::to_string(root) + " 0 R /MediaBox [0 0 200 200] /Resources << /XObject << /Fm "
                       + std::to_string(form) + " 0 R >> >> >>");
    pdf.set(second, "<< /Type /Page /Parent " + std::to_string(root) + " 0 R /MediaBox [0 0 200 200] >>");
    pdf.set(form, "<< /Type /XObject /Subtype /Form /BBox [0 0 10 10] /Next " + std::to_string(second) + " 0 R /Length 3 >>\nstream\nq Q\nendstream");
    auto copied = pdf::ops::ObjectGraphCopier::copyFile(pdf.bytes(catalog), {0, 1});
    REQUIRE(copied);

    auto session = tests::openCopy("assembly/refs-none.pdf");
    REQUIRE(session);
    tests::SaveSession* self = session.get();
    session->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); },
                                     [self] { return self->backend.settleEdits(self->handle); });
    const auto same = [&](const char* when) {
        INFO(when);
        pdf::ops::MutationSinkImpl* sink = session->sink();
        const QByteArray shown = session->backend.shownUpdate(session->handle);
        auto warm = pdf::ops::RevisionWriter::composeUpdate(*sink, false);
        sink->forgetComposedForTesting();
        auto cold = pdf::ops::RevisionWriter::composeUpdate(*sink, false);
        REQUIRE(warm);
        REQUIRE(cold);
        CHECK(tests::sha256(warm.value().update) == tests::sha256(cold.value().update));
        CHECK(tests::sha256(shown) == tests::sha256(warm.value().update));
    };
    REQUIRE(session->journal->run(std::make_unique<core::InsertPagesCommand>(0, copied.value())));
    REQUIRE(session->journal->settle());
    same("after the insert");
    REQUIRE(session->journal->run(std::make_unique<core::RotatePageCommand>(5, 90)));
    REQUIRE(session->journal->settle());
    same("after a rotation elsewhere -- the payload's unit written again as it was");
    REQUIRE(session->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{1})));
    REQUIRE(session->journal->settle());
    same("after the second copied page is deleted -- the form now points at nothing");
}
