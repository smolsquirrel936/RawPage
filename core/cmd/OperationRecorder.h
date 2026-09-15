// core/cmd/OperationRecorder -- what `CommandJournal` tells whoever is keeping a durable copy of its
// operations. Stage 3 T9, decision D3-11 (`Stage3-Mutation-Plan.md` §3).
//
// ONE IMPLEMENTATION TODAY, `core/cmd/RecoveryJournal`, and an interface anyway: the journal is the
// only holder of a mutable `Document` (D3-10) and must not depend on a class that writes files, and a
// test observing exactly which operations were reported needs no directory to do it.
//
// THE CONTRACT THE JOURNAL KEEPS:
//   * every call is made BEFORE the journal operation that caused it returns -- which is D3-11's
//     "a command is recoverable once `run()` has returned";
//   * a group is reported as it happens: `recordRun` with `opensGroup` set for the FIRST command that
//     lands in a group step, plain `recordRun` for the rest, and `recordGroupEnd` when the outermost
//     group that recorded anything closes. An empty group reports nothing;
//   * an undo or redo reports the WHOLE step it moved -- its name and every command in it, in the order
//     they were run -- so a replay never depends on a history it did not rebuild;
//   * a refused or failed command reports nothing.
//
// A recorder may fail -- a full disk, a directory that vanished -- and must not refuse the edit: the
// journal ignores what a recorder does and never asks it a question (D3-11's degradation to memory).

#ifndef RAWPAGE_CORE_CMD_OPERATIONRECORDER_H
#define RAWPAGE_CORE_CMD_OPERATIONRECORDER_H

#include "core/cmd/Command.h"

#include <QString>

#include <optional>
#include <vector>

namespace rawpage::core {

class Document;

class OperationRecorder {
public:
    virtual ~OperationRecorder() = default;

    // `opensGroup` is the group's name when this command is the first one recorded in it.
    virtual void recordRun(const Command& command, const std::optional<CatalogText>& opensGroup) = 0;
    virtual void recordGroupEnd() = 0;
    virtual void recordUndo(const CatalogText& stepName, const std::vector<const Command*>& step) = 0;
    virtual void recordRedo(const CatalogText& stepName, const std::vector<const Command*>& step) = 0;

    // The document is saved at `path`: from now on the recorded history starts here.
    virtual void recordSaved(const Document& document, const QString& path) = 0;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_OPERATIONRECORDER_H
