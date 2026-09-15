// core/cmd/RecoveryJournal -- a crashed session's edits, on disk, one operation per line. Stage 3 T9,
// decision D3-11 (`Stage3-Mutation-Plan.md` §3; the directory and the line grammar are §5.5).
//
// -------------------------------------------------------------------------------------------
// WHAT IS ON DISK
// -------------------------------------------------------------------------------------------
//   <root>/<DocumentId key>-<pid>-<n>/
//     lock            QLockFile: a live owner's directory is never offered for recovery
//     meta.json       {"format":1,"source":…,"size":…,"modified":…,"sha256":…} -- replaced atomically
//     journal.ndjson  {"format":1} then one operation per line, each flushed to disk before the edit returns
//
// `<n>` makes the same file opened twice in one process two directories; `<pid>` does the same for two
// processes (two instances with `general/singleInstance` off). The lines, per §5.5:
//
//   {"op":"run","cmd":<serialized>}          {"op":"groupBegin","name":<catalog text>}
//   {"op":"undo","name":<catalog text>,"cmds":[<serialized>…]}     {"op":"groupEnd"}
//   {"op":"redo","name":<catalog text>,"cmds":[<serialized>…]}
//
// **Undo and redo carry the WHOLE STEP** -- `"cmds"`, every command of it in run order -- where §5.5
// wrote a single `"cmd"`: a group is one undo step, and a line naming one of its commands could not
// replay it. That is this file's one deviation from the grammar.
//
// -------------------------------------------------------------------------------------------
// DURABILITY (D3-11): a command is recoverable once `run()` has returned
// -------------------------------------------------------------------------------------------
// `CommandJournal` calls this recorder before `run()`, `undo()` and `redo()` return; each call writes one
// whole line, flushes it through Qt, and `platform::IDurableFiles::syncToDisk`s the file. A kill can lose
// at most the operation still inside the call -- and leave a TORN trailing line, which replay ignores and
// reports. A save rewrites `meta.json` to the saved file's identity and truncates the lines: history
// before the last save is the saved file, not the journal.
//
// **THE WINDOW THIS DOES NOT CLOSE**, stated rather than hidden: between `SaveCoordinator`'s replace and
// the `meta.json` rewrite, a kill leaves a journal whose meta names the file BEFORE the save and whose
// lines are already in the file on disk. Discovery then sees a source that does not match and offers
// *recover to a new file*, which replays those lines onto the saved file -- the edits twice, into an
// untitled document the user must save as. The original is never touched.
//
// **DEGRADATION** (Tech-Stack-Plan.md §5.5): a directory that cannot be created, locked or written, a
// null `IDurableFiles`, or a command whose `serialize()` is empty -- and the journal keeps editing in
// memory, writes nothing more, and says so ONCE (`isRecoverable()`, `unrecoverableKey()`, one log line).
//
// -------------------------------------------------------------------------------------------
// REPLAY: the unit is the UNDO STEP
// -------------------------------------------------------------------------------------------
// `replay()` rebuilds a document's state and its undo history since the last save by replaying steps in
// order into a fresh `CommandJournal` with no recorder attached. A malformed line stops replay AT its
// step and keeps every step before it; a group that fails part-way is abandoned whole
// (`CommandJournal::abandonOpenGroup`); a group with no `groupEnd` at the end of the file is dropped and
// reported; a torn trailing line is ignored and reported. An undo whose step is still in the rebuilt
// history is `undo()`; an undo of a step from BEFORE the last save -- which the history no longer holds --
// applies each command's `revert()` as a new step, which leaves the document right and the undo menu
// holding that inverse rather than the original step. A redo that no longer matches is re-run.
//
// FILE I/O LIVES HERE, BEHIND NAMED layer-lint WAIVERS (D3-11, `core/render/DiskTileStore`'s precedent):
// one on the journal's append and one on the whole-file write both `meta.json` and the truncation go
// through. Deletions go through `IDurableFiles::removeFile`. The directory is created with `QDir::mkpath`
// and removed with `QDir::rmdir`, and the lock is a `QLockFile` -- none of which the gate's pattern list
// names, which is a blind spot of the gate and not a licence.

#ifndef RAWPAGE_CORE_CMD_RECOVERYJOURNAL_H
#define RAWPAGE_CORE_CMD_RECOVERYJOURNAL_H

#include "core/Result.h"
#include "core/cmd/OperationRecorder.h"
#include "core/doc/DocumentId.h"
#include "platform/platform.h"

#include <QByteArray>
#include <QFile>
#include <QLatin1String>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QLockFile;

namespace rawpage::core {

class CommandJournal;

struct RecoveryMeta {
    QString sourcePath;
    qint64 size = -1;
    qint64 modifiedMsecs = 0;
    QByteArray contentHash;     // SHA-256, raw
};

struct RecoveryCandidate {
    QString directory;
    RecoveryMeta meta;
    bool sourceMatches = false;     // false: offer "recover to a new file", never a replay onto the source
    int operationLines = 0;         // complete lines after the format line
    QLatin1String problemKey{""};   // non-empty: the directory is damaged and cannot be offered as is
};

struct ReplayReport {
    int stepsApplied = 0;
    std::optional<int> stoppedAtStep;   // 1-based: the step that could not be replayed
    QLatin1String stopKey{""};
    QString stopContext;
    bool tornTrailingLine = false;
    bool droppedUnterminatedGroup = false;
    QByteArray appliedLines;            // the lines of the applied steps, to seed the next journal
};

class RecoveryJournal final : public OperationRecorder {
public:
    // Creates `<root>/<key>-<pid>-<n>/`, takes its lock, and writes `meta.json` and the format line (and
    // `seedLines` after it -- a replayed journal's lines, so a crash after recovery loses nothing). Never
    // fails: a journal that cannot write degrades to memory and says so.
    static std::unique_ptr<RecoveryJournal> open(const QString& root, const QString& sourcePath, const DocumentId& id,
                                                 platform::IDurableFiles* durableFiles, const QByteArray& seedLines = {});
    ~RecoveryJournal() override;   // releases the lock and KEEPS the files: only `discard()` removes them

    RecoveryJournal(const RecoveryJournal&) = delete;
    RecoveryJournal& operator=(const RecoveryJournal&) = delete;

    bool isRecoverable() const noexcept { return unrecoverableKey_.size() == 0; }
    QLatin1String unrecoverableKey() const noexcept { return unrecoverableKey_; }
    QString unrecoverableContext() const { return unrecoverableContext_; }
    QString directory() const { return directory_; }

    // Removes the directory: the user discarded the document (D3-14), or closed it with nothing unsaved.
    void discard();

    // OperationRecorder.
    void recordRun(const Command& command, const std::optional<CatalogText>& opensGroup) override;
    void recordGroupEnd() override;
    void recordUndo(const CatalogText& stepName, const std::vector<const Command*>& step) override;
    void recordRedo(const CatalogText& stepName, const std::vector<const Command*>& step) override;
    void recordSaved(const Document& document, const QString& path) override;

    // Every directory under `root` whose owner is gone. A directory whose lock is held by a live process
    // is not listed.
    static std::vector<RecoveryCandidate> discover(const QString& root);

    // Replays `directory`'s journal into `journal`, which must have no recorder attached. An unreadable
    // journal is an error; damage inside it is a report.
    static Result<ReplayReport> replay(const QString& directory, CommandJournal& journal);

    // Removes a discovered directory (the user discarded a recovery). Refuses one whose lock is live.
    static Status discardDirectory(const QString& directory, platform::IDurableFiles* durableFiles);

    // `%LOCALAPPDATA%\RawPage\recovery` on Windows; `documents/recoveryDir` overrides it at T12.
    static QString defaultRoot();

    // The kill harness's view of where a write is (guide cases A-68, A-69). Process-wide; empty in the
    // product. The observer may sleep, to hold a phase open long enough for a kill to land in it.
    enum class Phase { Writing, Flushing, Flushed };
    static void setPhaseObserverForTesting(std::function<void(Phase)> observer);

private:
    RecoveryJournal(QString directory, platform::IDurableFiles* durableFiles);
    void appendLine(const QByteArray& line);
    void degrade(const char* key, const QString& context);
    bool writeMeta(const QString& sourcePath, const DocumentId& id);

    QString directory_;
    platform::IDurableFiles* durableFiles_;
    std::unique_ptr<QLockFile> lock_;
    QFile journal_;
    QLatin1String unrecoverableKey_{""};
    QString unrecoverableContext_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_RECOVERYJOURNAL_H
