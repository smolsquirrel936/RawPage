// tst_reference_repair -- every reference lands on the page it named, graded by what that page SHOWS. Stage 7
// T2, decisions D7-2, D7-17 and D7-18 (`Stage7-Assembly-Plan.md` §3); guide cases A-19, A-20 and A-21
// (`Stage7-Assembly-Testing-Guide.md` §5.4) and the verdict §3.2.
//
// THE ORACLE IS THE PAGE. Every page of the assembly corpus prints its own identity (D7-17), and the label of
// each fixture says, per reference, the identity of the page it targets. So a reference is graded by the text
// PDFium EXTRACTS from the page it lands on in the SAVED file, reopened in a fresh engine -- never by an object
// number or an index the code under test computed (the guide's rule 7). A case comparing our page map with
// our page map proves only that it is deterministic.
//
// WHO RESOLVES WHAT. The outline, the named destinations and the page labels are resolved by PDFium (D4-3's
// reading, `FPDF_GetPageLabel`). PDFium's public API reads links, structure elements, beads and /OpenAction
// nowhere this product reaches, so those are read FIRST-PARTY from the saved bytes by this file's own small
// reader (`Reading`) -- a reference's page object from the file, that object's place by following `/Kids`
// from the root -- and then graded by the identity PDFium extracts at that place. That reader shares no code
// with `pdf/ops/ReferenceRepair`.
//
// WHAT IT DOES NOT COVER (convention 19):
//   * the SCREEN: every grade here is of the saved file; that the live engine shows the same is Stage 7 T3's
//     (A-29, *shown is saved*);
//   * which of two pages showing one identity a reference lands on, after a DUPLICATE: the copy shows its
//     source's identity, so "lands on a page showing T" cannot tell them apart. That a reference keeps
//     pointing at the ORIGINAL is the headless half's (`tests/unit/tst_reference_repair.cpp`: the source's
//     annotations are not the copy's, and a move writes no reference);
//   * `qpdf --check` over what this saves: CI's `portable-asan` job (A-25).

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Destinations.h"
#include "core/doc/Document.h"
#include "core/doc/Outline.h"
#include "core/doc/SaveCoordinator.h"
#include "core/render/RenderRequest.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/cos/XrefRepair.h"
#include "platform/win/WinDurableFiles.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
using pdf::cos::CosObject;

namespace {

// ---- The live document: the shipped backend, its journal SHOWN as `DocumentWindow` shows it -------------

struct Live {
    QTemporaryDir scratch;
    pdf::DocumentBackendImpl backend;
    platform::win::WinDurableFiles durableFiles;
    QString path;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    std::unique_ptr<core::SaveCoordinator> coordinator;
    int saves = 0;
};

std::unique_ptr<Live> openLive(const std::string& relative)
{
    auto live = std::make_unique<Live>();
    REQUIRE(live->scratch.isValid());
    live->path = live->scratch.filePath(QStringLiteral("document.pdf"));
    REQUIRE(QFile::copy(corpusPath(relative), live->path));
    QFile::setPermissions(live->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    auto handle = live->backend.open(live->path, nullptr);
    INFO(relative);
    REQUIRE(handle);
    live->handle = handle.value();
    REQUIRE(live->backend.mutationSink(live->handle) != nullptr);
    core::Document document;
    REQUIRE(document.hydrate(live->backend, live->handle));
    live->journal = std::make_unique<core::CommandJournal>(std::move(document), live->backend.mutationSink(live->handle));
    Live* self = live.get();
    live->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); });
    live->coordinator = std::make_unique<core::SaveCoordinator>(live->backend, &live->durableFiles);
    return live;
}

// Saved where the product would save it -- or Saved As, when the coordinator asks -- into a path of its own
// each time, so a reading of an earlier save is never the file a later one rewrote.
QString save(Live& live)
{
    auto result = live.coordinator->save(*live.journal, live.path);
    INFO("save: " << (result ? std::string() : std::string(result.error().messageKey.data()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    REQUIRE(result.value().kind == core::SaveOutcome::Kind::Saved);
    const QString copy = live.scratch.filePath(QStringLiteral("saved-%1.pdf").arg(++live.saves));
    REQUIRE(QFile::copy(live.path, copy));
    return copy;
}

// ---- The saved file, as readers that are not the code under test read it -------------------------------

const QRegularExpression& identityPattern()
{
    static const QRegularExpression pattern(QStringLiteral("(?<![A-Za-z0-9-])[A-Z][A-Z0-9]*-[0-9]{4}(?![A-Za-z0-9-])"));
    return pattern;
}

struct Reading {
    pdf::DocumentBackendImpl backend;
    core::DocumentHandle handle;
    std::vector<std::string> identity;   // per page: what PDFium extracts; "" for none
    std::vector<QString> label;          // per page: PDFium's label
    QByteArray bytes;
    std::optional<pdf::cos::XrefTable> table;
    std::map<long long, int> indexOf;    // page object -> its place, by following /Kids from the root

    std::optional<CosObject> object(long long number)
    {
        auto fetched = table->fetch(number);
        if (!fetched) return std::nullopt;
        CosObject value = fetched.value().object;
        value.clearSpansRecursively();
        return value;
    }
    std::optional<CosObject> resolve(const CosObject& value) { return value.isReference() ? object(value.reference().number) : value; }
    CosObject catalog() { return *object(table->trailer().get("Root").reference().number); }
    const core::Destinations& destinations() { return *backend.destinations(handle); }
    const core::Outline& outline() { return *backend.outline(handle); }
    // The page a destination value names: a page object of the tree, -1 for none, -2 for an object that is
    // no page of the tree (dangling), and a NAMED destination resolved by PDFium.
    int pageOf(const CosObject& value, int depth = 0)
    {
        if (depth > 6) return -1;
        if (value.isReference()) {
            const std::optional<CosObject> resolved = object(value.reference().number);
            return resolved ? pageOf(*resolved, depth + 1) : -2;
        }
        if (value.isArray() && !value.items().empty() && value.items().front().isReference()) {
            const auto found = indexOf.find(value.items().front().reference().number);
            return found == indexOf.end() ? -2 : found->second;
        }
        if (value.isDictionary()) return value.has("D") ? pageOf(value.get("D"), depth + 1) : -1;
        if (value.isName() || value.isString()) {
            const core::NamedDestination* named = destinations().find(QString::fromStdString(value.text()));
            return named == nullptr ? -2 : named->destination.pageIndex;
        }
        return -1;
    }
    // A link's or an outline item's target, as `pageOf` answers; -1 when it has neither /Dest nor a GoTo.
    int targetOf(const CosObject& holder)
    {
        if (const CosObject* dest = holder.find("Dest")) return pageOf(*dest);
        if (const CosObject* action = holder.find("A")) {
            const std::optional<CosObject> resolved = resolve(*action);
            if (resolved && resolved->get("S").isName() && resolved->get("S").text() == "GoTo") return pageOf(resolved->get("D"));
        }
        return -1;
    }
    std::string identityAt(int index) const
    {
        return index >= 0 && index < static_cast<int>(identity.size()) ? identity[static_cast<std::size_t>(index)] : std::string("(no page)");
    }
};

std::unique_ptr<Reading> read(const QString& path)
{
    auto reading = std::make_unique<Reading>();
    auto handle = reading->backend.open(path, nullptr);
    REQUIRE(handle);
    reading->handle = handle.value();
    const core::PageTree* pages = reading->backend.pages(reading->handle);
    REQUIRE(pages != nullptr);
    core::ITextExtractor* extractor = reading->backend.textExtractor(reading->handle);
    REQUIRE(extractor != nullptr);
    for (int index = 0; index < pages->count(); ++index) {
        PageTextModel model;
        REQUIRE(extractor->extractPage(reading->handle, index, model));
        const QRegularExpressionMatch match = identityPattern().match(model.text());
        reading->identity.push_back(match.hasMatch() ? match.captured(0).toStdString() : std::string());
        reading->label.push_back(pages->at(index)->label());
    }
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    reading->bytes = file.readAll();
    auto opened = pdf::cos::openWithRepair(std::string_view(reading->bytes.constData(), static_cast<std::size_t>(reading->bytes.size())));
    REQUIRE(opened);
    REQUIRE(opened.value().usedOriginalXref);
    reading->table.emplace(std::move(opened.value().table));
    std::vector<long long> stack{reading->catalog().get("Pages").reference().number};
    std::set<long long> visited;
    while (!stack.empty()) {
        const long long number = stack.back();
        stack.pop_back();
        REQUIRE(visited.insert(number).second);
        const std::optional<CosObject> node = reading->object(number);
        REQUIRE(node);
        if (!node->get("Kids").isArray()) {
            reading->indexOf.emplace(number, static_cast<int>(reading->indexOf.size()));
            continue;
        }
        const auto& kids = node->get("Kids").items();
        for (auto kid = kids.rbegin(); kid != kids.rend(); ++kid) stack.push_back(kid->reference().number);
    }
    REQUIRE(reading->indexOf.size() == reading->identity.size());   // the two readers agree on the count
    return reading;
}

// ---- The grade -----------------------------------------------------------------------------------------

struct Grade {
    std::vector<std::string> failures;
    int landed = 0;    // references graded on the page they land on
    int cleared = 0;   // references to a page gone, graded as D7-2 treats them
    void fail(std::string line) { failures.push_back(std::move(line)); }
};

std::string text(const QJsonValue& value) { return value.toString().toStdString(); }

// Every reference `label` claims, in the saved file `reading`, for a document whose pages showing an identity
// in `gone` were deleted -- §3.2's table, one line per failure.
Grade grade(Reading& reading, const QJsonObject& label, const std::set<std::string>& gone)
{
    Grade g;
    const auto landsOn = [&](const std::string& where, int index, const std::string& identity) {
        if (gone.count(identity) != 0) {
            if (index >= 0) g.fail(where + " still lands on page " + std::to_string(index) + " (" + reading.identityAt(index) + ") for " + identity + ", which was deleted");
            else if (index == -2) g.fail(where + " dangles: it names an object that is no page of the document");
            else ++g.cleared;
            return;
        }
        if (reading.identityAt(index) != identity)
            g.fail(where + " lands on page " + std::to_string(index) + " showing '" + reading.identityAt(index) + "', not " + identity);
        else
            ++g.landed;
    };

    // The outline, resolved by PDFium, walked beside the label's tree: no item is ever removed (D7-2).
    std::vector<std::pair<const std::vector<core::OutlineItem>*, QJsonArray>> stack{
        {&reading.outline().roots(), label.value(QStringLiteral("outline")).toArray()}};
    while (!stack.empty()) {
        const auto [items, expected] = stack.back();
        stack.pop_back();
        if (items->size() != static_cast<std::size_t>(expected.size())) {
            g.fail("the outline has " + std::to_string(items->size()) + " items where the label has " + std::to_string(expected.size()));
            continue;
        }
        for (qsizetype at = 0; at < expected.size(); ++at) {
            const QJsonObject entry = expected.at(at).toObject();
            const core::OutlineItem& item = items->at(static_cast<std::size_t>(at));
            if (item.title.toStdString() != text(entry.value(QStringLiteral("title"))))
                g.fail("outline item '" + item.title.toStdString() + "' where the label has '" + text(entry.value(QStringLiteral("title"))) + "'");
            if (!entry.value(QStringLiteral("targetIdentity")).isNull())
                landsOn("outline item '" + item.title.toStdString() + "'", item.destination.pageIndex, text(entry.value(QStringLiteral("targetIdentity"))));
            stack.push_back({&item.children, entry.value(QStringLiteral("children")).toArray()});
        }
    }

    // Named destinations, resolved by PDFium: one to a page gone is REMOVED.
    for (const QJsonValue value : label.value(QStringLiteral("namedDestinations")).toArray()) {
        const QJsonObject entry = value.toObject();
        const std::string identity = text(entry.value(QStringLiteral("targetIdentity")));
        const core::NamedDestination* found = reading.destinations().find(entry.value(QStringLiteral("name")).toString());
        const std::string where = "named destination '" + text(entry.value(QStringLiteral("name"))) + "'";
        if (gone.count(identity) != 0) {
            if (found != nullptr) g.fail(where + " is still listed, for " + identity + ", which was deleted");
            else ++g.cleared;
        } else if (found == nullptr) {
            g.fail(where + " is missing");
        } else {
            landsOn(where, found->destination.pageIndex, identity);
        }
    }

    // Links, read first-party: on a page that stays, cleared when their target went. A GoToR is A-22's.
    std::vector<QJsonObject> links;
    for (const QJsonValue value : label.value(QStringLiteral("links")).toArray()) links.push_back(value.toObject());
    for (const QJsonValue page : label.value(QStringLiteral("pages")).toArray()) {
        for (const QJsonValue annot : page.toObject().value(QStringLiteral("annots")).toArray()) {
            if (annot.toObject().value(QStringLiteral("role")).toString() != QLatin1String("link")) continue;
            QJsonObject link = annot.toObject();
            link.insert(QStringLiteral("onIdentity"), page.toObject().value(QStringLiteral("identity")));
            links.push_back(link);
        }
    }
    const QJsonArray pages = label.value(QStringLiteral("pages")).toArray();
    for (const QJsonObject& link : links) {
        if (link.value(QStringLiteral("form")).toString() == QLatin1String("goToR")) continue;
        const std::string on = link.contains(QStringLiteral("onIdentity"))
                                   ? text(link.value(QStringLiteral("onIdentity")))
                                   : text(pages.at(link.value(QStringLiteral("page")).toInt()).toObject().value(QStringLiteral("identity")));
        if (gone.count(on) != 0) continue;   // gone with its page
        const long long number = static_cast<long long>(link.value(QStringLiteral("object")).toDouble());
        const std::optional<CosObject> dictionary = reading.object(number);
        const std::string where = "link " + std::to_string(number);
        if (!dictionary) {
            g.fail(where + " is missing");
            continue;
        }
        landsOn(where, reading.targetOf(*dictionary), text(link.value(QStringLiteral("targetIdentity"))));
    }

    // Structure elements, read first-party: an element on a page gone has no /Pg and no content item.
    for (const QJsonValue value : label.value(QStringLiteral("structure")).toArray()) {
        const QJsonObject entry = value.toObject();
        const long long number = static_cast<long long>(entry.value(QStringLiteral("object")).toDouble());
        const std::optional<CosObject> element = reading.object(number);
        const std::string identity = text(entry.value(QStringLiteral("pageIdentity")));
        const std::string where = "structure element " + std::to_string(number);
        if (!element) {
            g.fail(where + " is missing");
            continue;
        }
        const int page = element->has("Pg") ? reading.pageOf(CosObject::makeArray({element->get("Pg")})) : -1;
        landsOn(where, page, identity);
        if (gone.count(identity) != 0 && element->get("K").isInteger()) g.fail(where + " keeps a marked-content item of a page deleted");
    }

    // Threads, read first-party: each ring from /F along /N; a bead on a page gone is out of it.
    std::set<long long> listedThreads;
    if (const std::optional<CosObject> threads = reading.resolve(reading.catalog().get("Threads")); threads && threads->isArray())
        for (const CosObject& thread : threads->items()) listedThreads.insert(thread.reference().number);
    for (const QJsonValue value : label.value(QStringLiteral("threads")).toArray()) {
        const QJsonObject entry = value.toObject();
        const long long number = static_cast<long long>(entry.value(QStringLiteral("object")).toDouble());
        std::vector<long long> ring;
        if (const std::optional<CosObject> thread = reading.object(number)) {
            std::set<long long> seen;
            for (long long at = thread->get("F").reference().number; seen.insert(at).second;) {
                ring.push_back(at);
                const std::optional<CosObject> bead = reading.object(at);
                if (!bead || !bead->get("N").isReference()) break;
                if (reading.object(bead->get("N").reference().number)->get("V").reference().number != at)
                    g.fail("thread " + std::to_string(number) + ": bead " + std::to_string(at) + "'s next does not point back");
                at = bead->get("N").reference().number;
            }
        }
        bool anyLeft = false;
        for (const QJsonValue beadValue : entry.value(QStringLiteral("beads")).toArray()) {
            const QJsonObject bead = beadValue.toObject();
            const long long beadNumber = static_cast<long long>(bead.value(QStringLiteral("object")).toDouble());
            const std::string identity = text(bead.value(QStringLiteral("pageIdentity")));
            const bool inRing = std::find(ring.begin(), ring.end(), beadNumber) != ring.end();
            const std::string where = "bead " + std::to_string(beadNumber);
            if (gone.count(identity) != 0) {
                if (inRing && listedThreads.count(number) != 0) g.fail(where + " is still in its thread, for " + identity + ", which was deleted");
                else ++g.cleared;
                continue;
            }
            anyLeft = true;
            if (!inRing) {
                g.fail(where + " is no longer in its thread");
                continue;
            }
            landsOn(where, reading.pageOf(CosObject::makeArray({reading.object(beadNumber)->get("P")})), identity);
        }
        if (anyLeft != (listedThreads.count(number) != 0))
            g.fail("thread " + std::to_string(number) + (anyLeft ? " lost its place in /Threads" : " is listed with no bead left"));
    }

    // /OpenAction, read first-party.
    if (label.contains(QStringLiteral("openAction"))) {
        const std::string identity = text(label.value(QStringLiteral("openAction")).toObject().value(QStringLiteral("targetIdentity")));
        const CosObject catalog = reading.catalog();
        const int page = catalog.has("OpenAction") ? reading.pageOf(catalog.get("OpenAction")) : -1;
        landsOn("/OpenAction", page, identity);
    }

    // AND NOTHING DANGLES (§3.2's second row), whether the label names it or not: every reference IN THE FILE,
    // read first-party, names a page of the document or nothing. PDFium answers "unresolved" alike for a
    // destination cleared and one left pointing at a page the tree no longer lists, so the grade above cannot
    // tell them apart through the engine -- this sweep can (found by the plant that leaves outline items alone).
    {
        const CosObject catalog = reading.catalog();
        std::vector<long long> items;
        std::set<long long> seen;
        if (const std::optional<CosObject> outlines = reading.resolve(catalog.get("Outlines")); outlines && outlines->get("First").isReference())
            items.push_back(outlines->get("First").reference().number);
        while (!items.empty()) {
            const long long number = items.back();
            items.pop_back();
            if (!seen.insert(number).second) continue;
            const std::optional<CosObject> item = reading.object(number);
            if (!item) continue;
            if (reading.targetOf(*item) == -2) g.fail("outline item " + std::to_string(number) + " dangles");
            if (item->get("Next").isReference()) items.push_back(item->get("Next").reference().number);
            if (item->get("First").isReference()) items.push_back(item->get("First").reference().number);
        }
        for (const auto& [page, index] : reading.indexOf) {
            const std::optional<CosObject> dictionary = reading.object(page);
            const std::optional<CosObject> annots = dictionary ? reading.resolve(dictionary->get("Annots")) : std::nullopt;
            if (!annots || !annots->isArray()) continue;
            for (const CosObject& entry : annots->items()) {
                const std::optional<CosObject> annotation = reading.resolve(entry);
                if (!annotation || annotation->get("Subtype").text() != "Link") continue;
                if (reading.targetOf(*annotation) == -2) g.fail("a link on page " + std::to_string(index) + " dangles");
            }
        }
        for (const core::NamedDestination& named : reading.destinations().all()) {
            if (!named.destination.isResolved()) g.fail("named destination '" + named.name.toStdString() + "' names no page");
        }
        if (const std::optional<CosObject> root = reading.resolve(catalog.get("StructTreeRoot")); root && root->isDictionary()) {
            std::vector<CosObject> elements{root->get("K")};
            std::set<long long> visited;
            while (!elements.empty()) {
                const CosObject value = elements.back();
                elements.pop_back();
                if (value.isReference() && !visited.insert(value.reference().number).second) continue;
                const std::optional<CosObject> resolved = reading.resolve(value);
                if (!resolved) continue;
                if (resolved->isArray()) {
                    for (const CosObject& item : resolved->items()) elements.push_back(item);
                    continue;
                }
                if (!resolved->isDictionary()) continue;
                if (resolved->get("Pg").isReference() && reading.indexOf.count(resolved->get("Pg").reference().number) == 0)
                    g.fail("a structure element's /Pg dangles");
                if (resolved->has("K")) elements.push_back(resolved->get("K"));
            }
        }
        if (const std::optional<CosObject> threads = reading.resolve(catalog.get("Threads")); threads && threads->isArray()) {
            for (const CosObject& thread : threads->items()) {
                const std::optional<CosObject> value = reading.resolve(thread);
                if (!value) continue;
                std::set<long long> ring;
                for (long long at = value->get("F").reference().number; ring.insert(at).second;) {
                    const std::optional<CosObject> bead = reading.object(at);
                    if (!bead || !bead->get("N").isReference()) break;
                    if (reading.indexOf.count(bead->get("P").reference().number) == 0) g.fail("a bead's /P dangles");
                    at = bead->get("N").reference().number;
                }
            }
        }
        if (catalog.has("OpenAction") && reading.pageOf(catalog.get("OpenAction")) == -2) g.fail("/OpenAction dangles");
    }
    return g;
}

std::set<std::string> identitiesOf(const QJsonObject& label, const std::vector<int>& pages)
{
    std::set<std::string> out;
    const QJsonArray all = label.value(QStringLiteral("pages")).toArray();
    for (const int page : pages) out.insert(text(all.at(page).toObject().value(QStringLiteral("identity"))));
    return out;
}

void report(const Grade& g)
{
    for (const std::string& failure : g.failures) UNSCOPED_INFO(failure);
    CHECK(g.failures.empty());
}

// The fixtures with references the labels name: every kind in `refs-every-kind`, outlines and links in the merge
// sources, links in `annots-on-pages`, an outline in `split-blanks`.
const char* const kFixtures[] = {"refs-every-kind", "merge-a", "merge-b", "merge-c", "annots-on-pages", "split-blanks"};

} // namespace

// ==========================================================================================
// A-19 -- every page command, every fixture with references: saved, reopened, graded by what the page shows
// ==========================================================================================

TEST_CASE("reference_repair every reference lands on the page it named after every page command", "[render][pages][reference_repair][A-19]")
{
    int landed = 0;
    int cleared = 0;
    for (const char* fixture : kFixtures) {
        const QJsonObject label = loadLabel(std::string("assembly-") + fixture);
        const std::string file = text(label.value(QStringLiteral("file")));
        const int count = static_cast<int>(label.value(QStringLiteral("pages")).toArray().size());
        struct Run {
            const char* name;
            std::function<std::unique_ptr<core::Command>()> make;
            std::vector<int> deleted;
        };
        std::vector<Run> runs{
            {"move the first page to the end", [count] { return std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, count - 1); }, {}},
            {"move the last page to the front", [count] { return std::make_unique<core::MovePagesCommand>(std::vector<int>{count - 1}, 0); }, {}},
            {"duplicate the first and last pages", [count] { return std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0, count - 1}); }, {}},
            {"delete the second page", [] { return std::make_unique<core::DeletePagesCommand>(std::vector<int>{1}); }, {1}},
        };
        if (count >= 3) {
            runs.push_back({"move the last two pages to the front", [count] {
                                return std::make_unique<core::MovePagesCommand>(std::vector<int>{count - 2, count - 1}, 0); }, {}});
            runs.push_back({"delete the first and last pages", [count] {
                                return std::make_unique<core::DeletePagesCommand>(std::vector<int>{0, count - 1}); }, {0, count - 1}});
        }
        for (const Run& run : runs) {
            INFO(file << ": " << run.name);
            auto live = openLive(file);
            const QByteArray hash = live->journal->document().modelHash();
            REQUIRE(live->journal->run(run.make()));
            {
                auto reading = read(save(*live));
                const Grade g = grade(*reading, label, identitiesOf(label, run.deleted));
                report(g);
                landed += g.landed;
                cleared += g.cleared;
                // Shown is saved, for the labels: the model's page labels are the ones the saved file reads back as.
                const core::PageTree& model = live->journal->document().pages();
                REQUIRE(model.count() == static_cast<int>(reading->label.size()));
                for (int index = 0; index < model.count(); ++index) CHECK(model.at(index)->label() == reading->label[static_cast<std::size_t>(index)]);
            }
            // §3.2's last row: UNDONE after the save, the document is the original again and so is every reference.
            REQUIRE(live->journal->undo());
            CHECK(live->journal->document().modelHash() == hash);
            auto reading = read(save(*live));
            const Grade back = grade(*reading, label, {});
            INFO("after the undo");
            report(back);
        }
    }
    // Recorded rather than guessed: the grades that ran, so a fixture that graded nothing shows here.
    WARN("A-19: " << landed << " references graded on the page they land on, " << cleared << " cleared as D7-2 says");
    // The measured totals (2026-10-30), exact: the corpus is generated byte-identically, so a change is a fixture
    // or a grade that changed -- either is worth reading before the number is moved.
    CHECK(landed == 296);
    CHECK(cleared == 19);
}

// ==========================================================================================
// A-20 -- a reference to a deleted page is never left dangling
// ==========================================================================================

TEST_CASE("reference_repair.deleted each page of refs-every-kind deleted in turn", "[render][pages][reference_repair.deleted][A-20]")
{
    const QJsonObject label = loadLabel("assembly-refs-every-kind");
    const QJsonArray pages = label.value(QStringLiteral("pages")).toArray();
    for (int page = 0; page < pages.size(); ++page) {
        const std::string identity = text(pages.at(page).toObject().value(QStringLiteral("identity")));
        INFO("deleting page " << page << " (" << identity << ")");
        auto live = openLive("assembly/refs-every-kind.pdf");
        const QByteArray hash = live->journal->document().modelHash();
        auto command = std::make_unique<core::DeletePagesCommand>(std::vector<int>{page});
        core::DeletePagesCommand* deleted = command.get();
        REQUIRE(live->journal->run(std::move(command)));

        // THE REPORT lists every reference the label says targets that page, each with D7-2's treatment.
        std::multiset<std::pair<int, std::string>> expected;
        const auto expect = [&](core::PageReference::Kind kind, const std::string& what) { expected.insert({static_cast<int>(kind), what}); };
        std::vector<std::pair<QJsonArray, int>> items{{label.value(QStringLiteral("outline")).toArray(), 0}};
        while (!items.empty()) {
            const QJsonArray level = items.back().first;
            items.pop_back();
            for (const QJsonValue value : level) {
                const QJsonObject item = value.toObject();
                if (text(item.value(QStringLiteral("targetIdentity"))) == identity) expect(core::PageReference::Kind::OutlineItem, text(item.value(QStringLiteral("title"))));
                items.push_back({item.value(QStringLiteral("children")).toArray(), 0});
            }
        }
        for (const QJsonValue value : label.value(QStringLiteral("namedDestinations")).toArray())
            if (text(value.toObject().value(QStringLiteral("targetIdentity"))) == identity)
                expect(core::PageReference::Kind::NamedDestination, text(value.toObject().value(QStringLiteral("name"))));
        for (const QJsonValue value : label.value(QStringLiteral("links")).toArray()) {
            const QJsonObject link = value.toObject();
            if (link.value(QStringLiteral("page")).toInt() == page || link.value(QStringLiteral("form")).toString() == QLatin1String("goToR")) continue;
            if (text(link.value(QStringLiteral("targetIdentity"))) == identity)
                expect(core::PageReference::Kind::Link, std::to_string(static_cast<long long>(link.value(QStringLiteral("object")).toDouble())));
        }
        for (const QJsonValue value : label.value(QStringLiteral("structure")).toArray())
            if (text(value.toObject().value(QStringLiteral("pageIdentity"))) == identity)
                expect(core::PageReference::Kind::StructureElement, std::to_string(static_cast<long long>(value.toObject().value(QStringLiteral("object")).toDouble())));
        for (const QJsonValue thread : label.value(QStringLiteral("threads")).toArray())
            for (const QJsonValue bead : thread.toObject().value(QStringLiteral("beads")).toArray())
                if (text(bead.toObject().value(QStringLiteral("pageIdentity"))) == identity)
                    expect(core::PageReference::Kind::ThreadBead, std::to_string(static_cast<long long>(bead.toObject().value(QStringLiteral("object")).toDouble())));
        if (text(label.value(QStringLiteral("openAction")).toObject().value(QStringLiteral("targetIdentity"))) == identity)
            expect(core::PageReference::Kind::OpenAction, "");

        std::multiset<std::pair<int, std::string>> found;
        for (const core::PageReference& r : deleted->report().references) {
            using K = core::PageReference::Kind;
            using T = core::PageReference::Treatment;
            if (r.kind == K::RemoteGoTo || r.kind == K::IndexDestination) {
                CHECK(r.treatment == T::Reported);
                continue;
            }
            const T want = (r.kind == K::OutlineItem || r.kind == K::Link) ? T::Cleared : r.kind == K::ThreadBead ? T::Unlinked : T::Removed;
            CHECK(r.treatment == want);
            CHECK(r.page == page);
            const bool byWords = r.kind == K::OutlineItem || r.kind == K::NamedDestination;
            found.insert({static_cast<int>(r.kind), byWords ? r.name.toStdString() : r.kind == K::OpenAction ? std::string() : std::to_string(r.object)});
        }
        CHECK(found == expected);

        // THE FILE: every reference graded as §3.2 says, nothing dangling.
        auto reading = read(save(*live));
        report(grade(*reading, label, {identity}));

        // And undone after the save: the structural hash and every reference back.
        REQUIRE(live->journal->undo());
        CHECK(live->journal->document().modelHash() == hash);
        auto back = read(save(*live));
        report(grade(*back, label, {}));
    }
}

// ==========================================================================================
// A-21 -- page labels follow their pages
// ==========================================================================================

TEST_CASE("reference_repair.labels a moved page keeps its label and a copy takes the next of its range", "[render][pages][reference_repair.labels][A-21]")
{
    const QJsonObject label = loadLabel("assembly-refs-every-kind");
    std::map<std::string, QString> labelOf;
    for (const QJsonValue page : label.value(QStringLiteral("pages")).toArray())
        labelOf[text(page.toObject().value(QStringLiteral("identity")))] = page.toObject().value(QStringLiteral("pageLabel")).toString();

    SECTION("a move: every page shows the label it had, read by PDFium")
    {
        for (const auto& [pages, to] : std::vector<std::pair<std::vector<int>, int>>{{{0}, 11}, {{3, 4, 9}, 0}, {{11}, 5}}) {
            auto live = openLive("assembly/refs-every-kind.pdf");
            REQUIRE(live->journal->run(std::make_unique<core::MovePagesCommand>(pages, to)));
            auto reading = read(save(*live));
            for (std::size_t index = 0; index < reading->identity.size(); ++index) {
                INFO("page " << index << " showing " << reading->identity[index]);
                CHECK(reading->label[index] == labelOf.at(reading->identity[index]));
            }
        }
    }
    SECTION("a duplicate and a delete, against a hand-written table")
    {
        auto live = openLive("assembly/refs-every-kind.pdf");
        REQUIRE(live->journal->run(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0, 6})));
        auto copied = read(save(*live));
        CHECK(copied->label == std::vector<QString>{"i", "ii", "ii", "iii", "1", "2", "3", "4", "5", "5", "6", "A-1", "A-2", "A-3"});
        REQUIRE(live->journal->undo());
        REQUIRE(live->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{3, 10})));
        auto deleted = read(save(*live));
        CHECK(deleted->label == std::vector<QString>{"i", "ii", "iii", "2", "3", "4", "5", "6", "A-1", "A-3"});
    }
    SECTION("outline-labels.pdf: a move keeps every page's label, read by PDFium before and after")
    {
        auto live = openLive("ordinary/outline-labels.pdf");
        const auto before = read(live->path);
        const int count = static_cast<int>(before->label.size());
        REQUIRE(count >= 3);
        REQUIRE(live->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, count - 1)));
        auto after = read(save(*live));
        std::vector<QString> expected(before->label.begin() + 1, before->label.end());
        expected.push_back(before->label.front());
        CHECK(after->label == expected);
    }
}

// ==========================================================================================
// D7-2's move under ANOTHER PARENT: what a page inherited stays what it draws with
// ==========================================================================================

namespace {

core::RasterImage renderWhole(core::IDocumentBackend& backend, core::DocumentHandle handle, int page)
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
    core::IPageRasterizer* rasterizer = backend.rasterizer(handle);
    REQUIRE(rasterizer != nullptr);
    auto image = rasterizer->raster(request);
    REQUIRE(image);
    return image.value();
}

std::string digestOf(const core::RasterImage& image)
{
    QByteArray bytes;
    bytes.append(QByteArray::number(image.width)).append('x').append(QByteArray::number(image.height)).append(':');
    for (int row = 0; row < image.height; ++row)
        bytes.append(image.pixels.constData() + static_cast<qsizetype>(row) * image.bytesPerLine, image.width * 4);
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

} // namespace

TEST_CASE("reference_repair.inherit a page moved under another parent draws exactly as it drew", "[render][pages][reference_repair.inherit]")
{
    // `inherit-resources.pdf`: no page carries /Resources, /MediaBox or /CropBox, each inherited from a different
    // level, and /Rotate inherited except on one page. PDFium resolves the four up `/Parent`, so a page listed
    // under another node draws with THAT node's fonts, boxes and turn unless what it inherited is made its own --
    // which the sink does for exactly the keys the new parent would change. Graded on the pixels of the saved
    // file, reopened, against the original's: 0 differing pixels, by digest.
    std::map<std::string, std::string> before;   // identity -> raster digest
    {
        auto original = read(corpusPath("assembly/inherit-resources.pdf"));
        for (int index = 0; index < static_cast<int>(original->identity.size()); ++index)
            before[original->identity[static_cast<std::size_t>(index)]] = digestOf(renderWhole(original->backend, original->handle, index));
    }
    REQUIRE(before.size() == 5);
    struct Run {
        const char* name;
        std::vector<int> pages;
        int to;
    };
    for (const Run& run : {Run{"the first page (A4, 90, shared resources) to the end, under the Letter branch", {0}, 4},
                           Run{"the fourth page (Letter, 180) to the front, under the A4 branch", {3}, 0},
                           Run{"the last page (its own /Rotate 0) to the middle", {4}, 2}}) {
        INFO(run.name);
        auto live = openLive("assembly/inherit-resources.pdf");
        REQUIRE(live->journal->run(std::make_unique<core::MovePagesCommand>(run.pages, run.to)));
        auto saved = read(save(*live));
        REQUIRE(saved->identity.size() == 5);
        for (int index = 0; index < 5; ++index) {
            const std::string& identity = saved->identity[static_cast<std::size_t>(index)];
            INFO("page " << index << " shows " << identity);
            REQUIRE(before.count(identity) == 1);
            CHECK(digestOf(renderWhole(saved->backend, saved->handle, index)) == before.at(identity));
        }
    }
}
