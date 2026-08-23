// Stage 1 T5 -- core/settings/RecentFiles. Guide case A-46 (Stage1-AppShell-Testing-Guide.md 5.5).
//
// The four behaviours worth pinning, because each is a different way a recent list becomes annoying
// rather than broken -- and an annoying recent list is one nobody reports:
//
//   * `documents/recentCount` bounds the HISTORY and nothing else.
//   * A pinned entry survives eviction, and does not spend a history slot.
//   * `clear()` clears the history and keeps the pins; `clearIncludingPins()` is the other one.
//   * Re-noting a path moves it rather than duplicating it, whatever separators it arrives with.

#include <catch2/catch_test_macros.hpp>

#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"

#include <QStringList>
#include <QTemporaryDir>

using namespace rawpage::core;

namespace {

QStringList pathsOf(const RecentFiles& recent)
{
    QStringList paths;
    for (const RecentFiles::Entry& entry : recent.entries()) paths << entry.path;
    return paths;
}

} // namespace

TEST_CASE("a fresh list is empty and takes its capacity from the schema", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    RecentFiles recent(settings);

    CHECK(recent.entries().empty());
    CHECK(recent.capacity() == 10);
}

TEST_CASE("noting puts the most recent first and never duplicates", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    RecentFiles recent(settings);

    recent.note(QStringLiteral("C:/docs/a.pdf"));
    recent.note(QStringLiteral("C:/docs/b.pdf"));
    recent.note(QStringLiteral("C:/docs/a.pdf"));

    const QStringList paths = pathsOf(recent);
    REQUIRE(paths.size() == 2);
    CHECK(paths.at(0) == QStringLiteral("C:/docs/a.pdf"));
    CHECK(paths.at(1) == QStringLiteral("C:/docs/b.pdf"));
}

TEST_CASE("the same document reached two ways is one entry", "[recentfiles]")
{
    // Windows hands us both separators depending on whether the path came from a dialog, from the
    // shell, or from a command line. Three spellings of one file in a ten-entry list is most of the
    // list gone.
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    RecentFiles recent(settings);

    recent.note(QStringLiteral("C:/docs/a.pdf"));
    recent.note(QStringLiteral("C:\\docs\\a.pdf"));
    recent.note(QStringLiteral("C:/docs/./a.pdf"));

    // The count alone would be satisfied by a collapse onto the WRONG spelling -- and the stored
    // spelling is the one that gets displayed and re-opened, so it is the half worth pinning.
    const QStringList paths = pathsOf(recent);
    REQUIRE(paths.size() == 1);
    CHECK(paths.at(0) == QStringLiteral("C:/docs/a.pdf"));

    // ISSUE I-17. This case ran green on Windows from T5 and had never run on Linux until T9's CI
    // reached it, where it failed 2 == 1: QDir::cleanPath converts PLATFORM-NATIVE separators, so a
    // backslash is a separator on Windows and an ordinary filename character under POSIX. The fix is
    // in RecentFiles' own canonical(), which now normalizes by hand rather than leaving the question
    // to the platform, so the two builds share one dedup rule.
    //
    // This case is deliberately NOT #ifdef'd to Windows. Making it Windows-only would have been the
    // smaller diff and would have left the portable-asan job exercising a different rule from the
    // shipping build -- and a sanitizer finding there has to be reproducible in x64-debug before it
    // counts as fixed, which a divergent rule quietly makes impossible.
}

TEST_CASE("the history is bounded by documents/recentCount", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    settings.set(keys::documents::recentCount, 3);
    RecentFiles recent(settings);

    for (int i = 0; i < 6; ++i) recent.note(QStringLiteral("C:/docs/%1.pdf").arg(i));

    const QStringList paths = pathsOf(recent);
    REQUIRE(paths.size() == 3);
    CHECK(paths.at(0) == QStringLiteral("C:/docs/5.pdf"));
    CHECK(paths.at(2) == QStringLiteral("C:/docs/3.pdf"));
}

TEST_CASE("a count of zero keeps no history, which is a setting rather than a bug", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    settings.set(keys::documents::recentCount, 0);
    RecentFiles recent(settings);

    recent.note(QStringLiteral("C:/docs/a.pdf"));
    CHECK(recent.entries().empty());

    // ...and a pin is still honoured, because a pin is not history.
    REQUIRE(recent.pin(QStringLiteral("C:/docs/keep.pdf")));
    REQUIRE(recent.entries().size() == 1);
    CHECK(recent.entries().front().pinned);
}

TEST_CASE("a pinned entry survives eviction and does not spend a history slot", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    settings.set(keys::documents::recentCount, 2);
    RecentFiles recent(settings);

    recent.note(QStringLiteral("C:/docs/keep.pdf"));
    REQUIRE(recent.pin(QStringLiteral("C:/docs/keep.pdf")));

    for (int i = 0; i < 5; ++i) recent.note(QStringLiteral("C:/docs/%1.pdf").arg(i));

    const std::vector<RecentFiles::Entry> entries = recent.entries();
    REQUIRE(entries.size() == 3);                  // one pin plus a two-entry history
    CHECK(entries.front().path == QStringLiteral("C:/docs/keep.pdf"));
    CHECK(entries.front().pinned);
    CHECK(recent.isPinned(QStringLiteral("C:/docs/keep.pdf")));
}

TEST_CASE("noting an already-pinned path leaves it pinned", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    RecentFiles recent(settings);

    REQUIRE(recent.pin(QStringLiteral("C:/docs/keep.pdf")));
    recent.note(QStringLiteral("C:/docs/keep.pdf"));

    REQUIRE(recent.entries().size() == 1);
    CHECK(recent.entries().front().pinned);
}

TEST_CASE("unpinning returns the entry to the history rather than dropping it", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    RecentFiles recent(settings);

    REQUIRE(recent.pin(QStringLiteral("C:/docs/keep.pdf")));
    recent.unpin(QStringLiteral("C:/docs/keep.pdf"));

    REQUIRE(recent.entries().size() == 1);
    CHECK_FALSE(recent.entries().front().pinned);
}

TEST_CASE("clear() clears the history and keeps the pins", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    RecentFiles recent(settings);

    recent.note(QStringLiteral("C:/docs/a.pdf"));
    recent.note(QStringLiteral("C:/docs/b.pdf"));
    REQUIRE(recent.pin(QStringLiteral("C:/docs/keep.pdf")));

    recent.clear();
    REQUIRE(recent.entries().size() == 1);
    CHECK(recent.entries().front().path == QStringLiteral("C:/docs/keep.pdf"));

    recent.clearIncludingPins();
    CHECK(recent.entries().empty());
}

TEST_CASE("remove takes an entry out whether it was pinned or not", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    RecentFiles recent(settings);

    recent.note(QStringLiteral("C:/docs/a.pdf"));
    REQUIRE(recent.pin(QStringLiteral("C:/docs/keep.pdf")));

    recent.remove(QStringLiteral("C:/docs/keep.pdf"));
    recent.remove(QStringLiteral("C:/docs/a.pdf"));
    CHECK(recent.entries().empty());
}

TEST_CASE("an empty path is refused rather than pinned", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    RecentFiles recent(settings);

    const Result<void> pinned = recent.pin(QString{});
    CHECK_FALSE(pinned);
    CHECK(pinned.error().code == ErrorCode::InvalidArgument);

    recent.note(QString{});
    CHECK(recent.entries().empty());
}

TEST_CASE("the list survives a restart, pins and order included", "[recentfiles]")
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/settings.ini");

    {
        Settings settings(path);
        RecentFiles recent(settings);
        recent.note(QStringLiteral("C:/docs/a.pdf"));
        recent.note(QStringLiteral("C:/docs/b.pdf"));
        REQUIRE(recent.pin(QStringLiteral("C:/docs/keep.pdf")));
        REQUIRE(settings.sync());
    }

    Settings settings(path);
    RecentFiles recent(settings);

    const std::vector<RecentFiles::Entry> entries = recent.entries();
    REQUIRE(entries.size() == 3);
    CHECK(entries.at(0).path == QStringLiteral("C:/docs/keep.pdf"));
    CHECK(entries.at(0).pinned);
    CHECK(entries.at(1).path == QStringLiteral("C:/docs/b.pdf"));
    CHECK(entries.at(2).path == QStringLiteral("C:/docs/a.pdf"));
}

TEST_CASE("lowering the count in Preferences takes effect without a restart", "[recentfiles]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    RecentFiles recent(settings);

    for (int i = 0; i < 6; ++i) recent.note(QStringLiteral("C:/docs/%1.pdf").arg(i));
    REQUIRE(recent.entries().size() == 6);

    settings.set(keys::documents::recentCount, 2);
    // The next note applies the new bound. Eviction is deliberately not a subscriber to the
    // settings change: trimming a list the moment a spinbox passes through 1 on its way to 12 would
    // destroy entries the user never meant to drop.
    recent.note(QStringLiteral("C:/docs/new.pdf"));
    CHECK(recent.entries().size() == 2);
}

TEST_CASE("a hand-edited file that both pins and lists a path yields one entry", "[recentfiles]")
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        settings.set(keys::documents::recentPinned, QStringList{QStringLiteral("C:/docs/a.pdf")});
        settings.set(keys::documents::recentList,
                     QStringList{QStringLiteral("C:/docs/a.pdf"), QStringLiteral("C:/docs/b.pdf")});
        REQUIRE(settings.sync());
    }

    Settings settings(path);
    RecentFiles recent(settings);

    const std::vector<RecentFiles::Entry> entries = recent.entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries.at(0).path == QStringLiteral("C:/docs/a.pdf"));
    CHECK(entries.at(0).pinned);                   // the pin is the more deliberate statement
    CHECK(entries.at(1).path == QStringLiteral("C:/docs/b.pdf"));
}
