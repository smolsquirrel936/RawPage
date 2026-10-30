// core/doc/SaveCoordinator -- implementation. Stage 3 T8. The header states the order and why.

#include "core/doc/SaveCoordinator.h"

#include "core/Log.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/DocumentId.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>

#include <mutex>

namespace rawpage::core {
namespace {

std::mutex& observerMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::function<void(SaveCoordinator::Phase, const QString&)>& phaseObserver()
{
    static std::function<void(SaveCoordinator::Phase, const QString&)> instance;
    return instance;
}

void notePhase(SaveCoordinator::Phase phase, const QString& temporary)
{
    std::function<void(SaveCoordinator::Phase, const QString&)> copy;
    {
        std::lock_guard<std::mutex> lock(observerMutex());
        copy = phaseObserver();
    }
    if (copy)
        copy(phase, temporary);
}

SaveOutcome saveAsRequired(const char* reasonKey)
{
    SaveOutcome outcome;
    outcome.kind = SaveOutcome::Kind::SaveAsRequired;
    outcome.reasonKey = QLatin1String(reasonKey);
    return outcome;
}

// D3-16's second half (the header argues the line): would ANOTHER path be a real answer to this
// failure? A refusal by the file system is about this place; `Corrupted` is about the bytes we
// composed, and every path would refuse them the same way.
bool anotherPlaceMightWork(const Error& error)
{
    switch (error.code) {
    case ErrorCode::IoFailure:          // the temporary could not be written in this directory
    case ErrorCode::PermissionDenied:   // read-only, or denied by the OS
    case ErrorCode::Unavailable:        // another process holds the target
    case ErrorCode::NotFound:           // the directory, or the replacement, is gone
        return true;
    default:
        return false;
    }
}

// D3-16: is the file at `path` still the one the document's identity describes? Read and hashed here,
// once per save -- a real cost on a large file, which `rawpage_bench_save` measures (A-61).
bool stillTheOpenedFile(const DocumentId& opened, const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return false;
    // The cheap facts first: a different size or time is already an answer, with no read.
    if (info.size() != opened.size || info.lastModified().toMSecsSinceEpoch() != opened.modifiedMsecs)
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return DocumentId::of(path, info.lastModified().toMSecsSinceEpoch(), file.readAll()) == opened;
}

} // namespace

SaveCoordinator::SaveCoordinator(IDocumentBackend& backend, platform::IDurableFiles* durableFiles)
    : backend_(backend), durableFiles_(durableFiles), processId_(QCoreApplication::applicationPid())
{
}

QString SaveCoordinator::temporaryPathFor(const QString& target, qint64 processId)
{
    const QFileInfo info(target);
    return info.dir().filePath(QStringLiteral(".%1.rawpage-%2.tmp").arg(info.fileName()).arg(processId));
}

Result<SaveOutcome> SaveCoordinator::save(CommandJournal& journal, const QString& path)
{
    // S7 T4: the last edit's verify first -- a save writes the composition that VERIFIED, and an edit refused after
    // it returned is taken back, and told to whoever listens for refusals, before anything is written.
    journal.settle();
    if (durableFiles_ == nullptr)
        return Error{ErrorCode::Unavailable, QLatin1String("error.save.noDurableFiles"), path};
    if (!journal.isDirty())
        return Error{ErrorCode::Unavailable, QLatin1String("error.save.nothingToSave"), path};

    const Document& document = journal.document();
    if (document.repairReport().requiresSaveAs())
        return saveAsRequired("error.save.repairedDocument");
    if (!stillTheOpenedFile(document.id(), path))
        return saveAsRequired("error.save.fileChanged");

    return writeThrough(journal, path);
}

Result<SaveOutcome> SaveCoordinator::saveAs(CommandJournal& journal, const QString& target)
{
    journal.settle();   // S7 T4: as `save`
    if (durableFiles_ == nullptr)
        return Error{ErrorCode::Unavailable, QLatin1String("error.save.noDurableFiles"), target};
    // The document's own file goes through every check a Save makes -- or a repaired document could
    // be "saved as" itself and skip D3-6.
    if (DocumentId::normalizePath(target) == journal.document().id().path)
        return save(journal, target);
    return writeThrough(journal, target);
}

void SaveCoordinator::removeStrayTemporaries(const QString& target)
{
    const QFileInfo info(target);
    const QString prefix = QStringLiteral(".%1.rawpage-").arg(info.fileName());
    const QStringList candidates = info.dir().entryList(
        QStringList{prefix + QStringLiteral("*.tmp")}, QDir::Files | QDir::Hidden | QDir::System);
    for (const QString& name : candidates) {
        bool numeric = false;
        const qint64 owner = name.mid(prefix.size(), name.size() - prefix.size() - 4).toLongLong(&numeric);
        if (!numeric || durableFiles_->processExists(owner))
            continue;   // not ours to judge, or its writer may still be between write and replace
        const QString stray = info.dir().filePath(name);
        const Status removed = durableFiles_->removeFile(stray);
        RAWPAGE_LOG_INFO(LogSubsystem::Save)
            << "save: removed a temporary file left by process" << owner << "--" << stray
            << (removed ? "" : "(failed)");
    }
}

Result<SaveOutcome> SaveCoordinator::writeThrough(CommandJournal& journal, const QString& target)
{
    const DocumentHandle handle = journal.document().handle();
    removeStrayTemporaries(target);
    const QString temporary = temporaryPathFor(target, processId_);

    // Steps 5 and 6. Every failure here leaves the target as it was and the temporary file gone.
    auto abandon = [&](const Error& error) -> Result<SaveOutcome> {
        durableFiles_->removeFile(temporary);
        const bool elsewhere = anotherPlaceMightWork(error);
        RAWPAGE_LOG_WARNING(LogSubsystem::Save)
            << "save: failed before replacing" << target << "--" << QLatin1String(error.messageKey) << error.context
            << (elsewhere ? "-- Save As required" : "-- refused");
        if (!elsewhere)
            return error;
        // The error is not swallowed: it becomes the reason the caller shows BEFORE it asks where to
        // put the file (D3-16). A caller with nobody to ask refuses by `error.save.cancelled` and
        // writes nothing -- flow K reaches this line and never blocks on a dialog.
        SaveOutcome outcome;
        outcome.kind = SaveOutcome::Kind::SaveAsRequired;
        outcome.reasonKey = error.messageKey;
        outcome.reasonContext = error.context;
        return outcome;
    };
    notePhase(Phase::Writing, temporary);
    if (const Status written = backend_.writeRevision(handle, temporary); !written)
        return abandon(written.error());
    notePhase(Phase::Written, temporary);
    if (const Status flushed = durableFiles_->syncToDisk(temporary); !flushed)
        return abandon(flushed.error());
    notePhase(Phase::Replacing, temporary);
    if (const Status replaced = durableFiles_->replaceFile(target, temporary); !replaced)
        return abandon(replaced.error());
    notePhase(Phase::Replaced, temporary);

    // Step 7, after the replace: see the header for why a failure here asks for Save-As next time.
    if (const Status adopted = backend_.adoptRevision(handle, target); !adopted) {
        RAWPAGE_LOG_ERROR(LogSubsystem::Save)
            << "save: wrote" << target << "but the backend could not adopt it --"
            << QLatin1String(adopted.error().messageKey) << adopted.error().context;
        return adopted.error();
    }
    if (const Status marked = journal.adoptSavedFile(backend_, target); !marked)
        return marked.error();

    RAWPAGE_LOG_INFO(LogSubsystem::Save) << "save: saved" << target;
    SaveOutcome outcome;
    outcome.path = target;
    return outcome;
}

void SaveCoordinator::setPhaseObserverForTesting(std::function<void(Phase, const QString&)> observer)
{
    std::lock_guard<std::mutex> lock(observerMutex());
    phaseObserver() = std::move(observer);
}

} // namespace rawpage::core
