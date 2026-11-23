// tests/golden/DecorationCases -- see the header.

#include "tests/golden/DecorationCases.h"

#include "core/doc/Page.h"
#include "core/doc/Stamps.h"
#include "core/doc/Assembly.h"
#include "pdf/ops/DocumentAssembler.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/PageStamp.h"
#include "pdf/ops/RevisionWriter.h"

#include <QDir>
#include <QFile>

#include <cstdio>

namespace rawpage::golden {
namespace {

const QString kKey(32, QLatin1Char('d'));   // a fixed key: the same bytes every run

core::FieldValues fixedValues()
{
    return core::FieldValues{QDate(2026, 11, 23), QTime(10, 30), QStringLiteral("golden.pdf"), QStringLiteral("C:\\golden\\golden.pdf")};
}

core::HeaderFooterSpec base(double size = 10.0)
{
    core::HeaderFooterSpec spec;
    spec.size = size;
    spec.top = spec.bottom = 14.0;
    spec.left = spec.right = 18.0;
    return spec;
}

// A one-page PDF of `width` by `height` points drawing `content`, as bytes.
std::string onePage(double width, double height, const std::string& content, int rotate)
{
    std::vector<std::string> bodies(4);
    bodies[3] = "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream";
    bodies[0] = "<< /Type /Catalog /Pages 2 0 R >>";
    bodies[1] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
    char box[96];
    std::snprintf(box, sizeof box, "[0 0 %g %g]", width, height);
    bodies[2] = "<< /Type /Page /Parent 2 0 R /MediaBox " + std::string(box) + " /Contents 4 0 R"
                + (rotate != 0 ? " /Rotate " + std::to_string(rotate) : std::string()) + " >>";
    std::string base = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        offsets.push_back(base.size());
        base += std::to_string(i + 1) + " 0 obj\n" + bodies[i] + "\nendobj\n";
    }
    const std::size_t xref = base.size();
    base += "xref\n0 " + std::to_string(bodies.size() + 1) + "\n0000000000 65535 f \n";
    for (const std::size_t offset : offsets) {
        char entry[24];
        std::snprintf(entry, sizeof entry, "%010zu 00000 n \n", offset);
        base += entry;
    }
    base += "trailer\n<< /Size " + std::to_string(bodies.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref)
            + "\n%%EOF\n";
    return base;
}

// A 64 x 32 gradient, red to blue across, its alpha falling to a quarter down the image -- deterministic pixels.
core::StampImage gradient()
{
    constexpr int w = 64;
    constexpr int h = 32;
    QByteArray rgb;
    QByteArray alpha;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            rgb.append(static_cast<char>(255 - x * 4));
            rgb.append(static_cast<char>(64));
            rgb.append(static_cast<char>(x * 4));
            alpha.append(static_cast<char>(255 - y * 6));
        }
    }
    auto image = core::stampImageFromPixels(w, h, rgb, alpha);
    return image ? image.value() : core::StampImage{};
}

// A page as a watermark's source: a 200 x 100 page -- a green frame, a filled blue disc's square, a line across --
// copied as an insert copies it and read as a stamp's page.
core::StampPage sourcePage()
{
    const std::string bytes = onePage(200, 100, "0 0.6 0 RG 4 w 2 2 196 96 re S 0 0 1 rg 70 20 60 60 re f 0 g 1 w 10 50 m 190 50 l S", 0);
    auto copied = pdf::ops::ObjectGraphCopier::copyFile(bytes, {0});
    if (!copied) return {};
    auto page = pdf::ops::PageStamp::fromPayload(copied.value().graph);
    return page ? page.value() : core::StampPage{};
}

core::WatermarkSpec watermarkText(const QString& text, double size)
{
    core::WatermarkSpec spec;
    spec.text = text;
    spec.size = size;
    spec.colour = {0.3, 0.3, 0.3};
    return spec;
}

std::vector<DecorationCase> buildCases()
{
    std::vector<DecorationCase> cases;
    {
        core::HeaderFooterSpec spec = base();
        spec.areas = {QStringLiteral("Header left"), QStringLiteral("Header centre"), QStringLiteral("Header right"),
                      QStringLiteral("Footer left"), QStringLiteral("Footer centre"), QStringLiteral("Footer right")};
        cases.push_back({QStringLiteral("align-six"), QStringLiteral("all six areas, 10 pt black, 14 and 18 pt from the edges"), 0,
                         core::DecorationKind::HeaderFooter, spec});
    }
    {
        core::HeaderFooterSpec spec = base(24.0);
        spec.areas[1] = QStringLiteral("Large 24");
        spec.top = 8;
        cases.push_back({QStringLiteral("size-large"), QStringLiteral("24 pt, centred, 8 pt from the top"), 0,
                         core::DecorationKind::HeaderFooter, spec});
    }
    {
        core::HeaderFooterSpec spec = base(11.0);
        spec.areas[0] = QStringLiteral("Red, underlined");
        spec.areas[5] = QStringLiteral("<<page>> / <<count>>");
        spec.colour = {0.8, 0.0, 0.0};
        spec.underline = true;
        cases.push_back({QStringLiteral("colour-underline"), QStringLiteral("red and underlined, a field expanded"), 0,
                         core::DecorationKind::HeaderFooter, spec});
    }
    {
        core::HeaderFooterSpec spec = base();
        spec.areas[2] = QStringLiteral("top 40, right 60");
        spec.areas[3] = QStringLiteral("bottom 50, left 30");
        spec.top = 40;
        spec.right = 60;
        spec.bottom = 50;
        spec.left = 30;
        cases.push_back({QStringLiteral("edges"), QStringLiteral("each edge distance its own"), 0, core::DecorationKind::HeaderFooter, spec});
    }
    for (const int rotate : {90, 270}) {
        core::HeaderFooterSpec spec = base();
        spec.areas[1] = QStringLiteral("Turned %1").arg(rotate);
        spec.areas[5] = QStringLiteral("Page <<page>> of <<count>>");
        cases.push_back({QStringLiteral("rotated-%1").arg(rotate),
                         QStringLiteral("a page turned %1: the header across the top as SHOWN").arg(rotate), rotate,
                         core::DecorationKind::HeaderFooter, spec});
    }
    {
        core::HeaderFooterSpec spec = base(12.0);
        spec.areas[1] = QStringLiteral("SHRINK");
        spec.areas[4] = QStringLiteral("the content scaled between the bands");
        spec.shrink = true;
        cases.push_back({QStringLiteral("shrink"), QStringLiteral("the page's content scaled down between the header and footer"), 0,
                         core::DecorationKind::HeaderFooter, spec});
    }
    {
        core::HeaderFooterSpec spec = base(12.0);
        spec.areas[1] = QStringLiteral("第一章 ページ 한국 Résumé");
        cases.push_back({QStringLiteral("cjk"), QStringLiteral("Han, kana, Hangul and Latin in one line (machine fonts for CJK)"), 0,
                         core::DecorationKind::HeaderFooter, spec, true});
    }
    {
        core::HeaderFooterSpec spec = base(9.0);
        spec.areas[5] = QStringLiteral("<<bates>>");
        spec.bates = core::BatesFormat{QStringLiteral("GOLD-"), QStringLiteral("-A"), 6, 42};
        cases.push_back({QStringLiteral("bates"), QStringLiteral("a Bates number at the bottom right, its prefix, digits and suffix"), 0,
                         core::DecorationKind::Bates, spec});
    }
    // ---- Stage 7 T15: watermarks ------------------------------------------------------------------------------------
    const auto watermark = [&cases](const char* name, const char* note, int rotate, core::WatermarkSpec spec) {
        DecorationCase item;
        item.name = QStringLiteral("watermark-") + QLatin1String(name);
        item.note = QLatin1String(note);
        item.rotate = rotate;
        item.kind = core::DecorationKind::Watermark;
        item.watermark = std::move(spec);
        cases.push_back(std::move(item));
    };
    {
        core::WatermarkSpec spec = watermarkText(QStringLiteral("DRAFT"), 72);
        spec.opacity = 0.4;
        spec.rotation = 30;
        spec.scale = 0.8;
        watermark("diagonal-behind", "grey DRAFT turned 30 degrees, 80 % of the page, 40 % opaque, BEHIND the bars", 0, spec);
    }
    {
        core::WatermarkSpec spec = watermarkText(QStringLiteral("COPY"), 48);
        spec.colour = {0.85, 0.0, 0.0};
        spec.opacity = 0.5;
        spec.rotation = -20;
        spec.behind = false;
        spec.scale = 0.6;
        watermark("front-red", "red COPY turned -20 degrees, half opaque, IN FRONT of the bars", 0, spec);
    }
    {
        core::WatermarkSpec spec = watermarkText(QStringLiteral("TL"), 18);
        spec.rotation = 0;
        spec.scaleMode = core::WatermarkScale::Absolute;
        spec.scale = 1.0;
        spec.anchor = core::WatermarkAnchor::TopLeft;
        spec.offsetX = 12;
        spec.offsetY = -12;
        spec.opacity = 1.0;
        spec.behind = false;
        core::WatermarkSpec right = spec;
        watermark("anchor-offset", "18 pt TL at its natural size, top-left, 12 points in from each edge", 0, spec);
        right.text = QStringLiteral("BR");
        right.anchor = core::WatermarkAnchor::BottomRight;
        right.offsetX = -12;
        right.offsetY = 12;
        watermark("anchor-bottom-right", "18 pt BR at the bottom-right corner, 12 points in", 0, right);
    }
    {
        core::WatermarkSpec spec;
        spec.source = core::WatermarkSource::Image;
        spec.image = gradient();
        spec.rotation = 0;
        spec.scaleMode = core::WatermarkScale::Absolute;
        spec.scale = 1.0;   // 48 x 24 points
        spec.tile = true;
        spec.tileGap = 10;
        spec.opacity = 0.7;
        spec.behind = false;
        watermark("image-tiled", "a gradient with alpha, 48 x 24 points, tiled 10 points apart from the centre, 70 % opaque, in front", 0, spec);
        spec.tile = false;
        spec.rotation = 90;
        spec.scale = 2.0;
        spec.anchor = core::WatermarkAnchor::BottomRight;
        spec.opacity = 1.0;
        watermark("image-turned", "the gradient twice its size, turned 90 degrees, at the bottom-right corner", 0, spec);
    }
    {
        core::WatermarkSpec spec;
        spec.source = core::WatermarkSource::Page;
        spec.page = sourcePage();
        spec.rotation = 15;
        spec.scale = 0.7;
        spec.opacity = 0.6;
        spec.behind = false;
        watermark("page", "a PDF page (frame, square, line) turned 15 degrees, 70 % of the page, 60 % opaque", 0, spec);
    }
    {
        core::WatermarkSpec spec = watermarkText(QStringLiteral("TOP"), 24);
        spec.rotation = 0;
        spec.scaleMode = core::WatermarkScale::Absolute;
        spec.scale = 1.0;
        spec.anchor = core::WatermarkAnchor::Top;
        spec.offsetY = -6;
        spec.opacity = 1.0;
        spec.behind = false;
        watermark("rotated-page", "a page turned 90: TOP across the top of the page AS SHOWN, reading upright", 90, spec);
    }

    // Stage 7 T16 (D7-46, guide A-104): impositions on a 4 x 6 inch sheet, one side each.
    const auto imposition = [&cases](const char* name, const char* note, const char* source, int side, const core::ImpositionSpec& spec) {
        DecorationCase item;
        item.name = QStringLiteral("imposition-") + QLatin1String(name);
        item.note = QLatin1String(note);
        item.imposition = spec;
        item.source = QLatin1String(source);
        item.side = side;
        cases.push_back(std::move(item));
    };
    core::ImpositionSpec small;
    small.sheet = core::PdfSize{288.0, 432.0};
    small.margin = 24.0;
    {
        core::ImpositionSpec spec = small;
        spec.layout = core::ImpositionLayout::Booklet;
        spec.duplex = core::DuplexFlip::ShortEdge;
        spec.cropMarks = spec.registrationMarks = spec.colourBar = spec.sheetLabels = spec.frames = true;
        imposition("booklet-marks", "5 pages as a booklet, sheet 1 front: a blank and C05-0001, every mark, frames and the label", "assembly/odd-count-5.pdf", 0,
                   spec);
    }
    {
        core::ImpositionSpec spec = small;
        spec.layout = core::ImpositionLayout::Booklet;
        spec.duplex = core::DuplexFlip::LongEdge;
        spec.cropMarks = spec.sheetLabels = spec.frames = true;
        imposition("booklet-back-turned", "sheet 1 back for a long-edge flip: C05-0002 and a blank, the whole side half-turned, label too",
                   "assembly/odd-count-5.pdf", 1, spec);
    }
    {
        core::ImpositionSpec spec = small;
        spec.layout = core::ImpositionLayout::Booklet;
        spec.creep = 6.0;
        spec.cropMarks = spec.frames = true;
        imposition("booklet-creep", "17 pages, the innermost sheet's front (C17-0012, C17-0009): 24 points of creep toward the fold, clipped there",
                   "assembly/odd-count-17.pdf", 8, spec);
    }
    {
        core::ImpositionSpec spec = small;
        spec.perSheet = 4;
        spec.gutter = 12.0;
        spec.cropMarks = spec.frames = true;
        imposition("4up-gutter", "7 pages 4-up across, the second side: C07-0005 ... 0007, a 12-point gutter, an empty cell", "assembly/odd-count-7.pdf", 1, spec);
    }
    {
        core::ImpositionSpec spec = small;
        spec.perSheet = 9;
        spec.order = core::ImpositionOrder::Down;
        spec.reversed = true;
        spec.gutter = 3.0;
        spec.sheetLabels = spec.frames = true;
        imposition("9up-down-reversed", "9 pages 9-up, down and reversed: C09-0001 at the top right, columns right to left", "assembly/odd-count-9.pdf", 0,
                   spec);
    }
    {
        core::ImpositionSpec spec = small;
        spec.perSheet = 2;
        spec.frames = true;
        imposition("2up-turned-pages", "inherit-resources' pages 3 and 4: /Rotate 90 and 180 and CropBoxes, each as a reader shows it",
                   "assembly/inherit-resources.pdf", 1, spec);
    }
    {
        core::ImpositionSpec spec = small;
        spec.perSheet = 16;
        spec.gutter = 2.0;
        spec.registrationMarks = spec.colourBar = spec.frames = true;
        imposition("16up-bar", "17 pages 16-up across, the first side: registration marks and the colour bar", "assembly/odd-count-17.pdf", 0, spec);
    }
    {
        core::ImpositionSpec spec = small;
        spec.perSheet = 6;
        spec.orientation = core::SheetOrientation::Landscape;
        spec.sheetLabels = true;
        imposition("6up-landscape", "7 pages 6-up across on a landscape sheet: 3 x 2, no frames, the label", "assembly/odd-count-7.pdf", 0, spec);
    }
    return cases;
}

// An imposition case: its source imposed through the shipped path, and its one side made a document of its own.
std::string imposedPdf(const DecorationCase& item)
{
    QFile file(QDir(QStringLiteral(RAWPAGE_CORPUS_DIR)).filePath(item.source));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    auto source = pdf::ops::DocumentAssembler::Source::open(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!source) return {};
    const core::ImpositionText text{{QStringLiteral("Sheet %1 of %2"), QStringLiteral("Sheet %1 of %2, front"), QStringLiteral("Sheet %1 of %2, back")},
                                    core::TypedFont::Simplified};
    auto imposed = source.value()->impose(*item.imposition, text);
    if (!imposed) return {};
    auto sheets = pdf::ops::DocumentAssembler::Source::open(
        std::string(imposed.value().bytes.constData(), static_cast<std::size_t>(imposed.value().bytes.size())));
    if (!sheets) return {};
    auto one = sheets.value()->make({item.side});
    if (!one) return {};
    return one.value().bytes;
}

} // namespace

const std::vector<DecorationCase>& decorationCases()
{
    static const std::vector<DecorationCase> cases = buildCases();
    return cases;
}

QString decorationBaselinePath(const DecorationCase& item)
{
    return QStringLiteral("decoration/%1.png").arg(item.name);
}

std::string decorationPdf(const DecorationCase& item)
{
    if (item.imposition) return imposedPdf(item);   // S7 T16
    // THE PAGE: a catalog, a tree, one page and its content -- a grey band and a black bar across it.
    std::vector<std::string> bodies(4);
    const std::string content = "0.75 g 20 60 248 40 re f 0 g 20 130 248 8 re f 20 20 8 176 re f";
    bodies[3] = "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream";
    bodies[0] = "<< /Type /Catalog /Pages 2 0 R >>";
    bodies[1] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
    char box[96];
    std::snprintf(box, sizeof box, "[0 0 %g %g]", kDecorationPageWidth, kDecorationPageHeight);
    bodies[2] = "<< /Type /Page /Parent 2 0 R /MediaBox " + std::string(box) + " /Contents 4 0 R"
                + (item.rotate != 0 ? " /Rotate " + std::to_string(item.rotate) : std::string()) + " >>";
    std::string base = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        offsets.push_back(base.size());
        base += std::to_string(i + 1) + " 0 obj\n" + bodies[i] + "\nendobj\n";
    }
    const std::size_t xref = base.size();
    base += "xref\n0 " + std::to_string(bodies.size() + 1) + "\n0000000000 65535 f \n";
    for (const std::size_t offset : offsets) {
        char entry[24];
        std::snprintf(entry, sizeof entry, "%010zu 00000 n \n", offset);
        base += entry;
    }
    base += "trailer\n<< /Size " + std::to_string(bodies.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref)
            + "\n%%EOF\n";

    // THE DECORATION, through the shipped path: laid out as the command lays it out, written as a save writes it.
    core::Page page(0, core::PdfRect{core::PdfPoint{0, 0}, core::PdfSize{kDecorationPageWidth, kDecorationPageHeight}});
    page.setRotation(item.rotate);
    core::FieldPage field{0, 1, QString(), std::nullopt};
    if (core::usesBates(item.spec)) field.bates = item.spec.bates.start;
    core::Result<core::PageDecoration> laid = item.watermark ? core::layoutWatermark(*item.watermark, page)
                                                             : core::layoutHeaderFooter(item.spec, item.kind, page, fixedValues(), field);
    if (!laid) return {};
    auto sink = pdf::ops::MutationSinkImpl::open(base);
    if (!sink) return {};
    if (!sink.value()->addPageDecorations({0}, {laid.value()}, kKey)) return {};
    core::Result<pdf::ops::ComposedRevision> written = pdf::ops::RevisionWriter::compose(*sink.value(), false);
    if (!written) return {};
    const std::string bytes(written.value().bytes.constData(), static_cast<std::size_t>(written.value().bytes.size()));
    if (!pdf::ops::RevisionWriter::verify(base, bytes, *sink.value())) return {};
    return bytes;
}

} // namespace rawpage::golden
