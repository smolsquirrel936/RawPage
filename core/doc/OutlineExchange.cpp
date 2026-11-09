#include "core/doc/OutlineExchange.h"

#include "core/cmd/commands/BookmarkCommands.h"

#include <QStringList>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cmath>

namespace rawpage::core {
namespace {

constexpr QLatin1String kTextHeader{"RawPage bookmarks 1"};

Error malformed(const QString& where)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.outline.importMalformed"), where};
}

QString oneLine(QString text)
{
    for (QChar& c : text) {
        if (c == QLatin1Char('\t') || c == QLatin1Char('\n') || c == QLatin1Char('\r')) c = QLatin1Char(' ');
    }
    return text;
}

QString number(bool has, double value)
{
    return has ? QString::number(value, 'g', 10) : QStringLiteral("-");
}

// `XYZ 72 700 -`, `Fit`, `FitH 700` ... -- the view a text line carries.
QString viewText(const Destination& d)
{
    QString out = QLatin1String(destinationFitName(d.fit == Destination::Fit::Unknown ? Destination::Fit::Fit : d.fit));
    switch (d.fit) {
    case Destination::Fit::XYZ:
        out += QLatin1Char(' ') + number(d.hasX, d.x) + QLatin1Char(' ') + number(d.hasY, d.y) + QLatin1Char(' ') + number(d.hasZoom, d.zoom);
        break;
    case Destination::Fit::FitH:
    case Destination::Fit::FitBH:
        out += QLatin1Char(' ') + number(d.hasY, d.y);
        break;
    case Destination::Fit::FitV:
    case Destination::Fit::FitBV:
        out += QLatin1Char(' ') + number(d.hasX, d.x);
        break;
    default:
        break;
    }
    return out;
}

// One number of a view: `-` is a null; anything else must be finite and of a sane size.
bool parseNumber(const QString& text, bool& has, double& value)
{
    if (text == QLatin1String("-")) return true;
    bool ok = false;
    const double parsed = text.toDouble(&ok);
    if (!ok || !std::isfinite(parsed) || std::fabs(parsed) > 1.0e7) return false;
    has = true;
    value = parsed;
    return true;
}

bool parseView(const QString& text, Destination& d)
{
    const QStringList parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    const std::optional<Destination::Fit> fit = destinationFitFromName(parts.front());
    if (!fit) return false;
    d.fit = *fit;
    switch (d.fit) {
    case Destination::Fit::XYZ:
        return parts.size() == 4 && parseNumber(parts[1], d.hasX, d.x) && parseNumber(parts[2], d.hasY, d.y)
               && parseNumber(parts[3], d.hasZoom, d.zoom);
    case Destination::Fit::FitH:
    case Destination::Fit::FitBH:
        return parts.size() == 2 && parseNumber(parts[1], d.hasY, d.y);
    case Destination::Fit::FitV:
    case Destination::Fit::FitBV:
        return parts.size() == 2 && parseNumber(parts[1], d.hasX, d.x);
    default:
        return parts.size() == 1;
    }
}

// A destination to page `page` (from 1) of the document: unresolved -- the title kept, going nowhere -- past its end.
void place(OutlineItem& item, int page, Destination view, int pageCount)
{
    if (page < 1 || page > pageCount) return;
    view.pageIndex = page - 1;
    item.action = OutlineAction::goTo(view);
    item.destination = view;
}

void writeText(QString& out, const std::vector<OutlineItem>& items, int depth)
{
    for (const OutlineItem& item : items) {
        out += QString(depth, QLatin1Char('\t')) + oneLine(item.title);
        if (item.action.kind == OutlineAction::Kind::GoTo && item.action.destination.isResolved())
            out += QLatin1Char('\t') + QString::number(item.action.destination.pageIndex + 1) + QLatin1Char('\t')
                   + viewText(item.action.destination);
        out += QLatin1Char('\n');
        writeText(out, item.children, depth + 1);
    }
}

void writeXml(QXmlStreamWriter& xml, const std::vector<OutlineItem>& items)
{
    for (const OutlineItem& item : items) {
        xml.writeStartElement(QStringLiteral("bookmark"));
        xml.writeAttribute(QStringLiteral("title"), item.title);
        const OutlineAction& a = item.action;
        if (a.kind == OutlineAction::Kind::GoTo) {
            if (a.destination.isResolved()) {
                xml.writeAttribute(QStringLiteral("page"), QString::number(a.destination.pageIndex + 1));
                xml.writeAttribute(QStringLiteral("view"), viewText(a.destination));
            }
        } else if (a.kind != OutlineAction::Kind::None && a.kind != OutlineAction::Kind::Other) {
            xml.writeAttribute(QStringLiteral("action"), QLatin1String(outlineActionKindName(a.kind)));
            if (!a.file.isEmpty()) xml.writeAttribute(QStringLiteral("file"), a.file);
            if (!a.uri.isEmpty()) xml.writeAttribute(QStringLiteral("uri"), a.uri);
            if (!a.script.isEmpty()) xml.writeAttribute(QStringLiteral("script"), a.script);
            if (!a.name.isEmpty()) xml.writeAttribute(QStringLiteral("name"), a.name);
            if (a.kind == OutlineAction::Kind::GoToRemote && a.name.isEmpty())
                xml.writeAttribute(QStringLiteral("remotePage"), QString::number(std::max(0, a.destination.pageIndex) + 1));
        }
        if (item.openByDefault) xml.writeAttribute(QStringLiteral("open"), QStringLiteral("true"));
        if (item.bold) xml.writeAttribute(QStringLiteral("bold"), QStringLiteral("true"));
        if (item.italic) xml.writeAttribute(QStringLiteral("italic"), QStringLiteral("true"));
        if (item.colour) {
            const auto channel = [](double v) {
                return QStringLiteral("%1").arg(static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)), 2, 16, QLatin1Char('0'));
            };
            xml.writeAttribute(QStringLiteral("color"),
                               QLatin1Char('#') + channel((*item.colour)[0]) + channel((*item.colour)[1]) + channel((*item.colour)[2]));
        }
        writeXml(xml, item.children);
        xml.writeEndElement();
    }
}

struct Reader {
    int pageCount = 0;
    QString key;
    int ordinal = 0;
    std::size_t nodes = 0;

    OutlineItem fresh(const QString& title)
    {
        OutlineItem item;
        item.id = newBookmarkId(key, ordinal++);
        item.title = title.left(kMaxOutlineTitleLength);
        return item;
    }
};

Result<std::vector<OutlineItem>> readText(const QString& text, Reader& reader)
{
    std::vector<OutlineItem> roots;
    std::vector<std::vector<OutlineItem>*> levels{&roots};
    const QStringList lines = text.split(QLatin1Char('\n'));
    int lineNumber = 0;
    for (QString line : lines) {
        ++lineNumber;
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (lineNumber == 1 && line == kTextHeader) continue;
        if (line.trimmed().isEmpty()) continue;
        int depth = 0;
        while (depth < line.size() && line[depth] == QLatin1Char('\t')) ++depth;
        const QStringList fields = line.mid(depth).split(QLatin1Char('\t'));
        const QString where = QStringLiteral("line %1").arg(lineNumber);
        if (fields.size() != 1 && fields.size() != 3) return malformed(where);
        if (depth >= static_cast<int>(levels.size()) || depth >= kMaxOutlineDepth) return malformed(where);
        if (++reader.nodes > kMaxOutlineNodes) return malformed(where);
        OutlineItem item = reader.fresh(fields.front());
        if (fields.size() == 3) {
            bool ok = false;
            const int page = fields[1].toInt(&ok);
            Destination view;
            if (!ok || !parseView(fields[2], view)) return malformed(where);
            place(item, page, view, reader.pageCount);
        }
        levels.resize(static_cast<std::size_t>(depth) + 1);
        std::vector<OutlineItem>* into = levels.back();
        into->push_back(std::move(item));
        levels.push_back(&into->back().children);
    }
    return roots;
}

Result<std::vector<OutlineItem>> readXml(const QByteArray& bytes, Reader& reader)
{
    QXmlStreamReader xml(bytes);
    std::vector<OutlineItem> roots;
    // The open elements: each `<bookmark>`'s item, by a path of indices from the top -- a pointer into a vector that a
    // sibling's arrival can move would be a pointer into freed memory.
    std::vector<std::size_t> path;
    const auto childrenAt = [&roots](const std::vector<std::size_t>& at) {
        std::vector<OutlineItem>* level = &roots;
        for (const std::size_t i : at) level = &(*level)[i].children;
        return level;
    };
    bool sawRoot = false;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        const QString where = QStringLiteral("line %1").arg(xml.lineNumber());
        // NO DTD, AND SO NO ENTITY: refused whole, before anything it declares could be expanded (see the header).
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference) return malformed(where);
        if (token == QXmlStreamReader::StartElement) {
            if (!sawRoot) {
                if (xml.name() != QLatin1String("bookmarks")) return malformed(where);
                sawRoot = true;
                continue;
            }
            if (xml.name() != QLatin1String("bookmark")) return malformed(where);
            if (static_cast<int>(path.size()) >= kMaxOutlineDepth || ++reader.nodes > kMaxOutlineNodes) return malformed(where);
            const QXmlStreamAttributes attributes = xml.attributes();
            OutlineItem item = reader.fresh(attributes.value(QLatin1String("title")).toString());
            if (attributes.hasAttribute(QLatin1String("page"))) {
                bool ok = false;
                const int page = attributes.value(QLatin1String("page")).toInt(&ok);
                Destination view;
                view.fit = Destination::Fit::Fit;
                if (!ok) return malformed(where);
                if (attributes.hasAttribute(QLatin1String("view")) && !parseView(attributes.value(QLatin1String("view")).toString(), view))
                    return malformed(where);
                place(item, page, view, reader.pageCount);
            } else if (attributes.hasAttribute(QLatin1String("action"))) {
                const std::optional<OutlineAction::Kind> kind =
                    outlineActionKindFromName(attributes.value(QLatin1String("action")).toString());
                if (!kind || *kind == OutlineAction::Kind::Other || *kind == OutlineAction::Kind::GoTo) return malformed(where);
                OutlineAction& a = item.action;
                a.kind = *kind;
                a.file = attributes.value(QLatin1String("file")).toString().left(kMaxOutlineTextLength);
                a.uri = attributes.value(QLatin1String("uri")).toString().left(kMaxOutlineTextLength);
                a.script = attributes.value(QLatin1String("script")).toString().left(kMaxOutlineTextLength);
                a.name = attributes.value(QLatin1String("name")).toString().left(kMaxOutlineTitleLength);
                if (a.kind == OutlineAction::Kind::GoToRemote) {
                    a.destination.fit = Destination::Fit::Fit;
                    a.destination.pageIndex = std::max(0, attributes.value(QLatin1String("remotePage")).toInt() - 1);
                }
            }
            item.openByDefault = attributes.value(QLatin1String("open")) == QLatin1String("true");
            item.bold = attributes.value(QLatin1String("bold")) == QLatin1String("true");
            item.italic = attributes.value(QLatin1String("italic")) == QLatin1String("true");
            const QString colour = attributes.value(QLatin1String("color")).toString();
            if (!colour.isEmpty()) {
                bool ok = colour.size() == 7 && colour.startsWith(QLatin1Char('#'));
                const uint rgb = ok ? colour.mid(1).toUInt(&ok, 16) : 0;
                if (!ok) return malformed(where);
                item.colour = std::array<double, 3>{((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0};
            }
            std::vector<OutlineItem>* level = childrenAt(path);
            level->push_back(std::move(item));
            path.push_back(level->size() - 1);
        } else if (token == QXmlStreamReader::EndElement) {
            if (xml.name() == QLatin1String("bookmark")) {
                if (path.empty()) return malformed(where);
                path.pop_back();
            }
        }
    }
    if (xml.hasError() || !sawRoot) return malformed(QStringLiteral("line %1").arg(xml.lineNumber()));
    return roots;
}

} // namespace

QByteArray writeOutlineExchange(const std::vector<OutlineItem>& items, OutlineFormat format)
{
    if (format == OutlineFormat::Text) {
        QString out = kTextHeader + QLatin1Char('\n');
        writeText(out, items, 0);
        return out.toUtf8();
    }
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("bookmarks"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1"));
    writeXml(xml, items);
    xml.writeEndElement();
    xml.writeEndDocument();
    return out;
}

Result<std::vector<OutlineItem>> readOutlineExchange(const QByteArray& bytes, int pageCount, const QString& key)
{
    if (bytes.size() > kMaxOutlineExchangeBytes) return malformed(QStringLiteral("size"));
    Reader reader;
    reader.pageCount = pageCount;
    reader.key = key;
    const QByteArray trimmed = bytes.trimmed();
    const bool xml = trimmed.startsWith('<') || trimmed.startsWith("\xEF\xBB\xBF<");
    QString text = xml ? QString() : QString::fromUtf8(bytes);
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);   // a byte-order mark is not the first title's
    Result<std::vector<OutlineItem>> read = xml ? readXml(bytes, reader) : readText(text, reader);
    if (!read) return read.error();
    if (read.value().empty()) return Error{ErrorCode::NotFound, QLatin1String("error.outline.nothingToAdd"), QString()};
    return read;
}

} // namespace rawpage::core
