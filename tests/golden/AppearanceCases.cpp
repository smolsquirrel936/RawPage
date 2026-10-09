// tests/golden/AppearanceCases -- implementation. Stage 6 T8. See the header for why the spec is code.

#include "tests/golden/AppearanceCases.h"

#include "core/doc/DrawnShapes.h"
#include "core/doc/Stamps.h"
#include "core/doc/TypedText.h"
#include "core/settings/CommentingTypes.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/ops/AnnotationOps.h"
#include "pdf/ops/AppearanceStreamBuilder.h"

#include <cmath>
#include <cstdio>
#include <optional>

namespace rawpage::golden {
namespace {

using core::Annotation;
using core::AnnotationSubtype;
using core::PdfPoint;
using core::PdfRect;
using core::PdfSize;

constexpr std::int64_t kInstant = 1791763200;   // 2026-10-12T00:00:00Z: every case's, so its bytes never move

// A FIXED identity -- the case's name and the annotation's place in it -- so a case writes the same bytes on
// every run and every machine.
Annotation made(AnnotationSubtype subtype, const QString& caseName, int index)
{
    Annotation a = core::newAnnotation(subtype, 0, QStringLiteral("Appearance"), kInstant);
    QString nm = QStringLiteral("%1%2").arg(caseName).arg(index);
    nm = QString::fromLatin1(nm.toUtf8().toHex()).left(32).leftJustified(32, QLatin1Char('0'));
    a.nm = nm;
    a.id = core::AnnotationId::ofCreated(nm);
    return a;
}

Annotation styled(Annotation a, std::vector<double> colour, double width, core::LineStyle style = core::LineStyle::Solid)
{
    a.color = std::move(colour);
    a.borderWidth = width;
    a.borderStyle = style == core::LineStyle::Solid ? QStringLiteral("S") : QStringLiteral("D");
    a.dashPattern = core::dashFor(style, width);
    return a;
}

Annotation fit(Annotation a)
{
    if (std::optional<Annotation> fitted = core::fittedShape(a)) return *fitted;
    return a;   // the suite's case fails on it, loudly, rather than this spec hiding it
}

Annotation line(const QString& name, int index, PdfPoint from, PdfPoint to, std::vector<double> colour, double width,
                const QString& start, const QString& end, core::LineStyle style = core::LineStyle::Solid,
                std::vector<double> fill = {})
{
    Annotation a = styled(made(AnnotationSubtype::Line, name, index), std::move(colour), width, style);
    a.line = {from.x, from.y, to.x, to.y};
    if (start != QLatin1String("None") || end != QLatin1String("None")) a.lineEndings = {start, end};
    a.interiorColor = std::move(fill);
    return fit(a);
}

Annotation box(const QString& name, int index, AnnotationSubtype kind, PdfRect rect, std::vector<double> colour, double width,
               core::LineStyle style = core::LineStyle::Solid, std::vector<double> fill = {})
{
    Annotation a = styled(made(kind, name, index), std::move(colour), width, style);
    a.rect = rect;
    a.interiorColor = std::move(fill);
    return fit(a);
}

Annotation polygon(const QString& name, int index, AnnotationSubtype kind, std::vector<double> vertices,
                   std::vector<double> colour, double width, std::vector<double> fill = {})
{
    Annotation a = styled(made(kind, name, index), std::move(colour), width);
    a.vertices = std::move(vertices);
    a.interiorColor = std::move(fill);
    return fit(a);
}

Annotation markup(const QString& name, AnnotationSubtype kind, double left, double bottom, double right, double top,
                  std::vector<double> colour)
{
    Annotation a = made(kind, name, 0);
    a.quadPoints = {left, top, right, top, left, bottom, right, bottom};   // Acrobat's corner order
    a.rect = *core::markupRect(a.quadPoints);
    a.color = std::move(colour);
    return a;
}

Annotation typed(const QString& name, core::FreeTextKind kind, PdfPoint topLeft, const QString& text,
                 const QString& ending = QStringLiteral("OpenArrow"))
{
    Annotation a = made(AnnotationSubtype::FreeText, name, 0);
    a.intent = QString(core::freeTextIntent(kind));
    a.contents = text;
    a.color = {0.0, 0.0, 0.6};
    a.borderWidth = kind == core::FreeTextKind::Typewriter ? 0.0 : 1.0;
    if (kind == core::FreeTextKind::Box) a.interiorColor = {1.0, 1.0, 0.85};
    a.defaultAppearance = core::freeTextDefaultAppearance(core::TypedFont::Helvetica, 11.0, a.color);
    const double inset = *a.borderWidth + core::kFreeTextPadding;
    a.rect = core::freeTextBoxAt(topLeft, kind == core::FreeTextKind::Typewriter ? 20.0 : 110.0, 11 * 1.2 + 2 * inset, 0);
    if (kind == core::FreeTextKind::Callout) {
        a.lineEndings = {ending};
        a.callout = core::calloutLeader(PdfPoint{20, 20}, a.rect);
    }
    const core::Result<Annotation> fitted = core::fittedFreeText(a, core::TypedFont::Simplified);
    return fitted ? fitted.value() : a;
}

// S6 T10: a stamp made as the stamp tool makes it, given a FIXED identity and fitted into `into` -- turned first
// when `degrees` says so, as the properties bar turns one.
Annotation stamp(Annotation a, const QString& name, int index, PdfRect into, int degrees = 0)
{
    const Annotation fixed = made(AnnotationSubtype::Stamp, name, index);
    a.nm = fixed.nm;
    a.id = fixed.id;
    if (degrees != 0) {
        if (core::Result<Annotation> turned = core::rotatedStamp(a, degrees)) a = turned.value();
    }
    core::Result<Annotation> fitted = core::stampFittedTo(a, into);
    return fitted ? fitted.value() : a;   // the suite's case fails on it, loudly
}

Annotation standardStamp(const QString& caseName, int index, const char* stampName, PdfRect into, int degrees = 0)
{
    core::Result<Annotation> a = core::newStandardStamp(QString::fromLatin1(stampName), 0, PdfPoint{100, 75},
                                                         QStringLiteral("Appearance"), kInstant);
    return stamp(a ? a.value() : Annotation{}, caseName, index, into, degrees);
}

std::vector<AppearanceCase> buildCases()
{
    const std::vector<double> red{0.85, 0.1, 0.1}, blue{0.1, 0.2, 0.85}, green{0.1, 0.6, 0.2}, black{0.0, 0.0, 0.0};
    const std::vector<double> yellow{1.0, 0.9, 0.2}, cyan{0.6, 0.9, 1.0}, pale{0.85, 1.0, 0.85};
    std::vector<AppearanceCase> cases;
    const auto add = [&cases](const char* name, const char* note, std::vector<Annotation> annotations, int rotate = 0) {
        cases.push_back(AppearanceCase{QString::fromLatin1(name), QString::fromLatin1(note), rotate, std::move(annotations)});
    };
    const auto n = [](const char* name) { return QString::fromLatin1(name); };

    add("line-solid", "a 1-point red line, no endings",
        {line(n("line-solid"), 0, {30, 30}, {170, 120}, red, 1.0, n("None"), n("None"))});
    add("line-dashed", "a 3-point blue dashed line",
        {line(n("line-dashed"), 0, {30, 120}, {170, 30}, blue, 3.0, n("None"), n("None"), core::LineStyle::Dashed)});
    add("arrow", "the arrow tool's: an open head at the end, 2 points",
        {line(n("arrow"), 0, {30, 40}, {160, 110}, red, 2.0, n("None"), n("OpenArrow"))});
    add("line-endings-1", "Square, Circle, Diamond, OpenArrow, ClosedArrow and Butt, filled green",
        {line(n("line-endings-1"), 0, {40, 125}, {160, 125}, black, 1.5, n("Square"), n("Circle"), core::LineStyle::Solid, green),
         line(n("line-endings-1"), 1, {40, 80}, {160, 80}, black, 1.5, n("Diamond"), n("OpenArrow"), core::LineStyle::Solid, green),
         line(n("line-endings-1"), 2, {40, 35}, {160, 35}, black, 1.5, n("ClosedArrow"), n("Butt"), core::LineStyle::Solid, green)});
    add("line-endings-2", "ROpenArrow, RClosedArrow and Slash, unfilled, at an angle",
        {line(n("line-endings-2"), 0, {40, 110}, {160, 130}, blue, 1.5, n("ROpenArrow"), n("RClosedArrow")),
         line(n("line-endings-2"), 1, {40, 40}, {160, 70}, blue, 1.5, n("Slash"), n("Slash"))});
    add("rect-fill", "a 3-point red rectangle filled yellow",
        {box(n("rect-fill"), 0, AnnotationSubtype::Square, PdfRect{PdfPoint{30, 30}, PdfSize{140, 90}}, red, 3.0,
             core::LineStyle::Solid, yellow)});
    add("rect-dashed", "a 2-point blue dashed rectangle, no fill",
        {box(n("rect-dashed"), 0, AnnotationSubtype::Square, PdfRect{PdfPoint{30, 30}, PdfSize{140, 90}}, blue, 2.0,
             core::LineStyle::Dashed)});
    add("rect-dotted", "a 4-point green dotted rectangle",
        {box(n("rect-dotted"), 0, AnnotationSubtype::Square, PdfRect{PdfPoint{30, 30}, PdfSize{140, 90}}, green, 4.0,
             core::LineStyle::Dotted)});
    add("oval-fill", "a 2-point black oval filled cyan",
        {box(n("oval-fill"), 0, AnnotationSubtype::Circle, PdfRect{PdfPoint{30, 25}, PdfSize{140, 100}}, black, 2.0,
             core::LineStyle::Solid, cyan)});
    add("oval-dashed", "a 1-point red dashed circle",
        {box(n("oval-dashed"), 0, AnnotationSubtype::Circle, PdfRect{PdfPoint{50, 25}, PdfSize{100, 100}}, red, 1.0,
             core::LineStyle::Dashed)});
    add("polygon-fill", "a 2-point blue pentagon filled pale green",
        {polygon(n("polygon-fill"), 0, AnnotationSubtype::Polygon, {40, 40, 160, 30, 175, 100, 100, 135, 30, 100}, blue, 2.0, pale)});
    {
        Annotation polyline = polygon(n("polyline-endings"), 0, AnnotationSubtype::PolyLine, {30, 40, 70, 120, 120, 40, 170, 110},
                                      red, 2.0, yellow);
        polyline.lineEndings = {QStringLiteral("Circle"), QStringLiteral("ClosedArrow")};
        add("polyline-endings", "a 2-point polyline, a circle at its start and a closed arrow at its end, filled", {fit(polyline)});
    }
    {
        Annotation one = polygon(n("cloud-1"), 0, AnnotationSubtype::Polygon, {40, 35, 160, 35, 160, 115, 40, 115}, red, 1.0);
        one.cloudIntensity = 1.0;
        one.intent = QStringLiteral("PolygonCloud");
        add("cloud-1", "the cloud tool's: intensity 1, red, 1 point", {fit(one)});
        Annotation two = polygon(n("cloud-2"), 0, AnnotationSubtype::Polygon, {45, 45, 155, 40, 150, 105, 50, 110}, blue, 2.0, pale);
        two.cloudIntensity = 2.0;
        two.intent = QStringLiteral("PolygonCloud");
        add("cloud-2", "intensity 2, filled, 2 points", {fit(two)});
        Annotation square = box(n("cloud-square"), 0, AnnotationSubtype::Square, PdfRect{PdfPoint{30, 25}, PdfSize{140, 100}}, green, 1.0);
        square.cloudIntensity = 1.0;
        square.rectDifferences = {9, 9, 9, 9};
        add("cloud-square", "a producer's cloudy rectangle, drawn round its rectangle less /RD", {square});
    }
    {
        std::vector<double> loop;
        for (int i = 0; i <= 30; ++i) {
            const double t = i * 2 * 3.14159265358979 / 26.0;
            loop.push_back(120 + 25 * std::cos(t) + i * 0.8);
            loop.push_back(80 + 25 * std::sin(t));
        }
        Annotation thin = styled(made(AnnotationSubtype::Ink, n("ink-thin"), 0), blue, 1.0);
        thin.borderStyle.clear();
        thin.dashPattern.clear();
        thin.inkList = {{20, 40, 40, 110, 60, 40, 80, 110}, loop};
        add("ink-thin", "the pencil's: a zigzag and a loop through their samples, 1 point", {fit(thin)});
        Annotation thick = styled(made(AnnotationSubtype::Ink, n("ink-thick"), 0), red, 6.0);
        thick.borderStyle.clear();
        thick.dashPattern.clear();
        thick.inkList = {{30, 30, 70, 80, 110, 50, 170, 120}, {40, 120}};
        add("ink-thick", "6 points: a stroke and a dot", {fit(thick)});
    }
    add("highlight", "a highlight over the black bar: multiply keeps the bar black",
        {markup(n("highlight"), AnnotationSubtype::Highlight, 15, 95, 185, 112, {1.0, 1.0, 0.0})});
    {
        Annotation normal = markup(n("highlight-normal"), AnnotationSubtype::Highlight, 15, 95, 185, 112, {1.0, 1.0, 0.0});
        normal.blendMode = QStringLiteral("Normal");
        add("highlight-normal", "the same told not to multiply: the bar is covered", {normal});
    }
    add("underline", "an underline on the grey band",
        {markup(n("underline"), AnnotationSubtype::Underline, 20, 30, 180, 52, red)});
    add("strikeout", "a strikeout on the grey band (D6-32's height)",
        {markup(n("strikeout"), AnnotationSubtype::StrikeOut, 20, 30, 180, 52, red)});
    add("squiggly", "a squiggly on the grey band (D6-32's band)",
        {markup(n("squiggly"), AnnotationSubtype::Squiggly, 20, 30, 180, 52, green)});
    {
        Annotation caret = made(AnnotationSubtype::Caret, n("caret"), 0);
        caret.rect = PdfRect{PdfPoint{90, 60}, PdfSize{12, 16}};
        caret.color = blue;
        Annotation comment = made(AnnotationSubtype::Text, n("notes"), 0);
        comment.rect = PdfRect{PdfPoint{30, 70}, PdfSize{20, 20}};
        comment.icon = QStringLiteral("Comment");
        comment.color = yellow;
        Annotation note = made(AnnotationSubtype::Text, n("notes"), 1);
        note.rect = PdfRect{PdfPoint{140, 70}, PdfSize{20, 20}};
        note.color = cyan;
        add("caret-notes", "a caret, the Comment icon and the Note icon", {caret, comment, note});
    }
    {
        Annotation half = box(n("opacity"), 0, AnnotationSubtype::Square, PdfRect{PdfPoint{30, 20}, PdfSize{140, 110}}, red, 2.0,
                              core::LineStyle::Solid, red);
        half.opacity = 0.5;
        add("opacity", "a red rectangle at 50 % over the band and the bar", {half});
        Annotation multiply = polygon(n("blend-multiply"), 0, AnnotationSubtype::Polygon,
                                      {20, 20, 180, 20, 180, 120, 20, 120}, blue, 1.0, cyan);
        multiply.blendMode = QStringLiteral("Multiply");
        add("blend-multiply", "a cyan polygon under multiply: the band and the bar show through", {multiply});
        Annotation screen = polygon(n("blend-screen"), 0, AnnotationSubtype::Polygon, {20, 20, 180, 20, 180, 120, 20, 120},
                                    blue, 1.0, cyan);
        screen.blendMode = QStringLiteral("Screen");
        add("blend-screen", "the same under screen: the bar goes cyan", {screen});
    }
    add("rotated-90", "a rectangle and an arrow on a page at /Rotate 90",
        {box(n("rotated-90"), 0, AnnotationSubtype::Square, PdfRect{PdfPoint{30, 30}, PdfSize{80, 50}}, blue, 2.0),
         line(n("rotated-90"), 1, {120, 30}, {180, 120}, red, 2.0, n("None"), n("ClosedArrow"))},
        90);
    {
        Annotation note = made(AnnotationSubtype::Text, n("norotate"), 0);
        note.rect = PdfRect{PdfPoint{90, 70}, PdfSize{20, 20}};
        note.icon = QStringLiteral("Comment");
        note.color = yellow;
        note.flags = core::AnnotationFlag::Print | core::AnnotationFlag::NoZoom | core::AnnotationFlag::NoRotate;
        add("norotate-note", "a NoRotate note on a page at /Rotate 90: upright", {note}, 90);
    }
    add("freetext-box", "a text box: Helvetica 11, filled, outlined",
        {typed(n("freetext-box"), core::FreeTextKind::Box, PdfPoint{40, 120}, QStringLiteral("Typed text, wrapped in its box."))});
    add("callout-diamond", "a callout whose leader ends in a diamond (T8 draws every ending)",
        {typed(n("callout-diamond"), core::FreeTextKind::Callout, PdfPoint{70, 120}, QStringLiteral("Look here"),
               QStringLiteral("Diamond"))});
    add("typewriter", "typewriter text: no border, no fill",
        {typed(n("typewriter"), core::FreeTextKind::Typewriter, PdfPoint{30, 90}, QStringLiteral("Typewriter"))});

    // ---- S6 T10: stamps (D6-16, guide A-75) -----------------------------------------------------------------
    // Every one of ISO 32000-1 Table 181's fourteen, three to a page, each fitted into a band of the page -- its
    // words in Helvetica-Bold, in its colour, inside its rounded border.
    {
        const std::vector<QLatin1String>& names = core::standardStampNames();
        const PdfRect bands[3] = {PdfRect{PdfPoint{15, 102}, PdfSize{170, 38}}, PdfRect{PdfPoint{15, 56}, PdfSize{170, 38}},
                                  PdfRect{PdfPoint{15, 10}, PdfSize{170, 38}}};
        static const char* const pageNames[5] = {"stamps-standard-1", "stamps-standard-2", "stamps-standard-3",
                                                 "stamps-standard-4", "stamps-standard-5"};
        static const char* const pageNotes[5] = {"Approved, Experimental, NotApproved", "AsIs, Expired, NotForPublicRelease",
                                                 "Confidential, Final, Sold", "Departmental, ForComment, TopSecret",
                                                 "Draft, ForPublicRelease"};
        for (int page = 0; page < 5; ++page) {
            std::vector<Annotation> three;
            for (int k = 0; k < 3 && page * 3 + k < static_cast<int>(names.size()); ++k)
                three.push_back(standardStamp(n(pageNames[page]), k, names[static_cast<std::size_t>(page * 3 + k)].data(), bands[k]));
            add(pageNames[page], pageNotes[page], std::move(three));
        }
    }
    {
        // Two DYNAMIC stamps, their lines baked at the cases' fixed instant -- the identity and the minute, at UTC.
        std::vector<Annotation> dynamic;
        int k = 0;
        for (const char* name : {"DynamicApproved", "DynamicConfidential"}) {
            core::Result<Annotation> a = core::newDynamicStamp(QString::fromLatin1(name), 0, PdfPoint{100, 75},
                                                               QStringLiteral("Appearance Tester"), kInstant, 0);
            dynamic.push_back(stamp(a ? a.value() : Annotation{}, n("stamps-dynamic"), k,
                                    k == 0 ? PdfRect{PdfPoint{20, 80}, PdfSize{160, 60}} : PdfRect{PdfPoint{20, 10}, PdfSize{160, 60}}));
            ++k;
        }
        add("stamps-dynamic", "DynamicApproved and DynamicConfidential: the words, the author and the minute baked", std::move(dynamic));
    }
    add("stamps-turned", "Approved turned 30 degrees, Draft turned 90: each inside its rectangle",
        {standardStamp(n("stamps-turned"), 0, "Approved", PdfRect{PdfPoint{10, 40}, PdfSize{110, 100}}, 30),
         standardStamp(n("stamps-turned"), 1, "Draft", PdfRect{PdfPoint{140, 10}, PdfSize{50, 130}}, 90)});
    {
        // An IMAGE stamp: 32 by 16 pixels, a red-to-blue ramp under an alpha that is clear on the left and opaque on
        // the right -- the grey band shows through where it is clear (the pixels are code, so the case is too).
        QByteArray rgb, alpha;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 32; ++x) {
                rgb.append(static_cast<char>(255 - x * 8));
                rgb.append(static_cast<char>(y * 12));
                rgb.append(static_cast<char>(x * 8));
                alpha.append(static_cast<char>(x * 8 + 7));
            }
        }
        const core::Result<core::StampImage> pixels = core::stampImageFromPixels(32, 16, rgb, alpha);
        core::Result<Annotation> a = pixels ? core::newImageStamp(pixels.value(), QStringLiteral("Ramp"), 0, PdfPoint{100, 75},
                                                                 QStringLiteral("Appearance"), kInstant)
                                            : core::Result<Annotation>(pixels.error());
        add("stamp-image", "a 32 x 16 image with an alpha ramp, over the band: its own pixels, translucent at the left",
            {stamp(a ? a.value() : Annotation{}, n("stamp-image"), 0, PdfRect{PdfPoint{20, 20}, PdfSize{160, 80}})});
    }
    return cases;
}

std::string serialized(const pdf::cos::CosObject& object, const std::string* data)
{
    if (data == nullptr) return pdf::cos::serializeObject(object);
    std::vector<pdf::cos::CosObject::Entry> entries(object.entries().begin(), object.entries().end());
    entries.push_back({"Length", pdf::cos::CosObject::makeInteger(static_cast<long long>(data->size()))});
    return pdf::cos::serializeObject(pdf::cos::CosObject::makeDictionary(std::move(entries))) + "\nstream\n" + *data
           + "\nendstream";
}

} // namespace

const std::vector<AppearanceCase>& appearanceCases()
{
    static const std::vector<AppearanceCase> cases = buildCases();
    return cases;
}

QString appearanceBaselinePath(const AppearanceCase& item)
{
    return QStringLiteral("appearance/%1.png").arg(item.name);
}

std::string appearancePdf(const std::vector<core::Annotation>& annotations, int rotate, bool withAppearance)
{
    // 1 the catalog, 2 the page tree, 3 the page, 4 its content; then the objects appearances share, then each
    // annotation's form and its dictionary.
    std::vector<std::string> bodies(4);
    const std::string content = "0.75 g 15 30 170 22 re f 0 g 15 98 170 10 re f";
    bodies[3] = "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream";

    pdf::ops::CollectedAppearanceObjects shared(5);
    std::vector<pdf::ops::Appearance> forms;
    if (withAppearance) {
        for (const core::Annotation& a : annotations) {
            auto built = pdf::ops::AppearanceStreamBuilder::normal(a, shared);
            if (!built) return std::string();   // the case fails, by name, rather than a page drawn without it
            forms.push_back(std::move(built).value());
        }
    }
    for (const pdf::ops::CollectedAppearanceObjects::Entry& entry : shared.entries())
        bodies.push_back(serialized(entry.object, entry.data ? &*entry.data : nullptr));
    std::string annots;
    for (std::size_t i = 0; i < annotations.size(); ++i) {
        std::optional<pdf::cos::CosRef> appearance;
        if (withAppearance) {
            bodies.push_back(serialized(forms[i].dictionary, &forms[i].data));
            appearance = pdf::cos::CosRef{static_cast<long long>(bodies.size()), 0};
        }
        auto dictionary = pdf::ops::AnnotationOps::createdDictionary(annotations[i], pdf::cos::CosRef{3, 0}, appearance,
                                                                     pdf::ops::AnnotationOps::objectReferences());
        if (!dictionary) return std::string();
        bodies.push_back(pdf::cos::serializeObject(dictionary.value()));
        annots += std::to_string(bodies.size()) + " 0 R ";
    }
    bodies[0] = "<< /Type /Catalog /Pages 2 0 R >>";
    bodies[1] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
    char box[96];
    std::snprintf(box, sizeof box, "[0 0 %g %g]", kAppearancePageWidth, kAppearancePageHeight);
    bodies[2] = "<< /Type /Page /Parent 2 0 R /MediaBox " + std::string(box) + " /Contents 4 0 R"
                + (rotate != 0 ? " /Rotate " + std::to_string(rotate) : std::string()) + " /Annots [" + annots + "] >>";

    std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + bodies[i] + "\nendobj\n";
    }
    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(bodies.size() + 1) + "\n0000000000 65535 f \n";
    for (const std::size_t offset : offsets) {
        char entry[24];
        std::snprintf(entry, sizeof entry, "%010zu 00000 n \n", offset);
        out += entry;
    }
    out += "trailer\n<< /Size " + std::to_string(bodies.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
           + std::to_string(xref) + "\n%%EOF\n";
    return out;
}

} // namespace rawpage::golden
