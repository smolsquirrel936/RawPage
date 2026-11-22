// tests/golden/DecorationCases -- see the header.

#include "tests/golden/DecorationCases.h"

#include "core/doc/Page.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "pdf/ops/RevisionWriter.h"

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
    return cases;
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
    core::Result<core::PageDecoration> laid = core::layoutHeaderFooter(item.spec, item.kind, page, fixedValues(), field);
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
