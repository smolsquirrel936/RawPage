#include "ui/shell/DocumentWindow.h"

#include "core/Log.h"
#include "core/doc/PageTree.h"
#include "core/settings/Settings.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "platform/platform.h"
#include "ui/shell/Catalog.h"
#include "ui/viewer/PageView.h"

#include <QFileInfo>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;

// How long the destructor will wait for a tile that is already inside the engine. One tile is
// measured at p95 0.5 ms (A2's probe), so this is three orders of magnitude of headroom and is a
// bound rather than an expectation -- a closing tab that hung on a wedged worker would be a
// product that cannot be quit.
constexpr int kDrainBudgetMs = 2000;

} // namespace

DocumentWindow::DocumentWindow(app::Services services, QString path,
                               core::DocumentHandle handle, QWidget* parent)
    : QWidget(parent), services_(services), path_(std::move(path)), handle_(handle)
{
    setObjectName(QStringLiteral("documentWindow"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    view_ = new PageView(services_.renderScheduler, services_.platform.displayTopology.get(), this);
    column->addWidget(view_);

    // THE PREFERENCES BEFORE THE DOCUMENT. `layoutMode` and `defaultZoom` are applied only on the
    // first `applySettings` call -- they are what a document OPENS at -- so they have to be in place
    // before there is a document to open at them.
    applyViewSettings();

    // The page tree comes from the BACKEND, through `core::IDocumentBackend` -- moved onto that
    // interface at T13 for exactly this call, because `ui/` may never name `pdf/` (Stage 2 guide
    // issue I-08). Null is an ordinary answer for an invalid handle and the view draws nothing.
    view_->setDocument(handle_, revision_, services_.backend.pages(handle_));

    // S2 T15. A `pageDisplay/` change reaches every open tab, not only the one in front: a person
    // who turns off image smoothing expects it off everywhere, and a tab that kept the old value
    // until it was next touched would be a preference that appears not to have applied.
    settingsSubscription_ = services_.settings.subscribe([this](const core::Settings::Change& change) {
        if (change.group == core::SettingsGroup::PageDisplay
            || change.group == core::SettingsGroup::PerformanceAndCache) {
            applyViewSettings();
        }
    });
}

void DocumentWindow::applyViewSettings()
{
    if (!view_) return;

    // ONE READ PER KEY, AND NO DEFAULTS SPELLED HERE. `Settings::get` never asks a caller for a
    // fallback -- `SettingsKeys.def` rule 2 -- so this cannot disagree with the table.
    const core::Settings& settings = services_.settings;

    ViewSettings view;
    view.layoutMode        = settings.get(core::keys::pageDisplay::layoutMode);
    view.defaultZoom       = settings.get(core::keys::pageDisplay::defaultZoom);
    view.showCoverInTwoUp  = settings.get(core::keys::pageDisplay::showCoverInTwoUp);
    view.rtlBinding        = settings.get(core::keys::pageDisplay::rtlBinding);
    view.lineWeight        = settings.get(core::keys::pageDisplay::lineWeight);
    view.antialiasText     = settings.get(core::keys::pageDisplay::antialiasText);
    view.antialiasImages   = settings.get(core::keys::pageDisplay::antialiasImages);
    view.antialiasLineArt  = settings.get(core::keys::pageDisplay::antialiasLineArt);
    view.transparencyGrid  = settings.get(core::keys::pageDisplay::transparencyGrid);
    view.maxRenderDpi      = settings.get(core::keys::pageDisplay::maxRenderResolution);
    view.wheelZooms        = settings.get(core::keys::pageDisplay::wheelZooms);
    view.prefetchPages     = settings.get(core::keys::performance::prefetchPages);

    view_->applySettings(view);
}

DocumentWindow::~DocumentWindow()
{
    // BEFORE ANYTHING ELSE. `Settings` belongs to the composition root and outlives every tab, so a
    // subscription left behind is a call into a destroyed widget on the next preference change --
    // convention 10, in the form where the caller is a settings store rather than a signal.
    if (settingsSubscription_ != 0) services_.settings.unsubscribe(settingsSubscription_);

    // ORDER, and the header argues each step. Cancel first, then drain, then drop the tiles, then
    // close -- a `close()` that ran while a worker was inside this document's PDFium handle would
    // be a use-after-free in the engine, which `IDocumentBackend::close()` states as the caller's
    // protocol because no mutex can express it.
    if (handle_.isValid()) {
        services_.renderScheduler.cancelDocument(handle_);

        if (!services_.renderScheduler.waitForIdle(kDrainBudgetMs)) {
            // REPORTED, not ignored. Closing anyway is still the least bad option -- refusing to
            // close the tab would strand the user -- but a drain that timed out is the precondition
            // for the crash this sequence exists to prevent, and it must be in the log when it
            // happens rather than inferred from the dump afterwards.
            RAWPAGE_LOG_ERROR(LogSubsystem::Render)
                << "close: the render scheduler did not go idle within" << kDrainBudgetMs
                << "ms; closing" << path_ << "anyway";
        }

        services_.renderCache.dropDocument(handle_);

        if (const core::Status closed = services_.backend.close(handle_); !closed) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Render)
                << "close: backend refused" << path_ << "--" << closed.error().messageKey;
        }
    }
}

int DocumentWindow::pageCount() const
{
    const core::PageTree* pages = services_.backend.pages(handle_);
    return pages != nullptr ? pages->count() : 0;
}

QString DocumentWindow::title() const
{
    const QFileInfo info(path_);
    const QString name = info.fileName();
    return name.isEmpty() ? catalog::translate(catalog::key::DocumentUntitled) : name;
}

} // namespace rawpage::ui
