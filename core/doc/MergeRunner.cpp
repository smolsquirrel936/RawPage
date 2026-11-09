// core/doc/MergeRunner -- implementation. Stage 7 T10. The header says which thread calls what.

#include "core/doc/MergeRunner.h"

#include "core/Log.h"

#include <QMetaObject>

#include <utility>

namespace rawpage::core {

MergeRunner::MergeRunner(IDocumentBackend& backend)
    : backend_(backend), cancelled_(std::make_shared<std::atomic<bool>>(false))
{
}

MergeRunner::~MergeRunner()
{
    cancel();
    join();
}

void MergeRunner::join()
{
    if (worker_.joinable()) worker_.join();
}

Status MergeRunner::start(std::vector<MergeInput> inputs, MergeOptions options, Progress progress, Done done)
{
    if (running_) return Error{ErrorCode::Unavailable, QLatin1String("error.merge.busy"), QString()};
    if (inputs.empty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.merge.noInputs"), QString()};
    join();   // a run that has answered may not have returned from its thread yet
    running_ = true;
    cancelled_ = std::make_shared<std::atomic<bool>>(false);
    RAWPAGE_LOG_INFO(LogSubsystem::Save) << "merge: started over" << static_cast<int>(inputs.size()) << "input(s)";

    // The worker holds what it needs by value -- the flag shared, the backend outliving the runner (the composition
    // root's) -- and reaches back only through `context_`, whose posts die with it.
    QObject* context = &context_;
    std::shared_ptr<std::atomic<bool>> cancelled = cancelled_;
    IDocumentBackend* backend = &backend_;
    worker_ = std::thread([this, context, cancelled, backend, inputs = std::move(inputs), options, progress = std::move(progress),
                           done = std::move(done)]() mutable {
        const MergeProgress step = [context, cancelled, progress](int finished, int total) {
            if (progress) {
                QMetaObject::invokeMethod(context, [progress, finished, total] { progress(finished, total); }, Qt::QueuedConnection);
            }
            return !cancelled->load();
        };
        Result<MergedDocument> merged = backend->mergeFiles(inputs, options, step);
        if (merged && cancelled->load())
            merged = Error{ErrorCode::Unavailable, QLatin1String("error.merge.cancelled"), QString()};
        auto shared = std::make_shared<Result<MergedDocument>>(std::move(merged));
        QMetaObject::invokeMethod(
            context,
            [this, shared, done = std::move(done)] {
                running_ = false;
                join();
                if (*shared) {
                    RAWPAGE_LOG_INFO(LogSubsystem::Save)
                        << "merge: done --" << shared->value().report.inputsMerged << "input(s)," << shared->value().report.pageCount
                        << "page(s)," << static_cast<int>(shared->value().report.skipped.size()) << "skipped";
                } else {
                    RAWPAGE_LOG_INFO(LogSubsystem::Save) << "merge: refused --" << shared->error().messageKey << shared->error().context;
                }
                if (done) done(std::move(*shared));
            },
            Qt::QueuedConnection);
    });
    return Status{};
}

} // namespace rawpage::core
