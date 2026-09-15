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

namespace rawpage::core {
namespace {

SaveOutcome saveAsRequired(const char* reasonKey)
{
    SaveOutcome outcome;
    outcome.kind = SaveOutcome::Kind::SaveAsRequired;
    outcome.reasonKey = QLatin1String(reasonKey);
    return outcome;
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
        RAWPAGE_LOG_WARNING(LogSubsystem::Save)
            << "save: failed before replacing" << target << "--" << QLatin1String(error.messageKey) << error.context;
        return error;
    };
    if (const Status written = backend_.writeRevision(handle, temporary); !written)
        return abandon(written.error());
    if (const Status flushed = durableFiles_->syncToDisk(temporary); !flushed)
        return abandon(flushed.error());
    if (const Status replaced = durableFiles_->replaceFile(target, temporary); !replaced)
        return abandon(replaced.error());

    // Step 7, after the replace: see the header for why a failure here asks for Save-As next time.
    if (const Status adopted = backend_.adoptRevision(handle, target); !adopted) {
        RAWPAGE_LOG_ERROR(LogSubsystem::Save)
            << "save: wrote" << target << "but the backend could not adopt it --"
            << QLatin1String(adopted.error().messageKey) << adopted.error().context;
        return adopted.error();
    }
    if (const Status marked = journal.adoptSavedFile(backend_); !marked)
        return marked.error();

    RAWPAGE_LOG_INFO(LogSubsystem::Save) << "save: saved" << target;
    SaveOutcome outcome;
    outcome.path = target;
    return outcome;
}

} // namespace rawpage::core
