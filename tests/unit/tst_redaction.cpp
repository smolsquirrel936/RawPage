// Stage 6 T14 -- a redaction MARK (`core/doc/Redaction`, D6-25): what a tool makes, its two appearances -- `/AP /N`, the
// outline, and `/RO`, what the area will look like once applied -- saved, and EVERY CONTENT STREAM OF EVERY PAGE
// BYTE-IDENTICAL afterwards (guide case A-100's headless half); its overlay text, fill and repeat edited as one
// command; a corpus mark read as the generator labelled it; and a mark through XFDF and FDF.
//
// THE ORACLE FOR "REMOVES NOTHING" IS THE ORIGINAL FILE'S BYTES: each page's `/Contents` -- the reference written in
// its page dictionary and every byte of every stream it names -- is read from the file as opened and from the file as
// saved, and compared byte for byte. Nothing our own reader decides stands between the two.
//
// WHAT IT DOES NOT COVER (convention 19): whether the text is still EXTRACTED and FOUND, and whether the screen shows what
// the file holds -- both need the engine, and are `tst_redactink`'s, through the tool (A-100, A-34); and whether Acrobat
// shows `/RO` as RawPage drew it (M-02).

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/Attachments.h"
#include "core/doc/Fdf.h"
#include "core/doc/Redaction.h"
#include "core/doc/TypedText.h"
#include "core/doc/XfdfCodec.h"
#include "core/settings/CommentingTypes.h"
#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/FdfCodec.h"
#include "pdf/ops/PageOps.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace rawpage;
using core::Annotation;
using core::AnnotationSubtype;
using pdf::cos::CosObject;

namespace {

constexpr std::int64_t kNow = 1792108800;   // 2026-10-16T00:00:00Z
const QString kAuthor = QStringLiteral("Amara Nwosu");

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// "REDACTED" as `showText` writes it: Helvetica's WinAnsi codes in hex.
const std::string kRedactedHex = "<5245444143544544>";

core::RedactionStyle styleWith(const QString& overlay, bool repeat, std::vector<double> fill = {0.0, 0.0, 0.0})
{
    core::RedactionStyle style;
    style.overlayText = overlay;
    style.repeat = repeat;
    style.fill = std::move(fill);
    return style;
}

// Two lines of text in Acrobat's corner order (top-left, top-right, bottom-left, bottom-right).
std::vector<double> twoLines()
{
    return {72, 712, 300, 712, 72, 700, 300, 700, 72, 696, 220, 696, 72, 684, 220, 684};
}

// EVERY PAGE'S CONTENT: what its dictionary's `/Contents` says, and each stream it names as raw bytes, by number.
struct PageContent {
    CosObject contents;
    std::vector<std::pair<long long, std::string>> streams;
    bool operator==(const PageContent&) const = default;
};

std::vector<PageContent> contentOf(const QByteArray& bytes)
{
    auto opened = pdf::cos::openWithRepair(viewOf(bytes));
    REQUIRE(opened);
    pdf::cos::XrefTable& table = opened.value().table;
    auto walked = pdf::ops::PageOps::walk(table);
    REQUIRE(walked);
    std::vector<PageContent> out;
    for (const pdf::ops::PageEntry& page : walked.value().pages) {
        auto dictionary = table.fetch(page.number);
        REQUIRE(dictionary);
        PageContent content;
        const CosObject* value = dictionary.value().object.find("Contents");
        REQUIRE(value != nullptr);
        content.contents = *value;
        content.contents.clearSpansRecursively();
        std::vector<CosObject> refs;
        if (value->isReference()) refs.push_back(*value);
        else if (value->isArray()) refs = value->items();
        for (const CosObject& ref : refs) {
            REQUIRE(ref.isReference());
            auto stream = table.fetch(ref.reference().number);
            REQUIRE(stream);
            REQUIRE(stream.value().object.isStream());
            content.streams.emplace_back(ref.reference().number, std::string(stream.value().object.rawStreamData(table.source())));
        }
        REQUIRE_FALSE(content.streams.empty());
        out.push_back(std::move(content));
    }
    return out;
}

struct ReadBack {
    QByteArray bytes;
    std::optional<pdf::cos::XrefTable> table;
    core::AnnotationStore store;

    explicit ReadBack(QByteArray fileBytes) : bytes(std::move(fileBytes))
    {
        auto opened = pdf::cos::openWithRepair(viewOf(bytes));
        REQUIRE(opened);
        table.emplace(std::move(opened.value().table));
        auto walked = pdf::ops::PageOps::walk(*table);
        REQUIRE(walked);
        store = core::AnnotationStore::fromReading(pdf::ops::AnnotationReader::read(*table, walked.value()));
    }
    CosObject object(long long number)
    {
        auto fetched = table->fetch(number);
        REQUIRE(fetched);
        return fetched.value().object;
    }
    const Annotation* withNm(const QString& nm) const
    {
        for (const Annotation& a : store.all())
            if (a.nm == nm) return &a;
        return nullptr;
    }
    // The form `key` (`AP /N` or `RO`) of annotation object `number`, as raw bytes; empty for none.
    std::string form(long long number, bool overlay)
    {
        const CosObject d = object(number);
        const CosObject& ref = overlay ? d.get("RO") : d.get("AP").get("N");
        if (!ref.isReference()) return {};
        const CosObject stream = object(ref.reference().number);
        if (!stream.isStream() || stream.has("Filter")) return {};
        return std::string(stream.rawStreamData(table->source()));
    }
};

QByteArray save(tests::SaveSession& session, const QString& keepAs)
{
    auto result = session.coordinator->save(*session.journal, session.path);
    INFO("save error: " << (result ? std::string() : keyOf(result.error()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    REQUIRE(result.value().kind == core::SaveOutcome::Kind::Saved);
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);   // kept for `qpdf --check` (A-31)
    REQUIRE(QDir().mkpath(dir));
    const QString target = QDir(dir).filePath(keepAs);
    QFile::remove(target);
    REQUIRE(QFile::copy(session.path, target));
    return tests::readAllBytes(session.path);
}

std::size_t count(const std::string& haystack, const std::string& needle)
{
    std::size_t n = 0;
    for (std::size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + needle.size())) ++n;
    return n;
}

QJsonObject everyTypeMark()
{
    QFile file(tests::corpusFile("labels/annots-every-type.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonObject label = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue page : label.value(QStringLiteral("pages")).toArray()) {
        for (const QJsonValue entry : page.toObject().value(QStringLiteral("annots")).toArray()) {
            if (entry.toObject().value(QStringLiteral("subtype")).toString() == QLatin1String("Redact")) return entry.toObject();
        }
    }
    FAIL("annots-every-type.json holds no Redact");
    return {};
}

} // namespace

TEST_CASE("a mark is made over text or an area with its overlay readable on its fill", "[redaction][A-100][D6-25]")
{
    const std::optional<Annotation> text = core::newTextRedaction(2, twoLines(), styleWith(QStringLiteral("REDACTED"), true),
                                                                  kAuthor, kNow);
    REQUIRE(text);
    CHECK(text->subtype == AnnotationSubtype::Redact);
    CHECK(text->page == 2);
    CHECK(text->quadPoints == twoLines());
    CHECK(text->rect.left() <= 72.0);
    CHECK(text->rect.top() >= 712.0);
    CHECK(text->color == std::vector<double>{1.0, 0.0, 0.0});         // Acrobat's red outline
    CHECK(text->interiorColor == std::vector<double>{0.0, 0.0, 0.0}); // a black fill once applied
    CHECK(text->overlayText == QLatin1String("REDACTED"));
    CHECK(text->repeat);
    CHECK(text->hasAppearance);
    const std::optional<core::DefaultAppearance> da = core::DefaultAppearance::parse(text->defaultAppearance);
    REQUIRE(da);
    CHECK(da->font == QLatin1String("Helv"));
    CHECK(da->size == core::kRedactionDefaultTextSize);
    CHECK(da->colour == std::vector<double>{1.0});   // white on black

    const std::optional<Annotation> area = core::newAreaRedaction(0, core::PdfRect{core::PdfPoint{300, 100}, core::PdfSize{-120, 80}},
                                                                  styleWith(QString(), false, {1.0, 1.0, 1.0}), kAuthor, kNow);
    REQUIRE(area);
    CHECK(area->quadPoints.empty());
    CHECK(area->rect.left() == 180.0);   // normalized
    CHECK(area->rect.size.width == 120.0);
    CHECK(core::DefaultAppearance::parse(area->defaultAppearance)->colour == std::vector<double>{0.0});   // black on white
    CHECK_FALSE(core::newAreaRedaction(0, core::PdfRect{core::PdfPoint{1, 1}, core::PdfSize{0, 5}}, {}, kAuthor, kNow));
    CHECK_FALSE(core::newTextRedaction(0, {1, 2, 3}, {}, kAuthor, kNow));

    // A fill changed keeps the text readable -- and a CMYK or gray fill is judged by its light.
    CHECK(core::redactionTextColour({0.2}) == std::vector<double>{1.0});
    CHECK(core::redactionTextColour({0.0, 0.0, 0.0, 0.1}) == std::vector<double>{0.0});
    CHECK(core::redactionTextColour({}) == std::vector<double>{0.0});
    const Annotation white = core::withRedactionFill(*text, {1.0, 1.0, 0.9});
    CHECK(core::DefaultAppearance::parse(white.defaultAppearance)->colour == std::vector<double>{0.0});
    CHECK(core::DefaultAppearance::parse(white.defaultAppearance)->size == core::kRedactionDefaultTextSize);
}

TEST_CASE("a mark's outline and its overlay are drawn and well formed", "[redaction][A-100][D6-25]")
{
    const std::optional<Annotation> once = core::newTextRedaction(0, twoLines(), styleWith(QStringLiteral("REDACTED"), false),
                                                                  kAuthor, kNow);
    REQUIRE(once);
    const auto outline = pdf::ops::AppearanceStreamBuilder::normal(*once);
    REQUIRE(outline);
    CHECK(outline.value().data.find("1 0 0 RG") != std::string::npos);   // red
    CHECK(count(outline.value().data, " S") + count(outline.value().data, "\nS") >= 1);
    CHECK(outline.value().data.find(" f") == std::string::npos);         // an outline fills nothing

    const auto overlay = pdf::ops::AppearanceStreamBuilder::redactionOverlay(*once);
    REQUIRE(overlay);
    CHECK(pdf::cos::ContentStreamBuilder::check(overlay.value().data));
    CHECK(overlay.value().data.find("0 g") != std::string::npos);        // the black fill
    CHECK(count(overlay.value().data, kRedactedHex) == 2);               // once in each line's region
    CHECK(overlay.value().dictionary.get("Resources").get("Font").find("Helv") != nullptr);

    // REPEATED: tiled -- many copies in each region, each clipped to it.
    Annotation tiled = *once;
    tiled.repeat = true;
    const auto repeated = pdf::ops::AppearanceStreamBuilder::redactionOverlay(tiled);
    REQUIRE(repeated);
    CHECK(count(repeated.value().data, kRedactedHex) > 4);
    CHECK(count(repeated.value().data, " W") + count(repeated.value().data, "\nW") >= 2);

    // NO FILL: the overlay text alone. NO TEXT: the fill alone.
    Annotation clear = *once;
    clear.interiorColor.clear();
    CHECK(pdf::ops::AppearanceStreamBuilder::redactionOverlay(clear).value().data.find(" f") == std::string::npos);
    Annotation blank = *once;
    blank.overlayText.clear();
    CHECK(count(pdf::ops::AppearanceStreamBuilder::redactionOverlay(blank).value().data, "Tj") == 0);

    // A character no face can show is refused BY NAME, as typed text is (D6-7); and `/RO` belongs to a mark alone.
    Annotation greek = *once;
    greek.overlayText = QStringLiteral("Ω");
    const auto refused = pdf::ops::AppearanceStreamBuilder::redactionOverlay(greek);
    REQUIRE_FALSE(refused);
    CHECK(keyOf(refused.error()) == "error.edit.typedCharacterRefused");
    CHECK_FALSE(pdf::ops::MutationSinkImpl::canCreate(greek));
    Annotation highlight = core::newAnnotation(AnnotationSubtype::Highlight, 0, kAuthor, kNow);
    CHECK_FALSE(pdf::ops::AppearanceStreamBuilder::redactionOverlay(highlight));
}

TEST_CASE("marking text and an area saves the marks and leaves every content stream byte-identical",
          "[redaction][A-100][D6-25]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const std::vector<PageContent> before = contentOf(session->openedBytes);
    const std::optional<Annotation> text = core::newTextRedaction(0, twoLines(), styleWith(QStringLiteral("REDACTED"), true),
                                                                  kAuthor, kNow);
    const std::optional<Annotation> area = core::newAreaRedaction(0, core::PdfRect{core::PdfPoint{350, 400}, core::PdfSize{150, 90}},
                                                                  styleWith(QStringLiteral("REDACTED"), false), kAuthor, kNow);
    REQUIRE(text);
    REQUIRE(area);
    REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{*text})));
    REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{*area})));
    ReadBack back(save(*session, QStringLiteral("redaction-marks.pdf")));

    // REMOVES NOTHING: every page's /Contents written as it was, every stream it names byte for byte.
    const std::vector<PageContent> after = contentOf(back.bytes);
    REQUIRE(after.size() == before.size());
    for (std::size_t page = 0; page < before.size(); ++page) {
        INFO("page " << page + 1);
        CHECK(after[page] == before[page]);
    }
    // And the original file is a byte-identical prefix of the saved one: an incremental update appended.
    CHECK(back.bytes.left(session->openedBytes.size()) == session->openedBytes);

    for (const Annotation* made : {back.withNm(text->nm), back.withNm(area->nm)}) {
        REQUIRE(made != nullptr);
        REQUIRE(made->id.kind == core::AnnotationId::Kind::Object);
        CHECK(made->subtype == AnnotationSubtype::Redact);
        CHECK(made->overlayText == QLatin1String("REDACTED"));
        CHECK(made->interiorColor == std::vector<double>{0.0, 0.0, 0.0});
        const std::string outline = back.form(made->id.object, false);
        const std::string overlay = back.form(made->id.object, true);
        REQUIRE_FALSE(outline.empty());
        REQUIRE_FALSE(overlay.empty());
        CHECK(pdf::cos::ContentStreamBuilder::check(outline));
        CHECK(pdf::cos::ContentStreamBuilder::check(overlay));
        CHECK(count(overlay, kRedactedHex) >= 1);
    }
    const Annotation* readText = back.withNm(text->nm);
    CHECK(readText->repeat);
    CHECK(readText->quadPoints.size() == 16);
    CHECK_FALSE(back.withNm(area->nm)->repeat);
    CHECK(back.withNm(area->nm)->quadPoints.empty());
    // `/Repeat false` is ISO's default and written as nothing; `/Repeat true` written.
    CHECK(back.object(back.withNm(area->nm)->id.object).find("Repeat") == nullptr);
    CHECK(back.object(readText->id.object).get("Repeat").booleanValue());
}

TEST_CASE("a mark's overlay text fill and repeat are one edit and redraw its overlay", "[redaction][A-100][D6-25]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const std::optional<Annotation> mark = core::newTextRedaction(0, twoLines(), styleWith(QStringLiteral("REDACTED"), false),
                                                                  kAuthor, kNow);
    REQUIRE(mark);
    REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{*mark})));
    (void)save(*session, QStringLiteral("redaction-before-edit.pdf"));
    const core::Annotation* saved = nullptr;
    for (const Annotation& a : session->journal->document().annotations().all())
        if (a.nm == mark->nm) saved = &a;
    REQUIRE(saved != nullptr);
    const core::AnnotationId id = saved->id;

    core::AnnotationAppearance change;
    change.overlayText = QStringLiteral("WITHHELD");
    change.repeat = true;
    change.interiorColor = std::vector<double>{1.0, 1.0, 1.0};
    const std::size_t steps = session->journal->position();
    REQUIRE(session->journal->run(std::make_unique<core::SetAnnotationAppearanceCommand>(std::vector<core::AnnotationId>{id}, change, kNow)));
    CHECK(session->journal->position() == steps + 1);   // ONE undo step
    const pdf::ops::MutationSinkImpl::Edit* edit = session->sink()->editOf(id);
    REQUIRE(edit != nullptr);
    CHECK(edit->redraw);
    ReadBack back(save(*session, QStringLiteral("redaction-edited.pdf")));
    const Annotation* edited = back.withNm(mark->nm);
    REQUIRE(edited != nullptr);
    CHECK(edited->overlayText == QLatin1String("WITHHELD"));
    CHECK(edited->repeat);
    CHECK(edited->interiorColor == std::vector<double>{1.0, 1.0, 1.0});
    CHECK(core::DefaultAppearance::parse(edited->defaultAppearance)->colour == std::vector<double>{0.0});   // black on white
    const std::string overlay = back.form(edited->id.object, true);
    CHECK(count(overlay, "<5749544848454C44>") > 2);   // "WITHHELD", tiled
    CHECK(count(overlay, kRedactedHex) == 0);

    // A hostile line: an overlay text past the bound, or one on something that is no mark, is refused by the parser.
    QJsonObject line = core::SetAnnotationAppearanceCommand(std::vector<core::AnnotationId>{id}, change, kNow).serialize();
    QJsonObject appearance = line.value(QStringLiteral("appearance")).toObject();
    appearance.insert(QStringLiteral("overlayText"), QString(core::kMaxOverlayTextLength + 1, QLatin1Char('x')));
    line.insert(QStringLiteral("appearance"), appearance);
    CHECK_FALSE(core::CommandRegistry::deserialize(line));
    Annotation onHighlight = core::newAnnotation(AnnotationSubtype::Highlight, 0, kAuthor, kNow);
    const std::vector<double> lines = twoLines();
    onHighlight.quadPoints = std::vector<double>(lines.begin(), lines.begin() + 8);
    onHighlight.rect = *core::markupRect(onHighlight.quadPoints);
    onHighlight.overlayText = QStringLiteral("NOT A MARK");
    CHECK_FALSE(core::CommandRegistry::deserialize(core::AddAnnotationCommand(std::vector<Annotation>{onHighlight}).serialize()));
}

TEST_CASE("the corpus's mark is read as labelled and redrawn with its overlay when recoloured", "[redaction][A-100][D6-25]")
{
    const QJsonObject want = everyTypeMark();
    auto session = tests::openCopy("ordinary/annots-every-type.pdf");
    REQUIRE(session);
    const std::vector<PageContent> before = contentOf(session->openedBytes);
    const Annotation* mark = nullptr;
    for (const Annotation& a : session->journal->document().annotations().all())
        if (a.subtype == AnnotationSubtype::Redact) mark = &a;
    REQUIRE(mark != nullptr);
    CHECK(mark->overlayText == want.value(QStringLiteral("overlayText")).toString());
    CHECK(mark->repeat == want.value(QStringLiteral("repeat")).toBool());
    CHECK(mark->quadPoints.size() == static_cast<std::size_t>(want.value(QStringLiteral("quadPoints")).toArray().size()));
    const core::AnnotationId id = mark->id;
    core::AnnotationAppearance blue;
    blue.color = std::vector<double>{0.0, 0.0, 1.0};
    REQUIRE(session->journal->run(std::make_unique<core::SetAnnotationAppearanceCommand>(std::vector<core::AnnotationId>{id}, blue, kNow)));
    ReadBack back(save(*session, QStringLiteral("redaction-corpus-recoloured.pdf")));
    REQUIRE(id.kind == core::AnnotationId::Kind::Object);
    const std::string outline = back.form(id.object, false);
    CHECK(outline.find("0 0 1 RG") != std::string::npos);
    CHECK(count(back.form(id.object, true), "<5245444143544544>") == 1);   // its /RO drawn anew, its overlay kept
    const std::vector<PageContent> after = contentOf(back.bytes);
    REQUIRE(after.size() == before.size());
    for (std::size_t page = 0; page < before.size(); ++page) CHECK(after[page] == before[page]);
}

TEST_CASE("the redaction tool's overlay text and repeat are keys of the tool defaults family", "[redaction][D6-13][D6-25]")
{
    // Two properties appended at T14 (D6-25): declared only for the mark, validated on every read.
    CHECK(core::ToolDefaultKey::of<core::CommentingTool::RedactMark, core::ToolProperty::OverlayText>().path()
          == QLatin1String("commenting/defaultOverlayText.redactMark"));
    CHECK(core::ToolDefaultKey::of<core::CommentingTool::RedactMark, core::ToolProperty::Repeat>().path()
          == QLatin1String("commenting/defaultRepeat.redactMark"));
    CHECK_FALSE(core::ToolDefaultKey::find(core::CommentingTool::Highlight, core::ToolProperty::OverlayText));
    CHECK_FALSE(core::ToolDefaultKey::find(core::CommentingTool::RecordAudio, core::ToolProperty::Colour));
    const core::ToolDefaults defaults = core::builtInToolDefaults(core::CommentingTool::RedactMark);
    CHECK(defaults.colour == std::vector<double>{1.0, 0.0, 0.0});
    CHECK(defaults.fillColour == std::vector<double>{0.0, 0.0, 0.0});
    CHECK(defaults.fontSize == 10.0);
    CHECK(defaults.overlayText.isEmpty());
    CHECK_FALSE(defaults.repeat);
    core::ToolDefaults v;
    CHECK(core::parseToolProperty(core::ToolProperty::OverlayText, QStringLiteral("Personal data — removed"), v));
    CHECK(v.overlayText == QStringLiteral("Personal data — removed"));
    CHECK(core::formatToolProperty(core::ToolProperty::OverlayText, v) == v.overlayText);
    CHECK_FALSE(core::parseToolProperty(core::ToolProperty::OverlayText, QStringLiteral("two\nlines"), v));
    CHECK_FALSE(core::parseToolProperty(core::ToolProperty::OverlayText, QString(core::kToolOverlayTextMax + 1, QLatin1Char('x')), v));
    CHECK(core::parseToolProperty(core::ToolProperty::Repeat, QStringLiteral("true"), v));
    CHECK(v.repeat);
    CHECK(core::formatToolProperty(core::ToolProperty::Repeat, v) == QLatin1String("true"));
    CHECK_FALSE(core::parseToolProperty(core::ToolProperty::Repeat, QStringLiteral("yes"), v));
    CHECK(core::toolPropertyFromName("OverlayText") == core::ToolProperty::OverlayText);
}

TEST_CASE("a mark round-trips XFDF and FDF with its overlay and repeat", "[redaction][D6-25][D6-20]")
{
    const std::optional<Annotation> made = core::newTextRedaction(0, twoLines(), styleWith(QStringLiteral("CONFIDENTIAL"), true),
                                                                  kAuthor, kNow);
    REQUIRE(made);
    core::Fdf exchange;
    exchange.annotations.push_back(*made);
    for (const bool xfdf : {true, false}) {
        INFO((xfdf ? "XFDF" : "FDF"));
        const QByteArray bytes = xfdf ? core::XfdfCodec::write(exchange) : QByteArray::fromStdString(pdf::ops::FdfCodec::write(exchange));
        const core::Result<core::Fdf> back = pdf::ops::FdfCodec::readEither(bytes);
        REQUIRE(back);
        REQUIRE(back.value().annotations.size() == 1);
        const Annotation& a = back.value().annotations.front();
        CHECK(a.subtype == AnnotationSubtype::Redact);
        CHECK(a.overlayText == QLatin1String("CONFIDENTIAL"));
        CHECK(a.repeat);
        CHECK(a.quadPoints == made->quadPoints);
        CHECK(a.interiorColor == made->interiorColor);
        CHECK(a.defaultAppearance == made->defaultAppearance);
    }
}

// ==========================================================================================
// A-26, A-27 and A-32 for the kinds T11 and T14 write -- Stage 6 T15's sweep
// ==========================================================================================
// Each earlier task closed these three cases for the kinds it wrote; the pushpin (T11), the redaction mark and the
// sound (T14) are the last. Here they are made by their own constructors (the tools' paths are A-108's and
// `tst_redactink`'s), saved together, and read back FIRST-PARTY.

namespace {

// Every number in a JSON value to three decimals: what is written is `%.3f`-ish, what was made is a double.
QJsonValue rounded(const QJsonValue& value)
{
    if (value.isDouble()) return std::round(value.toDouble() * 1000.0) / 1000.0;
    if (value.isArray()) {
        QJsonArray out;
        for (const QJsonValue item : value.toArray()) out.append(rounded(item));
        return out;
    }
    if (value.isObject()) {
        QJsonObject out;
        const QJsonObject in = value.toObject();
        for (auto it = in.begin(); it != in.end(); ++it) out.insert(it.key(), rounded(it.value()));
        return out;
    }
    return value;
}

// An annotation's every modelled field as JSON, less what a save legitimately changes: the identity (`Created` before,
// `Object` after) and the PAYLOAD's bytes, which a made annotation carries and a read one is asked for (D6-17, D6-18 --
// compared below by hash).
QJsonObject modelled(const Annotation& a)
{
    QJsonObject json = a.toJson();
    json.remove(QStringLiteral("id"));
    for (const char* payload : {"attachment", "sound"}) {
        if (!json.contains(QLatin1String(payload))) continue;
        QJsonObject inner = json.value(QLatin1String(payload)).toObject();
        inner.remove(QStringLiteral("data"));
        inner.remove(QStringLiteral("samples"));
        json.insert(QLatin1String(payload), inner);
    }
    return rounded(json).toObject();
}

QByteArray monoWav(int frames)
{
    QByteArray data;
    for (int f = 0; f < frames; ++f) {
        const auto sample = static_cast<std::uint16_t>(static_cast<std::int16_t>((f % 64) * 500 - 16000));
        data.append(static_cast<char>(sample & 0xff));
        data.append(static_cast<char>(sample >> 8));
    }
    const auto le32 = [](QByteArray& out, std::uint32_t v) { for (int i = 0; i < 4; ++i) out.append(static_cast<char>((v >> (8 * i)) & 0xff)); };
    const auto le16 = [](QByteArray& out, std::uint16_t v) { out.append(static_cast<char>(v & 0xff)); out.append(static_cast<char>(v >> 8)); };
    QByteArray body("WAVEfmt ", 8);
    le32(body, 16);
    le16(body, 1);
    le16(body, 1);
    le32(body, 11025);
    le32(body, 22050);
    le16(body, 2);
    le16(body, 16);
    body.append("data", 4);
    le32(body, static_cast<std::uint32_t>(data.size()));
    body.append(data);
    QByteArray out("RIFF", 4);
    le32(out, static_cast<std::uint32_t>(body.size()));
    return out + body;
}

std::string sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

} // namespace

TEST_CASE("the pushpin the redaction mark and the sound keep their identity and every modelled field through a save",
          "[redaction][A-27][A-32][D6-4][D6-17][D6-18][D6-25]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const QByteArray carried("A-32: the pushpin's file\n");
    const Annotation pin = core::newFileAttachment(0, core::PdfRect{core::PdfPoint{500, 600}, core::PdfSize{20, 20}},
                                                   core::newEmbeddedFile(QStringLiteral("notes.txt"), carried, QStringLiteral("a note"), kNow),
                                                   {0.2, 0.4, 0.9}, kAuthor, kNow);
    const std::optional<Annotation> mark = core::newTextRedaction(0, twoLines(), styleWith(QStringLiteral("REDACTED"), true),
                                                                  kAuthor, kNow);
    REQUIRE(mark);
    const core::Result<core::SoundData> wav = core::soundFromWav(monoWav(1500));
    REQUIRE(wav);
    const Annotation sound = core::newSoundComment(0, core::PdfPoint{500, 760}, wav.value(), {1.0, 0.85, 0.3}, kAuthor, kNow);
    REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{pin, *mark, sound})));
    ReadBack back(save(*session, QStringLiteral("T15-pushpin-mark-sound.pdf")));

    for (const Annotation* made : {&pin, &*mark, &sound}) {
        INFO(QString(core::annotationSubtypeName(made->subtype)).toStdString());
        const Annotation* read = back.withNm(made->nm);
        // A-27: found by the `/NM` RawPage gave it, an object of the file now, with the identity keys written.
        REQUIRE(read != nullptr);
        CHECK(read->id.kind == core::AnnotationId::Kind::Object);
        const CosObject d = back.object(read->id.object);
        CHECK(d.has("NM"));
        CHECK(d.has("M"));
        CHECK(d.has("CreationDate"));
        CHECK(d.get("P").isReference());
        CHECK(d.get("AP").get("N").isReference());
        // A-32: every modelled field as made.
        const QJsonObject want = modelled(*made);
        const QJsonObject got = modelled(*read);
        INFO("made " << QJsonDocument(want).toJson(QJsonDocument::Compact).toStdString());
        INFO("read " << QJsonDocument(got).toJson(QJsonDocument::Compact).toStdString());
        CHECK(got == want);
    }
    // And the payloads, by hash: the pushpin's file and the sound's samples, from a document opened on the saved file.
    tests::CosBackend fresh;
    auto handle = fresh.open(session->path, nullptr);
    REQUIRE(handle);
    const auto file = fresh.attachmentData(handle.value(), core::AttachmentRef::ofAnnotation(back.withNm(pin.nm)->id));
    REQUIRE(file);
    CHECK(sha256(file.value()) == sha256(carried));
    const auto samples = fresh.soundSamples(handle.value(), back.withNm(sound.nm)->id);
    REQUIRE(samples);
    CHECK(sha256(samples.value()) == sha256(wav.value().samples));
    (void)fresh.close(handle.value());
}

TEST_CASE("the corpus's mark recoloured keeps every key it does not model", "[redaction][A-26][D6-5][D6-25]")
{
    auto session = tests::openCopy("ordinary/annots-every-type.pdf");
    REQUIRE(session);
    ReadBack before(session->openedBytes);
    std::vector<core::AnnotationId> ids;
    for (const Annotation& a : session->journal->document().annotations().all())
        if (a.subtype == AnnotationSubtype::Redact) ids.push_back(a.id);
    REQUIRE(ids.size() == 1);   // the generator's mark, its own outline and overlay
    REQUIRE(ids.front().kind == core::AnnotationId::Kind::Object);
    core::AnnotationAppearance blue;
    blue.color = std::vector<double>{0.0, 0.0, 1.0};
    REQUIRE(session->journal->run(std::make_unique<core::SetAnnotationAppearanceCommand>(ids, blue, kNow)));
    ReadBack after(save(*session, QStringLiteral("T15-corpus-mark-recoloured.pdf")));
    const CosObject was = before.object(ids.front().object);
    const CosObject now = after.object(ids.front().object);
    // What a recolour owns: the colour, the instant, and the two appearances it redraws (D6-25's outline and `/RO`).
    for (const CosObject::Entry& entry : was.entries()) {
        if (entry.key == "C" || entry.key == "M" || entry.key == "AP" || entry.key == "RO") continue;
        INFO(entry.key);
        CosObject a = entry.value;
        CosObject b = now.get(entry.key);
        a.clearSpansRecursively();
        b.clearSpansRecursively();
        CHECK(a == b);
    }
    // And nothing added but what it owns.
    for (const CosObject::Entry& entry : now.entries()) {
        INFO(entry.key);
        CHECK((was.has(entry.key) || entry.key == "C" || entry.key == "M" || entry.key == "AP" || entry.key == "RO"));
    }
}
