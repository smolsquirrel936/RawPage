// pdf/ops/DecorationOps -- see the header.

#include "pdf/ops/DecorationOps.h"

#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/XrefTable.h"

#include <algorithm>
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

CosObject DecorationOps::markedDictionary(const QString& id, core::DecorationKind kind)
{
    return CosObject::makeDictionary(
        {{"RawPageDecoration", CosObject::makeDictionary({{"Kind", CosObject::makeName(core::decorationKindName(kind).data())},
                                                          {"Id", CosObject::makeString(id.toStdString(), cos::StringForm::Hex)}})}});
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
