// Last-resort crash capture and the symbolization path behind it -- decision D1-13 in
// Stage1-AppShell-Plan.md 3.
//
// WHY THIS EXISTS AT STAGE 1 AND NOT AT STAGE 15:
// this toolchain emits DWARF and never a .pdb (Tech-Stack-Plan.md 6.3, risk R7), so there is
// no symbol server to lean on: a field minidump is symbolized against an archived unstripped
// binary, matched by build id. A crash-handling path that has never symbolized a real dump is
// not a crash-handling path, and the first real dump arrives during Stage 2's rendering work
// -- long before packaging would otherwise force the question.
//
// WHAT A BUILD ID IS ON THIS TOOLCHAIN:
// -Wl,--build-id is an ELF spelling; on PE, GNU ld honours the flag by emitting a CodeView
// RSDS record into a `.buildid` section -- a 16-byte GUID plus a 4-byte age -- which is the
// same record MiniDumpWriteDump copies into the dump's module list. So the dump identifies
// its own binary, and scripts/symbolize.ps1 needs no separate manifest lookup to find the
// right archived exe. Measured on the pinned toolchain 2026-08-23; there is NO
// .note.gnu.build-id section in the output and looking for one finds nothing.
//
// EVERYTHING IN THE FILTER RUNS IN A PROCESS THAT IS ALREADY BROKEN.
// The heap may be corrupt, the stack may be nearly exhausted, and another thread may hold
// any lock. So the paths are computed and the directory created at install() time, the
// dbghelp entry point is resolved lazily but stored, and the filter itself allocates nothing
// it can avoid.

#ifndef RAWPAGE_APP_CRASHHANDLER_H
#define RAWPAGE_APP_CRASHHANDLER_H

#include <QString>

#include <functional>

namespace rawpage::app {

// Called on the crash path BEFORE the dump is written, so that whatever must reach disk to
// make recovery possible gets its chance while the process still exists.
//
// Stage 3 wires RecoveryJournal::flushAll() here. Until then install() supplies a no-op, and
// says so in the log and in the dump's sidecar -- an unwired hook that is silent is
// indistinguishable from one that ran and did nothing (testing guide A-66).
using LastRites = std::function<void()>;

class CrashHandler {
public:
    // Installs the unhandled-exception filter. Call once, as early in main() as possible and
    // before any Qt object exists: an exception thrown while Qt is initialising is exactly
    // the one worth catching.
    static void install(LastRites lastRites = {});

    // %LOCALAPPDATA%\RawPage\crashes\ -- created by install().
    static QString crashDirectory();

    // The CodeView GUID+age of the running executable, 33 uppercase hex characters, or an
    // empty string if the binary carries no build id (which is itself a finding: it means
    // -Wl,--build-id was lost and no dump from this build can be symbolized).
    static QString buildId();

    // Deliberate access violation for --crash-test. Debug configurations only; the release
    // build has no path to it at all, because a shipping binary that will crash on request
    // is a denial-of-service switch.
    [[noreturn]] static void forceCrashForTesting();
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_CRASHHANDLER_H
