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

CosObject reference(long long number)
{
    return CosObject::makeReference(number, 0);
}

// One modelled field: its key, and its value as written -- nullopt when the annotation does not set it.
// The SAME table writes a created dictionary and decides which keys an edit overwrites, so the two can
// never disagree about what a field means.
struct Field {
    const char* key;
    std::function<std::optional<CosObject>(const Annotation&)> value;
};

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
        {"Rect", [](const Annotation& a) -> std::optional<CosObject> {
             return numberArray({a.rect.left(), a.rect.bottom(), a.rect.right(), a.rect.top()});
         }},
        {"QuadPoints", [](const Annotation& a) { return numbersIfAny(a.quadPoints); }},
        {"Vertices", [](const Annotation& a) { return numbersIfAny(a.vertices); }},
        {"L", [](const Annotation& a) { return numbersIfAny(a.line); }},
        {"InkList", [](const Annotation& a) -> std::optional<CosObject> {
             if (a.inkList.empty()) return std::nullopt;
             std::vector<CosObject> strokes;
             for (const std::vector<double>& stroke : a.inkList) strokes.push_back(numberArray(stroke));
             return CosObject::makeArray(std::move(strokes));
         }},
        {"CL", [](const Annotation& a) { return numbersIfAny(a.callout); }},
        {"RD", [](const Annotation& a) { return numbersIfAny(a.rectDifferences); }},
        {"LE", [](const Annotation& a) -> std::optional<CosObject> {
             // A Line's /LE is an array of two names, a callout's one name (ISO 32000-1 Tables 176, 174).
             if (a.lineEndings.empty()) return std::nullopt;
             if (a.lineEndings.size() == 1) return CosObject::makeName(a.lineEndings.front().toStdString());
             std::vector<CosObject> names;
             for (const QString& ending : a.lineEndings) names.push_back(CosObject::makeName(ending.toStdString()));
             return CosObject::makeArray(std::move(names));
         }},
        {"C", [](const Annotation& a) { return numbersIfAny(a.color); }},
        {"IC", [](const Annotation& a) { return numbersIfAny(a.interiorColor); }},
        {"CA", [](const Annotation& a) -> std::optional<CosObject> {
             if (!a.opacity) return std::nullopt;
             return CosObject::makeReal(*a.opacity);
         }},
        {"T", [](const Annotation& a) { return textIfAny(a.author); }},
        {"Subj", [](const Annotation& a) { return textIfAny(a.subject); }},
        {"Contents", [](const Annotation& a) { return textIfAny(a.contents); }},
        {"NM", [](const Annotation& a) { return textIfAny(a.nm); }},
        {"M", [](const Annotation& a) { return textIfAny(a.modified.raw); }},
        {"CreationDate", [](const Annotation& a) { return textIfAny(a.created.raw); }},
        {"F", [](const Annotation& a) -> std::optional<CosObject> {
             if (a.flags == 0) return std::nullopt;
             return CosObject::makeInteger(static_cast<long long>(a.flags));
         }},
        {"IT", [](const Annotation& a) { return nameIfAny(a.intent); }},
        {"Name", [](const Annotation& a) { return nameIfAny(a.icon); }},
        {"DA", [](const Annotation& a) { return textIfAny(a.defaultAppearance); }},
        {"RC", [](const Annotation& a) { return textIfAny(a.richText); }},
        {"IRT", [](const Annotation& a) -> std::optional<CosObject> {
             if (!a.inReplyTo) return std::nullopt;
             return reference(*a.inReplyTo);
         }},
        {"RT", [](const Annotation& a) -> std::optional<CosObject> {
             if (a.replyType != core::ReplyType::Group) return std::nullopt;   // /R is the default
             return CosObject::makeName("Group");
         }},
        // ISO 32000-1 Table 172: TEXT STRINGS, not names (Stage 6 guide I-03).
        {"StateModel", [](const Annotation& a) { return textIfAny(a.stateModel); }},
        {"State", [](const Annotation& a) { return textIfAny(a.state); }},
        {"Popup", [](const Annotation& a) -> std::optional<CosObject> {
             if (!a.popup) return std::nullopt;
             return reference(*a.popup);
         }},
        {"Parent", [](const Annotation& a) -> std::optional<CosObject> {
             if (!a.parent) return std::nullopt;
             return reference(*a.parent);
         }},
        {"Open", [](const Annotation& a) -> std::optional<CosObject> {
             if (!a.open) return std::nullopt;
             return CosObject::makeBoolean(*a.open);
         }},
        {"Rotate", [](const Annotation& a) -> std::optional<CosObject> {
             if (a.rotate == 0) return std::nullopt;
             return CosObject::makeInteger(a.rotate);
         }},
    };
    return table;
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
                                                         std::optional<cos::CosRef> appearance)
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
        if (std::optional<CosObject> value = field.value(annotation)) entries.push_back({field.key, std::move(*value)});
    }
    if (annotation.borderWidth) {
        entries.push_back({"BS", CosObject::makeDictionary({{"W", CosObject::makeReal(*annotation.borderWidth)},
                                                            {"S", CosObject::makeName("S")}})});
    }
    entries.push_back({"P", CosObject::makeReference(page.number, page.generation)});
    if (appearance) {
        entries.push_back({"AP", CosObject::makeDictionary(
                                     {{"N", CosObject::makeReference(appearance->number, appearance->generation)}})});
    }
    return CosObject::makeDictionary(std::move(entries));
}

CosObject AnnotationOps::editedDictionary(const CosObject& onDisk, const Annotation& before, const Annotation& after)
{
    CosObject edited = onDisk;
    for (const Field& field : fields()) {
        const std::optional<CosObject> was = field.value(before);
        const std::optional<CosObject> now = field.value(after);
        if (was == now) continue;   // unchanged: the key on disk stays exactly as it was written
        edited = now ? withEntry(edited, field.key, *now) : withoutEntry(edited, field.key);
    }

    // /RC follows /Contents when the edit did not set it, and only where the dictionary has one.
    if (before.contents != after.contents && before.richText == after.richText && edited.has("RC")) {
        edited = after.contents.isEmpty() ? withoutEntry(edited, "RC")
                                          : withEntry(edited, "RC", textString(richTextFor(after.contents)));
    }

    // The border width lives inside /BS when there is one written inline; otherwise a /BS is added,
    // which takes precedence over a legacy /Border (ISO 32000-1 Table 164).
    if (before.borderWidth != after.borderWidth) {
        const CosObject* bs = edited.find("BS");
        if (!after.borderWidth) {
            if (bs != nullptr && bs->type() == cos::CosType::Dictionary)
                edited = withEntry(edited, "BS", withoutEntry(*bs, "W"));
        } else if (bs != nullptr && bs->type() == cos::CosType::Dictionary) {
            edited = withEntry(edited, "BS", withEntry(*bs, "W", CosObject::makeReal(*after.borderWidth)));
        } else {
            edited = withEntry(edited, "BS", CosObject::makeDictionary({{"W", CosObject::makeReal(*after.borderWidth)}}));
        }
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
