#include "ui/shell/DocumentWindow.h"

#include "core/Log.h"
#include "core/doc/PageTree.h"
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

    // The page tree comes from the BACKEND, through `core::IDocumentBackend` -- moved onto that
    // interface at T13 for exactly this call, because `ui/` may never name `pdf/` (Stage 2 guide
    // issue I-08). Null is an ordinary answer for an invalid handle and the view draws nothing.
    view_->setDocument(handle_, revision_, services_.backend.pages(handle_));
}

DocumentWindow::~DocumentWindow()
{
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
