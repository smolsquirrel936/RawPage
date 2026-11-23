// pdf/ops/DecorationOps -- see the header.

#include "pdf/ops/DecorationOps.h"

#include "core/doc/Stamps.h"
#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/PageStamp.h"

#include <QJsonDocument>

#include <algorithm>
#include <array>
#include <set>

namespace rawpage::pdf::ops {

using cos::CosObject;
using cos::ContentStreamBuilder;

namespace {

CosObject resolved(cos::XrefTable& table, const CosObject& value)
{
    if (!value.isReference()) return value;
    auto fetched = table.fetch(value.reference().number);
    if (!fetched) return CosObject();
    CosObject out = fetched.value().object;
    out.clearSpansRecursively();
    return out;
}

CosObject boxArray(const core::PdfRect& box)
{
    return CosObject::makeArray({CosObject::makeReal(box.left()), CosObject::makeReal(box.bottom()),
                                 CosObject::makeReal(box.right()), CosObject::makeReal(box.top())});
}

} // namespace

std::string DecorationOps::resourceName(const QString& id)
{
    return std::string(kResourcePrefix) + id.toStdString();
}

bool DecorationOps::isDecorationResource(const std::string& name)
{
    return name.rfind(kResourcePrefix, 0) == 0;
}

std::string DecorationOps::formContent(const core::PageDecoration& decoration)
{
    ContentStreamBuilder content;
    for (const core::DecorationLine& line : decoration.lines) {
        if (line.runs.empty()) continue;
        const auto& m = line.matrix;
        content.save().concat(m[0], m[1], m[2], m[3], m[4], m[5]);
        content.fillColor(line.colour.data(), 3);
        content.beginText().textMatrix(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        std::optional<core::TypedFont> current;
        for (const core::TypedRun& run : line.runs) {
            if (current != run.font) {
                content.font(core::typedFontSpec(run.font).resource, line.size);
                current = run.font;
            }
            const std::string codes = AppearanceStreamBuilder::encodeText(run.font, run.text);
            if (!codes.empty()) content.showText(codes);
        }
        content.endText();
        if (line.underline && line.width > 0.0) {
            // Under the baseline by a tenth of the size, a twentieth thick -- Helvetica's own underline position.
            const double thickness = std::max(0.25, line.size / 20.0);
            content.rectangle(0.0, -line.size * 0.1 - thickness, line.width, thickness).fill();
        }
        content.restore();
    }
    return content.bytes();
}

CosObject DecorationOps::formDictionary(const core::PageDecoration& decoration, const QString& id, AppearanceObjects& objects)
{
    std::set<core::TypedFont> faces;
    for (const core::DecorationLine& line : decoration.lines) {
        for (const core::TypedRun& run : line.runs) faces.insert(run.font);
    }
    std::vector<CosObject::Entry> fonts;
    for (const core::TypedFont face : faces) {
        const cos::CosRef at = AppearanceStreamBuilder::fontObject(face, objects);
        fonts.push_back({core::typedFontSpec(face).resource, CosObject::makeReference(at.number, at.generation)});
    }
    std::vector<CosObject::Entry> resources;
    if (!fonts.empty()) resources.push_back({"Font", CosObject::makeDictionary(std::move(fonts))});
    const CosObject mark = markedDictionary(id, decoration.kind);
    std::vector<CosObject::Entry> entries{
        {"Type", CosObject::makeName("XObject")},
        {"Subtype", CosObject::makeName("Form")},
        {"BBox", boxArray(decoration.box)},
        {"Resources", CosObject::makeDictionary(std::move(resources))},
    };
    for (const CosObject::Entry& entry : mark.entries()) entries.push_back(entry);
    return CosObject::makeStream(std::move(entries), cos::CosStreamData{});
}

std::string DecorationOps::drawContent(const core::PageDecoration& decoration, const QString& id)
{
    std::string properties = "<< /Type /Pagination";
    if (!decoration.subtype.isEmpty()) properties += " /Subtype /" + decoration.subtype.toStdString();
    properties += " >>";
    return "/Artifact " + properties + " BDC q /" + resourceName(id) + " Do Q EMC\n";
}

CosObject DecorationOps::markedDictionary(const QString& id, core::DecorationKind kind, bool behind)
{
    std::vector<CosObject::Entry> mark{{"Kind", CosObject::makeName(core::decorationKindName(kind).data())},
                                       {"Id", CosObject::makeString(id.toStdString(), cos::StringForm::Hex)}};
    if (behind) mark.push_back({"Behind", CosObject::makeBoolean(true)});
    return CosObject::makeDictionary({{"RawPageDecoration", CosObject::makeDictionary(std::move(mark))}});
}

CosObject DecorationOps::decoratedResources(cos::XrefTable& table, const CosObject& effective,
                                            const std::vector<std::pair<std::string, CosObject>>& forms)
{
    std::vector<CosObject::Entry> entries;
    std::vector<CosObject::Entry> xobjects;
    bool hadXObject = false;
    if (effective.isDictionary()) {
        for (const CosObject::Entry& entry : effective.entries()) {
            if (entry.key != "XObject") {
                entries.push_back(entry);
                continue;
            }
            hadXObject = true;
            const CosObject named = resolved(table, entry.value);
            if (named.isDictionary()) {
                for (const CosObject::Entry& x : named.entries()) {
                    if (!isDecorationResource(x.key)) xobjects.push_back(x);
                }
            }
            entries.push_back({"XObject", CosObject()});   // its place, filled below
        }
    }
    for (const auto& [name, form] : forms) xobjects.push_back({name, form});
    if (!hadXObject) entries.push_back({"XObject", CosObject()});
    for (CosObject::Entry& entry : entries) {
        if (entry.key == "XObject") entry.value = CosObject::makeDictionary(xobjects);
    }
    CosObject out = CosObject::makeDictionary(std::move(entries));
    out.clearSpansRecursively();
    return out;
}

// ---- Watermarks (Stage 7 T15) -------------------------------------------------------------------------------------------

namespace {

std::array<double, 6> sourceMatrix(const core::WatermarkDrawing& drawing)
{
    // An image is drawn into the unit square: scaled to its natural box. A form is already in it.
    if (drawing.source == core::WatermarkSource::Image) return {drawing.natural.width, 0.0, 0.0, drawing.natural.height, 0.0, 0.0};
    return {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
}

std::string hexJson(const QJsonObject& json)
{
    return QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
}

} // namespace

core::Result<cos::CosRef> DecorationOps::watermarkSource(const core::WatermarkDrawing& drawing, const QString& id,
                                                         AppearanceObjects& objects)
{
    const CosObject naturalBox = CosObject::makeArray({CosObject::makeInteger(0), CosObject::makeInteger(0),
                                                       CosObject::makeReal(drawing.natural.width), CosObject::makeReal(drawing.natural.height)});
    switch (drawing.source) {
    case core::WatermarkSource::Image:
        if (core::invalidStampImage(drawing.image))
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.edit.stampImageUnsupported"), QStringLiteral("Watermark")};
        return AppearanceStreamBuilder::imageObject(drawing.image, objects);
    case core::WatermarkSource::Page: {
        core::Result<PageStamp::Form> form = PageStamp::form(drawing.page, objects);
        if (!form) return form.error();
        const std::array<double, 6> m =
            PageStamp::showMatrix(form.value().box, form.value().rotate, drawing.natural.width, drawing.natural.height);
        ContentStreamBuilder content;
        content.save().concat(m[0], m[1], m[2], m[3], m[4], m[5]).paintXObject("Pg").restore();
        // A TRANSPARENCY GROUP, so the watermark's opacity is applied to the page as one picture (see the header).
        CosObject dictionary = CosObject::makeStream(
            {{"Type", CosObject::makeName("XObject")},
             {"Subtype", CosObject::makeName("Form")},
             {"BBox", naturalBox},
             {"Group", CosObject::makeDictionary({{"S", CosObject::makeName("Transparency")}})},
             {"Resources", CosObject::makeDictionary({{"XObject", CosObject::makeDictionary({{"Pg", CosObject::makeReference(
                                                                                                        form.value().at.number,
                                                                                                        form.value().at.generation)}})}})}},
            cos::CosStreamData{});
        return objects.sharedStream("wmpage:" + core::stampPageKey(drawing.page).toStdString(), std::move(dictionary), content.bytes());
    }
    case core::WatermarkSource::Text: break;
    }
    // TEXT: the lines in the natural box, in D6-7's faces -- one form for every page this watermark is on.
    core::PageDecoration lines;
    lines.lines = drawing.lines;
    std::set<core::TypedFont> faces;
    for (const core::DecorationLine& line : drawing.lines) {
        for (const core::TypedRun& run : line.runs) faces.insert(run.font);
    }
    std::vector<CosObject::Entry> fonts;
    for (const core::TypedFont face : faces) {
        const cos::CosRef at = AppearanceStreamBuilder::fontObject(face, objects);
        fonts.push_back({core::typedFontSpec(face).resource, CosObject::makeReference(at.number, at.generation)});
    }
    CosObject dictionary = CosObject::makeStream({{"Type", CosObject::makeName("XObject")},
                                                  {"Subtype", CosObject::makeName("Form")},
                                                  {"BBox", naturalBox},
                                                  {"Resources", CosObject::makeDictionary({{"Font", CosObject::makeDictionary(std::move(fonts))}})}},
                                                 cos::CosStreamData{});
    return objects.sharedStream("wmtext:" + id.toStdString(), std::move(dictionary), formContent(lines));
}

std::string DecorationOps::watermarkContent(const core::WatermarkDrawing& drawing)
{
    ContentStreamBuilder content;
    content.graphicsState(kStateResource);
    const std::array<double, 6> s = sourceMatrix(drawing);
    for (const std::array<double, 6>& m : drawing.tiles) {
        content.save().concat(m[0], m[1], m[2], m[3], m[4], m[5]);
        if (s != std::array<double, 6>{1.0, 0.0, 0.0, 1.0, 0.0, 0.0}) content.concat(s[0], s[1], s[2], s[3], s[4], s[5]);
        content.paintXObject(kSourceResource).restore();
    }
    return content.bytes();
}

CosObject DecorationOps::watermarkFormDictionary(const core::PageDecoration& decoration, const QString& id, const CosObject& source,
                                                 const std::optional<CosObject>& group)
{
    const double opacity = decoration.watermark ? decoration.watermark->opacity : 1.0;
    const CosObject state = CosObject::makeDictionary(
        {{"Type", CosObject::makeName("ExtGState")}, {"ca", CosObject::makeReal(opacity)}, {"CA", CosObject::makeReal(opacity)}});
    std::vector<CosObject::Entry> entries{
        {"Type", CosObject::makeName("XObject")},
        {"Subtype", CosObject::makeName("Form")},
        {"BBox", boxArray(decoration.box)},
        {"Resources", CosObject::makeDictionary({{"ExtGState", CosObject::makeDictionary({{kStateResource, state}})},
                                                 {"XObject", CosObject::makeDictionary({{kSourceResource, source}})}})},
    };
    if (group) entries.push_back({"OC", *group});
    std::vector<CosObject::Entry> mark{{"Kind", CosObject::makeName(core::decorationKindName(core::DecorationKind::Watermark).data())},
                                       {"Id", CosObject::makeString(id.toStdString(), cos::StringForm::Hex)}};
    if (decoration.watermark) mark.push_back({"Spec", CosObject::makeString(hexJson(decoration.watermark->spec), cos::StringForm::Hex)});
    entries.push_back({"RawPageDecoration", CosObject::makeDictionary(std::move(mark))});
    return CosObject::makeStream(std::move(entries), cos::CosStreamData{});
}

CosObject DecorationOps::groupDictionary(const QString& id, core::WatermarkUsage usage)
{
    const bool print = usage != core::WatermarkUsage::ScreenOnly;
    const bool view = usage != core::WatermarkUsage::PrintOnly;
    return CosObject::makeDictionary(
        {{"Type", CosObject::makeName("OCG")},
         {"Name", CosObject::makeString("Watermark")},
         {"Usage", CosObject::makeDictionary(
                       {{"Print", CosObject::makeDictionary({{"PrintState", CosObject::makeName(print ? "ON" : "OFF")}})},
                        {"View", CosObject::makeDictionary({{"ViewState", CosObject::makeName(view ? "ON" : "OFF")}})}})},
         {"RawPageDecoration", CosObject::makeDictionary({{"Kind", CosObject::makeName(core::decorationKindName(core::DecorationKind::Watermark).data())},
                                                          {"Id", CosObject::makeString(id.toStdString(), cos::StringForm::Hex)}})}});
}

std::optional<QJsonObject> DecorationOps::recordedSpec(const CosObject& form)
{
    const CosObject* mark = form.find("RawPageDecoration");
    if (mark == nullptr || !mark->isDictionary()) return std::nullopt;
    const CosObject* spec = mark->find("Spec");
    if (spec == nullptr || !spec->isString()) return std::nullopt;
    QJsonParseError error{};
    const QJsonDocument json = QJsonDocument::fromJson(QByteArray::fromStdString(spec->text()), &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) return std::nullopt;
    return json.object();
}

std::optional<QString> DecorationOps::markedId(const CosObject& dictionary)
{
    const CosObject* mark = dictionary.find("RawPageDecoration");
    if (mark == nullptr || !mark->isDictionary()) return std::nullopt;
    const CosObject* id = mark->find("Id");
    if (id == nullptr || !id->isString()) return std::nullopt;
    return QString::fromStdString(id->text());
}

std::optional<CosObject> DecorationOps::formOf(cos::XrefTable& table, const CosObject& resources, const QString& id)
{
    const CosObject dictionary = resolved(table, resources);
    if (!dictionary.isDictionary()) return std::nullopt;
    const CosObject xobjects = resolved(table, dictionary.get("XObject"));
    if (!xobjects.isDictionary()) return std::nullopt;
    const CosObject* form = xobjects.find(resourceName(id));
    if (!form) return std::nullopt;
    CosObject out = *form;
    out.clearSpansRecursively();
    return out;
}

} // namespace rawpage::pdf::ops
