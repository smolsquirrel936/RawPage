#include "ui/shell/DocumentWindow.h"

#include "core/Log.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/settings/Settings.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "platform/platform.h"
#include "ui/shell/Catalog.h"
#include "ui/viewer/GridGuideLayer.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/RulerWidget.h"
#include "ui/viewer/SplitViewContainer.h"

#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
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
    sourcePath_ = path_;
    build(nullptr);
}

DocumentWindow::DocumentWindow(app::Services services, core::DocumentHandle handle, const Recovery& recovery,
                               QWidget* parent)
    : QWidget(parent), services_(services), handle_(handle)
{
    sourcePath_ = recovery.sourcePath;
    path_ = recovery.untitled ? QString() : recovery.sourcePath;
    build(&recovery);
}

DocumentWindow::DocumentWindow(app::Services services, QString path, core::DocumentHandle handle,
                               DocumentWindow* leader, QWidget* parent)
    : QWidget(parent), services_(services), path_(std::move(path)), handle_(handle)
{
    sourcePath_ = path_;

    // A SECOND VIEW OF A SECOND VIEW IS STILL A SECOND VIEW OF THE DOCUMENT. Resolving to the
    // leader here rather than at the call site means there is one chain of length one instead of a
    // tree nobody walks, and it is why `leader()` is documented never to point at a companion.
    leader_ = (leader != nullptr && leader->leader_ != nullptr) ? leader->leader_ : leader;
    if (leader_ != nullptr) leader_->attachCompanion(this);

    build(nullptr);
}

void DocumentWindow::attachCompanion(DocumentWindow* companion)
{
    if (companion == nullptr || companions_.contains(companion)) return;
    companions_.append(companion);

    // THE LEADER'S MUTATIONS ARE THE COMPANION'S TOO. `mutated` is emitted after every command,
    // undo and redo, and the revision is part of every tile key (D2-12) -- so a companion that was
    // not told would go on showing the un-rotated page with no error and nothing to fail. This is
    // `afterMutation`'s fan-out over `split_->views()` reaching one window further out.
    connect(this, &DocumentWindow::mutated, companion, [this, companion] {
        for (PageView* pane : companion->split_->views()) pane->modelChanged(revision());
    });
}

void DocumentWindow::unlinkFromLeader()
{
    if (leader_ == nullptr) return;
    disconnect(leader_, nullptr, this, nullptr);
    leader_->companions_.removeAll(this);
    leader_ = nullptr;
}

void DocumentWindow::detachFromLeader()
{
    if (leader_ == nullptr) return;
    unlinkFromLeader();

    // BACK ONTO ITS OWN HANDLE'S TREE, and this is the correct answer rather than a fallback: the
    // leader's disposition has just been settled, and what this tab's own `DocumentHandle` has been
    // rasterizing all along is the file. See the constructor's comment.
    revision_ = core::Revision::initial();
    for (PageView* pane : split_->views()) handDocumentTo(pane);
}

const core::PageTree* DocumentWindow::modelPages() const
{
    if (leader_ != nullptr) return leader_->modelPages();
    return journal_ ? &journal_->document().pages() : services_.backend.pages(handle_);
}

bool DocumentWindow::synchronizedWithOtherWindows() const noexcept
{
    return leader_ != nullptr ? leader_->syncWithOtherWindows_ : syncWithOtherWindows_;
}

void DocumentWindow::setSynchronizedWithOtherWindows(bool on)
{
    if (leader_ != nullptr) {
        leader_->setSynchronizedWithOtherWindows(on);
        return;
    }
    syncWithOtherWindows_ = on;
}

void DocumentWindow::setBackgroundWindow(bool background)
{
    for (PageView* pane : split_->views()) pane->setBackgroundWindow(background);
}

void DocumentWindow::build(const Recovery* recovery)
{
    setObjectName(QStringLiteral("documentWindow"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // THE PAGE SURFACE AND ITS TWO RULERS, in a grid: the corner box, the horizontal ruler above
    // the view and the vertical ruler beside it (S4 T8). A grid rather than nested boxes because
    // the rulers must share the view's COLUMN and ROW exactly -- a ruler one pixel wider than the
    // surface it measures reads the wrong number everywhere, and only a grid guarantees that
    // without arithmetic.
    auto* surface = new QWidget(this);
    auto* grid = new QGridLayout(surface);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    // THE SURFACE IS A SPLIT CONTAINER WITH ONE PANE IN IT (S4 T9, D4-13), not a `PageView`. A
    // document that is never split is one view inside one container and costs a widget; the
    // alternative -- swapping a `PageView` for a container the first time someone splits -- would
    // put a second construction path under the document, which is where the difference between
    // "the pane a document opened into" and "a pane a split made" gets in.
    split_ = new SplitViewContainer(services_.renderScheduler,
                                    services_.platform.displayTopology.get(), surface);
    horizontalRuler_ = new RulerWidget(Qt::Horizontal, surface);
    verticalRuler_ = new RulerWidget(Qt::Vertical, surface);

    // The corner box names the unit ONCE, so the ticks carry numbers rather than five characters
    // of "mm" apiece. A label and not a button: the unit is `units/rulerUnit` and its surface is
    // the Units & Guides Preferences page, which convention 17 generates from the row -- a second
    // control for it here would be a second place to keep in agreement.
    rulerCorner_ = new QLabel(surface);
    rulerCorner_->setAlignment(Qt::AlignCenter);
    rulerCorner_->setFixedSize(RulerWidget::kThickness, RulerWidget::kThickness);

    grid->addWidget(rulerCorner_, 0, 0);
    grid->addWidget(horizontalRuler_, 0, 1);
    grid->addWidget(verticalRuler_, 1, 0);
    grid->addWidget(split_, 1, 1);
    grid->setRowStretch(1, 1);
    grid->setColumnStretch(1, 1);
    column->addWidget(surface);

    // THE THREE THINGS A SPLIT CHANGES ABOUT THIS CLASS (S4 T9). A pane the container adds is
    // adopted here and nowhere else; a pane it retires stops being the bound one; and the rulers
    // follow whichever pane has focus.
    connect(split_, &SplitViewContainer::viewAdded, this, [this](PageView* view) { adoptView(view); });
    connect(split_, &SplitViewContainer::viewRetired, this, [this](PageView* view) {
        if (view == boundView_) unbindActiveView();
    });
    connect(split_, &SplitViewContainer::activeViewChanged, this, [this](PageView* view) {
        bindActiveView(view);
        updateRulerMappings();
        emit activeViewChanged(view);
    });

    // S4 T11. Forwarded rather than consumed: the cross-window mirror is `ui/shell/WindowArranger`'s
    // and a tab may not name another window.
    connect(split_, &SplitViewContainer::scrolled, this,
            [this](PageView* pane) { emit scrolled(pane); });

    bindActiveView(split_->primaryView());

    for (RulerWidget* ruler : {horizontalRuler_, verticalRuler_}) {
        connect(ruler, &RulerWidget::contextMenuRequested, this,
                [this](QPoint global) { raiseGuideMenu(-1, global); });

        // THE DRAG IS TRACKED ON THE RULER AND DROPPED ON THE PAGE. Qt keeps the mouse grab where
        // the press landed, so the moves arrive here in global coordinates and this is the only
        // object that can map them onto the surface. The marker on that ruler is the feedback
        // during the drag -- without it a guide appears from nowhere on release.
        connect(ruler, &RulerWidget::guideDragged, this,
                [this](Qt::Orientation orientation, QPoint global) {
                    const QPoint local = view()->viewport()->mapFromGlobal(global);
                    const QPoint offset = viewportOffset();
                    if (orientation == Qt::Horizontal)
                        horizontalRuler_->setCursorPixel(local.x() + offset.x());
                    else
                        verticalRuler_->setCursorPixel(local.y() + offset.y());
                });

        connect(ruler, &RulerWidget::guideDropped, this,
                [this](Qt::Orientation orientation, QPoint global) {
                    // THE ACTIVE PANE, WHICH IS THE ONE THE RULERS MEASURE (S4 T9). A drop over
                    // another pane of a split therefore lands outside this one's viewport and is
                    // refused below, which is the same answer as a drop over the scrollbars: a
                    // guide is only made where the ruler that made it is reading.
                    PageView* target = view();
                    const QPoint local = target->viewport()->mapFromGlobal(global);

                    // A DROP THAT IS NOT ON THE SURFACE MAKES NO GUIDE -- a press on the ruler
                    // that goes nowhere is how a person changes their mind, and `dropGuide`
                    // refuses a point off the page for the same reason.
                    if (target->viewport()->rect().contains(local))
                        target->guides().dropGuide(target->layoutEngine().layout(),
                                                   target->toContentPoint(local), orientation);
                });
    }

    applyGuideSettings();

    // THE PREFERENCES BEFORE THE DOCUMENT. `layoutMode` and `defaultZoom` are applied only on the
    // first `applySettings` call -- they are what a document OPENS at -- so they have to be in place
    // before there is a document to open at them.
    applyViewSettings();

    // THE DOCUMENT AND ITS JOURNAL (Stage 3 T10). Hydrated from the backend -- a copy of its page tree, so
    // a rotation changes the model and never the engine's reading (D3-15). A handle the backend does not
    // know hydrates nothing, and the view draws nothing.
    // A COMPANION HYDRATES NOTHING (S4 T11). No journal, no recovery journal, no coordinator --
    // the constructor's comment argues why two journals over one file is the defect rather than
    // the feature. Its panes are handed the LEADER'S tree below, through `modelPages()`.
    if (handle_.isValid() && leader_ == nullptr) {
        core::Document document;
        if (document.hydrate(services_.backend, handle_)) {
            journal_ = std::make_unique<core::CommandJournal>(std::move(document), services_.backend.mutationSink(handle_));
            coordinator_ = std::make_unique<core::SaveCoordinator>(services_.backend, services_.platform.durableFiles.get());

            // THE CRASHED SESSION, REPLAYED FIRST (Stage 3 T11): no recorder yet, because replay refuses a
            // recording journal and the replayed lines are handed to the new recovery journal whole; no
            // subscription yet, because nothing is on screen to update.
            QByteArray seedLines;
            if (recovery) {
                core::Result<core::ReplayReport> replayed = core::RecoveryJournal::replay(recovery->directory, *journal_);
                if (replayed) {
                    recoveryReport_ = replayed.value();
                    seedLines = recoveryReport_->appliedLines;
                } else {
                    recoveryFailure_ = replayed.error();
                }
            }

            if (isEditable()) {
                // `documents/recoveryDir` first (Stage 3 T12), read when the document opens: an open document
                // keeps the journal it has when the setting changes.
                const QString root = core::RecoveryJournal::resolveRoot(
                    services_.settings.get(core::keys::documents::recoveryDir), services_.recoveryRoot);
                recovery_ = core::RecoveryJournal::open(root, sourcePath_, journal_->document().id(),
                                                        services_.platform.durableFiles.get(), seedLines, isUntitled());
                journal_->setRecorder(recovery_.get());
            }
            journal_->subscribeMutated([this](const core::Scope& scope) {
                services_.renderScheduler.documentChanged(handle_, scope, journal_->revision());
                afterMutation();
            });
            journal_->subscribeDirty([this](bool dirty) { emit dirtyChanged(dirty); });
        }
    }

    // The MODEL's pages for layout, the ENGINE's for the delta each tile carries (D3-15). EVERY
    // pane, because `adoptView` routes a pane a split adds back through the same two lines and a
    // second spelling of them here is a second reading of D3-15 to keep in agreement.
    for (PageView* pane : split_->views()) handDocumentTo(pane);

    // S2 T15. A `pageDisplay/` change reaches every open tab, not only the one in front: a person
    // who turns off image smoothing expects it off everywhere, and a tab that kept the old value
    // until it was next touched would be a preference that appears not to have applied.
    settingsSubscription_ = services_.settings.subscribe([this](const core::Settings::Change& change) {
        if (change.group == core::SettingsGroup::PageDisplay
            || change.group == core::SettingsGroup::PerformanceAndCache) {
            applyViewSettings();
        }

        // S4 T8, and it reaches every open tab for the reason above: a person who turns the grid
        // on expects it on, not on in the tab they happened to be looking at.
        if (change.group == core::SettingsGroup::UnitsAndGuides) applyGuideSettings();
    });
}

PageView* DocumentWindow::view() const noexcept
{
    return split_ ? split_->activeView() : nullptr;
}

void DocumentWindow::handDocumentTo(PageView* pane)
{
    // S4 T12 (D4-14). The content box is the one thing on `IDocumentBackend` that is computed per
    // page on demand, so it reaches the view as a function rather than as a pointer to something
    // already read. `handle_` is captured by VALUE and the backend by reference, which is safe for
    // the same reason every other use of `services_` here is: the services outlive every window,
    // and the handle is this tab's own and is closed by this tab's destructor.
    //
    // `NotFound` -- a page with no objects -- becomes `std::nullopt`, and `core/render/FitPolicy`
    // falls back to the CropBox. The error is not logged: an empty page is an ordinary document,
    // not a defect, and a log line per fit would be a log nobody reads.
    core::IDocumentBackend& backend = services_.backend;
    const core::DocumentHandle handle = handle_;
    pane->setContentBoxSource([&backend, handle](int pageIndex) -> std::optional<core::PdfRect> {
        const core::Result<core::PdfRect> box = backend.contentBox(handle, pageIndex);
        if (!box) return std::nullopt;
        return box.value();
    });

    // The MODEL's pages for layout, the ENGINE's for the delta each tile carries (D3-15). For a
    // COMPANION (S4 T11) the model is the LEADER'S and the engine tree is this tab's own handle's,
    // which is the same split one window further out: the leader's unsaved rotations decide the
    // geometry, this handle's pages decide what the rasterizer is asked for, and the difference
    // rides on the request.
    if (leader_ != nullptr) {
        pane->setDocument(handle_, leader_->revision(), modelPages(), services_.backend.pages(handle_));
        return;
    }
    if (journal_) {
        pane->setDocument(handle_, journal_->revision(), &journal_->document().pages(),
                          services_.backend.pages(handle_));
    } else {
        pane->setDocument(handle_, revision_, services_.backend.pages(handle_));
    }
}

void DocumentWindow::adoptView(PageView* pane)
{
    // THE PREFERENCES BEFORE THE DOCUMENT, which is the order `build()` uses and for the reason it
    // states: `layoutMode` and `defaultZoom` are applied only on a view's FIRST `applySettings`,
    // so they have to be in place before there is a document to open at them.
    applyViewSettings();
    applyGuideSettings();
    handDocumentTo(pane);

    // AND THEN IT IS PUT WHERE THE PERSON IS LOOKING. `setDocument` starts a view at page 1 with
    // no zoom of its own -- it is a document ARRIVING -- so a pane adopted and left there would
    // send half the window back to the top of the file the moment anyone split it. The source is
    // the primary rather than the active pane because the active one may be this very pane: the
    // container makes a view active as soon as it takes focus.
    PageView* source = split_->primaryView();
    if (pane == source) return;

    pane->setLayoutMode(source->controller().layoutMode());
    pane->controller().setViewRotation(source->controller().viewRotation());
    pane->setFitMode(source->controller().fitMode());
    pane->setZoom(source->controller().zoom());
    pane->controller().setCurrentPage(source->controller().currentPage());
    pane->controller().setScroll(source->controller().scroll());

    // THE GUIDES ARE THE PAGE'S, NOT THE PANE'S (D4-7). `core::Guide::position` is in PDF points in
    // page space, so the same guide is in the same place in both halves of a split -- and a
    // `GridGuideLayer` per view is a per-view DRAG, which is what `PageView`'s header said a split
    // would need, not a per-view guide list.
    pane->guides().setGuides(source->guides().guides());
    pane->refresh();
}

void DocumentWindow::bindActiveView(PageView* pane)
{
    if (pane == boundView_) return;
    unbindActiveView();
    if (pane == nullptr) return;
    boundView_ = pane;

    // A SCROLL EMITS THIS TOO, which is what makes the rulers follow a drag: `scrollContentsBy`
    // writes the controller, the controller's `changed` reaches `refresh()`, and `refresh()` emits
    // `viewChanged`. A connection to a scrollbar signal instead would miss every scroll that came
    // from a key, a wheel or the hand tool.
    connect(pane, &PageView::viewChanged, this, [this](int, int, double) { updateRulerMappings(); });

    connect(pane, &PageView::cursorMoved, this, [this](QPoint position, bool onSurface) {
        const QPoint offset = viewportOffset();
        horizontalRuler_->setCursorPixel(onSurface ? position.x() + offset.x() : -1.0);
        verticalRuler_->setCursorPixel(onSurface ? position.y() + offset.y() : -1.0);
    });

    connect(pane, &PageView::guideContextMenuRequested, this,
            [this](int guideIndex, QPoint global) { raiseGuideMenu(guideIndex, global); });

    // AND THE GUIDES FOLLOW THE DOCUMENT RATHER THAN THE PANE (D4-7, S4 T9). Every gesture that
    // changes a guide -- a drag off a ruler, a move, a lock, a delete, `view.clearGuides` -- runs
    // on the ACTIVE pane, because focus is what makes a pane active and all five need the pointer
    // or the focus to be in it. So mirroring from the bound pane covers every one of them, and the
    // followers are not connected, which is what makes the copy unable to echo back.
    connect(&pane->guides(), &GridGuideLayer::changed, this, [this] { mirrorGuides(); });
}

void DocumentWindow::mirrorGuides()
{
    PageView* source = view();
    if (source == nullptr) return;
    for (PageView* pane : split_->views())
        if (pane != source) pane->guides().setGuides(source->guides().guides());
}

void DocumentWindow::unbindActiveView()
{
    // Convention 10 (Stage 1's I-12), and `QEvent::Leave` is what makes it real rather than
    // theoretical: Qt delivers it to a widget under the pointer as it goes away, `PageView` turns
    // it into `cursorMoved`, and the slot would then ask a half-destroyed window for its rulers.
    // A retired pane of a split reaches this the same way a destructor does.
    //
    // TWO OBJECTS, NOT ONE: the layer is a CHILD of the view and a separate `QObject`, so
    // disconnecting the view leaves the guide connection standing. It was found by reading rather
    // than by a failure, which is what convention 10 is for.
    if (boundView_ != nullptr) {
        disconnect(&boundView_->guides(), nullptr, this, nullptr);
        disconnect(boundView_, nullptr, this, nullptr);
    }
    boundView_ = nullptr;
}

void DocumentWindow::applyViewSettings()
{
    if (!split_) return;

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
    view.autoScrollSpeed   = settings.get(core::keys::pageDisplay::autoScrollSpeed);
    view.prefetchPages     = settings.get(core::keys::performance::prefetchPages);

    // EVERY PANE OF THE SPLIT (S4 T9). The eleven `pageDisplay/` keys are the document's, not one
    // pane's: a person who turns off image smoothing expects it off in both halves of a split for
    // exactly the reason S2 T15 gives about it reaching every open tab.
    for (PageView* pane : split_->views()) pane->applySettings(view);
}

void DocumentWindow::applyGuideSettings()
{
    if (!split_) return;

    const core::Settings& settings = services_.settings;

    GuideSettings guides;
    guides.unit             = settings.get(core::keys::units::rulerUnit);
    guides.showRulers       = settings.get(core::keys::units::showRulers);
    guides.showGrid         = settings.get(core::keys::units::showGrid);
    guides.showGuides       = settings.get(core::keys::units::showGuides);
    guides.gridSpacing      = settings.get(core::keys::units::gridSpacing);
    guides.gridSubdivisions = settings.get(core::keys::units::gridSubdivisions);
    guides.gridColour       = settings.get(core::keys::units::gridColour);
    guides.gridOriginX      = settings.get(core::keys::units::gridOriginX);
    guides.gridOriginY      = settings.get(core::keys::units::gridOriginY);
    guides.snapToGrid       = settings.get(core::keys::units::snapToGrid);
    guides.snapToGuides     = settings.get(core::keys::units::snapToGuides);

    for (PageView* pane : split_->views()) pane->applyGuideSettings(guides);

    horizontalRuler_->setUnit(guides.unit);
    verticalRuler_->setUnit(guides.unit);
    horizontalRuler_->setVisible(guides.showRulers);
    verticalRuler_->setVisible(guides.showRulers);
    rulerCorner_->setVisible(guides.showRulers);

    // The unit's SYMBOL, not a translated word: "mm" is "mm" in every locale `Features.md` §9.1
    // lists, and `core/Units.h` says why treating one as a string to translate is an invitation to
    // translate it wrongly.
    rulerCorner_->setText(QString::fromUtf8(core::unitSuffix(guides.unit)));

    updateRulerMappings();
}

QPoint DocumentWindow::viewportOffset() const
{
    // MAPPED TO THE CONTAINER, not to the view. The rulers share the container's row and column
    // in the grid, so with one pane this is the scroll area's frame inset and with a split it is
    // that PLUS the pane's own position -- which is the number the ruler needs in both cases.
    PageView* active = view();
    return active ? active->viewport()->mapTo(split_, QPoint(0, 0)) : QPoint(0, 0);
}

void DocumentWindow::updateRulerMappings()
{
    PageView* view_ = view();
    if (!view_ || !horizontalRuler_) return;

    // `isHidden()`, NOT `isVisible()`. A widget whose ancestors are not on screen yet is not
    // *visible*, and this runs during `build()` -- before the tab is in the window and before the
    // window is shown. Asking `isVisible()` skipped every mapping until something else changed the
    // view, so the rulers came up blank and a drag off one produced nothing (found by
    // `tst_rulers`'s first case, which is why it drags rather than calling `dropGuide`).
    if (horizontalRuler_->isHidden() && verticalRuler_->isHidden()) return;

    const core::render::PageLayout& layout = view_->layoutEngine().layout();
    const core::DeviceRect visible = view_->controller().visibleRect();
    const double dpr = view_->devicePixelRatioF() > 0.0 ? view_->devicePixelRatioF() : 1.0;
    const QPoint offset = viewportOffset();

    // THE PAGE IN FRONT OF THE PERSON, falling back to the first one laid out. A ruler measured
    // from a page that is off screen would put its zero somewhere nobody can see, which is worse
    // than measuring the wrong one of two visible pages.
    const core::render::PageSlot* slot = layout.slotForPage(view_->controller().currentPage());
    if (slot == nullptr && !layout.pageSlots().empty()) slot = &layout.pageSlots().front();

    RulerWidget::Mapping horizontal;
    RulerWidget::Mapping vertical;

    if (slot != nullptr && layout.presentedScale() > 0.0) {
        // LOGICAL pixels per PDF point. `presentedScale()` is DEVICE pixels per point, and the
        // ruler paints in the same logical coordinates its mouse events arrive in -- a mapping
        // that skipped this division is correct at 100 % and two thirds of the way along at 150 %.
        const double pixelsPerPoint = layout.presentedScale() / dpr;

        horizontal.valid = true;
        horizontal.pixelsPerPoint = pixelsPerPoint;
        horizontal.zeroPixel = offset.x() + (slot->rect.left() - visible.left()) / dpr;
        horizontal.extentPoints = slot->rect.size.width / layout.presentedScale();

        vertical.valid = true;
        vertical.pixelsPerPoint = pixelsPerPoint;
        vertical.zeroPixel = offset.y() + (slot->rect.top() - visible.top()) / dpr;
        vertical.extentPoints = slot->rect.size.height / layout.presentedScale();
    }

    horizontalRuler_->setMapping(horizontal);
    verticalRuler_->setMapping(vertical);
}

void DocumentWindow::raiseGuideMenu(int guideIndex, QPoint global)
{
    PageView* view_ = view();
    if (!view_) return;

    // THE TARGET IS RECORDED BEFORE THE MENU EXISTS, because the commands in it take no argument
    // and will ask the view which guide they are about (I-50's rule). -1 is a ruler click, and it
    // clears the target rather than leaving the last one standing.
    view_->guides().setContextGuide(guideIndex);
    emit guideMenuRequested(guideIndex, global);
}

DocumentWindow::~DocumentWindow()
{
    // BEFORE ANYTHING ELSE. `Settings` belongs to the composition root and outlives every tab, so a
    // subscription left behind is a call into a destroyed widget on the next preference change --
    // convention 10, in the form where the caller is a settings store rather than a signal.
    if (settingsSubscription_ != 0) services_.settings.unsubscribe(settingsSubscription_);

    // EVERY SECOND VIEW OF THIS DOCUMENT IS CUT LOOSE FIRST (S4 T11). A companion lays out from
    // THIS tab's page tree, which `journal_` owns and which is destroyed at the end of this body --
    // so a companion still pointing at it would lay out over freed memory on its next paint. It is
    // detached rather than closed: the window it lives in is the user's, and the file its own
    // handle rasterizes is still there.
    //
    // `detachFromLeader` mutates `companions_`, so the list is copied rather than walked.
    for (DocumentWindow* companion : QList<DocumentWindow*>(companions_)) companion->detachFromLeader();

    // AND THIS TAB, IF IT IS ITSELF A COMPANION, is only UNLINKED: re-handing a document to panes
    // that are about to be destroyed would start a round of tile requests for a widget nobody will
    // ever paint, which is the difference between the two functions.
    unlinkFromLeader();

    // AND THE CHILDREN'S SIGNALS, which is convention 10 in its original form (Stage 1's I-12): a
    // lambda bound to a CHILD's signal and holding `this` outlives the derived object, because
    // `~QWidget` deletes the children after this body has run. `QEvent::Leave` is the one that
    // makes it real rather than theoretical here -- Qt delivers it to a widget under the pointer
    // as it goes away, `PageView` turns it into `cursorMoved`, and the slot would then ask a
    // half-destroyed window for its rulers.
    unbindActiveView();
    if (split_) disconnect(split_, nullptr, this, nullptr);
    if (horizontalRuler_) disconnect(horizontalRuler_, nullptr, this, nullptr);
    if (verticalRuler_) disconnect(verticalRuler_, nullptr, this, nullptr);

    // NOTHING UNSAVED, NOTHING TO RECOVER. A document closed clean takes its recovery directory with it;
    // one closed dirty -- which only a Discard or a process ending reaches -- keeps it for the next
    // launch unless the Discard already removed it (D3-14).
    if (journal_ && recovery_) {
        journal_->setRecorder(nullptr);
        if (!journal_->isDirty())
            recovery_->discard();
    }

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

core::Revision DocumentWindow::revision() const noexcept
{
    // A COMPANION IS AT ITS LEADER'S REVISION (S4 T11), because it is laying the leader's model out:
    // an answer of its own would key its tiles differently from the tree they were laid out from,
    // which is D2-12's failure with nothing on screen to show for it.
    if (leader_ != nullptr) return leader_->revision();
    return journal_ ? journal_->revision() : revision_;
}

bool DocumentWindow::isEditable() const
{
    return journal_ && services_.backend.mutationSink(handle_) != nullptr
           && journal_->document().permissions().canEditPages();
}

bool DocumentWindow::isDirty() const
{
    return journal_ && journal_->isDirty();
}

void DocumentWindow::afterMutation()
{
    // EVERY PANE, AT THE SAME REVISION (S4 T9, guide case A-54). A pane that was not told would go
    // on showing the un-rotated page with no error and no failing test, because the revision is
    // part of every tile's key -- D2-12's argument, arriving at its third consumer after the
    // thumbnails pane.
    for (PageView* pane : split_->views()) pane->modelChanged(journal_->revision());
    if (recovery_ && !recovery_->isRecoverable() && !recoveryReported_) {
        recoveryReported_ = true;
        emit recoveryUnavailable(catalog::translateError(QString::fromLatin1(recovery_->unrecoverableKey()),
                                                         recovery_->unrecoverableContext()));
    }
    emit mutated();
}

core::Result<core::SaveOutcome> DocumentWindow::save()
{
    if (!journal_ || !coordinator_)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), path_};
    if (isUntitled()) {
        // NO FILE OF ITS OWN, SO NOTHING TO SAVE IN PLACE (Stage 3 T11). Its source is not its file: writing
        // there would be the very write "recover to a new file" promised not to make.
        core::SaveOutcome outcome;
        outcome.kind = core::SaveOutcome::Kind::SaveAsRequired;
        outcome.reasonKey = QLatin1String("error.save.untitled");
        return outcome;
    }
    return coordinator_->save(*journal_, path_);
}

core::Result<core::SaveOutcome> DocumentWindow::saveAs(const QString& target)
{
    if (!journal_ || !coordinator_)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), target};
    auto result = coordinator_->saveAs(*journal_, target);
    if (result && result.value().kind == core::SaveOutcome::Kind::Saved) {
        path_ = QFileInfo(target).absoluteFilePath();
        sourcePath_ = path_;                     // an untitled document now has a file of its own
    }
    return result;
}

QString DocumentWindow::suggestedSavePath() const
{
    if (!isUntitled())
        return path_;
    const QFileInfo source(sourcePath_);
    const QString name = catalog::translate(catalog::key::RecoverySuggestedName).arg(source.completeBaseName());
    return source.dir().filePath(name + QLatin1String(".pdf"));
}

void DocumentWindow::discardRecovery()
{
    if (!recovery_)
        return;
    if (journal_)
        journal_->setRecorder(nullptr);
    recovery_->discard();
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
