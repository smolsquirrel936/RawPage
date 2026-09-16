// pdf/ops/RevisionWriter -- implementation. Stage 3 T8. See the header for the order and the seam.

#include "pdf/ops/RevisionWriter.h"

#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/PageOps.h"

#include <QFile>
#include <QIODevice>

#include <cstring>
#include <mutex>

namespace rawpage::pdf::ops {
namespace {

core::Error saveError(core::ErrorCode code, const char* key, const QString& context = {})
{
    return core::Error{code, QLatin1String(key), context};
}

std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::mutex& faultMutex()
{
    static std::mutex mutex;
    return mutex;
}

RevisionWriter::ComposeFault& fault()
{
    static RevisionWriter::ComposeFault instance;
    return instance;
}

core::Result<QByteArray> readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return saveError(core::ErrorCode::IoFailure, "error.save.readBackFailed", path);
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return saveError(core::ErrorCode::IoFailure, "error.save.readBackFailed", path);
    return bytes;
}

void removeQuietly(const QString& path)
{
    QFile::remove(path);
}

} // namespace

core::Result<ComposedRevision> RevisionWriter::compose(MutationSinkImpl& sink, bool complete)
{
    auto delta = sink.delta();
    if (!delta)
        return delta.error();

    const std::string_view original = sink.bytes();
    ComposedRevision composed;
    composed.complete = complete;

    // Whenever anything will follow the original, the delta must not let it change what the original
    // MEANS -- an object that runs to the end of the file, a number referenced and never defined
    // (Stage 3 guide issue I-12; `IncrementalWriter::protectOriginal` says how).
    if (complete || !delta.value().isEmpty())
        cos::IncrementalWriter::protectOriginal(sink.table(), delta.value());

    if (!complete && delta.value().isEmpty()) {
        // Nothing to append and no table to complete: the next revision IS the newest one on disk.
        composed.bytes = QByteArray(original.data(), static_cast<qsizetype>(original.size()));
    } else {
        auto update = complete ? cos::IncrementalWriter::appendComplete(original, sink.table(), delta.value())
                               : cos::IncrementalWriter::append(original, sink.table(), delta.value());
        if (!update)
            return update.error();
        composed.bytes.reserve(static_cast<qsizetype>(original.size() + update.value().bytes.size()));
        composed.bytes.append(original.data(), static_cast<qsizetype>(original.size()));
        composed.bytes.append(update.value().bytes.data(), static_cast<qsizetype>(update.value().bytes.size()));
        composed.objectsWritten = update.value().objectsWritten;
    }

    std::lock_guard<std::mutex> lock(faultMutex());
    if (fault())
        fault()(composed.bytes);
    return composed;
}

core::Status RevisionWriter::verify(std::string_view original, std::string_view written, const MutationSinkImpl& sink)
{
    // The prefix property, on bytes: every byte of the revision the update was computed against.
    if (written.size() < original.size() || std::memcmp(written.data(), original.data(), original.size()) != 0)
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("prefix"));

    // It opens through its OWN structures, with nothing that would force the next save to Save-As --
    // which for a repaired document's complete section is the whole point of writing one (D3-6).
    auto opened = cos::openWithRepair(written);
    if (!opened)
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("parse"));
    if (!opened.value().usedOriginalXref || opened.value().report.requiresSaveAs())
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("repair"));

    // Every page reads back as the model says -- which covers the changed ones and, because the walk
    // is over every page, catches an update that disturbed one it should not have touched.
    auto walk = PageOps::walk(opened.value().table);
    if (!walk)
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("pages"));
    if (static_cast<int>(walk.value().pages.size()) != sink.pageCount())
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("pageCount"));
    for (int index = 0; index < sink.pageCount(); ++index) {
        if (walk.value().pages[static_cast<std::size_t>(index)].effectiveRotate() != sink.desiredRotation(index)) {
            return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed",
                             QStringLiteral("rotation:%1").arg(index + 1));
        }
    }
    return core::Status{};
}

core::Status RevisionWriter::write(MutationSinkImpl& sink, bool complete, const QString& temporaryPath)
{
    auto composed = compose(sink, complete);
    if (!composed)
        return composed.error();

    {
        QFile file(temporaryPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return saveError(core::ErrorCode::IoFailure, "error.save.writeFailed", temporaryPath);
        const qint64 written = file.write(composed.value().bytes);
        file.close();
        if (written != composed.value().bytes.size() || file.error() != QFileDevice::NoError) {
            removeQuietly(temporaryPath);
            return saveError(core::ErrorCode::IoFailure, "error.save.writeFailed", temporaryPath);
        }
    }

    // Read back from the PATH: the claim is about the file on disk, not the buffer we meant to write.
    auto onDisk = readFile(temporaryPath);
    if (!onDisk) {
        removeQuietly(temporaryPath);
        return onDisk.error();
    }
    const core::Status verified = verify(sink.bytes(), viewOf(onDisk.value()), sink);
    if (!verified) {
        removeQuietly(temporaryPath);
        return verified;
    }
    return core::Status{};
}

core::Result<AdoptedRevision> RevisionWriter::adopt(MutationSinkImpl& sink, const QString& path)
{
    auto onDisk = readFile(path);
    if (!onDisk)
        return onDisk.error();

    AdoptedRevision adopted;
    adopted.bytes = std::move(onDisk).value();
    auto opened = cos::openWithRepair(viewOf(adopted.bytes));
    if (!opened)
        return opened.error();
    adopted.report = opened.value().report;

    // QByteArray's storage does not move when the object is moved or returned, so the views the sink
    // takes here stay valid in the caller's copy.
    const core::Status moved = sink.adopt(viewOf(adopted.bytes));
    if (!moved)
        return moved.error();
    return adopted;
}

void RevisionWriter::setComposeFaultForTesting(ComposeFault newFault)
{
    std::lock_guard<std::mutex> lock(faultMutex());
    fault() = std::move(newFault);
}

} // namespace rawpage::pdf::ops
