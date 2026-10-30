// tests/platform/killprobe/PageSteps -- the page commands `rawpage_killprobe pages` runs, one per step, and that
// `tst_recoverykill` runs again to know what any number of them left. Stage 7 T2, guide case A-28
// (`Stage7-Assembly-Testing-Guide.md` §5.4), decision D7-24.
//
// DETERMINISTIC BY CONSTRUCTION, as `AnnotationSteps.h` is and for its reason: the harness's expectation is a
// second run of the same steps, so every page set and every KEY below is a function of the step's number alone
// -- a delete's and a duplicate's key is the step number in 32 hex digits, not `core::newCommandKey()`.
//
// THE CYCLE, four steps over `assembly/refs-every-kind.pdf` (twelve pages, every reference kind); with
// c = (k - 1) % 4:
//   c == 0   MovePages       the first page to the end
//   c == 1   DuplicatePages  page k % count, its copy right after it
//   c == 2   DeletePages     page 1 -- which, in turn, deletes pages that bookmarks, links, named destinations,
//                            structure elements and beads point at, and the copies the duplicates made
//   c == 3   MovePages       the last two pages to the front
// so the page count stays twelve, every page goes everywhere, and every reference repair runs.

#ifndef RAWPAGE_TESTS_PLATFORM_KILLPROBE_PAGESTEPS_H
#define RAWPAGE_TESTS_PLATFORM_KILLPROBE_PAGESTEPS_H

#include "core/cmd/Command.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"

#include <QString>

#include <memory>
#include <vector>

namespace rawpage::tests::killsteps {

inline constexpr int kPageCycle = 4;

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
    default:
        return std::make_unique<core::MovePagesCommand>(std::vector<int>{count - 2, count - 1}, 0);
    }
}

} // namespace rawpage::tests::killsteps

#endif // RAWPAGE_TESTS_PLATFORM_KILLPROBE_PAGESTEPS_H
