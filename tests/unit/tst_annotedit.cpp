// tst_annotedit -- annotations changed IN PLACE. Stage 6 T5, guide cases A-26 (through the command), A-28
// and A-29 for the four new commands, A-43's headless half (D6-27's refusals), and decision D6-30's
// geometry (`Stage6-Annotations-Testing-Guide.md` §5.5, §5.7).
//
// WHAT THIS SUITE IS TRYING TO CATCH: an edit that drops a key nobody here models, or redraws an
// appearance nobody changed (D6-5); a move that leaves the geometry behind the rectangle; an edit undone
// before a save that still writes; a direct dictionary edited and lost, or listed twice; a lock that does
// not hold, or one that cannot be taken off; a serialized command a hostile journal can make lie; a hit
// test that answers for a rectangle rather than for a shape.
//
// WHAT IT DOES NOT COVER (convention 19):
//   * the ENGINE. The backend is `tests/unit/CosBackend.h`; that PDFium draws an edited annotation where
//     the saved file draws it is `tests/render/tst_annotdisplay.cpp`'s (A-34's edits), and that a hit test
//     finds an annotation where its INK is is `tests/composition/tst_annotink.cpp`'s (A-41) -- the hand
//     values below say what each shape is, not that the shape is where a reader draws it;
//   * the GESTURE. That one drag is one command, and a burst of nudges one move, is the overlay's
//     (`tests/ui/tst_annotoverlay.cpp`); here each command is constructed;
//   * `qpdf`: every saved file is copied to RAWPAGE_WRITTEN_DIR for the portable-asan job (A-31).

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/AnnotationGeometry.h"
#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/EmbeddedFileReader.h"
#include "pdf/ops/RevisionWriter.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using core::Annotation;
namespace AnnotationFlag = core::AnnotationFlag;
using core::AnnotationId;
using core::AnnotationSubtype;
using core::EditAnnotationCommand;
using core::LockAnnotationCommand;
using core::MoveAnnotationCommand;
using core::SetAnnotationAppearanceCommand;
using pdf::cos::CosObject;

namespace {

constexpr std::int64_t kNow = 1791590400;   // 2026-10-10T00:00:00Z

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QByteArray save(tests::SaveSession& session, const QString& keepAs)
{
    auto result = session.coordinator->save(*session.journal, session.path);
    INFO("save error: " << (result ? std::string() : keyOf(result.error()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    REQUIRE(result.value().kind == core::SaveOutcome::Kind::Saved);
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    REQUIRE(QDir().mkpath(dir));
    const QString target = QDir(dir).filePath(keepAs);
    QFile::remove(target);
    REQUIRE(QFile::copy(session.path, target));
    return tests::readAllBytes(session.path);
}

std::vector<long long> objectsTheUpdateWrites(tests::SaveSession& session)
{
    auto composed = pdf::ops::RevisionWriter::compose(*session.sink(), false);
    REQUIRE(composed);
    return composed.value().objectsWritten;
}

bool deltaIsEmpty(tests::SaveSession& session)
{
    auto delta = session.sink()->delta();
    REQUIRE(delta);
    return delta.value().isEmpty();
}

// A file read back FROM ITS BYTES with the first-party reader.
struct ReadBack {
    QByteArray bytes;
    std::optional<pdf::cos::XrefTable> table;
    pdf::ops::PageWalk walk;
    core::AnnotationStore store;

    explicit ReadBack(QByteArray fileBytes) : bytes(std::move(fileBytes))
    {
        auto opened = pdf::cos::openWithRepair(viewOf(bytes));
        REQUIRE(opened);
        table.emplace(std::move(opened.value().table));
        auto walked = pdf::ops::PageOps::walk(*table);
        REQUIRE(walked);
        walk = std::move(walked).value();
        store = core::AnnotationStore::fromReading(pdf::ops::AnnotationReader::read(*table, walk));
    }
    CosObject object(long long number)
    {
        auto fetched = table->fetch(number);
        REQUIRE(fetched);
        return fetched.value().object;
    }
    const Annotation* byObject(long long number) const
    {
        const std::optional<std::size_t> index = store.findObject(number);
        return index ? &store.at(*index) : nullptr;
    }
};

const core::AnnotationStore& storeOf(tests::SaveSession& session)
{
    return session.journal->document().annotations();
}

const Annotation& current(tests::SaveSession& session, const AnnotationId& id)
{
    const std::optional<std::size_t> index = storeOf(session).find(id);
    REQUIRE(index);
    return storeOf(session).at(*index);
}

std::optional<AnnotationId> firstOf(const core::AnnotationStore& store, AnnotationSubtype subtype, int page = -1)
{
    for (const Annotation& a : store.all()) {
        if (a.subtype == subtype && (page < 0 || a.page == page)) return a.id;
    }
    return std::nullopt;
}

core::Status move(tests::SaveSession& session, std::vector<AnnotationId> ids, double dx, double dy)
{
    return session.journal->run(std::make_unique<MoveAnnotationCommand>(std::move(ids), dx, dy, kNow));
}

core::Status lock(tests::SaveSession& session, std::vector<AnnotationId> ids, std::uint32_t set, std::uint32_t clear)
{
    return session.journal->run(std::make_unique<LockAnnotationCommand>(std::move(ids), set, clear, kNow));
}

core::Status recolour(tests::SaveSession& session, std::vector<AnnotationId> ids, std::vector<double> colour)
{
    core::AnnotationAppearance appearance;
    appearance.color = std::move(colour);
    return session.journal->run(std::make_unique<SetAnnotationAppearanceCommand>(std::move(ids), appearance, kNow));
}

core::Status retext(tests::SaveSession& session, const AnnotationId& id, const QString& text)
{
    Annotation after = current(session, id);
    after.contents = text;
    after.modified = core::PdfDate::fromUtcSeconds(kNow);
    return session.journal->run(std::make_unique<EditAnnotationCommand>(std::vector<Annotation>{after}));
}

// Round trip through the registry: what serialize() wrote is what deserialize() reads, written again.
void roundTrips(const core::Command& command)
{
    const QJsonObject json = command.serialize();
    auto back = core::CommandRegistry::deserialize(json);
    INFO(QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString());
    REQUIRE(back);
    CHECK(back.value()->typeId() == command.typeId());
    CHECK(back.value()->serialize() == json);
}

void refused(const QJsonObject& json, const char* why)
{
    INFO(why << ": " << QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString());
    auto back = core::CommandRegistry::deserialize(json);
    REQUIRE_FALSE(back);
    CHECK(keyOf(back.error()) == "error.command.malformed");
    CHECK(back.error().context.size() < 200);   // bounded, whatever the input
}

Annotation sample(AnnotationId id, int page)
{
    Annotation a = core::newAnnotation(AnnotationSubtype::Highlight, page, QStringLiteral("Edit Tester"), kNow);
    a.id = id;
    if (id.kind != AnnotationId::Kind::Created) a.nm = QStringLiteral("producer-name");
    a.quadPoints = {72, 714, 272, 714, 72, 700, 272, 700};
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {1, 0.5, 0};
    a.contents = QStringLiteral("Zoë's text <&>");
    return a;
}

} // namespace

// ==========================================================================================
// A-29 -- the four commands serialize, and a hostile journal is refused by key
// ==========================================================================================

TEST_CASE("an edit serializes whole annotations of every identity and refuses a malformed one",
          "[annotedit][serialize:EditAnnotationCommand]")
{
    roundTrips(EditAnnotationCommand({sample(AnnotationId::ofObject(12), 0)}));
    roundTrips(EditAnnotationCommand({sample(AnnotationId::ofDirect(3, 1), 3), sample(AnnotationId::ofCreated(core::newAnnotationName()), 2)}));

    const QJsonObject good = EditAnnotationCommand({sample(AnnotationId::ofObject(12), 0)}).serialize();
    QJsonObject wrongType = good;
    wrongType.insert(QStringLiteral("type"), QStringLiteral("annot.add"));
    refused(wrongType, "another command's type");
    QJsonObject none = good;
    none.insert(QStringLiteral("annotations"), QJsonArray{});
    refused(none, "no annotation");
    QJsonObject notObject = good;
    notObject.insert(QStringLiteral("annotations"), QJsonArray{1});
    refused(notObject, "an annotation that is a number");
    QJsonObject badId = good;
    QJsonObject inner = good.value(QStringLiteral("annotations")).toArray().first().toObject();
    inner.insert(QStringLiteral("id"), QStringLiteral("obj:-4"));
    badId.insert(QStringLiteral("annotations"), QJsonArray{inner});
    refused(badId, "an object number below zero");
    QJsonObject hugeName = good;
    QJsonObject named = good.value(QStringLiteral("annotations")).toArray().first().toObject();
    named.insert(QStringLiteral("nm"), QString(10 * 1024 * 1024, QLatin1Char('n')));
    hugeName.insert(QStringLiteral("annotations"), QJsonArray{named});
    refused(hugeName, "a 10 MB /NM");
}

TEST_CASE("a move serializes its ids its offset and its instant and refuses what a drag cannot say",
          "[annotedit][serialize:MoveAnnotationCommand]")
{
    roundTrips(MoveAnnotationCommand({AnnotationId::ofObject(9)}, 12.5, -3.25, kNow));
    roundTrips(MoveAnnotationCommand({AnnotationId::ofDirect(0, 0), AnnotationId::ofCreated(core::newAnnotationName())}, 0.0, 1.0, 0));

    const QJsonObject good = MoveAnnotationCommand({AnnotationId::ofObject(9)}, 1.0, 2.0, kNow).serialize();
    const auto with = [&good](const char* key, const QJsonValue& value) {
        QJsonObject json = good;
        json.insert(QLatin1String(key), value);
        return json;
    };
    refused(with("dx", QStringLiteral("1")), "an offset that is text");
    refused(with("dx", 2.0e6), "an offset past every page");
    refused(with("dy", -2.0e6), "an offset past every page, downwards");
    QJsonObject still = good;
    still.insert(QStringLiteral("dx"), 0.0);
    still.insert(QStringLiteral("dy"), 0.0);
    refused(still, "no move at all");
    refused(with("at", -1.0), "an instant before 1970");
    refused(with("at", 1.5), "a fraction of a second");
    refused(with("at", 1.0e15), "an instant past year 9999");
    refused(with("ids", QJsonArray{}), "no id");
    refused(with("ids", QJsonArray{7}), "an id that is a number");
    refused(with("ids", QJsonArray{QString(10 * 1024 * 1024, QLatin1Char('x'))}), "a 10 MB id");
    QJsonObject noInstant = good;
    noInstant.remove(QStringLiteral("at"));
    refused(noInstant, "no instant");
}

TEST_CASE("an appearance change serializes the properties it sets and refuses one out of range",
          "[annotedit][serialize:SetAnnotationAppearanceCommand]")
{
    core::AnnotationAppearance all;
    all.color = std::vector<double>{0.1, 0.2, 0.3};
    all.interiorColor = std::vector<double>{};
    all.opacity = 0.4;
    all.borderWidth = 2.5;
    roundTrips(SetAnnotationAppearanceCommand({AnnotationId::ofObject(5)}, all, kNow));
    core::AnnotationAppearance one;
    one.opacity = 1.0;
    roundTrips(SetAnnotationAppearanceCommand({AnnotationId::ofObject(5), AnnotationId::ofObject(6)}, one, kNow));

    const QJsonObject good = SetAnnotationAppearanceCommand({AnnotationId::ofObject(5)}, all, kNow).serialize();
    const auto withAppearance = [&good](const QJsonObject& appearance) {
        QJsonObject json = good;
        json.insert(QStringLiteral("appearance"), appearance);
        return json;
    };
    refused(withAppearance({}), "an appearance that sets nothing");
    refused(withAppearance({{QStringLiteral("dash"), 3}}), "a property this build does not know");
    refused(withAppearance({{QStringLiteral("color"), QJsonArray{0.1, 0.2}}}), "a colour of two components");
    refused(withAppearance({{QStringLiteral("color"), QJsonArray{0, 0, 0, 0, 0}}}), "a colour of five components");
    refused(withAppearance({{QStringLiteral("color"), QJsonArray{1.5, 0, 0}}}), "a component past one");
    refused(withAppearance({{QStringLiteral("opacity"), 2.0}}), "an opacity past one");
    refused(withAppearance({{QStringLiteral("borderWidth"), -1.0}}), "a negative border");
    refused(withAppearance({{QStringLiteral("borderWidth"), 1.0e9}}), "a border wider than any page");
    QJsonObject notObject = good;
    notObject.insert(QStringLiteral("appearance"), 3);
    refused(notObject, "an appearance that is a number");
}

TEST_CASE("a lock serializes the bits it sets and clears and refuses any other bit",
          "[annotedit][serialize:LockAnnotationCommand]")
{
    roundTrips(LockAnnotationCommand({AnnotationId::ofObject(5)}, AnnotationFlag::Locked, 0, kNow));
    roundTrips(LockAnnotationCommand({AnnotationId::ofObject(5)}, AnnotationFlag::ReadOnly,
                                     AnnotationFlag::Locked | AnnotationFlag::LockedContents, kNow));

    const QJsonObject good = LockAnnotationCommand({AnnotationId::ofObject(5)}, AnnotationFlag::Locked, 0, kNow).serialize();
    const auto with = [&good](const char* key, const QJsonValue& value) {
        QJsonObject json = good;
        json.insert(QLatin1String(key), value);
        return json;
    };
    refused(with("set", static_cast<double>(AnnotationFlag::Hidden)), "Hidden is not a lock");
    refused(with("set", 0.0), "nothing set and nothing cleared");
    refused(with("clear", static_cast<double>(AnnotationFlag::Locked)), "one bit both set and cleared");
    refused(with("set", 128.5), "a fraction of a bit");
    refused(with("set", QStringLiteral("128")), "bits as text");
}

// ==========================================================================================
// A-26 through the command -- an edit preserves what it does not model, and redraws only what it changed
// ==========================================================================================

TEST_CASE("an edited annotation keeps every key the edit did not change and its appearance where nothing drawn changed",
          "[annotedit][A-26]")
{
    std::size_t edited = 0;
    std::size_t withRichText = 0;
    for (const char* name : {"ordinary/annots-unknown-keys.pdf", "ordinary/annots-mixed.pdf"}) {
        INFO("file: " << name);
        auto session = tests::openCopy(name);
        REQUIRE(session);
        ReadBack before(session->openedBytes);

        // New text on every annotation whose text is not what it draws: one EditAnnotation each.
        std::vector<AnnotationId> textEdited;
        for (const Annotation& a : before.store.all()) {
            if (a.id.kind != AnnotationId::Kind::Object || a.subtype == AnnotationSubtype::FreeText) continue;
            REQUIRE(retext(*session, a.id, QStringLiteral("Edited & <checked>")));
            textEdited.push_back(a.id);
        }
        REQUIRE_FALSE(textEdited.empty());
        // The update holds those dictionaries and nothing else -- no page, no appearance.
        std::vector<long long> expected;
        for (const AnnotationId& id : textEdited) expected.push_back(id.object);
        std::sort(expected.begin(), expected.end());
        CHECK(objectsTheUpdateWrites(*session) == expected);

        ReadBack after(save(*session, QString::fromLatin1(name).section(QLatin1Char('/'), -1).replace(QStringLiteral(".pdf"), QStringLiteral("-A-26-text.pdf"))));
        for (const AnnotationId& id : textEdited) {
            INFO("annotation " << id.toString().toStdString());
            ++edited;
            const CosObject was = before.object(id.object);
            const CosObject now = after.object(id.object);
            for (const CosObject::Entry& entry : was.entries()) {
                INFO("key /" << entry.key);
                if (entry.key == "Contents" || entry.key == "RC" || entry.key == "M") continue;
                REQUIRE(now.find(entry.key) != nullptr);
                CHECK(*now.find(entry.key) == *was.find(entry.key));   // /AP, /PieceInfo, /RawPageTestKey included
            }
            CHECK(after.byObject(id.object)->contents == QStringLiteral("Edited & <checked>"));
            CHECK(after.byObject(id.object)->modified.utcSeconds == kNow);
            CHECK(now.has("RC") == was.has("RC"));
            if (was.has("RC")) {
                ++withRichText;
                const QString rc = pdf::ops::EmbeddedFileReader::decodeTextString(now.get("RC").text());
                CHECK(rc.contains(QStringLiteral("Edited &amp; &lt;checked&gt;")));
            }
        }
    }
    CHECK(edited >= 8);
    CHECK(withRichText >= 1);
}

TEST_CASE("a recoloured highlight from the file is redrawn by our builder and nothing else is written",
          "[annotedit][A-26]")
{
    auto session = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(session);
    const std::optional<AnnotationId> highlight = firstOf(storeOf(*session), AnnotationSubtype::Highlight);
    REQUIRE(highlight);
    ReadBack before(session->openedBytes);
    const CosObject was = before.object(highlight->object);

    REQUIRE(recolour(*session, {*highlight}, {0.2, 0.8, 0.2}));
    // The annotation and ONE new appearance.
    const std::vector<long long> written = objectsTheUpdateWrites(*session);
    REQUIRE(written.size() == 2);
    CHECK(std::count(written.begin(), written.end(), highlight->object) == 1);

    ReadBack after(save(*session, QStringLiteral("A-26-highlight-recoloured.pdf")));
    const CosObject now = after.object(highlight->object);
    CHECK(now.get("C").items().size() == 3);
    CHECK(now.get("C").items()[1].numberValue() == 0.8);
    REQUIRE(now.get("AP").get("N").isReference());
    CHECK(now.get("AP").get("N").reference().number != was.get("AP").get("N").reference().number);
    const CosObject form = after.object(now.get("AP").get("N").reference().number);
    REQUIRE(form.isStream());
    CHECK(pdf::cos::ContentStreamBuilder::check(form.rawStreamData(after.table->source())));
    // Every other key, as it was.
    for (const CosObject::Entry& entry : was.entries()) {
        if (entry.key == "C" || entry.key == "AP" || entry.key == "M") continue;
        INFO("key /" << entry.key);
        REQUIRE(now.find(entry.key) != nullptr);
        CHECK(*now.find(entry.key) == *was.find(entry.key));
    }
}

// Guide issue I-08 (Stage 6 T5): A-35's render case found an undo after a save drawing a file's highlight
// with OUR builder in its old colour. An undo that returns to a version the file held writes that dictionary.
TEST_CASE("an edit undone after its save writes back the dictionary the file held appearance and all",
          "[annotedit][A-35][I-08]")
{
    auto session = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(session);
    const std::optional<AnnotationId> highlight = firstOf(storeOf(*session), AnnotationSubtype::Highlight);
    REQUIRE(highlight);
    ReadBack original(session->openedBytes);
    const CosObject was = original.object(highlight->object);

    REQUIRE(recolour(*session, {*highlight}, {0.2, 0.8, 0.2}));
    save(*session, QStringLiteral("I-08-recoloured.pdf"));
    REQUIRE(session->journal->undo());
    // The annotation alone -- no new appearance: the file's own is listed again.
    CHECK(objectsTheUpdateWrites(*session) == std::vector<long long>{highlight->object});
    ReadBack restored(save(*session, QStringLiteral("I-08-restored.pdf")));
    CosObject now = restored.object(highlight->object);
    CosObject expected = was;
    now.clearSpansRecursively();
    expected.clearSpansRecursively();
    CHECK(now == expected);   // value for value, /AP's reference to the producer's form included
    CHECK(*restored.byObject(highlight->object) == *original.byObject(highlight->object));

    // And redone: our appearance again, through the version THAT save left.
    REQUIRE(session->journal->redo());
    CHECK(objectsTheUpdateWrites(*session) == std::vector<long long>{highlight->object});
}

TEST_CASE("a change of what a kind draws is refused by key when this build cannot draw that kind",
          "[annotedit][A-26][refuse]")
{
    // A FILE ATTACHMENT since T10, which draws the stamp this case recoloured from T8 (`tst_stamps` holds a stamp
    // redrawn) -- as T8 moved it off the Square. Attachments are T11's.
    auto session = tests::openCopy("ordinary/annots-every-type.pdf");
    REQUIRE(session);
    const std::optional<AnnotationId> pin = firstOf(storeOf(*session), AnnotationSubtype::FileAttachment);
    REQUIRE(pin);
    const QByteArray hash = session->journal->document().modelHash();
    const core::Status status = recolour(*session, {*pin}, {0, 0, 1});
    REQUIRE_FALSE(status);
    CHECK(keyOf(status.error()) == "error.pdf.appearanceUnsupported");   // the sink's reason, not "it failed"
    CHECK(status.error().context == QStringLiteral("FileAttachment"));
    CHECK(session->journal->document().modelHash() == hash);
    CHECK_FALSE(session->journal->canUndo());
    CHECK(deltaIsEmpty(*session));
    // The same pushpin MOVED is not a change of what it draws: its appearance goes with its rectangle.
    REQUIRE(move(*session, {*pin}, 10, -10));
}

// ==========================================================================================
// A move carries every point, keeps the producer's appearance, and an undo writes nothing
// ==========================================================================================

TEST_CASE("a move rewrites the annotation alone with every coordinate moved and its appearance kept",
          "[annotedit][move]")
{
    auto session = tests::openCopy("ordinary/annots-every-type.pdf");
    REQUIRE(session);
    ReadBack before(session->openedBytes);
    std::vector<AnnotationId> ids;
    for (const Annotation& a : storeOf(*session).all()) {
        if (a.id.kind == AnnotationId::Kind::Object && a.subtype != AnnotationSubtype::Popup) ids.push_back(a.id);
    }
    REQUIRE(ids.size() >= 18);
    REQUIRE(move(*session, ids, 7.5, -12.25));
    std::vector<long long> expected;
    for (const AnnotationId& id : ids) expected.push_back(id.object);
    std::sort(expected.begin(), expected.end());
    CHECK(objectsTheUpdateWrites(*session) == expected);   // no page, no appearance: ONE object each

    ReadBack after(save(*session, QStringLiteral("T5-move-every-type.pdf")));
    for (const AnnotationId& id : ids) {
        const Annotation* was = before.byObject(id.object);
        const Annotation* now = after.byObject(id.object);
        REQUIRE(was != nullptr);
        REQUIRE(now != nullptr);
        INFO(core::annotationSubtypeName(was->subtype).data());
        CHECK(std::abs(now->rect.left() - (was->rect.left() + 7.5)) < 1e-3);
        CHECK(std::abs(now->rect.bottom() - (was->rect.bottom() - 12.25)) < 1e-3);
        const auto shifted = [](const std::vector<double>& xy, const std::vector<double>& moved) {
            if (xy.size() != moved.size()) return false;
            for (std::size_t i = 0; i < xy.size(); ++i) {
                const double offset = i % 2 == 0 ? 7.5 : -12.25;
                if (std::abs(moved[i] - (xy[i] + offset)) > 1e-3) return false;
            }
            return true;
        };
        CHECK(shifted(was->quadPoints, now->quadPoints));
        CHECK(shifted(was->vertices, now->vertices));
        CHECK(shifted(was->line, now->line));
        CHECK(shifted(was->callout, now->callout));
        REQUIRE(was->inkList.size() == now->inkList.size());
        for (std::size_t s = 0; s < was->inkList.size(); ++s) CHECK(shifted(was->inkList[s], now->inkList[s]));
        // The producer's appearance, unredrawn: the same /AP.
        const CosObject wasDictionary = before.object(id.object);
        const CosObject nowDictionary = after.object(id.object);
        if (wasDictionary.has("AP")) CHECK(*nowDictionary.find("AP") == *wasDictionary.find("AP"));
    }
}

TEST_CASE("an edit undone before a save writes nothing and one undone after a save writes it back",
          "[annotedit][A-25]")
{
    auto session = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(session);
    const std::optional<AnnotationId> square = firstOf(storeOf(*session), AnnotationSubtype::Square);
    const std::optional<AnnotationId> highlight = firstOf(storeOf(*session), AnnotationSubtype::Highlight);
    REQUIRE(square);
    REQUIRE(highlight);
    const QByteArray hash = session->journal->document().modelHash();

    REQUIRE(move(*session, {*square}, 5, 5));
    REQUIRE(move(*session, {*square}, -5, -5));
    // Back where it was, and NOT nothing: a move by hand stamps `/M`, so the annotation is written -- alone.
    CHECK(objectsTheUpdateWrites(*session) == std::vector<long long>{square->object});
    REQUIRE(recolour(*session, {*highlight}, {0, 0, 1}));
    REQUIRE(lock(*session, {*square}, AnnotationFlag::Locked, 0));
    REQUIRE(retext(*session, *square, QStringLiteral("x")));
    for (int i = 0; i < 5; ++i) REQUIRE(session->journal->undo());
    CHECK(session->journal->document().modelHash() == hash);
    CHECK(deltaIsEmpty(*session));   // UNDONE, it is the disk again: nothing to write
    CHECK_FALSE(session->journal->isDirty());

    // After a save: the undo writes the annotation back as it was on disk before.
    ReadBack original(session->openedBytes);
    REQUIRE(session->journal->redo());   // the first move
    save(*session, QStringLiteral("T5-moved-saved.pdf"));
    REQUIRE(session->journal->undo());
    CHECK(objectsTheUpdateWrites(*session) == std::vector<long long>{square->object});
    ReadBack restored(save(*session, QStringLiteral("T5-move-undone-saved.pdf")));
    CHECK(restored.byObject(square->object)->rect == original.byObject(square->object)->rect);
}

TEST_CASE("a direct annotation edited becomes an object its page lists and keeps its name across a save",
          "[annotedit][D6-4]")
{
    auto session = tests::openCopy("ordinary/annots-direct.pdf");
    REQUIRE(session);
    std::optional<AnnotationId> direct;
    for (const Annotation& a : storeOf(*session).all()) {
        if (a.id.kind == AnnotationId::Kind::Direct && a.subtype == AnnotationSubtype::Square) direct = a.id;
    }
    REQUIRE(direct);
    const core::PdfRect at = current(*session, *direct).rect;

    REQUIRE(move(*session, {*direct}, 0, -20));
    // The page's listing and ONE new object.
    CHECK(objectsTheUpdateWrites(*session).size() == 2);
    ReadBack first(save(*session, QStringLiteral("T5-direct-promoted.pdf")));
    const CosObject pageDictionary = first.object(first.walk.pages[static_cast<std::size_t>(direct->page)].number);
    CosObject annots = pageDictionary.get("Annots");
    if (annots.isReference()) annots = first.object(annots.reference().number);
    REQUIRE(annots.isArray());
    REQUIRE(annots.items().at(static_cast<std::size_t>(direct->position)).isReference());
    const long long promoted = annots.items().at(static_cast<std::size_t>(direct->position)).reference().number;
    REQUIRE(first.byObject(promoted) != nullptr);
    CHECK(std::abs(first.byObject(promoted)->rect.bottom() - (at.bottom() - 20)) < 1e-3);

    // The model's name for it has not changed; a second edit rewrites the object alone.
    REQUIRE(storeOf(*session).find(*direct));
    REQUIRE(move(*session, {*direct}, 0, 20));
    CHECK(objectsTheUpdateWrites(*session) == std::vector<long long>{promoted});
    ReadBack second(save(*session, QStringLiteral("T5-direct-edited-again.pdf")));
    CHECK(std::abs(second.byObject(promoted)->rect.bottom() - at.bottom()) < 1e-3);

    // Deleted and restored after the save: listed again as that object.
    REQUIRE(session->journal->run(std::make_unique<core::DeleteAnnotationCommand>(std::vector<AnnotationId>{*direct})));
    REQUIRE(session->journal->undo());
    CHECK(objectsTheUpdateWrites(*session).empty());
}

TEST_CASE("a created annotation moved before its save is written once where it ended and after it is rewritten",
          "[annotedit][D6-4]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    Annotation created = core::newAnnotation(AnnotationSubtype::Highlight, 0, QStringLiteral("Edit Tester"), kNow);
    created.quadPoints = {72, 714, 272, 714, 72, 700, 272, 700};
    created.rect = *core::boundsOfPoints(created.quadPoints);
    created.color = {1, 1, 0};
    REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{created})));
    REQUIRE(move(*session, {created.id}, 0, -100));
    CHECK(objectsTheUpdateWrites(*session).size() == 3);   // the page, the annotation, its appearance
    ReadBack first(save(*session, QStringLiteral("T5-created-moved.pdf")));
    REQUIRE(first.store.onPage(0).size() == 1);
    const Annotation& saved = first.store.at(first.store.onPage(0).front());
    CHECK(saved.nm == created.nm);
    CHECK(std::abs(saved.rect.top() - 614) < 1e-3);

    REQUIRE(move(*session, {created.id}, 10, 0));
    CHECK(objectsTheUpdateWrites(*session) == std::vector<long long>{saved.id.object});   // carried: no new /AP
    ReadBack second(save(*session, QStringLiteral("T5-created-moved-again.pdf")));
    CHECK(std::abs(second.byObject(saved.id.object)->rect.left() - 82) < 1e-3);
}

// ==========================================================================================
// A-43's headless half -- D6-27: the flags are honoured, and changed only by command
// ==========================================================================================

TEST_CASE("a locked annotation is not moved recoloured or deleted and its text may change", "[annotedit][A-43][D6-27]")
{
    auto session = tests::openCopy("ordinary/annots-flags.pdf");
    REQUIRE(session);
    const core::AnnotationStore& store = storeOf(*session);
    const auto withFlag = [&store](std::uint32_t flag) {
        for (const Annotation& a : store.all()) {
            if (a.hasFlag(flag)) return a.id;
        }
        FAIL("no annotation with the flag");
        return AnnotationId{};
    };
    const AnnotationId locked = withFlag(AnnotationFlag::Locked);
    const AnnotationId contentsLocked = withFlag(AnnotationFlag::LockedContents);
    const AnnotationId readOnly = withFlag(AnnotationFlag::ReadOnly);
    const QByteArray hash = session->journal->document().modelHash();

    const auto refusedWith = [&](const core::Status& status, const char* key) {
        REQUIRE_FALSE(status);
        CHECK(keyOf(status.error()) == key);
        CHECK(status.error().context == QStringLiteral("1"));   // the page, as a person counts it
    };
    refusedWith(move(*session, {locked}, 5, 5), "error.edit.annotationLocked");
    refusedWith(recolour(*session, {locked}, {0, 0, 0}), "error.edit.annotationLocked");
    refusedWith(session->journal->run(std::make_unique<core::DeleteAnnotationCommand>(std::vector<AnnotationId>{locked})),
                "error.edit.annotationLocked");
    refusedWith(retext(*session, contentsLocked, QStringLiteral("no")), "error.edit.annotationContentsLocked");
    refusedWith(move(*session, {readOnly}, 5, 5), "error.edit.annotationReadOnly");
    refusedWith(retext(*session, readOnly, QStringLiteral("no")), "error.edit.annotationReadOnly");
    refusedWith(session->journal->run(std::make_unique<core::DeleteAnnotationCommand>(std::vector<AnnotationId>{readOnly})),
                "error.edit.annotationReadOnly");
    // ONE locked annotation in a set refuses the set: nothing of it moves.
    std::vector<AnnotationId> everything;
    for (const Annotation& a : store.all()) everything.push_back(a.id);
    refusedWith(move(*session, everything, 1, 1), "error.edit.annotationLocked");
    CHECK(session->journal->document().modelHash() == hash);
    CHECK_FALSE(session->journal->canUndo());

    // Locked leaves the TEXT editable (ISO 32000-1 Table 165), and a lock comes off by command.
    REQUIRE(retext(*session, locked, QStringLiteral("still mine to say")));
    REQUIRE(lock(*session, {locked}, 0, AnnotationFlag::Locked));
    REQUIRE(move(*session, {locked}, 5, 5));
    REQUIRE(lock(*session, {readOnly}, 0, AnnotationFlag::ReadOnly));
    REQUIRE(move(*session, {readOnly}, 5, 5));
    // Undone, the locks are back and hold again.
    for (int i = 0; i < 4; ++i) REQUIRE(session->journal->undo());
    refusedWith(move(*session, {locked}, 5, 5), "error.edit.annotationLocked");
    refusedWith(move(*session, {readOnly}, 5, 5), "error.edit.annotationReadOnly");

    // Set by command, saved, and read back from the bytes.
    const std::optional<AnnotationId> plain = [&store]() -> std::optional<AnnotationId> {
        for (const Annotation& a : store.all()) {
            if (a.subtype == AnnotationSubtype::Square && (a.flags & LockAnnotationCommand::kLockFlags) == 0
                && !a.hasFlag(AnnotationFlag::Hidden) && !a.hasFlag(AnnotationFlag::NoView))
                return a.id;
        }
        return std::nullopt;
    }();
    REQUIRE(plain);
    REQUIRE(lock(*session, {*plain}, AnnotationFlag::Locked | AnnotationFlag::LockedContents, 0));
    ReadBack back(save(*session, QStringLiteral("T5-locked.pdf")));
    CHECK(back.byObject(plain->object)->hasFlag(AnnotationFlag::Locked));
    CHECK(back.byObject(plain->object)->hasFlag(AnnotationFlag::LockedContents));
    CHECK(back.byObject(plain->object)->hasFlag(AnnotationFlag::Print));
    refusedWith(move(*session, {*plain}, 1, 1), "error.edit.annotationLocked");
}

// ==========================================================================================
// A-30 for edits -- verify reads every edited annotation back edited
// ==========================================================================================

TEST_CASE("a save whose composed bytes lost an edit fails verify before any replace", "[annotedit][A-30]")
{
    auto session = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(session);
    const std::optional<AnnotationId> square = firstOf(storeOf(*session), AnnotationSubtype::Square);
    REQUIRE(square);
    REQUIRE(move(*session, {*square}, 0, 333));
    const QByteArray disk = tests::readAllBytes(session->path);
    const std::size_t original = static_cast<std::size_t>(session->openedBytes.size());

    // The moved rectangle's top put back where it was: every listing still reads as composed.
    pdf::ops::RevisionWriter::setComposeFaultForTesting([original](QByteArray& bytes) {
        const qsizetype at = bytes.indexOf("/Rect", static_cast<qsizetype>(original));
        if (at < 0) return;
        const qsizetype open = bytes.indexOf('[', at);
        const qsizetype close = bytes.indexOf(']', open);
        const QList<QByteArray> numbers = bytes.mid(open + 1, close - open - 1).simplified().split(' ');
        if (numbers.size() != 4) return;
        // The top moved by 333: write it back 333 lower, keeping the byte count by trimming the fraction.
        const QByteArray top = QByteArray::number(numbers[3].toDouble() - 333.0, 'f', 2);
        QByteArray replacement = numbers[0] + ' ' + numbers[1] + ' ' + numbers[2] + ' ' + top;
        bytes.replace(open + 1, close - open - 1, replacement.leftJustified(close - open - 1, ' '));
    });
    auto result = session->coordinator->save(*session->journal, session->path);
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    REQUIRE_FALSE(result);
    CHECK(keyOf(result.error()) == "error.save.verifyFailed");
    CHECK(result.error().context.startsWith(QStringLiteral("annotations:")));
    CHECK(tests::sha256(tests::readAllBytes(session->path)) == tests::sha256(disk));
    CHECK(session->journal->isDirty());

    // THE CONTROL: unfaulted, the same save is written.
    save(*session, QStringLiteral("T5-verify-control.pdf"));
}

// ==========================================================================================
// A-29 -- recovery replays the four to the same model and the same bytes
// ==========================================================================================

TEST_CASE("recovery replays edits moves appearances and locks to the same model and the same bytes",
          "[annotedit][A-29][recovery]")
{
    auto session = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = core::RecoveryJournal::open(root.path(), session->path, session->journal->document().id(),
                                                &session->durableFiles);
    REQUIRE(recovery->isRecoverable());
    session->journal->setRecorder(recovery.get());

    const std::optional<AnnotationId> square = firstOf(storeOf(*session), AnnotationSubtype::Square);
    const std::optional<AnnotationId> highlight = firstOf(storeOf(*session), AnnotationSubtype::Highlight);
    REQUIRE(square);
    REQUIRE(highlight);
    REQUIRE(move(*session, {*square, *highlight}, 3, 4));
    REQUIRE(recolour(*session, {*highlight}, {0.5, 0.5, 0.5}));
    REQUIRE(retext(*session, *square, QStringLiteral("Recovered text")));
    REQUIRE(lock(*session, {*square}, AnnotationFlag::Locked, 0));
    REQUIRE(lock(*session, {*highlight}, AnnotationFlag::ReadOnly, 0));
    REQUIRE(session->journal->undo());

    const QByteArray expectedModel = session->journal->document().modelHash();
    auto expected = pdf::ops::RevisionWriter::compose(*session->sink(), false);
    REQUIRE(expected);
    recovery.reset();
    session->journal->setRecorder(nullptr);

    const auto candidates = core::RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    auto fresh = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(fresh);
    auto report = core::RecoveryJournal::replay(candidates.front().directory, *fresh->journal);
    REQUIRE(report);
    CHECK_FALSE(report.value().stoppedAtStep.has_value());
    CHECK(fresh->journal->document().modelHash() == expectedModel);
    auto replayed = pdf::ops::RevisionWriter::compose(*fresh->sink(), false);
    REQUIRE(replayed);
    CHECK(tests::sha256(replayed.value().bytes) == tests::sha256(expected.value().bytes));
}

// ==========================================================================================
// Scope -- a move repaints where the annotation was and where it is
// ==========================================================================================

TEST_CASE("a move's scope is the rectangle it left and the one it reached", "[annotedit][scope]")
{
    auto session = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(session);
    const std::optional<AnnotationId> square = firstOf(storeOf(*session), AnnotationSubtype::Square);
    REQUIRE(square);
    const core::PdfRect was = current(*session, *square).rect;
    auto owned = std::make_unique<MoveAnnotationCommand>(std::vector<AnnotationId>{*square}, 0, 300, kNow);
    const MoveAnnotationCommand& command = *owned;   // the journal keeps it alive in its history
    CHECK(command.scope().touchesPage(current(*session, *square).page));   // before apply: rounds outward
    REQUIRE(session->journal->run(std::move(owned)));
    const core::Scope scope = command.scope();
    CHECK(scope.aspect() == core::Scope::Aspect::Annotations);
    const int page = current(*session, *square).page;
    REQUIRE(scope.regionsOf(page) != nullptr);
    const std::vector<core::PdfRect>& regions = *scope.regionsOf(page);
    REQUIRE(regions.size() == 2);
    const bool hasOld = std::any_of(regions.begin(), regions.end(), [&was](const core::PdfRect& r) { return r.contains(core::PdfPoint{was.left() + 1, was.bottom() + 1}); });
    const bool hasNew = std::any_of(regions.begin(), regions.end(), [&was](const core::PdfRect& r) { return r.contains(core::PdfPoint{was.left() + 1, was.bottom() + 301}); });
    CHECK(hasOld);
    CHECK(hasNew);
}

// ==========================================================================================
// The store's half -- replace in place, all or nothing
// ==========================================================================================

TEST_CASE("the store replaces annotations in place all or nothing and keeps their listings", "[annotedit][store]")
{
    auto session = tests::openCopy("ordinary/annots-direct.pdf");
    REQUIRE(session);
    core::AnnotationStore store = storeOf(*session);
    const auto rectsOf = [](const core::AnnotationStore& s) {
        std::vector<std::optional<core::PdfRect>> rects;
        for (const auto& page : s.listingFacts()) {
            for (const core::AnnotationFact& fact : page) rects.push_back(fact.rect);
        }
        return rects;
    };
    const std::vector<std::optional<core::PdfRect>> facts = rectsOf(store);
    Annotation first = store.at(0);
    const Annotation untouched = first;
    Annotation moved = core::translated(first, 0, 10);

    Annotation otherSubtype = moved;
    otherSubtype.subtype = AnnotationSubtype::Ink;
    CHECK_FALSE(store.replace({otherSubtype}));
    Annotation otherPage = moved;
    otherPage.page = first.page + 1;
    CHECK_FALSE(store.replace({otherPage}));
    Annotation unknown = moved;
    unknown.id = AnnotationId::ofObject(999999);
    CHECK_FALSE(store.replace({moved, unknown}));
    CHECK_FALSE(store.replace({moved, moved}));
    CHECK(store.at(0) == untouched);   // nothing of a refused set
    CHECK(rectsOf(store) == facts);

    REQUIRE(store.replace({moved}));
    CHECK(store.at(0) == moved);
    // Every listing entry naming it carries its new rectangle -- what D6-3 compares with the engine.
    for (const core::AnnotationPlacement& at : store.placementsOf(moved.id)) {
        const core::AnnotationFact& fact = store.listing(at.page).at(static_cast<std::size_t>(at.position)).fact;
        REQUIRE(fact.rect);
        CHECK(fact.rect->bottom() == moved.rect.bottom());
    }
}

// ==========================================================================================
// D6-30 -- the geometry: one mapping for every point, and a hit where the kind is drawn
// ==========================================================================================

TEST_CASE("an annotation mapped into a rectangle carries every point by the same mapping", "[annotedit][geometry]")
{
    Annotation a;
    a.subtype = AnnotationSubtype::Ink;
    a.rect = core::PdfRect{core::PdfPoint{100, 200}, core::PdfSize{50, 20}};
    a.inkList = {{100, 200, 150, 220}, {125, 210}};
    a.quadPoints = {100, 220, 150, 220, 100, 200, 150, 200};
    a.vertices = {110, 205, 140, 215};
    a.line = {100, 200, 150, 220};
    a.callout = {100, 200, 125, 210};
    a.rectDifferences = {1, 2, 3, 4};
    const std::optional<Annotation> m = core::mappedInto(a, core::PdfRect{core::PdfPoint{0, 0}, core::PdfSize{100, 40}});
    REQUIRE(m);
    CHECK(m->inkList[0] == std::vector<double>{0, 0, 100, 40});
    CHECK(m->inkList[1] == std::vector<double>{50, 20});
    CHECK(m->quadPoints == std::vector<double>{0, 40, 100, 40, 0, 0, 100, 0});
    CHECK(m->vertices == std::vector<double>{20, 10, 80, 30});
    CHECK(m->rectDifferences == std::vector<double>{2, 4, 6, 8});
    CHECK(core::isMappedCopy(a, *m));

    const Annotation t = core::translated(a, 0.1, 0.2);
    CHECK(t.rect.left() == 100.1);
    CHECK(t.line == std::vector<double>{100.1, 200.2, 150.1, 220.2});
    CHECK(core::isMappedCopy(a, t, 1e-9));
    CHECK(t.rectDifferences == a.rectDifferences);   // a move does not scale the inset

    // NOT a mapped copy: a point moved on its own, or any other field.
    Annotation bent = *m;
    bent.inkList[1][0] += 1;
    CHECK_FALSE(core::isMappedCopy(a, bent));
    Annotation recoloured = *m;
    recoloured.color = {1, 0, 0};
    CHECK_FALSE(core::isMappedCopy(a, recoloured));
    Annotation restamped = *m;
    restamped.modified = core::PdfDate::fromUtcSeconds(kNow);
    CHECK(core::isMappedCopy(a, restamped));   // /M is the edit's own stamp

    Annotation infinite = a;
    infinite.rect.size.width = std::numeric_limits<double>::infinity();
    CHECK_FALSE(core::mappedInto(infinite, a.rect));
}

TEST_CASE("each kind is hit by its own shape and a hidden one is never hit", "[annotedit][geometry][D6-30]")
{
    const auto box = [](double l, double b, double r, double t) {
        return core::PdfRect{core::PdfPoint{l, b}, core::PdfSize{r - l, t - b}};
    };
    Annotation square;
    square.subtype = AnnotationSubtype::Square;
    square.rect = box(100, 100, 200, 160);
    square.borderWidth = 2;
    CHECK(core::hitsAnnotation(square, {101, 130}, 1));    // on its left edge
    CHECK(core::hitsAnnotation(square, {150, 159}, 1));    // on its top edge
    CHECK_FALSE(core::hitsAnnotation(square, {150, 130}, 1));   // the empty middle is not ink
    CHECK_FALSE(core::hitsAnnotation(square, {96, 130}, 1));    // outside, past the tolerance
    Annotation filled = square;
    filled.interiorColor = {1, 0, 0};
    CHECK(core::hitsAnnotation(filled, {150, 130}, 1));

    Annotation circle;
    circle.subtype = AnnotationSubtype::Circle;
    circle.rect = box(0, 0, 100, 50);
    circle.borderWidth = 1;
    CHECK(core::hitsAnnotation(circle, {50, 49.5}, 1));    // the top of the ellipse
    CHECK(core::hitsAnnotation(circle, {0.5, 25}, 1));     // its left end
    CHECK_FALSE(core::hitsAnnotation(circle, {50, 25}, 1));    // the centre
    CHECK_FALSE(core::hitsAnnotation(circle, {3, 3}, 1));      // the box's corner is not the ellipse

    Annotation line;
    line.subtype = AnnotationSubtype::Line;
    line.rect = box(0, 0, 100, 100);
    line.line = {0, 0, 100, 100};
    line.borderWidth = 2;
    CHECK(core::hitsAnnotation(line, {50, 51}, 1));
    CHECK_FALSE(core::hitsAnnotation(line, {50, 55}, 1));   // beside the stroke, inside the box
    CHECK_FALSE(core::hitsAnnotation(line, {90, 10}, 1));

    Annotation ink;
    ink.subtype = AnnotationSubtype::Ink;
    ink.rect = box(0, 0, 100, 100);
    ink.inkList = {{0, 0, 100, 0}, {0, 100, 100, 100}};
    CHECK(core::hitsAnnotation(ink, {50, 0.5}, 1));
    CHECK(core::hitsAnnotation(ink, {50, 99.5}, 1));
    CHECK_FALSE(core::hitsAnnotation(ink, {50, 50}, 1));

    Annotation polygon;
    polygon.subtype = AnnotationSubtype::Polygon;
    polygon.rect = box(0, 0, 100, 100);
    polygon.vertices = {0, 0, 100, 0, 50, 100};
    CHECK(core::hitsAnnotation(polygon, {50, 0}, 1));
    CHECK_FALSE(core::hitsAnnotation(polygon, {50, 30}, 1));
    polygon.interiorColor = {0, 0, 1};
    CHECK(core::hitsAnnotation(polygon, {50, 30}, 1));
    CHECK_FALSE(core::hitsAnnotation(polygon, {5, 90}, 1));   // inside the box, outside the triangle

    // TEXT MARKUP IS HIT OVER THE WORDS IT MARKS (D6-30), in either corner order.
    Annotation underline;
    underline.subtype = AnnotationSubtype::Underline;
    underline.rect = box(72, 700, 272, 714);
    underline.quadPoints = {72, 714, 272, 714, 72, 700, 272, 700};   // Acrobat's
    CHECK(core::hitsAnnotation(underline, {150, 712}, 0.5));
    CHECK_FALSE(core::hitsAnnotation(underline, {150, 690}, 0.5));
    underline.quadPoints = {72, 700, 272, 700, 272, 714, 72, 714};   // ISO's figure
    CHECK(core::hitsAnnotation(underline, {150, 712}, 0.5));

    Annotation note;
    note.subtype = AnnotationSubtype::Text;
    note.rect = box(10, 10, 30, 30);
    CHECK(core::hitsAnnotation(note, {20, 20}, 0));
    note.flags = AnnotationFlag::Hidden;
    CHECK_FALSE(core::hitsAnnotation(note, {20, 20}, 0));
    note.flags = AnnotationFlag::NoView;
    CHECK_FALSE(core::hitsAnnotation(note, {20, 20}, 0));
    Annotation popup = note;
    popup.flags = 0;
    popup.subtype = AnnotationSubtype::Popup;
    CHECK_FALSE(core::hitsAnnotation(popup, {20, 20}, 0));   // the overlay's, never the page's

    CHECK(core::isResizable(AnnotationSubtype::Square));
    CHECK_FALSE(core::isResizable(AnnotationSubtype::Text));
    CHECK_FALSE(core::isResizable(AnnotationSubtype::Highlight));
}
