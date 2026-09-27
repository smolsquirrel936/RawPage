// platform/win/WinDurableFiles -- `platform::IDurableFiles` on Windows. Stage 3 T7, decision D3-4
// (`Stage3-Mutation-Plan.md` §3); the replace REBUILT on 2026-09-27 (`Stage4-ViewingSurface-Testing-Guide.md`
// §8, I-31).
//
// -------------------------------------------------------------------------------------------
// THE REPLACE: CARRY THE IDENTITY, THEN ONE ATOMIC RENAME -- AND WHY NOT `ReplaceFileW`
// -------------------------------------------------------------------------------------------
// A save must keep what a plain rename throws away -- the target's creation time, its DACL and its
// alternate data streams, `Zone.Identifier` (which marks a downloaded file) among them -- because a
// rename gives the result the REPLACEMENT's identity, which is exactly what `QSaveFile` does and why
// it is not used (D3-4).
//
// Stage 3 T7 got that from `ReplaceFileW`, with a backup name so that every documented failure left
// both files findable. **That was wrong in a way no failure code can report**: `ReplaceFileW` is two
// renames made inside the calling process -- the target to the backup name, then the replacement to
// the target's -- and a process that DIES between them (a kill, a crash, a power loss) leaves NO FILE
// AT THE TARGET'S PATH, the document under `<name>.rawpage-replace-<uuid>.tmp` and the new revision
// under its temporary name. Measured on 2026-09-27 by guide A-69 (`tst_recoverykill`), once its kills
// stopped being phase-locked to the timer tick (I-30): 5 of 36 runs of twenty kills each.
//
// So the order is now: (1) carry the identity ONTO THE REPLACEMENT while the target is untouched --
// its named streams copied byte for byte and flushed, its DACL with its protection from inheritance,
// its creation time and the attributes in `kCarriedAttributes` -- and (2) make the ONE change to the
// target, a rename of the replacement over it through `SetFileInformationByHandle`, which NTFS makes
// atomically: at every instant the target's name is the old file or the new one. A death during (1)
// leaves the target as it was; after (2) it is the new revision.
//
// THE RENAME. `FileRenameInfoEx` with POSIX semantics first: it is the only rename that succeeds past
// a reader holding the target with `FILE_SHARE_DELETE` (the reader keeps the old bytes, as it did
// under `ReplaceFileW`). Windows before 10 1809, and a volume without POSIX renames, refuse it as
// unsupported, and the classic `FileRenameInfo` with `ReplaceIfExists` follows -- as atomic, but
// refused by ANY open handle on the target, which is then reported as in use (measured 2026-09-27:
// over a holder without delete sharing the POSIX rename reports 32, the classic one 5, so a classic
// access-denied is disambiguated by asking the target for exclusive access).
// `forceClassicRenameForTesting` exists because this machine never takes the fallback on its own.
//
// WHAT IS NOT CARRIED, AND WAS BY `ReplaceFileW`: the 8.3 short name, the object identifier (link
// tracking), and compression and encryption, none of which a PDF editor's user can see or set through
// RawPage. The OWNER is not carried either -- setting another owner needs a privilege a user does not
// have -- so a saved document is owned by whoever saved it, as a new file is.
//
// A VOLUME WITHOUT STREAMS OR SECURITY (FAT, exFAT) has nothing to carry: `unsupportedByVolume()`
// reads that answer as "nothing here" rather than as a failed save.
//
// -------------------------------------------------------------------------------------------
// WHEN THE TARGET DOES NOT EXIST
// -------------------------------------------------------------------------------------------
// `MoveFileExW(MOVEFILE_WRITE_THROUGH)`, and deliberately WITHOUT `MOVEFILE_REPLACE_EXISTING` or
// `MOVEFILE_COPY_ALLOWED`: a file that appears at the target between the check and the move is not
// overwritten, and a move that would have to copy across volumes is refused rather than made
// non-atomic. The caller puts the replacement in the target's directory (D3-4).
//
// A READ-ONLY TARGET IS REFUSED UP FRONT (`error.file.readOnly`), before anything is carried, so the
// refusal SAYS read-only: access denied is also what a permissions problem reports, and the two need
// different answers from the user (T8, manual case M-08). A target held open without
// `FILE_SHARE_DELETE` is `error.file.inUse`.
//
// FOUR `layer-lint` WAIVERS, one per call spelling, each behind one helper: `CreateFileW` (every open,
// for the flush, the streams and the rename's handle), `MoveFileExW`, `DeleteFileW` and
// `SetFileInformationByHandle` (the replacement's basic information and its rename) -- the last joined
// the gate's list with this change, because a rename the gate cannot see is Stage 1 I-15's shape.
// Run `layer-lint --explain` for the census.
//
// WHAT THE TESTS CANNOT REACH (`tests/platform/tst_durablefiles.cpp` says so at its cases): a DACL,
// which is not planted; a volume without streams or security; and whether a flush survives a power
// loss. What they DO reach since I-31: a kill at a random instant inside the replace (A-69), and the
// classic rename, through `forceClassicRenameForTesting`.

#ifndef RAWPAGE_PLATFORM_WIN_WINDURABLEFILES_H
#define RAWPAGE_PLATFORM_WIN_WINDURABLEFILES_H

#include "platform/platform.h"

namespace rawpage::platform::win {

class WinDurableFiles final : public IDurableFiles {
public:
    Status syncToDisk(const QString& path) override;
    Status replaceFile(const QString& target, const QString& replacement) override;
    Status removeFile(const QString& path) override;
    bool processExists(qint64 processId) const override;

    // TEST-ONLY: skip the POSIX rename, as Windows before 1809 or a volume without it would. Process
    // wide; a case that sets it restores it.
    static void forceClassicRenameForTesting(bool classicOnly);
};

} // namespace rawpage::platform::win

#endif // RAWPAGE_PLATFORM_WIN_WINDURABLEFILES_H
