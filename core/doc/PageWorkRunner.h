// core/doc/PageWorkRunner -- one ENGINE task per page of a set, on the render thread, and a call when every one has
// run. Stage 7 T9 (`Stage7-Assembly-Plan.md` D7-15, D7-32): what *remove white margins* needs of each page (its
// content box, Stage 4's D4-14) and what *embed thumbnails* needs (its raster) is the ENGINE's to answer, and the UI
// thread never asks the engine for a document's worth of pages (CLAUDE.md: the UI thread never rasterizes).
//
// SPLIT'S SHAPE, MADE GENERAL (`core/doc/SplitRunner`, its blank pages): each page's work is an engine task at
// `Visible` -- a person is waiting -- with at most `kTaskWindow` queued at once, so a 900-page run never fills the
// scheduler's bounded queue, whose overflow drops its LOWEST lane, which `Visible` is not. The work runs ON THE WORKER
// and writes what it finds where its caller keeps it, under the caller's own lock; the scheduler's report that it
// ran, on the pumping thread, is the hand-off. `done` is called exactly once per run `start` accepted, from that pump,
// with the run's verdict: success when every page's work ran, `error.pages.unreadable` naming the first page whose
// task the scheduler refused. A run `cancel`led or destroyed calls nothing; `cancel` is also what a document closing
// asks for (`runningFor`).

#ifndef RAWPAGE_CORE_DOC_PAGEWORKRUNNER_H
#define RAWPAGE_CORE_DOC_PAGEWORKRUNNER_H

#include "core/backend.h"
#include "core/render/RenderScheduler.h"

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace rawpage::core {

class PageWorkRunner {
public:
    static constexpr int kTaskWindow = 8;

    using Work = std::function<void(int page)>;   // ON THE WORKER, once per page
    using Done = std::function<void(Status)>;     // on the pumping thread, once

    explicit PageWorkRunner(render::RenderScheduler& scheduler);
    ~PageWorkRunner();
    PageWorkRunner(const PageWorkRunner&) = delete;
    PageWorkRunner& operator=(const PageWorkRunner&) = delete;

    // Refused, with no run and `done` not called: a run already going (`error.pages.busy`), and an empty set or work.
    Status start(DocumentHandle document, std::vector<int> pages, Work work, Done done);
    bool isRunning() const noexcept { return run_ != nullptr; }
    DocumentHandle runningFor() const noexcept;
    void cancel();

private:
    struct Run {
        DocumentHandle document;
        std::vector<int> pages;
        Work work;
        Done done;
        std::size_t next = 0;
        std::size_t answered = 0;
        std::map<std::uint64_t, int> tickets;   // in flight: ticket -> page
    };
    void queue();
    void onTaskDone(render::RenderTicket ticket);
    void finish(Status status);

    render::RenderScheduler& scheduler_;
    render::RenderScheduler::SubscriptionToken subscription_ = 0;
    std::unique_ptr<Run> run_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_PAGEWORKRUNNER_H
