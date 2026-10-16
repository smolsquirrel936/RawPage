// core/doc/XfdfCodec -- implementation. Stage 6 T13. See the header for the DTD rule, what is written where, and why
// the output is written by hand.

#include "core/doc/XfdfCodec.h"

#include <QXmlStreamReader>

#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace rawpage::core {
namespace {

const QString kXfdfNamespace = QStringLiteral("http://ns.adobe.com/xfdf/");

Error exchangeError(const char* key, const QString& context)
{
    return Error{ErrorCode::ParseFailure, QLatin1String(key), context};
}

// ---- The kinds, by their XFDF element ---------------------------------------------------------------------------

struct Kind {
    const char* element;
    AnnotationSubtype subtype;
};

constexpr std::array<Kind, 18> kKinds{{
    {"text", AnnotationSubtype::Text},           {"freetext", AnnotationSubtype::FreeText},
    {"line", AnnotationSubtype::Line},           {"square", AnnotationSubtype::Square},
    {"circle", AnnotationSubtype::Circle},       {"polygon", AnnotationSubtype::Polygon},
    {"polyline", AnnotationSubtype::PolyLine},   {"highlight", AnnotationSubtype::Highlight},
    {"underline", AnnotationSubtype::Underline}, {"squiggly", AnnotationSubtype::Squiggly},
    {"strikeout", AnnotationSubtype::StrikeOut}, {"stamp", AnnotationSubtype::Stamp},
    {"caret", AnnotationSubtype::Caret},         {"ink", AnnotationSubtype::Ink},
    {"fileattachment", AnnotationSubtype::FileAttachment}, {"sound", AnnotationSubtype::Sound},
    {"redact", AnnotationSubtype::Redact},       {"popup", AnnotationSubtype::Popup},
}};

std::optional<AnnotationSubtype> subtypeOfElement(QStringView name)
{
    for (const Kind& k : kKinds) {
        if (name == QLatin1String(k.element)) return k.subtype;
    }
    return std::nullopt;
}

const char* elementOf(AnnotationSubtype subtype)
{
    for (const Kind& k : kKinds) {
        if (k.subtype == subtype) return k.element;
    }
    return nullptr;
}

// ---- The flags, by XFDF's names (ISO 32000-1 Table 165) ---------------------------------------------------------

constexpr std::array<std::pair<const char*, std::uint32_t>, 10> kFlags{{
    {"invisible", AnnotationFlag::Invisible}, {"hidden", AnnotationFlag::Hidden},
    {"print", AnnotationFlag::Print},         {"nozoom", AnnotationFlag::NoZoom},
    {"norotate", AnnotationFlag::NoRotate},   {"noview", AnnotationFlag::NoView},
    {"readonly", AnnotationFlag::ReadOnly},   {"locked", AnnotationFlag::Locked},
    {"togglenoview", AnnotationFlag::ToggleNoView}, {"lockedcontents", AnnotationFlag::LockedContents},
}};

std::uint32_t flagsOf(QStringView text)
{
    std::uint32_t flags = 0;
    for (const QStringView part : text.split(QLatin1Char(','))) {
        const QString name = part.trimmed().toString().toLower();
        for (const auto& [key, bit] : kFlags) {
            if (name == QLatin1String(key)) flags |= bit;
        }
    }
    return flags;
}

QString flagsText(std::uint32_t flags)
{
    QStringList names;
    for (const auto& [key, bit] : kFlags) {
        if ((flags & bit) != 0) names << QLatin1String(key);
    }
    return names.join(QLatin1Char(','));
}

// ---- The border styles: XFDF's words for ISO 32000-1 Table 166's letters -----------------------------------------

constexpr std::array<std::pair<const char*, const char*>, 5> kStyles{{
    {"solid", "S"}, {"dash", "D"}, {"bevelled", "B"}, {"inset", "I"}, {"underline", "U"},
}};

// ---- Numbers ------------------------------------------------------------------------------------------------------

// The shortest spelling that reads back as the same double, in FIXED notation: what `pdf/cos`'s writer writes, so a
// number survives XFDF exactly as it survives a save.
QString number(double value)
{
    if (!std::isfinite(value)) value = 0.0;
    char buffer[400];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed);
    return QString::fromLatin1(buffer, static_cast<qsizetype>(result.ptr - buffer));
}

QString numbers(const std::vector<double>& values, QLatin1Char separator = QLatin1Char(','))
{
    QString out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += separator;
        out += number(values[i]);
    }
    return out;
}

// x,y pairs joined by `;` -- XFDF's vertices and gestures.
QString points(const std::vector<double>& values)
{
    QString out;
    for (std::size_t i = 0; i + 1 < values.size(); i += 2) {
        if (i != 0) out += QLatin1Char(';');
        out += number(values[i]) + QLatin1Char(',') + number(values[i + 1]);
    }
    return out;
}

// Every number in `text`, whatever separates them -- commas, semicolons, white space -- at most `budget`; a token that
// is no finite number makes the whole list nothing (`ok` false), which every caller treats as "not said".
std::vector<double> numbersIn(QStringView text, std::size_t& budget, bool& truncated, bool& ok)
{
    std::vector<double> out;
    ok = true;
    qsizetype start = -1;
    const auto take = [&](qsizetype from, qsizetype to) {
        if (from < 0 || to <= from) return;
        if (budget == 0) {
            truncated = true;
            return;
        }
        bool parsed = false;
        const double v = text.mid(from, to - from).toDouble(&parsed);
        if (!parsed || !std::isfinite(v)) {
            ok = false;
            return;
        }
        out.push_back(v);
        --budget;
    };
    for (qsizetype i = 0; i <= text.size(); ++i) {
        const bool separator = i == text.size() || text[i] == QLatin1Char(',') || text[i] == QLatin1Char(';')
                               || text[i].isSpace();
        if (separator) {
            take(start, i);
            start = -1;
            if (!ok) return {};
        } else if (start < 0) {
            start = i;
        }
    }
    return out;
}

std::vector<double> smallNumbers(QStringView text, std::size_t limit = 16)
{
    std::size_t budget = limit;
    bool truncated = false;
    bool ok = true;
    std::vector<double> out = numbersIn(text, budget, truncated, ok);
    if (!ok || truncated) return {};
    return out;
}

std::optional<double> numberAttribute(const QXmlStreamAttributes& attributes, const char* name)
{
    if (!attributes.hasAttribute(QLatin1String(name))) return std::nullopt;
    bool ok = false;
    const double v = attributes.value(QLatin1String(name)).trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(v)) return std::nullopt;
    return v;
}

// ---- Colour ---------------------------------------------------------------------------------------------------

std::vector<double> colourOfHex(QStringView text)
{
    if (text.size() != 7 || text[0] != QLatin1Char('#')) return {};
    bool ok = false;
    const unsigned value = text.mid(1).toUInt(&ok, 16);
    if (!ok) return {};
    return {((value >> 16) & 0xFF) / 255.0, ((value >> 8) & 0xFF) / 255.0, (value & 0xFF) / 255.0};
}

int channel(double v)
{
    return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
}

QString hexOfColour(const std::vector<double>& colour)
{
    return QStringLiteral("#%1%2%3")
        .arg(channel(colour[0]), 2, 16, QLatin1Char('0'))
        .arg(channel(colour[1]), 2, 16, QLatin1Char('0'))
        .arg(channel(colour[2]), 2, 16, QLatin1Char('0'))
        .toUpper();
}

// Whether XFDF's `#RRGGBB` says `colour` exactly.
bool hexIsExact(const std::vector<double>& colour)
{
    if (colour.size() != 3) return false;
    return colourOfHex(hexOfColour(colour)) == colour;
}

// ---- Escaping -------------------------------------------------------------------------------------------------

// A character XML 1.0 can hold, else U+FFFD. Surrogates are kept only in pairs.
QString xmlSafe(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t c = text[i].unicode();
        if (QChar::isHighSurrogate(c) && i + 1 < text.size() && QChar::isLowSurrogate(text[i + 1].unicode())) {
            out += text[i];
            out += text[++i];
            continue;
        }
        const bool allowed = c == 0x9 || c == 0xA || c == 0xD || (c >= 0x20 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD);
        out += allowed ? text[i] : QChar(0xFFFD);
    }
    return out;
}

// Text content: the three markup characters, and a carriage return as a reference -- an XML reader turns a bare one
// into a line feed (XML 1.0 §2.11), and a comment's `\r` is the comment's.
QString escapeText(const QString& raw)
{
    QString out;
    const QString text = xmlSafe(raw);
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('&')) out += QLatin1String("&amp;");
        else if (c == QLatin1Char('<')) out += QLatin1String("&lt;");
        else if (c == QLatin1Char('>')) out += QLatin1String("&gt;");
        else if (c == QLatin1Char('\r')) out += QLatin1String("&#13;");
        else out += c;
    }
    return out;
}

// An attribute value: also the quote, and every white-space character as a reference -- an XML reader normalizes a
// bare one to a space (XML 1.0 §3.3.3).
QString escapeAttribute(const QString& raw)
{
    QString out;
    const QString text = xmlSafe(raw);
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('&')) out += QLatin1String("&amp;");
        else if (c == QLatin1Char('<')) out += QLatin1String("&lt;");
        else if (c == QLatin1Char('>')) out += QLatin1String("&gt;");
        else if (c == QLatin1Char('"')) out += QLatin1String("&quot;");
        else if (c == QLatin1Char('\r')) out += QLatin1String("&#13;");
        else if (c == QLatin1Char('\n')) out += QLatin1String("&#10;");
        else if (c == QLatin1Char('\t')) out += QLatin1String("&#9;");
        else out += c;
    }
    return out;
}

// ---- Reading ---------------------------------------------------------------------------------------------------

struct Reader {
    QXmlStreamReader& xml;
    Fdf& result;
    std::set<QString> names;
    std::optional<Error> refusal;

    void note(ExchangeNoteKind kind, const QString& name, int page = -1)
    {
        result.notes.push_back(ExchangeNote{kind, name, page});
    }

    QString bounded(QString text, const QString& name)
    {
        if (text.size() > kMaxExchangeTextLength) {
            text.truncate(kMaxExchangeTextLength);
            note(ExchangeNoteKind::TextTooLong, name);
        }
        return text;
    }

    // The children of the element the reader is on, serialized back to the XML they are -- a rich-text body. Leaves
    // the reader on the element's end. Empty, and noted, for a body past the text bound.
    QString innerXml(const QString& name)
    {
        QString out;
        int depth = 0;
        bool openTag = false;
        bool tooLong = false;
        while (!xml.atEnd()) {
            const QXmlStreamReader::TokenType token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                if (openTag) out += QLatin1Char('>');
                out += QLatin1Char('<') + xml.qualifiedName().toString();
                for (const QXmlStreamNamespaceDeclaration& ns : xml.namespaceDeclarations()) {
                    out += ns.prefix().isEmpty() ? QStringLiteral(" xmlns=\"")
                                                 : QStringLiteral(" xmlns:") + ns.prefix().toString() + QStringLiteral("=\"");
                    out += escapeAttribute(ns.namespaceUri().toString()) + QLatin1Char('"');
                }
                for (const QXmlStreamAttribute& attribute : xml.attributes()) {
                    out += QLatin1Char(' ') + attribute.qualifiedName().toString() + QStringLiteral("=\"")
                           + escapeAttribute(attribute.value().toString()) + QLatin1Char('"');
                }
                openTag = true;
                ++depth;
            } else if (token == QXmlStreamReader::EndElement) {
                if (depth == 0) break;
                if (openTag) out += QStringLiteral("/>");
                else out += QStringLiteral("</") + xml.qualifiedName().toString() + QLatin1Char('>');
                openTag = false;
                --depth;
            } else if (token == QXmlStreamReader::Characters) {
                if (openTag) out += QLatin1Char('>');
                openTag = false;
                out += escapeText(xml.text().toString());
            } else if (token == QXmlStreamReader::EntityReference) {
                refusal = exchangeError("error.exchange.dtd", xml.name().toString());
                return {};
            }
            if (out.size() > kMaxExchangeTextLength) tooLong = true;
            if (tooLong) out.clear();
        }
        if (tooLong) {
            note(ExchangeNoteKind::TextTooLong, name);
            return {};
        }
        return out;
    }

    QString elementText(const QString& name)
    {
        return bounded(xml.readElementText(QXmlStreamReader::SkipChildElements), name);
    }

    // A `<data>` element's bytes: hex (the default) or base64. Nothing for a filter this reader does not undo, or
    // text that is neither -- the annotation then carries no file and an import refuses it as one with nothing to
    // write.
    std::optional<QByteArray> data(const QString& name)
    {
        const QXmlStreamAttributes attributes = xml.attributes();
        const QString filter = attributes.value(QStringLiteral("filter")).toString();
        const QString encoding = attributes.value(QStringLiteral("encoding")).toString().toLower();
        const QString text = xml.readElementText(QXmlStreamReader::SkipChildElements);
        if (!filter.isEmpty()) {
            note(ExchangeNoteKind::UnknownElement, name);
            return std::nullopt;
        }
        QByteArray compact;
        compact.reserve(text.size());
        for (const QChar c : text) {
            if (!c.isSpace()) compact += static_cast<char>(c.unicode() & 0xFF);
        }
        if (encoding == QLatin1String("base64")) {
            auto decoded = QByteArray::fromBase64Encoding(compact, QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) return std::nullopt;
            return std::move(*decoded);
        }
        if (compact.size() % 2 != 0) return std::nullopt;
        for (const char c : compact) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
        }
        return QByteArray::fromHex(compact);
    }

    std::vector<MeasureNumberFormat> formats(const QXmlStreamAttributes& a)
    {
        MeasureNumberFormat f;
        f.unit = a.value(QStringLiteral("unit")).toString().left(64);
        f.factor = numberAttribute(a, "factor").value_or(1.0);
        const double d = numberAttribute(a, "denominator").value_or(100.0);
        f.denominator = d >= 1.0 && d <= 1.0e9 && d == std::floor(d) ? static_cast<int>(d) : 100;
        if (a.hasAttribute(QStringLiteral("thousands"))) f.thousands = a.value(QStringLiteral("thousands")).toString().left(8);
        if (a.hasAttribute(QStringLiteral("decimal"))) f.decimal = a.value(QStringLiteral("decimal")).toString().left(8);
        f.plain = a.value(QStringLiteral("plain")).toString() != QLatin1String("false");
        return {f};
    }

    MeasureDictionary measure()
    {
        MeasureDictionary m;
        const QXmlStreamAttributes attributes = xml.attributes();
        if (attributes.hasAttribute(QStringLiteral("subtype"))) m.subtype = attributes.value(QStringLiteral("subtype")).toString().left(64);
        m.ratio = attributes.value(QStringLiteral("ratio")).toString().left(256);
        while (xml.readNextStartElement()) {
            const QStringView n = xml.name();
            std::vector<MeasureNumberFormat>* chain = n == QLatin1String("x")          ? &m.x
                                                      : n == QLatin1String("y")        ? &m.y
                                                      : n == QLatin1String("distance") ? &m.distance
                                                      : n == QLatin1String("area")     ? &m.area
                                                                                       : nullptr;
            if (chain != nullptr && chain->size() < 16) {
                std::vector<MeasureNumberFormat> one = formats(xml.attributes());
                chain->insert(chain->end(), one.begin(), one.end());
            }
            xml.skipCurrentElement();
        }
        return m;
    }

    // ONE ANNOTATION ELEMENT, the reader on its start; leaves it on its end.
    void annotation(AnnotationSubtype subtype)
    {
        const QXmlStreamAttributes at = xml.attributes();
        const auto text = [&at](const char* name) { return at.value(QLatin1String(name)).toString(); };
        const auto has = [&at](const char* name) { return at.hasAttribute(QLatin1String(name)); };
        const QString rpNamespace = QLatin1String(XfdfCodec::kRawPageNamespace);
        const auto rp = [&at, &rpNamespace](const char* name) { return at.value(rpNamespace, QLatin1String(name)).toString(); };

        Annotation a;
        a.subtype = subtype;
        a.nm = text("name");
        bool pageOk = false;
        a.page = has("page") ? text("page").trimmed().toInt(&pageOk) : -1;
        if (!pageOk) a.page = -1;
        const std::vector<double> rect = smallNumbers(text("rect"), 4);
        if (rect.size() == 4) {
            if (const std::optional<PdfRect> r = normalizedRect(rect[0], rect[1], rect[2], rect[3])) a.rect = *r;
            else a.rectFinite = false;
        } else {
            a.rectFinite = false;
        }
        a.flags = flagsOf(text("flags"));
        a.author = bounded(text("title"), a.nm);
        a.subject = bounded(text("subject"), a.nm);
        if (has("date")) a.modified = PdfDate::parse(text("date"));
        if (has("creationdate")) a.created = PdfDate::parse(text("creationdate"));
        a.color = !rp("color").isEmpty() ? smallNumbers(rp("color"), 4) : colourOfHex(text("color"));
        a.interiorColor = !rp("interior-color").isEmpty() ? smallNumbers(rp("interior-color"), 4)
                                                           : colourOfHex(text("interior-color"));
        a.opacity = numberAttribute(at, "opacity");
        a.borderWidth = numberAttribute(at, "width");
        const QString style = text("style").toLower();
        for (const auto& [word, letter] : kStyles) {
            if (style == QLatin1String(word)) a.borderStyle = QLatin1String(letter);
        }
        if (!rp("border-style").isEmpty()) a.borderStyle = rp("border-style").left(1);
        if (style == QLatin1String("cloudy")) a.cloudIntensity = numberAttribute(at, "intensity").value_or(0.0);
        a.dashPattern = smallNumbers(text("dashes"));
        a.blendMode = rp("blend-mode").left(32);
        a.intent = text("intent").left(64);
        a.icon = text("icon").left(64);
        a.rotate = static_cast<int>(std::clamp(numberAttribute(at, "rotation").value_or(0.0), -360000.0, 360000.0));
        if (const std::optional<double> q = numberAttribute(at, "justification"); q && (*q == 1.0 || *q == 2.0))
            a.justification = static_cast<int>(*q);
        a.rectDifferences = smallNumbers(text("fringe"), 4);
        a.callout = smallNumbers(text("callout"), 6);
        if (has("start") && has("end")) {
            const std::vector<double> start = smallNumbers(text("start"), 2);
            const std::vector<double> end = smallNumbers(text("end"), 2);
            if (start.size() == 2 && end.size() == 2) a.line = {start[0], start[1], end[0], end[1]};
        }
        if (has("head") || has("tail")) {
            const QString head = has("head") ? text("head").left(32) : QStringLiteral("None");
            const QString tail = has("tail") ? text("tail").left(32) : QStringLiteral("None");
            if (subtype == AnnotationSubtype::FreeText) a.lineEndings = {head};
            else a.lineEndings = {head, tail};
        }
        a.leaderLength = numberAttribute(at, "leaderLength").value_or(0.0);
        a.leaderExtension = std::max(0.0, numberAttribute(at, "leaderExtend").value_or(0.0));
        a.leaderOffset = std::max(0.0, numberAttribute(at, "leaderOffset").value_or(0.0));
        a.caption = text("caption") == QLatin1String("yes") || text("caption") == QLatin1String("true");
        if (text("caption-style") == QLatin1String("Top")) a.captionPosition = QStringLiteral("Top");
        if (has("caption-offset-h") || has("caption-offset-v"))
            a.captionOffset = {numberAttribute(at, "caption-offset-h").value_or(0.0), numberAttribute(at, "caption-offset-v").value_or(0.0)};
        a.stateModel = text("statemodel").left(64);
        if (!a.stateModel.isEmpty()) a.state = text("state").left(64);
        // Stage 6 T14 (D6-25): a redaction mark's overlay, as Acrobat writes it -- no longer than a command carries.
        if (subtype == AnnotationSubtype::Redact) {
            a.overlayText = text("overlay-text").left(kMaxOverlayTextLength);
            a.repeat = text("repeat").toLower() == QLatin1String("true") || text("repeat").toLower() == QLatin1String("yes");
        }
        // Stage 6 T14 (D6-18): a sound's numbers from its own attributes, its samples from its `<data>` -- each within
        // what a command accepts, or the annotation carries no sound and an import refuses it as one with nothing to play.
        std::optional<SoundData> sound;
        if (subtype == AnnotationSubtype::Sound) {
            SoundData s;
            const auto whole = [&](const char* name, double low, double high, int fallback) -> std::optional<int> {
                if (!has(name)) return fallback;
                const std::optional<double> v = numberAttribute(at, name);
                if (!v || *v < low || *v > high || *v != std::floor(*v)) return std::nullopt;
                return static_cast<int>(*v);
            };
            const std::optional<int> rate = whole("rate", 1.0, 10000000.0, -1);
            const std::optional<int> channels = whole("channels", 1.0, 64.0, 1);
            const std::optional<int> bits = whole("bits", 1.0, 32.0, 8);
            const std::optional<SoundEncoding> encoding =
                soundEncodingFromName(has("encoding") ? text("encoding") : QStringLiteral("Raw"));
            if (rate && *rate >= 1 && channels && bits && encoding) {
                s.rate = *rate;
                s.channels = *channels;
                s.bits = *bits;
                s.encoding = *encoding;
                sound = s;
            }
        }
        if (text("replyType").toLower() == QLatin1String("group")) a.replyType = ReplyType::Group;
        const QString inReplyTo = text("inreplyto");

        std::size_t budget = kMaxExchangeCoordinates;
        bool truncated = false;
        bool ok = true;
        a.quadPoints = numbersIn(text("coords"), budget, truncated, ok);
        if (!ok) a.quadPoints.clear();

        // A FileAttachment's file, from its own attributes and its `<data>`.
        EmbeddedFile file;
        file.name = text("file").left(1024);
        file.description = text("description");
        file.mime = text("mimetype").left(128);
        if (const std::optional<double> size = numberAttribute(at, "size"); size && *size >= 0 && *size <= 9.0e15)
            file.size = static_cast<long long>(*size);
        if (has("creation")) file.created = PdfDate::parse(text("creation"));
        if (has("modification")) file.modified = PdfDate::parse(text("modification"));
        if (const QByteArray sum = QByteArray::fromHex(text("checksum").toLatin1()); sum.size() == 16) file.checksum = sum;

        std::optional<Annotation> popup;
        while (!xml.atEnd()) {
            const QXmlStreamReader::TokenType token = xml.readNext();
            if (token == QXmlStreamReader::EndElement) break;
            if (token == QXmlStreamReader::EntityReference) {
                refusal = exchangeError("error.exchange.dtd", xml.name().toString());
                return;
            }
            if (token != QXmlStreamReader::StartElement) continue;
            const QString child = xml.name().toString();
            if (xml.namespaceUri() == rpNamespace) {
                if (child == QLatin1String("measure")) a.measure = measure();
                else xml.skipCurrentElement();
            } else if (child == QLatin1String("contents")) {
                a.contents = elementText(a.nm);
            } else if (child == QLatin1String("contents-richtext")) {
                const QString body = innerXml(a.nm);
                if (refusal) return;
                if (!body.trimmed().isEmpty()) a.richText = QStringLiteral("<?xml version=\"1.0\"?>") + body;
            } else if (child == QLatin1String("defaultappearance")) {
                a.defaultAppearance = elementText(a.nm).left(4096);
            } else if (child == QLatin1String("vertices")) {
                bool fine = true;
                a.vertices = numbersIn(elementText(a.nm), budget, truncated, fine);
                if (!fine) a.vertices.clear();
            } else if (child == QLatin1String("inklist")) {
                while (xml.readNextStartElement()) {
                    if (xml.name() == QLatin1String("gesture")) {
                        bool fine = true;
                        std::vector<double> stroke = numbersIn(elementText(a.nm), budget, truncated, fine);
                        if (fine && !stroke.empty()) a.inkList.push_back(std::move(stroke));
                    } else {
                        xml.skipCurrentElement();
                    }
                }
            } else if (child == QLatin1String("data")) {
                std::optional<QByteArray> bytes = data(a.nm);
                if (bytes && subtype == AnnotationSubtype::FileAttachment) {
                    file.data = std::move(*bytes);
                    file.carried = true;
                    file.size = file.data.size();
                } else if (bytes && sound && bytes->size() <= kMaxSoundBytes) {   // T14: a sound's samples
                    sound->samples = std::move(*bytes);
                    sound->carried = true;
                }
            } else if (child == QLatin1String("popup")) {
                const QXmlStreamAttributes pa = xml.attributes();
                Annotation p;
                p.subtype = AnnotationSubtype::Popup;
                p.flags = flagsOf(pa.value(QStringLiteral("flags")));
                const std::vector<double> r = smallNumbers(pa.value(QStringLiteral("rect")), 4);
                if (r.size() == 4) {
                    if (const std::optional<PdfRect> box = normalizedRect(r[0], r[1], r[2], r[3])) p.rect = *box;
                    else p.rectFinite = false;
                } else {
                    p.rectFinite = false;
                }
                const QString open = pa.value(QStringLiteral("open")).toString().toLower();
                if (open == QLatin1String("yes") || open == QLatin1String("true")) p.open = true;
                else if (open == QLatin1String("no") || open == QLatin1String("false")) p.open = false;
                popup = std::move(p);
                xml.skipCurrentElement();
            } else {
                xml.skipCurrentElement();   // defaultstyle, appearance, imagedata, …: not the model's
            }
        }
        if (truncated) note(ExchangeNoteKind::GeometryTruncated, a.nm, a.page);
        if (subtype == AnnotationSubtype::FileAttachment && !file.name.isEmpty()) a.attachment = std::move(file);
        if (sound) a.sound = std::move(sound);

        // ITS NAME: the file's, else the one its content gives it (`core/doc/Fdf.h`).
        if (a.nm.isEmpty()) a.nm = contentNameOf(a);
        if (a.nm.size() > kMaxExchangeNameLength) {
            note(ExchangeNoteKind::NameTooLong, a.nm.left(64), a.page);
            return;
        }
        if (!names.insert(a.nm).second) {
            note(ExchangeNoteKind::DuplicateName, a.nm, a.page);
            return;
        }
        a.id = AnnotationId::ofCreated(a.nm);
        if (!inReplyTo.isEmpty() && inReplyTo.size() <= kMaxExchangeNameLength)
            a.inReplyTo = AnnotationId::ofCreated(inReplyTo);
        // A pop-up's name is its parent's and `-popup` -- unless a comment of the file already HAS that name, and then
        // the pop-up goes rather than two annotations answering to one name.
        if (popup && !names.insert(popupNameFor(a.nm)).second) {
            note(ExchangeNoteKind::PopupWithoutParent, a.nm, a.page);
            popup.reset();
        }
        if (popup) {
            popup->nm = popupNameFor(a.nm);
            popup->id = AnnotationId::ofCreated(popup->nm);
            popup->page = a.page;
            popup->parent = a.id;
            a.popup = popup->id;
        }
        if (result.annotations.size() >= kMaxExchangeAnnotations) {
            refusal = exchangeError("error.exchange.tooMany", QString::number(kMaxExchangeAnnotations));
            return;
        }
        result.annotations.push_back(std::move(a));
        if (popup) result.annotations.push_back(std::move(*popup));
    }

    void annots()
    {
        while (!refusal && !xml.atEnd()) {
            const QXmlStreamReader::TokenType token = xml.readNext();
            if (token == QXmlStreamReader::EndElement) return;
            if (token == QXmlStreamReader::EntityReference) {
                refusal = exchangeError("error.exchange.dtd", xml.name().toString());
                return;
            }
            if (token != QXmlStreamReader::StartElement) continue;
            const std::optional<AnnotationSubtype> subtype = subtypeOfElement(xml.name());
            // A pop-up on its own names no parent XFDF can give it: it is its parent's child or nothing.
            if (!subtype || *subtype == AnnotationSubtype::Popup) {
                note(subtype ? ExchangeNoteKind::PopupWithoutParent : ExchangeNoteKind::UnknownElement, xml.name().toString());
                xml.skipCurrentElement();
                continue;
            }
            annotation(*subtype);
        }
    }
};

// ---- Writing ---------------------------------------------------------------------------------------------------

struct Writer {
    QString out;

    void attribute(const char* name, const QString& value)
    {
        if (value.isEmpty()) return;
        out += QLatin1Char(' ') + QLatin1String(name) + QStringLiteral("=\"") + escapeAttribute(value) + QLatin1Char('"');
    }

    void colour(const char* name, const char* exact, const std::vector<double>& c)
    {
        if (c.empty()) return;
        if (c.size() == 3) attribute(name, hexOfColour(c));
        if (!hexIsExact(c)) attribute(exact, numbers(c, QLatin1Char(' ')));
    }

    // A rich-text body, written as the elements it is -- or nothing, when it is not well-formed XML on its own.
    void richText(const QString& rc)
    {
        if (rc.isEmpty()) return;
        QXmlStreamReader probe(rc);
        bool sawRoot = false;
        qsizetype bodyStart = -1;
        while (!probe.atEnd()) {
            const QXmlStreamReader::TokenType token = probe.readNext();
            if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference) return;
            if (token == QXmlStreamReader::StartElement && !sawRoot) {
                sawRoot = true;
                bodyStart = rc.indexOf(QLatin1Char('<'), qsizetype{0});
                // The body begins at the first element: past an XML declaration and anything before it.
                while (bodyStart >= 0 && bodyStart + 1 < rc.size()
                       && (rc[bodyStart + 1] == QLatin1Char('?') || rc[bodyStart + 1] == QLatin1Char('!')))
                    bodyStart = rc.indexOf(QLatin1Char('<'), bodyStart + 1);
            }
        }
        if (probe.hasError() || !sawRoot || bodyStart < 0) return;
        out += QStringLiteral("<contents-richtext>") + xmlSafe(rc.mid(bodyStart)) + QStringLiteral("</contents-richtext>\n");
    }

    void annotation(const Annotation& a, const Annotation* popup)
    {
        const char* element = elementOf(a.subtype);
        if (element == nullptr || a.subtype == AnnotationSubtype::Popup) return;
        out += QLatin1Char('<') + QLatin1String(element);
        colour("color", "rp:color", a.color);
        attribute("creationdate", a.created.raw);
        attribute("flags", flagsText(a.flags));
        attribute("date", a.modified.raw);
        attribute("name", a.nm);
        attribute("page", QString::number(a.page));
        attribute("rect", numbers({a.rect.left(), a.rect.bottom(), a.rect.right(), a.rect.top()}));
        attribute("subject", a.subject);
        attribute("title", a.author);
        if (a.opacity) attribute("opacity", number(*a.opacity));
        if (a.inReplyTo && a.inReplyTo->kind == AnnotationId::Kind::Created) attribute("inreplyto", a.inReplyTo->nm);
        if (a.replyType == ReplyType::Group) attribute("replyType", QStringLiteral("group"));
        attribute("intent", a.intent);
        if (a.borderWidth) attribute("width", number(*a.borderWidth));
        if (a.cloudIntensity) {
            attribute("style", QStringLiteral("cloudy"));
            attribute("intensity", number(*a.cloudIntensity));
            attribute("rp:border-style", a.borderStyle);
        } else {
            for (const auto& [word, letter] : kStyles) {
                if (a.borderStyle == QLatin1String(letter)) attribute("style", QLatin1String(word));
            }
            if (!a.borderStyle.isEmpty() && a.borderStyle.size() == 1
                && std::none_of(kStyles.begin(), kStyles.end(), [&a](const auto& s) { return a.borderStyle == QLatin1String(s.second); }))
                attribute("rp:border-style", a.borderStyle);
        }
        attribute("dashes", numbers(a.dashPattern));
        colour("interior-color", "rp:interior-color", a.interiorColor);
        attribute("rp:blend-mode", a.blendMode);
        attribute("fringe", numbers(a.rectDifferences));
        if (a.rotate != 0) attribute("rotation", QString::number(a.rotate));
        if (a.justification != 0) attribute("justification", QString::number(a.justification));
        attribute("icon", a.icon);
        attribute("statemodel", a.stateModel);
        attribute("state", a.state);
        attribute("coords", numbers(a.quadPoints));
        attribute("callout", numbers(a.callout));
        if (a.line.size() == 4) {
            attribute("start", numbers({a.line[0], a.line[1]}));
            attribute("end", numbers({a.line[2], a.line[3]}));
        }
        if (!a.lineEndings.empty()) {
            attribute("head", a.lineEndings[0]);
            if (a.lineEndings.size() > 1) attribute("tail", a.lineEndings[1]);
        }
        if (a.leaderLength != 0.0) attribute("leaderLength", number(a.leaderLength));
        if (a.leaderExtension != 0.0) attribute("leaderExtend", number(a.leaderExtension));
        if (a.leaderOffset != 0.0) attribute("leaderOffset", number(a.leaderOffset));
        if (a.caption) attribute("caption", QStringLiteral("yes"));
        if (!a.captionPosition.isEmpty()) attribute("caption-style", a.captionPosition);
        if (a.captionOffset.size() == 2) {
            attribute("caption-offset-h", number(a.captionOffset[0]));
            attribute("caption-offset-v", number(a.captionOffset[1]));
        }
        if (a.attachment) {
            const EmbeddedFile& f = *a.attachment;
            attribute("file", f.name);
            attribute("description", f.description);
            attribute("mimetype", f.mime);
            if (f.size >= 0) attribute("size", QString::number(f.size));
            attribute("creation", f.created.raw);
            attribute("modification", f.modified.raw);
            if (!f.checksum.isEmpty()) attribute("checksum", QString::fromLatin1(f.checksum.toHex()));
        }
        // Stage 6 T14: a redaction mark's overlay and a sound's numbers, in Acrobat's attribute names.
        if (a.subtype == AnnotationSubtype::Redact) {
            attribute("overlay-text", a.overlayText);
            if (a.repeat) attribute("repeat", QStringLiteral("true"));
        }
        if (a.sound) {
            attribute("bits", QString::number(a.sound->bits));
            attribute("channels", QString::number(a.sound->channels));
            attribute("encoding", QString(soundEncodingName(a.sound->encoding)));
            attribute("rate", QString::number(a.sound->rate));
        }
        out += QStringLiteral(">\n");

        if (!a.contents.isEmpty()) out += QStringLiteral("<contents>") + escapeText(a.contents) + QStringLiteral("</contents>\n");
        richText(a.richText);
        if (!a.defaultAppearance.isEmpty())
            out += QStringLiteral("<defaultappearance>") + escapeText(a.defaultAppearance) + QStringLiteral("</defaultappearance>\n");
        if (!a.vertices.empty()) out += QStringLiteral("<vertices>") + points(a.vertices) + QStringLiteral("</vertices>\n");
        if (!a.inkList.empty()) {
            out += QStringLiteral("<inklist>\n");
            for (const std::vector<double>& stroke : a.inkList) out += QStringLiteral("<gesture>") + points(stroke) + QStringLiteral("</gesture>\n");
            out += QStringLiteral("</inklist>\n");
        }
        if (a.attachment && a.attachment->carried) {
            out += QStringLiteral("<data MODE=\"raw\" encoding=\"hex\" length=\"") + QString::number(a.attachment->data.size())
                   + QStringLiteral("\">") + QString::fromLatin1(a.attachment->data.toHex().toUpper()) + QStringLiteral("</data>\n");
        }
        if (a.sound && a.sound->carried) {   // T14: the samples, as the stream holds them
            out += QStringLiteral("<data MODE=\"raw\" encoding=\"hex\" length=\"") + QString::number(a.sound->samples.size())
                   + QStringLiteral("\">") + QString::fromLatin1(a.sound->samples.toHex().toUpper()) + QStringLiteral("</data>\n");
        }
        if (a.measure) {
            const MeasureDictionary& m = *a.measure;
            out += QStringLiteral("<rp:measure");
            if (m.subtype != QLatin1String("RL")) attribute("subtype", m.subtype);
            attribute("ratio", m.ratio);
            out += QStringLiteral(">\n");
            const auto chain = [this](const char* name, const std::vector<MeasureNumberFormat>& formats) {
                for (const MeasureNumberFormat& f : formats) {
                    out += QStringLiteral("<rp:") + QLatin1String(name);
                    attribute("unit", f.unit);
                    attribute("factor", number(f.factor));
                    attribute("denominator", QString::number(f.denominator));
                    if (f.thousands != QLatin1String(",")) out += QStringLiteral(" thousands=\"") + escapeAttribute(f.thousands) + QLatin1Char('"');
                    if (f.decimal != QLatin1String(".")) out += QStringLiteral(" decimal=\"") + escapeAttribute(f.decimal) + QLatin1Char('"');
                    if (!f.plain) attribute("plain", QStringLiteral("false"));
                    out += QStringLiteral("/>\n");
                }
            };
            chain("x", m.x);
            chain("y", m.y);
            chain("distance", m.distance);
            chain("area", m.area);
            out += QStringLiteral("</rp:measure>\n");
        }
        if (popup != nullptr) {
            out += QStringLiteral("<popup");
            attribute("flags", flagsText(popup->flags));
            if (popup->open) attribute("open", *popup->open ? QStringLiteral("yes") : QStringLiteral("no"));
            attribute("page", QString::number(a.page));
            attribute("rect", numbers({popup->rect.left(), popup->rect.bottom(), popup->rect.right(), popup->rect.top()}));
            out += QStringLiteral("/>\n");
        }
        out += QStringLiteral("</") + QLatin1String(element) + QStringLiteral(">\n");
    }
};

} // namespace

Result<Fdf> XfdfCodec::read(const QByteArray& bytes)
{
    if (bytes.size() > kMaxExchangeBytes)
        return Error{ErrorCode::Unsupported, QLatin1String("error.exchange.tooLarge"), QString::number(bytes.size())};

    Fdf out;
    QXmlStreamReader xml(bytes);
    xml.setNamespaceProcessing(true);
    // The second wall (the header): no entity, declared or not, expands into anything.
    xml.setEntityExpansionLimit(0);
    Reader reader{xml, out, {}, std::nullopt};

    bool sawRoot = false;
    while (!xml.atEnd() && !reader.refusal) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
            return exchangeError("error.exchange.dtd", QString::number(xml.lineNumber()));
        if (token != QXmlStreamReader::StartElement) continue;
        const QStringView name = xml.name();
        if (!sawRoot) {
            if (name != QLatin1String("xfdf"))
                return exchangeError("error.exchange.malformed", QString::number(xml.lineNumber()));
            sawRoot = true;
            continue;
        }
        if (name == QLatin1String("annots")) {
            reader.annots();
        } else if (name == QLatin1String("f")) {
            out.file = xml.attributes().value(QStringLiteral("href")).toString().left(1024);
            xml.skipCurrentElement();
        } else if (name == QLatin1String("ids")) {
            out.originalId = xml.attributes().value(QStringLiteral("original")).toString().left(64);
            out.modifiedId = xml.attributes().value(QStringLiteral("modified")).toString().left(64);
            xml.skipCurrentElement();
        } else {
            reader.note(ExchangeNoteKind::UnknownElement, name.toString());
            xml.skipCurrentElement();
        }
    }
    if (reader.refusal) return *reader.refusal;
    if (xml.hasError() || !sawRoot)
        return exchangeError("error.exchange.malformed", QString::number(xml.lineNumber()));
    return out;
}

QByteArray XfdfCodec::write(const Fdf& exchange)
{
    Writer w;
    w.out += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xfdf xmlns=\"") + kXfdfNamespace
             + QStringLiteral("\" xmlns:rp=\"") + QLatin1String(kRawPageNamespace) + QStringLiteral("\" xml:space=\"preserve\">\n<annots>\n");
    std::map<QString, const Annotation*> popupOf;
    for (const Annotation& a : exchange.annotations) {
        if (a.subtype == AnnotationSubtype::Popup && a.parent && a.parent->kind == AnnotationId::Kind::Created)
            popupOf.emplace(a.parent->nm, &a);
    }
    for (const Annotation& a : exchange.annotations) {
        if (a.subtype == AnnotationSubtype::Popup) continue;
        const auto popup = a.popup ? popupOf.find(a.nm) : popupOf.end();
        w.annotation(a, popup == popupOf.end() ? nullptr : popup->second);
    }
    w.out += QStringLiteral("</annots>\n");
    if (!exchange.file.isEmpty()) w.out += QStringLiteral("<f href=\"") + escapeAttribute(exchange.file) + QStringLiteral("\"/>\n");
    if (!exchange.originalId.isEmpty() || !exchange.modifiedId.isEmpty()) {
        w.out += QStringLiteral("<ids original=\"") + escapeAttribute(exchange.originalId) + QStringLiteral("\" modified=\"")
                 + escapeAttribute(exchange.modifiedId) + QStringLiteral("\"/>\n");
    }
    w.out += QStringLiteral("</xfdf>\n");
    return w.out.toUtf8();
}

} // namespace rawpage::core
