// pdf/ops/ImpositionOps -- implementation. Stage 7 T16 (D7-8, D7-46). The header says what is copied, what is drawn
// and what is not.

#include "pdf/ops/ImpositionOps.h"

#include "core/doc/TypedText.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/DocumentAssembler.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/PageStamp.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;

core::Error pageError(int page, const QString& what)
{
    return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.imposition.pageUnsupported"),
                       QStringLiteral("%1:%2").arg(page + 1).arg(what)};
}

// A number as content writes it: at most five decimals, no trailing zeros.
std::string num(double value)
{
    if (!std::isfinite(value) || std::fabs(value) < 5e-6) return "0";
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.5f", value);
    std::string out(buffer);
    while (!out.empty() && out.back() == '0') out.pop_back();
    if (!out.empty() && out.back() == '.') out.pop_back();
    return out;
}

std::string rect(const core::PdfRect& r)
{
    return num(r.left()) + ' ' + num(r.bottom()) + ' ' + num(r.size.width) + ' ' + num(r.size.height) + " re";
}

std::string hexOf(const std::string& bytes)
{
    static const char* digits = "0123456789ABCDEF";
    std::string out = "<";
    for (const unsigned char c : bytes) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out + '>';
}

core::PdfRect clipped(const core::PdfRect& a, const core::PdfRect& b)
{
    const double left = std::max(a.left(), b.left());
    const double bottom = std::max(a.bottom(), b.bottom());
    const double right = std::min(a.right(), b.right());
    const double top = std::min(a.top(), b.top());
    return core::PdfRect{core::PdfPoint{left, bottom}, core::PdfSize{std::max(0.0, right - left), std::max(0.0, top - bottom)}};
}

// One object of the payload: its value, and a stream's bytes as written.
struct Made {
    CosObject value;
    std::optional<std::string> data;
};

// What the forms, fonts and sides are written through: numbered from `next`, each key once, a reserved key written later.
class Objects final : public AppearanceObjects {
public:
    Objects(std::map<long long, Made>& into, long long next) : into_(into), next_(next) {}
    cos::CosRef shared(const std::string& key, CosObject object) override { return put(key, std::move(object), std::nullopt); }
    cos::CosRef sharedStream(const std::string& key, CosObject dictionary, std::string data) override
    {
        return put(key, std::move(dictionary), std::move(data));
    }
    cos::CosRef reserved(const std::string& key) override
    {
        const auto found = keys_.find(key);
        if (found != keys_.end()) return cos::CosRef{found->second, 0};
        const long long number = next_++;
        keys_.emplace(key, number);
        return cos::CosRef{number, 0};
    }
    // Every number handed out has been written.
    bool complete() const { return static_cast<long long>(into_.size()) >= next_ - 1; }
    long long next() const noexcept { return next_; }

private:
    cos::CosRef put(const std::string& key, CosObject object, std::optional<std::string> data)
    {
        const cos::CosRef at = reserved(key);
        if (into_.count(at.number) == 0) into_[at.number] = Made{std::move(object), std::move(data)};
        return at;
    }
    std::map<long long, Made>& into_;
    std::map<std::string, long long> keys_;
    long long next_;
};

// The payload written as a PDF: every object of `made`, numbered from 1 with no gap, one classic section.
core::Result<std::string> written(const std::map<long long, Made>& made)
{
    std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    long long expected = 1;
    for (const auto& [number, object] : made) {
        if (number != expected++)
            return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.assembly.verifyFailed"), QStringLiteral("numbering")};
        offsets.push_back(out.size());
        out += cos::serializeIndirect(number, 0, object.value, object.data);
        if (out.size() > static_cast<std::size_t>(core::kMaxCopiedGraphBytes) * 4)
            return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.copy.tooLarge"), QString()};
    }
    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(made.size() + 1) + "\n0000000000 65535 f\r\n";
    char row[24];
    for (const std::size_t offset : offsets) {
        std::snprintf(row, sizeof row, "%010llu 00000 n\r\n", static_cast<unsigned long long>(offset));
        out += row;
    }
    out += "trailer\n<< /Size " + std::to_string(made.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return out;
}

// The colour every mark is drawn in: REGISTRATION -- all of the device's colorants (ISO 32000-1 8.6.6.4, `/All`).
CosObject registrationSpace()
{
    return CosObject::makeArray({
        CosObject::makeName("Separation"),
        CosObject::makeName("All"),
        CosObject::makeName("DeviceCMYK"),
        CosObject::makeDictionary({
            {"FunctionType", CosObject::makeInteger(2)},
            {"Domain", CosObject::makeArray({CosObject::makeInteger(0), CosObject::makeInteger(1)})},
            {"C0", CosObject::makeArray({CosObject::makeInteger(0), CosObject::makeInteger(0), CosObject::makeInteger(0), CosObject::makeInteger(0)})},
            {"C1", CosObject::makeArray({CosObject::makeInteger(1), CosObject::makeInteger(1), CosObject::makeInteger(1), CosObject::makeInteger(1)})},
            {"N", CosObject::makeInteger(1)},
        }),
    });
}

constexpr const char* kRegistrationResource = "RawPageAll";

// The colour bar's patches, as CMYK.
constexpr std::array<std::array<double, 4>, core::kColourPatches> kPatches{{
    {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {1, 1, 0, 0},
    {1, 0, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0.75}, {0, 0, 0, 0.5}, {0, 0, 0, 0.25},
}};

// A circle of radius `r` about `c` and a cross through it, stroked.
std::string registrationMark(core::PdfPoint c, double r)
{
    const double k = 0.5523 * r;
    std::string out;
    out += num(c.x + r) + ' ' + num(c.y) + " m\n";
    out += num(c.x + r) + ' ' + num(c.y + k) + ' ' + num(c.x + k) + ' ' + num(c.y + r) + ' ' + num(c.x) + ' ' + num(c.y + r) + " c\n";
    out += num(c.x - k) + ' ' + num(c.y + r) + ' ' + num(c.x - r) + ' ' + num(c.y + k) + ' ' + num(c.x - r) + ' ' + num(c.y) + " c\n";
    out += num(c.x - r) + ' ' + num(c.y - k) + ' ' + num(c.x - k) + ' ' + num(c.y - r) + ' ' + num(c.x) + ' ' + num(c.y - r) + " c\n";
    out += num(c.x + k) + ' ' + num(c.y - r) + ' ' + num(c.x + r) + ' ' + num(c.y - k) + ' ' + num(c.x + r) + ' ' + num(c.y) + " c S\n";
    const double arm = 1.6 * r;
    out += num(c.x - arm) + ' ' + num(c.y) + " m " + num(c.x + arm) + ' ' + num(c.y) + " l S\n";
    out += num(c.x) + ' ' + num(c.y - arm) + " m " + num(c.x) + ' ' + num(c.y + arm) + " l S\n";
    return out;
}

} // namespace

core::Result<ImpositionOps::Payload> ImpositionOps::payload(cos::XrefTable& source, const PageWalk& walk, const core::ImpositionSpec& spec,
                                                            const core::ImpositionText& text)
{
    const int count = static_cast<int>(walk.pages.size());
    if (count == 0) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.imposition.noPages"), QString()};
    if (count > core::kMaxImposedPages)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.imposition.tooManyPages"), QString::number(count)};

    // THE PLAN, from what each page SHOWS.
    std::vector<core::PdfSize> shown;
    shown.reserve(static_cast<std::size_t>(count));
    for (int at = 0; at < count; ++at) {
        const std::optional<PageStamp::Shown> page = PageStamp::shown(walk.pages[static_cast<std::size_t>(at)]);
        if (!page) return pageError(at, QStringLiteral("box"));
        shown.push_back(page->size);
    }
    auto planned = core::planImposition(spec, shown);
    if (!planned) return planned.error();
    Payload out;
    out.plan = std::move(planned).value();
    const core::ImpositionPlan& plan = out.plan;

    // EVERY PAGE COPIED ONCE, then read back as the payload it is.
    std::vector<int> all(static_cast<std::size_t>(count));
    std::iota(all.begin(), all.end(), 0);
    auto copied = ObjectGraphCopier::copy(source, walk, all);
    if (!copied) return copied.error();
    const std::string& graph = copied.value().pdf;
    auto loaded = cos::XrefTable::load(graph);
    if (!loaded) return loaded.error();
    cos::XrefTable& table = loaded.value();
    auto pages = PageOps::walk(table);
    if (!pages || !pages.value().notes.empty() || static_cast<int>(pages.value().pages.size()) != count)
        return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.assembly.verifyFailed"), QStringLiteral("payload")};
    std::set<long long> frame{1, 2};
    for (const PageEntry& entry : pages.value().pages) frame.insert(entry.number);
    // The annotations left behind, counted in the SOURCE: the copier itself leaves widgets behind (D7-4).
    for (const PageEntry& entry : walk.pages) {
        if (auto fetched = source.fetch(entry.number)) {
            CosObject annots = fetched.value().object.get("Annots");
            if (annots.isReference()) {
                auto resolved = source.fetch(annots.reference().number);
                annots = resolved ? resolved.value().object : CosObject::makeNull();
            }
            if (annots.isArray()) out.annotationsNotDrawn += static_cast<int>(annots.items().size());
        }
    }

    const long long sides = static_cast<long long>(plan.sides.size());
    std::map<long long, Made> made;
    Objects objects(made, 3 + sides);

    // EACH PAGE ONE FORM, what the pages share written once.
    std::vector<PageStamp::Form> forms;
    forms.reserve(static_cast<std::size_t>(count));
    for (int at = 0; at < count; ++at) {
        auto form = PageStamp::pageForm(table, pages.value().pages[static_cast<std::size_t>(at)], frame, "impose:", objects);
        if (!form) return pageError(at, form.error().context);
        forms.push_back(form.value());
    }
    out.forms = count;

    const bool marks = spec.cropMarks || spec.registrationMarks;
    std::optional<cos::CosRef> registration;
    if (marks) registration = objects.shared("impose:registration", registrationSpace());

    // THE LABELS, laid out first: a character no face can show refuses the imposition before anything is drawn.
    std::vector<std::vector<core::TypedRun>> labels(plan.sides.size());
    std::vector<CosObject::Entry> fontResources;
    if (spec.sheetLabels) {
        std::set<core::TypedFont> faces;
        for (std::size_t k = 0; k < plan.sides.size(); ++k) {
            const QString label = core::sheetLabel(text.labels, spec, plan.sides[k], plan.sheets);
            auto laid = core::layoutTypedText(label, core::kSheetLabelSize, std::nullopt, text.han);
            if (!laid) return laid.error();
            labels[k] = laid.value().lines.front().runs;
            for (const core::TypedRun& run : labels[k]) faces.insert(run.font);
        }
        for (const core::TypedFont face : faces) {
            const cos::CosRef at = AppearanceStreamBuilder::fontObject(face, objects);
            fontResources.push_back({core::typedFontSpec(face).resource, CosObject::makeReference(at.number, at.generation)});
        }
    }

    // EACH SIDE A PAGE.
    const double width = plan.sheet.width;
    const double height = plan.sheet.height;
    for (std::size_t k = 0; k < plan.sides.size(); ++k) {
        const core::ImposedSide& side = plan.sides[k];
        std::string content = "q\n";
        if (side.turned) content += "-1 0 0 -1 " + num(width) + ' ' + num(height) + " cm\n";
        std::vector<CosObject::Entry> xobjects;
        for (const core::ImposedCell& cell : side.cells) {
            if (cell.page < 0) continue;
            const PageStamp::Form& form = forms[static_cast<std::size_t>(cell.page)];
            const std::string name = "P" + std::to_string(cell.page);
            xobjects.push_back({name, CosObject::makeReference(form.at.number, form.at.generation)});
            const std::array<double, 6> m = PageStamp::showMatrix(form.box, form.rotate, cell.placed.size.width, cell.placed.size.height);
            content += "q " + rect(cell.cell) + " W n\n";
            content += num(m[0]) + ' ' + num(m[1]) + ' ' + num(m[2]) + ' ' + num(m[3]) + ' ' + num(m[4] + cell.placed.left()) + ' '
                       + num(m[5] + cell.placed.bottom()) + " cm /" + name + " Do Q\n";
        }
        if (spec.frames) {
            content += "q 0 G 0.5 w\n";
            for (const core::ImposedCell& cell : side.cells) {
                if (cell.page >= 0) content += rect(clipped(cell.placed, cell.cell)) + " S\n";
            }
            content += "Q\n";
        }
        if (registration) {
            content += std::string("q /") + kRegistrationResource + " CS 1 SCN 0.25 w\n";
            for (const core::ImposedLine& line : side.cropMarks)
                content += num(line.from.x) + ' ' + num(line.from.y) + " m " + num(line.to.x) + ' ' + num(line.to.y) + " l S\n";
            for (const core::PdfPoint& centre : plan.registration) content += registrationMark(centre, core::kRegistrationRadius);
            content += "Q\n";
        }
        if (spec.colourBar) {
            for (int p = 0; p < core::kColourPatches; ++p) {
                const std::array<double, 4>& c = kPatches[static_cast<std::size_t>(p)];
                const core::PdfRect patch{core::PdfPoint{plan.colourBar.left() + p * core::kColourPatch, plan.colourBar.bottom()},
                                          core::PdfSize{core::kColourPatch, core::kColourPatch}};
                content += num(c[0]) + ' ' + num(c[1]) + ' ' + num(c[2]) + ' ' + num(c[3]) + " k " + rect(patch) + " f\n";
            }
        }
        if (spec.sheetLabels) {
            content += "q 0 g\n";
            double x = plan.labelOrigin.x;
            for (const core::TypedRun& run : labels[k]) {
                const std::string codes = AppearanceStreamBuilder::encodeText(run.font, run.text);
                if (!codes.empty())
                    content += "BT /" + std::string(core::typedFontSpec(run.font).resource) + ' ' + num(core::kSheetLabelSize) + " Tf "
                               + num(x) + ' ' + num(plan.labelOrigin.y) + " Td " + hexOf(codes) + " Tj ET\n";
                x += run.width;
            }
            content += "Q\n";
        }
        content += "Q\n";

        std::vector<CosObject::Entry> resources;
        if (!xobjects.empty()) resources.push_back({"XObject", CosObject::makeDictionary(std::move(xobjects))});
        if (registration)
            resources.push_back({"ColorSpace", CosObject::makeDictionary({{kRegistrationResource,
                                                                           CosObject::makeReference(registration->number, registration->generation)}})});
        if (!fontResources.empty()) resources.push_back({"Font", CosObject::makeDictionary(fontResources)});
        const cos::CosRef stream = objects.sharedStream("impose:side:" + std::to_string(k),
                                                        CosObject::makeStream(std::vector<CosObject::Entry>{}, cos::CosStreamData{}), std::move(content));
        made[3 + static_cast<long long>(k)] = Made{
            CosObject::makeDictionary({
                {"Type", CosObject::makeName("Page")},
                {"Parent", CosObject::makeReference(2, 0)},
                {"MediaBox", CosObject::makeArray({CosObject::makeInteger(0), CosObject::makeInteger(0), CosObject::makeReal(width),
                                                   CosObject::makeReal(height)})},
                {"Resources", CosObject::makeDictionary(std::move(resources))},
                {"Contents", CosObject::makeReference(stream.number, stream.generation)},
                {"Rotate", CosObject::makeInteger(0)},
            }),
            std::nullopt};
    }

    // THE FRAME: the catalog and the one node -- `DocumentAssembler::build` balances it.
    std::vector<CosObject> kids;
    for (long long k = 0; k < sides; ++k) kids.push_back(CosObject::makeReference(3 + k, 0));
    made[1] = Made{CosObject::makeDictionary({{"Type", CosObject::makeName("Catalog")}, {"Pages", CosObject::makeReference(2, 0)}}), std::nullopt};
    made[2] = Made{CosObject::makeDictionary({{"Type", CosObject::makeName("Pages")},
                                              {"Kids", CosObject::makeArray(std::move(kids))},
                                              {"Count", CosObject::makeInteger(sides)}}),
                   std::nullopt};
    if (!objects.complete() || static_cast<long long>(made.size()) != objects.next() - 1)
        return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.assembly.verifyFailed"), QStringLiteral("reserved")};
    out.objects = static_cast<long long>(made.size()) - 2;
    auto pdf = written(made);
    if (!pdf) return pdf.error();
    out.pdf = std::move(pdf).value();
    return out;
}

core::Result<core::ImposedDocument> ImpositionOps::impose(cos::XrefTable& source, const PageWalk& walk, const core::ImpositionSpec& spec,
                                                          const core::ImpositionText& text, std::string_view idSeed)
{
    auto made = payload(source, walk, spec, text);
    if (!made) return made.error();
    auto built = DocumentAssembler::build(made.value().pdf, idSeed);
    if (!built) return built.error();
    if (core::Status verified = DocumentAssembler::verify(built.value(), made.value().pdf); !verified) return verified.error();
    core::ImposedDocument out;
    out.bytes = QByteArray(built.value().data(), static_cast<qsizetype>(built.value().size()));
    out.pageCount = static_cast<int>(made.value().plan.sides.size());
    out.sheets = made.value().plan.sheets;
    out.sourcePages = static_cast<int>(walk.pages.size());
    out.blanks = made.value().plan.pagesPadded - out.sourcePages;
    out.annotationsNotDrawn = made.value().annotationsNotDrawn;
    out.plan = std::move(made.value().plan);
    return out;
}

} // namespace rawpage::pdf::ops
