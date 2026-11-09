#include "pdf/ops/OutlineOps.h"

#include "pdf/ops/AnnotationOps.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <algorithm>

namespace rawpage::pdf::ops {

using cos::CosObject;
using core::Destination;
using core::OutlineAction;

namespace {

CosObject number(bool has, double value)
{
    return has ? CosObject::makeReal(value) : CosObject::makeNull();
}

// A file specification: the name as a string, as Acrobat writes one for a GoToR, a Launch and a SubmitForm.
CosObject fileSpecification(const QString& name, bool url)
{
    std::vector<CosObject::Entry> entries{{"Type", CosObject::makeName("Filespec")},
                                          {"F", AnnotationOps::textString(name)}};
    if (url) entries.insert(entries.begin() + 1, {"FS", CosObject::makeName("URL")});
    else entries.push_back({"UF", AnnotationOps::textString(name)});
    return CosObject::makeDictionary(std::move(entries));
}

// A Name made only of what PDF writes unescaped -- what a `Named` action names (`Print`, `NextPage` ...).
bool plainName(const QString& name)
{
    return !name.isEmpty() && name.size() <= 127 && std::all_of(name.begin(), name.end(), [](QChar c) {
        return (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
               || (c >= QLatin1Char('0') && c <= QLatin1Char('9'));
    });
}

core::Error refused(const char* what)
{
    return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.outline.actionUnwritable"), QString::fromLatin1(what)};
}

} // namespace

CosObject OutlineOps::destinationArray(CosObject page, const Destination& view)
{
    std::vector<CosObject> items{std::move(page)};
    switch (view.fit) {
    case Destination::Fit::XYZ:
        items.push_back(CosObject::makeName("XYZ"));
        items.push_back(number(view.hasX, view.x));
        items.push_back(number(view.hasY, view.y));
        items.push_back(number(view.hasZoom, view.zoom));
        break;
    case Destination::Fit::FitH:
    case Destination::Fit::FitBH:
        items.push_back(CosObject::makeName(view.fit == Destination::Fit::FitH ? "FitH" : "FitBH"));
        items.push_back(number(view.hasY, view.y));
        break;
    case Destination::Fit::FitV:
    case Destination::Fit::FitBV:
        items.push_back(CosObject::makeName(view.fit == Destination::Fit::FitV ? "FitV" : "FitBV"));
        items.push_back(number(view.hasX, view.x));
        break;
    case Destination::Fit::FitB:
        items.push_back(CosObject::makeName("FitB"));
        break;
    default:
        items.push_back(CosObject::makeName("Fit"));
        break;
    }
    return CosObject::makeArray(std::move(items));
}

core::Result<CosObject> OutlineOps::withTarget(CosObject dictionary, const OutlineAction& action, std::optional<CosObject> page)
{
    dictionary = AnnotationOps::withoutEntry(AnnotationOps::withoutEntry(dictionary, "Dest"), "A");
    const auto act = [](const char* type, std::vector<CosObject::Entry> entries) {
        entries.insert(entries.begin(), {"S", CosObject::makeName(type)});
        return CosObject::makeDictionary(std::move(entries));
    };
    switch (action.kind) {
    case OutlineAction::Kind::None:
        return dictionary;
    case OutlineAction::Kind::GoTo:
        if (!page) return dictionary;
        return AnnotationOps::withEntry(dictionary, "Dest", destinationArray(std::move(*page), action.destination));
    case OutlineAction::Kind::GoToNamed:
        if (action.name.isEmpty()) return refused("goToNamed");
        return AnnotationOps::withEntry(dictionary, "Dest", AnnotationOps::textString(action.name));
    case OutlineAction::Kind::GoToRemote: {
        if (action.file.isEmpty()) return refused("goToRemote");
        CosObject d = action.name.isEmpty()
                          ? destinationArray(CosObject::makeInteger(std::max(0, action.destination.pageIndex)), action.destination)
                          : AnnotationOps::textString(action.name);
        return AnnotationOps::withEntry(dictionary, "A",
                                        act("GoToR", {{"F", fileSpecification(action.file, false)}, {"D", std::move(d)}}));
    }
    case OutlineAction::Kind::Launch:
        if (action.file.isEmpty()) return refused("launch");
        return AnnotationOps::withEntry(dictionary, "A", act("Launch", {{"F", fileSpecification(action.file, false)}}));
    case OutlineAction::Kind::Uri: {
        // A URI is 7-bit ASCII (ISO 32000-1 §12.6.4.7): anything else is refused rather than written as something else.
        if (action.uri.isEmpty()) return refused("uri");
        for (const QChar c : action.uri) {
            if (c.unicode() < 0x20 || c.unicode() > 0x7E) return refused("uri");
        }
        return AnnotationOps::withEntry(dictionary, "A",
                                        act("URI", {{"URI", CosObject::makeString(action.uri.toStdString())}}));
    }
    case OutlineAction::Kind::JavaScript:
        return AnnotationOps::withEntry(dictionary, "A", act("JavaScript", {{"JS", AnnotationOps::textString(action.script)}}));
    case OutlineAction::Kind::Named:
        if (!plainName(action.name)) return refused("named");
        return AnnotationOps::withEntry(dictionary, "A", act("Named", {{"N", CosObject::makeName(action.name.toStdString())}}));
    case OutlineAction::Kind::SubmitForm:
        if (action.uri.isEmpty()) return refused("submitForm");
        return AnnotationOps::withEntry(dictionary, "A", act("SubmitForm", {{"F", fileSpecification(action.uri, true)}}));
    case OutlineAction::Kind::Other:
        break;
    }
    return refused("other");
}

std::optional<std::array<double, 3>> OutlineOps::colourOf(const CosObject& dictionary)
{
    const CosObject& c = dictionary.get("C");
    if (!c.isArray() || c.items().size() != 3) return std::nullopt;
    std::array<double, 3> rgb{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!c.items()[i].isNumber()) return std::nullopt;
        rgb[i] = std::clamp(c.items()[i].numberValue(), 0.0, 1.0);
    }
    return rgb;
}

long long OutlineOps::flagsOf(const CosObject& dictionary)
{
    const CosObject& f = dictionary.get("F");
    return f.isInteger() ? f.integerValue() : 0;
}

CosObject OutlineOps::withProperties(CosObject dictionary, const core::OutlineItem& item)
{
    const CosObject& title = dictionary.get("Title");
    if (!title.isString() || EmbeddedFileReader::decodeTextString(title.text()) != item.title)
        dictionary = AnnotationOps::withEntry(dictionary, "Title", AnnotationOps::textString(item.title));
    if (colourOf(dictionary) != item.colour) {
        if (item.colour) {
            dictionary = AnnotationOps::withEntry(
                dictionary, "C",
                CosObject::makeArray({CosObject::makeReal((*item.colour)[0]), CosObject::makeReal((*item.colour)[1]),
                                      CosObject::makeReal((*item.colour)[2])}));
        } else {
            dictionary = AnnotationOps::withoutEntry(dictionary, "C");
        }
    }
    const long long flags = flagsOf(dictionary);
    const long long want = (flags & ~3LL) | (item.italic ? 1 : 0) | (item.bold ? 2 : 0);
    if (want != flags) {
        dictionary = want == 0 ? AnnotationOps::withoutEntry(dictionary, "F")
                               : AnnotationOps::withEntry(dictionary, "F", CosObject::makeInteger(want));
    }
    return dictionary;
}

} // namespace rawpage::pdf::ops
