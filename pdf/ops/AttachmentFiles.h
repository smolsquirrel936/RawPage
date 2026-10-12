// pdf/ops/AttachmentFiles -- an attachment's bytes, read from a revision and written to a file. Stage 6 T11, decision
// D6-17 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// WHY THE WRITE IS HERE
// ===========================================================================================
// Extract writes an attachment's bytes to a path a person chose, and opening a non-PDF attachment writes them into
// the session folder (`%LOCALAPPDATA%\RawPage\attachments\<session>\`) for the shell to open. Both are a file
// written with bytes the DOCUMENT supplied -- the file format's business -- so both are written by `pdf/`, which
// layer-lint lets write, and neither needs a waiver: D6-17 promised the census would stay at fifteen, and this is
// how. `write` is the one function both backends call (`pdf/DocumentBackendImpl`, `tests/unit/CosBackend.h`), so
// the shipped product and the headless suite write an extract the same way.
//
// THROUGH `QSaveFile`: the bytes go to a temporary file beside the target and are renamed over it only when every
// byte is written, so a failed extract leaves no file at the path -- not a truncated one a person would mistake for
// the attachment. A file already at the path is refused unless the caller says to replace it.
//
// `read` is the backend's reading for a document it holds NO SINK for -- an encrypted or otherwise read-only one --
// over a table loaded for the call; with a sink, `MutationSinkImpl::attachmentData` answers, because it knows what
// the document holds now, unsaved attachments included.
//
// Portable, in `rawpage_ops`: no engine.

#ifndef RAWPAGE_PDF_OPS_ATTACHMENTFILES_H
#define RAWPAGE_PDF_OPS_ATTACHMENTFILES_H

#include "core/Result.h"
#include "core/doc/Attachments.h"
#include "pdf/cos/XrefTable.h"

#include <QByteArray>
#include <QString>

namespace rawpage::pdf::ops {

class AttachmentFiles {
public:
    // `attachment`'s bytes as `table` holds them: a document-level file by its key in the name tree, a FileAttachment
    // by its object number (a direct annotation dictionary has none, and is `NotFound`).
    static core::Result<QByteArray> read(cos::XrefTable& table, const core::AttachmentRef& attachment);

    // `bytes` to `path`, its folder created when absent. Refused by `error.attachment.exists` for a path that holds a
    // file already, unless `overwrite`; by `error.attachment.writeFailed` for one that cannot be written. On any
    // failure no file is left at `path`.
    static core::Status write(const QByteArray& bytes, const QString& path, bool overwrite);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_ATTACHMENTFILES_H
