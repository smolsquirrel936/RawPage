// tests/platform/killprobe/PageSteps -- the page commands `rawpage_killprobe pages` runs, one per step, and that
// `tst_recoverykill` runs again to know what any number of them left. Stage 7 T2, guide case A-28
// (`Stage7-Assembly-Testing-Guide.md` §5.4), decision D7-24.
//
// DETERMINISTIC BY CONSTRUCTION, as `AnnotationSteps.h` is and for its reason: the harness's expectation is a
// second run of the same steps, so every page set and every KEY below is a function of the step's number alone
// -- a delete's and a duplicate's key is the step number in 32 hex digits, not `core::newCommandKey()`.
//
// THE CYCLE, five steps over `assembly/refs-every-kind.pdf` (twelve pages, every reference kind); with
// c = (k - 1) % 5:
//   c == 0   MovePages       the first page to the end
//   c == 1   DuplicatePages  page k % count, its copy right after it
//   c == 2   DeletePages     page 1 -- which, in turn, deletes pages that bookmarks, links, named destinations,
//                            structure elements and beads point at, and the copies the duplicates made
//   c == 3   MovePages       the last two pages to the front
//   c == 4   InsertPages     Stage 7 T5 (guide A-28, A-44): `pages-900.pdf`'s first 300 pages, at the middle -- copied
//                            from a TEMPORARY COPY of the file, deleted as soon as it is copied, so a replay has only
//                            what the journal carries (D7-6); the payload is over 64 KiB, so its line names a SIDE FILE
//                            (D6-23)
// so every page goes everywhere, and every reference repair runs.

#ifndef RAWPAGE_TESTS_PLATFORM_KILLPROBE_PAGESTEPS_H
#define RAWPAGE_TESTS_PLATFORM_KILLPROBE_PAGESTEPS_H

#include "core/cmd/Command.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "pdf/ops/ObjectGraphCopier.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <memory>
#include <vector>

namespace rawpage::tests::killsteps {

inline constexpr int kPageCycle = 5;

// The insert step's payload, made once per process (see the header). An empty payload -- the file not found -- makes
// an insert that refuses, which the harness reports as a step that did not run.
inline const core::CopiedPages& insertPayload()
{
    static const core::CopiedPages payload = [] {
        QTemporaryDir scratch;
        const QString copy = scratch.filePath(QStringLiteral("source.pdf"));
        if (!QFile::copy(QStringLiteral(RAWPAGE_CORPUS_DIR "/ordinary/pages-900.pdf"), copy)) return core::CopiedPages{};
        QFile file(copy);
        if (!file.open(QIODevice::ReadOnly)) return core::CopiedPages{};
        const QByteArray bytes = file.readAll();
        file.close();
        QFile::remove(copy);   // the source is gone before the first insert runs
        std::vector<int> pages;
        for (int i = 0; i < 300; ++i) pages.push_back(i);
        auto copied = pdf::ops::ObjectGraphCopier::copyFile(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                                                            pages);
        return copied ? copied.value() : core::CopiedPages{};
    }();
    return payload;
}

inline QString pageKey(int k)
{
    return QStringLiteral("%1").arg(static_cast<qulonglong>(k), 32, 16, QLatin1Char('0'));
}

// Step `k` (1-based) of the page cycle, for `document` -- whose page count places it.
inline std::unique_ptr<core::Command> pageStep(int k, const core::Document& document)
{
    const int count = document.pages().count();
    switch ((k - 1) % kPageCycle) {
    case 0:
        return std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, count - 1);
    case 1:
        return std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{k % count}, pageKey(k));
    case 2:
        return std::make_unique<core::DeletePagesCommand>(std::vector<int>{1}, pageKey(k));
    case 4:
        return std::make_unique<core::InsertPagesCommand>(count / 2, insertPayload(), pageKey(k));
    default:
        return std::make_unique<core::MovePagesCommand>(std::vector<int>{count - 2, count - 1}, 0);
    }
}

} // namespace rawpage::tests::killsteps

#endif // RAWPAGE_TESTS_PLATFORM_KILLPROBE_PAGESTEPS_H
