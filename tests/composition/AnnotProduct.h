// tests/composition/AnnotProduct -- the product's own composition opened over a corpus copy, and the page as
// the ENGINE draws it, for the annotation cases graded against rendered ink (Stage 6 guide rule 6). Shared by
// `tst_annotink` (T5) and `tst_markupink` (T6); lifted out of the first at T6 rather than copied into the second.
//
// Every root -- settings, session, recovery -- is a scratch directory (`CLAUDE.md` -> *Traps*), and the login
// name is fixed, so a comment's `/T` never depends on who runs the suite (S6 T6).

#ifndef RAWPAGE_TESTS_COMPOSITION_ANNOTPRODUCT_H
#define RAWPAGE_TESTS_COMPOSITION_ANNOTPRODUCT_H

#include <catch2/catch_test_macros.hpp>

#include "app/CompositionRoot.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/render/RenderScheduler.h"
#include "tests/render/TextOracle.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/HeldRaster.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ViewportController.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLayout>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace annot_product {

using namespace rawpage;

inline constexpr const char* kLoginName = "composition-login";

struct Product {
    QTemporaryDir                         scratch;
    std::unique_ptr<app::CompositionRoot> root;
    std::unique_ptr<ui::MainWindow>       window;
    ui::PageView*                         view = nullptr;
    QString                               openedPath;

    Product()
    {
        REQUIRE(scratch.isValid());
        app::CompositionRoot::Options options;
        options.settingsPath = QDir(scratch.path()).filePath(QStringLiteral("settings.ini"));
        options.sessionPath = QDir(scratch.path()).filePath(QStringLiteral("session.json"));
        options.recoveryRoot = QDir(scratch.path()).filePath(QStringLiteral("recovery"));
        options.loginName = QString::fromLatin1(kLoginName);
        root = std::make_unique<app::CompositionRoot>(std::move(options));
        window = std::make_unique<ui::MainWindow>(root->services());
        window->resize(1100, 1000);
        window->show();
        QCoreApplication::processEvents();
    }

    ~Product()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        root.reset();
    }

    ui::PageView* open(const char* relative)
    {
        const QString source = test_oracle::corpusPath(relative);
        const QString target = QDir(scratch.path()).filePath(QFileInfo(source).fileName());
        QFile::remove(target);
        REQUIRE(QFile::copy(source, target));
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        openedPath = target;
        window->openFiles({target});
        for (QWidget* widget : window->findChildren<QWidget*>())
            if (QLayout* layout = widget->layout()) layout->activate();
        QCoreApplication::processEvents();
        view = window->currentView();
        REQUIRE(view != nullptr);
        view->setFocus(Qt::OtherFocusReason);
        settle();
        return view;
    }

    void settle()
    {
        for (int round = 0; round < 8; ++round) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            root->services().renderScheduler.waitForIdle(4000);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        view->refresh();
        view->viewport()->repaint();
    }

    ui::DocumentWindow* document() const { return window->currentDocument(); }
    ui::AnnotationOverlay* overlay() const { return document()->annotations(); }
    const core::AnnotationStore& store() const { return document()->journal()->document().annotations(); }

    // A whole page as the view lays it out, drawn by the engine with or without its annotations, asked
    // for by THIS FILE through the scheduler, bypassing the cache.
    QImage page(int index, bool annotations)
    {
        const core::render::PageSlot* slot = view->layoutEngine().layout().slotForPage(index);
        REQUIRE(slot != nullptr);
        core::render::RenderRequest request;
        request.document = view->document();
        request.pageIndex = index;
        request.deviceScale = view->layoutEngine().layout().presentedScale();
        request.revision = view->revision();
        request.viewRotation = view->controller().viewRotation();
        request.flags = annotations ? view->renderFlags()
                                    : static_cast<core::render::RenderFlags>(
                                          static_cast<std::uint32_t>(view->renderFlags())
                                          & ~static_cast<std::uint32_t>(core::render::RenderFlags::Annotations));
        request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(slot->rect.size.width)),
                                              static_cast<int>(std::ceil(slot->rect.size.height))};
        REQUIRE(request.isValid());
        core::render::RenderScheduler& scheduler = root->services().renderScheduler;
        std::optional<QImage> arrived;
        const auto token = scheduler.subscribeReady([&](const core::render::RenderRequest& done, const core::RasterImage& raster) {
            if (!(done == request) || !raster.isValid()) return;
            const ui::HeldRaster held = ui::holdRaster(raster);
            arrived = held.image.copy().convertToFormat(QImage::Format_RGB32);
        });
        scheduler.enqueue(request, core::render::RenderPriority::Visible, core::render::CachePolicy::Bypass);
        QElapsedTimer timer;
        timer.start();
        while (!arrived && timer.elapsed() < 30000) settle();
        scheduler.unsubscribeReady(token);
        REQUIRE(arrived.has_value());
        return *arrived;
    }
};

// Where a page-space rectangle lies in the page's own raster (device pixels from the sheet's corner).
inline QRectF onSheet(const ui::PageView& view, int page, const core::PdfRect& rect)
{
    const core::render::PageSlot* slot = view.layoutEngine().layout().slotForPage(page);
    REQUIRE(slot != nullptr);
    double left = 1e18, top = 1e18, right = -1e18, bottom = -1e18;
    for (const core::PdfPoint corner : {core::PdfPoint{rect.left(), rect.bottom()}, core::PdfPoint{rect.right(), rect.top()},
                                        core::PdfPoint{rect.left(), rect.top()}, core::PdfPoint{rect.right(), rect.bottom()}}) {
        const std::optional<core::DevicePoint> d = view.layoutEngine().layout().toContent(page, corner);
        REQUIRE(d);
        left = std::min(left, d->x - slot->rect.left());
        right = std::max(right, d->x - slot->rect.left());
        top = std::min(top, d->y - slot->rect.top());
        bottom = std::max(bottom, d->y - slot->rect.top());
    }
    return QRectF(left, top, right - left, bottom - top);
}

// A sheet pixel, in the viewport's logical pixels -- the coordinates a press arrives in.
inline QPoint toWidget(const ui::PageView& view, int page, int x, int y)
{
    const core::render::PageSlot* slot = view.layoutEngine().layout().slotForPage(page);
    const core::DeviceRect visible = view.controller().visibleRect();
    const double dpr = view.devicePixelRatioF();
    return QPoint(static_cast<int>(std::floor((slot->rect.left() + x + 0.5 - visible.left()) / dpr)),
                  static_cast<int>(std::floor((slot->rect.top() + y + 0.5 - visible.top()) / dpr)));
}

inline long long differingPixels(const QImage& a, const QImage& b)
{
    if (a.size() != b.size()) return a.width() * static_cast<long long>(a.height());
    long long n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y)) ++n;
    return n;
}

} // namespace annot_product

#endif // RAWPAGE_TESTS_COMPOSITION_ANNOTPRODUCT_H
