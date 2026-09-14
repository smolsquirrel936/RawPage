// platform/win/WinDurableFiles -- `platform::IDurableFiles` on Windows. Stage 3 T7, decision D3-4
// (`Stage3-Mutation-Plan.md` §3).
//
// -------------------------------------------------------------------------------------------
// WHY `ReplaceFileW`, AND WHY WITH A BACKUP NAME
// -------------------------------------------------------------------------------------------
// `ReplaceFileW` is the call that keeps what a rename throws away: the target's creation time, its
// security descriptor and its alternate data streams -- `Zone.Identifier`, which marks a downloaded
// file, among them. A `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` gives the result the REPLACEMENT's
// identity instead, which is exactly what `QSaveFile` does and why it is not used (D3-4).
//
// It is called WITH a backup file name, although nothing wants the backup, because of what its
// failure codes mean without one. Microsoft documents `ERROR_UNABLE_TO_MOVE_REPLACEMENT` as: "if
// lpBackupFileName was specified, the replaced and replacement files retain their original file
// names. Otherwise, the replaced file no longer exists" -- that is, without a backup name one failure
// DELETES THE USER'S DOCUMENT. With one, every documented failure leaves both files findable:
//   * ERROR_UNABLE_TO_REMOVE_REPLACED    -- nothing moved;
//   * ERROR_UNABLE_TO_MOVE_REPLACEMENT   -- both keep their names;
//   * ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 -- the target was moved to the backup name, and this class
//     moves it back before reporting. If even that fails, the error names where the original is
//     (`error.file.originalMoved`) rather than claiming nothing happened.
// On success the backup -- which holds the OLD content -- is deleted. A backup that cannot be deleted
// is logged and the replace still succeeds: the target is right, and a stray file beside it is not a
// failed save.
//
// -------------------------------------------------------------------------------------------
// WHEN THE TARGET DOES NOT EXIST
// -------------------------------------------------------------------------------------------
// `MoveFileExW(MOVEFILE_WRITE_THROUGH)`, and deliberately WITHOUT `MOVEFILE_REPLACE_EXISTING` or
// `MOVEFILE_COPY_ALLOWED`: a file that appears at the target between the check and the move is not
// overwritten, and a move that would have to copy across volumes is refused rather than made
// non-atomic. The caller puts the replacement in the target's directory (D3-4).
//
// A READ-ONLY TARGET IS REFUSED UP FRONT (`error.file.readOnly`), before `ReplaceFileW` is asked. Not
// for safety -- measured at T7, `ReplaceFileW` refuses a read-only target by itself, with
// `ERROR_ACCESS_DENIED`, and leaves both files -- but so the refusal SAYS read-only: access denied is
// also what a permissions problem reports, and the two need different answers from the user (T8,
// manual case M-08). A target open without `FILE_SHARE_DELETE` fails with `ERROR_SHARING_VIOLATION`
// (also measured), which is `error.file.inUse`.
//
// FOUR `layer-lint` WAIVERS, one per call spelling: `CreateFileW` (the flush opens for write),
// `ReplaceFileW`, `MoveFileExW` (both moves go through one helper) and `DeleteFileW` (the backup).
// Run `layer-lint --explain` for the census.
//
// WHAT THE TESTS CANNOT REACH (`tests/platform/tst_durablefiles.cpp` says so at its cases):
// `ERROR_UNABLE_TO_MOVE_REPLACEMENT_2` and a backup that cannot be deleted, which need a filesystem
// to fail between two steps of one call; the security descriptor, which is not planted; and whether
// a flush survives a power loss.

#ifndef RAWPAGE_PLATFORM_WIN_WINDURABLEFILES_H
#define RAWPAGE_PLATFORM_WIN_WINDURABLEFILES_H

#include "platform/platform.h"

namespace rawpage::platform::win {

class WinDurableFiles final : public IDurableFiles {
public:
    Status syncToDisk(const QString& path) override;
    Status replaceFile(const QString& target, const QString& replacement) override;
};

} // namespace rawpage::platform::win

#endif // RAWPAGE_PLATFORM_WIN_WINDURABLEFILES_H
