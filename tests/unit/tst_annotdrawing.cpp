// tst_annotdrawing -- Stage 6 T8 headless: what the drawing tools write, below the widgets
// (`Stage6-Annotations-Testing-Guide.md` §5.10, §5.11; A-58's file half, A-60's command, A-63's graphics state,
// A-66; A-26's, A-27's, A-29's and A-32's T8 halves; D6-15, D6-35).
//
//   * A-58, the file half: a line, an arrow, a rectangle, an oval, a polygon, a polyline, a cloud of each
//     intensity and an ink of two strokes -- made as the tools make them (`core::fittedShape` over a
//     `core::newAnnotation`, styled as `ui::styledWith` styles), ONE command, saved, and read back first-party
//     with every key the tool writes: `/L` and `/LE`, `/Vertices`, `/BE << /S /C /I >>` and `/IT /PolygonCloud`,
//     `/InkList`, `/BS << /W /S /D >>`, `/IC`, `/CA`, `/BM` -- and the store read back equal, field for field, to
//     what was made (A-32), each with its identity keys (A-27).
//   * A-66: every appearance the drawing kinds can generate -- every kind, every ending, every line style, every
//     blend, filled and not, cloudy and not -- passes the re-read and names only resources its form holds.
//   * A-63: a highlight's opacity and blend reach its form's graphics state (`/CA`, `/ca`, `/BM /Multiply`), and
//     a blend that is not the kind's own is written to the dictionary as `/BM` too.
//   * A-26: a file's shapes -- the corpus generator's, each with its own appearance -- RECOLOURED are redrawn by
//     our builder now, every key the model does not own kept; a Line carrying leader lines and a measurement
//     are not redrawn (the model does not hold what they draw) and are refused by key.
//   * A-60: the eraser's command -- strokes taken out as `core::erasedStrokes` takes them -- is ONE
//     `EditAnnotationCommand`, and its undo puts back exactly the samples there were.
//   * A-29: the new properties travel in a journal line, and out-of-range ones are refused by key.
//
// WHAT IT DOES NOT COVER (convention 19): the engine -- whether PDFium draws these streams where the geometry
// says is `tests/render/tst_appearance`'s (and its PDFium control, A-65), what they look like is the golden
// suite's (A-64), and the gestures that make them are `tests/ui/tst_drawtools`' and
// `tests/composition/tst_drawink`'s.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/AnnotationGeometry.h"
#include "core/doc/DrawnShapes.h"
#include "core/settings/CommentingTypes.h"
#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/Lexer.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace rawpage;
using core::AddAnnotationCommand;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;
using pdf::cos::CosObject;

namespace {

constexpr std::int64_t kNow = 1791763200;   // 2026-10-12T00:00:00Z, a fixed instant
const QString kAuthor = QStringLiteral("Drawing Tester");

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// A shape as a drawing tool makes one: its identity, author and instant from `newAnnotation`, its geometry,
// styled as `ui::styledWith` styles a tool's defaults -- `/C`, `/BS /W`, a style with its dash -- and fitted.
Annotation shape(AnnotationSubtype subtype, int page, core::LineStyle style = core::LineStyle::Solid, double width = 1.0)
{
    Annotation a = core::newAnnotation(subtype, page, kAuthor, kNow);
    a.color = {1.0, 0.0, 0.0};
    a.borderWidth = width;
    a.borderStyle = style == core::LineStyle::Solid ? QStringLiteral("S") : QStringLiteral("D");
    a.dashPattern = core::dashFor(style, width);
    return a;
}

Annotation fitted(Annotation a)
{
    const std::optional<Annotation> out = core::fittedShape(std::move(a));
    REQUIRE(out);
    return *out;
}

core::Status add(tests::SaveSession& session, std::vector<Annotation> annotations)
{
    return session.journal->run(std::make_unique<AddAnnotationCommand>(std::move(annotations)));
}

QByteArray save(tests::SaveSession& session, const QString& keepAs)
{
    auto result = session.coordinator->save(*session.journal, session.path);
    INFO("save error: " << (result ? std::string() : keyOf(result.error()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    REQUIRE(result.value().kind == core::SaveOutcome::Kind::Saved);
    // Kept for `qpdf --check` (A-31), as every file the headless suite saves is.
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    REQUIRE(QDir().mkpath(dir));
    const QString target = QDir(dir).filePath(keepAs);
    QFile::remove(target);
    REQUIRE(QFile::copy(session.path, target));
    return tests::readAllBytes(session.path);
}

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
    CosObject resolved(const CosObject& value) { return value.isReference() ? object(value.reference().number) : value; }
    const Annotation* withNm(const QString& nm) const
    {
        for (const Annotation& a : store.all())
            if (a.nm == nm) return &a;
        return nullptr;
    }
    const Annotation* byObject(long long number) const
    {
        for (const Annotation& a : store.all())
            if (a.id.kind == AnnotationId::Kind::Object && a.id.object == number) return &a;
        return nullptr;
    }
    CosObject dictionaryOf(const QString& nm)
    {
        const Annotation* a = withNm(nm);
        REQUIRE(a != nullptr);
        REQUIRE(a->id.kind == AnnotationId::Kind::Object);
        return object(a->id.object);
    }
};

bool near(const std::vector<double>& a, const std::vector<double>& b, double tolerance = 1e-3)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(std::abs(a[i] - b[i]) <= tolerance)) return false;
    }
    return true;
}

// Every modelled field of `read` the same as `made`'s, numbers within what a written real keeps -- the
// identity apart (the file names it by its object; the model by its `/NM`, which is compared).
void sameModelled(const Annotation& made, const Annotation& read)
{
    INFO(QJsonDocument(made.toJson()).toJson(QJsonDocument::Compact).toStdString());
    CHECK(read.subtype == made.subtype);
    CHECK(read.nm == made.nm);
    CHECK(read.page == made.page);
    CHECK(near({read.rect.left(), read.rect.bottom(), read.rect.right(), read.rect.top()},
               {made.rect.left(), made.rect.bottom(), made.rect.right(), made.rect.top()}));
    CHECK(near(read.line, made.line));
    CHECK(near(read.vertices, made.vertices));
    REQUIRE(read.inkList.size() == made.inkList.size());
    for (std::size_t i = 0; i < made.inkList.size(); ++i) CHECK(near(read.inkList[i], made.inkList[i]));
    CHECK(read.lineEndings == made.lineEndings);
    CHECK(near(read.color, made.color));
    CHECK(near(read.interiorColor, made.interiorColor));
    CHECK(read.opacity.has_value() == made.opacity.has_value());
    if (read.opacity && made.opacity) CHECK(std::abs(*read.opacity - *made.opacity) < 1e-3);
    REQUIRE(read.borderWidth.has_value() == made.borderWidth.has_value());
    if (read.borderWidth) CHECK(std::abs(*read.borderWidth - *made.borderWidth) < 1e-3);
    CHECK(read.borderStyle == made.borderStyle);
    CHECK(near(read.dashPattern, made.dashPattern));
    CHECK(read.cloudIntensity.has_value() == made.cloudIntensity.has_value());
    if (read.cloudIntensity && made.cloudIntensity) CHECK(std::abs(*read.cloudIntensity - *made.cloudIntensity) < 1e-3);
    CHECK(read.blendMode == made.blendMode);
    CHECK(read.intent == made.intent);
    CHECK(read.author == made.author);
    CHECK(read.created.utcSeconds == made.created.utcSeconds);
    CHECK(read.modified.utcSeconds == made.modified.utcSeconds);
    CHECK(read.flags == made.flags);
    CHECK(read.hasAppearance);
}

// Every resource a content stream NAMES -- the `gs` and `Tf` operands -- and whether the form's resources hold
// each. A name the resources lack is drawn differently by every reader (A-66).
std::vector<std::string> unresolvedResources(const pdf::ops::Appearance& appearance)
{
    std::vector<std::string> missing;
    const CosObject& resources = appearance.dictionary.get("Resources");
    pdf::cos::Lexer lexer(appearance.data);
    std::string lastName;
    for (pdf::cos::Token token = lexer.next(); !token.isEnd(); token = lexer.next()) {
        if (token.type == pdf::cos::TokenType::Name) {
            lastName = token.text;
            continue;
        }
        if (token.type != pdf::cos::TokenType::Keyword) continue;
        if (token.text == "gs" && !resources.get("ExtGState").has(lastName)) missing.push_back("ExtGState " + lastName);
        if (token.text == "Tf" && !resources.get("Font").has(lastName)) missing.push_back("Font " + lastName);
        if (token.text == "Do") missing.push_back("XObject " + lastName);   // no drawing kind names one
    }
    return missing;
}

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
    CHECK(back.error().context.size() < 200);
}

// Every drawing kind as its tool makes it, on `page`.
std::vector<Annotation> everyKind(int page)
{
    std::vector<Annotation> out;
    Annotation line = shape(AnnotationSubtype::Line, page);
    line.line = {80, 700, 220, 720};
    out.push_back(fitted(line));

    Annotation arrow = shape(AnnotationSubtype::Line, page, core::LineStyle::Dashed, 2.0);
    arrow.line = {80, 660, 220, 640};
    arrow.lineEndings = {QStringLiteral("None"), QStringLiteral("OpenArrow")};
    out.push_back(fitted(arrow));

    Annotation rectangle = shape(AnnotationSubtype::Square, page, core::LineStyle::Dashed, 3.0);
    rectangle.rect = core::PdfRect{core::PdfPoint{260, 600}, core::PdfSize{120, 80}};
    rectangle.interiorColor = {0.9, 0.9, 1.0};
    out.push_back(fitted(rectangle));

    Annotation oval = shape(AnnotationSubtype::Circle, page, core::LineStyle::Dotted, 2.0);
    oval.rect = core::PdfRect{core::PdfPoint{400, 600}, core::PdfSize{100, 80}};
    oval.interiorColor = {1.0, 1.0, 0.8};
    oval.opacity = 0.5;
    out.push_back(fitted(oval));

    Annotation polygon = shape(AnnotationSubtype::Polygon, page);
    polygon.vertices = {80, 500, 180, 520, 200, 450, 120, 420, 70, 460};
    polygon.interiorColor = {0.8, 1.0, 0.8};
    polygon.blendMode = QStringLiteral("Multiply");
    out.push_back(fitted(polygon));

    Annotation polyline = shape(AnnotationSubtype::PolyLine, page, core::LineStyle::Solid, 1.5);
    polyline.vertices = {260, 520, 300, 470, 340, 520, 380, 470};
    polyline.lineEndings = {QStringLiteral("Circle"), QStringLiteral("ClosedArrow")};
    polyline.interiorColor = {0.0, 0.0, 1.0};
    out.push_back(fitted(polyline));

    for (const double intensity : {1.0, 2.0}) {
        Annotation cloud = shape(AnnotationSubtype::Polygon, page);
        const double x = intensity == 1.0 ? 420.0 : 80.0;
        const double y = intensity == 1.0 ? 420.0 : 260.0;
        cloud.vertices = {x, y, x + 120, y, x + 120, y + 80, x, y + 80};
        cloud.borderStyle = QStringLiteral("S");
        cloud.dashPattern.clear();
        cloud.cloudIntensity = intensity;
        cloud.intent = QStringLiteral("PolygonCloud");
        out.push_back(fitted(cloud));
    }

    Annotation ink = shape(AnnotationSubtype::Ink, page, core::LineStyle::Solid, 2.0);
    ink.borderStyle.clear();   // the pencil has no line style (`ToolDefaults.def`)
    ink.dashPattern.clear();
    ink.inkList = {{300, 300, 310, 320, 330, 310, 345, 340, 360, 300}, {300, 260, 380, 270}};
    out.push_back(fitted(ink));
    return out;
}

} // namespace

// ==========================================================================================
// A-58, A-27, A-32: every kind saved with the keys its tool writes, and read back as made
// ==========================================================================================

TEST_CASE("every drawing kind is saved with the keys its tool writes and read back as made",
          "[annotdrawing][A-58][A-27][A-32]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const std::vector<Annotation> made = everyKind(0);
    REQUIRE(made.size() == 9);
    REQUIRE(add(*session, made));
    CHECK(session->journal->historySize() == 1);   // one command for the set (D6-1)
    ReadBack file(save(*session, QStringLiteral("T8-every-drawing-kind.pdf")));

    for (const Annotation& a : made) {
        const Annotation* read = file.withNm(a.nm);
        REQUIRE(read != nullptr);
        sameModelled(a, *read);
        const CosObject d = file.dictionaryOf(a.nm);
        INFO(QString(core::annotationSubtypeName(a.subtype)).toStdString());
        CHECK(d.get("Subtype").text() == QString(core::annotationSubtypeName(a.subtype)).toStdString());
        // A-27: the identity keys.
        CHECK(d.has("NM"));
        CHECK(d.has("M"));
        CHECK(d.has("CreationDate"));
        CHECK(pdf::ops::EmbeddedFileReader::decodeTextString(d.get("T").text()) == kAuthor);
        CHECK(d.get("P").isReference());
        CHECK(d.get("AP").get("N").isReference());
        // The border: /W always, /S as the tool's style, /D only for a dash.
        const CosObject bs = file.resolved(d.get("BS"));
        CHECK(bs.get("W").numberValue() == *a.borderWidth);
        if (!a.borderStyle.isEmpty()) CHECK(bs.get("S").text() == a.borderStyle.toStdString());
        CHECK(bs.has("D") == !a.dashPattern.empty());
    }
    // The keys of each kind.
    const CosObject arrow = file.dictionaryOf(made[1].nm);
    REQUIRE(arrow.get("L").isArray());
    REQUIRE(arrow.get("LE").isArray());
    CHECK(arrow.get("LE").items().size() == 2);
    CHECK(arrow.get("LE").items()[1].text() == "OpenArrow");
    CHECK_FALSE(file.dictionaryOf(made[0].nm).has("LE"));   // a plain line writes none
    CHECK(file.dictionaryOf(made[2].nm).get("IC").isArray());
    CHECK(file.dictionaryOf(made[3].nm).get("CA").numberValue() == 0.5);
    CHECK(file.dictionaryOf(made[4].nm).get("BM").text() == "Multiply");   // not the polygon's own: written
    CHECK(file.dictionaryOf(made[4].nm).get("Vertices").items().size() == 10);
    CHECK(file.dictionaryOf(made[5].nm).get("LE").items()[0].text() == "Circle");
    for (const std::size_t at : {std::size_t{6}, std::size_t{7}}) {
        const CosObject cloud = file.dictionaryOf(made[at].nm);
        const CosObject be = file.resolved(cloud.get("BE"));
        CHECK(be.get("S").text() == "C");
        CHECK(be.get("I").numberValue() == *made[at].cloudIntensity);
        CHECK(cloud.get("IT").text() == "PolygonCloud");
        CHECK_FALSE(cloud.has("RD"));   // ISO 32000-1 Table 178 gives a polygon no /RD
    }
    const CosObject ink = file.dictionaryOf(made[8].nm);
    CHECK(ink.get("InkList").items().size() == 2);
    CHECK_FALSE(file.resolved(ink.get("BS")).has("S"));
    CHECK_FALSE(ink.has("BM"));   // the ink's own blend: written as nothing
}

// ==========================================================================================
// A-66: every appearance the drawing kinds can generate is well-formed
// ==========================================================================================

TEST_CASE("every appearance the drawing kinds generate is well formed and names only resources it holds",
          "[annotdrawing][A-66][D6-6]")
{
    std::vector<Annotation> cases;
    const std::vector<QString> endings{QStringLiteral("None"),   QStringLiteral("Square"),     QStringLiteral("Circle"),
                                       QStringLiteral("Diamond"), QStringLiteral("OpenArrow"), QStringLiteral("ClosedArrow"),
                                       QStringLiteral("Butt"),   QStringLiteral("ROpenArrow"), QStringLiteral("RClosedArrow"),
                                       QStringLiteral("Slash")};
    for (const core::LineStyle style : {core::LineStyle::Solid, core::LineStyle::Dashed, core::LineStyle::Dotted}) {
        for (const QString& start : endings) {
            for (const bool fill : {false, true}) {
                Annotation line = shape(AnnotationSubtype::Line, 0, style, 2.0);
                line.line = {10, 10, 110, 60};
                line.lineEndings = {start, endings[(&start - &endings[0] + 3) % endings.size()]};
                if (fill) line.interiorColor = {0.2, 0.4, 0.6};
                cases.push_back(fitted(line));
                Annotation polyline = line;
                polyline.subtype = AnnotationSubtype::PolyLine;
                polyline.line.clear();
                polyline.vertices = {10, 10, 60, 80, 110, 10};
                cases.push_back(fitted(polyline));
            }
        }
        for (const AnnotationSubtype kind : {AnnotationSubtype::Square, AnnotationSubtype::Circle}) {
            for (const bool fill : {false, true}) {
                Annotation box = shape(kind, 0, style, 4.0);
                box.rect = core::PdfRect{core::PdfPoint{10, 10}, core::PdfSize{90, 50}};
                if (fill) box.interiorColor = {1, 1, 0};
                cases.push_back(fitted(box));
            }
        }
    }
    for (const char* blend : {"", "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference", "Exclusion"}) {
        Annotation polygon = shape(AnnotationSubtype::Polygon, 0);
        polygon.vertices = {10, 10, 90, 20, 50, 80};
        polygon.blendMode = QString::fromLatin1(blend);
        polygon.opacity = 0.4;
        cases.push_back(fitted(polygon));
        for (const double intensity : {0.0, 1.0, 2.0}) {
            Annotation cloud = polygon;
            cloud.cloudIntensity = intensity;
            cases.push_back(fitted(cloud));
        }
        Annotation square = shape(AnnotationSubtype::Square, 0);
        square.rect = core::PdfRect{core::PdfPoint{10, 10}, core::PdfSize{90, 50}};
        square.rectDifferences = {6, 6, 6, 6};
        square.cloudIntensity = 1.0;
        square.blendMode = QString::fromLatin1(blend);
        cases.push_back(fitted(square));
    }
    Annotation ink = shape(AnnotationSubtype::Ink, 0, core::LineStyle::Dashed, 3.0);
    ink.inkList = {{10, 10}, {20, 20, 30, 25}, {40, 40, 50, 60, 70, 45, 75, 80}};   // a dot, a line, a curve
    cases.push_back(fitted(ink));

    int checked = 0;
    for (const Annotation& a : cases) {
        INFO(QJsonDocument(a.toJson()).toJson(QJsonDocument::Compact).toStdString());
        pdf::ops::CollectedAppearanceObjects objects(100);
        auto built = pdf::ops::AppearanceStreamBuilder::normal(a, objects);
        REQUIRE(built);
        CHECK(pdf::cos::ContentStreamBuilder::check(built.value().data));
        CHECK(unresolvedResources(built.value()).empty());
        CHECK(objects.entries().empty());   // a drawing kind writes nothing beside its form
        ++checked;
    }
    CHECK(checked > 150);
}

// ==========================================================================================
// A-63: opacity and blend reach the appearance's graphics state
// ==========================================================================================

TEST_CASE("opacity and blend mode are written to the appearance's graphics state", "[annotdrawing][A-63]")
{
    // A highlight at 50 % under its own multiply: the graphics state says so, and the dictionary writes no /BM.
    Annotation highlight = core::newAnnotation(AnnotationSubtype::Highlight, 0, kAuthor, kNow);
    highlight.quadPoints = {72, 714, 272, 714, 72, 700, 272, 700};
    highlight.rect = *core::markupRect(highlight.quadPoints);
    highlight.color = {1, 1, 0};
    highlight.opacity = 0.5;
    auto built = pdf::ops::AppearanceStreamBuilder::normal(highlight);
    REQUIRE(built);
    const CosObject gs = built.value().dictionary.get("Resources").get("ExtGState").get("GS0");
    CHECK(gs.get("CA").numberValue() == 0.5);
    CHECK(gs.get("ca").numberValue() == 0.5);
    CHECK(gs.get("BM").text() == "Multiply");

    // Told NOT to multiply, it draws normal -- and says /BM /Normal in its dictionary, which other readers read.
    Annotation normal = highlight;
    normal.nm = core::newAnnotationName();
    normal.id = AnnotationId::ofCreated(normal.nm);
    normal.blendMode = QStringLiteral("Normal");
    auto normalBuilt = pdf::ops::AppearanceStreamBuilder::normal(normal);
    REQUIRE(normalBuilt);
    CHECK_FALSE(normalBuilt.value().dictionary.get("Resources").get("ExtGState").get("GS0").has("BM"));

    // A rectangle under screen: its graphics state blends, and an unknown blend is refused by key.
    Annotation square = shape(AnnotationSubtype::Square, 0);
    square.rect = core::PdfRect{core::PdfPoint{100, 100}, core::PdfSize{50, 50}};
    square.blendMode = QStringLiteral("Screen");
    auto screen = pdf::ops::AppearanceStreamBuilder::normal(square);
    REQUIRE(screen);
    CHECK(screen.value().dictionary.get("Resources").get("ExtGState").get("GS0").get("BM").text() == "Screen");
    square.blendMode = QStringLiteral("Glow");
    auto glow = pdf::ops::AppearanceStreamBuilder::normal(square);
    REQUIRE_FALSE(glow);
    CHECK(keyOf(glow.error()) == "error.pdf.appearanceUnsupported");

    // Saved and read back: /BM where it is not the kind's own.
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    square.blendMode = QStringLiteral("Screen");
    REQUIRE(add(*session, {highlight, normal, square}));
    ReadBack file(save(*session, QStringLiteral("T8-blend.pdf")));
    CHECK_FALSE(file.dictionaryOf(highlight.nm).has("BM"));
    CHECK(file.dictionaryOf(normal.nm).get("BM").text() == "Normal");
    CHECK(file.dictionaryOf(square.nm).get("BM").text() == "Screen");
    CHECK(file.withNm(square.nm)->blendMode == QStringLiteral("Screen"));
}

// ==========================================================================================
// A-26: a file's shapes are redrawn by our builder now, and what we cannot draw is refused
// ==========================================================================================

TEST_CASE("a file's shapes recoloured are redrawn by our builder and keep every key they do not model",
          "[annotdrawing][A-26]")
{
    auto session = tests::openCopy("ordinary/annots-every-type.pdf");
    REQUIRE(session);
    ReadBack before(session->openedBytes);
    std::vector<AnnotationId> ids;
    for (const Annotation& a : session->journal->document().annotations().all()) {
        const bool drawing = a.subtype == AnnotationSubtype::Square || a.subtype == AnnotationSubtype::Circle
                             || a.subtype == AnnotationSubtype::Line || a.subtype == AnnotationSubtype::Polygon
                             || a.subtype == AnnotationSubtype::PolyLine || a.subtype == AnnotationSubtype::Ink;
        if (drawing && a.id.kind == AnnotationId::Kind::Object && a.intent.isEmpty()) ids.push_back(a.id);
        else if (drawing && a.intent == QLatin1String("PolygonCloud")) ids.push_back(a.id);
    }
    REQUIRE(ids.size() >= 6);
    core::AnnotationAppearance blue;
    blue.color = std::vector<double>{0, 0, 1};
    REQUIRE(session->journal->run(std::make_unique<core::SetAnnotationAppearanceCommand>(ids, blue, kNow)));
    ReadBack after(save(*session, QStringLiteral("T8-recoloured-shapes.pdf")));
    for (const AnnotationId& id : ids) {
        INFO("object " << id.object);
        CosObject was = before.object(id.object);
        CosObject now = after.object(id.object);
        // REDRAWN: a new /AP /N, our own, well-formed.
        REQUIRE(now.get("AP").get("N").isReference());
        CHECK(now.get("AP").get("N").reference().number != was.get("AP").get("N").reference().number);
        const CosObject form = after.object(now.get("AP").get("N").reference().number);
        CHECK(pdf::cos::ContentStreamBuilder::check(form.rawStreamData(after.table->source())));
        CHECK(std::string(form.rawStreamData(after.table->source())).find("0 0 1 RG") != std::string::npos);
        // Every key but /C, /M and /AP equal in value (D6-5).
        for (const CosObject::Entry& entry : was.entries()) {
            if (entry.key == "C" || entry.key == "M" || entry.key == "AP") continue;
            INFO(entry.key);
            CosObject a = entry.value;
            CosObject b = now.get(entry.key);
            a.clearSpansRecursively();
            b.clearSpansRecursively();
            CHECK(a == b);
        }
    }
}

TEST_CASE("a line with leader lines is redrawn with them and a measurement this build cannot read is refused by key",
          "[annotdrawing][A-26][D6-35][D6-14]")
{
    // A Line with leader lines and a caption -- ISO 32000-1 Table 175's /LL, /LLE, /Cap -- with its producer's
    // appearance; and a PolyLine carrying a /Measure with no number formats. UNTIL T12 the model held neither, and
    // both were refused a redraw by key (D6-35(j)). SINCE T12 (D6-14) the model holds a line's leaders and caption and
    // the builder draws them, so the line IS redrawn -- its leaders with it; the measure states no reading this
    // build can display (`core::measured`: no `/X`, no `/D`), so it is still refused, and still carried by a move.
    const std::string form = "q 0 0 1 RG 10 10 m 110 10 l S Q";
    const std::string bytes = tests::annotatedDocument(1, [&](int, tests::TinyPdf& pdf, int pageObject) -> std::string {
        const int ap = pdf.add("<< /Type /XObject /Subtype /Form /BBox [ 0 0 200 100 ] /Length " + std::to_string(form.size())
                               + " >>\nstream\n" + form + "\nendstream");
        const int leader = pdf.add("<< /Type /Annot /Subtype /Line /Rect [ 0 0 200 100 ] /L [ 10 10 110 10 ] /LL 15 /LLE 5 "
                                   "/Cap true /C [ 0 0 1 ] /AP << /N " + std::to_string(ap) + " 0 R >> /P "
                                   + std::to_string(pageObject) + " 0 R >>");
        const int measured = pdf.add("<< /Type /Annot /Subtype /PolyLine /Rect [ 200 200 300 300 ] /Vertices [ 210 210 290 290 ] "
                                     "/Measure << /Type /Measure /Subtype /RL /R (1 in = 1 ft) >> /C [ 1 0 0 ] /AP << /N "
                                     + std::to_string(ap) + " 0 R >> /P " + std::to_string(pageObject) + " 0 R >>");
        return "[ " + std::to_string(leader) + " 0 R " + std::to_string(measured) + " 0 R ]";
    });
    auto session = tests::openBuilt(QStringLiteral("leader.pdf"), bytes);
    REQUIRE(session);
    std::vector<AnnotationId> ids;
    for (const Annotation& a : session->journal->document().annotations().all()) ids.push_back(a.id);
    REQUIRE(ids.size() == 2);
    const AnnotationId lineId = ids[0], measuredId = ids[1];
    {
        // The model reads what the line draws.
        const auto at = session->journal->document().annotations().find(lineId);
        REQUIRE(at);
        const Annotation& line = session->journal->document().annotations().at(*at);
        CHECK(line.leaderLength == 15.0);
        CHECK(line.leaderExtension == 5.0);
        CHECK(line.caption);
    }
    core::AnnotationAppearance green;   // neither's colour: a change of what each draws
    green.color = std::vector<double>{0, 1, 0};
    // THE LINE: redrawn, in green, its two leaders stroked beside its line -- three strokes where T8 refused one.
    REQUIRE(session->journal->run(
        std::make_unique<core::SetAnnotationAppearanceCommand>(std::vector<AnnotationId>{lineId}, green, kNow)));
    ReadBack after(save(*session, QStringLiteral("T12-leader-redrawn.pdf")));
    const CosObject now = after.object(lineId.object);
    CHECK(now.get("LL").numberValue() == 15.0);
    CHECK(now.get("LLE").numberValue() == 5.0);
    REQUIRE(now.get("AP").get("N").isReference());
    const std::string stream(after.object(now.get("AP").get("N").reference().number).rawStreamData(after.table->source()));
    CHECK(stream.find("0 1 0 RG") != std::string::npos);
    std::size_t strokes = 0;
    std::istringstream tokens(stream);
    for (std::string token; tokens >> token;) strokes += token == "S" ? 1 : 0;
    CHECK(strokes >= 3);
    // THE MEASUREMENT this build cannot read: refused by key, and carried by a move.
    const core::Status status = session->journal->run(
        std::make_unique<core::SetAnnotationAppearanceCommand>(std::vector<AnnotationId>{measuredId}, green, kNow));
    REQUIRE_FALSE(status);
    CHECK(keyOf(status.error()) == "error.pdf.appearanceUnsupported");
    CHECK(session->journal->run(
        std::make_unique<core::MoveAnnotationCommand>(std::vector<AnnotationId>{measuredId}, 5.0, 5.0, kNow)));
}

// ==========================================================================================
// A-60: the eraser's command, and its undo
// ==========================================================================================

TEST_CASE("an erase is one edit and its undo puts back exactly the samples there were", "[annotdrawing][A-60][D6-15]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    Annotation ink = shape(AnnotationSubtype::Ink, 0, core::LineStyle::Solid, 2.0);
    ink.borderStyle.clear();
    ink.dashPattern.clear();
    std::vector<double> along;
    for (int x = 100; x <= 300; x += 4) {
        along.push_back(x);
        along.push_back(400 + (x % 8));
    }
    ink.inkList = {{100, 300, 104, 302, 108, 300}, along};
    ink = fitted(ink);
    REQUIRE(add(*session, {ink}));
    const QByteArray made = session->journal->document().modelHash();
    const std::size_t steps = session->journal->historySize();

    // Over the first stroke wholly, and down through the middle of the second: as the overlay asks.
    const std::vector<double> path{104, 290, 104, 310, 200, 380, 200, 420};
    const core::ErasedInk erased = core::erasedStrokes(ink.inkList, path, 3.0);
    REQUIRE(erased.changed);
    REQUIRE(erased.strokes.size() == 2);   // the short stroke gone, the long one in two
    Annotation after = ink;
    after.inkList = erased.strokes;
    after.modified = core::PdfDate::fromUtcSeconds(kNow + 1);
    after = fitted(after);
    REQUIRE(session->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{after})));
    CHECK(session->journal->historySize() == steps + 1);   // ONE step
    const auto index = session->journal->document().annotations().find(ink.id);
    REQUIRE(index);
    CHECK(session->journal->document().annotations().at(*index).inkList == erased.strokes);

    REQUIRE(session->journal->undo());
    CHECK(session->journal->document().modelHash() == made);
    CHECK(session->journal->document().annotations().at(*session->journal->document().annotations().find(ink.id)).inkList
          == ink.inkList);   // the very samples
    REQUIRE(session->journal->redo());
    ReadBack file(save(*session, QStringLiteral("T8-erased.pdf")));
    REQUIRE(file.withNm(ink.nm) != nullptr);
    CHECK(file.withNm(ink.nm)->inkList.size() == 2);
}

// ==========================================================================================
// The bar's new properties: applied where the kind has them, refitting what they reach
// ==========================================================================================

TEST_CASE("a line given heads and a wider border is refitted so nothing it draws is clipped", "[annotdrawing][D6-35]")
{
    Annotation line = fitted([] {
        Annotation a = shape(AnnotationSubtype::Line, 0);
        a.line = {100, 100, 200, 100};
        return a;
    }());
    core::AnnotationAppearance heads;
    heads.lineEndings = std::vector<QString>{QStringLiteral("RClosedArrow"), QStringLiteral("Diamond")};
    heads.borderWidth = 6.0;
    heads.lineStyle = core::LineStyle::Dashed;
    const Annotation after = heads.appliedTo(line);
    CHECK(after.lineEndings == *heads.lineEndings);
    CHECK(after.borderStyle == QStringLiteral("D"));
    CHECK(after.dashPattern == core::dashFor(core::LineStyle::Dashed, 6.0));
    // The reversed arrow's barbs reach FORWARD past x = 100 by the head's size, 27 points at width 6.
    CHECK(after.rect.left() < 100 - 27 * std::cos(3.14159265 / 6));
    CHECK(after.rect.size.height > line.rect.size.height);
    // Endings on a kind that has none are not applied; a cloud's intensity only on a cloud.
    Annotation square = shape(AnnotationSubtype::Square, 0);
    square.rect = core::PdfRect{core::PdfPoint{0, 0}, core::PdfSize{10, 10}};
    CHECK(heads.appliedTo(square).lineEndings.empty());
    core::AnnotationAppearance bigger;
    bigger.cloudIntensity = 2.0;
    CHECK_FALSE(bigger.appliedTo(square).cloudIntensity);
    Annotation cloud = shape(AnnotationSubtype::Polygon, 0);
    cloud.vertices = {0, 0, 100, 0, 100, 60, 0, 60};
    cloud.cloudIntensity = 1.0;
    cloud = fitted(cloud);
    const Annotation big = bigger.appliedTo(cloud);
    CHECK(*big.cloudIntensity == 2.0);
    CHECK(big.rect.size.width > cloud.rect.size.width);   // the larger bumps fit
}

// ==========================================================================================
// A-29: the new properties travel in a journal line, and out-of-range ones are refused
// ==========================================================================================

TEST_CASE("an appearance change carries line style blend endings and cloud and refuses them out of range",
          "[annotdrawing][A-29][serialize:SetAnnotationAppearanceCommand]")
{
    core::AnnotationAppearance all;
    all.lineStyle = core::LineStyle::Dotted;
    all.blendMode = core::ToolBlend::Difference;
    all.lineEndings = std::vector<QString>{QStringLiteral("Slash"), QStringLiteral("ROpenArrow")};
    all.cloudIntensity = 2.0;
    roundTrips(core::SetAnnotationAppearanceCommand({AnnotationId::ofObject(5)}, all, kNow));
    core::AnnotationAppearance callout;
    callout.lineEndings = std::vector<QString>{QStringLiteral("Butt")};
    roundTrips(core::SetAnnotationAppearanceCommand({AnnotationId::ofObject(5)}, callout, kNow));

    const QJsonObject good = core::SetAnnotationAppearanceCommand({AnnotationId::ofObject(5)}, all, kNow).serialize();
    const auto withAppearance = [&good](const QJsonObject& appearance) {
        QJsonObject json = good;
        json.insert(QStringLiteral("appearance"), appearance);
        return json;
    };
    refused(withAppearance({{QStringLiteral("lineStyle"), QStringLiteral("wavy")}}), "a line style there is not");
    refused(withAppearance({{QStringLiteral("lineStyle"), 1}}), "a line style that is a number");
    refused(withAppearance({{QStringLiteral("blendMode"), QStringLiteral("Hue")}}), "a blend no tool offers");
    refused(withAppearance({{QStringLiteral("lineEndings"), QJsonArray{}}}), "no ending");
    refused(withAppearance({{QStringLiteral("lineEndings"), QJsonArray{QStringLiteral("None"), QStringLiteral("None"),
                                                                       QStringLiteral("None")}}}),
            "three endings");
    refused(withAppearance({{QStringLiteral("lineEndings"), QJsonArray{QStringLiteral("Wavy")}}}), "an ending ISO does not name");
    refused(withAppearance({{QStringLiteral("cloudIntensity"), 0.0}}), "a cloud of no intensity");
    refused(withAppearance({{QStringLiteral("cloudIntensity"), 3.0}}), "a cloud past ISO's 2");
}

TEST_CASE("an added shape carries its border style dash cloud and blend and refuses them out of range",
          "[annotdrawing][A-29][serialize:AddAnnotationCommand]")
{
    const std::vector<Annotation> made = everyKind(2);
    roundTrips(AddAnnotationCommand(made));
    const QJsonObject good = AddAnnotationCommand({made[1]}).serialize();
    const auto withField = [&good](const char* key, const QJsonValue& value) {
        QJsonObject json = good;
        QJsonObject inner = good.value(QStringLiteral("annotations")).toArray().first().toObject();
        inner.insert(QLatin1String(key), value);
        json.insert(QStringLiteral("annotations"), QJsonArray{inner});
        return json;
    };
    refused(withField("borderStyle", QStringLiteral("Z")), "a border style ISO does not name");
    refused(withField("dashPattern", QJsonArray{3, -1}), "a negative dash");
    refused(withField("dashPattern", QJsonArray{0, 0}), "a dash of nothing");
    QJsonArray long_;
    for (int i = 0; i < 17; ++i) long_.append(1);
    refused(withField("dashPattern", long_), "a dash of seventeen");
    refused(withField("cloudIntensity", 2.5), "a cloud past ISO's 2");
    refused(withField("cloudIntensity", -1.0), "a cloud below none");
    refused(withField("blendMode", QStringLiteral("Glow")), "a blend ISO does not name");
}
