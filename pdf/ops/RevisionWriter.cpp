// pdf/ops/RevisionWriter -- implementation. Stage 3 T8. See the header for the order and the seam.

#include "pdf/ops/RevisionWriter.h"

#include "pdf/cos/FullWriter.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/PageOps.h"

#include <QFile>
#include <QIODevice>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>

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

std::function<void(std::string&)>& rewriteFault()
{
    static std::function<void(std::string&)> instance;
    return instance;
}

core::Status writeFile(const QString& path, const char* data, qsizetype size)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return saveError(core::ErrorCode::IoFailure, "error.save.writeFailed", path);
    const qint64 written = file.write(data, size);
    file.close();
    if (written != size || file.error() != QFileDevice::NoError) {
        removeQuietly(path);
        return saveError(core::ErrorCode::IoFailure, "error.save.writeFailed", path);
    }
    return core::Status{};
}

// A rewritten object against the one it was written from: values equal, a stream's `/Length` aside -- the writer
// writes the data's own -- and a stream's encoded bytes equal.
bool sameObject(const cos::IndirectObject& a, std::string_view aSource, const cos::IndirectObject& b, std::string_view bSource)
{
    if (a.object.isStream() != b.object.isStream()) return false;
    if (!a.object.isStream()) return a.object == b.object;
    auto withoutLength = [](const cos::CosObject& stream) {
        std::vector<cos::CosObject::Entry> entries;
        for (const cos::CosObject::Entry& entry : stream.entries()) {
            if (entry.key != "Length") entries.push_back(entry);
        }
        return cos::CosObject::makeDictionary(std::move(entries));
    };
    return withoutLength(a.object) == withoutLength(b.object)
           && a.object.rawStreamData(aSource) == b.object.rawStreamData(bSource);
}

} // namespace

core::Result<ComposedUpdate> RevisionWriter::composeUpdate(MutationSinkImpl& sink, bool complete)
{
    auto delta = sink.delta();
    if (!delta)
        return delta.error();

    const std::string_view original = sink.bytes();
    ComposedUpdate composed;
    composed.complete = complete;

    // Whenever anything will follow the original, the delta must not let it change what the original
    // MEANS -- an object that runs to the end of the file, a number referenced and never defined
    // (Stage 3 guide issue I-12; `IncrementalWriter::protectOriginal` says how). S7 T4: from the walk the
    // sink made once for this revision.
    if (complete || !delta.value().isEmpty())
        sink.protect(delta.value());

    if (!complete && delta.value().isEmpty()) {
        // Nothing to append and no table to complete: the next revision IS the newest one on disk.
        sink.composedNothing();
        return composed;
    }
    auto update = complete ? cos::IncrementalWriter::appendComplete(original, sink.table(), delta.value())
                           : cos::IncrementalWriter::append(original, sink.table(), delta.value());
    if (!update)
        return update.error();
    sink.composed(update.value());   // S7 T4: the round ends; what was written is kept
    composed.update = QByteArray(update.value().bytes.data(), static_cast<qsizetype>(update.value().bytes.size()));
    composed.objectsWritten = std::move(update.value().objectsWritten);
    composed.objectsReused = update.value().objectsReused;
    return composed;
}

core::Result<ComposedRevision> RevisionWriter::compose(MutationSinkImpl& sink, bool complete)
{
    auto update = composeUpdate(sink, complete);
    if (!update)
        return update.error();
    const std::string_view original = sink.bytes();
    ComposedRevision composed;
    composed.complete = complete;
    composed.bytes.reserve(static_cast<qsizetype>(original.size()) + update.value().update.size());
    composed.bytes.append(original.data(), static_cast<qsizetype>(original.size()));
    composed.bytes.append(update.value().update);
    composed.objectsWritten = std::move(update.value().objectsWritten);

    std::lock_guard<std::mutex> lock(faultMutex());
    if (fault())
        fault()(composed.bytes);
    return composed;
}

RevisionWriter::ComposeFault RevisionWriter::composeFault()
{
    std::lock_guard<std::mutex> lock(faultMutex());
    return fault();
}

core::Status RevisionWriter::verifyUpdate(std::string_view original, const QByteArray& update,
                                          const MutationSinkImpl& sink, const ComposeFault& fault)
{
    // The whole file, as `compose` would hand it over -- built here, on the verifying thread, so the caller
    // never copies the original to show an edit.
    QByteArray whole;
    whole.reserve(static_cast<qsizetype>(original.size()) + update.size());
    whole.append(original.data(), static_cast<qsizetype>(original.size()));
    whole.append(update);
    if (fault) fault(whole);
    return verify(original, viewOf(whole), sink);
}

namespace {
core::Status verifyModel(cos::OpenedFile& opened, const MutationSinkImpl& sink);
} // namespace

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
    return verifyModel(opened.value(), sink);
}

core::Status RevisionWriter::verifyRewrite(std::string_view before, std::string_view written, const MutationSinkImpl& sink)
{
    auto opened = cos::openWithRepair(written);
    if (!opened)
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("parse"));
    if (!opened.value().usedOriginalXref || opened.value().report.requiresSaveAs())
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("repair"));
    // ONE revision: one section, no `/Prev` (guide §3.9).
    cos::XrefTable& table = opened.value().table;
    if (table.sections().size() != 1 || table.sections().front().trailer.has("Prev"))
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("revisions"));

    auto composed = cos::openWithRepair(before);
    if (!composed)
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("composed"));
    cos::XrefTable& source = composed.value().table;
    // What it holds is what it reaches, and what it reaches is what the composed file reached: nothing unreachable
    // kept, nothing reachable lost.
    std::vector<long long> held;
    for (const auto& [number, entry] : table.entries()) {
        if (number > 0 && entry.type != cos::XrefEntryType::Free) held.push_back(number);
    }
    if (cos::FullWriter::reachable(table) != held)
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("unreachable"));
    if (cos::FullWriter::reachable(source) != held)
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("reachable"));
    // Each object is the one it was written from, at the same number and generation.
    for (const long long number : held) {
        auto got = table.fetch(number);
        auto want = source.fetch(number);
        if (!got || !want || got.value().generation != want.value().generation
            || !sameObject(got.value(), table.source(), want.value(), source.source()))
            return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("object:%1").arg(number));
    }
    return verifyModel(opened.value(), sink);
}

namespace {
core::Status verifyModel(cos::OpenedFile& opened, const MutationSinkImpl& sink)
{

    // Every page reads back as the model says -- which covers the changed ones and, because the walk
    // is over every page, catches an update that disturbed one it should not have touched.
    auto walk = PageOps::walk(opened.table);
    if (!walk)
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("pages"));
    if (static_cast<int>(walk.value().pages.size()) != sink.pageCount())
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("pageCount"));
    // Stage 7 T2: every page is the OBJECT the desired order puts there -- a page the file held, by its
    // number; a new page, a number no other page has -- so a move that landed one page short, or a copy
    // written over its source, fails here rather than in a reader.
    {
        std::set<long long> listed;
        for (int index = 0; index < sink.pageCount(); ++index) {
            const long long got = walk.value().pages[static_cast<std::size_t>(index)].number;
            const long long want = sink.pages().pages[static_cast<std::size_t>(index)].number;
            if (!listed.insert(got).second || (!sink.isNewPage(index) && got != want))
                return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("pageOrder:%1").arg(index + 1));
        }
        for (int index = 0; index < sink.pageCount(); ++index) {
            if (!sink.isNewPage(index)) continue;
            const long long got = walk.value().pages[static_cast<std::size_t>(index)].number;
            const auto& desired = sink.pages().pages;
            if (std::any_of(desired.begin(), desired.end(), [got](const PageEntry& p) { return p.number == got; }))
                return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("pageOrder:%1").arg(index + 1));
        }
    }
    for (int index = 0; index < sink.pageCount(); ++index) {
        if (walk.value().pages[static_cast<std::size_t>(index)].effectiveRotate() != sink.desiredRotation(index)) {
            return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed",
                             QStringLiteral("rotation:%1").arg(index + 1));
        }
    }

    // Every page's /Annots reads back as the sink composed it (Stage 6 T3) -- a created annotation as a
    // dictionary with its /NM, its /Subtype, its page and a well-formed appearance, every other entry
    // the one that was listed -- and, the walk being over every page, a page nobody changed reads back
    // unchanged. Guide case A-30.
    auto annotations = sink.reconcile(opened.table, walk.value(), true);
    if (!annotations)
        return annotations.error();
    // And the name tree is the one the sink composed (Stage 6 T11): every pair it kept the same pair, every
    // attached file a specification whose stream is the bytes compressed when it was attached.
    auto files = sink.reconcileFiles(opened.table, true);
    if (!files)
        return files.error();
    return core::Status{};
}
} // namespace

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

core::Status RevisionWriter::rewrite(MutationSinkImpl& sink, bool complete, const QString& temporaryPath, std::string& before)
{
    before.clear();
    auto composed = compose(sink, complete);
    if (!composed)
        return composed.error();
    std::string whole(composed.value().bytes.constData(), static_cast<std::size_t>(composed.value().bytes.size()));
    auto opened = cos::openWithRepair(whole);
    if (!opened)
        return saveError(core::ErrorCode::Corrupted, "error.save.verifyFailed", QStringLiteral("composed"));
    auto rewritten = cos::FullWriter::rewrite(whole, opened.value().table);
    if (!rewritten)
        return rewritten.error();
    {
        std::lock_guard<std::mutex> lock(faultMutex());
        if (rewriteFault()) rewriteFault()(rewritten.value().bytes);
    }
    const std::string& bytes = rewritten.value().bytes;
    if (core::Status written = writeFile(temporaryPath, bytes.data(), static_cast<qsizetype>(bytes.size())); !written)
        return written;

    // Read back from the PATH, as `write()` does.
    auto onDisk = readFile(temporaryPath);
    if (!onDisk) {
        removeQuietly(temporaryPath);
        return onDisk.error();
    }
    const core::Status verified = verifyRewrite(whole, viewOf(onDisk.value()), sink);
    if (!verified) {
        removeQuietly(temporaryPath);
        return verified;
    }
    before = std::move(whole);
    return core::Status{};
}

core::Result<AdoptedRevision> RevisionWriter::adoptRewrite(MutationSinkImpl& sink, const QString& path, std::string before)
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
    const core::Status moved = sink.adoptRewrite(viewOf(adopted.bytes), std::move(before));
    if (!moved)
        return moved.error();
    return adopted;
}

void RevisionWriter::setRewriteFaultForTesting(std::function<void(std::string&)> fault)
{
    std::lock_guard<std::mutex> lock(faultMutex());
    rewriteFault() = std::move(fault);
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
