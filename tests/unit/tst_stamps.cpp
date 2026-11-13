// tst_stamps -- Stage 6 T10 headless: what a stamp is and what the writer makes of it, below the widgets
// (`Stage6-Annotations-Testing-Guide.md` §5.13; D6-16, D6-37).
//
//   * The vocabulary: Table 181's fourteen names, the words a name shows, the dynamic lines at the host's offset.
//   * A JPEG's header read for its size, and every frame `/DCTDecode` cannot read refused by key; pixels held
//     compressed, an opaque alpha dropped.
//   * The geometry: a stamp's rectangle is its natural box turned and scaled; a turn keeps the centre and the
//     scale; a dragged box fits the turned box.
//   * A-75, the file half: every standard stamp saved and read back with a `/Name` from Table 181 and a form whose
//     every resource resolves -- Helvetica-Bold with its `/Widths`.
//   * A-76: a dynamic stamp BAKED -- its lines from the clock and offset at placement, saved, read back the same
//     with no clock at all, and a redraw (a turn) drawing the baked lines, not the time of the redraw.
//   * A-77, the file half: a JPEG written `/DCTDecode` BYTE-IDENTICAL to the file; pixels written `/FlateDecode`
//     that decompress to the samples given, their alpha an `/SMask` that decompresses to the alpha given.
//   * A-78, the file half: one image on two pages, in one save, is ONE image object and one mask, named by both
//     appearances.
//   * A-80, the file half: a resize is not a redraw -- the appearance object is carried -- and a turn is one
//     command whose form carries the turn in `/Matrix` and whose rectangle bounds the turned box.
//   * A stamp RawPage cannot redraw -- a foreign `/Name`, an image stamp reopened -- moves and resizes, and a
//     turn is refused by key; the corpus's own `/Approved` recoloured IS redrawn, every unmodelled key kept (A-26's
//     stamp half), and a dynamic stamp's lines edited are the lines drawn.
//   * A-29's stamp half: an image stamp travels in a journal line; pixels on a kind that is not an image stamp,
//     a JPEG whose header disagrees and bytes that are not base64 are refused by key; and a journal of every kind of
//     stamp, a turn and an undo, replayed by `RecoveryJournal` into a fresh open, reaches the same model and composes
//     the same bytes. The image travels INSIDE its line until T11's side files (D6-23).
//
// WHAT IT DOES NOT COVER (convention 19): pixels. Whether PDFium draws the stamp where its rectangle says, the
// image as sharp as the source, and the turned ink inside the rectangle, is `tests/composition/tst_stampink`'s
// (A-77's and A-80's ink halves); what each stamp LOOKS like is the golden suite's (A-75's baselines). A PNG
// decoded is `ui/`'s (convention 3): the pixels here are synthetic. The palette, the tool and the paste are
// `tests/ui/tst_stamps`'.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/Stamps.h"
#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/Lexer.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/PageStamp.h"
#include "pdf/cos/filters/Filter.h"
#include "pdf/cos/filters/FlateFilter.h"
#include "pdf/ops/RevisionWriter.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using core::AddAnnotationCommand;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;
using core::PdfPoint;
using core::PdfRect;
using core::PdfSize;
using pdf::cos::CosObject;

namespace {

constexpr std::int64_t kPlaced = 1791106200;   // 2026-10-04T09:30:00Z -- A-76's clock
const QString kAuthor = QStringLiteral("Stamp Tester");

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QByteArray fileBytes(const char* relative)
{
    QFile file(tests::corpusFile(relative));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

QByteArray inflated(const QByteArray& zlib, qsizetype size)
{
    QByteArray framed(4, '\0');
    framed[0] = static_cast<char>((size >> 24) & 0xFF);
    framed[1] = static_cast<char>((size >> 16) & 0xFF);
    framed[2] = static_cast<char>((size >> 8) & 0xFF);
    framed[3] = static_cast<char>(size & 0xFF);
    return qUncompress(framed + zlib);
}

// A 40 x 20 picture: an RGB gradient, and an alpha that is clear on the left, opaque on the right and half in
// between -- every kind of value an `/SMask` must carry.
struct Pixels {
    int width = 40;
    int height = 20;
    QByteArray rgb;
    QByteArray alpha;
    Pixels()
    {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                rgb.append(static_cast<char>(x * 6));
                rgb.append(static_cast<char>(y * 12));
                rgb.append(static_cast<char>(200));
                alpha.append(static_cast<char>(x < 10 ? 0 : (x < 30 ? 128 : 255)));
            }
        }
    }
};

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
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);   // kept for `qpdf --check` (A-31)
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
    CosObject dictionaryOf(const QString& nm)
    {
        const Annotation* a = withNm(nm);
        REQUIRE(a != nullptr);
        REQUIRE(a->id.kind == AnnotationId::Kind::Object);
        return object(a->id.object);
    }
    CosObject appearanceOf(const QString& nm) { return resolved(dictionaryOf(nm).get("AP").get("N")); }
    std::string streamBytes(const CosObject& stream) { return std::string(stream.rawStreamData(table->source())); }
};

// Every resource the stream names -- `gs`, `Tf`, `Do` -- held by the form's own resources.
std::vector<std::string> unresolved(ReadBack& back, const CosObject& form)
{
    std::vector<std::string> missing;
    const CosObject resources = back.resolved(form.get("Resources"));
    const std::string data = back.streamBytes(form);
    pdf::cos::Lexer lexer(data);
    std::string last;
    for (pdf::cos::Token token = lexer.next(); !token.isEnd(); token = lexer.next()) {
        if (token.type == pdf::cos::TokenType::Name) {
            last = token.text;
            continue;
        }
        if (token.type != pdf::cos::TokenType::Keyword) continue;
        const char* family = token.text == "gs" ? "ExtGState" : token.text == "Tf" ? "Font" : token.text == "Do" ? "XObject" : nullptr;
        if (family != nullptr && !back.resolved(resources.get(family)).has(last)) missing.push_back(std::string(family) + " " + last);
    }
    return missing;
}

std::vector<double> numbersOf(ReadBack& back, const CosObject& array)
{
    std::vector<double> out;
    const CosObject held = back.resolved(array);   // held: a range over a temporary's items would dangle
    for (const CosObject& n : held.items()) out.push_back(n.numberValue());
    return out;
}

bool near(double a, double b, double tolerance = 1e-3) { return std::abs(a - b) <= tolerance; }

QString hexOf(const QString& ascii) { return QString::fromLatin1(ascii.toLatin1().toHex().toUpper()); }

Annotation standard(const char* name, int page, PdfPoint centre)
{
    auto made = core::newStandardStamp(QLatin1String(name), page, centre, kAuthor, kPlaced);
    REQUIRE(made);
    return made.value();
}

} // namespace

// ---- The vocabulary --------------------------------------------------------------------------------

TEST_CASE("stamps: Table 181 has fourteen names and each shows its own words", "[stamps][D6-16]")
{
    REQUIRE(core::standardStampNames().size() == 14);
    CHECK(core::stampTitle(u"NotForPublicRelease") == QStringLiteral("NOT FOR PUBLIC RELEASE"));
    CHECK(core::stampTitle(u"AsIs") == QStringLiteral("AS IS"));
    CHECK(core::stampTitle(u"DynamicApproved") == QStringLiteral("APPROVED"));
    CHECK(core::isDynamicStampName(u"DynamicReceived"));
    CHECK_FALSE(core::isStandardStampName(u"DynamicReceived"));
    CHECK_FALSE(core::isStandardStampName(u"SBApproved"));
    for (const QLatin1String name : core::standardStampNames()) {
        INFO(name.data());
        // Every title is set in Helvetica-Bold, whose advances are written for codes 32 to 90 only.
        CHECK(core::helveticaBoldTextWidth(core::stampTitle(QString(name)), 20.0).has_value());
        CHECK(core::stampColour(QString(name)).size() == 3);
    }
    CHECK(core::stampColour(u"Approved") != core::stampColour(u"NotApproved"));
    // The AFM's advances, for the letters that differ from Helvetica's.
    CHECK(core::helveticaBoldWidth('A') == 722);
    CHECK(core::helveticaBoldWidth('J') == 556);
    CHECK(core::helveticaBoldWidth('a') == 0);
}

TEST_CASE("stamps: a dynamic stamp's lines are the author and the minute at the host's offset", "[stamps][A-76]")
{
    CHECK(core::dynamicStampLines(QStringLiteral("Ada"), kPlaced, 0) == QStringList{QStringLiteral("Ada"), QStringLiteral("2026-10-04 09:30")});
    CHECK(core::dynamicStampLines(QStringLiteral("Ada"), kPlaced, 8 * 3600).last() == QStringLiteral("2026-10-04 17:30"));
    // West of Greenwich across midnight, and across a year.
    CHECK(core::dynamicStampLines(QString(), 1798761600 /* 2027-01-01T00:00Z */, -5 * 3600)
          == QStringList{QStringLiteral("2026-12-31 19:00")});
    CHECK(core::dynamicStampLines(QStringLiteral("  "), 0, 0) == QStringList{QStringLiteral("1970-01-01 00:00")});
}

// ---- Images ---------------------------------------------------------------------------------------

TEST_CASE("stamps: a JPEG is read from its header and anything DCTDecode cannot read is refused", "[stamps][A-77]")
{
    const QByteArray jpeg = fileBytes("stamps/signature.jpg");
    auto image = core::stampImageFromJpeg(jpeg);
    REQUIRE(image);
    CHECK(image.value().encoding == core::StampImage::Encoding::Jpeg);
    CHECK(image.value().width == 160);
    CHECK(image.value().height == 60);
    CHECK(image.value().components == 3);
    CHECK(image.value().data == jpeg);   // the file itself
    CHECK_FALSE(core::invalidStampImage(image.value()));

    const auto refusedWith = [](const QByteArray& bytes) {
        auto r = core::stampImageFromJpeg(bytes);
        REQUIRE_FALSE(r);
        CHECK(keyOf(r.error()) == "error.edit.stampImageUnsupported");
        return r.error().context;
    };
    CHECK(refusedWith(fileBytes("stamps/seal-rgba.png")) == QStringLiteral("jpeg"));
    CHECK(refusedWith(jpeg.left(20)) == QStringLiteral("jpeg"));
    // A lossless frame (SOF3) in place of the baseline one: well-formed, and not what /DCTDecode reads.
    QByteArray lossless = jpeg;
    const qsizetype sof = lossless.indexOf(QByteArray("\xFF\xC0", 2));
    REQUIRE(sof > 0);
    lossless[sof + 1] = static_cast<char>(0xC3);
    CHECK(refusedWith(lossless) == QStringLiteral("coding"));
    // Four components (CMYK) and twelve bits.
    QByteArray cmyk = jpeg;
    cmyk[sof + 9] = 4;
    CHECK(refusedWith(cmyk) == QStringLiteral("components"));
    QByteArray twelve = jpeg;
    twelve[sof + 4] = 12;
    CHECK(refusedWith(twelve) == QStringLiteral("precision"));
}

TEST_CASE("stamps: pixels are held compressed and an opaque alpha is dropped", "[stamps][A-77]")
{
    const Pixels p;
    auto image = core::stampImageFromPixels(p.width, p.height, p.rgb, p.alpha);
    REQUIRE(image);
    CHECK(image.value().encoding == core::StampImage::Encoding::Flate);
    CHECK(inflated(image.value().data, p.rgb.size()) == p.rgb);
    CHECK(inflated(image.value().alpha, p.alpha.size()) == p.alpha);

    auto opaque = core::stampImageFromPixels(p.width, p.height, p.rgb, QByteArray(p.width * p.height, static_cast<char>(0xFF)));
    REQUIRE(opaque);
    CHECK(opaque.value().alpha.isEmpty());
    // The key is the pixels': the same pixels the same key, one sample changed another.
    CHECK(core::stampImageKey(opaque.value()) != core::stampImageKey(image.value()));
    CHECK(core::stampImageKey(image.value()) == core::stampImageKey(core::stampImageFromPixels(p.width, p.height, p.rgb, p.alpha).value()));

    CHECK_FALSE(core::stampImageFromPixels(p.width, p.height, p.rgb.left(10), p.alpha));
    CHECK_FALSE(core::stampImageFromPixels(5000, 5000, QByteArray(), QByteArray()));   // past the pixel bound
}

// ---- Geometry ---------------------------------------------------------------------------------------

TEST_CASE("stamps: a turn keeps the centre and the scale and its rectangle bounds the turned box", "[stamps][A-80]")
{
    const Annotation approved = standard("Approved", 0, PdfPoint{300, 400});
    auto natural = core::stampNaturalSize(approved);
    REQUIRE(natural);
    // Placed at its natural size, centred where it was asked.
    CHECK(near(approved.rect.size.width, natural.value().width));
    CHECK(near(approved.rect.left() + approved.rect.size.width / 2, 300));
    CHECK(near(approved.rect.bottom() + approved.rect.size.height / 2, 400));

    Annotation doubled = approved;
    doubled.rect = core::stampRect(PdfPoint{300, 400}, natural.value(), 2.0, 2.0, 0);
    auto quarter = core::rotatedStamp(doubled, 90);
    REQUIRE(quarter);
    CHECK(quarter.value().rotate == 90);
    CHECK(near(quarter.value().rect.size.width, 2 * natural.value().height));   // swapped, at the same scale
    CHECK(near(quarter.value().rect.size.height, 2 * natural.value().width));
    CHECK(near(quarter.value().rect.left() + quarter.value().rect.size.width / 2, 300));

    auto thirty = core::rotatedStamp(doubled, 30);
    REQUIRE(thirty);
    const double c = std::cos(std::numbers::pi / 6), s = std::sin(std::numbers::pi / 6);
    const double w = 2 * natural.value().width, h = 2 * natural.value().height;
    CHECK(near(thirty.value().rect.size.width, w * c + h * s));
    CHECK(near(thirty.value().rect.size.height, w * s + h * c));
    // And back: -330 is 30, and turning to 0 again gives the doubled box.
    CHECK(core::rotatedStamp(doubled, -330).value().rotate == 30);
    auto back = core::rotatedStamp(thirty.value(), 0);
    REQUIRE(back);
    CHECK(near(back.value().rect.size.width, w));
    CHECK(near(back.value().rect.size.height, h));

    // A box dragged out: the turned box fitted inside it, uniformly.
    auto fitted = core::stampFittedTo(quarter.value(), PdfRect{PdfPoint{0, 0}, PdfSize{100, 400}});
    REQUIRE(fitted);
    CHECK(near(fitted.value().rect.size.width, 100));
    CHECK(fitted.value().rect.size.height <= 400 + 1e-6);
}

TEST_CASE("stamps: a foreign stamp has no natural size and cannot be turned", "[stamps][D6-37]")
{
    Annotation foreign = core::newAnnotation(AnnotationSubtype::Stamp, 0, kAuthor, kPlaced);
    foreign.icon = QStringLiteral("SBApproved");
    foreign.rect = PdfRect{PdfPoint{10, 10}, PdfSize{100, 40}};
    CHECK(core::stampKind(foreign) == core::StampKind::Foreign);
    auto turned = core::rotatedStamp(foreign, 90);
    REQUIRE_FALSE(turned);
    CHECK(keyOf(turned.error()) == "error.edit.stampNotRedrawable");
    // An image stamp whose pixels the model does not hold -- one read from a file -- is foreign too.
    Annotation reopened = foreign;
    reopened.icon.clear();
    reopened.intent = core::stampImageIntent();
    CHECK(core::stampKind(reopened) == core::StampKind::Foreign);
    auto built = pdf::ops::AppearanceStreamBuilder::normal(reopened);
    REQUIRE_FALSE(built);
    CHECK(keyOf(built.error()) == "error.edit.stampNotRedrawable");
}

// ---- The file -------------------------------------------------------------------------------------

TEST_CASE("stamps: every standard stamp is saved with its Table 181 name and a form that resolves", "[stamps][A-75]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    std::vector<Annotation> made;
    int at = 0;
    for (const QLatin1String name : core::standardStampNames()) {
        made.push_back(standard(name.data(), at % 2, PdfPoint{150.0 + 250.0 * ((at / 2) % 2), 780.0 - 100.0 * (at / 4)}));
        ++at;
    }
    REQUIRE(add(*session, made));
    ReadBack back(save(*session, QStringLiteral("stamps-standard.pdf")));
    for (const Annotation& stamp : made) {
        INFO(stamp.icon.toStdString());
        const Annotation* read = back.withNm(stamp.nm);
        REQUIRE(read != nullptr);
        CHECK(read->subtype == AnnotationSubtype::Stamp);
        CHECK(core::isStandardStampName(read->icon));
        CHECK(read->icon == stamp.icon);
        CHECK(read->color == stamp.color);
        const CosObject form = back.appearanceOf(stamp.nm);
        CHECK(pdf::cos::ContentStreamBuilder::check(back.streamBytes(form)));
        CHECK(unresolved(back, form).empty());
        // Drawn at its natural size: the box is 0 0 W H, the rectangle the same size unturned.
        const std::vector<double> bbox = numbersOf(back, form.get("BBox"));
        REQUIRE(bbox.size() == 4);
        CHECK(near(bbox[2] - bbox[0], stamp.rect.size.width));
        CHECK_FALSE(form.has("Matrix"));
        // Its words, in Helvetica-Bold with the advances the layout used.
        const CosObject font = back.resolved(back.resolved(form.get("Resources")).get("Font").get("HeBo"));
        CHECK(font.get("BaseFont").text() == "Helvetica-Bold");
        CHECK(numbersOf(back, font.get("Widths")).size() == 59);
        CHECK(QString::fromLatin1(QByteArray::fromStdString(back.streamBytes(form)).toUpper()).contains(hexOf(core::stampTitle(stamp.icon))));
    }
}

TEST_CASE("stamps: a dynamic stamp is baked at placement and drawn from what was baked", "[stamps][A-76]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    auto made = core::newDynamicStamp(QStringLiteral("DynamicApproved"), 0, PdfPoint{300, 500}, kAuthor, kPlaced, 0);
    REQUIRE(made);
    CHECK(made.value().contents == kAuthor + QStringLiteral("\n2026-10-04 09:30"));
    REQUIRE(add(*session, {made.value()}));
    const QByteArray saved = save(*session, QStringLiteral("stamps-dynamic.pdf"));

    // Reopened: no clock is asked anything -- the reader has none -- and the baked lines are what is there.
    ReadBack back(saved);
    const Annotation* read = back.withNm(made.value().nm);
    REQUIRE(read != nullptr);
    CHECK(read->contents == made.value().contents);
    CHECK(read->icon == QStringLiteral("DynamicApproved"));
    const std::string drawn = QByteArray::fromStdString(back.streamBytes(back.appearanceOf(made.value().nm))).toUpper().toStdString();
    CHECK(drawn.find(hexOf(QStringLiteral("2026-10-04 09:30")).toStdString()) != std::string::npos);
    CHECK(drawn.find(hexOf(kAuthor).toStdString()) != std::string::npos);

    // Redrawn a quarter of a year later -- a turn, in a session opened from the saved file: the same lines.
    auto later = tests::openBuilt(QStringLiteral("dynamic.pdf"), std::string(saved.constData(), static_cast<std::size_t>(saved.size())));
    REQUIRE(later);
    const core::AnnotationStore& store = later->journal->document().annotations();
    const auto index = store.find(read->id);
    REQUIRE(index);
    auto turned = core::rotatedStamp(store.all()[*index], 90);
    REQUIRE(turned);
    REQUIRE(later->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{turned.value()})));
    ReadBack again(save(*later, QStringLiteral("stamps-dynamic-turned.pdf")));
    const std::string redrawn = QByteArray::fromStdString(again.streamBytes(again.resolved(again.object(read->id.object).get("AP").get("N")))).toUpper().toStdString();
    CHECK(redrawn.find(hexOf(QStringLiteral("2026-10-04 09:30")).toStdString()) != std::string::npos);
}

TEST_CASE("stamps: a JPEG is written byte for byte and pixels with their mask", "[stamps][A-77]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const QByteArray jpeg = fileBytes("stamps/signature.jpg");
    const Pixels p;
    auto signature = core::newImageStamp(core::stampImageFromJpeg(jpeg).value(), QStringLiteral("Signature"), 0, PdfPoint{200, 300}, kAuthor, kPlaced);
    auto seal = core::newImageStamp(core::stampImageFromPixels(p.width, p.height, p.rgb, p.alpha).value(), QString(), 0, PdfPoint{400, 300}, kAuthor, kPlaced);
    REQUIRE(signature);
    REQUIRE(seal);
    REQUIRE(add(*session, {signature.value(), seal.value()}));
    ReadBack back(save(*session, QStringLiteral("stamps-images.pdf")));

    const auto pictureOf = [&back](const QString& nm) {
        const CosObject form = back.appearanceOf(nm);
        CHECK(unresolved(back, form).empty());
        return back.resolved(back.resolved(form.get("Resources")).get("XObject").get("Im0"));
    };
    const Annotation* readSignature = back.withNm(signature.value().nm);
    REQUIRE(readSignature != nullptr);
    CHECK(readSignature->intent == QStringLiteral("StampImage"));
    CHECK(readSignature->icon.isEmpty());
    CHECK(readSignature->subject == QStringLiteral("Signature"));
    CHECK(readSignature->image.isEmpty());   // a reader never holds pixels: the stamp is foreign once reopened

    const CosObject jpegImage = pictureOf(signature.value().nm);
    CHECK(jpegImage.get("Subtype").text() == "Image");
    CHECK(jpegImage.get("Filter").text() == "DCTDecode");
    CHECK(jpegImage.get("Width").numberValue() == 160);
    CHECK(jpegImage.get("ColorSpace").text() == "DeviceRGB");
    CHECK_FALSE(jpegImage.has("SMask"));
    CHECK(back.streamBytes(jpegImage) == jpeg.toStdString());   // BYTE-IDENTICAL

    const CosObject pixels = pictureOf(seal.value().nm);
    CHECK(pixels.get("Filter").text() == "FlateDecode");
    CHECK(pixels.get("Width").numberValue() == p.width);
    CHECK(inflated(QByteArray::fromStdString(back.streamBytes(pixels)), p.rgb.size()) == p.rgb);
    const CosObject mask = back.resolved(pixels.get("SMask"));
    CHECK(mask.get("ColorSpace").text() == "DeviceGray");
    CHECK(inflated(QByteArray::fromStdString(back.streamBytes(mask)), p.alpha.size()) == p.alpha);
}

TEST_CASE("stamps: one image placed on two pages in one save is one image object", "[stamps][A-78]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const Pixels p;
    const core::StampImage image = core::stampImageFromPixels(p.width, p.height, p.rgb, p.alpha).value();
    auto first = core::newImageStamp(image, QString(), 0, PdfPoint{200, 300}, kAuthor, kPlaced);
    auto second = core::newImageStamp(image, QString(), 1, PdfPoint{200, 300}, kAuthor, kPlaced);
    REQUIRE(first);
    REQUIRE(second);
    // Two commands, as two clicks of the tool make them, and one save.
    REQUIRE(add(*session, {first.value()}));
    REQUIRE(add(*session, {second.value()}));
    const QByteArray saved = save(*session, QStringLiteral("stamps-shared.pdf"));
    ReadBack back(saved);

    const CosObject a = back.dictionaryOf(first.value().nm).get("AP").get("N");
    const CosObject b = back.dictionaryOf(second.value().nm).get("AP").get("N");
    REQUIRE(a.isReference());
    REQUIRE(b.isReference());
    CHECK(a.reference().number != b.reference().number);   // two appearances …
    const CosObject imA = back.resolved(back.resolved(back.resolved(a).get("Resources")).get("XObject")).get("Im0");
    const CosObject imB = back.resolved(back.resolved(back.resolved(b).get("Resources")).get("XObject")).get("Im0");
    REQUIRE(imA.isReference());
    CHECK(imA.reference().number == imB.reference().number);   // … one picture

    // And the whole update holds exactly two images: the picture and its mask.
    std::set<long long> images;
    for (const auto& [number, entry] : back.table->entries()) {
        if (number <= 0 || entry.type == pdf::cos::XrefEntryType::Free) continue;
        auto fetched = back.table->fetch(number);
        if (!fetched || !fetched.value().object.isStream()) continue;
        const CosObject* subtype = fetched.value().object.find("Subtype");
        if (subtype != nullptr && subtype->isName() && subtype->text() == "Image"
            && !back.streamBytes(fetched.value().object).empty())
            images.insert(number);
    }
    // text-a4.pdf holds no image of its own (its corpus row), so every image is the update's.
    CHECK(images.size() == 2);
}

TEST_CASE("stamps: a resize carries the appearance and a turn redraws it turned", "[stamps][A-80]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const Annotation approved = standard("Approved", 0, PdfPoint{300, 500});
    REQUIRE(add(*session, {approved}));
    ReadBack first(save(*session, QStringLiteral("stamps-resize-0.pdf")));
    const CosObject before = first.dictionaryOf(approved.nm).get("AP").get("N");
    REQUIRE(before.isReference());

    const core::AnnotationStore& store = session->journal->document().annotations();
    const auto index = store.find(approved.id);
    REQUIRE(index);
    Annotation bigger = store.all()[*index];
    bigger.rect = PdfRect{bigger.rect.origin, PdfSize{bigger.rect.size.width * 1.5, bigger.rect.size.height * 1.5}};
    REQUIRE(session->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{bigger})));
    ReadBack resized(save(*session, QStringLiteral("stamps-resize-1.pdf")));
    const CosObject carried = resized.dictionaryOf(approved.nm).get("AP").get("N");
    REQUIRE(carried.isReference());
    CHECK(carried.reference().number == before.reference().number);   // the same appearance, not redrawn

    auto turned = core::rotatedStamp(store.all()[*store.find(approved.id)], 30);
    REQUIRE(turned);
    REQUIRE(session->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{turned.value()})));
    ReadBack rotated(save(*session, QStringLiteral("stamps-turned.pdf")));
    const CosObject form = rotated.appearanceOf(approved.nm);
    const std::vector<double> matrix = numbersOf(rotated, form.get("Matrix"));
    REQUIRE(matrix.size() == 6);
    CHECK(near(matrix[0], std::cos(std::numbers::pi / 6)));
    CHECK(near(matrix[1], std::sin(std::numbers::pi / 6)));
    const Annotation* read = rotated.withNm(approved.nm);
    REQUIRE(read != nullptr);
    CHECK(read->rotate == 30);
    // The rectangle is the turned box's bounds at the 1.5 scale (A-80: the rectangle bounds the turned ink).
    const std::vector<double> bbox = numbersOf(rotated, form.get("BBox"));
    const double w = 1.5 * (bbox[2] - bbox[0]), h = 1.5 * (bbox[3] - bbox[1]);
    CHECK(near(read->rect.size.width, w * std::cos(std::numbers::pi / 6) + h * std::sin(std::numbers::pi / 6), 0.02));
    CHECK(near(read->rect.size.height, w * std::sin(std::numbers::pi / 6) + h * std::cos(std::numbers::pi / 6), 0.02));
    // One command each: the undo takes the turn back and leaves the resize.
    REQUIRE(session->journal->undo());
    CHECK(session->journal->document().annotations().all()[*store.find(approved.id)].rotate == 0);
}

TEST_CASE("stamps: a stamp RawPage cannot redraw moves and resizes and is not turned", "[stamps][D6-37]")
{
    // An image stamp, saved, and reopened: its pixels are in its appearance, not in the model.
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const Pixels p;
    auto seal = core::newImageStamp(core::stampImageFromPixels(p.width, p.height, p.rgb, p.alpha).value(), QString(), 0,
                                    PdfPoint{300, 300}, kAuthor, kPlaced);
    REQUIRE(seal);
    REQUIRE(add(*session, {seal.value()}));
    const QByteArray saved = save(*session, QStringLiteral("stamps-foreign-0.pdf"));

    auto reopened = tests::openBuilt(QStringLiteral("reopened.pdf"), std::string(saved.constData(), static_cast<std::size_t>(saved.size())));
    REQUIRE(reopened);
    const core::AnnotationStore& store = reopened->journal->document().annotations();
    const Annotation* found = nullptr;
    for (const Annotation& a : store.all())
        if (a.nm == seal.value().nm) found = &a;
    REQUIRE(found != nullptr);
    const Annotation original = *found;
    CHECK(core::stampKind(original) == core::StampKind::Foreign);

    Annotation resized = original;
    resized.rect = PdfRect{PdfPoint{100, 100}, PdfSize{original.rect.size.width * 2, original.rect.size.height * 2}};
    CHECK(reopened->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{resized})));
    CHECK(reopened->journal->run(std::make_unique<core::MoveAnnotationCommand>(std::vector<AnnotationId>{original.id}, 10.0, 5.0, kPlaced)));

    Annotation turned = store.all()[*store.find(original.id)];
    turned.rotate = 90;
    const core::Status refused = reopened->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{turned}));
    REQUIRE_FALSE(refused);
    CHECK(keyOf(refused.error()) == "error.edit.stampNotRedrawable");

    ReadBack back(save(*reopened, QStringLiteral("stamps-foreign-1.pdf")));
    const Annotation* read = back.withNm(seal.value().nm);
    REQUIRE(read != nullptr);
    CHECK(near(read->rect.left(), 110));
    CHECK(near(read->rect.size.width, original.rect.size.width * 2));
    // The picture is still the one written first.
    const CosObject form = back.appearanceOf(seal.value().nm);
    const CosObject picture = back.resolved(back.resolved(back.resolved(form.get("Resources")).get("XObject")).get("Im0"));
    CHECK(inflated(QByteArray::fromStdString(back.streamBytes(picture)), p.rgb.size()) == p.rgb);
}

TEST_CASE("stamps: the corpus's own Approved recoloured is redrawn by our builder and keeps every key it does not model",
          "[stamps][A-26]")
{
    auto session = tests::openCopy("ordinary/annots-every-type.pdf");
    REQUIRE(session);
    ReadBack before(session->openedBytes);
    std::vector<AnnotationId> ids;
    for (const Annotation& a : session->journal->document().annotations().all())
        if (a.subtype == AnnotationSubtype::Stamp) ids.push_back(a.id);
    REQUIRE(ids.size() == 1);   // the generator's /Approved, its own appearance
    core::AnnotationAppearance blue;
    blue.color = std::vector<double>{0, 0, 1};
    REQUIRE(session->journal->run(std::make_unique<core::SetAnnotationAppearanceCommand>(ids, blue, kPlaced)));
    ReadBack after(save(*session, QStringLiteral("stamps-corpus-recoloured.pdf")));
    const CosObject was = before.object(ids.front().object);
    const CosObject now = after.object(ids.front().object);
    REQUIRE(now.get("AP").get("N").isReference());
    CHECK(now.get("AP").get("N").reference().number != was.get("AP").get("N").reference().number);
    const CosObject form = after.resolved(now.get("AP").get("N"));
    CHECK(after.streamBytes(form).find("0 0 1 RG") != std::string::npos);
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

TEST_CASE("stamps: a dynamic stamp's lines edited are its lines drawn", "[stamps][A-76]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    auto made = core::newDynamicStamp(QStringLiteral("DynamicRevised"), 0, PdfPoint{300, 500}, kAuthor, kPlaced, 0);
    REQUIRE(made);
    REQUIRE(add(*session, {made.value()}));
    const QByteArray saved = save(*session, QStringLiteral("stamps-dynamic-edit-0.pdf"));
    // In a session opened from the saved file, so the stamp is the file's and the edit goes through the sink's carry rule.
    auto later = tests::openBuilt(QStringLiteral("dynamic-edit.pdf"), std::string(saved.constData(), static_cast<std::size_t>(saved.size())));
    REQUIRE(later);
    const core::AnnotationStore& store = later->journal->document().annotations();
    const Annotation* found = nullptr;
    for (const Annotation& a : store.all())
        if (a.nm == made.value().nm) found = &a;
    REQUIRE(found != nullptr);
    Annotation edited = *found;
    edited.contents = QStringLiteral("Second reviewer\n2026-10-05 10:00");
    REQUIRE(later->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{edited})));
    ReadBack back(save(*later, QStringLiteral("stamps-dynamic-edit-1.pdf")));
    const std::string drawn = QByteArray::fromStdString(back.streamBytes(back.appearanceOf(made.value().nm))).toUpper().toStdString();
    CHECK(drawn.find(hexOf(QStringLiteral("Second reviewer")).toStdString()) != std::string::npos);
    CHECK(drawn.find(hexOf(kAuthor).toStdString()) == std::string::npos);
}

// ---- The journal ----------------------------------------------------------------------------------

TEST_CASE("stamps: an image stamp travels in a journal line and hostile pixels are refused", "[stamps][A-29][serialize:AddAnnotationCommand]")
{
    auto made = core::newImageStamp(core::stampImageFromJpeg(fileBytes("stamps/signature.jpg")).value(), QStringLiteral("Sig"), 2,
                                    PdfPoint{100, 100}, kAuthor, kPlaced);
    REQUIRE(made);
    const AddAnnotationCommand command({made.value()});
    const QJsonObject json = command.serialize();
    auto back = core::CommandRegistry::deserialize(json);
    REQUIRE(back);
    CHECK(back.value()->serialize() == json);
    auto parsed = Annotation::fromJson(json.value(QStringLiteral("annotations")).toArray().first().toObject());
    REQUIRE(parsed);
    CHECK(parsed.value() == made.value());

    const auto refusedBy = [&json](const std::function<void(QJsonObject&)>& plant, const char* field) {
        QJsonObject hostile = json;
        QJsonArray list = hostile.value(QStringLiteral("annotations")).toArray();
        QJsonObject annotation = list.first().toObject();
        QJsonObject image = annotation.value(QStringLiteral("image")).toObject();
        plant(image);
        annotation.insert(QStringLiteral("image"), image);
        if (std::string(field) == "annotation.image") annotation.insert(QStringLiteral("subtype"), QStringLiteral("Square"));
        list.replace(0, annotation);
        hostile.insert(QStringLiteral("annotations"), list);
        INFO(field);
        auto r = core::CommandRegistry::deserialize(hostile);
        REQUIRE_FALSE(r);
        CHECK(keyOf(r.error()) == "error.command.malformed");
    };
    refusedBy([](QJsonObject&) {}, "annotation.image");                                                       // pixels on a Square
    refusedBy([](QJsonObject& i) { i.insert(QStringLiteral("width"), 161); }, "annotation.image.data");        // the header disagrees
    refusedBy([](QJsonObject& i) { i.insert(QStringLiteral("data"), QStringLiteral("not*base64")); }, "annotation image");
    refusedBy([](QJsonObject& i) { i.insert(QStringLiteral("encoding"), QStringLiteral("gif")); }, "annotation image");
}

TEST_CASE("stamps: recovery replays every kind of stamp to the same model and the same bytes", "[stamps][A-29][recovery]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = core::RecoveryJournal::open(root.path(), session->path, session->journal->document().id(),
                                                &session->durableFiles);
    REQUIRE(recovery->isRecoverable());
    session->journal->setRecorder(recovery.get());
    core::CommandJournal& journal = *session->journal;

    // A standard stamp, a dynamic one, a JPEG and pixels with their alpha, the standard one turned -- and one undone.
    const Pixels p;
    auto approved = core::newStandardStamp(QStringLiteral("Approved"), 0, PdfPoint{150, 700}, kAuthor, kPlaced);
    auto received = core::newDynamicStamp(QStringLiteral("DynamicReceived"), 0, PdfPoint{400, 700}, kAuthor, kPlaced, 7200);
    auto signature = core::newImageStamp(core::stampImageFromJpeg(fileBytes("stamps/signature.jpg")).value(), QStringLiteral("Sig"), 1,
                                         PdfPoint{200, 300}, kAuthor, kPlaced);
    auto pixels = core::newImageStamp(core::stampImageFromPixels(p.width, p.height, p.rgb, p.alpha).value(), QString(), 1,
                                      PdfPoint{400, 300}, kAuthor, kPlaced);
    REQUIRE(approved);
    REQUIRE(received);
    REQUIRE(signature);
    REQUIRE(pixels);
    REQUIRE(add(*session, {approved.value(), received.value()}));
    REQUIRE(add(*session, {signature.value()}));
    REQUIRE(add(*session, {pixels.value()}));
    auto turned = core::rotatedStamp(approved.value(), 45);
    REQUIRE(turned);
    REQUIRE(journal.run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{turned.value()})));
    REQUIRE(journal.undo());   // the pixels' stamp's add stays; the turn goes

    const QByteArray expectedModel = journal.document().modelHash();
    auto expected = pdf::ops::RevisionWriter::compose(*session->sink(), false);
    REQUIRE(expected);
    recovery.reset();
    journal.setRecorder(nullptr);

    const auto candidates = core::RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    auto fresh = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(fresh);
    auto report = core::RecoveryJournal::replay(candidates.front().directory, *fresh->journal);
    REQUIRE(report);
    CHECK_FALSE(report.value().stoppedAtStep.has_value());
    CHECK(fresh->journal->document().modelHash() == expectedModel);
    CHECK(fresh->journal->canRedo());   // the undone turn, redoable after the replay
    auto bytes = pdf::ops::RevisionWriter::compose(*fresh->sink(), false);
    REQUIRE(bytes);
    CHECK(tests::sha256(bytes.value().bytes) == tests::sha256(expected.value().bytes));
}

// ---- A page (Stage 7 T13, `Stage7-Assembly-Plan.md` D7-36; Stage 7 guide A-89's and A-90's file halves) -------------

namespace {

// Page `page` of a corpus file as a stamp's page: copied as an insert copies it, then read as the stamp draws it.
core::StampPage pageStampOf(const char* relative, int page)
{
    const QByteArray bytes = fileBytes(relative);
    auto copied = pdf::ops::ObjectGraphCopier::copyFile(viewOf(bytes), {page});
    REQUIRE(copied);
    auto made = pdf::ops::PageStamp::fromPayload(copied.value().graph);
    INFO(relative << " page " << page + 1 << ": "
                  << (made ? std::string() : keyOf(made.error()) + " " + made.error().context.toStdString()));
    REQUIRE(made);
    return made.value();
}

std::pair<double, double> applied(const std::array<double, 6>& m, double x, double y)
{
    return {m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]};
}

// Every reference any collected object holds names a collected object: the form's graph is closed.
std::vector<long long> dangling(const pdf::ops::CollectedAppearanceObjects& objects)
{
    std::set<long long> held;
    for (const auto& entry : objects.entries()) held.insert(entry.at.number);
    std::vector<long long> out;
    std::function<void(const CosObject&)> walk = [&](const CosObject& value) {
        if (value.isReference()) {
            if (held.count(value.reference().number) == 0) out.push_back(value.reference().number);
        } else if (value.isArray()) {
            for (const CosObject& item : value.items()) walk(item);
        } else if (value.isDictionary() || value.isStream()) {
            for (const CosObject::Entry& entry : value.entries()) walk(entry.value);
        }
    };
    for (const auto& entry : objects.entries()) walk(entry.object);
    return out;
}

const pdf::ops::CollectedAppearanceObjects::Entry* formEntry(const pdf::ops::CollectedAppearanceObjects& objects)
{
    const pdf::ops::CollectedAppearanceObjects::Entry* found = nullptr;
    for (const auto& entry : objects.entries()) {
        if (entry.key.size() > 5 && entry.key.compare(entry.key.size() - 5, 5, ":form") == 0) {
            if (found != nullptr) return nullptr;   // two forms for one page
            found = &entry;
        }
    }
    return found;
}

} // namespace

TEST_CASE("stamps.page_source: a page is read for the size it shows and anything that is not one page is refused",
          "[stamps][A-89][D7-36]")
{
    // refs-every-kind.pdf's page 3, A4: the size it shows.
    const core::StampPage three = pageStampOf("assembly/refs-every-kind.pdf", 2);
    CHECK(near(three.width, 595.276));
    CHECK(near(three.height, 841.89));
    // rotated-pages.pdf: one content at 0 and 90 degrees -- a turned page shows its sides the other way.
    const core::StampPage upright = pageStampOf("ordinary/rotated-pages.pdf", 0);
    const core::StampPage turned = pageStampOf("ordinary/rotated-pages.pdf", 1);
    CHECK(near(turned.width, upright.height));
    CHECK(near(turned.height, upright.width));
    // rotate-inherited.pdf: page 1 inherits `/Rotate 90` from the root node and page 2 states `/Rotate 0` -- the copy
    // made the inherited turn the page's own, so it is read.
    const core::StampPage inherited = pageStampOf("ordinary/rotate-inherited.pdf", 0);
    const core::StampPage stated = pageStampOf("ordinary/rotate-inherited.pdf", 1);
    CHECK(near(inherited.width, stated.height));
    CHECK(near(inherited.height, stated.width));
    // boxes.pdf: a CropBox inside the MediaBox is what shows, and one past it is cut to it.
    const core::StampPage cropped = pageStampOf("assembly/boxes.pdf", 0);
    CHECK(near(cropped.width, 576.0));
    CHECK(near(cropped.height, 756.0));
    const core::StampPage clipped = pageStampOf("assembly/boxes.pdf", 1);
    CHECK(near(clipped.width, 400.0));
    CHECK(near(clipped.height, 841.89 - 200.0));

    // Sized as an image is (the user's choice, 2026-11-13): the longer side 216 points ...
    auto placed = core::newPageStamp(three, QStringLiteral("Three"), 0, PdfPoint{300, 400}, kAuthor, kPlaced);
    REQUIRE(placed);
    CHECK(core::stampKind(placed.value()) == core::StampKind::Page);
    CHECK(placed.value().icon.isEmpty());
    CHECK(placed.value().intent.isEmpty());
    CHECK(near(placed.value().rect.size.height, core::kImageStampMaxSide));
    CHECK(near(placed.value().rect.size.width, core::kImageStampMaxSide * 595.276 / 841.89));
    // ... and a small page scaled UP to 72 -- it is a vector.
    auto blank = pdf::ops::ObjectGraphCopier::blankPages(PdfSize{20.0, 10.0}, 1);
    REQUIRE(blank);
    auto small = pdf::ops::PageStamp::fromPayload(blank.value().graph);
    REQUIRE(small);
    auto tiny = core::newPageStamp(small.value(), QString(), 0, PdfPoint{100, 100}, kAuthor, kPlaced);
    REQUIRE(tiny);
    CHECK(near(tiny.value().rect.size.width, core::kImageStampMinSide));
    CHECK(near(tiny.value().rect.size.height, core::kImageStampMinSide / 2.0));

    // Refused: two pages, bytes that are no PDF, a page too large for a page -- each by its key, naming what it met.
    const QByteArray refs = fileBytes("assembly/refs-every-kind.pdf");
    auto two = pdf::ops::ObjectGraphCopier::copyFile(viewOf(refs), {0, 1});
    REQUIRE(two);
    auto refusedTwo = pdf::ops::PageStamp::fromPayload(two.value().graph);
    REQUIRE_FALSE(refusedTwo);
    CHECK(keyOf(refusedTwo.error()) == "error.edit.stampPageUnsupported");
    CHECK(refusedTwo.error().context == QStringLiteral("pages:2"));
    auto noPdf = pdf::ops::PageStamp::fromPayload(QByteArray("%PDF-1.7 and nothing after it"));
    REQUIRE_FALSE(noPdf);
    CHECK(keyOf(noPdf.error()) == "error.edit.stampPageUnsupported");
    CHECK(core::invalidStampPage(core::StampPage{three.graph, 2.0, 100.0}) == QStringLiteral("stampPage.size"));
    CHECK(core::invalidStampPage(core::StampPage{QByteArray("not a pdf"), 100.0, 100.0}) == QStringLiteral("stampPage.graph"));
    CHECK_FALSE(core::newPageStamp(core::StampPage{three.graph, 100.0, 1e6}, QString(), 0, PdfPoint{}, kAuthor, kPlaced));
}

TEST_CASE("stamps.page_source: the page as shown is mapped onto the stamp's box at every turn", "[stamps][A-89][D7-36]")
{
    // A box 100 wide and 200 tall, drawn into a natural box twice the size it shows: each corner of the page lands on
    // the corner a reader shows it at, `/Rotate` turning it CLOCKWISE.
    const core::PdfRect box{PdfPoint{10.0, 20.0}, PdfSize{100.0, 200.0}};
    struct Corner {
        double x, y;     // on the page
        double sx, sy;   // where it shows, as a fraction of the natural box
    };
    struct Turn {
        int rotate;
        std::vector<Corner> corners;
    };
    const double x0 = 10.0, y0 = 20.0, x1 = 110.0, y1 = 220.0;
    const std::vector<Turn> turns{
        {0, {{x0, y0, 0, 0}, {x1, y1, 1, 1}, {x0, y1, 0, 1}, {x1, y0, 1, 0}}},
        {90, {{x0, y1, 1, 1}, {x0, y0, 0, 1}, {x1, y0, 0, 0}, {x1, y1, 1, 0}}},
        {180, {{x0, y0, 1, 1}, {x1, y1, 0, 0}, {x0, y1, 1, 0}, {x1, y0, 0, 1}}},
        {270, {{x0, y0, 1, 0}, {x1, y1, 0, 1}, {x0, y1, 0, 0}, {x1, y0, 1, 1}}},
    };
    for (const Turn& turn : turns) {
        const bool sideways = turn.rotate == 90 || turn.rotate == 270;
        const double width = 2.0 * (sideways ? 200.0 : 100.0);
        const double height = 2.0 * (sideways ? 100.0 : 200.0);
        const std::array<double, 6> m = pdf::ops::PageStamp::showMatrix(box, turn.rotate, width, height);
        for (const Corner& c : turn.corners) {
            INFO("rotate " << turn.rotate << ", page corner " << c.x << "," << c.y);
            const auto [x, y] = applied(m, c.x, c.y);
            CHECK(near(x, c.sx * width, 1e-9));
            CHECK(near(y, c.sy * height, 1e-9));
        }
    }
}

TEST_CASE("stamps.page_source: the form is the page's own content and resources, a closed graph, one stream however written",
          "[stamps][A-89][D7-36]")
{
    // decorated-elsewhere.pdf: pages whose `/Contents` is an ARRAY (another producer's header and watermark beside the
    // page's own); image-lzw-ascii85.pdf: content behind filters this writer only passes through.
    int arrays = 0;
    for (const char* file : {"assembly/decorated-elsewhere.pdf", "ordinary/image-lzw-ascii85.pdf", "assembly/refs-every-kind.pdf"}) {
        const QByteArray bytes = fileBytes(file);
        auto count = pdf::ops::ObjectGraphCopier::countFile(viewOf(bytes));
        REQUIRE(count);
        for (int page = 0; page < count.value(); ++page) {
            INFO(file << " page " << page + 1);
            const core::StampPage source = pageStampOf(file, page);
            pdf::ops::CollectedAppearanceObjects objects;
            auto form = pdf::ops::PageStamp::form(source, objects);
            REQUIRE(form);
            const auto* entry = formEntry(objects);
            REQUIRE(entry != nullptr);
            CHECK(entry->at == form.value().at);
            CHECK(dangling(objects).empty());
            for (const auto& e : objects.entries()) CHECK(e.written);
            const CosObject& dictionary = entry->object;
            CHECK(dictionary.get("Subtype").text() == "Form");
            CHECK(dictionary.get("Resources").isDictionary());
            const std::vector<double> bbox = [&] {
                std::vector<double> out;
                for (const CosObject& n : dictionary.get("BBox").items()) out.push_back(n.numberValue());
                return out;
            }();
            REQUIRE(bbox.size() == 4);
            CHECK(near(bbox[2] - bbox[0], form.value().box.size.width));
            CHECK(near(bbox[3] - bbox[1], form.value().box.size.height));

            // What the page's content decodes to, from the payload: one stream, or several joined by a line break.
            auto table = pdf::cos::XrefTable::load(viewOf(source.graph));
            REQUIRE(table);
            auto walk = pdf::ops::PageOps::walk(table.value());
            REQUIRE(walk);
            const CosObject page0 = table.value().fetch(walk.value().pages.front().number).value().object;
            CosObject contents = page0.get("Contents");
            if (contents.isReference()) contents = table.value().fetch(contents.reference().number).value().object;
            std::string want;
            if (contents.isArray()) {
                ++arrays;
                for (const CosObject& item : contents.items()) {
                    const CosObject stream = table.value().fetch(item.reference().number).value().object;
                    auto decoded = pdf::cos::decodeStream(stream, table.value().source());
                    REQUIRE(decoded);
                    if (!want.empty()) want += '\n';
                    want += std::string(decoded.value().begin(), decoded.value().end());
                }
                CHECK(dictionary.get("Filter").text() == "FlateDecode");   // several, written again as one
            } else {
                auto decoded = pdf::cos::decodeStream(contents, table.value().source());
                REQUIRE(decoded);
                want = std::string(decoded.value().begin(), decoded.value().end());
                CHECK(entry->data == std::string(contents.rawStreamData(table.value().source())));   // ONE: its own bytes
            }
            // The form's own bytes, decoded through its own filters, are the page's content.
            if (!dictionary.find("Filter")) {
                CHECK(*entry->data == want);
            } else if (dictionary.get("Filter").isName() && dictionary.get("Filter").text() == "FlateDecode" && !dictionary.find("DecodeParms")) {
                auto inflated = pdf::cos::filters::flateDecode(*entry->data);
                REQUIRE(inflated);
                CHECK(std::string(inflated.value().begin(), inflated.value().end()) == want);
            }
        }
    }
    CHECK(arrays >= 1);   // the array path met at least once -- else this case proves only the single-stream one
}

TEST_CASE("stamps.page_source_shared: one page placed twice in one save is one graph, read back foreign", "[stamps][A-90][D7-36]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const core::StampPage three = pageStampOf("assembly/refs-every-kind.pdf", 2);
    auto first = core::newPageStamp(three, QStringLiteral("P-0003"), 0, PdfPoint{200, 300}, kAuthor, kPlaced);
    auto second = core::newPageStamp(three, QStringLiteral("P-0003"), 1, PdfPoint{300, 500}, kAuthor, kPlaced);
    REQUIRE(first);
    REQUIRE(second);
    auto turned = core::rotatedStamp(second.value(), 30);   // turned: a redraw, its form the same page
    REQUIRE(turned);
    REQUIRE(add(*session, {first.value()}));
    REQUIRE(add(*session, {turned.value()}));
    const QByteArray saved = save(*session, QStringLiteral("stamps-page-shared.pdf"));
    ReadBack back(saved);

    const CosObject apA = back.appearanceOf(first.value().nm);
    const CosObject apB = back.appearanceOf(second.value().nm);
    const CosObject pgA = back.resolved(back.resolved(apA.get("Resources")).get("XObject")).get("Pg0");
    const CosObject pgB = back.resolved(back.resolved(apB.get("Resources")).get("XObject")).get("Pg0");
    REQUIRE(pgA.isReference());
    REQUIRE(pgB.isReference());
    CHECK(pgA.reference().number == pgB.reference().number);   // one page, two placements
    CHECK(unresolved(back, apA).empty());
    CHECK(unresolved(back, apB).empty());
    CHECK(back.resolved(apB.get("Matrix")).isArray());   // the turn is the placement's, not the page's

    // COUNTED: the saved file holds one form of the page and exactly the fonts the source page uses beside the
    // document's own -- the graph written once, not once per placement.
    const auto countFonts = [](pdf::cos::XrefTable& table) {
        int n = 0;
        for (const auto& [number, entry] : table.entries()) {
            if (number <= 0 || entry.type == pdf::cos::XrefEntryType::Free) continue;
            auto fetched = table.fetch(number);
            if (!fetched) continue;
            const CosObject* type = fetched.value().object.find("Type");
            if (type != nullptr && type->isName() && type->text() == "Font") ++n;
        }
        return n;
    };
    const QByteArray original = fileBytes("ordinary/text-a4.pdf");
    auto originalTable = pdf::cos::XrefTable::load(viewOf(original));
    REQUIRE(originalTable);
    auto pageTable = pdf::cos::XrefTable::load(viewOf(three.graph));
    REQUIRE(pageTable);
    pdf::ops::CollectedAppearanceObjects pageObjects;
    REQUIRE(pdf::ops::PageStamp::form(three, pageObjects));
    int pageFonts = 0;
    for (const auto& entry : pageObjects.entries()) {
        const CosObject* type = entry.object.find("Type");
        if (type != nullptr && type->isName() && type->text() == "Font") ++pageFonts;
    }
    REQUIRE(pageFonts >= 1);   // P-0003 is set in Helvetica-Bold: the page reaches a font
    CHECK(countFonts(*back.table) == countFonts(originalTable.value()) + pageFonts);
    int forms = 0;
    for (const auto& [number, entry] : back.table->entries()) {
        if (number <= 0 || entry.type == pdf::cos::XrefEntryType::Free) continue;
        auto fetched = back.table->fetch(number);
        if (!fetched || !fetched.value().object.isStream()) continue;
        // Each placement's own appearance has the page's box too (D7-36(d)): it is not the page.
        if (number == back.dictionaryOf(first.value().nm).get("AP").get("N").reference().number
            || number == back.dictionaryOf(second.value().nm).get("AP").get("N").reference().number)
            continue;
        const std::vector<double> bbox = numbersOf(back, fetched.value().object.get("BBox"));
        if (bbox.size() == 4 && near(bbox[3] - bbox[1], 841.89) && near(bbox[2] - bbox[0], 595.276)) ++forms;
    }
    CHECK(forms == 1);
    std::cout << "A-90's file half: two placements of page 3, one form object " << pgA.reference().number << ", " << pageFonts
              << " font(s) of the page's written once\n";

    // READ BACK FOREIGN, as an image stamp is: the model holds no page for it -- it moves and resizes, and no turn.
    const Annotation* readA = back.withNm(first.value().nm);
    REQUIRE(readA != nullptr);
    CHECK(readA->stampPage.isEmpty());
    CHECK(core::stampKind(*readA) == core::StampKind::Foreign);
    CHECK_FALSE(core::rotatedStamp(*readA, 90));
}

TEST_CASE("stamps.page_source: a page stamp travels in a journal line and a hostile page is refused",
          "[stamps][A-89][D7-36][serialize:AddAnnotationCommand]")
{
    const core::StampPage three = pageStampOf("assembly/refs-every-kind.pdf", 2);
    auto made = core::newPageStamp(three, QStringLiteral("P-0003"), 1, PdfPoint{100, 100}, kAuthor, kPlaced);
    REQUIRE(made);
    const AddAnnotationCommand command({made.value()});
    const QJsonObject json = command.serialize();
    auto back = core::CommandRegistry::deserialize(json);
    REQUIRE(back);
    CHECK(back.value()->serialize() == json);
    auto parsed = Annotation::fromJson(json.value(QStringLiteral("annotations")).toArray().first().toObject());
    REQUIRE(parsed);
    // Compared as JSON: a rectangle travels as its corners, so a width of 152.73… comes back as `right - left`, a
    // rounding away from the double it was -- every annotation's, not the page's.
    CHECK(parsed.value().toJson() == made.value().toJson());
    CHECK(parsed.value().stampPage == three);

    const auto refusedBy = [&json](const std::function<void(QJsonObject& annotation, QJsonObject& page)>& plant, const char* why) {
        QJsonObject hostile = json;
        QJsonArray list = hostile.value(QStringLiteral("annotations")).toArray();
        QJsonObject annotation = list.first().toObject();
        QJsonObject page = annotation.value(QStringLiteral("stampPage")).toObject();
        plant(annotation, page);
        annotation.insert(QStringLiteral("stampPage"), page);
        list.replace(0, annotation);
        hostile.insert(QStringLiteral("annotations"), list);
        INFO(why);
        auto r = core::CommandRegistry::deserialize(hostile);
        REQUIRE_FALSE(r);
        CHECK(keyOf(r.error()) == "error.command.malformed");
    };
    refusedBy([](QJsonObject& a, QJsonObject&) { a.insert(QStringLiteral("subtype"), QStringLiteral("Square")); }, "a page on a Square");
    refusedBy([](QJsonObject& a, QJsonObject&) { a.insert(QStringLiteral("icon"), QStringLiteral("Approved")); }, "a page and a /Name");
    refusedBy([](QJsonObject&, QJsonObject& p) { p.insert(QStringLiteral("graph"), QStringLiteral("not*base64")); }, "bytes that are not base64");
    refusedBy([](QJsonObject&, QJsonObject& p) { p.insert(QStringLiteral("graph"), QString::fromLatin1(QByteArray("no pdf").toBase64())); },
              "bytes that are no PDF");
    refusedBy([](QJsonObject&, QJsonObject& p) { p.insert(QStringLiteral("width"), 1e9); }, "a page wider than a page can be");
    refusedBy([](QJsonObject&, QJsonObject& p) { p.remove(QStringLiteral("height")); }, "no height");
}

TEST_CASE("stamps.page_source: recovery replays a page stamp, turned and undone, to the same model and bytes", "[stamps][A-89][recovery]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = core::RecoveryJournal::open(root.path(), session->path, session->journal->document().id(),
                                                &session->durableFiles);
    REQUIRE(recovery->isRecoverable());
    session->journal->setRecorder(recovery.get());
    core::CommandJournal& journal = *session->journal;

    const core::StampPage three = pageStampOf("assembly/refs-every-kind.pdf", 2);
    const core::StampPage turned = pageStampOf("ordinary/rotated-pages.pdf", 1);
    auto a = core::newPageStamp(three, QStringLiteral("P-0003"), 0, PdfPoint{200, 300}, kAuthor, kPlaced);
    auto b = core::newPageStamp(turned, QStringLiteral("Turned"), 1, PdfPoint{300, 300}, kAuthor, kPlaced);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(add(*session, {a.value()}));
    REQUIRE(add(*session, {b.value()}));
    auto again = core::rotatedStamp(a.value(), 90);
    REQUIRE(again);
    REQUIRE(journal.run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{again.value()})));
    REQUIRE(journal.undo());

    const QByteArray expectedModel = journal.document().modelHash();
    auto expected = pdf::ops::RevisionWriter::compose(*session->sink(), false);
    REQUIRE(expected);
    recovery.reset();
    journal.setRecorder(nullptr);

    const auto candidates = core::RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    std::cout << "a page stamp's journal: the graph " << three.graph.size() << " bytes, "
              << QDir(candidates.front().directory).entryList(QDir::Files).size() << " file(s) in the recovery directory\n";
    auto fresh = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(fresh);
    auto report = core::RecoveryJournal::replay(candidates.front().directory, *fresh->journal);
    REQUIRE(report);
    CHECK_FALSE(report.value().stoppedAtStep.has_value());
    CHECK(fresh->journal->document().modelHash() == expectedModel);
    CHECK(fresh->journal->canRedo());
    auto bytes = pdf::ops::RevisionWriter::compose(*fresh->sink(), false);
    REQUIRE(bytes);
    CHECK(tests::sha256(bytes.value().bytes) == tests::sha256(expected.value().bytes));
}
