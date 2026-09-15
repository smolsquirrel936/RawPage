// core/doc/Document -- the open document as the domain sees it. Stage 3 T4.
//
// WHAT IT HOLDS AT STAGE 3, and D3-8 is why the list is short: the page tree, the repair report, the
// pre-flight permissions, the identity of the file, the handle it came from, and the current
// revision. `Architecture-FileMap.md`'s row names an aggregate of eleven parts -- annotations, forms,
// outline, layers, tags, attachments, metadata -- and each arrives with the stage whose commands
// read it (`Outline` and `Metadata` at Stage 4). A model part with no caller is tested only against
// its own idea of what a caller wants.
//
// IT OWNS NO FILE AND KNOWS NO PDF SYNTAX. `hydrate()` copies what the backend already read; from
// then on the MODEL is what layout, the status bar and every other reader of page geometry consult,
// and the backend's own tree stays the engine's reading of the bytes it loaded (D3-15). After a
// rotation the two differ, on purpose, until a save.
//
// WHO MAY CHANGE IT (D3-10). Only `core/cmd/CommandJournal` (T5) holds a mutable `Document`;
// `ui/shell/DocumentWindow` receives `const Document&`. The non-const accessors below exist for the
// journal and for a command's `apply()`/`revert()`, which the journal hands a mutable reference --
// and that is a property of the types at T10, not of review.

#ifndef RAWPAGE_CORE_DOC_DOCUMENT_H
#define RAWPAGE_CORE_DOC_DOCUMENT_H

#include "core/Result.h"
#include "core/backend.h"
#include "core/doc/DocumentId.h"
#include "core/doc/PageTree.h"
#include "core/doc/Permissions.h"
#include "core/doc/RepairReport.h"
#include "core/doc/Revision.h"

namespace rawpage::core {

class Document {
public:
    Document() = default;

    // Copies the page tree, the repair report, the permissions and the identity of `handle` from
    // `backend`. Fails, leaving this document unchanged, for a handle the backend does not know. A
    // backend that supplies no permissions or no identity leaves them unassessed and invalid -- which
    // refuses editing rather than allowing it.
    Status hydrate(const IDocumentBackend& backend, DocumentHandle handle);

    // After a save (Stage 3 T8): re-reads the identity and the repair report the backend now holds for
    // the file on disk, and NOT the page tree -- the model is already what was saved, and the engine's
    // tree still describes the bytes it loaded (D3-15). Fails, changing nothing, for an unknown handle.
    Status refreshFileState(const IDocumentBackend& backend);

    DocumentHandle handle() const noexcept { return handle_; }
    const DocumentId& id() const noexcept { return id_; }

    const PageTree& pages() const noexcept { return pages_; }
    PageTree& pages() noexcept { return pages_; }

    const RepairReport& repairReport() const noexcept { return repair_; }
    const Permissions& permissions() const noexcept { return permissions_; }

    // Starts at `Revision::initial()` on hydration and only ever goes up (`Revision::next()`), so an
    // undo is a new revision -- which is why dirty is not "revision differs from the saved one"
    // (D3-10(c)).
    Revision revision() const noexcept { return revision_; }
    void advanceRevision() noexcept { revision_ = revision_.next(); }

    // A SHA-256 over the MODEL -- every page's index, boxes, rotation and label, in order -- and
    // nothing else: not the revision, which only goes up, and not the handle or identity, which no
    // command changes. Stage 3 T5. Two properties are measured with it: apply-then-revert restores it
    // exactly (guide case A-43), and a refused or failed command leaves it unchanged (A-40). Stage 6
    // and later extend it as the model grows; a model part a command can change and this hash cannot
    // see is a reversibility test that passes for the wrong reason.
    QByteArray modelHash() const;

private:
    DocumentHandle handle_;
    DocumentId id_;
    PageTree pages_;
    RepairReport repair_;
    Permissions permissions_;
    Revision revision_ = Revision::invalid();
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_DOCUMENT_H
