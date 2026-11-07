// core/doc/PageWorkRunner -- implementation. Stage 7 T9. See the header.

#include "core/doc/PageWorkRunner.h"

namespace rawpage::core {

PageWorkRunner::PageWorkRunner(render::RenderScheduler& scheduler) : scheduler_(scheduler)
{
    subscription_ = scheduler_.subscribeTasks([this](render::RenderTicket ticket) { onTaskDone(ticket); });
}

PageWorkRunner::~PageWorkRunner()
{
    cancel();
    scheduler_.unsubscribeTasks(subscription_);
}

DocumentHandle PageWorkRunner::runningFor() const noexcept
{
    return run_ ? run_->document : DocumentHandle{};
}

void PageWorkRunner::cancel()
{
    if (!run_) return;
    for (const auto& [ticket, page] : run_->tickets) scheduler_.cancel(render::RenderTicket{ticket});
    run_.reset();
}

Status PageWorkRunner::start(DocumentHandle document, std::vector<int> pages, Work work, Done done)
{
    if (run_) return Error{ErrorCode::Unavailable, QLatin1String("error.pages.busy"), QString()};
    if (pages.empty() || !work || !done) return Error{ErrorCode::InvalidArgument, QLatin1String("error.pages.busy"), QString()};
    run_ = std::make_unique<Run>();
    run_->document = document;
    run_->pages = std::move(pages);
    run_->work = std::move(work);
    run_->done = std::move(done);
    queue();
    return Status{};
}

void PageWorkRunner::queue()
{
    Run& run = *run_;
    while (run.next < run.pages.size() && static_cast<int>(run.tickets.size()) < kTaskWindow) {
        const int page = run.pages[run.next++];
        const Work work = run.work;
        const render::RenderTicket ticket =
            scheduler_.enqueueTask(run.document, render::RenderPriority::Visible, [work, page] { work(page); });
        if (!ticket.isValid()) {
            finish(Error{ErrorCode::Unavailable, QLatin1String("error.pages.unreadable"), QString::number(page + 1)});
            return;
        }
        run.tickets.emplace(ticket.id, page);
    }
}

void PageWorkRunner::onTaskDone(render::RenderTicket ticket)
{
    if (!run_) return;
    const auto found = run_->tickets.find(ticket.id);
    if (found == run_->tickets.end()) return;
    run_->tickets.erase(found);
    if (++run_->answered < run_->pages.size()) {
        queue();
        return;
    }
    finish(Status{});
}

void PageWorkRunner::finish(Status status)
{
    // THE RUN ENDS BEFORE `done` IS CALLED, so `done` may start the next one.
    std::unique_ptr<Run> run = std::move(run_);
    for (const auto& [ticket, page] : run->tickets) scheduler_.cancel(render::RenderTicket{ticket});
    run->done(std::move(status));
}

} // namespace rawpage::core
