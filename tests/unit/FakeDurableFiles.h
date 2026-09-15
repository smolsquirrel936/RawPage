// tests/unit/FakeDurableFiles.h -- a `platform::IDurableFiles` for the headless suite. Stage 3 T8.
//
// WHY IT EXISTS: `rawpage_tests` may not link `rawpage_platform` (D2-2), and `core/doc/SaveCoordinator`
// needs something that flushes, replaces and removes. This one does each with Qt on real files in a
// temporary directory -- so the bytes a round trip reads back are real bytes on disk -- and RECORDS
// every call, so a case can assert that `replaceFile` was never reached.
//
// WHAT IT IS NOT, and every suite using it says so at its assertion (convention 19): the real replace.
// `replaceFile` here removes the target and renames the replacement over it, which is neither atomic nor
// stream-preserving; `syncToDisk` checks the file exists and flushes nothing. The real ones are
// `platform/win/WinDurableFiles`, measured by `tests/platform/tst_durablefiles.cpp` and used by
// `tests/render/tst_saveengine.cpp`.

#ifndef RAWPAGE_TESTS_UNIT_FAKEDURABLEFILES_H
#define RAWPAGE_TESTS_UNIT_FAKEDURABLEFILES_H

#include "platform/platform.h"

#include <QFile>
#include <QFileInfo>

#include <set>

namespace rawpage::tests {

class FakeDurableFiles final : public platform::IDurableFiles {
public:
    core::Status syncToDisk(const QString& path) override
    {
        ++syncCalls;
        if (failSync)
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.file.syncFailed"), path};
        if (!QFileInfo::exists(path))
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.file.notFound"), path};
        return core::Status{};
    }

    core::Status replaceFile(const QString& target, const QString& replacement) override
    {
        ++replaceCalls;
        if (failReplace)
            return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.file.inUse"), target};
        if (!QFileInfo::exists(replacement))
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.file.notFound"), replacement};
        if (QFileInfo::exists(target) && !QFile::remove(target))
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.file.replaceFailed"), target};
        if (!QFile::rename(replacement, target))
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.file.replaceFailed"), target};
        return core::Status{};
    }

    core::Status removeFile(const QString& path) override
    {
        ++removeCalls;
        if (QFileInfo::exists(path) && !QFile::remove(path))
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.file.removeFailed"), path};
        return core::Status{};
    }

    bool processExists(qint64 processId) const override { return liveProcesses.count(processId) > 0; }

    int syncCalls = 0;
    int replaceCalls = 0;
    int removeCalls = 0;
    bool failSync = false;
    bool failReplace = false;
    std::set<qint64> liveProcesses;
};

} // namespace rawpage::tests

#endif // RAWPAGE_TESTS_UNIT_FAKEDURABLEFILES_H
