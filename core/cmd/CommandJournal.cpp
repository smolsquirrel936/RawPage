// core/cmd/CommandJournal -- implementation. Stage 3 T5. See the header for the three numbers.

#include "core/cmd/CommandJournal.h"

#include "core/Log.h"
#include "core/backend.h"

#include <utility>

namespace rawpage::core {
namespace {

CommandJournal::PartialApplicationHandler& partialHandler()
{
    static CommandJournal::PartialApplicationHandler handler;
    return handler;
}

void reportPartialApplication(const Command& command)
{
    if (partialHandler()) {
        partialHandler()(command);
        return;
    }
    // Under `Save`: the journal is the edit half of the edit-and-save pipeline, and a new subsystem would
    // need its own logging preference for one message.
    RAWPAGE_LOG_ERROR(LogSubsystem::Save)
        << "journal: PARTIAL APPLICATION --" << QLatin1String(command.typeId())
        << "returned false after changing the document; the model no longer matches the history";
    Q_ASSERT_X(false, "CommandJournal", "a command changed the document and returned false");
}

} // namespace

CommandJournal::CommandJournal(Document document, MutationSink* sink)
    : document_(std::move(document)), sink_(sink)
{
}

void CommandJournal::setPartialApplicationHandler(PartialApplicationHandler handler)
{
    partialHandler() = std::move(handler);
}

bool CommandJournal::isDirty() const noexcept
{
    return !savedPosition_ || *savedPosition_ != position_;
}

bool CommandJournal::canUndo() const noexcept
{
    return groupDepth_ == 0 && position_ > 0;
}

bool CommandJournal::canRedo() const noexcept
{
    return groupDepth_ == 0 && position_ < history_.size();
}

std::optional<CatalogText> CommandJournal::undoName() const
{
    if (!canUndo())
        return std::nullopt;
    return history_[position_ - 1].name;
}

std::optional<CatalogText> CommandJournal::redoName() const
{
    if (!canRedo())
        return std::nullopt;
    return history_[position_].name;
}

Status CommandJournal::run(std::unique_ptr<Command> command)
{
    if (!command)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.command.failed"), QString()};

    // 1. The pre-flight, before the command sees anything (D3-7).
    const Status permitted = command->permitted(document_);
    if (!permitted)
        return permitted;
    if (sink_ == nullptr) {
        return Error{ErrorCode::Unavailable, QLatin1String("error.command.noSink"),
                     QLatin1String(command->typeId())};
    }

    // 2. Apply, watching for a false that changed something.
    const QByteArray before = document_.modelHash();
    if (!command->apply(document_, *sink_)) {
        if (document_.modelHash() != before)
            reportPartialApplication(*command);
        return Error{ErrorCode::Unknown, QLatin1String("error.command.failed"),
                     QLatin1String(command->typeId())};
    }

    // 2b. Shown, or not done (S6 T4, D6-2): a refusal reverts the command and leaves no trace.
    if (showHandler_) {
        const Status shown = showHandler_(command->scope());
        if (!shown) {
            if (!command->revert(document_, *sink_) || document_.modelHash() != before)
                reportPartialApplication(*command);
            return shown;
        }
    }

    // 3. Record it: into the open group's step, or as a new step that throws away redo.
    const bool wasDirty = isDirty();
    const Scope scope = command->scope();
    const Command& recorded = *command;
    std::optional<CatalogText> opensGroup;
    if (groupDepth_ > 0 && groupStepOpen_) {
        history_[position_ - 1].commands.push_back(std::move(command));
    } else {
        if (groupDepth_ > 0)
            opensGroup = groupName_;
        if (history_.size() > position_) {
            history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(position_), history_.end());
            if (savedPosition_ && *savedPosition_ > position_)
                savedPosition_.reset();    // the saved state was in the redo just discarded
        }
        Step step;
        step.name = groupDepth_ > 0 ? groupName_ : command->name();
        step.commands.push_back(std::move(command));
        history_.push_back(std::move(step));
        ++position_;
        if (groupDepth_ > 0)
            groupStepOpen_ = true;
    }
    // 4. Durable before this returns (D3-11). A recorder that fails degrades itself; it cannot refuse.
    if (recorder_ != nullptr)
        recorder_->recordRun(recorded, opensGroup);
    afterOperation(scope, wasDirty);
    return Status{};
}

bool CommandJournal::revertStep(Step& step)
{
    // In reverse. If one refuses, re-apply the ones already reverted so the document is back where it
    // was -- a half-undone step is exactly the partial application this class refuses to create.
    for (std::size_t done = 0; done < step.commands.size(); ++done) {
        Command& command = *step.commands[step.commands.size() - 1 - done];
        if (!command.revert(document_, *sink_)) {
            for (std::size_t redoing = done; redoing > 0; --redoing) {
                Command& back = *step.commands[step.commands.size() - redoing];
                if (!back.apply(document_, *sink_))
                    reportPartialApplication(back);
            }
            return false;
        }
    }
    return true;
}

bool CommandJournal::applyStep(Step& step)
{
    for (std::size_t done = 0; done < step.commands.size(); ++done) {
        if (!step.commands[done]->apply(document_, *sink_)) {
            for (std::size_t undoing = done; undoing > 0; --undoing) {
                Command& back = *step.commands[undoing - 1];
                if (!back.revert(document_, *sink_))
                    reportPartialApplication(back);
            }
            return false;
        }
    }
    return true;
}

Scope CommandJournal::scopeOf(const Step& step) const
{
    Scope scope;
    bool first = true;
    for (const auto& command : step.commands) {
        scope = first ? command->scope() : scope.merged(command->scope());
        first = false;
    }
    return scope;
}

bool CommandJournal::undo()
{
    if (!canUndo() || sink_ == nullptr)
        return false;
    const bool wasDirty = isDirty();
    Step& step = history_[position_ - 1];
    if (!revertStep(step))
        return false;
    if (showHandler_ && !showHandler_(scopeOf(step))) {
        if (!applyStep(step)) {   // back where it was: the undo did not happen
            RAWPAGE_LOG_ERROR(LogSubsystem::Save) << "journal: an undo that could not be shown could not be taken back";
        }
        return false;
    }
    --position_;
    if (recorder_ != nullptr)
        recorder_->recordUndo(step.name, commandsOf(step));
    afterOperation(scopeOf(step), wasDirty);
    return true;
}

bool CommandJournal::redo()
{
    if (!canRedo() || sink_ == nullptr)
        return false;
    const bool wasDirty = isDirty();
    Step& step = history_[position_];
    if (!applyStep(step))
        return false;
    if (showHandler_ && !showHandler_(scopeOf(step))) {
        if (!revertStep(step)) {   // back where it was: the redo did not happen
            RAWPAGE_LOG_ERROR(LogSubsystem::Save) << "journal: a redo that could not be shown could not be taken back";
        }
        return false;
    }
    ++position_;
    if (recorder_ != nullptr)
        recorder_->recordRedo(step.name, commandsOf(step));
    afterOperation(scopeOf(step), wasDirty);
    return true;
}

void CommandJournal::beginGroup(CatalogText name)
{
    if (groupDepth_ == 0) {
        groupName_ = std::move(name);
        groupStepOpen_ = false;
    }
    ++groupDepth_;
}

bool CommandJournal::endGroup()
{
    if (groupDepth_ == 0)
        return false;
    if (--groupDepth_ == 0) {
        if (groupStepOpen_ && recorder_ != nullptr)
            recorder_->recordGroupEnd();
        groupStepOpen_ = false;
    }
    return true;
}

bool CommandJournal::abandonOpenGroup()
{
    if (groupDepth_ == 0)
        return false;
    const bool wasDirty = isDirty();
    groupDepth_ = 0;
    if (!groupStepOpen_)
        return true;                          // nothing was run inside it: nothing to take back
    groupStepOpen_ = false;
    Step& step = history_[position_ - 1];
    const Scope scope = scopeOf(step);
    if (!revertStep(step))
        return false;
    // Shown as well (D6-2) -- but a group abandoned is a step that never happened, so a refusal here
    // cannot keep it: it is logged, and the engine goes on drawing what it drew.
    if (showHandler_) {
        if (const Status shown = showHandler_(scope); !shown) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Save)
                << "journal: an abandoned group could not be shown --" << shown.error().messageKey;
        }
    }
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(position_ - 1));
    --position_;
    afterOperation(scope, wasDirty);
    return true;
}

std::vector<const Command*> CommandJournal::commandsOf(const Step& step)
{
    std::vector<const Command*> commands;
    for (const auto& command : step.commands)
        commands.push_back(command.get());
    return commands;
}

std::vector<const Command*> CommandJournal::undoStep() const
{
    return canUndo() ? commandsOf(history_[position_ - 1]) : std::vector<const Command*>{};
}

std::vector<const Command*> CommandJournal::redoStep() const
{
    return canRedo() ? commandsOf(history_[position_]) : std::vector<const Command*>{};
}

void CommandJournal::markSaved()
{
    const bool wasDirty = isDirty();
    savedPosition_ = position_;
    if (wasDirty != isDirty()) {
        const auto handlers = dirtyHandlers_;
        for (const auto& [token, handler] : handlers)
            if (handler) handler(isDirty());
    }
}

Status CommandJournal::adoptSavedFile(const IDocumentBackend& backend, const QString& path)
{
    const Status refreshed = document_.refreshFileState(backend);
    if (!refreshed)
        return refreshed;
    if (recorder_ != nullptr)
        recorder_->recordSaved(document_, path);
    markSaved();
    return Status{};
}

void CommandJournal::afterOperation(const Scope& scope, bool wasDirty)
{
    document_.advanceRevision();

    // Copies, because a handler may unsubscribe itself -- or another -- while being called.
    const auto mutated = mutatedHandlers_;
    for (const auto& [token, handler] : mutated)
        if (handler) handler(scope);

    if (wasDirty != isDirty()) {
        const auto dirty = dirtyHandlers_;
        for (const auto& [token, handler] : dirty)
            if (handler) handler(isDirty());
    }
}

int CommandJournal::subscribeMutated(MutatedHandler handler)
{
    const int token = nextToken_++;
    mutatedHandlers_.emplace(token, std::move(handler));
    return token;
}

int CommandJournal::subscribeDirty(DirtyHandler handler)
{
    const int token = nextToken_++;
    dirtyHandlers_.emplace(token, std::move(handler));
    return token;
}

void CommandJournal::unsubscribe(int token)
{
    mutatedHandlers_.erase(token);
    dirtyHandlers_.erase(token);
}

} // namespace rawpage::core
