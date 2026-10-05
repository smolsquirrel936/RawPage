// pdf/ops/AnnotationReader -- implementation. Stage 6 T2. See the header for why first-party, and
// for what is tolerated and bounded.

#include "pdf/ops/AnnotationReader.h"

#include "pdf/cos/CosObject.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;
using core::Annotation;
using core::AnnotationNote;
using core::AnnotationNoteKind;

// A value that may be written inline or as an indirect reference. The fetched object is kept in
// `holder`, which must outlive the pointer.
const CosObject* resolve(cos::XrefTable& table, const CosObject* value,
                         std::optional<cos::IndirectObject>& holder)
{
    if (value == nullptr) return nullptr;
    if (!value->isReference()) return value;
    auto fetched = table.fetch(value->reference().number);
    if (!fetched) return nullptr;
    holder = std::move(fetched).value();
    return &holder->object;
}

std::optional<long long> referenceOf(const CosObject& dictionary, std::string_view key)
{
    const CosObject* value = dictionary.find(key);
    if (value == nullptr || !value->isReference()) return std::nullopt;
    return value->reference().number;
}

// A text string as Unicode -- or, leniently, a NAME written where ISO 32000-1 asks for a text
// string, which producers do for `/StateModel` and `/State`.
QString textOf(const CosObject& dictionary, std::string_view key)
{
    const CosObject* value = dictionary.find(key);
    if (value == nullptr) return QString();
    if (value->isString()) return EmbeddedFileReader::decodeTextString(value->text());
    if (value->isName()) return QString::fromStdString(value->text());
    return QString();
}

QString nameOf(const CosObject& dictionary, std::string_view key)
{
    const CosObject* value = dictionary.find(key);
    if (value == nullptr || !value->isName()) return QString();
    return QString::fromStdString(value->text());
}

// The numbers of an array value, at most `budget` of them; `budget` is spent and `truncated` set
// when the array held more. Non-numbers are skipped, as every reader skips them.
std::vector<double> numbersOf(cos::XrefTable& table, const CosObject* value, std::size_t& budget,
                              bool& truncated)
{
    std::vector<double> out;
    std::optional<cos::IndirectObject> holder;
    const CosObject* array = resolve(table, value, holder);
    if (array == nullptr || !array->isArray()) return out;
    for (const CosObject& item : array->items()) {
        // A non-finite number -- PDF syntax has no NaN, but a real too long for a double overflows to
        // infinity -- is skipped as a non-number is: geometry at infinity is no geometry.
        if (!item.isNumber() || !std::isfinite(item.numberValue())) continue;
        if (budget == 0) { truncated = true; break; }
        out.push_back(item.numberValue());
        --budget;
    }
    return out;
}

// Small arrays a reader must not bound by the geometry budget: a rectangle, a colour, a line.
std::vector<double> smallNumbersOf(cos::XrefTable& table, const CosObject& dictionary,
                                   std::string_view key)
{
    std::size_t budget = 16;
    bool truncated = false;
    return numbersOf(table, dictionary.find(key), budget, truncated);
}

struct Parsed {
    Annotation annotation;
    core::AnnotationFact fact;
    std::optional<long long> pageReference;   // its /P
    bool nonFiniteRect = false;
    bool truncated = false;
};

Parsed parse(cos::XrefTable& table, const CosObject& d)
{
    Parsed out;
    Annotation& a = out.annotation;
    out.fact.dictionary = true;

    const std::string subtype = d.find("Subtype") != nullptr && d.get("Subtype").isName()
                                    ? d.get("Subtype").text()
                                    : std::string();
    a.subtype = core::annotationSubtypeFromName(subtype);
    if (a.subtype == core::AnnotationSubtype::Unknown) a.subtypeName = QString::fromStdString(subtype);
    out.fact.subtype = a.subtype;

    // `/Rect` AS PDFIUM READS IT, because the two readings are compared: an array of exactly four
    // entries, a non-number among them read as 0, and anything else -- absent, three numbers, five --
    // the empty rectangle at the origin. Reading a malformed one more cleverly than the engine would
    // make every such page disagree with what is drawn.
    std::array<double, 4> rect{0.0, 0.0, 0.0, 0.0};
    {
        std::optional<cos::IndirectObject> holder;
        const CosObject* array = resolve(table, d.find("Rect"), holder);
        if (array != nullptr && array->isArray() && array->items().size() == 4) {
            for (std::size_t i = 0; i < 4; ++i)
                rect[i] = array->items()[i].isNumber() ? array->items()[i].numberValue() : 0.0;
        }
    }
    if (const std::optional<core::PdfRect> normal =
            core::normalizedRect(rect[0], rect[1], rect[2], rect[3])) {
        a.rect = *normal;
        out.fact.rect = *normal;
    } else {
        a.rectFinite = false;
        out.nonFiniteRect = true;
    }

    std::size_t budget = AnnotationReader::kMaxCoordinates;
    a.quadPoints = numbersOf(table, d.find("QuadPoints"), budget, out.truncated);
    a.vertices = numbersOf(table, d.find("Vertices"), budget, out.truncated);
    {
        std::optional<cos::IndirectObject> holder;
        const CosObject* ink = resolve(table, d.find("InkList"), holder);
        if (ink != nullptr && ink->isArray()) {
            for (const CosObject& stroke : ink->items()) {
                if (budget == 0) { out.truncated = true; break; }
                std::vector<double> samples = numbersOf(table, &stroke, budget, out.truncated);
                a.inkList.push_back(std::move(samples));
            }
        }
    }
    a.line = smallNumbersOf(table, d, "L");
    a.callout = smallNumbersOf(table, d, "CL");
    a.rectDifferences = smallNumbersOf(table, d, "RD");
    a.color = smallNumbersOf(table, d, "C");
    a.interiorColor = smallNumbersOf(table, d, "IC");

    if (const CosObject* ends = d.find("LE")) {
        if (ends->isName()) a.lineEndings.push_back(QString::fromStdString(ends->text()));
        else if (ends->isArray()) {
            for (const CosObject& end : ends->items()) {
                if (end.isName()) a.lineEndings.push_back(QString::fromStdString(end.text()));
            }
        }
    }

    if (const CosObject* opacity = d.find("CA");
        opacity != nullptr && opacity->isNumber() && std::isfinite(opacity->numberValue()))
        a.opacity = opacity->numberValue();
    {
        std::optional<cos::IndirectObject> holder;
        const CosObject* bs = resolve(table, d.find("BS"), holder);
        if (bs != nullptr && bs->isDictionary()) {
            if (const CosObject* w = bs->find("W");
                w != nullptr && w->isNumber() && std::isfinite(w->numberValue()))
                a.borderWidth = w->numberValue();
        }
        if (!a.borderWidth) {
            const std::vector<double> border = smallNumbersOf(table, d, "Border");
            if (border.size() >= 3) a.borderWidth = border[2];
        }
    }

    a.nm = textOf(d, "NM");
    a.author = textOf(d, "T");
    a.subject = textOf(d, "Subj");
    a.contents = textOf(d, "Contents");
    if (const CosObject* rc = d.find("RC"); rc != nullptr && rc->isString())
        a.richText = EmbeddedFileReader::decodeTextString(rc->text());
    a.intent = nameOf(d, "IT");
    a.icon = nameOf(d, "Name");
    a.defaultAppearance = textOf(d, "DA");
    if (d.find("CreationDate") != nullptr) a.created = core::PdfDate::parse(textOf(d, "CreationDate"));
    if (d.find("M") != nullptr) a.modified = core::PdfDate::parse(textOf(d, "M"));
    if (const CosObject* flags = d.find("F"); flags != nullptr && flags->isNumber()) {
        const double value = flags->numberValue();
        if (value >= 0 && value <= 4294967295.0) a.flags = static_cast<std::uint32_t>(value);
    }

    a.inReplyTo = referenceOf(d, "IRT");
    if (nameOf(d, "RT") == QLatin1String("Group")) a.replyType = core::ReplyType::Group;
    a.stateModel = textOf(d, "StateModel");
    if (!a.stateModel.isEmpty()) a.state = textOf(d, "State");

    a.popup = referenceOf(d, "Popup");
    a.parent = referenceOf(d, "Parent");
    if (const CosObject* open = d.find("Open"); open != nullptr && open->isBoolean())
        a.open = open->booleanValue();
    // Range-checked BEFORE the cast: converting a double outside int's range is undefined behaviour,
    // and `/Rotate 1e300` is a value a file can carry.
    if (const CosObject* rotate = d.find("Rotate"); rotate != nullptr && rotate->isNumber()) {
        const double value = rotate->numberValue();
        if (std::fabs(value) <= 360000.0) a.rotate = static_cast<int>(value);
    }
    a.hasAppearance = d.find("AP") != nullptr;

    out.pageReference = referenceOf(d, "P");
    return out;
}

} // namespace

core::AnnotationStore::Reading AnnotationReader::read(cos::XrefTable& table, const PageWalk& walk)
{
    core::AnnotationStore::Reading reading;
    reading.pages.resize(walk.pages.size());

    std::map<long long, std::size_t> seen;           // object number -> index in annotations
    std::map<std::size_t, long long> pageReference;  // index -> its /P, where it has one
    std::vector<core::AnnotationFact> facts;         // index -> what its first listing compared

    auto note = [&reading](AnnotationNoteKind kind, int page, long long object) {
        reading.notes.push_back(AnnotationNote{kind, page, object});
    };

    for (std::size_t pageIndex = 0; pageIndex < walk.pages.size(); ++pageIndex) {
        const int page = static_cast<int>(pageIndex);
        auto fetched = table.fetch(walk.pages[pageIndex].number);
        if (!fetched || !fetched.value().object.isDictionary()) continue;
        const CosObject& pageDictionary = fetched.value().object;

        std::optional<cos::IndirectObject> annotsHolder;
        const CosObject* annots = resolve(table, pageDictionary.find("Annots"), annotsHolder);
        if (annots == nullptr) continue;
        if (!annots->isArray()) {
            // PDFium counts no annotation here either (it asks for an array and gets none).
            note(AnnotationNoteKind::AnnotsNotArray, page, -1);
            continue;
        }

        const std::vector<CosObject>& entries = annots->items();
        for (std::size_t position = 0; position < entries.size(); ++position) {
            const CosObject& entry = entries[position];
            core::AnnotationListingEntry listed;

            if (entry.isReference()) {
                const long long number = entry.reference().number;
                if (const auto known = seen.find(number); known != seen.end()) {
                    // A dictionary several pages list: the same annotation, owned by the page its /P
                    // names if that page lists it.
                    listed.annotation = known->second;
                    listed.fact = facts[known->second];
                    Annotation& owned = reading.annotations[known->second];
                    note(AnnotationNoteKind::ListedByManyPages, page, number);
                    const auto p = pageReference.find(known->second);
                    if (p != pageReference.end() && p->second == walk.pages[pageIndex].number)
                        owned.page = page;
                    reading.pages[pageIndex].push_back(listed);
                    continue;
                }
                auto object = table.fetch(number);
                if (!object || !object.value().object.isDictionary()) {
                    note(AnnotationNoteKind::EntryNotDictionary, page, number);
                    reading.pages[pageIndex].push_back(listed);
                    continue;
                }
                Parsed parsed = parse(table, object.value().object);
                parsed.annotation.id = core::AnnotationId::ofObject(number);
                parsed.annotation.page = page;
                if (parsed.nonFiniteRect) note(AnnotationNoteKind::NonFiniteRect, page, number);
                if (parsed.truncated) note(AnnotationNoteKind::GeometryTruncated, page, number);
                const std::size_t index = reading.annotations.size();
                seen.emplace(number, index);
                if (parsed.pageReference) pageReference.emplace(index, *parsed.pageReference);
                listed.fact = parsed.fact;
                listed.annotation = index;
                facts.push_back(parsed.fact);
                reading.annotations.push_back(std::move(parsed.annotation));
            } else if (entry.isDictionary()) {
                Parsed parsed = parse(table, entry);
                parsed.annotation.id = core::AnnotationId::ofDirect(page, static_cast<int>(position));
                parsed.annotation.page = page;
                if (parsed.nonFiniteRect) note(AnnotationNoteKind::NonFiniteRect, page, -1);
                if (parsed.truncated) note(AnnotationNoteKind::GeometryTruncated, page, -1);
                listed.fact = parsed.fact;
                listed.annotation = reading.annotations.size();
                facts.push_back(parsed.fact);
                reading.annotations.push_back(std::move(parsed.annotation));
            } else {
                note(AnnotationNoteKind::EntryNotDictionary, page, -1);
            }
            reading.pages[pageIndex].push_back(listed);
        }
    }

    // What the listed annotations POINT AT that no page lists: an /IRT parent, a /Popup, a pop-up's
    // /Parent. Each is reported once and not read further -- D6-3: with no page listing it, it has no
    // page, and PDFium draws it nowhere.
    std::set<long long> pointedAt;
    for (const Annotation& a : reading.annotations) {
        for (const std::optional<long long>& target : {a.inReplyTo, a.popup, a.parent}) {
            if (target && seen.find(*target) == seen.end()) pointedAt.insert(*target);
        }
    }
    for (const long long number : pointedAt) {
        auto object = table.fetch(number);
        if (!object || !object.value().object.isDictionary()) continue;   // missing: the store says so
        const CosObject& d = object.value().object;
        const bool annotation = d.find("Subtype") != nullptr
                                || (d.find("Type") != nullptr && d.get("Type").isName()
                                    && d.get("Type").text() == "Annot");
        if (annotation) note(AnnotationNoteKind::Unlisted, -1, number);
    }
    return reading;
}

} // namespace rawpage::pdf::ops
