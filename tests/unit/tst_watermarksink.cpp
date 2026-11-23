// tst_watermarksink -- Stage 7 T15 (`Stage7-Assembly-Plan.md` D7-7, D7-45): watermarks through the SHIPPED SINK and save,
// read back first-party (`Stage7-Assembly-Testing-Guide.md` §3.8, §5.17 -- A-100 whole, A-101's first-party half):
// applied, saved, removed and saved leave every page, its `/Contents`, its `/Resources` AND the catalog's
// `/OCProperties` equal to the original's in value over every editable corpus file; an update replaces only the one
// chosen and may keep its picture BY REFERENCE; a print- or screen-only watermark's group is listed while drawn and taken
// out with it; another producer's watermark is neither listed nor taken; an image becomes a page; the journal lines.
//
// WHAT IT DOES NOT COVER (convention 19): the INK -- that the watermark draws where it is placed, at its opacity, behind
// or in front, and only when printing or only on screen, is PDFium's to show (`tests/render/tst_watermarkengine`) and
// Acrobat's (M-09). And the surfaces: every command is constructed here.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/DecorationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/PageTree.h"
#include "core/doc/Stamps.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/DecorationOps.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/PageOps.h"

#include <QDir>
#include <QJsonArray>
#include <QFile>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using pdf::cos::CosObject;
namespace fs = std::filesystem;

namespace {

struct Read {
    QByteArray bytes;
    pdf::cos::XrefTable table;
    pdf::ops::PageWalk walk;

    CosObject object(long long number)
    {
        auto fetched = table.fetch(number);
        REQUIRE(fetched);
        CosObject out = fetched.value().object;
        out.clearSpansRecursively();
        return out;
    }
    CosObject resolve(const CosObject& value)
    {
        if (!value.isReference()) return value;
        auto fetched = table.fetch(value.reference().number);
        if (!fetched || fetched.value().object.isStream()) return value;
        CosObject out = fetched.value().object;
        out.clearSpansRecursively();
        return out;
    }
    CosObject page(int index) { return object(walk.pages[static_cast<std::size_t>(index)].number); }
    CosObject catalog() { return object(table.trailer().get("Root").reference().number); }
    pdf::ops::PageOps::ContentLayers layers(int index) { return pdf::ops::PageOps::contentLayers(table, page(index).get("Contents")); }
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

// §3.8's comparison, and the catalog's: each page, its `/Contents` and `/Resources` resolved one level, and the
// catalog with its `/OCProperties` and that dictionary's `/D` resolved -- what a watermark's group touches.
struct State {
    std::vector<std::array<CosObject, 3>> pages;
    CosObject catalog;
    CosObject properties;
    CosObject defaults;
    bool operator==(const State&) const = default;
};
State snapshot(Read& read)
{
    State out;
    for (int i = 0; i < static_cast<int>(read.walk.pages.size()); ++i) {
        const CosObject page = read.page(i);
        out.pages.push_back({page, read.resolve(page.get("Contents")), read.resolve(page.get("Resources"))});
    }
    out.catalog = read.catalog();
    out.properties = read.resolve(out.catalog.get("OCProperties"));
    out.defaults = read.resolve(out.properties.get("D"));
    return out;
}

core::Status saveIt(tests::SaveSession& session)
{
    auto saved = session.coordinator->save(*session.journal, session.path);
    if (!saved) return saved.error();
    if (saved.value().kind == core::SaveOutcome::Kind::SaveAsRequired) {
        const QString target = session.directory.filePath(QStringLiteral("saved-as.pdf"));
        auto as = session.coordinator->saveAs(*session.journal, target);
        if (!as) return as.error();
        session.path = target;
    }
    return core::Status{};
}

void keepWritten(const tests::SaveSession& session, const QString& name)
{
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    REQUIRE(QDir().mkpath(dir));
    const QString target = QDir(dir).filePath(QStringLiteral("watermark-%1.pdf").arg(name));
    QFile::remove(target);
    REQUIRE(QFile::copy(session.path, target));
}

core::StampImage picture(int width, int height, bool alpha)
{
    QByteArray rgb(width * height * 3, '\0');
    for (int i = 0; i < width * height; ++i) {
        rgb[i * 3] = static_cast<char>(i % 251);
        rgb[i * 3 + 1] = static_cast<char>(40);
        rgb[i * 3 + 2] = static_cast<char>(200);
    }
    QByteArray mask;
    if (alpha) mask = QByteArray(width * height, '\x80');
    auto image = core::stampImageFromPixels(width, height, rgb, mask);
    REQUIRE(image);
    return image.value();
}

core::WatermarkSpec textMark(core::WatermarkUsage usage = core::WatermarkUsage::Always)
{
    core::WatermarkSpec spec;
    spec.text = QStringLiteral("DRAFT");
    spec.size = 48;
    spec.usage = usage;
    return spec;
}

core::WatermarkSpec imageMark()
{
    core::WatermarkSpec spec;
    spec.source = core::WatermarkSource::Image;
    spec.image = picture(40, 20, true);
    spec.behind = false;
    spec.tile = true;
    spec.tileGap = 30;
    spec.scaleMode = core::WatermarkScale::Absolute;
    spec.scale = 1.0;
    spec.opacity = 0.4;
    spec.sourceName = QStringLiteral("dots.png");
    return spec;
}

std::vector<fs::path> editableCorpus()
{
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        const fs::path relative = fs::relative(entry.path(), RAWPAGE_CORPUS_DIR);
        if (relative.begin()->string() == "encrypted") continue;
        if (relative.generic_string() == "malformed/pages-loop.pdf") continue;
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<core::DecorationEntry> listed(tests::SaveSession& session)
{
    auto entries = session.backend.decorations(session.handle, core::DecorationKind::Watermark);
    REQUIRE(entries);
    return entries.value();
}

bool deltaIsEmpty(tests::SaveSession& session)
{
    auto delta = session.sink()->delta();
    REQUIRE(delta);
    return delta.value().isEmpty();
}

// The groups `/OCProperties` lists that carry our mark.
std::vector<CosObject> ourGroups(Read& read)
{
    std::vector<CosObject> out;
    const CosObject properties = read.resolve(read.catalog().get("OCProperties"));
    const CosObject groups = read.resolve(properties.get("OCGs"));
    for (const CosObject& item : groups.items()) {
        if (item.isReference() && pdf::ops::DecorationOps::markedId(read.object(item.reference().number))) out.push_back(read.object(item.reference().number));
    }
    return out;
}

} // namespace

// ==========================================================================================
// A-100 -- apply, then remove: byte-identical (§3.8, first-party half), the catalog included
// ==========================================================================================

TEST_CASE("decoration.roundtrip: watermarks applied, saved, one removed by choice, the rest removed, leave every page and the catalog as they were, over every editable corpus file", "[decoration][watermark][A-100]")
{
    std::size_t files = 0;
    std::vector<std::string> notEditable;
    std::vector<std::string> failures;
    for (const fs::path& relative : editableCorpus()) {
        const std::string label = relative.generic_string();
        auto session = tests::openCopy(label);
        if (!session || !session->journal->document().permissions().may(core::Operation::EditPages)) {
            notEditable.push_back(label);
            continue;
        }
        auto original = readBytes(session->openedBytes);
        const State before = snapshot(*original);
        const auto step = [&](const char* what, core::Status status) {
            if (!status) failures.push_back(label + " " + what + ": " + status.error().messageKey.data() + " " + status.error().context.toStdString());
            return static_cast<bool>(status);
        };
        // A print-only text one behind, and a tiled image one in front -- each its own key.
        if (!step("text", session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(textMark(core::WatermarkUsage::PrintOnly)))))
            continue;
        ++files;
        if (!step("save text", saveIt(*session))) continue;
        if (!step("image", session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(imageMark())))) continue;
        if (!step("save image", saveIt(*session))) continue;
        const std::vector<core::DecorationEntry> both = listed(*session);
        if (both.size() != 2 || both[0].pages.size() != before.pages.size()) {
            failures.push_back(label + ": two watermarks on every page not listed");
            continue;
        }
        {
            auto two = readBytes(tests::readAllBytes(session->path));
            const auto layers = two->layers(0);
            if (layers.behind.size() != 1 || layers.front.size() != 1) failures.push_back(label + ": not one behind and one in front");
            if (ourGroups(*two).size() != 1) failures.push_back(label + ": the print-only group not listed once");
        }
        // REMOVE THE ONE CHOSEN -- the text one, with its group.
        const QString textId = both[0].spec.value(QStringLiteral("source")).toString() == QLatin1String("text") ? both[0].id : both[1].id;
        if (!step("remove one", session->journal->run(std::make_unique<core::RemoveWatermarkCommand>(std::vector<QString>{textId})))) continue;
        if (!step("save one removed", saveIt(*session))) continue;
        {
            auto one = readBytes(tests::readAllBytes(session->path));
            const auto layers = one->layers(0);
            if (!layers.behind.empty() || layers.front.size() != 1) failures.push_back(label + ": removing the chosen one took the other");
            if (!ourGroups(*one).empty()) failures.push_back(label + ": the removed watermark's group is still listed");
        }
        if (!step("remove all", session->journal->run(std::make_unique<core::RemoveWatermarkCommand>()))) continue;
        if (!step("save all removed", saveIt(*session))) continue;
        auto after = readBytes(tests::readAllBytes(session->path));
        const State now = snapshot(*after);
        if (now.pages.size() != before.pages.size()) {
            failures.push_back(label + ": the page count changed");
            continue;
        }
        for (std::size_t i = 0; i < now.pages.size(); ++i) {
            for (std::size_t k = 0; k < 3; ++k) {
                if (!(now.pages[i][k] == before.pages[i][k]))
                    failures.push_back(label + " page " + std::to_string(i + 1) + ": " + (k == 0 ? "the page" : k == 1 ? "/Contents" : "/Resources") + " differs");
            }
        }
        if (!(now.catalog == before.catalog)) failures.push_back(label + ": the catalog differs");
        if (!(now.properties == before.properties)) failures.push_back(label + ": /OCProperties differs");
        if (!(now.defaults == before.defaults)) failures.push_back(label + ": /OCProperties /D differs");
        if (label == "assembly/layers.pdf") keepWritten(*session, QStringLiteral("roundtrip-layers"));
    }
    UNSCOPED_INFO(files << " files watermarked and cleared; not editable: " << notEditable.size());
    for (const std::string& f : failures) UNSCOPED_INFO(f);
    CHECK(failures.empty());
    CHECK(files >= 100);
}

TEST_CASE("decoration.roundtrip: an update replaces only the watermark chosen, keeps its picture by reference, and one undo puts it back", "[decoration][watermark][A-100]")
{
    auto session = tests::openCopy("ordinary/mixed-page-sizes.pdf");
    REQUIRE(session);
    const State before = snapshot(*readBytes(session->openedBytes));
    REQUIRE(session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(imageMark())));
    REQUIRE(session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(textMark())));
    REQUIRE(saveIt(*session));
    const std::vector<core::DecorationEntry> two = listed(*session);
    REQUIRE(two.size() == 2);
    const core::DecorationEntry& image = two[0].spec.value(QStringLiteral("source")).toString() == QLatin1String("image") ? two[0] : two[1];
    const core::DecorationEntry& text = &image == &two[0] ? two[1] : two[0];
    auto saved = readBytes(tests::readAllBytes(session->path));
    const auto sourceOn = [](Read& read, int page, const QString& id) {
        const auto form = pdf::ops::DecorationOps::formOf(read.table, read.page(page).get("Resources"), id);
        REQUIRE(form);
        const CosObject dictionary = read.object(form->reference().number);
        return read.resolve(dictionary.get("Resources")).get("XObject").get(pdf::ops::DecorationOps::kSourceResource);
    };
    const CosObject picture = sourceOn(*saved, 0, image.id);
    REQUIRE(picture.isReference());

    // THE UPDATE: the image one made screen-only, half as opaque, its picture KEPT -- read from its record.
    auto recorded = core::watermarkFromJson(image.spec, false);
    REQUIRE(recorded);
    core::WatermarkSpec update = recorded.value();
    update.keepSourceOf = image.id;
    update.opacity = 0.2;
    update.usage = core::WatermarkUsage::ScreenOnly;
    REQUIRE(session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(update, std::vector<QString>{image.id})));
    REQUIRE(saveIt(*session));
    const std::vector<core::DecorationEntry> after = listed(*session);
    REQUIRE(after.size() == 2);
    const bool textKept = std::any_of(after.begin(), after.end(), [&](const core::DecorationEntry& e) { return e.id == text.id; });
    CHECK(textKept);
    const auto fresh = std::find_if(after.begin(), after.end(), [&](const core::DecorationEntry& e) { return e.id != text.id; });
    REQUIRE(fresh != after.end());
    CHECK(fresh->id != image.id);
    CHECK(fresh->spec.value(QStringLiteral("usage")).toString() == QLatin1String("screen"));
    auto updated = readBytes(tests::readAllBytes(session->path));
    for (int page = 0; page < static_cast<int>(updated->walk.pages.size()); ++page) {
        INFO("page " << page + 1);
        CHECK(sourceOn(*updated, page, fresh->id) == picture);   // the very image object -- not written again
    }
    REQUIRE(ourGroups(*updated).size() == 1);
    const CosObject group = ourGroups(*updated).front();
    CHECK(group.get("Usage").get("View").get("ViewState").text() == "ON");
    CHECK(group.get("Usage").get("Print").get("PrintState").text() == "OFF");

    // One undo: the image one back as it was saved, the update's group gone.
    REQUIRE(session->journal->undo());
    REQUIRE(saveIt(*session));
    auto undone = readBytes(tests::readAllBytes(session->path));
    CHECK(sourceOn(*undone, 0, image.id) == picture);
    CHECK(ourGroups(*undone).empty());
    // And the two applies undone after their save: the pages as they were.
    REQUIRE(session->journal->undo());
    REQUIRE(session->journal->undo());
    REQUIRE(saveIt(*session));
    CHECK(snapshot(*readBytes(tests::readAllBytes(session->path))) == before);
    keepWritten(*session, QStringLiteral("update-undone"));
}

TEST_CASE("decoration.roundtrip: an update keeping an UNSAVED picture draws the bytes it carried, and an update of a watermark gone is refused", "[decoration][watermark][A-100]")
{
    auto session = tests::openCopy("ordinary/text-letter.pdf");
    REQUIRE(session);
    REQUIRE(session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(imageMark())));
    const std::vector<core::DecorationEntry> one = listed(*session);
    REQUIRE(one.size() == 1);
    auto recorded = core::watermarkFromJson(one.front().spec, false);
    REQUIRE(recorded);
    core::WatermarkSpec update = recorded.value();
    update.keepSourceOf = one.front().id;
    update.rotation = 30;
    REQUIRE(session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(update, std::vector<QString>{one.front().id})));
    REQUIRE(saveIt(*session));
    auto saved = readBytes(tests::readAllBytes(session->path));
    REQUIRE(saved->layers(0).front.size() == 1);
    // ONE image object in the file, however many pages and copies draw it.
    int images = 0;
    for (long long n = 1; n <= saved->table.largestObjectNumber(); ++n) {
        auto fetched = saved->table.fetch(n);
        if (fetched && fetched.value().object.isStream() && fetched.value().object.get("Subtype").isName()
            && fetched.value().object.get("Subtype").text() == "Image" && !fetched.value().object.get("SMask").isNull())
            ++images;
    }
    CHECK(images == 1);
    // An update naming a watermark the document no longer has: refused by key, nothing changed, no undo step.
    REQUIRE(session->journal->run(std::make_unique<core::RemoveWatermarkCommand>()));
    const std::size_t depth = session->journal->historySize();
    const core::Status gone = session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(update, std::vector<QString>{one.front().id}));
    REQUIRE_FALSE(gone);
    CHECK(gone.error().messageKey == QLatin1String("error.decorate.noneFound"));
    CHECK(session->journal->historySize() == depth);
    // Keeping the picture of a watermark nowhere to be found: refused by its own key.
    update.keepSourceOf = QString(32, QLatin1Char('f'));
    const core::Status noSource = session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(update));
    REQUIRE_FALSE(noSource);
    CHECK(noSource.error().messageKey == QLatin1String("error.decorate.sourceGone"));
}

TEST_CASE("watermark.usage: a print-only group is listed with its usage and both events, beside a document's own layers, and leaves with the watermark", "[decoration][watermark][A-101]")
{
    auto session = tests::openCopy("assembly/layers.pdf");
    REQUIRE(session);
    auto original = readBytes(session->openedBytes);
    const State before = snapshot(*original);
    const std::size_t theirs = before.properties.get("OCGs").items().size();
    const std::size_t theirEvents = before.defaults.get("AS").items().size();
    REQUIRE(theirs > 0);
    REQUIRE(session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(textMark(core::WatermarkUsage::PrintOnly))));
    REQUIRE(saveIt(*session));
    auto saved = readBytes(tests::readAllBytes(session->path));
    const State with = snapshot(*saved);
    CHECK(with.properties.get("OCGs").items().size() == theirs + 1);
    REQUIRE(ourGroups(*saved).size() == 1);
    const CosObject group = ourGroups(*saved).front();
    CHECK(group.get("Usage").get("Print").get("PrintState").text() == "ON");
    CHECK(group.get("Usage").get("View").get("ViewState").text() == "OFF");
    CHECK(group.get("Name").text() == "Watermark");
    // Two applications appended -- View and Print, each naming our group alone -- and theirs as they were.
    const auto& events = with.defaults.get("AS").items();
    REQUIRE(events.size() == theirEvents + 2);
    for (std::size_t i = 0; i < theirEvents; ++i) CHECK(events[i] == before.defaults.get("AS").items()[i]);
    CHECK(events[theirEvents].get("Event").text() == "View");
    CHECK(events[theirEvents + 1].get("Event").text() == "Print");
    CHECK(events[theirEvents + 1].get("OCGs").items().size() == 1);
    // NOT in `/Order`: no person's layer.
    CHECK(with.defaults.get("Order") == before.defaults.get("Order"));
    // The form names the group.
    const auto id = listed(*session).front().id;
    const auto form = pdf::ops::DecorationOps::formOf(saved->table, saved->page(0).get("Resources"), id);
    REQUIRE(form);
    CHECK(saved->object(form->reference().number).get("OC").isReference());

    REQUIRE(session->journal->run(std::make_unique<core::RemoveWatermarkCommand>()));
    REQUIRE(saveIt(*session));
    CHECK(snapshot(*readBytes(tests::readAllBytes(session->path))) == before);
}

TEST_CASE("watermark.usage: on a document with no layers the group's /OCProperties is made with an empty /Order and taken away whole", "[decoration][watermark][A-101]")
{
    auto session = tests::openCopy("ordinary/text-letter.pdf");
    REQUIRE(session);
    const State before = snapshot(*readBytes(session->openedBytes));
    REQUIRE(before.properties.isNull());
    REQUIRE(session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(textMark(core::WatermarkUsage::ScreenOnly))));
    // Before a save, an undo writes nothing at all.
    REQUIRE(session->journal->undo());
    CHECK(deltaIsEmpty(*session));
    REQUIRE(session->journal->redo());
    REQUIRE(saveIt(*session));
    auto saved = readBytes(tests::readAllBytes(session->path));
    const State with = snapshot(*saved);
    REQUIRE(with.properties.isDictionary());
    CHECK(with.properties.get("OCGs").items().size() == 1);
    CHECK(with.defaults.get("Order").isArray());
    CHECK(with.defaults.get("Order").items().empty());
    REQUIRE(session->journal->run(std::make_unique<core::RemoveWatermarkCommand>()));
    REQUIRE(saveIt(*session));
    CHECK(snapshot(*readBytes(tests::readAllBytes(session->path))) == before);
    // The removal undone AFTER that save: the group listed again -- the same object.
    REQUIRE(session->journal->undo());
    REQUIRE(saveIt(*session));
    auto back = readBytes(tests::readAllBytes(session->path));
    REQUIRE(ourGroups(*back).size() == 1);
    CHECK(ourGroups(*back).front() == ourGroups(*saved).front());
}

TEST_CASE("decoration.foreign: another producer's watermark is neither listed nor removed", "[decoration][watermark][A-100]")
{
    auto session = tests::openCopy("assembly/decorated-elsewhere.pdf");
    REQUIRE(session);
    CHECK(listed(*session).empty());
    const core::Status none = session->journal->run(std::make_unique<core::RemoveWatermarkCommand>());
    REQUIRE_FALSE(none);
    CHECK(none.error().messageKey == QLatin1String("error.decorate.noneFound"));
    CHECK_FALSE(session->journal->canUndo());
    CHECK(deltaIsEmpty(*session));
}

TEST_CASE("watermark: an image becomes one page the image's size at 96 to the inch, inserted as any page is", "[watermark][A-100]")
{
    auto payload = pdf::ops::ObjectGraphCopier::imagePage(picture(400, 200, true));
    REQUIRE(payload);
    CHECK(payload.value().pageCount == 1);
    auto session = tests::openCopy("ordinary/text-letter.pdf");
    REQUIRE(session);
    const int count = session->journal->document().pages().count();
    REQUIRE(session->journal->run(std::make_unique<core::InsertPagesCommand>(count, payload.value())));
    REQUIRE(saveIt(*session));
    auto saved = readBytes(tests::readAllBytes(session->path));
    REQUIRE(static_cast<int>(saved->walk.pages.size()) == count + 1);
    const CosObject page = saved->page(count);
    const auto& box = page.get("MediaBox").items();
    REQUIRE(box.size() == 4);
    CHECK(box[2].numberValue() == 300.0);
    CHECK(box[3].numberValue() == 150.0);
    const CosObject image = saved->object(saved->resolve(page.get("Resources")).get("XObject").get("Im0").reference().number);
    CHECK(image.get("Width").integerValue() == 400);
    CHECK(image.get("SMask").isReference());
    keepWritten(*session, QStringLiteral("image-page"));
    // An image no writer here takes: refused by key.
    CHECK(pdf::ops::ObjectGraphCopier::imagePage(core::StampImage{}).error().messageKey == QLatin1String("error.edit.stampImageUnsupported"));
}

TEST_CASE("watermark commands: each journal line reads back as the command it was, and a hostile one is refused", "[decoration][watermark][serialize:ApplyWatermarkCommand][serialize:RemoveWatermarkCommand]")
{
    const QString key(32, QLatin1Char('c'));
    const core::ApplyWatermarkCommand apply(imageMark(), {QString(32, QLatin1Char('a'))}, key);
    auto back = core::CommandRegistry::deserialize(apply.serialize());
    REQUIRE(back);
    const auto* same = dynamic_cast<const core::ApplyWatermarkCommand*>(back.value().get());
    REQUIRE(same != nullptr);
    CHECK(same->spec() == apply.spec());
    CHECK(same->replaced() == apply.replaced());
    CHECK(same->key() == key);
    CHECK(same->serialize() == apply.serialize());
    const core::RemoveWatermarkCommand remove({QString(32, QLatin1Char('d'))}, key);
    // The ids chosen ARE the line -- read back, not only compared with a line that might have lost them too.
    CHECK(remove.serialize().value(QStringLiteral("ids")).toArray() == QJsonArray{QString(32, QLatin1Char('d'))});
    CHECK(apply.serialize().value(QStringLiteral("replace")).toArray() == QJsonArray{QString(32, QLatin1Char('a'))});
    auto removed = core::CommandRegistry::deserialize(remove.serialize());
    REQUIRE(removed);
    CHECK(removed.value()->serialize() == remove.serialize());
    const auto* removeBack = dynamic_cast<const core::RemoveWatermarkCommand*>(removed.value().get());
    REQUIRE(removeBack != nullptr);
    CHECK(removeBack->state().only() == std::vector<QString>{QString(32, QLatin1Char('d'))});

    QJsonObject hostile = apply.serialize();
    hostile.insert(QStringLiteral("replace"), QJsonArray{QStringLiteral("not hex")});
    CHECK_FALSE(core::CommandRegistry::deserialize(hostile));
    hostile = apply.serialize();
    QJsonObject spec = hostile.value(QStringLiteral("spec")).toObject();
    spec.remove(QStringLiteral("image"));   // a picture with no bytes and none kept
    hostile.insert(QStringLiteral("spec"), spec);
    CHECK_FALSE(core::CommandRegistry::deserialize(hostile));
    hostile = remove.serialize();
    hostile.insert(QStringLiteral("ids"), QJsonArray{key, key});   // twice
    CHECK_FALSE(core::CommandRegistry::deserialize(hostile));
}
