// Stage 2 T15 -- the sixteen `pageDisplay/` and `performance/` keys. Guide case A-63.
//
// ---------------------------------------------------------------------------------------------
// WHY THIS IS A FILE OF ITS OWN RATHER THAN SIXTEEN MORE CASES IN `tst_settings.cpp`.
//
// `tst_settings.cpp` tests the MECHANISM -- typed get and set, clamping, migration, the codecs, the
// negative-compile fixtures. Every one of those cases would pass with an empty table. This file
// tests the TABLE: that these sixteen rows exist, carry the values four other files used to own,
// and survive a round trip through a real INI. They are different questions, and the second one is
// the one that goes wrong when somebody edits a default.
//
// ---------------------------------------------------------------------------------------------
// THE FOUR MIGRATED DEFAULTS ARE THE POINT OF THE FILE.
//
// `SettingsKeys.def` rule 2 says a default lives in exactly one place. Four of these sixteen had a
// C++ home before T15 -- `DpiPolicy::Limits::maxRenderDpi`, `RenderCache::Config::memoryCapBytes`
// and `diskCapBytes`, and `RenderScheduler::Config::workerCount` -- and each of those homes said in
// its own comment that the row would become the owner when it landed. This file is what makes that
// a check rather than a promise: the row and the struct are asserted to agree, so the day one moves
// without the other is a failure rather than a surprise found by a user whose cache is the wrong
// size.

#include <catch2/catch_test_macros.hpp>

#include "core/render/DpiPolicy.h"
#include "core/render/PagePresentation.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <set>
#include <string_view>

using namespace rawpage;
using core::SettingsGroup;
using core::render::FitMode;
using core::render::LayoutMode;

namespace {

QString iniIn(const QTemporaryDir& directory)
{
    return QDir(directory.path()).filePath(QStringLiteral("settings.ini"));
}

} // namespace

TEST_CASE("the sixteen Stage 2 keys are declared, in the two groups the plan names",
          "[settings][stage2]")
{
    int pageDisplay = 0;
    int performance = 0;
    std::set<std::string_view> paths;

    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys()) {
        if (info.group == SettingsGroup::PageDisplay) ++pageDisplay;
        if (info.group == SettingsGroup::PerformanceAndCache) ++performance;
        paths.insert(info.path);
    }

    // ELEVEN AND FIVE AT STAGE 2, exactly as `Implementation-Plan.md` § Stage 2 lists them. The
    // counts are asserted as well as the names, for `tst_shell`'s reason about the action census:
    // a name-only check stays green when a row is added by accident, and "the sixteen keys" is a
    // claim about the whole set.
    //
    // **`PageDisplay` IS TWELVE FROM STAGE 4 T7**, and the twelfth is named below rather than
    // absorbed into a looser bound: `pageDisplay/autoScrollSpeed` is the first of D4-5's fifteen
    // Stage 4 keys. Stage 2's eleven are still asserted by name, which is what this case is for.
    // **THIRTEEN FROM STAGE 5 T10**: `pageDisplay/snapshotDpi` (D5-11), named for the same reason.
    CHECK(pageDisplay == 13);
    CHECK(performance == 5);

    for (const char* path : {"pageDisplay/layoutMode", "pageDisplay/defaultZoom",
                             "pageDisplay/showCoverInTwoUp", "pageDisplay/rtlBinding",
                             "pageDisplay/lineWeight", "pageDisplay/antialiasText",
                             "pageDisplay/antialiasImages", "pageDisplay/antialiasLineArt",
                             "pageDisplay/transparencyGrid", "pageDisplay/maxRenderResolution",
                             "pageDisplay/wheelZooms", "performance/renderWorkers",
                             "performance/tileCacheMB", "performance/diskCacheMB",
                             "performance/prefetchPages", "performance/throttleOnBattery"}) {
        INFO("path " << path);
        CHECK(paths.count(path) == 1);
    }
}

TEST_CASE("the four defaults that used to live in C++ now live in the table", "[settings][stage2]")
{
    QTemporaryDir directory;
    core::Settings settings(iniIn(directory));

    // Each of these four structs said, in its own comment, that the row would become the owner. The
    // assertion is that they AGREE -- not that either is right, which is a judgement, but that the
    // product cannot be running one number while the Preferences dialog shows another.
    const core::render::DpiPolicy::Limits limits;
    CHECK(static_cast<double>(settings.get(core::keys::pageDisplay::maxRenderResolution))
          == limits.maxRenderDpi);

    const core::render::RenderCache::Config cache;
    CHECK(static_cast<std::size_t>(settings.get(core::keys::performance::tileCacheMB))
              * 1024u * 1024u
          == cache.memoryCapBytes);

    // ZERO, AND THE ZERO IS D2-14's DECISION rather than an unset field: whether the disk tier earns
    // its place is a measurement T18 makes over the corpus, and a tier switched on by default would
    // make that measurement about itself.
    CHECK(settings.get(core::keys::performance::diskCacheMB) == 0);
    CHECK(cache.diskCapBytes == 0);

    // ONE, and it is I-05's answer: PDFium cannot be called from two threads at once.
    const core::render::RenderScheduler::Config scheduler;
    CHECK(settings.get(core::keys::performance::renderWorkers) == scheduler.workerCount);
    CHECK(scheduler.workerCount == 1);
}

TEST_CASE("every Stage 2 key round-trips through a real settings file", "[settings][stage2]")
{
    QTemporaryDir directory;
    const QString path = iniIn(directory);

    {
        core::Settings settings(path);

        // Every value written is DIFFERENT from its default, so a `get` that silently ignored the
        // file would read back the default and fail -- which is the failure mode A-63 is about and
        // the one Stage 1's I-08/I-09 (its own numbering) were.
        settings.set(core::keys::pageDisplay::layoutMode, LayoutMode::TwoUpContinuous);
        settings.set(core::keys::pageDisplay::defaultZoom, FitMode::Width);
        settings.set(core::keys::pageDisplay::showCoverInTwoUp, true);
        settings.set(core::keys::pageDisplay::rtlBinding, true);
        settings.set(core::keys::pageDisplay::lineWeight, true);
        settings.set(core::keys::pageDisplay::antialiasText, false);
        settings.set(core::keys::pageDisplay::antialiasImages, false);
        settings.set(core::keys::pageDisplay::antialiasLineArt, false);
        settings.set(core::keys::pageDisplay::transparencyGrid, true);
        settings.set(core::keys::pageDisplay::maxRenderResolution, 1200);
        settings.set(core::keys::pageDisplay::wheelZooms, true);
        settings.set(core::keys::performance::renderWorkers, 4);
        settings.set(core::keys::performance::tileCacheMB, 512);
        settings.set(core::keys::performance::diskCacheMB, 128);
        settings.set(core::keys::performance::prefetchPages, 3);
        settings.set(core::keys::performance::throttleOnBattery, false);
        REQUIRE(settings.sync());
    }

    core::Settings reloaded(path);
    CHECK(reloaded.get(core::keys::pageDisplay::layoutMode) == LayoutMode::TwoUpContinuous);
    CHECK(reloaded.get(core::keys::pageDisplay::defaultZoom) == FitMode::Width);
    CHECK(reloaded.get(core::keys::pageDisplay::showCoverInTwoUp));
    CHECK(reloaded.get(core::keys::pageDisplay::rtlBinding));
    CHECK(reloaded.get(core::keys::pageDisplay::lineWeight));
    CHECK_FALSE(reloaded.get(core::keys::pageDisplay::antialiasText));
    CHECK_FALSE(reloaded.get(core::keys::pageDisplay::antialiasImages));
    CHECK_FALSE(reloaded.get(core::keys::pageDisplay::antialiasLineArt));
    CHECK(reloaded.get(core::keys::pageDisplay::transparencyGrid));
    CHECK(reloaded.get(core::keys::pageDisplay::maxRenderResolution) == 1200);
    CHECK(reloaded.get(core::keys::pageDisplay::wheelZooms));
    CHECK(reloaded.get(core::keys::performance::renderWorkers) == 4);
    CHECK(reloaded.get(core::keys::performance::tileCacheMB) == 512);
    CHECK(reloaded.get(core::keys::performance::diskCacheMB) == 128);
    CHECK(reloaded.get(core::keys::performance::prefetchPages) == 3);
    CHECK_FALSE(reloaded.get(core::keys::performance::throttleOnBattery));
}

TEST_CASE("the two enums are stored as their own names, not as numbers", "[settings][stage2]")
{
    // A NUMBER IN THE FILE WOULD BE A TIME BOMB. `SettingsKeys.def` rule 1: the path is a permanent
    // public name and the file is read back by later versions -- and an enumerator's VALUE changes
    // the day somebody inserts `LayoutMode::Spread` in the middle, which would silently reinterpret
    // every saved preference. The token is the permanent name; this asserts the bytes.
    QTemporaryDir directory;
    const QString path = iniIn(directory);

    {
        core::Settings settings(path);
        settings.set(core::keys::pageDisplay::layoutMode, LayoutMode::TwoUp);
        settings.set(core::keys::pageDisplay::defaultZoom, FitMode::Visible);
        REQUIRE(settings.sync());
    }

    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString contents = QString::fromUtf8(file.readAll());

    CHECK(contents.contains(QStringLiteral("layoutMode=two-up")));
    CHECK(contents.contains(QStringLiteral("defaultZoom=visible")));

    // And the tokens are exactly what the enum's own name function returns, so a combo box built
    // from `SettingsEditorTraits` cannot offer a value the file is unable to hold.
    CHECK(std::string(core::render::layoutModeName(LayoutMode::TwoUp)) == "two-up");
    CHECK(std::string(core::render::fitModeName(FitMode::Visible)) == "visible");
}

TEST_CASE("a nonsense enum value in the file falls back rather than being invented",
          "[settings][stage2]")
{
    QTemporaryDir directory;
    const QString path = iniIn(directory);

    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[pageDisplay]\n";
        out << "layoutMode=three-up\n";
        out << "defaultZoom=fit-everything\n";
    }

    // THE DECLARED DEFAULT, not the first enumerator and not a crash. `fitModeFromName` answers
    // `nullopt` for a name it does not know precisely so this happens: answering `None` there would
    // make a typo silently mean "actual size", which is a real value and therefore indistinguishable
    // from a deliberate choice. A hand-edited INI must not prevent startup either (rule 3).
    core::Settings settings(path);
    CHECK(settings.get(core::keys::pageDisplay::layoutMode) == LayoutMode::SingleContinuous);
    CHECK(settings.get(core::keys::pageDisplay::defaultZoom) == FitMode::None);
}

TEST_CASE("the ranged Stage 2 keys clamp rather than refuse", "[settings][stage2]")
{
    QTemporaryDir directory;
    core::Settings settings(iniIn(directory));

    // Rule 3: a hand-edited INI or a downgraded install must not prevent startup, so a value out of
    // range is CLAMPED and logged rather than rejected.
    settings.set(core::keys::performance::renderWorkers, 999);
    CHECK(settings.get(core::keys::performance::renderWorkers) == 16);
    settings.set(core::keys::performance::renderWorkers, 0);
    CHECK(settings.get(core::keys::performance::renderWorkers) == 1);

    settings.set(core::keys::pageDisplay::maxRenderResolution, 1);
    CHECK(settings.get(core::keys::pageDisplay::maxRenderResolution) == 96);
    settings.set(core::keys::pageDisplay::maxRenderResolution, 100000);
    CHECK(settings.get(core::keys::pageDisplay::maxRenderResolution) == 4800);

    // Zero is a LEGITIMATE disk cache -- it is the default and it means "no tier" -- so the range
    // starts there rather than at 1, the same call `documents/recentCount` made about "keep no
    // history".
    settings.set(core::keys::performance::diskCacheMB, 0);
    CHECK(settings.get(core::keys::performance::diskCacheMB) == 0);

    settings.set(core::keys::performance::prefetchPages, 0);
    CHECK(settings.get(core::keys::performance::prefetchPages) == 0);
}
