// pdf/ops/OutlineFiles -- a bookmark tree read from, and written to, a file of its own. Stage 7 T11, decision D7-10
// (`Stage7-Assembly-Plan.md` §3: *import and export ... written and read through `pdf/ops`, so no waiver*).
//
// The syntax is `core/doc/OutlineExchange`'s, QtCore only; the FILE is this layer's, which layer-lint lets write, so the
// census stays at fifteen (A-09). The write goes through `AttachmentFiles::write` -- `QSaveFile`, so a failed export
// leaves no file at the path, and a file already there is replaced only when the caller says so -- and both backends
// call these two (`pdf/DocumentBackendImpl`, `tests/unit/CosBackend.h`), so the product and the headless suite read and
// write a bookmark file the same way.

#ifndef RAWPAGE_PDF_OPS_OUTLINEFILES_H
#define RAWPAGE_PDF_OPS_OUTLINEFILES_H

#include "core/Result.h"
#include "core/doc/OutlineExchange.h"

#include <QString>

#include <vector>

namespace rawpage::pdf::ops {

class OutlineFiles {
public:
    // The tree `path` holds -- `core::readOutlineExchange`'s refusals, and `error.outline.importUnreadable` for a file
    // that will not open or is past `core::kMaxOutlineExchangeBytes`.
    static core::Result<std::vector<core::OutlineItem>> read(const QString& path, int pageCount, const QString& key);
    // `items` written to `path` in `format`. `error.outline.exportExists` for a file there and not `overwrite`;
    // `error.outline.exportFailed` for a write that did not finish (and left nothing).
    static core::Status write(const std::vector<core::OutlineItem>& items, core::OutlineFormat format, const QString& path,
                              bool overwrite);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_OUTLINEFILES_H
