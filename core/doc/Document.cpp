// core/doc/Document -- implementation. Stage 3 T4. See the header for what the model holds.

#include "core/doc/Document.h"

#include <QCryptographicHash>

#include <cstring>

namespace rawpage::core {

namespace {

// The hash input is built by hand, big-endian and fixed-width, so the bytes -- and the hash -- do not
// depend on the host's endianness or on a Qt stream version. Not `QDataStream`: that needs a
// `QIODevice` open mode, which tools/layer-lint reads as a filesystem write, and a waiver for an
// in-memory buffer would put a false entry in the waiver census.
void addInt(QByteArray& out, qint64 value)
{
    const quint64 bits = static_cast<quint64>(value);
    for (int shift = 56; shift >= 0; shift -= 8)
        out.append(static_cast<char>((bits >> shift) & 0xFF));
}

void addReal(QByteArray& out, double value)
{
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    addInt(out, static_cast<qint64>(bits));
}

void addRect(QByteArray& out, const PdfRect& rect)
{
    addReal(out, rect.origin.x);
    addReal(out, rect.origin.y);
    addReal(out, rect.size.width);
    addReal(out, rect.size.height);
}

} // namespace

QByteArray Document::modelHash() const
{
    QByteArray buffer;
    addInt(buffer, pages_.count());
    for (const Page& page : pages_.pages()) {
        addInt(buffer, page.index());
        addInt(buffer, static_cast<qint64>(page.rotation()));
        addRect(buffer, page.mediaBox());
        addRect(buffer, page.cropBox());
        addRect(buffer, page.bleedBox());
        addRect(buffer, page.trimBox());
        addRect(buffer, page.artBox());
        const QByteArray label = page.label().toUtf8();
        addInt(buffer, label.size());       // length-prefixed, so one label cannot run into the next page
        buffer.append(label);
    }
    return QCryptographicHash::hash(buffer, QCryptographicHash::Sha256);
}

Status Document::hydrate(const IDocumentBackend& backend, DocumentHandle handle)
{
    const PageTree* pages = backend.pages(handle);
    if (pages == nullptr) {
        return Error{ErrorCode::NotFound, QLatin1String("error.open.notFound"),
                     QString::number(static_cast<qulonglong>(handle.id))};
    }

    // Everything is read before anything is assigned, so a failure above leaves the document as it
    // was -- the same "partial application is a bug" rule a command lives under.
    const RepairReport* repair = backend.repairReport(handle);
    const Permissions* permissions = backend.permissions(handle);
    const DocumentId* id = backend.documentId(handle);

    handle_ = handle;
    pages_ = *pages;
    repair_ = repair ? *repair : RepairReport{};
    permissions_ = permissions ? *permissions : Permissions{};
    id_ = id ? *id : DocumentId{};
    revision_ = Revision::initial();
    return Status{};
}

} // namespace rawpage::core
