// core/doc/SplitRunner -- a split, from what the person asked to the files on disk. Stage 7 T7 (`Stage7-Assembly-
// Plan.md` §6, D7-30; guide A-49, A-50).
//
// WHERE EACH FACT COMES FROM. `SplitPlan` decides which pages go in which file from facts; this gathers them, from the
// layer that has each, and never on the UI thread when the engine is the one that has it:
//   * EVERY N, BOOKMARKS -- the page count and the outline the backend carries (`IDocumentBackend::outline`, which
//     names pages as they stand now, D7-3). Nothing to wait for.
//   * BY SIZE -- the assembler, which weighs a run of pages by making the file it would write (`AssemblySource`). The
//     snapshot is first-party, no engine, so it runs where the caller does, as a save does.
//   * BLANK PAGES -- the ENGINE's content box (Stage 4, D4-14): one engine task per page on the render thread at
//     `Visible` (a person is waiting for the split), at most `kTaskWindow` queued at once so the split never fills
//     the scheduler's bounded queue -- whose overflow drops its LOWEST lane, which `Visible` is not.
//   * A TEXT PATTERN -- the SEARCH ENGINE over the document's text index (Stage 5, D5-5): the same match the find bar
//     makes, literal or regular, case as asked; each page holding a hit starts a file.
// Then the plan, and `AssemblySource::write` -- refused before a byte is written for a name the folder holds.
//
// THE SNAPSHOT IS TAKEN AT `start`, so the files are what the document was when the person asked. A page command
// landing while the engine reads its facts would make the facts and the snapshot two documents, so the run REFUSES
// (`error.split.documentChanged`) when the backend's page count no longer matches the snapshot's at the end.
//
// `done` is called exactly once per run that `start` accepted: from inside `start` for a split with nothing to wait
// for, or from the pump that delivers its last fact -- the scheduler's `deliverReady` or the search engine's, both on
// the thread that pumps them. A run `cancel`led, or destroyed, calls nothing.

#ifndef RAWPAGE_CORE_DOC_SPLITRUNNER_H
#define RAWPAGE_CORE_DOC_SPLITRUNNER_H

#include "core/backend.h"
#include "core/doc/Assembly.h"
#include "core/doc/SplitPlan.h"
#include "core/render/RenderScheduler.h"
#include "core/text/DocumentIndex.h"
#include "core/text/SearchEngine.h"
#include "core/text/SearchQuery.h"

#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace rawpage::core {

struct SplitRequest {
    SplitMethod method = SplitMethod::EveryN;
    int pagesPerFile = 1;               // EveryN
    int level = 1;                      // BookmarkLevel (TopLevelBookmarks is level 1)
    qint64 maxBytes = 0;                // MaxSize
    text::SearchQuery pattern;          // TextPattern: its pattern and flags; scope and bound are the runner's
    QString folder;                     // where the files go
    QString namePattern;                // `%1` the stem, `%2` the file's number -- the caller's, from the catalog
    QString stem;                       // the source's name without its suffix
};

struct SplitOutcome {
    std::vector<SplitPart> parts;
    std::vector<WrittenDocument> written;   // parallel to `parts`
    int blankPagesLeftOut = 0;              // BlankPages: the separators, not written
};

class SplitRunner {
public:
    static constexpr int kTaskWindow = 8;

    using Done = std::function<void(Result<SplitOutcome>)>;

    SplitRunner(IDocumentBackend& backend, render::RenderScheduler& scheduler, text::SearchEngine& search);
    ~SplitRunner();
    SplitRunner(const SplitRunner&) = delete;
    SplitRunner& operator=(const SplitRunner&) = delete;

    // `entry` is the document's text-index entry, read only by a pattern split. Refused, with no run and `done` not
    // called: a run already going (`error.split.busy`), a request no rule accepts (`SplitPlan`'s keys), and a document
    // the backend will not snapshot. Everything else is answered through `done`.
    Status start(DocumentHandle document, text::DocumentIndex::EntryId entry, SplitRequest request, Done done);
    bool isRunning() const noexcept { return run_ != nullptr; }
    // The document a run in progress is splitting; invalid when none is.
    DocumentHandle runningFor() const noexcept;
    void cancel();

    // The name each file of a split of `pageCount` pages would get -- `request`'s pattern over its stem, numbered
    // from 1, zero-padded to the count's width.
    static std::vector<QString> fileNames(const SplitRequest& request, std::size_t count);

private:
    struct Run;
    void onTaskDone(render::RenderTicket ticket);
    void queueBlankTasks();
    void finish(std::optional<Result<std::vector<SplitPart>>> plan = std::nullopt);

    IDocumentBackend& backend_;
    render::RenderScheduler& scheduler_;
    text::SearchEngine& search_;
    std::uint64_t taskSubscription_ = 0;
    std::unique_ptr<Run> run_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_SPLITRUNNER_H
