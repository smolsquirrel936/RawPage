// pdf/ops/DocumentAssembler -- a NEW document made from pages of another, written as one revision and proved before it
// counts. Stage 7 T7, decision D7-5 as amended by D7-25 (`Stage7-Assembly-Plan.md` §3), and D7-30.
//
// -------------------------------------------------------------------------------------------
// BUILT HERE, WRITTEN BY `FullWriter` (D7-25)
// -------------------------------------------------------------------------------------------
// The pages come out of their source through `ObjectGraphCopier` (D7-4): each with everything it reaches, each object
// once, inheritance made each page's own, the page tree left behind -- a payload that is already a small PDF, its
// catalog object 1, its one tree node object 2, its pages 3, 4, ... in order and every other object after them,
// numbered from 1. What the assembler adds is what a DOCUMENT needs that a payload does not:
//   * **A balanced page tree.** A payload's node lists every page, which no reader minds at twelve and every reader
//     pays for at nine hundred: a page lookup walks `/Kids`. Past `kMaxKids` pages, the pages are dealt into leaf
//     nodes of at most `kMaxKids` -- as evenly as the count divides, so every leaf is at the same depth -- and those
//     into nodes above them until one root holds at most `kMaxKids`. The root keeps number 2; each new node takes the
//     next number after the payload's last, so the objects stay numbered from 1 with no gap.
//   * **A fresh `/ID`** -- ISO 32000-1 14.4's "permanent identifier", which a new file must not share with the file
//     its pages came from: the first element a hash of the payload and a seed the caller makes unique, the second the
//     one `FullWriter` computes over what it wrote.
// The document so built is handed to `pdf/cos/FullWriter`, which WRITES it -- one classic cross-reference section, no
// `/Prev`, no object streams, nothing unreachable -- so a new file and a rewritten one are spelled by one writer.
//
// -------------------------------------------------------------------------------------------
// THE VERIFY (guide A-47)
// -------------------------------------------------------------------------------------------
// `verify` reads a written file back as any reader would and refuses it, by `error.assembly.verifyFailed` with what
// failed as context, unless: it opens through its own table with no repair; it has ONE section and no `/Prev`; its
// trailer has a two-string `/ID`; what it holds is exactly what its trailer reaches; it has the payload's page count,
// in the payload's order, and each page is the payload's page value for value (`/Parent` aside); and every other
// object the payload reached is in it at the same number, value for value -- a stream by its dictionary (`/Length`
// aside) and its encoded bytes. A file is not handed on until it passes: `Source::assemble` verifies what it built in
// memory, and `Source::write` verifies each file again FROM ITS PATH, because the claim is about the file on disk.
//
// -------------------------------------------------------------------------------------------
// THE SOURCE
// -------------------------------------------------------------------------------------------
// `Source` is the open document as it stood when it was taken -- `core::AssemblySource`, what the backend hands the
// person's extract or split: the WHOLE file (the newest revision on disk followed by the unsaved update, composed as
// a save would compose it), opened and walked once, from which any number of documents are made. It refuses what the
// copier refuses: an encrypted file (`error.pdf.encrypted`), a page tree the walk noted anything about
// (`error.pdf.pageTreeDamaged`). `write` is the file half: see `core::AssemblySource::write`.
//
// THE TEST SEAM. `setWriteFaultForTesting` corrupts a file's bytes after it is built and before it is written, so a
// case can watch a verify refuse a file on disk and the call take back every file it wrote (guide A-48's control).
// Process-wide, empty in the product, and every case that sets it clears it.
//
// PORTABLE, in `rawpage_ops`: no engine -- built by `portable-asan`, whose `qpdf --check` reads what the suite writes.

#ifndef RAWPAGE_PDF_OPS_DOCUMENTASSEMBLER_H
#define RAWPAGE_PDF_OPS_DOCUMENTASSEMBLER_H

#include "core/Result.h"
#include "core/doc/Assembly.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/PageOps.h"

#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawpage::pdf::ops {

class DocumentAssembler {
public:
    // Kids per page-tree node. Thirty-two keeps a 900-page document two levels deep and a million-page one four.
    static constexpr int kMaxKids = 32;

    struct Assembled {
        std::string bytes;
        std::string payload;              // the copier's, which `verify` checks a file against
        int pageCount = 0;
        long long objects = 0;            // objects the copy holds, catalog and payload node excluded
        core::PageCommandReport report;   // the copier's: what it met, by SOURCE page
    };

    // A payload `ObjectGraphCopier` made, as a new document: the tree balanced, the `/ID` fresh, written by
    // `FullWriter`. `idSeed` is folded into the first `/ID` element; the caller makes it unique. Not verified --
    // `verify` is the separate step, so a case can verify what it planted.
    static core::Result<std::string> build(std::string_view payload, std::string_view idSeed);

    // The checks above, of `written` against the `payload` it was built from.
    static core::Status verify(std::string_view written, std::string_view payload);

    class Source final : public core::AssemblySource {
    public:
        // `bytes`, a whole file, opened and walked -- refused as the header says.
        static core::Result<std::unique_ptr<Source>> open(std::string bytes);

        int pageCount() const override { return static_cast<int>(walk_.pages.size()); }
        core::Result<core::AssembledDocument> assemble(const std::vector<int>& pages) override;
        core::Result<std::vector<core::WrittenDocument>> write(const std::vector<core::AssemblyOutput>& outputs,
                                                               const QString& folder) override;

        // `assemble`'s result in this layer's types: the bytes as the file is written.
        core::Result<Assembled> make(const std::vector<int>& pages);

        Source(const Source&) = delete;              // the table views `bytes_`: never copied or moved
        Source& operator=(const Source&) = delete;

    private:
        Source() = default;
        std::string bytes_;
        std::optional<cos::OpenedFile> opened_;
        PageWalk walk_;
        unsigned long long made_ = 0;   // documents made from this source -- part of each `/ID` seed
    };

    using WriteFault = std::function<void(std::string& bytes)>;
    static void setWriteFaultForTesting(WriteFault fault);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_DOCUMENTASSEMBLER_H
