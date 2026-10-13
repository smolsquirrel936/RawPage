// tst_measure -- what a measurement MEASURES, and what is written for it. Stage 6 T12, decision D6-14
// (`Stage6-Annotations-Plan.md` §3); guide cases A-87, A-88, A-90's arithmetic and A-92's file half
// (`Stage6-Annotations-Testing-Guide.md` §5.15).
//
// THE ORACLE IS ARITHMETIC DONE BY HAND, never the code under test: every expected reading below is worked out in
// its comment from the figures -- 72 points an inch, 864 a foot, 25.4 millimetres an inch -- and the corpus's
// three measurements are graded against `corpus/labels/annots-measure-scale.json`, which `corpus/make-annots.py`
// wrote when it drew them (rule 7).
//
// WHAT IT CANNOT CLAIM (convention 19): where a point SNAPS -- the generator's points through the engine's paths --
// and what the measuring tool does with a pointer are `tests/ui/tst_measuretool` and
// `tests/composition/tst_measureproduct`; that another reader shows the same reading is M-09's.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "core/cmd/CommandRegistry.h"
#include "core/Measure.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/AnnotationGeometry.h"
#include "core/doc/DrawnShapes.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AppearanceStreamBuilder.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace rawpage;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;
using core::MeasureKind;
using core::MeasureScale;
using core::MeasureUnit;
using pdf::cos::CosObject;

namespace {

constexpr std::int64_t kNow = 1792022400;   // 2026-10-15T00:00:00Z, a fixed instant
const QString kAuthor = QStringLiteral("Kofi Mensah");

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

MeasureScale scaleOf(double ratio, MeasureUnit unit, int precision = 2)
{
    MeasureScale scale;
    scale.ratio = ratio;
    scale.unit = unit;
    scale.precision = precision;
    return scale;
}

std::string reading(MeasureKind kind, const std::vector<double>& points, const MeasureScale& scale)
{
    const core::Result<core::MeasureReading> r = core::measureAt(kind, points, scale);
    INFO((r ? std::string() : keyOf(r.error())));
    REQUIRE(r);
    return r.value().text.toStdString();
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

// The file as the first-party reader reads it back.
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
    CosObject resolved(const CosObject& value) { return value.isReference() ? object(value.reference().number) : value; }
    const Annotation* withNm(const QString& nm) const
    {
        for (const Annotation& a : store.all())
            if (a.nm == nm) return &a;
        return nullptr;
    }
};

// A measurement as the tool makes one: `core::newMeasurement` over a styled base, fitted.
Annotation measurement(MeasureKind kind, const std::vector<double>& points, const MeasureScale& scale,
                       double leader = 0.0, bool writeToComment = true)
{
    Annotation base = core::newAnnotation(core::measureSubtype(kind), 0, kAuthor, kNow);
    base.color = {0.0, 0.0, 1.0};
    core::Result<Annotation> made = core::newMeasurement(std::move(base), kind, points, scale, leader, writeToComment);
    INFO((made ? std::string() : keyOf(made.error())));
    REQUIRE(made);
    const std::optional<Annotation> fitted = core::fittedShape(std::move(made).value());
    REQUIRE(fitted);
    return *fitted;
}

QJsonObject label()
{
    QFile file(tests::corpusFile("labels/annots-measure-scale.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    REQUIRE(document.isObject());
    return document.object();
}

} // namespace

// =============================================================================================
// A-87 -- the arithmetic, against values worked by hand
// =============================================================================================

TEST_CASE("units convert exactly and their symbols are the settings tokens", "[measure][A-87]")
{
    CHECK(core::pointsPerMeasureUnit(MeasureUnit::Points) == 1.0);
    CHECK(core::pointsPerMeasureUnit(MeasureUnit::Inches) == 72.0);
    CHECK(core::pointsPerMeasureUnit(MeasureUnit::Feet) == 864.0);       // 12 x 72
    CHECK(core::pointsPerMeasureUnit(MeasureUnit::Yards) == 2592.0);     // 3 x 864
    CHECK(core::pointsPerMeasureUnit(MeasureUnit::Miles) == 4561920.0);  // 5280 x 864
    CHECK(std::abs(core::pointsPerMeasureUnit(MeasureUnit::Millimetres) * 25.4 - 72.0) < 1e-12);
    CHECK(std::abs(core::pointsPerMeasureUnit(MeasureUnit::Metres) - 1000.0 * 72.0 / 25.4) < 1e-9);
    for (int i = 0; i < core::kMeasureUnitCount; ++i) {
        const auto unit = static_cast<MeasureUnit>(i);
        const std::string_view token = core::measureUnitToken(unit);
        INFO(std::string(token));
        CHECK(core::measureUnitFromToken(token) == unit);
    }
    CHECK_FALSE(core::measureUnitFromToken("furlong").has_value());
}

TEST_CASE("the scale in words is one paper unit and what it stands for", "[measure][A-87]")
{
    CHECK(core::scaleString(scaleOf(48.0, MeasureUnit::Feet)).toStdString() == "1 in = 4 ft");      // 48 in = 4 ft
    CHECK(core::scaleString(scaleOf(120.0, MeasureUnit::Feet)).toStdString() == "1 in = 10 ft");    // 120 in = 10 ft
    CHECK(core::scaleString(scaleOf(1.0, MeasureUnit::Inches)).toStdString() == "1 in = 1 in");
    CHECK(core::scaleString(scaleOf(100.0, MeasureUnit::Metres)).toStdString() == "1 cm = 1 m");    // 100 cm = 1 m
    CHECK(core::scaleString(scaleOf(2.5, MeasureUnit::Centimetres)).toStdString() == "1 mm = 0.25 cm");
    CHECK(core::scaleString(scaleOf(1.0, MeasureUnit::Points)).toStdString() == "1 pt = 1 pt");
}

TEST_CASE("distances perimeters and areas read the hand values at several scales units and precisions",
          "[measure][A-87]")
{
    const MeasureScale fourFeet = scaleOf(48.0, MeasureUnit::Feet);   // 1 in = 4 ft: 18 points a foot
    // The corpus's rectangle side, 216 points: 216 / 18 = 12 ft.
    CHECK(reading(MeasureKind::Distance, {100, 500, 316, 500}, fourFeet) == "12.00 ft");
    // Its diagonal: sqrt(216^2 + 144^2) = 259.5997 points; / 18 = 14.4222 ft.
    CHECK(reading(MeasureKind::Distance, {100, 500, 316, 644}, fourFeet) == "14.42 ft");
    CHECK(reading(MeasureKind::Distance, {100, 500, 316, 644}, scaleOf(48.0, MeasureUnit::Feet, 3)) == "14.422 ft");
    CHECK(reading(MeasureKind::Distance, {100, 500, 316, 644}, scaleOf(48.0, MeasureUnit::Feet, 0)) == "14 ft");
    // The same diagonal in inches at that scale: 14.4222 x 12 = 173.0666 in.
    CHECK(reading(MeasureKind::Distance, {100, 500, 316, 644}, scaleOf(48.0, MeasureUnit::Inches)) == "173.07 in");
    // Three sides of the rectangle, 144 + 216 + 144 = 504 points: 28 ft.
    CHECK(reading(MeasureKind::Perimeter, {100, 500, 100, 644, 316, 644, 316, 500}, fourFeet) == "28.00 ft");
    // Its area: 216 x 144 = 31,104 square points; / 18^2 = 96 square feet.
    CHECK(reading(MeasureKind::Area, {100, 500, 316, 500, 316, 644, 100, 644}, fourFeet) == "96.00 sq ft");
    // A concave L: the 216 x 144 rectangle less a 108 x 72 corner -- 31,104 - 7,776 = 23,328 sq pt = 72 sq ft.
    CHECK(reading(MeasureKind::Area, {100, 500, 316, 500, 316, 572, 208, 572, 208, 644, 100, 644}, fourFeet)
          == "72.00 sq ft");
    // Metric: an inch on a 1:100 plan is 2,540 mm, 2.54 m.
    CHECK(reading(MeasureKind::Distance, {0, 0, 72, 0}, scaleOf(100.0, MeasureUnit::Metres)) == "2.54 m");
    // A map at 1:100,000: ten inches are 254 mm x 100,000 = 25,400 m -- a thousands separator, ISO's comma.
    CHECK(reading(MeasureKind::Distance, {0, 0, 720, 0}, scaleOf(100000.0, MeasureUnit::Metres)) == "25,400.00 m");
    // Actual size in points: the length itself.
    CHECK(reading(MeasureKind::Distance, {0, 0, 3, 4}, scaleOf(1.0, MeasureUnit::Points)) == "5.00 pt");
}

TEST_CASE("a number is displayed as its number format says", "[measure][A-87]")
{
    CHECK(core::formatMeasureNumber(1234.5, 2).toStdString() == "1,234.50");
    CHECK(core::formatMeasureNumber(1234567.891, 1, QStringLiteral("."), QStringLiteral(",")).toStdString() == "1.234.567,9");
    CHECK(core::formatMeasureNumber(12.0, 0).toStdString() == "12");
    CHECK(core::formatMeasureNumber(0.004, 2).toStdString() == "0.00");
    CHECK(core::decimalsOfDenominator(100) == 2);
    CHECK(core::decimalsOfDenominator(1) == 0);
    CHECK_FALSE(core::decimalsOfDenominator(50).has_value());   // a fraction's denominator, not a decimal's
}

TEST_CASE("a polygon that crosses itself has no area and says so by name", "[measure][A-87]")
{
    // A FIGURE EIGHT: its shoelace sum is the difference of its two loops, the area of nothing on the page.
    const std::vector<double> bowtie{0, 0, 100, 100, 100, 0, 0, 100};
    CHECK(core::selfIntersects(bowtie));
    const core::Result<double> area = core::polygonArea(bowtie);
    REQUIRE_FALSE(area);
    CHECK(keyOf(area.error()) == "error.measure.selfIntersecting");
    const core::Result<core::MeasureReading> reading = core::measureAt(MeasureKind::Area, bowtie, scaleOf(1, MeasureUnit::Points));
    REQUIRE_FALSE(reading);
    CHECK(keyOf(reading.error()) == "error.measure.selfIntersecting");
    // A FOLD BACK along one edge crosses itself too.
    CHECK(core::selfIntersects({0, 0, 100, 0, 50, 0, 50, 50}));
    // THE CONTROLS: a square, a concave L and a square whose last corner repeats its first are not crossed.
    CHECK_FALSE(core::selfIntersects({0, 0, 100, 0, 100, 100, 0, 100}));
    CHECK_FALSE(core::selfIntersects({100, 500, 316, 500, 316, 572, 208, 572, 208, 644, 100, 644}));
    REQUIRE(core::polygonArea({0, 0, 100, 0, 100, 100, 0, 100, 0, 0}));
    CHECK(core::polygonArea({0, 0, 100, 0, 100, 100, 0, 100, 0, 0}).value() == 10000.0);
    // Too few corners, and a point at infinity, refused by name.
    CHECK(keyOf(core::polygonArea({0, 0, 10, 10}).error()) == "error.measure.tooFewPoints");
    CHECK(keyOf(core::polygonArea({0, 0, 10, 10, std::numeric_limits<double>::infinity(), 0}).error())
          == "error.measure.geometry");
    // And nothing is made of one: the tool's command never exists.
    Annotation base = core::newAnnotation(AnnotationSubtype::Polygon, 0, kAuthor, kNow);
    const core::Result<Annotation> made =
        core::newMeasurement(base, MeasureKind::Area, bowtie, scaleOf(1, MeasureUnit::Points), 0.0, true);
    REQUIRE_FALSE(made);
    CHECK(keyOf(made.error()) == "error.measure.selfIntersecting");
}

// =============================================================================================
// A-90's arithmetic -- calibration
// =============================================================================================

TEST_CASE("calibration makes a measured length read what it is known to be", "[measure][A-90]")
{
    // The corpus rectangle's long side, 216 points, is known to be 12 ft: 12 x 864 / 216 = 48.
    const core::Result<double> ratio = core::calibratedRatio(216.0, 12.0, MeasureUnit::Feet);
    REQUIRE(ratio);
    CHECK(std::abs(ratio.value() - 48.0) < 1e-12);
    const MeasureScale calibrated = scaleOf(ratio.value(), MeasureUnit::Feet);
    // The 288-point line then reads 16 ft (288 / 18), and the diagonal 14.42 ft.
    CHECK(reading(MeasureKind::Distance, {100, 400, 388, 400}, calibrated) == "16.00 ft");
    CHECK(reading(MeasureKind::Distance, {100, 644, 316, 500}, calibrated) == "14.42 ft");
    // Refused: nothing measured, a negative length, and a ratio past the key's range.
    CHECK(keyOf(core::calibratedRatio(0.0, 12.0, MeasureUnit::Feet).error()) == "error.measure.calibration");
    CHECK(keyOf(core::calibratedRatio(216.0, -1.0, MeasureUnit::Feet).error()) == "error.measure.calibration");
    CHECK(keyOf(core::calibratedRatio(0.001, 1000.0, MeasureUnit::Kilometres).error()) == "error.measure.calibration");
}

// =============================================================================================
// A-88 -- /Measure written and read
// =============================================================================================

TEST_CASE("a distance a perimeter and an area are written with their scale and read back as shown",
          "[measure][A-88][A-92][D6-14]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const MeasureScale tenFeet = scaleOf(120.0, MeasureUnit::Feet);   // 1 in : 10 ft -- 7.2 points a foot
    const Annotation distance = measurement(MeasureKind::Distance, {72, 72, 216, 72}, tenFeet);
    const Annotation dimension = measurement(MeasureKind::Distance, {72, 300, 288, 300}, tenFeet, 24.0);
    const Annotation perimeter = measurement(MeasureKind::Perimeter, {300, 72, 300, 144, 372, 144}, tenFeet);
    const Annotation area = measurement(MeasureKind::Area, {300, 400, 372, 400, 372, 472, 300, 472}, tenFeet, 0.0, false);
    // BY HAND: 144 / 7.2 = 20 ft; 216 / 7.2 = 30 ft; (72 + 72) / 7.2 = 20 ft; 72 x 72 / 7.2^2 = 100 sq ft.
    CHECK(distance.contents.toStdString() == "20.00 ft");
    CHECK(dimension.contents.toStdString() == "30.00 ft");
    CHECK(perimeter.contents.toStdString() == "20.00 ft");
    CHECK(area.contents.isEmpty());   // `measuring/writeToComment` off: the reading is on the page and not in /Contents
    REQUIRE(core::measured(area));
    CHECK(core::measured(area).value().text.toStdString() == "100.00 sq ft");

    REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(
        std::vector<Annotation>{distance, dimension, perimeter, area})));
    ReadBack file(save(*session, QStringLiteral("T12-measurements.pdf")));
    for (const Annotation* made : {&distance, &dimension, &perimeter, &area}) {
        INFO(made->intent.toStdString());
        const Annotation* read = file.withNm(made->nm);
        REQUIRE(read != nullptr);
        // THE DICTIONARY: /IT the dimension kind, /Measure /Subtype /RL with the scale in words and the factors.
        REQUIRE(read->id.kind == AnnotationId::Kind::Object);
        const CosObject d = file.object(read->id.object);
        CHECK(d.get("IT").text() == made->intent.toStdString());
        const CosObject m = file.resolved(d.get("Measure"));
        REQUIRE(m.isDictionary());
        CHECK(m.get("Type").text() == "Measure");
        CHECK(m.get("Subtype").text() == "RL");
        CHECK(m.get("R").text() == "1 in = 10 ft");
        const CosObject x = file.resolved(m.get("X")).items().at(0);
        CHECK(std::abs(x.get("C").numberValue() - 120.0 / 864.0) < 1e-12);   // feet per point at the scale
        CHECK(x.get("U").text() == "ft");
        CHECK(x.get("D").numberValue() == 100.0);
        CHECK(file.resolved(m.get("A")).items().at(0).get("U").text() == "sq ft");
        // THE MODEL READ BACK: the same scale, and the reading it gives is the one shown when it was made.
        REQUIRE(read->measure);
        CHECK(*read->measure == *made->measure);
        const core::Result<core::MeasureReading> back = core::measured(*read);
        REQUIRE(back);
        CHECK(back.value().text == core::measured(*made).value().text);
        CHECK(read->contents == made->contents);
        CHECK(read->hasAppearance);
    }
    // A-92: the DIMENSION's leader lines, written -- `/LL` its offset, `/LLE` past the line -- and its caption on top.
    const CosObject dim = file.object(file.withNm(dimension.nm)->id.object);
    CHECK(dim.get("LL").numberValue() == 24.0);
    CHECK(dim.get("LLE").numberValue() == core::kDimensionLeaderExtension);
    CHECK(dim.get("Cap").booleanValue());
    CHECK(dim.get("CP").text() == "Top");
    // A plain distance has no leaders, and says nothing of them.
    const CosObject plain = file.object(file.withNm(distance.nm)->id.object);
    CHECK(plain.find("LL") == nullptr);
    CHECK(plain.find("LLE") == nullptr);
    // THE AREA, whose /Contents is empty, still DRAWS its reading: the appearance sets "100.00 sq ft".
    const CosObject areaDictionary = file.object(file.withNm(area.nm)->id.object);
    CHECK(areaDictionary.find("Contents") == nullptr);
    const CosObject form = file.object(areaDictionary.get("AP").get("N").reference().number);
    const std::string stream(form.rawStreamData(file.table->source()));
    const std::string drawn = QByteArray("100.00 sq ft").toHex().toUpper().toStdString();
    CHECK(QByteArray::fromStdString(stream).toUpper().toStdString().find(drawn) != std::string::npos);
}

TEST_CASE("the corpus's measurements are read with their scale and read what their labels say",
          "[measure][A-88]")
{
    auto session = tests::openCopy("ordinary/measure-scale.pdf");
    REQUIRE(session);
    const QJsonArray wanted = label().value("pages").toArray().at(0).toObject().value("annots").toArray();
    const core::AnnotationStore& store = session->journal->document().annotations();
    REQUIRE(static_cast<qsizetype>(store.all().size()) == wanted.size());
    for (const QJsonValue value : wanted) {
        const QJsonObject want = value.toObject();
        INFO(want.value("intent").toString().toStdString());
        const auto at = store.find(AnnotationId::ofObject(want.value("object").toInteger()));
        REQUIRE(at);
        const Annotation& a = store.at(*at);
        REQUIRE(a.measure);
        const QJsonObject m = want.value("measure").toObject();
        CHECK(a.measure->ratio == m.value("ratio").toString());
        REQUIRE(a.measure->x.size() == 1);
        CHECK(a.measure->x[0].unit == m.value("x").toObject().value("unit").toString());
        CHECK(std::abs(a.measure->x[0].factor - m.value("x").toObject().value("factor").toDouble()) < 1e-12);
        // WHAT IT MEASURES, from its geometry and its own /Measure: the label's /Contents, which the generator wrote
        // from the hand figures in its `expected` -- 12.00 ft, 28.00 ft, 96.00 sq ft.
        const core::Result<core::MeasureReading> r = core::measured(a);
        REQUIRE(r);
        CHECK(r.value().text == want.value("contents").toString());
        CHECK(a.leaderLength == want.value("leaderLength").toDouble(0.0));
    }
}

TEST_CASE("a measurement of the corpus recoloured is redrawn reading what it measures", "[measure][A-88][A-26]")
{
    auto session = tests::openCopy("ordinary/measure-scale.pdf");
    REQUIRE(session);
    std::vector<AnnotationId> ids;
    for (const Annotation& a : session->journal->document().annotations().all()) ids.push_back(a.id);
    REQUIRE(ids.size() == 3);
    core::AnnotationAppearance green;
    green.color = std::vector<double>{0, 0.5, 0};
    REQUIRE(session->journal->run(std::make_unique<core::SetAnnotationAppearanceCommand>(ids, green, kNow)));
    ReadBack file(save(*session, QStringLiteral("T12-corpus-recoloured.pdf")));
    for (const AnnotationId& id : ids) {
        const CosObject d = file.object(id.object);
        const std::string expected = d.get("Contents").text();
        INFO(expected);
        const CosObject form = file.object(d.get("AP").get("N").reference().number);
        const std::string stream = QByteArray::fromStdString(std::string(form.rawStreamData(file.table->source())))
                                       .toUpper().toStdString();
        CHECK(stream.find("0 0.5 0 RG") != std::string::npos);
        CHECK(stream.find(QByteArray::fromStdString(expected).toHex().toUpper().toStdString()) != std::string::npos);
        // The scale is the producer's, untouched (D6-5): an edit of the colour does not rewrite /Measure.
        CHECK(file.resolved(d.get("Measure")).get("R").text() == "1 in = 4 ft");
    }
}

TEST_CASE("a measurement moved or resized reads what it now spans", "[measure][A-87]")
{
    const MeasureScale fourFeet = scaleOf(48.0, MeasureUnit::Feet);
    const Annotation before = measurement(MeasureKind::Distance, {100, 500, 316, 500}, fourFeet);
    REQUIRE(before.contents.toStdString() == "12.00 ft");
    // Half as wide: its reading halves, and /Contents -- which held the old reading -- follows it.
    const core::PdfRect half{before.rect.origin, core::PdfSize{before.rect.size.width / 2.0, before.rect.size.height}};
    const std::optional<Annotation> resized = core::mappedInto(before, half);
    REQUIRE(resized);
    const Annotation after = core::withFreshReading(before, *resized);
    REQUIRE(core::measured(after));
    CHECK(core::measured(after).value().text.toStdString() == "6.00 ft");
    CHECK(after.contents.toStdString() == "6.00 ft");
    // Contents a person wrote are theirs, and stay.
    Annotation written = before;
    written.contents = QStringLiteral("the north wall");
    CHECK(core::withFreshReading(written, *core::mappedInto(written, half)).contents.toStdString() == "the north wall");
}

TEST_CASE("a measurement's model round-trips through its JSON", "[measure][A-88]")
{
    const Annotation dimension = measurement(MeasureKind::Distance, {72, 300, 288, 300}, scaleOf(48.0, MeasureUnit::Feet), -18.0);
    const core::Result<Annotation> back = Annotation::fromJson(dimension.toJson());
    REQUIRE(back);
    CHECK(back.value() == dimension);
    CHECK(back.value().leaderLength == -18.0);
    // A label writes one number format as an OBJECT; it reads as a chain of one.
    QJsonObject json = dimension.toJson();
    QJsonObject measure = json.value("measure").toObject();
    measure.insert("x", measure.value("x").toArray().at(0));
    json.insert("measure", measure);
    const core::Result<Annotation> fromLabel = Annotation::fromJson(json);
    REQUIRE(fromLabel);
    CHECK(fromLabel.value().measure == dimension.measure);
}

TEST_CASE("a producer's measurement this build cannot display is read moved and replayed and never redrawn",
          "[measure][A-88][D6-14]")
{
    // A /Measure whose unit is 100 characters long and whose factor is zero: kept as read -- the unit cut at the 64
    // a command accepts, the format NOT PLAIN -- so the measurement can be moved, and a move's journal line read back,
    // while a change that would need it redrawn is refused by key (S6 guide I-18's lesson, met at T12 before a run).
    const std::string longUnit(100, 'u');
    const std::string form = "q 0 0 1 RG 10 10 m 110 10 l S Q";
    const std::string bytes = tests::annotatedDocument(1, [&](int, tests::TinyPdf& pdf, int pageObject) -> std::string {
        const int ap = pdf.add("<< /Type /XObject /Subtype /Form /BBox [ 0 0 200 100 ] /Length " + std::to_string(form.size())
                               + " >>\nstream\n" + form + "\nendstream");
        const int line = pdf.add("<< /Type /Annot /Subtype /Line /Rect [ 0 0 200 100 ] /L [ 10 10 110 10 ] /IT /LineDimension "
                                 "/Measure << /Type /Measure /Subtype /RL /R (1 in = 1 in) /X [ << /U (" + longUnit
                                 + ") /C 0 /D 100 >> ] /D [ << /U (in) /C 1 /D 100 >> ] >> /C [ 0 0 1 ] /AP << /N "
                                 + std::to_string(ap) + " 0 R >> /P " + std::to_string(pageObject) + " 0 R >>");
        return "[ " + std::to_string(line) + " 0 R ]";
    });
    auto session = tests::openBuilt(QStringLiteral("long-unit.pdf"), bytes);
    REQUIRE(session);
    REQUIRE(session->journal->document().annotations().all().size() == 1);
    const Annotation read = session->journal->document().annotations().all().front();
    REQUIRE(read.measure);
    REQUIRE(read.measure->x.size() == 1);
    CHECK(read.measure->x[0].unit.size() == 64);
    CHECK_FALSE(read.measure->x[0].plain);
    CHECK(keyOf(core::measured(read).error()) == "error.measure.unsupportedScale");
    CHECK(core::isBoundedMeasure(*read.measure));

    // MOVED: carried, and its line read back.
    auto move = std::make_unique<core::MoveAnnotationCommand>(std::vector<AnnotationId>{read.id}, 5.0, 5.0, kNow);
    const QJsonObject line = move->serialize();
    REQUIRE(session->journal->run(std::move(move)));
    CHECK(core::CommandRegistry::deserialize(line));
    // RECOLOURED: refused by key -- it would need a redraw this build cannot make.
    core::AnnotationAppearance green;
    green.color = std::vector<double>{0, 1, 0};
    const core::Status recoloured = session->journal->run(
        std::make_unique<core::SetAnnotationAppearanceCommand>(std::vector<AnnotationId>{read.id}, green, kNow));
    REQUIRE_FALSE(recoloured);
    CHECK(keyOf(recoloured.error()) == "error.pdf.appearanceUnsupported");
}
