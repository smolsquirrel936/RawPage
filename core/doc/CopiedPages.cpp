// core/doc/CopiedPages -- the clipboard's form of a copied graph. Stage 7 T8 (D7-13). See the header.

#include "core/doc/CopiedPages.h"

#include "core/cmd/commands/PageCommands.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>

namespace rawpage::core {
namespace {

Error malformed(const char* field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.clipboard.malformed"), QLatin1String(field)};
}

} // namespace

QByteArray encodeCopiedPages(const CopiedPages& pages)
{
    const QJsonObject payload{{QStringLiteral("version"), 1},
                              {QStringLiteral("pages"), pages.pageCount},
                              {QStringLiteral("graph"), QString::fromLatin1(pages.graph.toBase64())}};
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

Result<CopiedPages> decodeCopiedPages(const QByteArray& bytes)
{
    // THE BOUND FIRST, before the JSON parser sees a byte: base64 of the largest graph, and the keys around it.
    if (bytes.size() > (kMaxCopiedGraphBytes / 3 + 1) * 4 + 1024) return malformed("size");
    QJsonParseError error{};
    const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject()) return malformed("json");
    const QJsonObject root = parsed.object();

    const QJsonValue version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != 1.0) return malformed("version");

    const QJsonValue count = root.value(QStringLiteral("pages"));
    const double pages = count.isDouble() ? count.toDouble() : -1.0;
    if (!std::isfinite(pages) || std::trunc(pages) != pages || pages < 1.0 || pages > static_cast<double>(kMaxPagesPerCommand))
        return malformed("pages");

    const QJsonValue graph = root.value(QStringLiteral("graph"));
    if (!graph.isString()) return malformed("graph");
    const QByteArray encoded = graph.toString().toLatin1();
    const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() > kMaxCopiedGraphBytes || !decoded.decoded.startsWith("%PDF-")) return malformed("graph");

    CopiedPages out;
    out.graph = decoded.decoded;
    out.pageCount = static_cast<int>(pages);
    return out;
}

} // namespace rawpage::core
