// tst_annotation -- Stage 6 T2, guide case A-20 (`Stage6-Annotations-Testing-Guide.md` §5.4): the
// invariants of `core/doc/Annotation` and of how `pdf/ops/AnnotationReader` fills it. Headless.
//
//   * a rectangle is normalized from any two diagonal corners, and a non-finite one is refused rather
//     than zeroed silently;
//   * quadpoints are kept in the order written -- Acrobat's top-left, top-right, bottom-left,
//     bottom-right, and ISO 32000-1's figure's counter-clockwise order, each read as written;
//   * colour arrays of 0, 1, 3 and 4 components;
//   * flags read and round-trip;
//   * dates parsed to UTC across zones, with the fields ISO lets a producer omit;
//   * `fromJson(toJson(a)) == a` for every subtype, and for every annotation the labelled corpus holds.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Annotation.h"
#include "core/doc/AnnotationStore.h"
#include "tests/unit/TinyPdf.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <limits>
#include <string>

using namespace rawpage;
using core::Annotation;
using core::AnnotationSubtype;

namespace {

// A one-page document carrying the annotation dictionaries given, each indirect.
std::optional<core::AnnotationStore::Reading> readOne(const std::vector<std::string>& dictionaries)
{
    return tests::readAnnotations(tests::annotatedDocument(1, [&](int, tests::TinyPdf& pdf, int page) {
        std::string refs = "[ ";
        for (const std::string& body : dictionaries) {
            refs += std::to_string(pdf.add("<< /Type /Annot /P " + std::to_string(page) + " 0 R " + body + " >>"))
                    + " 0 R ";
        }
        return refs + "]";
    }));
}

} // namespace

TEST_CASE("a rectangle is normalized from any two diagonal corners", "[annotation][A-20]")
{
    const auto swapped = core::normalizedRect(300, 680, 60, 600);
    REQUIRE(swapped);
    CHECK(swapped->left() == 60);
    CHECK(swapped->bottom() == 600);
    CHECK(swapped->right() == 300);
    CHECK(swapped->top() == 680);
    CHECK_FALSE(core::normalizedRect(0, 0, std::numeric_limits<double>::infinity(), 10));
    CHECK_FALSE(core::normalizedRect(std::nan(""), 0, 10, 10));

    const auto reading = readOne({"/Subtype /Square /Rect [ 300 680 60 600 ]"});
    REQUIRE(reading);
    REQUIRE(reading->annotations.size() == 1);
    CHECK(reading->annotations[0].rect == *swapped);
    REQUIRE(reading->pages[0][0].fact.rect);
    CHECK(*reading->pages[0][0].fact.rect == *swapped);
}

TEST_CASE("quadpoints are kept in the order the file wrote them", "[annotation][A-20]")
{
    // Acrobat's order and ISO 32000-1 Figure 64's: the same quadrilateral, two corner orders.
    const std::vector<double> acrobat{48, 500, 300, 500, 48, 480, 300, 480};
    const std::vector<double> iso{48, 480, 300, 480, 300, 500, 48, 500};
    const auto reading = readOne({"/Subtype /Highlight /Rect [ 48 480 300 500 ] /QuadPoints [ 48 500 300 500 48 480 300 480 ]",
                                  "/Subtype /Highlight /Rect [ 48 480 300 500 ] /QuadPoints [ 48 480 300 480 300 500 48 500 ]"});
    REQUIRE(reading);
    REQUIRE(reading->annotations.size() == 2);
    CHECK(reading->annotations[0].quadPoints == acrobat);
    CHECK(reading->annotations[1].quadPoints == iso);
}

TEST_CASE("colour arrays of none one three and four components are read", "[annotation][A-20]")
{
    const auto reading = readOne({"/Subtype /Square /Rect [ 0 0 1 1 ] /C [ ]",
                                  "/Subtype /Square /Rect [ 0 0 1 1 ] /C [ 0.5 ]",
                                  "/Subtype /Square /Rect [ 0 0 1 1 ] /C [ 1 0.5 0 ] /IC [ 0 0 1 ]",
                                  "/Subtype /Square /Rect [ 0 0 1 1 ] /C [ 0 0.1 0.2 0.3 ]"});
    REQUIRE(reading);
    REQUIRE(reading->annotations.size() == 4);
    CHECK(reading->annotations[0].color.empty());
    CHECK(reading->annotations[1].color == std::vector<double>{0.5});
    CHECK(reading->annotations[2].color == std::vector<double>{1, 0.5, 0});
    CHECK(reading->annotations[2].interiorColor == std::vector<double>{0, 0, 1});
    CHECK(reading->annotations[3].color == std::vector<double>{0, 0.1, 0.2, 0.3});
}

TEST_CASE("flags are read and named", "[annotation][A-20]")
{
    const auto reading = readOne({"/Subtype /Text /Rect [ 0 0 20 20 ] /F 28",
                                  "/Subtype /Square /Rect [ 0 0 1 1 ] /F 642"});
    REQUIRE(reading);
    const Annotation& note = reading->annotations[0];
    CHECK(note.hasFlag(core::AnnotationFlag::Print));
    CHECK(note.hasFlag(core::AnnotationFlag::NoZoom));
    CHECK(note.hasFlag(core::AnnotationFlag::NoRotate));
    CHECK_FALSE(note.hasFlag(core::AnnotationFlag::Hidden));
    const Annotation& locked = reading->annotations[1];   // 642 = Hidden | Locked | LockedContents
    CHECK(locked.flags == (core::AnnotationFlag::Hidden | core::AnnotationFlag::Locked
                           | core::AnnotationFlag::LockedContents));
}

TEST_CASE("a PDF date is read to the same UTC instant in any zone", "[annotation][A-20]")
{
    CHECK(core::PdfDate::parse(QStringLiteral("D:20260901091500+08'00'")).utcIso()
          == QStringLiteral("2026-09-01T01:15:00Z"));
    CHECK(core::PdfDate::parse(QStringLiteral("D:20260905084000Z")).utcIso() == QStringLiteral("2026-09-05T08:40:00Z"));
    CHECK(core::PdfDate::parse(QStringLiteral("D:20260906180500-05'00'")).utcIso()
          == QStringLiteral("2026-09-06T23:05:00Z"));
    CHECK(core::PdfDate::parse(QStringLiteral("D:20260906180500-05'30")).utcIso()
          == QStringLiteral("2026-09-06T23:35:00Z"));       // the closing apostrophe is often missing
    CHECK(core::PdfDate::parse(QStringLiteral("D:2026")).utcIso() == QStringLiteral("2026-01-01T00:00:00Z"));
    CHECK(core::PdfDate::parse(QStringLiteral("D:202609")).utcIso() == QStringLiteral("2026-09-01T00:00:00Z"));
    CHECK(core::PdfDate::parse(QStringLiteral("20260101000000")).utcIso() == QStringLiteral("2026-01-01T00:00:00Z"));
    // Crossing a day backwards: midnight in UTC+8 is the previous day in UTC.
    CHECK(core::PdfDate::parse(QStringLiteral("D:20260301000000+08'00'")).utcIso()
          == QStringLiteral("2026-02-28T16:00:00Z"));
    // Not a date: kept as written, no instant.
    for (const char* bad : {"D:2026134", "yesterday", "D:20261340", "D:20260230"}) {
        const core::PdfDate date = core::PdfDate::parse(QString::fromLatin1(bad));
        INFO(bad);
        CHECK_FALSE(date.utcSeconds);
        CHECK(date.raw == QString::fromLatin1(bad));
    }
}

TEST_CASE("an identity survives its own string form", "[annotation][A-20]")
{
    for (const core::AnnotationId& id : {core::AnnotationId::ofObject(12), core::AnnotationId::ofDirect(0, 3),
                                         core::AnnotationId::ofCreated(QStringLiteral("9f86d081-884c-7d65"))}) {
        const auto back = core::AnnotationId::fromString(id.toString());
        REQUIRE(back);
        CHECK(*back == id);
    }
    CHECK_FALSE(core::AnnotationId::fromString(QStringLiteral("obj:0")));
    CHECK_FALSE(core::AnnotationId::fromString(QStringLiteral("direct:1")));
    CHECK_FALSE(core::AnnotationId::fromString(QStringLiteral("nm:")));
}

TEST_CASE("every subtype name reads back to its subtype", "[annotation][A-20]")
{
    for (int value = 1; value <= static_cast<int>(AnnotationSubtype::Redact); ++value) {
        const auto subtype = static_cast<AnnotationSubtype>(value);
        if (subtype == AnnotationSubtype::XfaWidget) continue;       // no file writes it as a /Subtype
        const QLatin1String name = core::annotationSubtypeName(subtype);
        INFO(value);
        REQUIRE_FALSE(name.isEmpty());
        CHECK(core::annotationSubtypeFromName(std::string(name.data(), static_cast<std::size_t>(name.size()))) == subtype);
    }
    CHECK(core::annotationSubtypeFromName("Projection") == AnnotationSubtype::Unknown);
    CHECK(core::annotationSubtypeFromName("highlight") == AnnotationSubtype::Unknown);   // names are case-sensitive
}

TEST_CASE("an annotation of every subtype survives its JSON", "[annotation][A-20]")
{
    for (int value = 0; value <= static_cast<int>(AnnotationSubtype::Redact); ++value) {
        if (static_cast<AnnotationSubtype>(value) == AnnotationSubtype::XfaWidget) continue;
        Annotation a;
        a.id = core::AnnotationId::ofObject(40 + value);
        a.subtype = static_cast<AnnotationSubtype>(value);
        if (a.subtype == AnnotationSubtype::Unknown) a.subtypeName = QStringLiteral("Projection");
        a.page = value % 3;
        a.rect = *core::normalizedRect(10, 20, 30.5, 40.25);
        a.quadPoints = {1, 2, 3, 4, 5, 6, 7, 8};
        a.vertices = {1, 1, 2, 2};
        a.line = {0, 0, 9, 9};
        a.inkList = {{1, 2, 3, 4}, {5, 6}};
        a.callout = {1, 2, 3, 4, 5, 6};
        a.rectDifferences = {1, 1, 1, 1};
        a.lineEndings = {QStringLiteral("OpenArrow"), QStringLiteral("None")};
        a.color = {1, 0, 0};
        a.interiorColor = {0.25};
        a.opacity = 0.5;
        a.borderWidth = 2;
        a.nm = QStringLiteral("nm-%1").arg(value);
        a.author = QStringLiteral("Zoë Müller");
        a.subject = QStringLiteral("subject");
        a.contents = QStringLiteral("contents with 中文");
        a.richText = QStringLiteral("<body><p>x</p></body>");
        a.intent = QStringLiteral("FreeTextCallout");
        a.icon = QStringLiteral("Comment");
        a.defaultAppearance = QStringLiteral("0 g /Helv 10 Tf");
        a.created = core::PdfDate::parse(QStringLiteral("D:20260901091500+08'00'"));
        a.modified = core::PdfDate::parse(QStringLiteral("D:20260902091500Z"));
        a.flags = 28;
        a.inReplyTo = 1LL << 60;                     // beyond a double's 53 bits: kept exactly
        a.replyType = core::ReplyType::Group;
        a.stateModel = QStringLiteral("Review");
        a.state = QStringLiteral("Accepted");
        a.popup = 77;
        a.parent = 76;
        a.open = false;
        a.rotate = 90;
        a.hasAppearance = true;
        const auto back = Annotation::fromJson(a.toJson());
        INFO(value);
        REQUIRE(back);
        CHECK(back.value() == a);
    }
    // And the minimal one: every optional absent stays absent.
    Annotation bare;
    bare.id = core::AnnotationId::ofDirect(2, 0);
    bare.subtype = AnnotationSubtype::Square;
    const auto back = Annotation::fromJson(bare.toJson());
    REQUIRE(back);
    CHECK(back.value() == bare);
}

TEST_CASE("every annotation of the labelled corpus survives its JSON", "[annotation][A-20]")
{
    int checked = 0;
    for (const char* file : {"annots-every-type", "annots-no-ap", "annots-threads", "annots-flags", "annots-rotated",
                             "annots-direct", "annots-unknown-keys", "attachments", "measure-scale",
                             "annots-report-v1", "annots-900"}) {
        QFile pdf(QStringLiteral(RAWPAGE_CORPUS_DIR "/ordinary/%1.pdf").arg(QLatin1String(file)));
        REQUIRE(pdf.open(QIODevice::ReadOnly));
        const QByteArray bytes = pdf.readAll();
        const auto reading = tests::readAnnotations(bytes.toStdString());
        REQUIRE(reading);
        for (const Annotation& a : reading->annotations) {
            const auto back = Annotation::fromJson(a.toJson());
            REQUIRE(back);
            CHECK(back.value() == a);
            ++checked;
        }
    }
    CHECK(checked > 9000);
}
