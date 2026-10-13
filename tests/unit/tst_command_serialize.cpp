// tst_command_serialize -- `core/cmd/CommandRegistry` and every command's `serialize()`. Stage 3 T5, guide
// cases A-42 and A-45 (`Stage3-Mutation-Testing-Guide.md` §5.7), decisions D3-10 and D3-12.
//
// WHAT THIS SUITE IS TRYING TO CATCH: a `Commands.def` row that does not round-trip; a command whose
// `name()` is a sentence, or a key a shipped catalog lacks (guide issue I-01); a hostile serialized
// command -- the recovery journal is a file on disk -- that crashes, or that becomes a command.
//
// The tag `[serialize:Class]` is what `tools/mutation-lint` (T6) looks for, per command class (D3-12).
//
// WHAT IT DOES NOT COVER (convention 19): whether the recovery journal writes these objects to disk
// and reads them back (T9), and whether `ui/` resolves `name()` into the undo menu (T10). The catalog
// check reads the `.ts` sources as text; it does not load a compiled `.qm`.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/AttachmentCommands.h"
#include "core/cmd/commands/PageCommands.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <functional>
#include <map>
#include <string>

using namespace rawpage;
using core::Command;
using core::CommandRegistry;
using core::RotatePageCommand;

namespace {

// Stage 6 T3: a highlight as `core::newAnnotation` makes one, every field a created annotation carries.
core::Annotation sampleHighlight(int page, const QString& author = QStringLiteral("Ana"))
{
    core::Annotation a = core::newAnnotation(core::AnnotationSubtype::Highlight, page, author, 1791504000);
    a.quadPoints = {72, 714, 272, 714, 72, 700, 272, 700};
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {1, 1, 0};
    a.opacity = 0.5;
    a.contents = QStringLiteral("Zo\u00EB's note");
    return a;
}

// One or more sample commands per `Commands.def` row. A row with no entry here FAILS the round-trip
// case, so a new command cannot arrive without a sample.
const std::map<std::string, std::function<std::vector<std::unique_ptr<Command>>()>>& samples()
{
    static const std::map<std::string, std::function<std::vector<std::unique_ptr<Command>>()>> table{
        {"page.rotate",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             for (int page : {0, 1, 899, RotatePageCommand::kMaxPageIndex})
                 for (int degrees : {90, 180, 270, -90})
                     out.push_back(std::make_unique<RotatePageCommand>(page, degrees));
             return out;
         }},
        // Stage 6 T3: one annotation, a set across pages, and the limit of a page index.
        {"annot.add",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{sampleHighlight(0)}));
             out.push_back(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{
                 sampleHighlight(0), sampleHighlight(3, QStringLiteral("\u6797")), sampleHighlight(0)}));
             out.push_back(std::make_unique<core::AddAnnotationCommand>(
                 std::vector<core::Annotation>{sampleHighlight(core::AnnotationCommandLimits::kMaxPageIndex)}));
             return out;
         }},
        // Every kind of identity a command can name (D6-4).
        {"annot.delete",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::DeleteAnnotationCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofObject(12)}));
             out.push_back(std::make_unique<core::DeleteAnnotationCommand>(std::vector<core::AnnotationId>{
                 core::AnnotationId::ofObject(1LL << 40), core::AnnotationId::ofDirect(2, 3),
                 core::AnnotationId::ofCreated(core::newAnnotationName())}));
             return out;
         }},
        // Stage 6 T5: the four in-place commands, each over one identity and over every kind (D6-4).
        {"annot.edit",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::Annotation fromFile = sampleHighlight(0);
             fromFile.id = core::AnnotationId::ofObject(12);
             fromFile.nm = QStringLiteral("producer");
             out.push_back(std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{fromFile}));
             core::Annotation direct = sampleHighlight(2);
             direct.id = core::AnnotationId::ofDirect(2, 3);
             out.push_back(std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{direct, sampleHighlight(1)}));
             return out;
         }},
        {"annot.move",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::MoveAnnotationCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofObject(12)}, 10.5, -2.25, 1791504000));
             out.push_back(std::make_unique<core::MoveAnnotationCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofDirect(0, 1), core::AnnotationId::ofCreated(core::newAnnotationName())},
                 0.0, 0.1, 0));
             return out;
         }},
        {"annot.setAppearance",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::AnnotationAppearance colour;
             colour.color = std::vector<double>{0, 0.5, 1};
             out.push_back(std::make_unique<core::SetAnnotationAppearanceCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofObject(12)}, colour, 1791504000));
             core::AnnotationAppearance all;
             all.interiorColor = std::vector<double>{};
             all.opacity = 0.25;
             all.borderWidth = 3.0;
             out.push_back(std::make_unique<core::SetAnnotationAppearanceCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofDirect(1, 0), core::AnnotationId::ofObject(7)}, all, 1791504000));
             return out;
         }},
        {"annot.lock",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::LockAnnotationCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofObject(12)}, core::AnnotationFlag::Locked, 0, 1791504000));
             out.push_back(std::make_unique<core::LockAnnotationCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofObject(12)}, 0, core::LockAnnotationCommand::kLockFlags, 1791504000));
             return out;
         }},
        // Stage 6 T9: a reply to an annotation from the file and one to a reply RawPage made; statuses in both
        // models, as a set (D6-12).
        {"annot.reply",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::Annotation parent = sampleHighlight(0);
             parent.id = core::AnnotationId::ofObject(12);
             const core::PdfRect page{core::PdfPoint{0, 0}, core::PdfSize{612, 792}};
             core::Annotation first = core::newReply(parent, page, QStringLiteral("Agreed."), {1, 1, 0}, QStringLiteral("Ana"), 1791504000);
             core::Annotation second = core::newReply(first, page, QStringLiteral("Zoë too"), {1, 1, 0}, QStringLiteral("Zoë"), 1791504060);
             out.push_back(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<core::Annotation>{first}));
             out.push_back(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<core::Annotation>{first, second}));
             return out;
         }},
        {"annot.setStatus",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::Annotation parent = sampleHighlight(0);
             parent.id = core::AnnotationId::ofObject(12);
             out.push_back(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<core::Annotation>{
                 core::newStateReply(parent, QStringLiteral("Review"), QStringLiteral("Accepted"), QStringLiteral("Ben"), 1791504000)}));
             out.push_back(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<core::Annotation>{
                 core::newStateReply(parent, QStringLiteral("Marked"), QStringLiteral("Marked"), QStringLiteral("Ben"), 1791504000),
                 core::newStateReply(parent, QStringLiteral("Marked"), QStringLiteral("Unmarked"), QStringLiteral("Ana"), 1791504001)}));
             out.push_back(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<core::Annotation>{
                 core::newStateReply(parent, QStringLiteral("Review"), QStringLiteral("None"), QStringLiteral("Ana"), 1791504002),
                 core::newStateReply(parent, QStringLiteral("Marked"), QStringLiteral("Marked"), QStringLiteral("Ana"), 1791504003)}));
             return out;
         }},
        // Stage 6 T11 (D6-17): one file, and a set of two -- one EMPTY, which is a file too, and one named outside ASCII.
        {"attachment.add",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::AddAttachmentCommand>(std::vector<core::DocumentAttachment>{
                 {QStringLiteral("readme (2).txt"),
                  core::newEmbeddedFile(QStringLiteral("readme.txt"), QByteArray("notes\n"), QStringLiteral("Notes"), 1791504000)}}));
             out.push_back(std::make_unique<core::AddAttachmentCommand>(std::vector<core::DocumentAttachment>{
                 {QStringLiteral("empty.bin"), core::newEmbeddedFile(QStringLiteral("empty.bin"), QByteArray(), QString(), 1791504000)},
                 {QStringLiteral("Zo\u00EB \u5831\u544A.csv"),
                  core::newEmbeddedFile(QStringLiteral("Zo\u00EB \u5831\u544A.csv"), QByteArray("a,b\n1,2\n"), QStringLiteral("t\u00E4ble"), 1791504000)}}));
             return out;
         }},
        {"attachment.delete",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::DeleteAttachmentCommand>(std::vector<QString>{QStringLiteral("setup.exe")}));
             out.push_back(std::make_unique<core::DeleteAttachmentCommand>(
                 std::vector<QString>{QStringLiteral("appendix.pdf"), QStringLiteral("readme.txt")}));
             return out;
         }},
    };
    return table;
}

QByteArray compact(const QJsonObject& json)
{
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

bool catalogHas(const char* file, QLatin1String key)
{
    QFile catalog(QStringLiteral(RAWPAGE_I18N_DIR "/") + QLatin1String(file));
    REQUIRE(catalog.open(QIODevice::ReadOnly));
    const QByteArray text = catalog.readAll();
    return text.contains("<source>" + QByteArray(key.data(), key.size()) + "</source>");
}

} // namespace

// ==========================================================================================
// A-42 -- the registry round-trips what it registered and refuses what it did not
// ==========================================================================================

TEST_CASE("every Commands.def row round-trips through CommandRegistry", "[cmd][serialize][serialize:RotatePageCommand][serialize:AddAnnotationCommand][serialize:DeleteAnnotationCommand][serialize:EditAnnotationCommand][serialize:MoveAnnotationCommand][serialize:SetAnnotationAppearanceCommand][serialize:LockAnnotationCommand][serialize:ReplyToAnnotationCommand][serialize:SetAnnotationStatusCommand][serialize:AddAttachmentCommand][serialize:DeleteAttachmentCommand]")
{
    const auto typeIds = CommandRegistry::typeIds();
    REQUIRE_FALSE(typeIds.empty());
    std::size_t roundTrips = 0;
    for (const QLatin1String typeId : typeIds) {
        const std::string id(typeId.data(), static_cast<std::size_t>(typeId.size()));
        INFO("type id: " << id);
        const auto sample = samples().find(id);
        REQUIRE(sample != samples().end());      // a registered command with no sample is a failure

        for (const auto& command : sample->second()) {
            const QJsonObject json = command->serialize();
            INFO("serialized: " << compact(json).toStdString());
            CHECK(json.value(QStringLiteral("type")).toString() == QLatin1String(typeId));
            CHECK(command->typeId() == typeId);

            auto back = CommandRegistry::deserialize(json);
            REQUIRE(back);
            CHECK(back.value()->typeId() == typeId);
            CHECK(compact(back.value()->serialize()) == compact(json));
            CHECK(back.value()->name() == command->name());
            CHECK(back.value()->scope() == command->scope());
            CHECK(back.value()->operation() == command->operation());
            ++roundTrips;
        }
    }
    // Every sample table entry names a registered row: a sample for a deleted row is a stale test.
    for (const auto& [id, make] : samples()) {
        INFO("sampled type id: " << id);
        bool registered = false;
        for (const QLatin1String typeId : typeIds)
            registered = registered || typeId == QLatin1String(id.c_str());
        CHECK(registered);
    }
    CHECK(roundTrips == 16 + 3 + 2 + 2 + 2 + 2 + 2 + 2 + 3 + 2 + 2);
}

// Stage 6 T11 (D6-17): an attachment command is a journal line, and its bytes may be large -- every shape apply()
// would refuse is refused as the malformed line it is, and none reaches a command.
TEST_CASE("CommandRegistry refuses every malformed attachment command by Result", "[cmd][serialize][hostile][attachments]")
{
    const QJsonObject good = core::DocumentAttachment{
        QStringLiteral("a.txt"), core::newEmbeddedFile(QStringLiteral("a.txt"), QByteArray("abc"), QString(), 1791504000)}.toJson();
    const auto withFile = [&good](const std::function<void(QJsonObject&)>& change) {
        QJsonObject attachment = good;
        QJsonObject file = attachment.value(QStringLiteral("file")).toObject();
        change(file);
        attachment.insert(QStringLiteral("file"), file);
        return QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{attachment}}};
    };
    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"no files", QJsonObject{{"type", "attachment.add"}}},
        {"empty files", QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{}}}},
        {"a file that is not an object", QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{1}}}},
        {"no key", QJsonObject{{"type", "attachment.add"},
                               {"files", QJsonArray{QJsonObject{{"file", good.value(QStringLiteral("file"))}}}}}},
        {"one key twice", QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{good, good}}}},
        {"no bytes", withFile([](QJsonObject& f) { f.remove(QStringLiteral("data")); })},
        {"bytes that are not base64", withFile([](QJsonObject& f) { f.insert(QStringLiteral("data"), QStringLiteral("!!!")); })},
        {"a size that disagrees with the bytes", withFile([](QJsonObject& f) { f.insert(QStringLiteral("size"), 4); })},
        {"a negative size", withFile([](QJsonObject& f) { f.insert(QStringLiteral("size"), -1); })},
        {"a checksum that is not sixteen bytes", withFile([](QJsonObject& f) { f.insert(QStringLiteral("checksum"), QStringLiteral("abcd")); })},
        {"no name", withFile([](QJsonObject& f) { f.remove(QStringLiteral("name")); })},
        // S6 guide I-18: the bound binds what a command CREATES -- an attached file's name and its key.
        {"a name past the bound", withFile([](QJsonObject& f) {
             f.insert(QStringLiteral("name"), QString(core::kMaxAttachmentNameLength + 1, QLatin1Char('n')));
         })},
        {"a key past the bound", [&good] {
             QJsonObject attachment = good;
             attachment.insert(QStringLiteral("key"), QString(core::kMaxAttachmentNameLength + 1, QLatin1Char('k')));
             return QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{attachment}}};
         }()},
        {"a delete with no keys", QJsonObject{{"type", "attachment.delete"}}},
        {"a delete of an empty key", QJsonObject{{"type", "attachment.delete"}, {"keys", QJsonArray{QString()}}}},
        {"a delete of one key twice", QJsonObject{{"type", "attachment.delete"}, {"keys", QJsonArray{"a", "a"}}}},
        {"a delete of a number", QJsonObject{{"type", "attachment.delete"}, {"keys", QJsonArray{7}}}},
    };
    for (const auto& [what, json] : hostile) {
        INFO(what << ": " << compact(json).toStdString());
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
    }
    // THE CONTROL: the good line parses.
    CHECK(CommandRegistry::deserialize(QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{good}}}));
}

TEST_CASE("CommandRegistry returns NotFound for a type id it does not register", "[cmd][serialize]")
{
    for (const char* unknown : {"page.rotat", "page.rotate ", "PAGE.ROTATE", "", "test.lying"}) {
        INFO("type: '" << unknown << "'");
        auto result = CommandRegistry::deserialize(
            QJsonObject{{"type", QLatin1String(unknown)}, {"page", 0}, {"degrees", 90}});
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::NotFound);
        CHECK(keyOf(result.error()) == "error.command.unknownType");
        CHECK(result.error().context == QLatin1String(unknown));
    }
}

TEST_CASE("every command name is a catalog key present in both shipped locales", "[cmd][serialize][i18n]")
{
    // D3-10(a), guide issue I-01: `core/` returns a key and arguments, and `ui/` resolves them.
    for (const auto& [id, make] : samples()) {
        for (const auto& command : make()) {
            const core::CatalogText name = command->name();
            const std::string key(name.key.data(), static_cast<std::size_t>(name.key.size()));
            INFO("type id: " << id << ", key: " << key);
            CHECK(key.rfind("command.", 0) == 0);
            CHECK(key.find(' ') == std::string::npos);
            CHECK(catalogHas("rawpage_en.ts", name.key));
            CHECK(catalogHas("rawpage_zh_TW.ts", name.key));
        }
    }
    // THE CONTROL: the lookup can say no.
    CHECK_FALSE(catalogHas("rawpage_en.ts", QLatin1String("command.page.rotat")));
    CHECK_FALSE(catalogHas("rawpage_zh_TW.ts", QLatin1String("command.page.rotat")));
}

// ==========================================================================================
// A-45 -- hostile serialized commands
// ==========================================================================================

TEST_CASE("CommandRegistry refuses every malformed page rotation by Result", "[cmd][serialize][hostile]")
{
    // WHAT THIS CASE DOES NOT COVER on Windows: the sanitizer half of A-45. MinGW has no ASan, so "no
    // sanitizer line" is read from the `portable-asan` job's log, which runs this same case.
    const QString huge(10 * 1024 * 1024, QLatin1Char('9'));
    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"empty object", QJsonObject{}},
        {"type missing", QJsonObject{{"page", 0}, {"degrees", 90}}},
        {"type a number", QJsonObject{{"type", 1}, {"page", 0}, {"degrees", 90}}},
        {"type null", QJsonObject{{"type", QJsonValue::Null}, {"page", 0}, {"degrees", 90}}},
        {"page missing", QJsonObject{{"type", "page.rotate"}, {"degrees", 90}}},
        {"degrees missing", QJsonObject{{"type", "page.rotate"}, {"page", 0}}},
        {"page a string", QJsonObject{{"type", "page.rotate"}, {"page", "0"}, {"degrees", 90}}},
        {"page an array", QJsonObject{{"type", "page.rotate"}, {"page", QJsonArray{0}}, {"degrees", 90}}},
        {"page a bool", QJsonObject{{"type", "page.rotate"}, {"page", true}, {"degrees", 90}}},
        {"degrees a string", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", "90"}}},
        {"degrees an object", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", QJsonObject{}}}},
        {"page -1", QJsonObject{{"type", "page.rotate"}, {"page", -1}, {"degrees", 90}}},
        {"page 1e9", QJsonObject{{"type", "page.rotate"}, {"page", 1e9}, {"degrees", 90}}},
        {"page one past the limit",
         QJsonObject{{"type", "page.rotate"}, {"page", RotatePageCommand::kMaxPageIndex + 1}, {"degrees", 90}}},
        {"page 1.5", QJsonObject{{"type", "page.rotate"}, {"page", 1.5}, {"degrees", 90}}},
        {"page 2^63", QJsonObject{{"type", "page.rotate"}, {"page", 9.2233720368547758e18}, {"degrees", 90}}},
        {"degrees 45", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", 45}}},
        {"degrees 0", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", 0}}},
        {"degrees 360", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", 360}}},
        {"degrees -90", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", -90}}},
        {"degrees 90.5", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", 90.5}}},
        {"page a 10 MB string", QJsonObject{{"type", "page.rotate"}, {"page", huge}, {"degrees", 90}}},
        {"degrees a 10 MB string", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", huge}}},
    };

    for (const auto& [label, json] : hostile) {
        INFO("input: " << label);
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
        CHECK(result.error().context.size() < 64);
    }

    SECTION("a 10 MB type string is NotFound, and its context is bounded")
    {
        auto result = CommandRegistry::deserialize(
            QJsonObject{{"type", huge}, {"page", 0}, {"degrees", 90}});
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::NotFound);
        CHECK(result.error().context.size() == 64);
    }
    SECTION("the same 10 MB string through the JSON parser, as the recovery journal will read it")
    {
        QByteArray text = R"({"type":"page.rotate","degrees":90,"page":")";
        text += huge.toLatin1();
        text += "\"}";
        const QJsonDocument parsed = QJsonDocument::fromJson(text);
        REQUIRE(parsed.isObject());
        CHECK_FALSE(CommandRegistry::deserialize(parsed.object()));
    }
    SECTION("THE CONTROL: the boundaries themselves are accepted")
    {
        for (int page : {0, RotatePageCommand::kMaxPageIndex}) {
            for (int degrees : {90, 180, 270}) {
                INFO("page " << page << ", degrees " << degrees);
                auto result = CommandRegistry::deserialize(
                    QJsonObject{{"type", "page.rotate"}, {"page", page}, {"degrees", degrees}});
                REQUIRE(result);
                const auto* rotate = dynamic_cast<RotatePageCommand*>(result.value().get());
                REQUIRE(rotate != nullptr);
                CHECK(rotate->pageIndex() == page);
                CHECK(rotate->degrees() == degrees);
                CHECK(rotate->isValid());
            }
        }
        // A whole number written as a real is still a whole number.
        CHECK(CommandRegistry::deserialize(
            QJsonObject{{"type", "page.rotate"}, {"page", 3.0}, {"degrees", 180.0}}));
    }
}

TEST_CASE("CommandRegistry refuses every malformed annotation command by Result", "[cmd][serialize][hostile][annotops]")
{
    // Stage 6 T3. The recovery journal is a file on disk, so `annot.add` and `annot.delete` are parsers of
    // hostile input as `page.rotate` is (Stage 3's A-45). Each entry breaks ONE thing about a good command.
    const QJsonObject good = core::AddAnnotationCommand({sampleHighlight(0)}).serialize();
    const QJsonObject goodAnnotation = good.value("annotations").toArray().at(0).toObject();
    const auto withAnnotation = [&](const char* key, const QJsonValue& value) {
        QJsonObject annotation = goodAnnotation;
        if (value.isUndefined()) annotation.remove(QLatin1String(key));
        else annotation.insert(QLatin1String(key), value);
        return QJsonObject{{"type", "annot.add"}, {"annotations", QJsonArray{annotation}}};
    };
    QJsonArray tooMany;
    for (std::size_t i = 0; i <= core::AnnotationCommandLimits::kMaxAnnotations; ++i) tooMany.append(QStringLiteral("obj:1"));
    QJsonArray hugeQuads;
    for (std::size_t i = 0; i < core::AnnotationCommandLimits::kMaxCoordinates + 8; ++i) hugeQuads.append(1.0);
    const QString huge(10 * 1024 * 1024, QLatin1Char('9'));

    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"add: annotations missing", QJsonObject{{"type", "annot.add"}}},
        {"add: annotations an object", QJsonObject{{"type", "annot.add"}, {"annotations", QJsonObject{}}}},
        {"add: annotations empty", QJsonObject{{"type", "annot.add"}, {"annotations", QJsonArray{}}}},
        {"add: an annotation a number", QJsonObject{{"type", "annot.add"}, {"annotations", QJsonArray{7}}}},
        {"add: no id", withAnnotation("id", QJsonValue::Undefined)},
        {"add: an object id", withAnnotation("id", "obj:12")},
        {"add: a direct id", withAnnotation("id", "direct:0:1")},
        {"add: nm not the id", withAnnotation("nm", "something-else")},
        {"add: a 10 MB nm", withAnnotation("id", QStringLiteral("nm:") + huge)},
        {"add: an unknown subtype", withAnnotation("subtype", "Bogus")},
        {"add: no rect", withAnnotation("rect", QJsonValue::Undefined)},
        {"add: a 3-number rect", withAnnotation("rect", QJsonArray{1, 2, 3})},
        {"add: a non-finite rect flagged", withAnnotation("rectFinite", false)},
        {"add: page -1", withAnnotation("page", -1)},
        {"add: page past the limit", withAnnotation("page", core::AnnotationCommandLimits::kMaxPageIndex + 1)},
        {"add: too much geometry", withAnnotation("quadPoints", hugeQuads)},
        {"add: a colour of 5", withAnnotation("color", QJsonArray{1, 1, 1, 1, 1})},
        {"add: opacity 2", withAnnotation("opacity", 2.0)},
        {"add: a negative border", withAnnotation("borderWidth", -1.0)},
        {"delete: ids missing", QJsonObject{{"type", "annot.delete"}}},
        {"delete: ids empty", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{}}}},
        {"delete: an id a number", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{12}}}},
        {"delete: an id of no form", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{"12"}}}},
        {"delete: object 0", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{"obj:0"}}}},
        {"delete: a negative position", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{"direct:0:-1"}}}},
        {"delete: a direct page past the limit",
         QJsonObject{{"type", "annot.delete"},
                     {"ids", QJsonArray{QStringLiteral("direct:%1:0").arg(core::AnnotationCommandLimits::kMaxPageIndex + 1)}}}},
        {"delete: an empty nm", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{"nm:"}}}},
        {"delete: a 10 MB id", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{QStringLiteral("nm:") + huge}}}},
        {"delete: too many ids", QJsonObject{{"type", "annot.delete"}, {"ids", tooMany}}},
    };
    for (const auto& [label, json] : hostile) {
        INFO("input: " << label);
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
        CHECK(result.error().context.size() < 64);
    }

    // THE CONTROL: the good command, and every boundary, is accepted.
    CHECK(CommandRegistry::deserialize(good));
    CHECK(CommandRegistry::deserialize(withAnnotation("page", core::AnnotationCommandLimits::kMaxPageIndex)));
    CHECK(CommandRegistry::deserialize(withAnnotation("opacity", 0.0)));
    CHECK(CommandRegistry::deserialize(
        QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{"obj:1", "direct:0:0", "nm:x"}}}));
}

TEST_CASE("CommandRegistry refuses a malformed reply or status by Result", "[cmd][serialize][hostile][annotops][T9]")
{
    // Stage 6 T9 (D6-12). Each entry breaks ONE thing about a good reply or a good status -- what each promises
    // about its annotations is checked in the parser too, so a hostile journal cannot add a visible comment
    // under the name of a status.
    core::Annotation parent = sampleHighlight(0);
    parent.id = core::AnnotationId::ofObject(12);
    const core::PdfRect page{core::PdfPoint{0, 0}, core::PdfSize{612, 792}};
    const QJsonObject reply = core::newReply(parent, page, QStringLiteral("Agreed."), {1, 1, 0}, QStringLiteral("Ana"), 1791504000).toJson();
    const QJsonObject status =
        core::newStateReply(parent, QStringLiteral("Review"), QStringLiteral("Accepted"), QStringLiteral("Ben"), 1791504000).toJson();
    QJsonObject stated = reply;
    stated.insert(QStringLiteral("stateModel"), QStringLiteral("Review"));
    const auto with = [](const char* type, QJsonObject annotation, const char* key, const QJsonValue& value) {
        if (value.isUndefined()) annotation.remove(QLatin1String(key));
        else annotation.insert(QLatin1String(key), value);
        return QJsonObject{{"type", QLatin1String(type)}, {"annotations", QJsonArray{annotation}}};
    };
    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"reply: a highlight", with("annot.reply", reply, "subtype", "Highlight")},
        {"reply: no parent", with("annot.reply", reply, "inReplyTo", QJsonValue::Undefined)},
        {"reply: a group member", with("annot.reply", reply, "replyType", "Group")},
        {"reply: a state", with("annot.reply", stated, "state", "Accepted")},
        {"reply: a parent that is a direct dictionary", with("annot.reply", reply, "inReplyTo", "direct:0:1")},
        {"reply: an object id", with("annot.reply", reply, "id", "obj:40")},
        {"status: no model", with("annot.setStatus", status, "stateModel", QJsonValue::Undefined)},
        {"status: an unknown model", with("annot.setStatus", status, "stateModel", "Approval")},
        {"status: a state its model does not allow", with("annot.setStatus", status, "state", "Marked")},
        {"status: an unknown state", with("annot.setStatus", status, "state", "Maybe")},
        {"status: not hidden", with("annot.setStatus", status, "flags", 28)},
        {"status: no parent", with("annot.setStatus", status, "inReplyTo", QJsonValue::Undefined)},
        {"status: a square", with("annot.setStatus", status, "subtype", "Square")},
    };
    for (const auto& [label, json] : hostile) {
        INFO("input: " << label);
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
    }
    // THE CONTROL: both good ones are accepted, and a reply is not a status nor a status a reply.
    CHECK(CommandRegistry::deserialize(QJsonObject{{"type", "annot.reply"}, {"annotations", QJsonArray{reply}}}));
    CHECK(CommandRegistry::deserialize(QJsonObject{{"type", "annot.setStatus"}, {"annotations", QJsonArray{status}}}));
    CHECK_FALSE(CommandRegistry::deserialize(QJsonObject{{"type", "annot.reply"}, {"annotations", QJsonArray{status}}}));
    CHECK_FALSE(CommandRegistry::deserialize(QJsonObject{{"type", "annot.setStatus"}, {"annotations", QJsonArray{reply}}}));
}

TEST_CASE("RotatePageCommand normalizes a counter-clockwise turn and marks a non-quarter turn invalid", "[cmd][serialize]")
{
    CHECK(RotatePageCommand(0, -90).degrees() == 270);
    CHECK(RotatePageCommand(0, 450).degrees() == 90);
    CHECK(RotatePageCommand(0, -90).isValid());
    CHECK_FALSE(RotatePageCommand(0, 45).isValid());
    CHECK_FALSE(RotatePageCommand(0, 0).isValid());
    CHECK_FALSE(RotatePageCommand(0, 360).isValid());
    CHECK_FALSE(RotatePageCommand(-1, 90).isValid());
    CHECK_FALSE(RotatePageCommand(RotatePageCommand::kMaxPageIndex + 1, 90).isValid());
    CHECK(RotatePageCommand(4, 90).name()
          == core::CatalogText{QLatin1String("command.page.rotate"), {"5", "90"}});
}
