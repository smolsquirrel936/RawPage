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

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QString>
#include <QTemporaryDir>

#include <memory>

namespace rawpage::uitest {

// The blue every fake tile is filled with. Chosen so that no part of the viewer's own chrome -- the
// white sheet, the grey border, the palette's window colour -- can be mistaken for it.
inline constexpr int kFakeTileRed = 0x20;
inline constexpr int kFakeTileGreen = 0x60;
inline constexpr int kFakeTileBlue = 0xd0;

class FakeRasterizer final : public core::IPageRasterizer {
public:
    core::Result<core::RasterImage> raster(const core::render::RenderRequest& request) override
    {
        if (!request.isValid()) {
            return core::Error{core::ErrorCode::InvalidArgument,
                               QLatin1String("error.render.badRequest")};
        }

        core::RasterImage image;
        image.width = request.tile.width;
        image.height = request.tile.height;

        // PADDED ROWS. See the header: `bytesPerLine` exists so a caller cannot assume
        // `width * 4`, and a fake with tight rows would never exercise the assumption.
        image.bytesPerLine = request.tile.width * 4 + 16;
        image.format = core::PixelFormat::Bgrx8888;
        image.pixels = QByteArray(static_cast<qsizetype>(image.bytesPerLine) * image.height,
                                  Qt::Uninitialized);

        for (int y = 0; y < image.height; ++y) {
            auto* row = reinterpret_cast<unsigned char*>(image.pixels.data())
                        + static_cast<std::size_t>(y) * image.bytesPerLine;
            for (int x = 0; x < image.width; ++x) {
                row[x * 4 + 0] = static_cast<unsigned char>(kFakeTileBlue);
                row[x * 4 + 1] = static_cast<unsigned char>(kFakeTileGreen);
                row[x * 4 + 2] = static_cast<unsigned char>(kFakeTileRed);
                row[x * 4 + 3] = 0xff;
            }
        }

        return image;
    }
};

struct ShellFixture {
    QTemporaryDir         directory;
    QString               iniPath{QDir(directory.path()).filePath(QStringLiteral("settings.ini"))};
    core::Settings        settings{iniPath};
    core::RecentFiles     recentFiles{settings};
    core::ActionRegistry  actions;
    core::KeyBindingStore keyBindings;
    core::LogBuffer       logBuffer;
    platform::Platform    platform;   // every interface null: the honest state of a test bundle
    FakeBackend           backend;    // S2 T9: Services carries the engine

    core::render::RenderCache renderCache;

    // DECLARED LAST of the render pair, so it is DESTROYED FIRST. Its destructor joins the worker,
    // and a worker still inside `FakeRasterizer` when the cache went away would be reaching into a
    // destroyed object -- the same ordering `app/CompositionRoot` writes out explicitly.
    core::render::RenderScheduler renderScheduler{
        [] { return std::unique_ptr<core::IPageRasterizer>(new FakeRasterizer); }, &renderCache};

    app::Services services()
    {
        return app::Services{settings,    recentFiles,     actions,     keyBindings, logBuffer,
                             backend,     renderScheduler, renderCache, platform};
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
};

} // namespace rawpage::uitest

#endif // RAWPAGE_TESTS_UI_SHELLFIXTURE_H
