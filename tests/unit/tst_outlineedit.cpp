// tests/unit/tst_outlineedit.cpp -- the outline's six commands through the SHIPPED sink, headless. Stage 7 T11, guide
// cases A-76, A-77 and A-78 (`Stage7-Assembly-Testing-Guide.md` §5.13), and the bookmarks' half of A-19 and A-45.
//
// WHAT THIS FILE IS AND IS NOT (convention 19). Every case opens a copy of a corpus file through
// `tests/unit/CosBackend.h` -- the first-party reader and the real `pdf/ops/MutationSinkImpl` -- runs commands
// through the journal, saves through `SaveCoordinator`, and reads the written file back with `pdf/ops/OutlineReader`
// and the COS layer. **It does not see PDFium**: whether the engine reads what was written -- the two readings'
// agreement on every corpus file with an outline, and the links and counts read back by PDFium -- is
// `tests/render/tst_outlinereader.cpp`'s (A-75, A-77's PDFium half). The model's outline here is the FIRST-PARTY
// reading, which is what the shipped backend hands a document whose readings agree.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/BookmarkCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/OutlineExchange.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/OutlineReader.h"
#include "pdf/ops/PageOps.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using core::OutlineAction;
using core::OutlineItem;
using core::OutlineItemId;
using core::OutlinePosition;
using pdf::cos::CosObject;

namespace {

struct Written {
    QByteArray bytes;
    pdf::cos::XrefTable table;
    pdf::ops::PageWalk walk;
    pdf::ops::OutlineReader::Reading outline;

    std::map<long long, int> indexOf() const
    {
        std::map<long long, int> out;
        for (std::size_t i = 0; i < walk.pages.size(); ++i) out.emplace(walk.pages[i].number, static_cast<int>(i));
        return out;
    }
    CosObject object(long long number)
    {
        auto fetched = table.fetch(number);
        REQUIRE(fetched);
        CosObject out = fetched.value().object;
        out.clearSpansRecursively();
        return out;
    }
};

std::unique_ptr<Written> readBytes(QByteArray bytes)
{
    auto out = std::make_unique<Written>();
    out->bytes = std::move(bytes);
    auto opened = pdf::cos::openWithRepair(std::string_view(out->bytes.constData(), static_cast<std::size_t>(out->bytes.size())));
    REQUIRE(opened);
    out->table = std::move(opened.value().table);
    auto walk = pdf::ops::PageOps::walk(out->table);
    REQUIRE(walk);
    out->walk = std::move(walk).value();
    const pdf::ops::ReferenceRepair::Ledger none;
    const pdf::ops::ReferenceRepair::View view(out->table, none);
    out->outline = pdf::ops::OutlineReader::read(view);
    pdf::ops::OutlineReader::resolve(out->outline, out->indexOf());
    return out;
}

bool save(tests::SaveSession& session, const char* name)
{
    if (!session.coordinator->save(*session.journal, session.path)) return false;
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);   // kept for `qpdf --check` (D7-18's oracle 2)
    if (!QDir().mkpath(dir)) return false;
    const QString target = QDir(dir).filePath(QStringLiteral("outlineedit-%1.pdf").arg(QLatin1String(name)));
    QFile::remove(target);
    return QFile::copy(session.path, target);
}

std::vector<long long> deltaObjects(tests::SaveSession& session)
{
    auto delta = session.sink()->delta();
    REQUIRE(delta);
    std::vector<long long> out;
    for (const auto& change : delta.value().changes()) out.push_back(change.number);
    session.sink()->composedNothing();
    return out;
}

bool deltaIsEmpty(tests::SaveSession& session)
{
    return deltaObjects(session).empty();
}

// The written outline's titles and depths, depth first -- `"  1.1 Scope"` for a child.
std::vector<std::string> shape(const std::vector<OutlineItem>& items, int depth = 0)
{
    std::vector<std::string> out;
    for (const OutlineItem& item : items) {
        out.push_back(std::string(static_cast<std::size_t>(depth) * 2, ' ') + item.title.toStdString());
        for (std::string& child : shape(item.children, depth + 1)) out.push_back(std::move(child));
    }
    return out;
}

// EVERY LINK OF THE WRITTEN TREE CONSISTENT (A-77): each item's /Parent its parent, /Prev and /Next its neighbours,
// /First and /Last its first and last child, and /Count the signed number of visible descendants -- read from the
// dictionaries as written, not from the reader's tree. Answers what is wrong, or an empty string.
std::string linksWrong(Written& file)
{
    if (file.outline.root < 0) return file.outline.outline.isEmpty() ? std::string() : "no root";
    std::function<long long(const OutlineItem&)> visible = [&](const OutlineItem& item) {
        long long n = 0;
        for (const OutlineItem& child : item.children) n += 1 + (child.openByDefault ? visible(child) : 0);
        return n;
    };
    const auto ref = [](const CosObject& dictionary, const char* key) {
        const CosObject& value = dictionary.get(key);
        return value.isReference() ? value.reference().number : -1LL;
    };
    std::string wrong;
    std::function<void(const std::vector<OutlineItem>&, long long)> level = [&](const std::vector<OutlineItem>& items, long long parent) {
        for (std::size_t i = 0; i < items.size() && wrong.empty(); ++i) {
            const OutlineItem& item = items[i];
            const CosObject dictionary = file.object(item.id.object);
            const std::string at = item.title.toStdString();
            if (ref(dictionary, "Parent") != parent) wrong = at + ": Parent";
            else if (ref(dictionary, "Prev") != (i > 0 ? items[i - 1].id.object : -1)) wrong = at + ": Prev";
            else if (ref(dictionary, "Next") != (i + 1 < items.size() ? items[i + 1].id.object : -1)) wrong = at + ": Next";
            else if (ref(dictionary, "First") != (item.children.empty() ? -1 : item.children.front().id.object)) wrong = at + ": First";
            else if (ref(dictionary, "Last") != (item.children.empty() ? -1 : item.children.back().id.object)) wrong = at + ": Last";
            else if (!item.children.empty()) {
                const long long want = item.openByDefault ? visible(item) : -visible(item);
                if (dictionary.get("Count").integerValue(0) != want) wrong = at + ": Count";
            }
            level(item.children, item.id.object);
        }
    };
    level(file.outline.outline.roots(), file.outline.root);
    if (!wrong.empty()) return wrong;
    const CosObject root = file.object(file.outline.root);
    const auto& roots = file.outline.outline.roots();
    if (ref(root, "First") != (roots.empty() ? -1 : roots.front().id.object)) return "root First";
    if (ref(root, "Last") != (roots.empty() ? -1 : roots.back().id.object)) return "root Last";
    long long total = 0;
    for (const OutlineItem& item : roots) total += 1 + (item.openByDefault ? visible(item) : 0);
    if (!roots.empty() && root.get("Count").integerValue(-1) != total) return "root Count";
    return {};
}

core::Status run(tests::SaveSession& session, std::unique_ptr<core::Command> command)
{
    return session.journal->run(std::move(command));
}

OutlineItem newItem(const QString& key, int ordinal, const QString& title, int page)
{
    OutlineItem item;
    item.id = core::newBookmarkId(key, ordinal);
    item.title = title;
    core::Destination at;
    at.pageIndex = page;
    at.fit = core::Destination::Fit::XYZ;
    at.hasX = true;
    at.x = 0;
    at.hasY = true;
    at.y = 792;
    item.action = OutlineAction::goTo(at);
    item.destination = at;
    return item;
}

const OutlineItem& at(const core::Outline& outline, std::initializer_list<int> path)
{
    const std::vector<OutlineItem>* level = &outline.roots();
    const OutlineItem* item = nullptr;
    for (const int i : path) {
        REQUIRE(static_cast<std::size_t>(i) < level->size());
        item = &(*level)[static_cast<std::size_t>(i)];
        level = &item->children;
    }
    REQUIRE(item != nullptr);
    return *item;
}

// A one-page document whose two items carry keys this build does not model -- a private key, `/SE` and a
// `/Count` of 0 on a leaf -- and the second a title in PDFDocEncoding's upper half.
std::string privateKeys()
{
    tests::TinyPdf pdf;
    const int catalog = pdf.reserve();
    const int pages = pdf.reserve();
    const int page = pdf.add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>");
    const int outlines = pdf.reserve();
    const int first = pdf.reserve();
    const int second = pdf.reserve();
    pdf.set(catalog, "<< /Type /Catalog /Pages 2 0 R /Outlines " + std::to_string(outlines) + " 0 R >>");
    pdf.set(pages, "<< /Type /Pages /Kids [" + std::to_string(page) + " 0 R] /Count 1 >>");
    pdf.set(outlines, "<< /Type /Outlines /First " + std::to_string(first) + " 0 R /Last " + std::to_string(second)
                          + " 0 R /Count 2 >>");
    pdf.set(first, "<< /Title (Keep my keys) /Parent " + std::to_string(outlines) + " 0 R /Next " + std::to_string(second)
                       + " 0 R /Dest [" + std::to_string(page) + " 0 R /Fit] /RawPagePrivate (left alone) /SE 3 0 R /Count 0 >>");
    pdf.set(second, "<< /Title (Caf\351 \244) /Parent " + std::to_string(outlines) + " 0 R /Prev " + std::to_string(first)
                        + " 0 R /A << /S /URI /URI (https://example.com) /Next << /S /Named /N /NextPage >> >> /C [1 0 0] /F 3 >>");
    return pdf.bytes(catalog);
}

} // namespace

// ==========================================================================================
// A-76 -- an edit changes what it means to and nothing else
// ==========================================================================================

TEST_CASE("outline.preserve: a rename writes that item's dictionary alone, its unknown keys as they were", "[outline][outlineedit][A-76]")
{
    SECTION("outline-labels.pdf")
    {
        auto session = tests::openCopy("ordinary/outline-labels.pdf");
        REQUIRE(session);
        const core::Outline& before = session->journal->document().outline();
        REQUIRE(before.status());
        REQUIRE(before.nodeCount() >= 3);
        const OutlineItem target = at(before, {0, 0});
        auto properties = core::EditBookmarkCommand::Properties::of(target);
        properties.title = QStringLiteral("Renamed é章");
        REQUIRE(run(*session, std::make_unique<core::EditBookmarkCommand>(target.id, properties)));
        // THE UPDATE HOLDS THAT ITEM'S DICTIONARY ONLY.
        CHECK(deltaObjects(*session) == std::vector<long long>{target.id.object});
        auto disk = readBytes(session->openedBytes);
        const CosObject was = disk->object(target.id.object);
        REQUIRE(save(*session, "rename-labels"));
        auto written = readBytes(tests::readAllBytes(session->path));
        const CosObject now = written->object(target.id.object);
        CHECK(written->outline.outline.preorder().size() == before.nodeCount());
        CHECK(at(written->outline.outline, {0, 0}).title == properties.title);
        // Every key but the title, equal in value.
        for (const CosObject::Entry& entry : was.entries()) {
            INFO("key " << entry.key);
            if (entry.key == "Title") continue;
            REQUIRE(now.find(entry.key) != nullptr);
            CHECK(*now.find(entry.key) == entry.value);
        }
        CHECK(now.entries().size() == was.entries().size());
        // The other items' objects: not in the update at all.
        for (const OutlineItem* item : written->outline.outline.preorder()) {
            if (item->id == target.id) continue;
            INFO("item " << item->title.toStdString());
            CHECK(written->object(item->id.object) == disk->object(item->id.object));
        }
        CHECK(linksWrong(*written).empty());
    }
    SECTION("an item with private keys, /SE and an action chain")
    {
        auto session = tests::openBuilt(QStringLiteral("private.pdf"), privateKeys());
        REQUIRE(session);
        const core::Outline& before = session->journal->document().outline();
        REQUIRE(before.nodeCount() == 2);
        CHECK(at(before, {1}).title == QStringLiteral("Café ¤"));
        CHECK(at(before, {1}).bold);
        CHECK(at(before, {1}).italic);
        CHECK(at(before, {1}).action.kind == OutlineAction::Kind::Uri);
        for (const int index : {0, 1}) {
            const OutlineItem target = at(session->journal->document().outline(), {index});
            auto properties = core::EditBookmarkCommand::Properties::of(target);
            properties.title += QStringLiteral("!");
            REQUIRE(run(*session, std::make_unique<core::EditBookmarkCommand>(target.id, properties)));
        }
        auto disk = readBytes(session->openedBytes);
        REQUIRE(save(*session, "rename-private"));
        auto written = readBytes(tests::readAllBytes(session->path));
        for (const int index : {0, 1}) {
            const OutlineItem& item = at(written->outline.outline, {index});
            const CosObject was = disk->object(item.id.object);
            const CosObject now = written->object(item.id.object);
            for (const CosObject::Entry& entry : was.entries()) {
                INFO("item " << index << " key " << entry.key);
                if (entry.key == "Title") continue;
                REQUIRE(now.find(entry.key) != nullptr);
                CHECK(*now.find(entry.key) == entry.value);
            }
        }
        CHECK(at(written->outline.outline, {0}).title == QStringLiteral("Keep my keys!"));
        CHECK(at(written->outline.outline, {1}).title == QStringLiteral("Café ¤!"));
        // THE CONTROL: undone, the update writes nothing -- the rename is not a side effect of composing at all.
        REQUIRE(session->journal->undo());
        REQUIRE(session->journal->undo());
        CHECK(at(session->journal->document().outline(), {0}).title == QStringLiteral("Keep my keys"));
    }
}

// ==========================================================================================
// A-77 -- the hierarchy: delete, re-parent, re-order, and no cycle
// ==========================================================================================

TEST_CASE("outline.hierarchy: delete each way, re-parent, re-order, every link consistent, a cycle refused", "[outline][outlineedit][A-77]")
{
    auto session = tests::openCopy("assembly/refs-every-kind.pdf");
    REQUIRE(session);
    const auto outline = [&session]() -> const core::Outline& { return session->journal->document().outline(); };
    REQUIRE(outline().status());
    const std::vector<std::string> opened = shape(outline().roots());
    REQUIRE(opened == std::vector<std::string>{"P-0001", "  P-0002", "  P-0003", "P-0005", "  P-0006", "  P-0008", "P-0012"});
    const QByteArray hashBefore = session->journal->document().modelHash();

    SECTION("a delete with its children, and with them promoted")
    {
        const OutlineItemId first = at(outline(), {0}).id;
        REQUIRE(run(*session, std::make_unique<core::DeleteBookmarkCommand>(std::vector<OutlineItemId>{first}, false)));
        CHECK(shape(outline().roots()) == std::vector<std::string>{"P-0005", "  P-0006", "  P-0008", "P-0012"});
        REQUIRE(session->journal->undo());
        CHECK(session->journal->document().modelHash() == hashBefore);
        CHECK(deltaIsEmpty(*session));   // an undo before a save writes nothing
        REQUIRE(run(*session, std::make_unique<core::DeleteBookmarkCommand>(std::vector<OutlineItemId>{first}, true)));
        CHECK(shape(outline().roots()) == std::vector<std::string>{"P-0002", "P-0003", "P-0005", "  P-0006", "  P-0008", "P-0012"});
        REQUIRE(save(*session, "delete-promote"));
        auto written = readBytes(tests::readAllBytes(session->path));
        CHECK(shape(written->outline.outline.roots()) == shape(outline().roots()));
        CHECK(linksWrong(*written).empty());
        // AFTER THE SAVE: the undo writes the tree back as it was.
        REQUIRE(session->journal->undo());
        REQUIRE(save(*session, "delete-promote-undone"));
        auto back = readBytes(tests::readAllBytes(session->path));
        CHECK(shape(back->outline.outline.roots()) == opened);
        CHECK(linksWrong(*back).empty());
    }
    SECTION("demote, promote, move up and down")
    {
        const OutlineItemId last = at(outline(), {2}).id;
        const OutlineItemId middle = at(outline(), {1}).id;
        // Demote P-0012 under P-0005 (Tab), promote P-0006 out of it (Shift+Tab), P-0001 down one (Alt+Down).
        REQUIRE(run(*session, std::make_unique<core::ReparentBookmarkCommand>(last, OutlinePosition{middle, 2})));
        REQUIRE(run(*session, std::make_unique<core::ReparentBookmarkCommand>(at(outline(), {1, 0}).id, OutlinePosition{{}, 2})));
        REQUIRE(run(*session, std::make_unique<core::ReparentBookmarkCommand>(at(outline(), {0}).id, OutlinePosition{{}, 1})));
        const std::vector<std::string> want{"P-0005", "  P-0008", "  P-0012", "P-0001", "  P-0002", "  P-0003", "P-0006"};
        CHECK(shape(outline().roots()) == want);
        REQUIRE(save(*session, "reparent"));
        auto written = readBytes(tests::readAllBytes(session->path));
        CHECK(shape(written->outline.outline.roots()) == want);
        CHECK(linksWrong(*written).empty());
        // Every item keeps its object: a move rewrites links, it makes nothing new.
        std::set<long long> objects;
        for (const OutlineItem* item : written->outline.outline.preorder()) objects.insert(item->id.object);
        CHECK(objects == std::set<long long>{26, 27, 28, 29, 30, 31, 32});
        for (int i = 0; i < 3; ++i) REQUIRE(session->journal->undo());
        REQUIRE(save(*session, "reparent-undone"));
        CHECK(shape(readBytes(tests::readAllBytes(session->path))->outline.outline.roots()) == opened);
    }
    SECTION("a move onto its own descendant is refused, and so is onto itself")
    {
        const OutlineItemId first = at(outline(), {0}).id;
        const OutlineItemId child = at(outline(), {0, 1}).id;
        const core::Status cycle = run(*session, std::make_unique<core::ReparentBookmarkCommand>(first, OutlinePosition{child, 0}));
        REQUIRE_FALSE(cycle);
        CHECK(QString(cycle.error().messageKey) == QStringLiteral("error.outline.cycle"));
        const core::Status self = run(*session, std::make_unique<core::ReparentBookmarkCommand>(first, OutlinePosition{first, 0}));
        REQUIRE_FALSE(self);
        CHECK(QString(self.error().messageKey) == QStringLiteral("error.outline.cycle"));
        CHECK(session->journal->document().modelHash() == hashBefore);
        CHECK(deltaIsEmpty(*session));
        // THE CONTROL: the same item moved under a SIBLING's child is a tree, and runs.
        CHECK(run(*session, std::make_unique<core::ReparentBookmarkCommand>(first, OutlinePosition{at(outline(), {1, 0}).id, 0})));
    }
    SECTION("new items, at the top and under an item, numbered by the save and kept by the model's key")
    {
        const QString key = core::newBookmarkKey();
        REQUIRE(run(*session, std::make_unique<core::CreateBookmarkCommand>(OutlinePosition{{}, 3},
                                                                           std::vector<OutlineItem>{newItem(key, 0, QStringLiteral("New top"), 2)})));
        REQUIRE(run(*session, std::make_unique<core::CreateBookmarkCommand>(OutlinePosition{at(outline(), {2}).id, 0},
                                                                           std::vector<OutlineItem>{newItem(key, 1, QStringLiteral("New child"), 3)})));
        REQUIRE(save(*session, "create"));
        auto written = readBytes(tests::readAllBytes(session->path));
        CHECK(shape(written->outline.outline.roots())
              == std::vector<std::string>{"P-0001", "  P-0002", "  P-0003", "P-0005", "  P-0006", "  P-0008", "P-0012", "  New child", "New top"});
        CHECK(linksWrong(*written).empty());
        // After the save the model still names them by key, and a rename writes the object the save made.
        const OutlineItemId made = core::newBookmarkId(key, 0);
        REQUIRE(outline().find(made) != nullptr);
        auto properties = core::EditBookmarkCommand::Properties::of(*outline().find(made));
        properties.title = QStringLiteral("Renamed after the save");
        REQUIRE(run(*session, std::make_unique<core::EditBookmarkCommand>(made, properties)));
        CHECK(deltaObjects(*session) == std::vector<long long>{at(written->outline.outline, {3}).id.object});
    }
}

TEST_CASE("outline.hierarchy: an outline made in a document that had none -- the root and the catalog", "[outline][outlineedit][A-77]")
{
    auto session = tests::openCopy("assembly/refs-none.pdf");
    REQUIRE(session);
    REQUIRE(session->journal->document().outline().isEmpty());
    const QString key = core::newBookmarkKey();
    OutlineItem top = newItem(key, 0, QStringLiteral("First"), 0);
    top.children.push_back(newItem(key, 1, QStringLiteral("Inner"), 1));
    top.openByDefault = true;
    REQUIRE(run(*session, std::make_unique<core::GenerateBookmarksCommand>(OutlinePosition{}, std::vector<OutlineItem>{top},
                                                                          core::GenerateBookmarksCommand::Source::Headings)));
    REQUIRE(save(*session, "first-outline"));
    auto written = readBytes(tests::readAllBytes(session->path));
    CHECK(shape(written->outline.outline.roots()) == std::vector<std::string>{"First", "  Inner"});
    CHECK(linksWrong(*written).empty());
    CHECK(at(written->outline.outline, {0, 0}).destination.pageIndex == 1);
    // Undone after the save: the root stays, empty -- an incremental update removes nothing.
    REQUIRE(session->journal->undo());
    REQUIRE(save(*session, "first-outline-undone"));
    auto back = readBytes(tests::readAllBytes(session->path));
    CHECK(back->outline.outline.isEmpty());
    CHECK(linksWrong(*back).empty());
}

// ==========================================================================================
// A-78 -- destination, action, colour, style, open state
// ==========================================================================================

TEST_CASE("outline.properties: each target kind, the colour, the style and the open state written and read back", "[outline][outlineedit][A-78]")
{
    auto session = tests::openCopy("assembly/refs-every-kind.pdf");
    REQUIRE(session);
    const auto outline = [&session]() -> const core::Outline& { return session->journal->document().outline(); };
    const OutlineItemId first = at(outline(), {0}).id;
    const OutlineItemId second = at(outline(), {1}).id;

    std::vector<OutlineAction> actions;
    core::Destination view;
    view.pageIndex = 9;
    view.fit = core::Destination::Fit::XYZ;
    view.hasX = true;
    view.x = 72;
    view.hasY = true;
    view.y = 600;
    view.hasZoom = true;
    view.zoom = 1.5;
    actions.push_back(OutlineAction::goTo(view));
    OutlineAction remote;
    remote.kind = OutlineAction::Kind::GoToRemote;
    remote.file = QStringLiteral("other.pdf");
    remote.destination.pageIndex = 4;
    remote.destination.fit = core::Destination::Fit::Fit;
    actions.push_back(remote);
    OutlineAction launch;
    launch.kind = OutlineAction::Kind::Launch;
    launch.file = QStringLiteral("notes 報告.txt");
    actions.push_back(launch);
    OutlineAction uri;
    uri.kind = OutlineAction::Kind::Uri;
    uri.uri = QStringLiteral("https://example.com/bookmarks?x=1");
    actions.push_back(uri);
    OutlineAction script;
    script.kind = OutlineAction::Kind::JavaScript;
    script.script = QStringLiteral("app.alert('never run');");
    actions.push_back(script);
    OutlineAction print;
    print.kind = OutlineAction::Kind::Named;
    print.name = QStringLiteral("Print");
    actions.push_back(print);
    OutlineAction submit;
    submit.kind = OutlineAction::Kind::SubmitForm;
    submit.uri = QStringLiteral("https://example.com/submit");
    actions.push_back(submit);

    for (const OutlineAction& action : actions) {
        INFO("action " << core::outlineActionKindName(action.kind));
        REQUIRE(run(*session, std::make_unique<core::SetBookmarkTargetCommand>(first, action)));
        REQUIRE(save(*session, (std::string("target-") + core::outlineActionKindName(action.kind)).c_str()));
        auto written = readBytes(tests::readAllBytes(session->path));
        const OutlineItem& read = at(written->outline.outline, {0});
        CHECK(read.action.kind == action.kind);
        CHECK(read.action.file == action.file);
        CHECK(read.action.uri == action.uri);
        CHECK(read.action.script == action.script);
        CHECK(read.action.name == action.name);
        if (action.kind == OutlineAction::Kind::GoTo) {
            CHECK(read.destination == action.destination);
            // A destination is written by REFERENCE: /Dest, its page the page object, no /A beside it.
            const CosObject dictionary = written->object(read.id.object);
            CHECK(dictionary.has("Dest"));
            CHECK_FALSE(dictionary.has("A"));
            CHECK(dictionary.get("Dest").items().front().isReference());
        } else {
            CHECK_FALSE(written->object(read.id.object).has("Dest"));
            CHECK(read.destination.pageIndex == -1);   // nowhere to go in this document
        }
        if (action.kind == OutlineAction::Kind::GoToRemote) CHECK(read.action.destination.pageIndex == 4);
    }

    // Colour, bold and italic; and the open state's sign of /Count on an item with children.
    auto properties = core::EditBookmarkCommand::Properties::of(at(outline(), {1}));
    REQUIRE_FALSE(properties.open);   // P-0005 is written closed
    properties.colour = std::array<double, 3>{0.0, 0.5, 1.0};
    properties.bold = true;
    properties.italic = true;
    properties.open = true;
    REQUIRE(run(*session, std::make_unique<core::EditBookmarkCommand>(second, properties)));
    REQUIRE(save(*session, "properties"));
    auto written = readBytes(tests::readAllBytes(session->path));
    const OutlineItem& read = at(written->outline.outline, {1});
    CHECK(read.colour == properties.colour);
    CHECK(read.bold);
    CHECK(read.italic);
    CHECK(read.openByDefault);
    CHECK(written->object(read.id.object).get("Count").integerValue() == 2);
    CHECK(linksWrong(*written).empty());
    properties.open = false;
    properties.colour.reset();
    REQUIRE(run(*session, std::make_unique<core::EditBookmarkCommand>(second, properties)));
    REQUIRE(save(*session, "closed"));
    written = readBytes(tests::readAllBytes(session->path));
    CHECK(written->object(at(written->outline.outline, {1}).id.object).get("Count").integerValue() == -2);
    CHECK_FALSE(written->object(at(written->outline.outline, {1}).id.object).has("C"));
    CHECK(linksWrong(*written).empty());

    // `Other` -- a kind this build only keeps -- is refused, by key, with nothing changed.
    OutlineAction other;
    other.kind = OutlineAction::Kind::Other;
    const QByteArray hash = session->journal->document().modelHash();
    const core::Status refused = run(*session, std::make_unique<core::SetBookmarkTargetCommand>(first, other));
    REQUIRE_FALSE(refused);
    CHECK(QString(refused.error().messageKey) == QStringLiteral("error.outline.actionUnwritable"));
    CHECK(session->journal->document().modelHash() == hash);
}

// ==========================================================================================
// A-19's bookmark half: a page command carries the model's outline, and an undo puts a deleted target back
// ==========================================================================================

TEST_CASE("outline remap: a move carries every bookmark to its page, a delete leaves it unresolved and the undo puts it back", "[outline][outlineedit][A-19]")
{
    auto session = tests::openCopy("assembly/refs-every-kind.pdf");
    REQUIRE(session);
    const auto outline = [&session]() -> const core::Outline& { return session->journal->document().outline(); };
    // The label names each target by its identity: P-0001 is page 0, P-0012 page 11.
    REQUIRE(at(outline(), {0}).destination.pageIndex == 0);
    REQUIRE(at(outline(), {2}).destination.pageIndex == 11);

    // Page 0 to the end: P-0001's bookmark follows it.
    const int last = session->journal->document().pages().count() - 1;
    REQUIRE(run(*session, std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, last)));
    CHECK(at(outline(), {0}).destination.pageIndex == last);
    CHECK(at(outline(), {2}).destination.pageIndex == 10);
    CHECK(deltaObjects(*session).size() >= 1);   // the tree; no outline item -- a reference follows its page
    REQUIRE(session->journal->undo());

    // Page 11 deleted: P-0012's bookmark is unresolved, its kind kept; a rename of another item after it.
    REQUIRE(run(*session, std::make_unique<core::DeletePagesCommand>(std::vector<int>{11})));
    CHECK(at(outline(), {2}).destination.pageIndex == -1);
    CHECK(at(outline(), {2}).action.kind == OutlineAction::Kind::GoTo);
    auto properties = core::EditBookmarkCommand::Properties::of(at(outline(), {0}));
    properties.title = QStringLiteral("Renamed after the delete");
    REQUIRE(run(*session, std::make_unique<core::EditBookmarkCommand>(at(outline(), {0}).id, properties)));
    REQUIRE(save(*session, "remap-deleted"));
    auto written = readBytes(tests::readAllBytes(session->path));
    CHECK_FALSE(written->object(at(written->outline.outline, {2}).id.object).has("Dest"));   // D7-2: cleared in the file
    // AFTER THE SAVE, both undone: the bookmark lands on page 11 again, in the model and in the file.
    REQUIRE(session->journal->undo());
    REQUIRE(session->journal->undo());
    CHECK(at(outline(), {2}).destination.pageIndex == 11);
    REQUIRE(save(*session, "remap-restored"));
    auto back = readBytes(tests::readAllBytes(session->path));
    CHECK(at(back->outline.outline, {2}).destination.pageIndex == 11);
    CHECK(at(back->outline.outline, {0}).title == QStringLiteral("P-0001"));

    // A destination SET by a command, its page then deleted and saved, and both undone: the reference comes back.
    core::Destination view;
    view.pageIndex = 10;
    view.fit = core::Destination::Fit::Fit;
    const core::Status set = run(*session, std::make_unique<core::SetBookmarkTargetCommand>(at(outline(), {1}).id, OutlineAction::goTo(view)));
    INFO("set: " << (set ? std::string("ok") : QString(set.error().messageKey).toStdString() + " " + set.error().context.toStdString()));
    REQUIRE(set);
    REQUIRE(run(*session, std::make_unique<core::DeletePagesCommand>(std::vector<int>{10})));
    CHECK(at(outline(), {1}).destination.pageIndex == -1);
    properties = core::EditBookmarkCommand::Properties::of(at(outline(), {2}));
    properties.title = QStringLiteral("A rename after it");
    REQUIRE(run(*session, std::make_unique<core::EditBookmarkCommand>(at(outline(), {2}).id, properties)));
    REQUIRE(save(*session, "remap-set-deleted"));
    CHECK_FALSE(readBytes(tests::readAllBytes(session->path))->object(at(outline(), {1}).id.object).has("Dest"));
    REQUIRE(session->journal->undo());
    REQUIRE(session->journal->undo());
    CHECK(at(outline(), {1}).destination.pageIndex == 10);
    REQUIRE(save(*session, "remap-set-restored"));
    auto restored = readBytes(tests::readAllBytes(session->path));
    CHECK(at(restored->outline.outline, {1}).destination.pageIndex == 10);
    // THE CONTROL: a page past the last is refused, by key, with nothing changed.
    view.pageIndex = session->journal->document().pages().count();
    const core::Status past = run(*session, std::make_unique<core::SetBookmarkTargetCommand>(at(outline(), {1}).id, OutlineAction::goTo(view)));
    REQUIRE_FALSE(past);
    CHECK(QString(past.error().messageKey) == QStringLiteral("error.outline.badDestination"));
}

// ==========================================================================================
// A-45's bookmark half -- every malformed bookmark command refused by Result
// ==========================================================================================

TEST_CASE("CommandRegistry refuses every malformed bookmark command by Result", "[cmd][serialize][hostile][outlineedit]")
{
    const QJsonObject item{{"id", "c:" + QString(32, QLatin1Char('a')) + "-0"},
                           {"title", "T"},
                           {"action", QJsonObject{{"kind", "goTo"}, {"dest", QJsonObject{{"page", 0}, {"fit", "Fit"}}}}}};
    const auto with = [](QJsonObject json, const char* key, const QJsonValue& value) {
        json.insert(QLatin1String(key), value);
        return json;
    };
    const QJsonObject create{{"type", "bookmark.create"}, {"parent", ""}, {"index", 0}, {"items", QJsonArray{item}}};
    QJsonObject deep = item;
    for (int i = 0; i < core::kMaxOutlineDepth + 1; ++i) deep = with(item, "children", QJsonArray{deep});
    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"create: no items", with(create, "items", QJsonArray{})},
        {"create: an item a number", with(create, "items", QJsonArray{1})},
        {"create: a bad id", with(create, "items", QJsonArray{with(item, "id", "x:1")})},
        {"create: an object id of 0", with(create, "items", QJsonArray{with(item, "id", "o:0")})},
        {"create: a key in capitals", with(create, "items", QJsonArray{with(item, "id", "c:ABC")})},
        {"create: a title too long", with(create, "items", QJsonArray{with(item, "title", QString(core::kMaxOutlineTitleLength + 1, QLatin1Char('x')))})},
        {"create: an action of kind other", with(create, "items", QJsonArray{with(item, "action", QJsonObject{{"kind", "other"}})})},
        {"create: a goTo with no dest", with(create, "items", QJsonArray{with(item, "action", QJsonObject{{"kind", "goTo"}})})},
        {"create: a colour of two", with(create, "items", QJsonArray{with(item, "colour", QJsonArray{1, 0})})},
        {"create: a colour past 1", with(create, "items", QJsonArray{with(item, "colour", QJsonArray{1, 0, 2})})},
        {"create: a negative index", with(create, "index", -1)},
        {"create: a fractional index", with(create, "index", 1.5)},
        {"create: deeper than the bound", with(create, "items", QJsonArray{deep})},
        {"edit: no id", QJsonObject{{"type", "bookmark.edit"}, {"title", "T"}, {"italic", false}, {"bold", false}, {"open", false}}},
        {"edit: flags missing", QJsonObject{{"type", "bookmark.edit"}, {"id", "o:5"}, {"title", "T"}}},
        {"delete: an id twice", QJsonObject{{"type", "bookmark.delete"}, {"ids", QJsonArray{"o:5", "o:5"}}, {"keepChildren", true}}},
        {"delete: no keepChildren", QJsonObject{{"type", "bookmark.delete"}, {"ids", QJsonArray{"o:5"}}}},
        {"reparent: no index", QJsonObject{{"type", "bookmark.reparent"}, {"id", "o:5"}, {"parent", ""}}},
        {"setTarget: no action", QJsonObject{{"type", "bookmark.setTarget"}, {"id", "o:5"}}},
        {"setTarget: a dest page of -2",
         QJsonObject{{"type", "bookmark.setTarget"}, {"id", "o:5"},
                     {"action", QJsonObject{{"kind", "goTo"}, {"dest", QJsonObject{{"page", -2}, {"fit", "Fit"}}}}}}},
        {"setTarget: an unknown fit",
         QJsonObject{{"type", "bookmark.setTarget"}, {"id", "o:5"},
                     {"action", QJsonObject{{"kind", "goTo"}, {"dest", QJsonObject{{"page", 1}, {"fit", "Zoom"}}}}}}},
        {"generate: an unknown source", with(with(create, "type", "bookmark.generate"), "source", "dreams")},
    };
    for (const auto& [label, json] : hostile) {
        INFO("input: " << label);
        auto result = core::CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
    }
    // THE CONTROL: the good lines are read.
    CHECK(core::CommandRegistry::deserialize(create));
    CHECK(core::CommandRegistry::deserialize(with(with(create, "type", "bookmark.generate"), "source", "import")));
}

// D7-10: an outline whose readings disagree refuses every bookmark command, by key, before anything changes.
TEST_CASE("an outline read-only for its readings refuses every bookmark command by key", "[outline][outlineedit][A-75]")
{
    auto session = tests::openBuilt(QStringLiteral("private.pdf"), privateKeys(),
                                    [](tests::CosBackend& backend) { backend.plantOutlineDisagreement(); });
    REQUIRE(session);
    const core::Outline& outline = session->journal->document().outline();
    REQUIRE_FALSE(outline.status());
    const QByteArray hash = session->journal->document().modelHash();
    auto properties = core::EditBookmarkCommand::Properties::of(at(outline, {0}));
    properties.title = QStringLiteral("X");
    const core::Status refused = run(*session, std::make_unique<core::EditBookmarkCommand>(at(outline, {0}).id, properties));
    REQUIRE_FALSE(refused);
    CHECK(QString(refused.error().messageKey) == QStringLiteral("error.outline.readingsDisagree"));
    CHECK(session->journal->document().modelHash() == hash);
    CHECK(deltaIsEmpty(*session));
}

// ==========================================================================================
// A-81 -- import and export as indented text and as XML
// ==========================================================================================

namespace {

// The tree with each destination: "  title -> page (fit)".
std::vector<std::string> withTargets(const std::vector<OutlineItem>& items, int depth = 0)
{
    std::vector<std::string> out;
    for (const OutlineItem& item : items) {
        out.push_back(std::string(static_cast<std::size_t>(depth) * 2, ' ') + item.title.toStdString() + " -> "
                      + std::to_string(item.destination.pageIndex) + " " + core::destinationFitName(item.destination.fit));
        for (std::string& child : withTargets(item.children, depth + 1)) out.push_back(std::move(child));
    }
    return out;
}

void clearOutline(tests::SaveSession& session)
{
    std::vector<OutlineItemId> roots;
    for (const OutlineItem& item : session.journal->document().outline().roots()) roots.push_back(item.id);
    REQUIRE(run(session, std::make_unique<core::DeleteBookmarkCommand>(roots, false)));
    REQUIRE(session.journal->document().outline().isEmpty());
}

} // namespace

TEST_CASE("outline.exchange: export then import gives the same tree, in text and in XML", "[outline][outlineedit][A-81]")
{
    for (const core::OutlineFormat format : {core::OutlineFormat::Text, core::OutlineFormat::Xml}) {
        INFO((format == core::OutlineFormat::Text ? "text" : "xml"));
        auto session = tests::openCopy("assembly/refs-every-kind.pdf");
        REQUIRE(session);
        // A style, a colour and an action too -- which XML carries and text does not.
        const auto& outline = [&session]() -> const core::Outline& { return session->journal->document().outline(); };
        auto properties = core::EditBookmarkCommand::Properties::of(at(outline(), {2}));
        properties.bold = true;
        properties.colour = std::array<double, 3>{1.0, 0.0, 0.0};
        REQUIRE(run(*session, std::make_unique<core::EditBookmarkCommand>(at(outline(), {2}).id, properties)));
        const std::vector<std::string> before = withTargets(outline().roots());
        const QString path = session->directory.filePath(format == core::OutlineFormat::Text ? QStringLiteral("bookmarks.txt")
                                                                                              : QStringLiteral("bookmarks.xml"));
        REQUIRE(session->backend.writeOutlineExchange(outline().roots(), format, path, false));
        // A FILE THERE is replaced only when asked.
        const core::Status again = session->backend.writeOutlineExchange(outline().roots(), format, path, false);
        REQUIRE_FALSE(again);
        CHECK(QString(again.error().messageKey) == QStringLiteral("error.outline.exportExists"));

        clearOutline(*session);
        auto items = session->backend.readOutlineExchange(path, session->journal->document().pages().count(), core::newBookmarkKey());
        REQUIRE(items);
        REQUIRE(run(*session, std::make_unique<core::GenerateBookmarksCommand>(OutlinePosition{}, std::move(items).value(),
                                                                              core::GenerateBookmarksCommand::Source::Import)));
        CHECK(withTargets(outline().roots()) == before);
        for (const OutlineItem* item : outline().preorder()) CHECK(item->id.isCreated());
        if (format == core::OutlineFormat::Xml) {
            CHECK(at(outline(), {2}).bold);
            CHECK(at(outline(), {2}).colour == std::array<double, 3>{1.0, 0.0, 0.0});
            CHECK(at(outline(), {1, 1}).action.kind == OutlineAction::Kind::GoTo);
        }
        REQUIRE(save(*session, format == core::OutlineFormat::Text ? "import-text" : "import-xml"));
    }
}

TEST_CASE("outline.exchange: a malformed file is refused by key with nothing changed, and no XML entity is ever resolved", "[outline][outlineedit][A-81]")
{
    auto session = tests::openCopy("assembly/refs-every-kind.pdf");
    REQUIRE(session);
    const QByteArray hash = session->journal->document().modelHash();
    const auto write = [&session](const QString& name, const QByteArray& bytes) {
        const QString path = session->directory.filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        return path;
    };
    // The secret a planted entity names: were it ever read, its words would be a title.
    const QString secret = write(QStringLiteral("secret.txt"), "NEVER-READ-SECRET");
    const QByteArray entity = "<?xml version=\"1.0\"?>\n<!DOCTYPE bookmarks [<!ENTITY leak SYSTEM \"file:///"
                              + QDir::fromNativeSeparators(secret).toUtf8() + "\">]>\n<bookmarks version=\"1\"><bookmark title=\"&leak;\" page=\"1\"/></bookmarks>\n";
    const std::vector<std::pair<const char*, QByteArray>> hostile{
        {"a depth that skips a level", "RawPage bookmarks 1\nTop\t1\tFit\n\t\tToo deep\t1\tFit\n"},
        {"a page that is no number", "Top\tone\tFit\n"},
        {"a view that is no view", "Top\t1\tZoom 3\n"},
        {"two fields", "Top\t1\n"},
        {"an external entity", entity},
        {"an internal entity", "<!DOCTYPE bookmarks [<!ENTITY a \"x\">]><bookmarks><bookmark title=\"&a;\"/></bookmarks>"},
        {"an element that is no bookmark", "<bookmarks><chapter title=\"x\"/></bookmarks>"},
        {"a colour that is no colour", "<bookmarks><bookmark title=\"x\" color=\"red\"/></bookmarks>"},
        {"unclosed", "<bookmarks><bookmark title=\"x\">"},
        {"an action of kind other", "<bookmarks><bookmark title=\"x\" action=\"other\"/></bookmarks>"},
    };
    int index = 0;
    for (const auto& [label, bytes] : hostile) {
        INFO(label);
        const QString path = write(QStringLiteral("hostile-%1.txt").arg(index++), bytes);
        auto items = session->backend.readOutlineExchange(path, 12, core::newBookmarkKey());
        REQUIRE_FALSE(items);
        CHECK(QString(items.error().messageKey) == QStringLiteral("error.outline.importMalformed"));
    }
    CHECK(session->journal->document().modelHash() == hash);
    // THE CONTROL: the same file with its entity spelled out reads.
    const QString good = write(QStringLiteral("good.xml"), "<bookmarks version=\"1\"><bookmark title=\"x\" page=\"13\" view=\"Fit\"/></bookmarks>");
    auto read = session->backend.readOutlineExchange(good, 12, core::newBookmarkKey());
    REQUIRE(read);
    CHECK(read.value().front().title == QStringLiteral("x"));
    CHECK_FALSE(read.value().front().destination.isResolved());   // page 13 of 12: kept, going nowhere
}
