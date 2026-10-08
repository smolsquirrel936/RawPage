// tst_appearance -- guide case A-65 (`Stage6-Annotations-Testing-Guide.md` §5.11, §3.3's second oracle), Stage 6 T8,
// decision D6-6: OUR appearance against the one PDFium GENERATES for the same dictionary with no `/AP`.
//
// PDFium 153 generates an appearance at load for eight of the kinds RawPage writes -- highlight, underline,
// strikeout, squiggly, square, circle, ink and note (a line it draws as nothing, guide I-02) -- so the same
// annotation is written twice by `tests/golden/AppearanceCases`' `appearancePdf`: once with our form, once with its
// dictionary alone. Both pages are drawn at 144 dpi, each pixel's INK is how far it moved from the page drawn
// without annotations, as a fraction of how far the annotation's colour is from it -- binarized at one half -- and
// the two inks must overlap at **IoU >= 0.90**.
//
// **A strikeout and a squiggly are NOT graded here** (D6-32, guide I-09): A-50 measured PDFium's placement of both
// off the glyphs, and ours is drawn on them on purpose -- they are graded by A-50's measure against the glyphs'
// ink (`tests/composition/tst_markupink.cpp`, `[A-50]`). This case draws them all the same, says their IoU, and
// requires only that PDFium generated something: a control whose generation drew nothing would make every IoU
// against it meaningless (the reason I-02 removed the line).
//
// A NOTE is graded as the `Comment` bubble RawPage writes: PDFium generates its own bubble for EVERY note, whatever
// `/Name` says, in a fixed yellow -- so the ink measure compares shapes, each in its own colour.
//
// WHAT IT DOES NOT COVER (convention 19): any reader but PDFium -- Chrome and Edge ARE PDFium, so they agree by
// construction; Acrobat and Firefox are M-02's -- and the kinds PDFium does not generate (a line, a polygon, a
// polyline, free text), whose only oracles are the golden suite (A-64) and M-02.

#include <catch2/catch_test_macros.hpp>

#include "core/backend.h"
#include "core/doc/DrawnShapes.h"
#include "core/render/RenderRequest.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/render/FontResolverBridge.h"
#include "platform/win/WinFontResolver.h"
#include "tests/golden/AppearanceCases.h"

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using core::Annotation;
using core::AnnotationSubtype;
using core::PdfPoint;
using core::PdfRect;
using core::PdfSize;

namespace {

struct FontBridge {
    FontBridge() { REQUIRE(pdf::render::FontResolverBridge::install(resolver)); }
    ~FontBridge() { pdf::render::FontResolverBridge::uninstall(); }
    platform::win::WinFontResolver resolver;
};

constexpr double kScale = 2.0;   // 144 dpi

Annotation fixed(AnnotationSubtype subtype, const char* name)
{
    Annotation a = core::newAnnotation(subtype, 0, QStringLiteral("Control"), 1791763200);
    a.nm = QString::fromLatin1(QByteArray(name).toHex()).leftJustified(32, QLatin1Char('0')).left(32);
    a.id = core::AnnotationId::ofCreated(a.nm);
    return a;
}

struct Drawn {
    core::RasterImage with;
    core::RasterImage without;
};

core::RasterImage render(pdf::DocumentBackendImpl& backend, core::DocumentHandle handle, bool annotations)
{
    const core::PdfSize size = backend.pages(handle)->at(0)->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = 0;
    request.deviceScale = kScale;
    request.revision = core::Revision::initial();
    request.flags = annotations ? core::render::RenderFlags::Annotations : core::render::RenderFlags::None;
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(size.width * kScale)),
                                          static_cast<int>(std::ceil(size.height * kScale))};
    auto image = backend.rasterizer(handle)->raster(request);
    REQUIRE(image);
    return image.value();
}

// The page with `a` drawn -- our appearance, or PDFium's generated one -- and the page without annotations.
Drawn draw(const Annotation& a, bool ours, const char* name)
{
    const std::string bytes = golden::appearancePdf({a}, 0, ours);
    REQUIRE_FALSE(bytes.empty());
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = directory.filePath(QString::fromLatin1(name) + (ours ? QStringLiteral("-ours.pdf") : QStringLiteral("-pdfium.pdf")));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()));
    file.close();
    pdf::DocumentBackendImpl backend;
    auto handle = backend.open(path, nullptr);
    REQUIRE(handle);
    Drawn out{render(backend, handle.value(), true), render(backend, handle.value(), false)};
    backend.close(handle.value());
    return out;
}

int channel(const core::RasterImage& image, int x, int y, int c)
{
    // BGRA: c 0 blue, 1 green, 2 red.
    return static_cast<unsigned char>(image.pixels[y * image.bytesPerLine + x * 4 + c]);
}

// The annotation's INK: each pixel moved from the page without it by at least half as far as its colour is from
// that page -- the largest channel's move against the largest channel's distance.
std::vector<bool> inkOf(const Drawn& drawn, const std::vector<double>& colour)
{
    std::vector<bool> ink(static_cast<std::size_t>(drawn.with.width) * drawn.with.height, false);
    const int target[3] = {static_cast<int>(std::lround(colour[2] * 255)), static_cast<int>(std::lround(colour[1] * 255)),
                           static_cast<int>(std::lround(colour[0] * 255))};
    for (int y = 0; y < drawn.with.height; ++y) {
        for (int x = 0; x < drawn.with.width; ++x) {
            int moved = 0;
            int distance = 0;
            for (int c = 0; c < 3; ++c) {
                const int base = channel(drawn.without, x, y, c);
                moved = std::max(moved, std::abs(channel(drawn.with, x, y, c) - base));
                distance = std::max(distance, std::abs(target[c] - base));
            }
            if (distance > 0 && moved * 2 >= distance) ink[static_cast<std::size_t>(y) * drawn.with.width + x] = true;
        }
    }
    return ink;
}

struct Overlap {
    long long ours = 0, theirs = 0, both = 0;
    double iou() const
    {
        const long long either = ours + theirs - both;
        return either > 0 ? static_cast<double>(both) / static_cast<double>(either) : 0.0;
    }
};

Overlap overlap(const Annotation& a, const char* name)
{
    const std::vector<double> colour = a.color.size() == 3 ? a.color : std::vector<double>{0, 0, 0};
    const std::vector<bool> ours = inkOf(draw(a, true, name), colour);
    const std::vector<bool> theirs = inkOf(draw(a, false, name), colour);
    Overlap o;
    for (std::size_t i = 0; i < ours.size(); ++i) {
        o.ours += ours[i];
        o.theirs += theirs[i];
        o.both += ours[i] && theirs[i];
    }
    std::cout << "A-65 " << name << ": ours " << o.ours << " px, PDFium's generated " << o.theirs << " px, both " << o.both
              << ", IoU " << o.iou() << "\n";
    return o;
}

Annotation markup(AnnotationSubtype kind, const char* name, std::vector<double> colour)
{
    Annotation a = fixed(kind, name);
    // Over white, between the page's band and its bar -- and 12 points tall, BODY TEXT: an underline is PDFium's
    // 1-point line up to text 14 points tall and grows with the text past it (`markupStroke`), so a taller quad is
    // where the two differ on purpose, not where the control grades them.
    a.quadPoints = {30, 76, 170, 76, 30, 64, 170, 64};
    a.rect = *core::markupRect(a.quadPoints);
    a.color = std::move(colour);
    return a;
}

} // namespace

TEST_CASE("our appearance overlaps the one PDFium generates for the same annotation", "[appearance][A-65][D6-6]")
{
    FontBridge bridge;
    std::vector<std::pair<Annotation, const char*>> graded;

    graded.emplace_back(markup(AnnotationSubtype::Highlight, "highlight", {1.0, 1.0, 0.0}), "highlight");
    graded.emplace_back(markup(AnnotationSubtype::Underline, "underline", {0.85, 0.1, 0.1}), "underline");

    Annotation square = fixed(AnnotationSubtype::Square, "square");
    square.rect = PdfRect{PdfPoint{30, 58}, PdfSize{140, 36}};
    square.color = {0.1, 0.2, 0.85};
    square.interiorColor = {0.1, 0.2, 0.85};
    square.borderWidth = 2.0;
    graded.emplace_back(square, "square");

    Annotation circle = fixed(AnnotationSubtype::Circle, "circle");
    circle.rect = PdfRect{PdfPoint{40, 56}, PdfSize{120, 40}};
    circle.color = {0.1, 0.6, 0.2};
    circle.interiorColor = {0.1, 0.6, 0.2};
    circle.borderWidth = 2.0;
    graded.emplace_back(circle, "circle");

    // Ink, densely sampled as a hand draws it -- where the curve through the samples and PDFium's straight segments
    // between them are the same stroke.
    Annotation ink = fixed(AnnotationSubtype::Ink, "ink");
    std::vector<double> arc;
    for (int degree = 0; degree <= 180; degree += 3) {
        const double t = degree * 3.14159265358979 / 180.0;
        arc.push_back(100 - 60 * std::cos(t));
        arc.push_back(62 + 26 * std::sin(t));
    }
    ink.inkList = {arc};
    ink.color = {0.85, 0.1, 0.1};
    ink.borderWidth = 4.0;
    ink = *core::fittedShape(ink);
    graded.emplace_back(ink, "ink");

    Annotation note = fixed(AnnotationSubtype::Text, "note");
    note.rect = PdfRect{PdfPoint{90, 62}, PdfSize{20, 20}};
    // THE `Comment` ICON, which RawPage's note tool writes -- and the bubble PDFium generates whatever `/Name` says
    // (`Note` against it compares two different icons: measured 0.667 at T8). T6's bubble measured 0.877 here, and
    // was redrawn on PDFium's proportions (guide I-12).
    note.icon = QStringLiteral("Comment");
    note.color = {1.0, 0.9, 0.2};
    graded.emplace_back(note, "note");

    int passed = 0;
    for (const auto& [annotation, name] : graded) {
        INFO(name);
        const Overlap o = overlap(annotation, name);
        REQUIRE(o.ours > 0);     // our appearance drew
        REQUIRE(o.theirs > 0);   // and PDFium generated one: the control has something to compare
        CHECK(o.iou() >= 0.90);
        passed += o.iou() >= 0.90;
    }
    CHECK(passed == 6);

    // NOT GRADED BY IoU (D6-32): drawn, and PDFium's generated control is not empty -- graded by A-50 instead.
    for (const auto& [kind, name] : {std::pair{AnnotationSubtype::StrikeOut, "strikeout"}, std::pair{AnnotationSubtype::Squiggly, "squiggly"}}) {
        INFO(name);
        const Overlap o = overlap(markup(kind, name, {0.85, 0.1, 0.1}), name);
        CHECK(o.ours > 0);
        CHECK(o.theirs > 0);
    }
}
