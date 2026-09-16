// pdf/ops/MutationSinkImpl -- where a command's INTENT becomes an update to write. Stage 3 T3.
//
// The engine side of flow D (`Architecture-FileMap.md` §5): `RotatePageCommand::apply()` says
// "page 3 displays at 90 degrees" through `core::MutationSink`, and never touches bytes. This class
// decides which COS objects that means, and hands `pdf/cos/IncrementalWriter` a delta at save.
//
// -------------------------------------------------------------------------------------------
// DESIRED STATE, NOT A LOG (D3-5)
// -------------------------------------------------------------------------------------------
// It keeps, per page, the rotation the document should HAVE -- not the list of calls that got it
// there. `delta()` compares that desired state with the NEWEST REVISION ON DISK and emits only the
// pages that differ. Two properties follow, and guide cases A-31 and A-32 are each one of them:
//
//   * **An undo before a save writes nothing.** Rotate page 1 and rotate it back, or rotate it four
//     times: the desired rotation equals the one on disk, so the delta is empty -- not an update that
//     restates the original.
//   * **An undo after a save writes the page back.** `adopt()` moves the comparison onto the revision
//     just written; reverting then differs from THAT, so the delta holds the page's pre-rotation
//     dictionary. A sink that compared with the file as first opened would lose this undo on the next
//     save, silently.
//
// Which object and which dictionary is `pdf/ops/PageOps`' decision, including the rule that a
// rotation is never written onto an ancestor `/Pages` node.
//
// -------------------------------------------------------------------------------------------
// OWNERSHIP
// -------------------------------------------------------------------------------------------
// It owns the `XrefTable` of the revision it compares against and does NOT own the bytes that table
// points into -- the backend holds the opened bytes for the document's lifetime (D3-4), and a copy
// here would be a second 200 MB buffer for nothing. `adopt()` takes the next revision's bytes on the
// same terms. Not thread-safe: the journal calls it from one thread, and the save path is T8's.
//
// WHAT IT DOES NOT DO. It refuses nothing on policy -- whether this document may be edited at all is
// `core/doc/Permissions` (D3-7, T4) -- and it writes nothing: the update's bytes, the temporary file
// and the replace are `IncrementalWriter`, `SaveCoordinator` and `platform::IDurableFiles`.

#ifndef RAWPAGE_PDF_OPS_MUTATIONSINKIMPL_H
#define RAWPAGE_PDF_OPS_MUTATIONSINKIMPL_H

#include "core/Result.h"
#include "core/backend.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/PageOps.h"

#include <map>
#include <memory>
#include <string_view>

namespace rawpage::pdf::ops {

class MutationSinkImpl final : public core::MutationSink {
public:
    // Reads the table and walks the page tree of `bytes`, which must outlive the sink (or the next
    // `adopt()`). Fails for a file with no usable cross-reference table or no page tree -- an
    // encrypted file is declined one layer down, by name (D2-6).
    static core::Result<std::unique_ptr<MutationSinkImpl>> open(std::string_view bytes);

    // core::MutationSink. `degrees` is normalized: -90 is 270 and 450 is 90. A value that is not a
    // multiple of 90, or a page index outside the document, is refused with nothing changed.
    core::Status setPageRotation(int pageIndex, int degrees) override;

    // The update the desired state needs, against the newest revision on disk. Empty when nothing
    // differs. The delta is built over `table()`, which is what `IncrementalWriter` must be handed
    // with it.
    core::Result<cos::ObjectDelta> delta();

    // After a save: compare against `bytes` from now on. The page count must be unchanged -- nothing
    // at Stage 3 adds or removes a page -- and desired rotations now equal to the disk are dropped.
    core::Status adopt(std::string_view bytes);

    int pageCount() const noexcept { return static_cast<int>(walk_.pages.size()); }
    const PageWalk& pages() const noexcept { return walk_; }
    std::string_view bytes() const noexcept { return bytes_; }
    const cos::XrefTable& table() const noexcept { return table_; }
    // For `RevisionWriter::compose`, which hands it to `IncrementalWriter::protectOriginal` -- whose
    // walk fetches, and fetching caches.
    cos::XrefTable& table() noexcept { return table_; }

    // The rotation page `pageIndex` will display at once the desired state is written.
    int desiredRotation(int pageIndex) const;

private:
    MutationSinkImpl(std::string_view bytes, cos::XrefTable table, PageWalk walk);

    std::string_view bytes_;
    cos::XrefTable table_;
    PageWalk walk_;
    std::map<int, int> desired_;   // pageIndex -> rotation, only where it differs from the disk
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_MUTATIONSINKIMPL_H
