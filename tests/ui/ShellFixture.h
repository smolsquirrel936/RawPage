// Everything a `MainWindow` needs, owned together. Stage 2 T13.
//
// ---------------------------------------------------------------------------------------------
// WHY IT IS A HEADER NOW AND WAS SEVEN COPIES BEFORE.
//
// Each `tests/ui` file carried its own `Fixture` with the same eight members, which was fine while
// the list was eight and became a seven-way edit the moment `app::Services` grew a render pipeline.
// The copies also drifted -- two of them had a comment the other five had lost. One definition,
// derived from where a case needs extras.
//
// What is NOT here is the point of it: **no PDF engine and no OS bundle**. `platform::Platform` has
// every interface null, which is the honest state of a test bundle, and the backend is
// `uitest::FakeBackend`. That is the property flow A exists to produce -- `MainWindow` receives
// interfaces and is constructible with no PDFium, no Win32 and no display.
//
// ---------------------------------------------------------------------------------------------
// THE FAKE RASTERIZER PAINTS A COLOUR NO PAGE IS.
//
// Guide case A-53 asks whether *a page is on screen*, and distinguishes it from *tiles were
// requested*. A fake that returned white would make the two indistinguishable, because
// `TilePresenter` already paints a white sheet under every page before any tile arrives. So the
// fake fills with a flat blue that nothing else in the viewer draws, and the case asserts the
// grabbed pixels are that colour -- which can only be true if the request was built, queued, run,
// delivered, converted to a `QImage` and painted at the right rectangle.
//
// It also fills with a STRIDE LARGER THAN `width * 4` on purpose. A rasterizer is entitled to align
// its rows and `core::RasterImage` carries `bytesPerLine` precisely so a caller cannot assume;
// PDFium happens to return tight rows, so a fake that also did would leave the padded case
// untested until a real engine on some other machine produced one.
//
// ---------------------------------------------------------------------------------------------
// NOTHING PUMPS THE SCHEDULER HERE, AND THAT IS DELIBERATE.
//
// In the product `app/CompositionRoot` installs the one wake handler and posts `deliverReady()`
// onto the UI thread (convention 18: `setWakeHandler` REPLACES, so exactly one owner). A test with
// no composition root has no such owner, so `pumpTiles()` below calls `waitForIdle` and
// `deliverReady` itself. That is a back door and this comment is it being named (convention 19):
// **what these cases therefore do NOT cover is that a tile arrives without anyone asking** -- the
// wake path itself. Its owner is the composition root, and manual case M-03 is where a person sees
// a page appear without touching anything.

#ifndef RAWPAGE_TESTS_UI_SHELLFIXTURE_H
#define RAWPAGE_TESTS_UI_SHELLFIXTURE_H

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "core/LogBuffer.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "tests/ui/FakeBackend.h"
#include "tests/unit/FakeDurableFiles.h"

#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <atomic>
#include <memory>
#include <span>

namespace rawpage::uitest {

// The blue every fake tile is filled with. Chosen so that no part of the viewer's own chrome -- the
// white sheet, the grey border, the palette's window colour -- can be mistaken for it.
inline constexpr int kFakeTileRed = 0x20;
inline constexpr int kFakeTileGreen = 0x60;
inline constexpr int kFakeTileBlue = 0xd0;

// THE ROTATION DELTA THE LAST REQUEST FOR PAGE 1 CARRIED, in degrees, or -1 before any (Stage 3 T10, D3-15). The
// fake paints the same blue whatever the rotation, so a case about an unsaved rotation reaching the engine
// reads this rather than the pixels. Atomic because the rasterizer runs on the scheduler's worker.
inline std::atomic<int> fakeFirstPageRotationDelta{-1};

class FakeRasterizer final : public core::IPageRasterizer {
public:
    core::Result<core::RasterImage> raster(const core::render::RenderRequest& request) override
    {
        if (!request.isValid()) {
            return core::Error{core::ErrorCode::InvalidArgument,
                               QLatin1String("error.render.badRequest")};
        }

        // THE TRANSPARENCY FLAG IS HONOURED, because a fake that ignored it would make
        // `pageDisplay/transparencyGrid` untestable here -- and untestable is how that option came
        // to be implemented as a viewer-only paint in the first place, which showed nothing.
        if (request.pageIndex == 0) fakeFirstPageRotationDelta = static_cast<int>(request.pageRotationDelta);

        const bool transparent = core::render::hasFlag(
            request.flags, core::render::RenderFlags::TransparentBackground);

        core::RasterImage image;
        image.width = request.tile.width;
        image.height = request.tile.height;

        // PADDED ROWS. See the header: `bytesPerLine` exists so a caller cannot assume
        // `width * 4`, and a fake with tight rows would never exercise the assumption.
        image.bytesPerLine = request.tile.width * 4 + 16;
        image.format = transparent ? core::PixelFormat::Bgra8888 : core::PixelFormat::Bgrx8888;
        image.pixels = QByteArray(static_cast<qsizetype>(image.bytesPerLine) * image.height,
                                  Qt::Uninitialized);

        for (int y = 0; y < image.height; ++y) {
            auto* row = reinterpret_cast<unsigned char*>(image.pixels.data())
                        + static_cast<std::size_t>(y) * image.bytesPerLine;
            for (int x = 0; x < image.width; ++x) {
                row[x * 4 + 0] = static_cast<unsigned char>(kFakeTileBlue);
                row[x * 4 + 1] = static_cast<unsigned char>(kFakeTileGreen);
                row[x * 4 + 2] = static_cast<unsigned char>(kFakeTileRed);
                // A HOLE IN THE MIDDLE when the page is rendered onto transparency, so the
                // checkerboard behind it has somewhere to show. A real page's transparent regions
                // are wherever its content is not; this fake's are a quarter of every tile.
                const bool hole = transparent && (x % 4 == 0) && (y % 4 == 0);
                row[x * 4 + 3] = hole ? 0x00 : 0xff;
            }
        }

        return image;
    }
};

// =============================================================================================
// The two OS interfaces a presentation needs -- S4 T10, D4-12
// =============================================================================================
// `platform::Platform`'s members are null unless a build implements them (convention 6), and the
// widget suite has always taken the honest empty bundle. T10 is the first task whose SUBJECT is
// one of those members: `IPowerSession::setPresentationModeActive` is what guide case A-58
// asserts, and a null member would make that case vacuous rather than green.
//
// **The power fake COUNTS rather than merely records.** A-58's claim is *true on entry and false
// on every exit path*, which a final-state check alone cannot make: a controller that called
// `true` twice and `false` once ends inactive and is still leaking a request. The calls are kept
// in order so a case can assert the sequence.
struct FakePowerSession final : platform::IPowerSession {
    bool                 active = false;
    std::vector<bool>    calls;
    bool                 refuse = false;      // the OS saying no, which must not be fatal

    bool isOnBattery() const override { return false; }
    core::Status subscribe(EventHandler) override { return {}; }

    core::Status setPresentationModeActive(bool on) override
    {
        calls.push_back(on);
        if (refuse)
            return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.platform.refused"),
                               QString()};
        active = on;
        return {};
    }
};

// A topology a case can CHANGE, which is what A-60 needs: the monitor being presented on has to be
// able to disappear, and no headless run can unplug one.
struct FakeTopology final : platform::IDisplayTopology {
    std::vector<platform::MonitorInfo> displays;
    std::vector<ChangedHandler>        handlers;

    core::Result<std::vector<platform::MonitorInfo>> monitors() const override { return displays; }

    core::Status subscribe(ChangedHandler handler) override
    {
        handlers.push_back(std::move(handler));
        return {};
    }

    // Drives every subscriber, as `WinDisplayTopology` does when Windows reports a change.
    void notify()
    {
        for (const ChangedHandler& handler : handlers)
            if (handler) handler();
    }
};

struct ShellFixture {
    ShellFixture() = default;

    // A fixture whose settings file EXISTS BEFORE `Settings` loads it, read through a supplied alias
    // table. Stage 2 I-39: what a user keeps across an action rename is decided at load, so a case
    // about it has to hand the loader a file it did not write itself -- and the shipped alias table
    // has no rows, so the case has to hand it those too. Members initialise in declaration order,
    // which is what lets `iniPath` write the file between `directory` and `settings`.
    ShellFixture(const QString& iniContents, std::span<const core::ActionAlias> aliases)
        : iniPath(seedIni(directory, iniContents)), settings(iniPath, aliases)
    {
    }

    QTemporaryDir         directory;
    QString               iniPath{QDir(directory.path()).filePath(QStringLiteral("settings.ini"))};
    core::Settings        settings{iniPath};
    core::RecentFiles     recentFiles{settings};
    core::ActionRegistry  actions;
    core::KeyBindingStore keyBindings;
    core::LogBuffer       logBuffer;
    // Every interface null -- the honest state of a test bundle -- EXCEPT `durableFiles` (Stage 3 T10): a
    // document window saves and records through it, and with none every editable document would open
    // with a *recovery unavailable* banner and refuse every save. It is the headless suite's fake, which
    // writes real files and replaces NON-atomically; what that does not cover is in its own header.
    platform::Platform    platform = platformForTests();
    FakeBackend           backend;    // S2 T9: Services carries the engine

    core::render::RenderCache renderCache;

    // WHERE THIS FIXTURE'S RECOVERY JOURNALS GO: inside its own temporary directory, never the user's
    // `%LOCALAPPDATA%\RawPage\recovery` (Stage 3 T10).
    QString recoveryRoot{QDir(directory.path()).filePath(QStringLiteral("recovery"))};

    // DECLARED LAST of the render pair, so it is DESTROYED FIRST. Its destructor joins the worker,
    // and a worker still inside `FakeRasterizer` when the cache went away would be reaching into a
    // destroyed object -- the same ordering `app/CompositionRoot` writes out explicitly.
    core::render::RenderScheduler renderScheduler{
        [] { return std::unique_ptr<core::IPageRasterizer>(new FakeRasterizer); }, &renderCache};

    app::Services services()
    {
        return app::Services{settings,    recentFiles,     actions,     keyBindings, logBuffer,
                             backend,     renderScheduler, renderCache, platform,    recoveryRoot};
    }

    // Runs queued work until every tile has been rasterized and delivered. Returns how many results
    // were handed to subscribers. See the header on what this back door does NOT cover.
    int pumpTiles(int budgetMs = 4000)
    {
        int delivered = 0;
        QElapsedTimer timer;
        timer.start();

        do {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            renderScheduler.waitForIdle(budgetMs);
            delivered += renderScheduler.deliverReady();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        } while (renderScheduler.pendingCount() > 0 && timer.elapsed() < budgetMs);

        return delivered;
    }

    // The fake by its own type, so a case can make the FILE SYSTEM refuse -- the bundle holds it as the
    // interface, and `failReplace` / `failSync` are the fake's own (Stage 3 guide I-15).
    tests::FakeDurableFiles& durableFiles()
    {
        return *static_cast<tests::FakeDurableFiles*>(platform.durableFiles.get());
    }

    FakePowerSession& powerSession()
    {
        return *static_cast<FakePowerSession*>(platform.powerSession.get());
    }

    FakeTopology& topology()
    {
        return *static_cast<FakeTopology*>(platform.displayTopology.get());
    }

    // S4 T10: the bundle grew from one member to three, and the name went with it. `IPowerSession`
    // and `IDisplayTopology` are here because `ui/viewer/FullScreenController` is the first thing
    // in the product to call either, and a null member would make guide case A-58 vacuous.
    static platform::Platform platformForTests()
    {
        platform::Platform bundle;
        bundle.durableFiles = std::make_unique<tests::FakeDurableFiles>();
        bundle.powerSession = std::make_unique<FakePowerSession>();
        bundle.displayTopology = std::make_unique<FakeTopology>();
        return bundle;
    }

    // Writes `contents` as the settings file in `dir` and returns its path. Used only by the seeded
    // constructor above, where it runs as `iniPath`'s initialiser.
    static QString seedIni(const QTemporaryDir& dir, const QString& contents)
    {
        const QString path = QDir(dir.path()).filePath(QStringLiteral("settings.ini"));
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        REQUIRE(file.write(contents.toUtf8()) == contents.toUtf8().size());
        file.close();
        return path;
    }
};

} // namespace rawpage::uitest

#endif // RAWPAGE_TESTS_UI_SHELLFIXTURE_H
