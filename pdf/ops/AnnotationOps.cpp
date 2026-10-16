// pdf/ops/AnnotationOps -- implementation. Stage 6 T3. See the header for the identity keys and the
// preservation rule.

#include "pdf/ops/AnnotationOps.h"

#include <QStringList>

#include <functional>
#include <vector>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;
using core::Annotation;

CosObject numberArray(const std::vector<double>& values)
{
    std::vector<CosObject> items;
    items.reserve(values.size());
    for (const double v : values) items.push_back(CosObject::makeReal(v));
    return CosObject::makeArray(std::move(items));
}

// Stage 6 T12: a measure dictionary as RawPage writes one (ISO 32000-1 Tables 261 and 262) -- `/Type`, `/Subtype`,
// `/R`, and each chain of number formats with `/U`, `/C` and `/D`, and the separators only where they are not ISO's
// defaults. A format the model marks not `plain` is never written by RawPage: the model holds it only as read.
CosObject numberFormat(const core::MeasureNumberFormat& f)
{
    std::vector<CosObject::Entry> entries{
        {"Type", CosObject::makeName("NumberFormat")},
        {"U", AnnotationOps::textString(f.unit)},
        {"C", CosObject::makeReal(f.factor)},
        {"D", CosObject::makeInteger(f.denominator)},
    };
    if (f.thousands != QLatin1String(",")) entries.push_back({"RT", AnnotationOps::textString(f.thousands)});
    if (f.decimal != QLatin1String(".")) entries.push_back({"RD", AnnotationOps::textString(f.decimal)});
    return CosObject::makeDictionary(std::move(entries));
}

CosObject measureDictionary(const core::MeasureDictionary& m)
{
    std::vector<CosObject::Entry> entries{
        {"Type", CosObject::makeName("Measure")},
        {"Subtype", CosObject::makeName(m.subtype.toStdString())},
        {"R", AnnotationOps::textString(m.ratio)},
    };
    const auto chain = [&entries](const char* key, const std::vector<core::MeasureNumberFormat>& formats) {
        if (formats.empty()) return;
        std::vector<CosObject> items;
        for (const core::MeasureNumberFormat& f : formats) items.push_back(numberFormat(f));
        entries.push_back({key, CosObject::makeArray(std::move(items))});
    };
    chain("X", m.x);
    chain("Y", m.y);
    chain("D", m.distance);
    chain("A", m.area);
    return CosObject::makeDictionary(std::move(entries));
}

// One modelled field: its key, and its value as written -- nullopt when the annotation does not set it.
// The SAME table writes a created dictionary and decides which keys an edit overwrites, so the two can
// never disagree about what a field means.
struct Field {
    const char* key;
    std::function<std::optional<CosObject>(const Annotation&, const ReferenceResolver&)> value;
};

// A reference field: the target resolved by the caller, or the key left out (see the header).
std::optional<CosObject> referenceIfAny(const std::optional<core::AnnotationId>& target, const ReferenceResolver& resolve)
{
    if (!target) return std::nullopt;
    const std::optional<cos::CosRef> at = resolve(*target);
    if (!at) return std::nullopt;
    return CosObject::makeReference(at->number, at->generation);
}

std::optional<CosObject> numbersIfAny(const std::vector<double>& values)
{
    if (values.empty()) return std::nullopt;
    return numberArray(values);
}

std::optional<CosObject> textIfAny(const QString& text)
{
    if (text.isEmpty()) return std::nullopt;
    return AnnotationOps::textString(text);
}

std::optional<CosObject> nameIfAny(const QString& name)
{
    if (name.isEmpty()) return std::nullopt;
    return CosObject::makeName(name.toStdString());
}

const std::vector<Field>& fields()
{
    static const std::vector<Field> table{
        {"Rect", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             return numberArray({a.rect.left(), a.rect.bottom(), a.rect.right(), a.rect.top()});
         }},
        {"QuadPoints", [](const Annotation& a, const ReferenceResolver&) { return numbersIfAny(a.quadPoints); }},
        {"Vertices", [](const Annotation& a, const ReferenceResolver&) { return numbersIfAny(a.vertices); }},
        {"L", [](const Annotation& a, const ReferenceResolver&) { return numbersIfAny(a.line); }},
        {"InkList", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (a.inkList.empty()) return std::nullopt;
             std::vector<CosObject> strokes;
             for (const std::vector<double>& stroke : a.inkList) strokes.push_back(numberArray(stroke));
             return CosObject::makeArray(std::move(strokes));
         }},
        {"CL", [](const Annotation& a, const ReferenceResolver&) { return numbersIfAny(a.callout); }},
        {"RD", [](const Annotation& a, const ReferenceResolver&) { return numbersIfAny(a.rectDifferences); }},
        {"LE", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             // A Line's /LE is an array of two names, a callout's one name (ISO 32000-1 Tables 176, 174).
             if (a.lineEndings.empty()) return std::nullopt;
             if (a.lineEndings.size() == 1) return CosObject::makeName(a.lineEndings.front().toStdString());
             std::vector<CosObject> names;
             for (const QString& ending : a.lineEndings) names.push_back(CosObject::makeName(ending.toStdString()));
             return CosObject::makeArray(std::move(names));
         }},
        {"C", [](const Annotation& a, const ReferenceResolver&) { return numbersIfAny(a.color); }},
        {"IC", [](const Annotation& a, const ReferenceResolver&) { return numbersIfAny(a.interiorColor); }},
        {"CA", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (!a.opacity) return std::nullopt;
             return CosObject::makeReal(*a.opacity);
         }},
        {"T", [](const Annotation& a, const ReferenceResolver&) { return textIfAny(a.author); }},
        {"Subj", [](const Annotation& a, const ReferenceResolver&) { return textIfAny(a.subject); }},
        {"Contents", [](const Annotation& a, const ReferenceResolver&) { return textIfAny(a.contents); }},
        {"NM", [](const Annotation& a, const ReferenceResolver&) { return textIfAny(a.nm); }},
        {"M", [](const Annotation& a, const ReferenceResolver&) { return textIfAny(a.modified.raw); }},
        {"CreationDate", [](const Annotation& a, const ReferenceResolver&) { return textIfAny(a.created.raw); }},
        {"F", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (a.flags == 0) return std::nullopt;
             return CosObject::makeInteger(static_cast<long long>(a.flags));
         }},
        {"IT", [](const Annotation& a, const ReferenceResolver&) { return nameIfAny(a.intent); }},
        {"Name", [](const Annotation& a, const ReferenceResolver&) { return nameIfAny(a.icon); }},
        {"DA", [](const Annotation& a, const ReferenceResolver&) { return textIfAny(a.defaultAppearance); }},
        {"RC", [](const Annotation& a, const ReferenceResolver&) { return textIfAny(a.richText); }},
        {"IRT", [](const Annotation& a, const ReferenceResolver& resolve) { return referenceIfAny(a.inReplyTo, resolve); }},
        {"RT", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (a.replyType != core::ReplyType::Group) return std::nullopt;   // /R is the default
             return CosObject::makeName("Group");
         }},
        // ISO 32000-1 Table 172: TEXT STRINGS, not names (Stage 6 guide I-03).
        {"StateModel", [](const Annotation& a, const ReferenceResolver&) { return textIfAny(a.stateModel); }},
        {"State", [](const Annotation& a, const ReferenceResolver&) { return textIfAny(a.state); }},
        {"Popup", [](const Annotation& a, const ReferenceResolver& resolve) { return referenceIfAny(a.popup, resolve); }},
        {"Parent", [](const Annotation& a, const ReferenceResolver& resolve) { return referenceIfAny(a.parent, resolve); }},
        {"Open", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (!a.open) return std::nullopt;
             return CosObject::makeBoolean(*a.open);
         }},
        {"Rotate", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (a.rotate == 0) return std::nullopt;
             return CosObject::makeInteger(a.rotate);
         }},
        // Stage 6 T7: free text's alignment, left (the default) written as nothing.
        {"Q", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (a.justification == 0) return std::nullopt;
             return CosObject::makeInteger(a.justification);
         }},
        // Stage 6 T8: a cloudy border (ISO 32000-1 Table 167) and the blend its appearance uses (ISO 32000-2
        // Table 166). `/BS` -- width, style and dash together -- is not a field: it is one dictionary three
        // modelled values share, written by `borderStyleDictionary` below.
        {"BE", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (!a.cloudIntensity) return std::nullopt;
             return CosObject::makeDictionary({{"S", CosObject::makeName("C")},
                                               {"I", CosObject::makeReal(*a.cloudIntensity)}});
         }},
        {"BM", [](const Annotation& a, const ReferenceResolver&) { return nameIfAny(a.blendMode); }},
        // Stage 6 T12 (D6-14): a line's leaders and caption (ISO 32000-1 Table 175), each at its default written as
        // nothing, and a measurement's `/Measure`. An EDIT rewrites `/Measure` whole only when the model's changed
        // -- otherwise the producer's keys the model does not hold (`/O`, `/T`, `/S`, `/CYX`) stay (D6-5).
        {"LL", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (a.leaderLength == 0.0) return std::nullopt;
             return CosObject::makeReal(a.leaderLength);
         }},
        {"LLE", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (a.leaderExtension == 0.0) return std::nullopt;
             return CosObject::makeReal(a.leaderExtension);
         }},
        {"LLO", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (a.leaderOffset == 0.0) return std::nullopt;
             return CosObject::makeReal(a.leaderOffset);
         }},
        {"Cap", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (!a.caption) return std::nullopt;
             return CosObject::makeBoolean(true);
         }},
        {"CP", [](const Annotation& a, const ReferenceResolver&) { return nameIfAny(a.captionPosition); }},
        {"CO", [](const Annotation& a, const ReferenceResolver&) { return numbersIfAny(a.captionOffset); }},
        {"Measure", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (!a.measure) return std::nullopt;
             return measureDictionary(*a.measure);
         }},
        // Stage 6 T14 (D6-25): a redaction mark's overlay text and repeat (ISO 32000-1 Table 190), each only on a mark
        // -- `/Repeat false`, the default, written as nothing. A `/Sound` is a STREAM, not a field: the sink writes it.
        {"OverlayText", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (a.subtype != core::AnnotationSubtype::Redact) return std::nullopt;
             return textIfAny(a.overlayText);
         }},
        {"Repeat", [](const Annotation& a, const ReferenceResolver&) -> std::optional<CosObject> {
             if (a.subtype != core::AnnotationSubtype::Redact || !a.repeat) return std::nullopt;
             return CosObject::makeBoolean(true);
         }},
    };
    return table;
}

// `/BS` as the model holds it, ON TOP OF `base` (the dictionary on disk, or none): `/W`, `/S` and `/D` set from
// `after` where they differ from `before` -- each written, or removed when the model no longer has it -- and
// every other key `base` carries kept (D6-5). Nullopt when nothing would be left in it.
std::optional<CosObject> borderStyleDictionary(const CosObject* base, const Annotation& before, const Annotation& after)
{
    CosObject bs = base != nullptr && base->type() == cos::CosType::Dictionary ? *base : CosObject::makeDictionary({});
    if (before.borderWidth != after.borderWidth) {
        bs = after.borderWidth ? AnnotationOps::withEntry(bs, "W", CosObject::makeReal(*after.borderWidth))
                               : AnnotationOps::withoutEntry(bs, "W");
    }
    if (before.borderStyle != after.borderStyle) {
        bs = !after.borderStyle.isEmpty() ? AnnotationOps::withEntry(bs, "S", CosObject::makeName(after.borderStyle.toStdString()))
                                          : AnnotationOps::withoutEntry(bs, "S");
    }
    if (before.dashPattern != after.dashPattern) {
        bs = !after.dashPattern.empty() ? AnnotationOps::withEntry(bs, "D", numberArray(after.dashPattern))
                                        : AnnotationOps::withoutEntry(bs, "D");
    }
    if (bs.entries().empty()) return std::nullopt;
    return bs;
}

QString escapeXml(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('&')) out += QLatin1String("&amp;");
        else if (c == QLatin1Char('<')) out += QLatin1String("&lt;");
        else if (c == QLatin1Char('>')) out += QLatin1String("&gt;");
        else if (c == QLatin1Char('"')) out += QLatin1String("&quot;");
        else out += c;
    }
    return out;
}

} // namespace

ReferenceResolver AnnotationOps::objectReferences()
{
    return [](const core::AnnotationId& id) -> std::optional<cos::CosRef> {
        if (id.kind != core::AnnotationId::Kind::Object) return std::nullopt;
        return cos::CosRef{id.object, 0};
    };
}

CosObject AnnotationOps::textString(const QString& text)
{
    bool ascii = true;
    for (const QChar c : text) {
        const char16_t u = c.unicode();
        // PDFDocEncoding agrees with ASCII on the printable range and on tab and the two line ends;
        // 0x18-0x1F and every byte from 0x7F up mean something else, so those go as UTF-16.
        if (!((u >= 0x20 && u < 0x7F) || u == 0x09 || u == 0x0A || u == 0x0D)) {
            ascii = false;
            break;
        }
    }
    if (ascii) return CosObject::makeString(text.toStdString());

    std::string bytes = "\xFE\xFF";
    for (const QChar c : text) {
        bytes += static_cast<char>((c.unicode() >> 8) & 0xFF);
        bytes += static_cast<char>(c.unicode() & 0xFF);
    }
    return CosObject::makeString(std::move(bytes), cos::StringForm::Hex);
}

core::Result<CosObject> AnnotationOps::createdDictionary(const Annotation& annotation, cos::CosRef page,
                                                         std::optional<cos::CosRef> appearance,
                                                         const ReferenceResolver& resolve)
{
    const QLatin1String known = core::annotationSubtypeName(annotation.subtype);
    const QString subtype = annotation.subtype == core::AnnotationSubtype::Unknown ? annotation.subtypeName
                                                                                   : QString(known);
    if (subtype.isEmpty() || !annotation.rectFinite)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pdf.appearanceGeometry"), subtype};

    std::vector<CosObject::Entry> entries{
        {"Type", CosObject::makeName("Annot")},
        {"Subtype", CosObject::makeName(subtype.toStdString())},
    };
    for (const Field& field : fields()) {
        if (std::optional<CosObject> value = field.value(annotation, resolve)) entries.push_back({field.key, std::move(*value)});
    }
    // `/BS`: the width, and -- since T8 -- the style and the dash array, each only where the model has it.
    if (std::optional<CosObject> bs = borderStyleDictionary(nullptr, Annotation{}, annotation))
        entries.push_back({"BS", std::move(*bs)});
    entries.push_back({"P", CosObject::makeReference(page.number, page.generation)});
    if (appearance) {
        entries.push_back({"AP", CosObject::makeDictionary(
                                     {{"N", CosObject::makeReference(appearance->number, appearance->generation)}})});
    }
    return CosObject::makeDictionary(std::move(entries));
}

core::Result<CosObject> AnnotationOps::exchangeDictionary(const Annotation& annotation, const ReferenceResolver& resolve)
{
    const QLatin1String known = core::annotationSubtypeName(annotation.subtype);
    const QString subtype = annotation.subtype == core::AnnotationSubtype::Unknown ? annotation.subtypeName
                                                                                   : QString(known);
    if (subtype.isEmpty() || !annotation.rectFinite)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pdf.appearanceGeometry"), subtype};

    std::vector<CosObject::Entry> entries{
        {"Type", CosObject::makeName("Annot")},
        {"Subtype", CosObject::makeName(subtype.toStdString())},
    };
    for (const Field& field : fields()) {
        if (std::optional<CosObject> value = field.value(annotation, resolve)) entries.push_back({field.key, std::move(*value)});
    }
    if (std::optional<CosObject> bs = borderStyleDictionary(nullptr, Annotation{}, annotation))
        entries.push_back({"BS", std::move(*bs)});
    entries.push_back({"Page", CosObject::makeInteger(annotation.page)});
    return CosObject::makeDictionary(std::move(entries));
}

CosObject AnnotationOps::editedDictionary(const CosObject& onDisk, const Annotation& before, const Annotation& after,
                                         const ReferenceResolver& resolve)
{
    CosObject edited = onDisk;
    for (const Field& field : fields()) {
        const std::optional<CosObject> was = field.value(before, resolve);
        const std::optional<CosObject> now = field.value(after, resolve);
        if (was == now) continue;   // unchanged: the key on disk stays exactly as it was written
        edited = now ? withEntry(edited, field.key, *now) : withoutEntry(edited, field.key);
    }

    // /RC follows /Contents when the edit did not set it, and only where the dictionary has one.
    if (before.contents != after.contents && before.richText == after.richText && edited.has("RC")) {
        edited = after.contents.isEmpty() ? withoutEntry(edited, "RC")
                                          : withEntry(edited, "RC", textString(richTextFor(after.contents)));
    }

    // The border lives inside /BS when there is one written inline; otherwise a /BS is added, which takes
    // precedence over a legacy /Border (ISO 32000-1 Table 164). Since T8 its style and dash array too.
    if (before.borderWidth != after.borderWidth || before.borderStyle != after.borderStyle
        || before.dashPattern != after.dashPattern) {
        const CosObject* bs = edited.find("BS");
        const CosObject* inline_ = bs != nullptr && bs->type() == cos::CosType::Dictionary ? bs : nullptr;
        if (std::optional<CosObject> rewritten = borderStyleDictionary(inline_, before, after))
            edited = withEntry(edited, "BS", std::move(*rewritten));
        else if (inline_ != nullptr)
            edited = withoutEntry(edited, "BS");
    }
    return edited;
}

QString AnnotationOps::richTextFor(const QString& contents)
{
    QString body;
    const QStringList lines = contents.split(QLatin1Char('\n'));
    for (const QString& line : lines) body += QStringLiteral("<p>") + escapeXml(line) + QStringLiteral("</p>");
    return QStringLiteral("<?xml version=\"1.0\"?><body xmlns=\"http://www.w3.org/1999/xhtml\" "
                          "xmlns:xfa=\"http://www.xfa.org/schema/xfa-data/1.0/\" xfa:spec=\"2.0.2\">")
           + body + QStringLiteral("</body>");
}

CosObject AnnotationOps::withEntry(const CosObject& dictionary, const std::string& key, CosObject value)
{
    std::vector<CosObject::Entry> entries;
    entries.reserve(dictionary.entries().size() + 1);
    bool replaced = false;
    for (const CosObject::Entry& entry : dictionary.entries()) {
        if (entry.key == key) {
            // The LAST occurrence is the one every reader resolves (`CosObject::find`); a duplicate
            // before it is dropped rather than left to disagree with the value written.
            continue;
        }
        entries.push_back(entry);
    }
    // In its place: re-insert where the last occurrence stood, so an edit moves no other key.
    std::size_t at = entries.size();
    std::size_t kept = 0;
    for (const CosObject::Entry& entry : dictionary.entries()) {
        if (entry.key == key) {
            at = kept;
            replaced = true;
        } else {
            ++kept;
        }
    }
    entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(replaced ? at : entries.size()),
                   CosObject::Entry{key, std::move(value)});
    if (dictionary.isStream()) return CosObject::makeStream(std::move(entries), dictionary.streamData());
    return CosObject::makeDictionary(std::move(entries));
}

CosObject AnnotationOps::withoutEntry(const CosObject& dictionary, const std::string& key)
{
    std::vector<CosObject::Entry> entries;
    entries.reserve(dictionary.entries().size());
    for (const CosObject::Entry& entry : dictionary.entries()) {
        if (entry.key != key) entries.push_back(entry);
    }
    if (dictionary.isStream()) return CosObject::makeStream(std::move(entries), dictionary.streamData());
    return CosObject::makeDictionary(std::move(entries));
}

} // namespace rawpage::pdf::ops
