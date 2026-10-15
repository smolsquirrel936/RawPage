// pdf/ops/FdfCodec -- FDF, the Forms Data Format (ISO 32000-1 §12.7.8), read into and written from the exchange model
// `core/doc/Fdf`; and the one door an exchange FILE goes through, either syntax. Stage 6 T13, decision D6-20
// (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// AN FDF IS PDF SYNTAX, SO IT IS READ BY THE PDF READER
// ===========================================================================================
// An FDF file is objects and a trailer -- usually with NO cross-reference table (ISO 32000-1 §12.7.8.1 lets an FDF
// leave it out, and Acrobat does) -- so it is opened with `pdf/cos/XrefRepair::openWithRepair`, whose scan for `N G
// obj` is exactly how a file without a table is read, and its annotations are the same dictionaries a page's
// `/Annots` lists: each is read by `pdf/ops/AnnotationReader::parseDictionary`, the first-party reader's own code, so
// a field an FDF carries means what it means in a PDF. What differs is how they are tied together: `/Page` is the
// page's INDEX, and `/IRT`, `/Popup` and `/Parent` point at other objects OF THE FDF -- which this reader turns into
// the exchange's names (`core/doc/Fdf.h`). A reference to an object that is no annotation of the file is dropped and
// noted. A file attachment's bytes are read from its `/FS`'s `/EF` stream, as a PDF's are.
//
// Written the same way round: `pdf/ops/AnnotationOps::exchangeDictionary` -- the same field table a created
// annotation is written from -- each annotation an object, a file attachment's file an `/EmbeddedFile` stream behind
// a file specification, the catalog's `/FDF` naming them, and no cross-reference table.
//
// ===========================================================================================
// THE FILE
// ===========================================================================================
// `readFile` is the import's one read: the file's size bounded first, then its first bytes say which syntax -- `%FDF-`
// is FDF, `<` (after a byte-order mark and white space) XFDF, which `core/doc/XfdfCodec` reads -- and anything else is
// refused by `error.exchange.notExchange`. `writeFile` is the export's one write, through `pdf/ops/AttachmentFiles`
// (`QSaveFile`): an exchange file reaches the disk through `pdf/`, so no layer-lint waiver (D6-20).

#ifndef RAWPAGE_PDF_OPS_FDFCODEC_H
#define RAWPAGE_PDF_OPS_FDFCODEC_H

#include "core/Result.h"
#include "core/doc/Fdf.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <string>

namespace rawpage::pdf::ops {

class FdfCodec {
public:
    // An FDF's annotations. Refused by key: `error.exchange.tooLarge`, `error.exchange.notExchange` for bytes that
    // do not begin `%FDF-`, `error.exchange.malformed` for one with no `/FDF` dictionary, `error.exchange.tooMany`.
    static core::Result<core::Fdf> read(const QByteArray& bytes);
    // `exchange` as an FDF file.
    static std::string write(const core::Fdf& exchange);

    // Either syntax, told by its first bytes (see the header).
    static core::Result<core::Fdf> readEither(const QByteArray& bytes);
    // `path` read and decoded; `error.exchange.unreadable` for a file that cannot be read.
    static core::Result<core::Fdf> readFile(const QString& path);
    // `exchange` written to `path` in `format`. A file already there is replaced only when `overwrite`.
    static core::Status writeFile(const core::Fdf& exchange, core::ExchangeFormat format, const QString& path,
                                  bool overwrite);

    // Every FileAttachment of `exchange` whose file is not carried, given its bytes -- `read` asked for the annotation
    // `exchange.sources` says it came from. What an export does before it writes: the model holds what a file
    // specification SAYS, never the bytes (D6-38(b)). Refused by the first read that fails, by its key.
    static core::Status carryFiles(core::Fdf& exchange,
                                   const std::function<core::Result<QByteArray>(const core::AnnotationId&)>& read);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_FDFCODEC_H
