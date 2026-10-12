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

#include "core/Result.h"
#include "core/doc/Attachments.h"
#include "pdf/cos/CosObject.h"
#include "pdf/cos/XrefTable.h"

#include <QByteArray>
#include <QString>

#include <string>
#include <vector>

namespace rawpage::pdf::ops {

struct EmbeddedFileEntry {
    QString name;          // the file specification's /UF, else /F -- the engine's order of preference
    QString description;   // /Desc, empty when there is none
};

// ONE ENTRY OF THE NAME TREE AS WRITTEN (Stage 6 T11): its key string and its value -- a reference to the file
// specification, or one written inline -- exactly as the file holds them, so a tree the sink rewrites lists every
// file it did not change in the very bytes it was listed in.
struct NameTreeEntry {
    cos::CosObject key;     // a String, in every tree a reader agrees with
    cos::CosObject value;   // a Reference, or a direct Dictionary
};

class EmbeddedFileReader {
public:
    // The bounds of the walk (see the header): a tree past them yields what was read before them.
    static constexpr std::size_t kMaxEntries = 100000;

    // The entries of the `/EmbeddedFiles` name tree, in TREE ORDER -- the order the engine enumerates them
    // in. Never fails: a document with no embedded file yields an empty list.
    static std::vector<EmbeddedFileEntry> read(cos::XrefTable& table);

    // ---- Stage 6 T11 (D6-17) -------------------------------------------------------------------------
    // The same entries as WRITTEN, in tree order, every pair -- a key is a String in any file a reader agrees
    // with, and one that is not is kept, so a rewritten tree drops nothing. Spans cleared, so two equal entries
    // compare equal.
    static std::vector<NameTreeEntry> entries(cos::XrefTable& table);

    // The document-level files as the model holds them, in tree order: each key decoded, each specification
    // read by `fileOf`. One whose value is not a dictionary is skipped.
    static std::vector<core::DocumentAttachment> attachments(cos::XrefTable& table);

    // What a file specification says about its file (`core::EmbeddedFile`, never its bytes): `/UF` else `/F`,
    // `/Desc`, and from `/EF /F`'s stream (else `/EF /UF`'s) its `/Subtype` and `/Params` -- `/Size`,
    // `/CreationDate`, `/ModDate`, `/CheckSum`. A specification that is a plain STRING names a file outside the
    // document, which embeds nothing: its name, and a size of -1. Null when `specification` is neither.
    static std::optional<core::EmbeddedFile> fileOf(cos::XrefTable& table, const cos::CosObject* specification);

    // The bytes `specification`'s file holds, decoded through its filters. `NotFound` by
    // `error.attachment.notEmbedded` for a specification with no `/EF` stream; `Corrupted` by
    // `error.attachment.undecodable` for a stream that will not decode, or whose length disagrees with its
    // `/Params /Size` -- a reader that handed on a truncated attachment would extract a damaged file in silence.
    static core::Result<QByteArray> dataOf(cos::XrefTable& table, const cos::CosObject* specification);

    // A PDF TEXT STRING (ISO 32000-1 7.9.2.2) as Unicode: UTF-16BE behind its byte-order mark, UTF-8 behind
    // its own (PDF 2.0), and otherwise PDFDocEncoding -- which is NOT Latin-1 in 0x18-0x1F and 0x80-0xA0.
    static QString decodeTextString(const std::string& bytes);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_EMBEDDEDFILEREADER_H
