// SAVE COST -- Stage 3 T8, guide case A-61 (`Stage3-Mutation-Testing-Guide.md` §5.9), decisions D3-4 and
// D3-16 (`Stage3-Mutation-Plan.md` §3).
//
// D3-4 chose a save that writes EVERY byte of the file -- the opened bytes copied, then the update -- to a
// temporary file, re-reads it, flushes it and replaces the target. That costs I/O proportional to the FILE,
// not to the edit, and the plan records the cost as something to measure rather than to assume. D3-16
// adds a second whole-file cost: before a save, the file on disk is hashed to prove it is still the one
// that was opened. This binary reports both.
//
// WHAT IS TIMED, through the SHIPPED assembly -- `pdf::DocumentBackendImpl`, `CommandJournal`,
// `SaveCoordinator` and the real `platform::win::WinDurableFiles`:
//
//   * **open hash** -- `DocumentId::of()` over the bytes the backend loaded: SHA-256 of the whole file.
//   * **save** -- one `SaveCoordinator::save()` after one rotation: the identity check (a read and a hash
//     of the file on disk), the composed write, the re-read and verification, the flush, the replace and
//     the adopt. Each run rotates a different page, so every save appends a real update.
//
// Two files: `corpus/ordinary/pages-900.pdf`, and a GENERATED 200 MB file -- `text-a4.pdf` with one
// incremental update carrying an unreferenced 200 MB stream -- written to a temporary directory and
// removed afterwards. The 200 MB file is the size D3-4's *Revisit* column asks about.
//
// IT REPORTS AND NEVER FAILS ON SPEED. The exit code is about whether the measurement was POSSIBLE: a
// file that would not open, save or verify is exit 1; a slow figure is exit 0 and D3-4's question, not
// a failure. The figures are for this machine's disk, and a network share or a USB stick would differ.

#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/DocumentId.h"
#include "core/doc/SaveCoordinator.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefTable.h"
#include "platform/win/WinDurableFiles.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;

namespace {

constexpr int kRuns = 5;

struct Figures {
    double median = 0;
    double least = 0;
    double most = 0;
};

Figures summarize(std::vector<double> samples)
{
    std::sort(samples.begin(), samples.end());
    return Figures{samples[samples.size() / 2], samples.front(), samples.back()};
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

// `text-a4.pdf` plus one update holding an unreferenced stream of `payloadBytes`. A real PDF that opens
// with no repair, whose size is almost all one object the page tree never reaches.
bool generateLargeFile(const QString& source, const QString& target, std::size_t payloadBytes)
{
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const QByteArray original = in.readAll();
    const std::string_view view(original.constData(), static_cast<std::size_t>(original.size()));
    auto table = pdf::cos::XrefTable::load(view);
    if (!table)
        return false;
    pdf::cos::ObjectDelta delta(table.value());
    std::string payload(payloadBytes, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>('A' + (i * 7919) % 26);   // incompressible enough to be honest bytes
    delta.add(pdf::cos::CosObject::makeStream(std::vector<pdf::cos::CosObject::Entry>{}, pdf::cos::CosStreamData{}),
              std::move(payload));
    auto update = pdf::cos::IncrementalWriter::append(view, table.value(), delta);
    if (!update)
        return false;
    QByteArray bytes = original;
    bytes.append(update.value().bytes.data(), static_cast<qsizetype>(update.value().bytes.size()));
    return writeFile(target, bytes);
}

bool measure(const char* label, const QString& path)
{
    pdf::DocumentBackendImpl backend;
    platform::win::WinDurableFiles durableFiles;
    auto handle = backend.open(path, nullptr);
    if (!handle || backend.mutationSink(handle.value()) == nullptr) {
        std::printf("%s: could not be opened for editing -- nothing measured\n", label);
        return false;
    }
    const qint64 size = backend.documentId(handle.value())->size;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll();
    file.close();
    std::vector<double> hashMs;
    for (int run = 0; run < kRuns; ++run) {
        QElapsedTimer timer;
        timer.start();
        const core::DocumentId id = core::DocumentId::of(path, 0, bytes);
        hashMs.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
        (void)id;
    }

    core::Document document;
    if (!document.hydrate(backend, handle.value()))
        return false;
    core::CommandJournal journal(std::move(document), backend.mutationSink(handle.value()));
    core::SaveCoordinator coordinator(backend, &durableFiles);
    const int pages = journal.document().pages().count();

    std::vector<double> saveMs;
    for (int run = 0; run < kRuns; ++run) {
        if (!journal.run(std::make_unique<core::RotatePageCommand>(run % pages, 90))) {
            std::printf("%s: rotation refused -- nothing measured\n", label);
            return false;
        }
        QElapsedTimer timer;
        timer.start();
        auto saved = coordinator.save(journal, path);
        const double elapsed = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        if (!saved || saved.value().kind != core::SaveOutcome::Kind::Saved) {
            std::printf("%s: save %d failed (%s) -- nothing measured\n", label, run + 1,
                        saved ? saved.value().reasonKey.data() : saved.error().messageKey.data());
            return false;
        }
        saveMs.push_back(elapsed);
    }

    const Figures hash = summarize(hashMs);
    const Figures save = summarize(saveMs);
    std::printf("%s: %.1f MB, %d pages\n", label, static_cast<double>(size) / (1024.0 * 1024.0), pages);
    std::printf("  open hash (SHA-256 of the file)   median %9.2f ms   min %9.2f   max %9.2f   (%d runs)\n",
                hash.median, hash.least, hash.most, kRuns);
    std::printf("  save after one rotation           median %9.2f ms   min %9.2f   max %9.2f   (%d runs)\n",
                save.median, save.least, save.most, kRuns);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    std::printf("rawpage_bench_save -- Stage 3 T8, guide case A-61. Reports; never fails on speed.\n");

    QTemporaryDir dir;
    if (!dir.isValid())
        return 1;
    const QString corpus = QStringLiteral(RAWPAGE_CORPUS_DIR);

    const QString pages900 = dir.filePath(QStringLiteral("pages-900.pdf"));
    if (!QFile::copy(corpus + QStringLiteral("/ordinary/pages-900.pdf"), pages900))
        return 1;
    QFile::setPermissions(pages900, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    const QString large = dir.filePath(QStringLiteral("generated-200mb.pdf"));
    QElapsedTimer generation;
    generation.start();
    if (!generateLargeFile(corpus + QStringLiteral("/ordinary/text-a4.pdf"), large, 200u * 1024u * 1024u)) {
        std::printf("could not generate the 200 MB file -- nothing measured\n");
        return 1;
    }
    std::printf("(generated the 200 MB file in %.0f ms)\n", static_cast<double>(generation.elapsed()));

    const bool ok = measure("corpus/ordinary/pages-900.pdf", pages900) && measure("generated 200 MB file", large);
    return ok ? 0 : 1;
}
