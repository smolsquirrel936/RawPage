// core/doc/DocumentId -- which file a document is, stated so that "is it still that file?" has an
// answer. Stage 3 T4.
//
// FOUR FACTS, taken when the document is opened: the path, the size, the modification time, and a
// SHA-256 of the bytes. Two consumers, and each needs all four:
//
//   * `core/doc/SaveCoordinator` (T8, D3-16) checks the target is still the file that was opened
//     before appending an update computed against its bytes. An update appended to different bytes
//     has a `/Prev` pointing into someone else's content, so the CONTENT is compared, not only the
//     cheap facts: a same-size, same-timestamp edit -- a sync tool, a restore, a second program
//     writing the file back quickly -- is exactly the change that path, size and time all miss.
//   * `core/cmd/RecoveryJournal` (T9, D3-11) keys a crashed session's directory by it and decides
//     whether the file on disk is the one the journal was written against.
//
// -------------------------------------------------------------------------------------------
// THE PATH IS NORMALIZED BY HAND, and the rule is Windows' (convention 11)
// -------------------------------------------------------------------------------------------
// Every path this product sees is a Windows path; `core/` also builds on Linux only so the
// sanitizers can run, and `QDir::cleanPath` converts PLATFORM-NATIVE separators -- so a host
// function would give the two builds two different identities for one file, which is Stage 1's I-17
// in a new place. So: `\` becomes `/`, repeated separators collapse (a leading `//` survives, because
// a UNC path starts with it), `.` segments go, `..` segments resolve without climbing past the root,
// a trailing separator goes, and the whole path is CASE-FOLDED, because NTFS compares names that
// way by default and `C:\Docs\A.pdf` and `c:/docs/a.pdf` are one file. What is NOT resolved: links,
// junctions, `8.3` short names, and a relative path's base -- each needs the filesystem, which a
// value type in `core/` does not consult. A caller hands it an absolute path.
//
// WHAT IT DOES NOT DO. It reads no file and stats nothing: the backend, which already holds the
// bytes it opened, supplies them. The hash is over the whole file, which is a real cost on a large
// one; T8 measures it rather than assuming it is free (D3-16).

#ifndef RAWPAGE_CORE_DOC_DOCUMENTID_H
#define RAWPAGE_CORE_DOC_DOCUMENTID_H

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include <QStringList>

namespace rawpage::core {

struct DocumentId {
    QString path;               // normalized; see the header
    qint64 size = -1;           // bytes
    qint64 modifiedMsecs = 0;   // milliseconds since the epoch, UTC
    QByteArray contentHash;     // SHA-256 of the bytes, raw

    bool isValid() const noexcept { return size >= 0 && !path.isEmpty() && !contentHash.isEmpty(); }

    friend bool operator==(const DocumentId& a, const DocumentId& b)
    {
        return a.size == b.size && a.modifiedMsecs == b.modifiedMsecs && a.path == b.path
               && a.contentHash == b.contentHash;
    }
    friend bool operator!=(const DocumentId& a, const DocumentId& b) { return !(a == b); }

    // The identity of `bytes` read from `path`, last modified at `modifiedMsecs`.
    static DocumentId of(const QString& path, qint64 modifiedMsecs, const QByteArray& bytes)
    {
        DocumentId id;
        id.path = normalizePath(path);
        id.size = bytes.size();
        id.modifiedMsecs = modifiedMsecs;
        id.contentHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        return id;
    }

    // Twenty hex digits naming this identity, for a directory name (`<DocumentId>-<pid>-<n>`,
    // D3-11). Derived from all four facts, so a changed file is a different directory.
    QString key() const
    {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(path.toUtf8());
        hash.addData(QByteArray::number(size));
        hash.addData(QByteArray::number(modifiedMsecs));
        hash.addData(contentHash);
        return QString::fromLatin1(hash.result().toHex().left(20));
    }

    static QString normalizePath(const QString& raw)
    {
        QString text = raw;
        text.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const bool unc = text.startsWith(QLatin1String("//"));

        QStringList kept;
        const QStringList segments = text.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString& segment : segments) {
            if (segment == QLatin1String("."))
                continue;
            if (segment == QLatin1String("..")) {
                // Never above the root: a drive (`c:`) or a UNC server and share stay put.
                const int floor = unc ? 2 : (!kept.isEmpty() && kept.front().endsWith(QLatin1Char(':')) ? 1 : 0);
                if (kept.size() > floor)
                    kept.removeLast();
                continue;
            }
            kept.append(segment);
        }

        QString result = kept.join(QLatin1Char('/'));
        if (unc)
            result.prepend(QLatin1String("//"));
        else if (text.startsWith(QLatin1Char('/')))
            result.prepend(QLatin1Char('/'));
        return result.toCaseFolded();
    }
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_DOCUMENTID_H
