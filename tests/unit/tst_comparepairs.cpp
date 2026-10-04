// core/settings/ComparePairs -- the Compare dialog's memory of each older file's newer one. The user's request,
// 2026-10-04; guide case A-126 (`Stage5-TextSearch-Testing-Guide.md` §5.17). The dialog's use of it, and the
// window's noting it, are `tests/ui/tst_comparison.cpp`'s.

#include <catch2/catch_test_macros.hpp>

#include "core/settings/ComparePairs.h"
#include "core/settings/Settings.h"

#include <QStringList>
#include <QTemporaryDir>

using namespace rawpage::core;

TEST_CASE("the compare memory offers each older file the newer one it was last compared against", "[comparepairs][A-126]")
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        CHECK(ComparePairs::newerFor(settings, QStringLiteral("C:/r/a.pdf")).isEmpty());

        ComparePairs::note(settings, QStringLiteral("C:/r/a.pdf"), QStringLiteral("C:/r/b.pdf"));
        ComparePairs::note(settings, QStringLiteral("C:/r/c.pdf"), QStringLiteral("C:/r/d.pdf"));
        CHECK(ComparePairs::newerFor(settings, QStringLiteral("C:/r/a.pdf")) == QStringLiteral("C:/r/b.pdf"));
        CHECK(ComparePairs::newerFor(settings, QStringLiteral("C:/r/c.pdf")) == QStringLiteral("C:/r/d.pdf"));
        CHECK(ComparePairs::newerFor(settings, QStringLiteral("C:/r/b.pdf")).isEmpty());

        // A NEW PAIR FOR ONE OLDER FILE REPLACES ITS OLD ONE and goes first; the other is kept.
        ComparePairs::note(settings, QStringLiteral("C:/r/a.pdf"), QStringLiteral("C:/r/e.pdf"));
        CHECK(ComparePairs::newerFor(settings, QStringLiteral("C:/r/a.pdf")) == QStringLiteral("C:/r/e.pdf"));
        CHECK(settings.get(keys::documents::recentComparisons)
              == QStringList({QStringLiteral("C:/r/a.pdf"), QStringLiteral("C:/r/e.pdf"), QStringLiteral("C:/r/c.pdf"),
                              QStringLiteral("C:/r/d.pdf")}));
        REQUIRE(settings.sync());
    }

    // ACROSS A RESTART: the memory is a setting, not a member.
    Settings reloaded(path);
    CHECK(ComparePairs::newerFor(reloaded, QStringLiteral("C:/r/a.pdf")) == QStringLiteral("C:/r/e.pdf"));
    CHECK(ComparePairs::newerFor(reloaded, QStringLiteral("C:/r/c.pdf")) == QStringLiteral("C:/r/d.pdf"));
}

TEST_CASE("one older file reached through other separators or capitals is one key", "[comparepairs][A-126]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));

    ComparePairs::note(settings, QStringLiteral("C:\\Docs\\Report-v1.pdf"), QStringLiteral("C:\\Docs\\Report-v2.pdf"));

    // THE SPELLING NOTED IS THE ONE RETURNED -- it is what the dialog shows.
    CHECK(ComparePairs::newerFor(settings, QStringLiteral("c:/docs/report-v1.pdf"))
          == QStringLiteral("C:\\Docs\\Report-v2.pdf"));
    CHECK(ComparePairs::newerFor(settings, QStringLiteral("C:/Docs/./sub/../Report-v1.pdf"))
          == QStringLiteral("C:\\Docs\\Report-v2.pdf"));

    // And noted again under the other spelling, it is still one pair.
    ComparePairs::note(settings, QStringLiteral("c:/docs/report-v1.pdf"), QStringLiteral("C:/Docs/Report-v3.pdf"));
    CHECK(settings.get(keys::documents::recentComparisons).size() == 2);
    CHECK(ComparePairs::newerFor(settings, QStringLiteral("C:\\Docs\\Report-v1.pdf"))
          == QStringLiteral("C:/Docs/Report-v3.pdf"));
}

TEST_CASE("the compare memory keeps the most recent pairs and records no pair of one file", "[comparepairs][A-126]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));

    const auto older = [](int i) { return QStringLiteral("C:/r/old-%1.pdf").arg(i); };
    const auto newer = [](int i) { return QStringLiteral("C:/r/new-%1.pdf").arg(i); };
    const int noted = ComparePairs::kCapacity + 5;
    for (int i = 0; i < noted; ++i) ComparePairs::note(settings, older(i), newer(i));

    // THE OLDEST FIVE ARE GONE and the rest are kept, the bound and not one pair fewer.
    CHECK(settings.get(keys::documents::recentComparisons).size() == 2 * ComparePairs::kCapacity);
    for (int i = 0; i < 5; ++i) CHECK(ComparePairs::newerFor(settings, older(i)).isEmpty());
    for (int i = 5; i < noted; ++i) CHECK(ComparePairs::newerFor(settings, older(i)) == newer(i));

    // NOT RECORDED: a file against itself, under any spelling, and an empty path on either side.
    const QStringList before = settings.get(keys::documents::recentComparisons);
    ComparePairs::note(settings, QStringLiteral("C:/r/same.pdf"), QStringLiteral("c:\\R\\same.pdf"));
    ComparePairs::note(settings, QString(), newer(1));
    ComparePairs::note(settings, older(1), QStringLiteral("  "));
    CHECK(settings.get(keys::documents::recentComparisons) == before);

    // A HAND-EDITED LIST WITH AN ODD LAST ENTRY: that entry has no partner and is not read as one.
    settings.set(keys::documents::recentComparisons,
                 QStringList({QStringLiteral("C:/r/a.pdf"), QStringLiteral("C:/r/b.pdf"), QStringLiteral("C:/r/c.pdf")}));
    CHECK(ComparePairs::newerFor(settings, QStringLiteral("C:/r/a.pdf")) == QStringLiteral("C:/r/b.pdf"));
    CHECK(ComparePairs::newerFor(settings, QStringLiteral("C:/r/c.pdf")).isEmpty());
}
