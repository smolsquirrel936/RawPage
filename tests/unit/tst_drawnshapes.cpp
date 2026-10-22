// tst_drawnshapes -- Stage 6 T8 headless: the geometry the drawing tools make and the builder draws
// (`core/doc/DrawnShapes`; `Stage6-Annotations-Testing-Guide.md` §5.10, A-59 and A-60's geometry; D6-15, D6-35).
//
//   * A-59: an ink stroke -- sharp turns, a loop, repeated points -- deduplicated as the tool does, drawn by the
//     BUILDER, and its content stream READ BACK with `pdf/cos/Lexer` (not `core::inkCurve`, which produced it):
//     every sample is where a segment starts or ends, within 0.01 pt, and every point of every cubic, sampled,
//     lies inside the annotation's rectangle less half the stroke's width.
//   * A-60's geometry: a whole stroke erased goes; one erased in the middle splits in two at the covered samples;
//     a sweep across a stroke between two samples splits it there; a fragment of one sample goes, a dot that was
//     not touched stays. (The command and its undo are `tst_annotdrawing`'s; the tool's, `tst_drawtools`'.)
//   * The endings: every one of ISO 32000-1 Table 176's ten names inside the disk the hit test grants it, but
//     the arrows, whose barbs reach back along the shaft -- or, reversed, forward past the tip.
//   * The cloud: bumps that bulge OUTWARD whichever way the polygon is written, their reach the largest radius,
//     intensity 2's larger than 1's; and the rectangle a cloud is written with holds every bump.
//   * `Shift`'s constraints: a line to 45-degree steps, a box square.
//
// WHAT IT DOES NOT COVER (convention 19): the engine -- whether a reader draws the stream as the geometry says is
// `tests/render/tst_appearance`'s and the golden suite's -- and the tools' gestures (`tests/ui/tst_drawtools`).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/doc/AnnotationGeometry.h"
#include "core/doc/DrawnShapes.h"
#include "pdf/cos/Lexer.h"
#include "pdf/ops/AppearanceStreamBuilder.h"

#include <cmath>
#include <string>
#include <vector>

using namespace rawpage;
using core::Annotation;
using core::AnnotationSubtype;
using core::PdfPoint;

namespace {

struct Cubic {
    PdfPoint p0, c1, c2, p3;
};

// The strokes of a content stream as the READER sees them: each `m` starts one, each `l` adds a straight cubic
// and each `c` a curve. Independent of `core::inkCurve` -- this is the file's geometry, not the function's.
std::vector<std::vector<Cubic>> strokesOf(const std::string& stream)
{
    std::vector<std::vector<Cubic>> out;
    pdf::cos::Lexer lexer(stream);
    std::vector<double> operands;
    PdfPoint current;
    for (pdf::cos::Token token = lexer.next(); !token.isEnd(); token = lexer.next()) {
        if (token.type == pdf::cos::TokenType::Integer || token.type == pdf::cos::TokenType::Real) {
            operands.push_back(token.real);   // set for both kinds of number
            continue;
        }
        if (token.type != pdf::cos::TokenType::Keyword) {
            operands.clear();
            continue;
        }
        if (token.text == "m" && operands.size() == 2) {
            current = PdfPoint{operands[0], operands[1]};
            out.emplace_back();
            out.back().push_back(Cubic{current, current, current, current});   // where it starts
        } else if (token.text == "l" && operands.size() == 2 && !out.empty()) {
            const PdfPoint to{operands[0], operands[1]};
            out.back().push_back(Cubic{current, current, to, to});
            current = to;
        } else if (token.text == "c" && operands.size() == 6 && !out.empty()) {
            const PdfPoint to{operands[4], operands[5]};
            out.back().push_back(Cubic{current, PdfPoint{operands[0], operands[1]}, PdfPoint{operands[2], operands[3]}, to});
            current = to;
        }
        operands.clear();
    }
    return out;
}

PdfPoint at(const Cubic& c, double t)
{
    const double u = 1.0 - t;
    const double a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    return PdfPoint{a * c.p0.x + b * c.c1.x + d * c.c2.x + e * c.p3.x, a * c.p0.y + b * c.c1.y + d * c.c2.y + e * c.p3.y};
}

Annotation inkOf(std::vector<std::vector<double>> strokes, double width)
{
    Annotation a = core::newAnnotation(AnnotationSubtype::Ink, 0, QStringLiteral("Ink"), 1791676800);
    a.inkList = std::move(strokes);
    a.color = {0.0, 0.0, 1.0};
    a.borderWidth = width;
    const std::optional<Annotation> fitted = core::fittedShape(a);
    REQUIRE(fitted);
    return *fitted;
}

double distance(PdfPoint a, PdfPoint b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

} // namespace

TEST_CASE("an ink stroke is drawn through every sample and inside its rectangle", "[drawnshapes][A-59][D6-15]")
{
    // Sharp turns, a loop, repeated points -- and a stroke that is a dot.
    const std::vector<double> zigzag{100, 100, 140, 160, 141, 100, 180, 161, 181, 100, 220, 160};
    std::vector<double> loop;
    for (int i = 0; i <= 40; ++i) {
        const double t = i * 2 * 3.14159265358979 / 36.0;   // a little past a full turn: it crosses itself
        loop.push_back(300 + 40 * std::cos(t) + i);
        loop.push_back(300 + 40 * std::sin(t));
    }
    const std::vector<double> repeated{50, 400, 50, 400, 50.1, 400.05, 60, 410, 60, 410, 70, 405, 70.01, 405};
    const std::vector<double> dot{500, 500};
    // Three right angles: the curve through a square's corners BULGES 12.5 points past the samples' own box (the
    // rectangle must hold the curve's control points, not only its samples -- planted control P2 stayed green until
    // this stroke was added).
    const std::vector<double> square{600, 100, 700, 100, 700, 200, 600, 200};

    // The tool's deduplication: a quarter of a device pixel at 2 device pixels a point.
    const double minimum = 0.25 / 2.0;
    std::vector<std::vector<double>> strokes;
    for (const std::vector<double>* stroke : std::vector<const std::vector<double>*>{&zigzag, &loop, &repeated, &dot, &square})
        strokes.push_back(core::dedupedSamples(*stroke, minimum));
    // The repeats went, and nothing else did.
    CHECK(strokes[0] == zigzag);
    CHECK(strokes[1] == loop);
    CHECK(strokes[2] == std::vector<double>{50, 400, 60, 410, 70, 405});
    CHECK(strokes[3] == dot);
    CHECK(strokes[4] == square);

    for (const double width : {1.0, 6.0}) {
        const Annotation ink = inkOf(strokes, width);
        CHECK(ink.inkList == strokes);   // /InkList is the samples, as kept
        auto built = pdf::ops::AppearanceStreamBuilder::normal(ink);
        REQUIRE(built);
        const std::vector<std::vector<Cubic>> drawn = strokesOf(built.value().data);
        REQUIRE(drawn.size() == strokes.size());

        const double half = width / 2.0;
        int sampled = 0;
        for (std::size_t s = 0; s < strokes.size(); ++s) {
            const std::vector<double>& samples = strokes[s];
            const std::vector<Cubic>& path = drawn[s];
            // EVERY SAMPLE is where the path starts or where one of its pieces ends, in order.
            REQUIRE(path.size() == std::max<std::size_t>(samples.size() / 2, 2));
            for (std::size_t k = 0; k * 2 < samples.size(); ++k) {
                const PdfPoint sample{samples[2 * k], samples[2 * k + 1]};
                INFO("stroke " << s << " sample " << k);
                CHECK(distance(path[k].p3, sample) <= 0.01);
            }
            // EVERY POINT OF THE CURVE, sampled, inside the rectangle less half the stroke.
            for (const Cubic& piece : path) {
                for (int i = 0; i <= 50; ++i) {
                    const PdfPoint p = at(piece, i / 50.0);
                    ++sampled;
                    INFO("width " << width << " stroke " << s << " point " << p.x << "," << p.y);
                    CHECK(p.x >= ink.rect.left() + half - 1e-6);
                    CHECK(p.x <= ink.rect.right() - half + 1e-6);
                    CHECK(p.y >= ink.rect.bottom() + half - 1e-6);
                    CHECK(p.y <= ink.rect.top() - half + 1e-6);
                }
            }
        }
        CHECK(sampled > 1000);
    }
}

TEST_CASE("the curve through samples is centripetal and does not overshoot a sharp turn", "[drawnshapes][A-59][D6-15]")
{
    // A hairpin: out and straight back. The uniform Catmull-Rom loops past the turn; the centripetal one stays
    // within the segment's neighbourhood -- no point of the turn's two segments further out than the tip by more
    // than a fifth of the chord.
    const std::vector<double> hairpin{0, 0, 100, 0, 101, 2, 0, 4};
    const std::vector<core::CubicSegment> curve = core::inkCurve(hairpin);
    REQUIRE(curve.size() == 3);
    double furthest = 0.0;
    for (const core::CubicSegment& s : curve) {
        const Cubic c{s.p0, s.c1, s.c2, s.p3};
        for (int i = 0; i <= 100; ++i) furthest = std::max(furthest, at(c, i / 100.0).x);
    }
    CHECK(furthest <= 101.0 + 0.2 * 2.24);
    // And the ends repeat their sample: the first and last control point ARE the end samples.
    CHECK(distance(curve.front().c1, PdfPoint{0, 0}) < 1e-9);
    CHECK(distance(curve.back().c2, PdfPoint{0, 4}) < 1e-9);
}

TEST_CASE("the eraser removes the samples it covers and splits a stroke where it did", "[drawnshapes][A-60][D6-15]")
{
    const std::vector<double> whole{10, 10, 12, 10, 14, 10};             // short: wholly under the eraser
    std::vector<double> long_;
    for (int x = 0; x <= 100; x += 5) {
        long_.push_back(x);
        long_.push_back(50);
    }
    const std::vector<double> dot{200, 200};

    SECTION("a whole stroke goes, the middle of another splits it in two at the covered samples")
    {
        // A short swipe over the first stroke and down through the middle of the second.
        const std::vector<double> swipe{12, 4, 12, 16};
        const std::vector<double> cross{50, 40, 50, 60};
        core::ErasedInk first = core::erasedStrokes({whole, long_, dot}, swipe, 5.0);
        CHECK(first.changed);
        REQUIRE(first.strokes.size() == 2);   // the long one and the dot
        CHECK(first.strokes[0] == long_);
        CHECK(first.strokes[1] == dot);

        core::ErasedInk second = core::erasedStrokes(first.strokes, cross, 3.0);
        CHECK(second.changed);
        REQUIRE(second.strokes.size() == 3);
        // Covered: within 3 of x = 50 -- the samples at 50 alone (45 and 55 are 5 away).
        std::vector<double> left, right;
        for (int x = 0; x <= 45; x += 5) {
            left.push_back(x);
            left.push_back(50);
        }
        for (int x = 55; x <= 100; x += 5) {
            right.push_back(x);
            right.push_back(50);
        }
        CHECK(second.strokes[0] == left);
        CHECK(second.strokes[1] == right);
        CHECK(second.strokes[2] == dot);
    }
    SECTION("a sweep between two samples splits there and removes none")
    {
        const std::vector<double> sparse{0, 0, 40, 0, 80, 0, 120, 0};
        const core::ErasedInk erased = core::erasedStrokes({sparse}, std::vector<double>{60, -20, 60, 20}, 1.0);
        CHECK(erased.changed);
        REQUIRE(erased.strokes.size() == 2);
        CHECK(erased.strokes[0] == std::vector<double>{0, 0, 40, 0});
        CHECK(erased.strokes[1] == std::vector<double>{80, 0, 120, 0});
    }
    SECTION("a fragment of one sample goes; an untouched dot stays; nothing touched changes nothing")
    {
        const std::vector<double> three{0, 0, 10, 0, 20, 0};
        const core::ErasedInk erased = core::erasedStrokes({three, dot}, std::vector<double>{10, 0}, 2.0);
        CHECK(erased.changed);
        // The middle sample covered leaves two single samples -- fragments -- and the dot.
        REQUIRE(erased.strokes.size() == 1);
        CHECK(erased.strokes[0] == dot);

        const core::ErasedInk nothing = core::erasedStrokes({three, dot}, std::vector<double>{500, 500, 600, 600}, 2.0);
        CHECK_FALSE(nothing.changed);
        CHECK(nothing.strokes == std::vector<std::vector<double>>{three, dot});
    }
}

TEST_CASE("every line ending lies where the hit test grants it and the arrows point the way they say", "[drawnshapes][D6-35]")
{
    const PdfPoint tip{100, 100}, from{0, 100};   // a line arriving from the left
    const double size = core::lineEndingSize(1.0);
    CHECK(size == 9.0);
    CHECK(core::lineEndingSize(4.0) == 18.0);
    CHECK_FALSE(core::lineEnding(QStringLiteral("None"), tip, from, size));
    CHECK_FALSE(core::lineEnding(QStringLiteral("Wavy"), tip, from, size));
    CHECK_FALSE(core::lineEnding(QStringLiteral("OpenArrow"), tip, tip, size));   // no direction

    for (const char* name : {"Square", "Circle", "Diamond", "Butt", "Slash"}) {
        INFO(name);
        const std::optional<core::EndingShape> shape = core::lineEnding(QString::fromLatin1(name), tip, from, size);
        REQUIRE(shape);
        if (shape->kind == core::EndingShape::Kind::Circle) {
            CHECK(distance(shape->centre, tip) < 1e-9);
            CHECK(shape->radius <= size / 2.0);
            continue;
        }
        for (std::size_t i = 0; i + 1 < shape->points.size(); i += 2)
            CHECK(distance(PdfPoint{shape->points[i], shape->points[i + 1]}, tip) <= size / 2.0 + 1e-9);
    }
    // An arrow's barbs reach BACK along the shaft (x below the tip); a reversed one's forward, past it.
    for (const char* name : {"OpenArrow", "ClosedArrow", "ROpenArrow", "RClosedArrow"}) {
        INFO(name);
        const std::optional<core::EndingShape> shape = core::lineEnding(QString::fromLatin1(name), tip, from, size);
        REQUIRE(shape);
        REQUIRE(shape->points.size() == 6);
        CHECK(distance(PdfPoint{shape->points[2], shape->points[3]}, tip) < 1e-9);   // the tip in the middle
        const bool reversed = name[0] == 'R';
        for (const std::size_t barb : {std::size_t{0}, std::size_t{4}}) {
            CHECK(distance(PdfPoint{shape->points[barb], shape->points[barb + 1]}, tip) == Catch::Approx(size));
            CHECK((reversed ? shape->points[barb] > tip.x : shape->points[barb] < tip.x));
        }
        CHECK((shape->kind == core::EndingShape::Kind::Closed) == (std::string(name).find("Closed") != std::string::npos));
        // And the hit test finds the ending where it is drawn: on the barb's far end, not 3 points beyond it.
        Annotation line = core::newAnnotation(AnnotationSubtype::Line, 0, QString(), 0);
        line.line = {from.x, from.y, tip.x, tip.y};
        line.lineEndings = {QStringLiteral("None"), QString::fromLatin1(name)};
        line.borderWidth = 1.0;
        line = *core::fittedShape(line);
        CHECK(core::hitsAnnotation(line, PdfPoint{shape->points[0], shape->points[1]}, 0.5));
    }
}

TEST_CASE("a cloud's bumps bulge outward whichever way it is written and its rectangle holds them", "[drawnshapes][A-58][D6-35]")
{
    const std::vector<double> square{100, 100, 200, 100, 200, 200, 100, 200};   // counter-clockwise
    const std::vector<double> clockwise{100, 100, 100, 200, 200, 200, 200, 100};
    for (const std::vector<double>* vertices : {&square, &clockwise}) {
        for (const double intensity : {1.0, 2.0}) {
            INFO("intensity " << intensity);
            const std::vector<core::CubicSegment> cloud = core::cloudPath(*vertices, intensity);
            REQUIRE_FALSE(cloud.empty());
            // A closed path: the last piece ends where the first began.
            CHECK(distance(cloud.back().p3, cloud.front().p0) < 1e-6);
            // Every bump's top -- each first piece's end -- is OUTSIDE the square.
            for (std::size_t i = 0; i < cloud.size(); i += 2) {
                const PdfPoint top = cloud[i].p3;
                CHECK((top.x < 100 || top.x > 200 || top.y < 100 || top.y > 200));
            }
            const double reach = core::cloudReach(*vertices, intensity);
            CHECK(reach > 0.0);
            CHECK(reach < core::cloudBump(intensity));
        }
    }
    CHECK(core::cloudBump(1.0) == 10.0);   // the corpus generator's
    CHECK(core::cloudReach(square, 2.0) > core::cloudReach(square, 1.0));
    CHECK(core::cloudPath(std::vector<double>{0, 0, 10, 10}, 1.0).empty());   // two corners: no cloud
    CHECK(core::cloudPath(std::vector<double>{0, 0, 10, 0, 20, 0}, 1.0).empty());   // no area

    // The rectangle a cloud is written with holds every point of every bump, the stroke's half and a point.
    Annotation cloud = core::newAnnotation(AnnotationSubtype::Polygon, 0, QString(), 0);
    cloud.vertices = square;
    cloud.cloudIntensity = 2.0;
    cloud.borderWidth = 2.0;
    const std::optional<Annotation> fitted = core::fittedShape(cloud);
    REQUIRE(fitted);
    for (const core::CubicSegment& s : core::cloudPath(square, 2.0)) {
        for (int i = 0; i <= 20; ++i) {
            const PdfPoint p = at(Cubic{s.p0, s.c1, s.c2, s.p3}, i / 20.0);
            CHECK(p.x >= fitted->rect.left() + 1.0);
            CHECK(p.x <= fitted->rect.right() - 1.0);
            CHECK(p.y >= fitted->rect.bottom() + 1.0);
            CHECK(p.y <= fitted->rect.top() - 1.0);
        }
    }
    // And the hit test reaches the bumps, not the band past them.
    const double reach = core::cloudReach(square, 2.0);
    CHECK(core::hitsAnnotation(*fitted, PdfPoint{150, 100 - reach * 0.9}, 0.1));
    CHECK_FALSE(core::hitsAnnotation(*fitted, PdfPoint{150, 100 - reach - 4.0}, 0.1));
}

TEST_CASE("a cloud of more bumps than the bound is no cloud and one within it is drawn", "[drawnshapes][D6-35][I-37]")
{
    // S6 guide I-37, found reading I-36's code: an edge's bumps were counted with no bound -- an edge of 10^9 points
    // is 10^8 bumps, 13 GB of cubics -- and cast to `int` however many there were, which past INT_MAX is undefined.
    // A thin triangle at intensity 1 (10-point bumps): its two long edges are `side / 10` bumps each, its short one 1.
    const double bound = static_cast<double>(core::kMaxDrawnSteps);
    const auto triangle = [](double side) { return std::vector<double>{0, 0, side, 0, 0, 10}; };
    REQUIRE(core::cloudBump(1.0) == 10.0);

    // The control: within the bound the cloud is there, every bump of it (`cloudReach` walks the same edges as
    // `cloudPath` and allocates nothing, so the control costs no 100 MB path).
    CHECK(core::cloudReach(triangle(0.45 * bound * 10.0), 1.0) > 0.0);
    CHECK(core::cloudPath(triangle(1000.0), 1.0).size() == 2 * (100 + 100 + 1));
    // Past it, no cloud: no path, no reach, no rectangle -- and the builder refuses it by key, as any cloud with none.
    REQUIRE(core::cloudPath(triangle(0.55 * bound * 10.0), 1.0).empty());   // REQUIRE: unbounded, what follows is 25 GB
    CHECK(core::cloudReach(triangle(0.55 * bound * 10.0), 1.0) == 0.0);
    CHECK(core::cloudPath(triangle(1.0e300), 1.0).empty());   // 10^299 bumps: the cast that was undefined
    const auto polygon = [&](double side) {
        Annotation a = core::newAnnotation(AnnotationSubtype::Polygon, 0, QString(), 0);
        a.vertices = triangle(side);
        a.cloudIntensity = 1.0;
        a.color = {1.0, 0.0, 0.0};
        a.rect = core::PdfRect{PdfPoint{0, 0}, core::PdfSize{side, 10}};
        return a;
    };
    const auto refused = [](const Annotation& a) {
        const auto built = pdf::ops::AppearanceStreamBuilder::normal(a);
        return !built && built.error().messageKey == QLatin1String("error.pdf.appearanceGeometry");
    };
    CHECK(core::fittedShape(polygon(1000.0)));   // each refusal's control: the same shape, short, drawn
    CHECK(pdf::ops::AppearanceStreamBuilder::normal(polygon(1000.0)));
    CHECK_FALSE(core::fittedShape(polygon(1.0e9)));
    CHECK(refused(polygon(1.0e9)));
    // A cloudy SQUARE draws its rectangle's four sides the same way: 4e7 points of side is 4e6 bumps.
    const auto square = [](double side) {
        Annotation a = core::newAnnotation(AnnotationSubtype::Square, 0, QString(), 0);
        a.cloudIntensity = 1.0;
        a.color = {1.0, 0.0, 0.0};
        a.rect = core::PdfRect{PdfPoint{0, 0}, core::PdfSize{side, side}};
        return a;
    };
    CHECK(pdf::ops::AppearanceStreamBuilder::normal(square(100.0)));
    CHECK(refused(square(1.0e7)));
}

TEST_CASE("a shape that draws nothing has no rectangle to fit", "[drawnshapes][D6-35]")
{
    Annotation line = core::newAnnotation(AnnotationSubtype::Line, 0, QString(), 0);
    line.line = {5, 5, 5, 5};
    CHECK_FALSE(core::fittedShape(line));
    Annotation polygon = core::newAnnotation(AnnotationSubtype::Polygon, 0, QString(), 0);
    polygon.vertices = {0, 0, 10, 10};
    CHECK_FALSE(core::fittedShape(polygon));
    Annotation polyline = core::newAnnotation(AnnotationSubtype::PolyLine, 0, QString(), 0);
    polyline.vertices = {0, 0, 10, 10};
    CHECK(core::fittedShape(polyline));
    Annotation ink = core::newAnnotation(AnnotationSubtype::Ink, 0, QString(), 0);
    CHECK_FALSE(core::fittedShape(ink));
    ink.inkList = {{1, 2, std::nan("")}};
    CHECK_FALSE(core::fittedShape(ink));
}

TEST_CASE("Shift keeps a line to 45 degree steps and a box square", "[drawnshapes][A-58]")
{
    const PdfPoint from{100, 100};
    PdfPoint p = core::constrainedTo45(from, PdfPoint{200, 110});   // nearly flat: flat
    CHECK(p.y == 100.0);
    CHECK(p.x == Catch::Approx(200.0));
    p = core::constrainedTo45(from, PdfPoint{150, 145});            // nearly diagonal: diagonal
    CHECK(p.x - 100 == Catch::Approx(p.y - 100));
    p = core::constrainedTo45(from, PdfPoint{103, 40});             // nearly straight down: straight down
    CHECK(p.x == 100.0);
    CHECK(p.y == Catch::Approx(40.0));
    p = core::constrainedSquare(from, PdfPoint{160, 80});
    CHECK(p.x - 100 == Catch::Approx(60.0));
    CHECK(p.y - 100 == Catch::Approx(-60.0));
}
