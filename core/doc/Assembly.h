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
// Pure: QtCore and the standard library -- `rawpage_tests`, under `portable-asan` too.

#ifndef RAWPAGE_CORE_DOC_ASSEMBLY_H
#define RAWPAGE_CORE_DOC_ASSEMBLY_H

#include "core/Result.h"
#include "core/doc/PageMap.h"

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
