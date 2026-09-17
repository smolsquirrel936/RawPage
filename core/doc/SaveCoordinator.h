// core/doc/SaveCoordinator -- decides whether, where and in what order a document is saved. Stage 3 T8,
// decisions D3-4, D3-6 and D3-16 (`Stage3-Mutation-Plan.md` §3; the transaction is §5.3).
//
// IN `core/`, AND WRITING NO BYTE ITSELF. Whether to save, whether the file on disk is still the one
// that was opened, what to do when it is not, and which order the steps go in are domain policy --
// and the headless batch path (flow K) must reach them without a window. The bytes are the backend's
// (`IDocumentBackend::writeRevision`, `adoptRevision`) and the flush, the replace and the removal of a
// temporary file are `platform::IDurableFiles`'.
//
// -------------------------------------------------------------------------------------------
// `save()`, IN ORDER
// -------------------------------------------------------------------------------------------
//   0. No `IDurableFiles` → refused (`error.save.noDurableFiles`). There is no fallback to writing the
//      target directly: that fallback is the torn file D3-4 exists to rule out.
//   1. A clean journal → refused (`error.save.nothingToSave`), nothing written.
//   2. A repaired document (`RepairReport::requiresSaveAs()`) → **Save-As required**
//      (`error.save.repairedDocument`, D3-6). Not an error: the caller asks for a new path.
//   3. The file at `path` is no longer the one that was opened -- path, size, modification time and a
//      hash of its content, D3-16 -- or is gone → **Save-As required** (`error.save.fileChanged`).
//   4. Stray temporary files beside the target whose writer process has exited are removed (A-60).
//   5. `writeRevision` to `<dir>/.<name>.rawpage-<pid>.tmp`: the opened bytes verbatim, the update,
//      and the re-read that must pass before anything else happens.
//   6. `syncToDisk` the temporary file, then `replaceFile(target, temporary)`.
//   7. `adoptRevision(target)`, then `CommandJournal::adoptSavedFile`, which tells the recovery journal
//      the document now lives at the target (T9) and marks the journal saved.
//
// **A failure at 5 or 6 leaves the target untouched, removes the temporary file, and leaves the
// journal dirty** -- and asks for **Save-As** when another place might work, which is the second half
// of D3-16: *"a target that is read-only, locked by another process or in a directory that cannot hold
// the temporary file fails with a named error **and offers Save-As**"*. The named error becomes the
// outcome's `reasonKey` and `reasonContext`, so the caller says why before it asks where.
//
// **The line is WHERE the failure came from, not how bad it was.** A refusal by the file system is
// about this PLACE -- `IoFailure` (the temporary could not be written here), `PermissionDenied`
// (read-only, or denied), `Unavailable` (another process holds it), `NotFound` (the directory is
// gone) -- and another path is a real answer to it. `Corrupted` is not: that is `RevisionWriter`
// refusing the bytes we composed (`error.save.verifyFailed`), which every path would refuse
// identically, so it stays an error and no dialog opens. T8 built the named error and, until Stage 3
// guide I-15, not the offer. A failure at 7 happens after the replace: the new file IS on disk, the journal
// stays dirty, and the backend's identity still names the previous file -- so the next Save sees a
// changed file and asks for Save-As rather than appending to bytes it did not compute against. That is
// the conservative direction, and the error says the adopt failed.
//
// `saveAs(target)` is the same sequence without steps 1-3 -- a Save-As of a clean or repaired document
// is legitimate, and D3-6's complete section is what makes a repaired one worth writing -- except that
// a target naming the document's own file goes through `save()`, so a repaired document cannot be
// "saved as" itself and escape step 2.
//
// NOT HERE: asking the user anything (T10). The recovery journal hears about the save through
// `CommandJournal::adoptSavedFile` and its recorder (T9).

#ifndef RAWPAGE_CORE_DOC_SAVECOORDINATOR_H
#define RAWPAGE_CORE_DOC_SAVECOORDINATOR_H

#include "core/Result.h"
#include "core/backend.h"
#include "platform/platform.h"

#include <QLatin1String>
#include <QString>

#include <functional>

namespace rawpage::core {

class CommandJournal;

struct SaveOutcome {
    enum class Kind { Saved, SaveAsRequired };
    Kind kind = Kind::Saved;
    QLatin1String reasonKey{""};   // SaveAsRequired: why, as a catalog key
    QString reasonContext;         // SaveAsRequired: that key's argument, when its text takes one
    QString path;                  // Saved: the file now on disk
};

class SaveCoordinator {
public:
    // `durableFiles` may be null -- a platform that does not implement it -- and every save is then
    // refused by key rather than attempted another way.
    SaveCoordinator(IDocumentBackend& backend, platform::IDurableFiles* durableFiles);

    // Saves `journal`'s document to `path`, the file it was opened from (or last saved to).
    Result<SaveOutcome> save(CommandJournal& journal, const QString& path);

    // Saves it to `target`, which becomes the document's file.
    Result<SaveOutcome> saveAs(CommandJournal& journal, const QString& target);

    // `<dir>/.<name>.rawpage-<pid>.tmp`: beside the target, so the replace never crosses a volume;
    // hidden by its leading dot in a directory listing; and named by its writer, so a stray one can be
    // told from a live one.
    static QString temporaryPathFor(const QString& target, qint64 processId);

    // The kill harness's view of where a save is (guide case A-69, Stage 3 T9). Process-wide and empty in
    // the product; an observer may sleep, to hold a phase open long enough for a kill to land in it.
    enum class Phase { Writing, Written, Replacing, Replaced };
    static void setPhaseObserverForTesting(std::function<void(Phase, const QString& temporary)> observer);

    // The process id the temporary file is named by. The product's own, unless a test says otherwise.
    void setProcessIdForTesting(qint64 processId) { processId_ = processId; }

private:
    Result<SaveOutcome> writeThrough(CommandJournal& journal, const QString& target);
    void removeStrayTemporaries(const QString& target);

    IDocumentBackend& backend_;
    platform::IDurableFiles* durableFiles_;
    qint64 processId_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_SAVECOORDINATOR_H
