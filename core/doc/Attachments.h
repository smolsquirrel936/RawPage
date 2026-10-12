// core/doc/Attachments -- the files a document carries, as the model holds them. Stage 6 T11, decision D6-17
// (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// TWO KINDS OF ATTACHMENT, ONE FILE TYPE
// ===========================================================================================
// A PDF carries a file in two places (ISO 32000-1 §7.11.4 and §12.5.6.15): the catalog's `/Names
// /EmbeddedFiles` name tree -- a file attached to the document as a whole -- and a `FileAttachment`
// annotation's `/FS` -- a file attached at a place on a page, drawn as a pushpin. Both are a file
// specification with an `/EF`, so both are a `core::EmbeddedFile` (`core/doc/Annotation.h`); THIS class holds
// the first kind, and the annotation store holds the second, because a `FileAttachment` is an annotation in
// every other respect -- moved, deleted, replied to and listed with the comments. The Attachments pane shows
// both (`ui/panes/AttachmentsPane`), and `AttachmentRef` names either.
//
// ===========================================================================================
// A DOCUMENT-LEVEL FILE IS ITS KEY
// ===========================================================================================
// The name tree maps a STRING to a file specification, and its keys are unique within the tree -- so the key is
// the one name for the file that an incremental save never changes, and the journal can replay against
// (D6-4's argument for an annotation's object number). A file RawPage attaches is given its own file name as its
// key, made unique (`uniqueKey`) when the tree already holds one -- `readme.txt`, then `readme (2).txt`.
//
// THE ORDER IS THE TREE'S. A name tree is sorted by its keys' bytes (ISO 32000-1 §7.9.6), so the files are kept in
// the order of `nameTreeKeyBytes` -- the bytes RawPage writes a key as -- and an insertion goes where its key
// sorts. A file read from a document whose producer wrote its key in another encoding keeps the place the
// producer gave it; the sink orders what it writes by the bytes it writes (`pdf/ops/MutationSinkImpl`).
//
// ===========================================================================================
// THE POLICY HOOK (D6-17)
// ===========================================================================================
// `isExecutableName` is the FileMap row's *dangerous-type policy hook*: a fixed list of the extensions Windows
// runs or interprets, refused BEFORE anything is written when a person asks to open an attachment. Stage 10's
// `core/security/TrustPolicy` replaces it with a policy; until then nothing an attachment carries is ever run.
// `safeFileName` is the other half: what an attachment's name becomes as a file on disk -- no folder, no
// character Windows refuses, no device name, no trailing dot or space -- so an extract and an open write exactly
// one file, where they were asked to.
//
// Pure: QtCore only, no engine, no display -- `tst_attachments` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_ATTACHMENTS_H
#define RAWPAGE_CORE_DOC_ATTACHMENTS_H

#include "core/Result.h"
#include "core/doc/Annotation.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rawpage::core {

// THE BOUNDS OF AN ATTACHMENT A COMMAND CARRIES -- the recovery journal is a file on disk, so its parser is a parser
// of hostile input (Stage 3's guide case A-45). 256 MiB is past any file a reviewer attaches and short of what a
// 32-bit length or a base64 line on the UI thread would make dangerous; a larger file is refused when it is chosen.
inline constexpr qsizetype kMaxAttachmentBytes = qsizetype{256} * 1024 * 1024;
inline constexpr qsizetype kMaxAttachmentNameLength = 1024;
inline constexpr std::size_t kMaxAttachmentsPerCommand = 1000;

// A file attached to the document as a whole: its key in `/Names /EmbeddedFiles`, and the file.
struct DocumentAttachment {
    QString key;
    EmbeddedFile file;

    // `{"key": …, "file": {…}}` -- the form an attachment command carries.
    QJsonObject toJson() const;
    static Result<DocumentAttachment> fromJson(const QJsonObject& json);

    bool operator==(const DocumentAttachment&) const = default;
};

// WHICH ATTACHMENT: a document-level file by its key, or a `FileAttachment` annotation by its identity. What the
// pane, the extract and the open name, and what `IDocumentBackend::attachmentData` reads.
struct AttachmentRef {
    enum class Kind : std::uint8_t { Document, Annotation };

    Kind kind = Kind::Document;
    QString key;                 // Kind::Document
    AnnotationId annotation;     // Kind::Annotation

    static AttachmentRef ofDocument(const QString& key);
    static AttachmentRef ofAnnotation(const AnnotationId& id);
    bool operator==(const AttachmentRef&) const = default;
};

class Attachments {
public:
    // Unassessed: `status()` refuses, which refuses every change -- what a backend that read no attachments gives.
    Attachments();

    // The document's files as read, in the tree's order. Never fails: a document with none is an empty success.
    static Attachments fromReading(std::vector<DocumentAttachment> files);
    // A document whose attachments could not be read -- an encrypted one (D2-6): empty, and SAYS WHY.
    static Attachments refused(Error why);

    const Status& status() const noexcept { return status_; }
    const std::vector<DocumentAttachment>& files() const noexcept { return files_; }
    std::size_t size() const noexcept { return files_.size(); }
    std::optional<std::size_t> find(const QString& key) const;

    // Inserts `file` where its key sorts. Refused, changing nothing: an unassessed or refused store, an empty
    // key, or a key the store already holds.
    Status insert(DocumentAttachment file);
    // Removes the file with `key` and returns it; refused, changing nothing, for a key the store does not hold.
    Result<DocumentAttachment> remove(const QString& key);

    // `name` when no file has it as its key; else `stem (2).ext`, `stem (3).ext`, … -- the first free.
    QString uniqueKey(const QString& name) const;

    // Every file, every field, in order -- `Document::modelHash`'s input.
    QByteArray digest() const;

private:
    Status status_;
    std::vector<DocumentAttachment> files_;
};

// The bytes a name tree key is written as: the key as ASCII when every character is printable ASCII, else
// UTF-16BE behind its byte-order mark -- `pdf/ops/AnnotationOps::textString`'s rule, so the order the model keeps
// is the order the writer writes.
QByteArray nameTreeKeyBytes(const QString& key);

// THE FILE RAWPAGE ATTACHES: `name` (a file name, its folder dropped), `bytes` carried, the description, the
// type `mimeTypeForName` gives, the size, the MD5 `/CheckSum` ISO 32000-1 Table 46 names, and both dates at
// `utcNow`.
EmbeddedFile newEmbeddedFile(const QString& name, QByteArray bytes, const QString& description, std::int64_t utcNow);

// A FileAttachment annotation RawPage creates at `rect` on `page`, carrying `file`, with the `PushPin` icon
// Acrobat writes by default, its `/Contents` the file's name (what Acrobat writes and what the Comments pane
// shows), by `author` at `utcNow`.
Annotation newFileAttachment(int page, const PdfRect& rect, EmbeddedFile file, const std::vector<double>& colour,
                             const QString& author, std::int64_t utcNow);

// The MIME type of a file by its extension, from a fixed table; `application/octet-stream` for any other.
// The engine's own reading of the bytes decides whether a PDF is one (`IDocumentBackend::openBytes`).
QString mimeTypeForName(const QString& name);

// D6-17's policy hook -- see the header. True for a name whose LAST extension, after the trailing dots and spaces
// Windows ignores, is one Windows runs or interprets.
bool isExecutableName(const QString& name);
// Whether the file says it is a PDF -- by its type or its extension. The engine has the last word: a file that
// says so and is not one fails to open, by key.
bool isPdfAttachment(const EmbeddedFile& file);

// THIS SESSION'S FOLDER for an opened attachment (D6-17): `%LOCALAPPDATA%\RawPage\attachments\<process id>` --
// GenericDataLocation, `core::RecoveryJournal::defaultRoot`'s, so the two sit side by side. Asked for by
// `app/main.cpp` alone; a session's folder is its own, so two instances never share one.
QString attachmentSessionFolder();

// What `name` becomes as ONE file in a folder: its folder dropped, every character Windows refuses in a name
// and every control character replaced by `_`, trailing dots and spaces dropped, a device name (`CON`, `LPT1` …)
// prefixed with `_`, at most 200 characters keeping the extension; `attachment` when nothing is left.
QString safeFileName(const QString& name);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_ATTACHMENTS_H
