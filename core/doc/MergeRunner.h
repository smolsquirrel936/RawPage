// core/doc/MergeRunner -- a merge, off the thread that asked for it. Stage 7 T10 (`Stage7-Assembly-Plan.md` D7-16;
// guide §3.5 and A-72).
//
// WHY A THREAD. A thirty-way merge reads, copies, builds and verifies every input -- seconds of work that is all
// first-party (`IDocumentBackend::mergeFiles`: no engine, no open document), so nothing obliges it to run where the
// person waits. §3.5 bounds the UI thread to no gap over 100 ms while it runs, and cancel to a second.
//
// THE CONTRACT. `start` runs `mergeFiles` on a thread of its own. `progress` and `done` are called on the thread that
// called `start` -- posted to it through its event loop, so that thread must run one -- `progress` as each step ends,
// `done` exactly once per run `start` accepted, with the merged document or the refusal: a merge `cancel`led answers
// `error.merge.cancelled` at its next step. The run is over, and `isRunning` false, only once `done` has been called --
// so a merge cancelled and started again waits for the first to stop (`error.merge.busy`). Destroying the runner
// cancels a run, waits for its thread, and calls nothing.

#ifndef RAWPAGE_CORE_DOC_MERGERUNNER_H
#define RAWPAGE_CORE_DOC_MERGERUNNER_H

#include "core/backend.h"
#include "core/doc/Merge.h"

#include <QObject>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace rawpage::core {

class MergeRunner {
public:
    using Progress = std::function<void(int done, int total)>;
    using Done = std::function<void(Result<MergedDocument>)>;

    explicit MergeRunner(IDocumentBackend& backend);
    ~MergeRunner();
    MergeRunner(const MergeRunner&) = delete;
    MergeRunner& operator=(const MergeRunner&) = delete;

    // Refused, with no run and `done` not called: a run still going (`error.merge.busy`) and no input at all
    // (`error.merge.noInputs`).
    Status start(std::vector<MergeInput> inputs, MergeOptions options, Progress progress, Done done);
    bool isRunning() const noexcept { return running_; }
    void cancel() noexcept { cancelled_->store(true); }

private:
    void join();

    IDocumentBackend& backend_;
    QObject context_;   // lives on the starting thread: what the worker posts to, and what drops its posts once gone
    std::thread worker_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    bool running_ = false;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_MERGERUNNER_H
