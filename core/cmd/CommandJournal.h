// core/cmd/CommandJournal -- runs commands, and is the only holder of a mutable `Document`. Stage 3 T5,
// decision D3-10 (`Stage3-Mutation-Plan.md` §3, §5.4).
//
// -------------------------------------------------------------------------------------------
// THE THREE NUMBERS
// -------------------------------------------------------------------------------------------
//   * **Revision** -- `Document::revision()`, advanced by every successful run, undo and redo, and
//     ONLY EVER UPWARD. Cache keys and staleness use it. Dirty never does.
//   * **Position** -- how many history steps are applied. A run pushes a step (and throws away redo),
//     an undo steps back, a redo steps forward.
//   * **Saved position** -- where the history was at the last save. `isDirty()` is *position differs
//     from saved position*. A run that throws away redo past the saved position makes it
//     UNREACHABLE: after rotate, save, undo, rotate another page, no sequence of undos returns to the
//     saved file, and dirty must stay true (guide case A-39).
//
// Why dirty is not "revision differs from the saved revision" (D3-10(c)): rotate, save, rotate, undo
// leaves a document identical to the file on disk at a HIGHER revision than the save. A revision-based
// dirty flag asks the user to save an unchanged file, and a user asked that learns to ignore the
// question.
//
// -------------------------------------------------------------------------------------------
// A RUN THAT CANNOT HAPPEN LEAVES NO TRACE (guide case A-40)
// -------------------------------------------------------------------------------------------
// `run()` asks `Permissions::may(command.operation())` FIRST, before the command sees the document --
// so an encrypted document's refusal changes nothing, not even the revision. A command whose `apply()`
// returns false is not pushed, not counted and not announced. And because "false means nothing
// changed" is a promise a command can break, the journal compares `Document::modelHash()` before and
// after a refused apply; a difference is a PARTIAL APPLICATION, reported to the partial-application
// handler -- which asserts in a debug build and logs an error in a release one, and which a test
// replaces to observe it without an abort.
//
// -------------------------------------------------------------------------------------------
// GROUPS ARE ONE UNDO STEP
// -------------------------------------------------------------------------------------------
// `beginGroup` … `endGroup` gathers every command run in between into ONE history step with the
// group's name: "rotate all pages" is one user action and one `Ctrl+Z`. The step is created by the
// group's FIRST successful run, so dirty is correct while the group is still open, and an empty group
// records nothing. A nested `beginGroup` folds into the outer group. Undo and redo are refused while a
// group is open. A command that fails inside a group leaves the commands before it applied and in the
// step; the group's caller decides whether to close it or undo it.
//
// -------------------------------------------------------------------------------------------
// SUBSCRIPTIONS, NOT SIGNALS (D3-10(b))
// -------------------------------------------------------------------------------------------
// `core/` has no Signal type; `core/settings/Settings` established the token pattern and this is its
// second user. `mutated(Scope)` fires once per successful run, undo or redo -- an undo of a group once,
// with the group's merged scope -- and `dirtyChanged(bool)` only when the answer changes. A handler
// may unsubscribe itself while being called.
//
// WHAT IT DOES NOT DO YET: write the recovery journal (T9) or save (T8). `markSaved()` is what the save
// path calls once the file is on disk.

#ifndef RAWPAGE_CORE_CMD_COMMANDJOURNAL_H
#define RAWPAGE_CORE_CMD_COMMANDJOURNAL_H

#include "core/Result.h"
#include "core/cmd/Command.h"
#include "core/doc/Document.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace rawpage::core {

class MutationSink;

class CommandJournal {
public:
    // Takes the document (hydrated) and the sink its edits go to -- null for a document the backend
    // refused to edit, in which case every run is refused by the document's permissions anyway.
    CommandJournal(Document document, MutationSink* sink);

    CommandJournal(const CommandJournal&) = delete;
    CommandJournal& operator=(const CommandJournal&) = delete;

    // THE ONLY WAY OUT: a const reference. `ui/` holds this, and cannot mutate the model (D3-10).
    const Document& document() const noexcept { return document_; }

    Status run(std::unique_ptr<Command> command);
    bool undo();
    bool redo();

    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    std::optional<CatalogText> undoName() const;
    std::optional<CatalogText> redoName() const;

    void beginGroup(CatalogText name);
    bool endGroup();                          // false when no group is open
    bool isGroupOpen() const noexcept { return groupDepth_ > 0; }

    bool isDirty() const noexcept;
    Revision revision() const noexcept { return document_.revision(); }
    void markSaved();

    // What `core/doc/SaveCoordinator` calls once the file is on disk and the backend has adopted it
    // (Stage 3 T8): the document takes the new file's identity and repair report, then `markSaved()`.
    // The model and the revision are unchanged -- a save is not a mutation.
    Status adoptSavedFile(const IDocumentBackend& backend);

    std::size_t position() const noexcept { return position_; }
    std::size_t historySize() const noexcept { return history_.size(); }

    using MutatedHandler = std::function<void(const Scope&)>;
    using DirtyHandler = std::function<void(bool)>;
    int subscribeMutated(MutatedHandler handler);
    int subscribeDirty(DirtyHandler handler);
    void unsubscribe(int token);

    // Replaces the process-wide partial-application handler; an empty function restores the default
    // (assert in debug, log in release). For tests: observe the report without aborting.
    using PartialApplicationHandler = std::function<void(const Command&)>;
    static void setPartialApplicationHandler(PartialApplicationHandler handler);

private:
    struct Step {
        CatalogText name;
        std::vector<std::unique_ptr<Command>> commands;
    };

    bool revertStep(Step& step);
    bool applyStep(Step& step);
    Scope scopeOf(const Step& step) const;
    void afterOperation(const Scope& scope, bool wasDirty);

    Document document_;
    MutationSink* sink_ = nullptr;

    std::vector<Step> history_;
    std::size_t position_ = 0;
    std::optional<std::size_t> savedPosition_ = 0;    // nullopt: unreachable

    int groupDepth_ = 0;
    bool groupStepOpen_ = false;
    CatalogText groupName_;

    int nextToken_ = 1;
    std::map<int, MutatedHandler> mutatedHandlers_;
    std::map<int, DirtyHandler> dirtyHandlers_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDJOURNAL_H
