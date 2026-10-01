// core/text/FolderSearch -- Advanced Search over a FOLDER: every PDF in it, optionally its subfolders, one
// file at a time, never on the UI thread. Stage 5 T9, decision D5-13 (`Stage5-TextSearch-Plan.md` §3, as
// amended at T3).
//
// ---------------------------------------------------------------------------------------------
// ONE FILE AT A TIME, THROUGH WHAT ALREADY SEARCHES ONE DOCUMENT
// ---------------------------------------------------------------------------------------------
// A file is OPENED through `IDocumentBackend`'s staged open, ATTACHED to `DocumentIndex` under its
// identity, SEARCHED by `SearchEngine` with the query -- every flag, every content class, the bound --
// and CLOSED; then the next. So a folder search finds exactly what the find bar would in each file, and
// a file that is also open in a tab shares that tab's index entry (one identity, one entry) rather than
// extracting twice. **It is never shown in a tab** (D5-13), and **it never asks for a password**: an
// encrypted file is listed as skipped with a reason, a damaged one with its error, and the search goes
// on (guide A-81).
//
// ---------------------------------------------------------------------------------------------
// WHERE EACH PIECE RUNS (D5-13 as amended at T3; guide A-88)
// ---------------------------------------------------------------------------------------------
// `Tech-Stack-Plan.md` §3.3 measured the cost: `PdfiumDocument::open` held the engine ~145 ms for a
// 900-page file, and as one job a folder search's open would hold a visible tile for all of it. So the
// open is asked for in PIECES (`IDocumentBackend::beginOpen`/`continueOpen`), and each runs where it
// belongs:
//
//   * an ENGINE piece -- the load, the form environment, the page walk a few pages a piece, the readers
//     -- as an engine task on the render thread at `Background`, so strict priority keeps every visible
//     tile ahead of it and a tile waits behind at most one piece;
//   * every OTHER piece -- listing the folder, reading a file, the first-party reading -- on this
//     object's own HELPER thread, which never touches the engine.
//
// The UI thread asks (`beginOpen` reads nothing) and is told; it opens nothing. One file's pieces run one
// at a time. Closing a file is an EXCLUSIVE engine task after `RenderScheduler::cancelDocument`, which
// waits out whatever was already inside the engine for it, so nothing blocks on a drain.
//
// ---------------------------------------------------------------------------------------------
// CANCELLED, AND BOUNDED
// ---------------------------------------------------------------------------------------------
// `cancel()` stops at once: nothing more is delivered, the current file's search is cancelled, and the
// piece already running finishes and cleans up after itself -- an opening abandoned, a handle closed --
// so cancelling "stops within one file" with nothing left open. `SearchQuery::maxResults` bounds the
// WHOLE folder: each file is searched for what is left of it, and reaching it is said.

#ifndef RAWPAGE_CORE_TEXT_FOLDERSEARCH_H
#define RAWPAGE_CORE_TEXT_FOLDERSEARCH_H

#include "core/Result.h"
#include "core/backend.h"
#include "core/render/RenderScheduler.h"
#include "core/text/DocumentIndex.h"
#include "core/text/SearchEngine.h"
#include "core/text/SearchQuery.h"
#include "core/text/SearchResults.h"

#include <QString>
#include <QStringList>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rawpage::core::text {

class FolderSearch {
public:
    struct Request {
        QString folder;
        bool recursive = false;
        // When not empty, exactly these files, in this order, and `folder` is not listed -- what a REFINE
        // searches: the files of the first search's results.
        QStringList files;
        SearchQuery query;   // its `maxResults` bounds the whole folder
    };

    struct Progress {
        int filesDone = 0;
        int filesTotal = 0;   // 0 while the folder is still being listed
        int hits = 0;
    };

    struct Summary {
        int filesSearched = 0;
        int filesRefused = 0;
        int hits = 0;
        bool limitReached = false;
    };

    struct Handlers {
        std::function<void(FileResult)> file;              // each file, searched or refused, as it finishes
        std::function<void(const Progress&)> progress;     // optional
        std::function<void(const Summary&)> finished;
    };

    using WakeHandler = std::function<void()>;
    // A MEASUREMENT HOOK, AND NOTHING IN THE PRODUCT INSTALLS ONE (`SearchEngine::MatchObserver`'s
    // precedent): every piece of every open, whether it needed the engine, and how long it took -- called
    // ON THE THREAD THAT RAN IT, which is what guide A-88 asserts on.
    using PieceObserver = std::function<void(bool engine, double milliseconds)>;

    FolderSearch(IDocumentBackend& backend, render::RenderScheduler& scheduler, DocumentIndex& index,
                 SearchEngine& engine);
    ~FolderSearch();

    FolderSearch(const FolderSearch&) = delete;
    FolderSearch& operator=(const FolderSearch&) = delete;

    // Refused, and nothing started: an empty pattern or an invalid regular expression (the find bar's own
    // refusals), or a folder that does not exist (`error.search.noFolder`). A search already running is
    // cancelled first.
    Status start(Request request, Handlers handlers);
    void cancel();
    bool isRunning() const noexcept { return running_; }

    // THE PUMP for the helper thread's results, on the thread that pumps the scheduler and the search
    // engine -- every handler runs there. Returns how many it delivered.
    int deliverReady();
    // Called FROM THE HELPER THREAD, carrying nothing; must post and return.
    void setWakeHandler(WakeHandler handler);
    void setPieceObserver(PieceObserver observer);
    // Until the helper thread has nothing queued or running. For tests.
    bool waitForIdle(int milliseconds);

private:
    // ONE PIECE IN FLIGHT, shared with whoever runs it. If the run is cancelled while it runs, it cleans up
    // after itself: the side that finds the other already done does the cleaning, under `mutex`.
    struct Flight {
        std::mutex mutex;
        bool finished = false;
        bool orphaned = false;
        OpeningId opening = 0;
        Result<OpenProgress> result{OpenProgress{}};
    };

    struct HelperJob {
        std::function<void()> work;      // on the helper thread
        std::function<void()> deliver;   // then on the pump
    };

    void runHelper(std::function<void()> work, std::function<void()> deliver);
    void helperLoop();

    void listed(std::uint64_t run, QStringList files);
    void nextFile();
    void step(std::shared_ptr<Flight> flight, const OpenProgress& progress);
    void pieceDone(std::uint64_t run, const std::shared_ptr<Flight>& flight);
    void opened(DocumentHandle handle);
    void searched(const SearchEngine::Outcome& outcome);
    void refused(const Error& error);
    void closeCurrent();
    void finishRun();
    void onTask(render::RenderTicket ticket);
    // What a piece that outlived its run leaves behind: an opening abandoned, or a handle closed -- on the
    // render thread, as an engine task.
    void cleanUp(const Result<OpenProgress>& result, OpeningId opening);

    IDocumentBackend& backend_;
    render::RenderScheduler& scheduler_;
    DocumentIndex& index_;
    SearchEngine& engine_;
    SearchSession session_;
    render::RenderScheduler::SubscriptionToken taskToken_ = 0;

    // ---- The run, on the pumping thread ---------------------------------------------------------
    std::uint64_t run_ = 0;          // the current run's generation; a result of an older one is dropped
    bool running_ = false;
    Request request_;
    Handlers handlers_;
    QStringList files_;
    int nextFile_ = 0;
    int hits_ = 0;
    Summary summary_;
    QString currentPath_;
    std::shared_ptr<Flight> flight_;               // the piece in flight, if any
    DocumentHandle handle_;                          // the file open now, if any
    DocumentIndex::EntryId entry_ = 0;
    std::map<std::uint64_t, std::shared_ptr<Flight>> engineFlights_;   // by ticket id

    // ---- The helper thread, under `mutex_` ------------------------------------------------------
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<HelperJob> queue_;
    std::deque<std::function<void()>> completions_;
    bool helperBusy_ = false;
    bool stopping_ = false;
    WakeHandler wakeHandler_;
    PieceObserver pieceObserver_;
    std::thread helper_;
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_FOLDERSEARCH_H
