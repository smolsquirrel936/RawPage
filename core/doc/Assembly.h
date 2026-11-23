// core/doc/Assembly -- new documents made from an open one's pages: what is asked for, and what comes back. Stage 7
// T7 (`Stage7-Assembly-Plan.md` D7-5, D7-30).
//
// -------------------------------------------------------------------------------------------
// A NEW DOCUMENT IS NOT A COMMAND
// -------------------------------------------------------------------------------------------
// Extract and split change no open document, so they are not commands (D7-5): nothing is journalled, nothing is
// undone. What they need from the backend is a SOURCE -- the open document AS IT STANDS when the person asked,
// unsaved edits included, because what the screen shows is what an extract takes (D6-2's rule, carried) -- from
// which any number of new documents can be made. `IDocumentBackend::assemblySource` answers one: a SNAPSHOT, so an
// edit made while a split is planned changes nothing the split writes, and the document is composed once for the
// whole operation rather than once per output.
//
// `assemble` makes one new document in memory -- one revision, verified (`pdf/ops/DocumentAssembler`) -- for the
// untitled tab extract-to-one opens and for split-by-size to weigh. `write` makes several and writes them into a
// folder through `pdf/`, which owns the file format and may write (no layer-lint waiver): each name a bare file name,
// checked against the folder BEFORE anything is written, so a collision writes nothing at all; each file read back
// from its path and verified; and on any failure every file this call wrote removed again.
//
// IMPOSITION (Stage 7 T16, D7-8) is a third: the snapshot's pages drawn onto sheets, n-up or as a booklet, for the
// untitled tab the *Booklet* and *N-up* dialogs open (`impose`).
//
// Pure: QtCore and the standard library -- `rawpage_tests`, under `portable-asan` too.

#ifndef RAWPAGE_CORE_DOC_ASSEMBLY_H
#define RAWPAGE_CORE_DOC_ASSEMBLY_H

#include "core/Result.h"
#include "core/doc/CopiedPages.h"
#include "core/doc/Imposition.h"
#include "core/doc/PageMap.h"
#include "core/doc/TypedText.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace rawpage::core {

// One new document, in memory: the whole file, and what the copy of its pages met (a link whose page stayed behind,
// a widget, a tag tree -- `ObjectGraphCopier`'s report, each reference's `page` an index of the SOURCE).
struct AssembledDocument {
    QByteArray bytes;
    int pageCount = 0;
    PageCommandReport report;
};

// Stage 7 T16 (D7-8, D7-46): an IMPOSED document, in memory -- the sheets' sides as its pages, one revision, verified.
struct ImposedDocument {
    QByteArray bytes;
    int pageCount = 0;              // sides
    int sheets = 0;                 // physical sheets
    int sourcePages = 0;
    int blanks = 0;                 // a booklet's padding
    int annotationsNotDrawn = 0;    // a form draws a page's content; its comments, links and fields stay behind
    ImpositionPlan plan;            // what was drawn where
};

// How an imposition's sheet labels are worded and in which Han forms a CJK label is drawn -- the caller's.
struct ImpositionText {
    ImpositionLabels labels;
    TypedFont han = TypedFont::Simplified;
};

// One file a `write` makes: the source's pages, a strictly ascending set, under a bare file name in the folder.
struct AssemblyOutput {
    std::vector<int> pages;
    QString fileName;
};

// One file a `write` made.
struct WrittenDocument {
    QString path;          // absolute
    int pageCount = 0;
    qint64 bytes = 0;
    PageCommandReport report;
};

class AssemblySource {
public:
    virtual ~AssemblySource() = default;

    // Pages the snapshot holds -- the document's count when it was taken.
    virtual int pageCount() const = 0;

    // `pages` (strictly ascending, each inside the snapshot) as one new document. Refused by the copier's keys
    // (`error.pdf.pageOutOfRange`, `error.copy.tooLarge`) or the verify's (`error.assembly.verifyFailed`).
    virtual Result<AssembledDocument> assemble(const std::vector<int>& pages) = 0;

    // `pages` as the PAYLOAD an `InsertPagesCommand` carries -- `ObjectGraphCopier`'s copy, no document built round it.
    // Stage 7 T8 (D7-13): what `page.copy` puts on the clipboard and what a thumbnail dragged into another window's pane
    // inserts there -- the pages as the screen shows them, unsaved edits included, which `IDocumentBackend::copyPages`
    // (a FILE's pages) cannot give. Refused by the copier's keys.
    virtual Result<CopiedPages> copy(const std::vector<int>& pages) = 0;

    // STAGE 7 T16 (D7-8, D7-46): EVERY page of the snapshot imposed by `spec` -- n-up or a booklet, `core::planImposition`'s
    // plan drawn by `pdf/ops/ImpositionOps` -- as one new document, verified as `assemble`'s is. Refused by the plan's keys
    // (`error.imposition.*`), by `error.imposition.pageUnsupported` for a page whose content cannot be drawn as a form,
    // and by the copier's and the verify's.
    virtual Result<ImposedDocument> impose(const ImpositionSpec& spec, const ImpositionText& text) = 0;

    // Every output assembled and written into `folder`, which is created when absent, in order. REFUSED, with
    // nothing written: a name that is not a bare file name (`error.assembly.badName`), two outputs of one name
    // (`error.assembly.duplicateName`), a name the folder already holds (`error.assembly.exists`, its path), and
    // an output the assembler refuses. A file is never replaced: each is written beside its target and moved onto
    // a name nothing holds. Each written file is read back from its path and verified; on any failure the files
    // this call wrote are removed and the failure is answered by key.
    virtual Result<std::vector<WrittenDocument>> write(const std::vector<AssemblyOutput>& outputs, const QString& folder) = 0;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_ASSEMBLY_H
