// pdf/ops/EmbeddedFileReader -- each embedded file's name and DESCRIPTION, read first-party. Stage 5 T8,
// decision D5-12 (`Stage5-TextSearch-Plan.md` §3).
//
// ===========================================================================================
// WHY THIS IS FIRST-PARTY WHEN D4-3 SAYS TO READ THROUGH PDFIUM.
// ===========================================================================================
// **Because PDFium cannot** -- the second read in the project to reach D4-3's *unless PDFium cannot*,
// after `TransitionReader`. A search of attachments matches an embedded file's name and its
// description (D5-12), and the pinned engine's attachment API reads the name (`FPDFAttachment_GetName`),
// the bytes (`_GetFile`) and the MIME type, but its `FPDFAttachment_GetStringValue` reads the embedded
// file's PARAMS dictionary -- `/Size`, `/CheckSum`, dates -- and never the file specification's `/Desc`
// (`third_party/pdfium/include/fpdf_attachment.h`, read at T8). So the description, and only the
// description, is read here; the name the engine reports is what search units are built from, and the
// bytes are the engine's. **The name is read here too, but only to MATCH**: the two readers' lists are
// joined by position and confirmed by name, and a description whose name disagrees with the engine's at
// the same position is dropped rather than attached to the wrong file -- `PdfiumDocument::searchUnits`.
//
// ===========================================================================================
// IT COSTS NO SECOND PARSE, FOR `TransitionReader`'s REASON.
// ===========================================================================================
// `pdf/DocumentBackendImpl::open` already runs the first-party walk on every document; this reads the
// catalog's `/Names /EmbeddedFiles` name tree out of the table that walk built. A document with no
// embedded file -- almost all of them -- costs one dictionary lookup. **An encrypted document is not read
// first-party at all** (D2-6: `pdf/cos/` declines it by name until Stage 10), so its attachments are
// searched by name and by text, and their descriptions are not -- a bound the caller logs.
//
// A tolerant read, like `TransitionReader`'s: a malformed tree yields the entries it could read. The walk
// is bounded in depth and in nodes, because `/Kids` are references and a hostile file can point them in a
// circle, and a reader that follows one does not fail -- it hangs.

#ifndef RAWPAGE_PDF_OPS_EMBEDDEDFILEREADER_H
#define RAWPAGE_PDF_OPS_EMBEDDEDFILEREADER_H

#include "pdf/cos/XrefTable.h"

#include <QString>

#include <string>
#include <vector>

namespace rawpage::pdf::ops {

struct EmbeddedFileEntry {
    QString name;          // the file specification's /UF, else /F -- the engine's order of preference
    QString description;   // /Desc, empty when there is none
};

class EmbeddedFileReader {
public:
    // The entries of the `/EmbeddedFiles` name tree, in TREE ORDER -- the order the engine enumerates them
    // in. Never fails: a document with no embedded file yields an empty list.
    static std::vector<EmbeddedFileEntry> read(cos::XrefTable& table);

    // A PDF TEXT STRING (ISO 32000-1 7.9.2.2) as Unicode: UTF-16BE behind its byte-order mark, UTF-8 behind
    // its own (PDF 2.0), and otherwise PDFDocEncoding -- which is NOT Latin-1 in 0x18-0x1F and 0x80-0xA0.
    static QString decodeTextString(const std::string& bytes);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_EMBEDDEDFILEREADER_H
