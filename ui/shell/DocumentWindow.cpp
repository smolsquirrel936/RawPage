#include "ui/shell/DocumentWindow.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/settings/Settings.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "platform/platform.h"
#include "ui/shell/Catalog.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/GridGuideLayer.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/viewer/ReflowView.h"
#include "ui/viewer/RulerWidget.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/write/FindReplaceBar.h"
#include "core/text/DocumentIndex.h"
#include "ui/viewer/SplitViewContainer.h"

#include <QAbstractButton>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>

#include <optional>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;

// How long the destructor will wait for a tile that is already inside the engine. One tile is
// measured at p95 0.5 ms (A2's probe), so this is three orders of magnitude of headroom and is a
// bound rather than an expectation -- a closing tab that hung on a wedged worker would be a
// product that cannot be quit.
constexpr int kDrainBudgetMs = 2000;

// The rulers' corner box: a button that DRAWS LIKE THE LABEL IT REPLACED (2026-10-03). Not a
// `QToolButton`, measured: on the `windows` platform its style pads the text 8 pixels a side --
// sizeHint 37 pixels wide for "mm" against a 22-pixel box -- and a text that does not fit is ELIDED,
// so "mm" read "..." while "in" fitted. The text here is centred over the whole box with no margin,
// which is what the label did ("mm" is 21 pixels in the default font at 100 %). Pressed and hovered
// tint the box with the palette's highlight, so it follows the live theme like the rulers beside it
// (conventions 20 and 21); the text stays `WindowText`, the rulers' own numbers' colour.
class RulerCornerButton final : public QAbstractButton {
public:
    explicit RulerCornerButton(QWidget* parent) : QAbstractButton(parent)
    {
        setAttribute(Qt::WA_Hover);   // repaint on enter and leave, for the hover tint
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override { return {RulerWidget::kThickness, RulerWidget::kThickness}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const QPalette& colours = palette();
        if (isDown() || underMouse()) {
            QColor tint = colours.color(QPalette::Highlight);
            tint.setAlpha(isDown() ? 110 : 55);
            painter.fillRect(rect(), tint);
        }
        painter.setPen(colours.color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignCenter, text());
    }
};

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

void DocumentWindow::presentSearchHits(const QString& pattern, const std::vector<core::text::SearchHit>& hits,
                                       int current)
{
    if (findBar_ != nullptr) findBar_->presentHits(pattern, hits, current);
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

    // AND ITS ENGINE DRAWS THE LEADER'S EDITS (S6 T4, D6-2): the companion rasterizes through a handle
    // of its own, so an unsaved annotation reaches its pixels only if that engine is shown the same
    // revision. Nothing to show while the leader has nothing unsaved.
    if (journal_ && journal_->isDirty() && companion->handle_.isValid()) {
        if (const core::Status shown = services_.backend.showEdits(handle_, {companion->handle_}); !shown) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Render)
                << "window: the second view of" << path_ << "cannot show its unsaved edits --"
                << shown.error().messageKey;
        }
    }
}

core::Status DocumentWindow::showEdits()
{
    std::vector<core::DocumentHandle> viewers;
    for (const DocumentWindow* companion : companions_)
        if (companion->handle_.isValid()) viewers.push_back(companion->handle_);
    return services_.backend.showEdits(handle_, viewers);
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
    // ITS ENGINE BACK ON THE FILE (S6 T4, D6-2): the leader's unsaved edits went with the leader, and
    // its own handle has none -- no work when it was never shown any.
    if (handle_.isValid()) {
        if (const core::Status shown = services_.backend.showEdits(handle_, {}); !shown) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Render)
                << "window: the detached view of" << path_ << "could not be put back on its file --"
                << shown.error().messageKey;
        }
    }
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

    // THE SELECTION BEFORE ANY PANE IS HANDED A DOCUMENT (S5 T5): `handDocumentTo` gives it to each pane,
    // and the primary pane exists already. Its reports are forwarded as this tab's own signals.
    selection_ = new SelectionOverlay(services_.documentIndex, this);
    connect(selection_, &SelectionOverlay::copyProgress, this, &DocumentWindow::selectionCopyProgress);
    connect(selection_, &SelectionOverlay::copied, this, &DocumentWindow::selectionCopied);
    connect(selection_, &SelectionOverlay::copyNotExtractable, this, &DocumentWindow::selectionNotExtractable);
    connect(selection_, &SelectionOverlay::copyCancelled, this, &DocumentWindow::selectionCopyCancelled);
    connect(selection_, &SelectionOverlay::presenceChanged, this, &DocumentWindow::selectionPresenceChanged);

    // THE ANNOTATIONS (S6 T5): one overlay for every pane, given its store and its runner once the journal
    // exists (below). A refusal is said in the person's words; a selection re-asks the chrome and the bar.
    annotations_ = new AnnotationOverlay(this);
    connect(annotations_, &AnnotationOverlay::refused, this, [this](const core::Error& error) {
        emit annotationRefused(catalog::translateError(QString(error.messageKey), error.context));
    });
    connect(annotations_, &AnnotationOverlay::selectionChanged, this, [this] {
        refreshPropertiesBar();
        emit annotationStateChanged();
    });
    horizontalRuler_ = new RulerWidget(Qt::Horizontal, surface);
    verticalRuler_ = new RulerWidget(Qt::Vertical, surface);

    // The corner box names the unit ONCE, so the ticks carry numbers rather than five characters
    // of "mm" apiece. **A BUTTON since 2026-10-03, on the user's request**: a press dispatches
    // `view.cycleRulerUnit`, which writes `units/rulerUnit` and nothing else. It was a label until
    // then so that it would not be a second place to keep in agreement with the Units & Guides
    // page; it still is not one, because it holds no state -- `applyGuideSettings` sets its text
    // from the row, whoever changed it. Through the registry rather than straight to the setting,
    // for `SidebarDock`'s close button's reason: the tool finder and the batch runner reach the
    // command, and a button with its own handler would be a behaviour they could never share.
    // NOT a default action (convention 23): the button shows the UNIT, not the command's label.
    // NO FOCUS, by the user's choice: a mouse convenience that adds no Tab stop to every tab; the
    // keyboard reaches the same command through the tool finder, and the unit through Preferences.
    rulerCorner_ = new RulerCornerButton(surface);
    rulerCorner_->setObjectName(QStringLiteral("rulerUnitButton"));
    rulerCorner_->setFocusPolicy(Qt::NoFocus);
    rulerCorner_->setToolTip(catalog::translateAction(core::ActionId::ViewCycleRulerUnit));
    rulerCorner_->setFixedSize(RulerWidget::kThickness, RulerWidget::kThickness);
    connect(rulerCorner_, &QAbstractButton::clicked, this, [this] {
        (void)services_.actions.dispatch(core::ActionId::ViewCycleRulerUnit, core::KeyContext::Viewer);
    });

    grid->addWidget(rulerCorner_, 0, 0);
    grid->addWidget(horizontalRuler_, 0, 1);
    grid->addWidget(verticalRuler_, 1, 0);
    grid->addWidget(split_, 1, 1);
    grid->setRowStretch(1, 1);
    grid->setColumnStretch(1, 1);
    column->addWidget(surface);
    surface_ = surface;

    // REFLOW VIEW (S5 T11, D5-16), beside the surface and hidden until `Ctrl+4`: the find bar stays above
    // both, so a search started in the reflow is the same search, and its hit takes the tab back to the page.
    reflow_ = new ReflowView(services_.documentIndex, this);
    reflow_->hide();
    column->addWidget(reflow_);

    // THE FIND BAR (S5 T7, D5-14), above the surface and hidden until `Ctrl+F`. One per tab: it searches
    // this tab's document and paints through this tab's selection overlay, which every pane of a split
    // shares -- so its hits show in every pane. The current hit is revealed in the pane a person is in.
    findBar_ = new FindReplaceBar(services_, this);
    column->insertWidget(0, findBar_);
    findBar_->setCurrentPageProvider([this] { return view() != nullptr ? view()->controller().currentPage() : 0; });

    // THE PROPERTIES BAR (S6 T5, D6-9), under the find bar and hidden until `Ctrl+E`. It collects; the overlay
    // constructs the one command each change means.
    propertiesBar_ = new PropertiesBar(this);
    propertiesBar_->hide();
    column->insertWidget(1, propertiesBar_);
    connect(propertiesBar_, &PropertiesBar::appearanceRequested, this,
            [this](const core::AnnotationAppearance& appearance) { (void)annotations_->setAppearance(appearance); });
    connect(propertiesBar_, &PropertiesBar::contentsRequested, this,
            [this](const QString& text) { (void)annotations_->setContents(text); });
    connect(propertiesBar_, &PropertiesBar::locksRequested, this,
            [this](std::uint32_t set, std::uint32_t clear) { (void)annotations_->setLocks(set, clear); });
    connect(propertiesBar_, &PropertiesBar::toolDefaultsChanged, this, [this](const AnnotationToolDefaults& defaults) {
        if (PageView* pane = view()) {
            if (const std::optional<AnnotationToolKind> armed = pane->tools().armedAnnotation())
                sessionToolDefaults(*armed) = defaults;
        }
    });
    // A HIT LEAVES REFLOW FIRST (S5 T11, D5-16): find does not run inside it, and a hit is a place ON A PAGE.
    connect(findBar_, &FindReplaceBar::hitActivated, this, [this](int page, core::PdfRect rect) {
        setReflowing(false);
        if (PageView* pane = view()) pane->revealPageRect(page, rect);
    });
    // A BOOKMARK HIT (S5 T8, D5-12) goes where the bookmark goes -- the Bookmarks pane's own path.
    connect(findBar_, &FindReplaceBar::destinationActivated, this, [this](const core::Destination& destination) {
        setReflowing(false);
        if (PageView* pane = view()) pane->goToDestination(destination);
    });
    connect(findBar_, &FindReplaceBar::closed, this, [this] {
        if (PageView* pane = view()) pane->setFocus(Qt::OtherFocusReason);
    });

    // THE THREE THINGS A SPLIT CHANGES ABOUT THIS CLASS (S4 T9). A pane the container adds is
    // adopted here and nowhere else; a pane it retires stops being the bound one; and the rulers
    // follow whichever pane has focus.
    connect(split_, &SplitViewContainer::viewAdded, this, [this](PageView* view) { adoptView(view); });
    connect(split_, &SplitViewContainer::viewRetired, this, [this](PageView* view) {
        if (view == boundView_) unbindActiveView();

        // And a frozen pane's own two connections (I-19), which no binding owns.
        disconnect(&view->guides(), nullptr, this, nullptr);
        disconnect(view, nullptr, this, nullptr);
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
                    PageView* target = view();
                    const QPoint local = target->viewport()->mapFromGlobal(global);
                    const QPoint offset = viewportOffset();
                    if (orientation == Qt::Horizontal)
                        horizontalRuler_->setCursorPixel(local.x() + offset.x());
                    else
                        verticalRuler_->setCursorPixel(local.y() + offset.y());

                    // AND A DOTTED GUIDE ON THE PAGE, where the release would put the real one --
                    // only over the surface, by the same rule the drop below applies.
                    if (target->viewport()->rect().contains(local))
                        target->guides().previewGuide(target->layoutEngine().layout(),
                                                      target->toContentPoint(local), orientation);
                    else
                        target->guides().clearPreview();
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
                    else
                        target->guides().clearPreview();
                });
    }

    applyGuideSettings();

    // THE PREFERENCES BEFORE THE DOCUMENT. `layoutMode` and `defaultZoom` are applied only on the
    // first `applySettings` call -- they are what a document OPENS at -- so they have to be in place
    // before there is a document to open at them.
    applyViewSettings();

    // THE DOCUMENT AND ITS JOURNAL (Stage 3 T10). Hydrated from the backend -- a copy of its page tree, so
    // a rotation changes the model and never the engine's page tree (D3-15); since S6 T4 the engine itself
    // is shown each edit (D6-2, the show step below). A handle the backend does not
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
                // THE ENGINE'S TREE places an annotation's rectangle in each tile, so only the tiles it
                // reaches are dropped (S6 T4): a request's delta is relative to that tree.
                services_.renderScheduler.documentChanged(handle_, scope, journal_->revision(),
                                                          services_.backend.pages(handle_));
                // THE TEXT GOES STALE WHERE IT IS KEPT (S5 T6, D5-4): the index drops the pages the scope
                // names, for this window and every companion reading the same entry, and the next search
                // re-extracts exactly those (guide A-64).
                services_.documentIndex.invalidate(indexEntry_, scope);
                afterMutation();
            });
            journal_->subscribeDirty([this](bool dirty) { emit dirtyChanged(dirty); });

            // THE OVERLAY READS THIS DOCUMENT'S STORE AND RUNS THROUGH THIS JOURNAL (S6 T5, D6-1).
            annotations_->setStore(&journal_->document().annotations());
            annotations_->setRunner([this](std::unique_ptr<core::Command> command) { return journal_->run(std::move(command)); });

            // WHAT THE SCREEN SHOWS IS WHAT SAVE WRITES (S6 T4, D6-2): every command, undo and redo is
            // composed, verified and handed to the engine before it counts -- one Save could not write
            // is refused when it is made. Set after the replay, which runs with no window to show; a
            // replayed session is then shown once, whole.
            journal_->setShowHandler([this](const core::Scope&) { return showEdits(); });
            if (journal_->position() > 0) {
                if (const core::Status shown = showEdits(); !shown) {
                    RAWPAGE_LOG_WARNING(LogSubsystem::Render)
                        << "recovery: the replayed session of" << path_ << "could not be shown --"
                        << shown.error().messageKey << shown.error().context;
                }
            }
        }
    }

    // The MODEL's pages for layout, the ENGINE's for the delta each tile carries (D3-15). EVERY
    // pane, because `adoptView` routes a pane a split adds back through the same two lines and a
    // second spelling of them here is a second reading of D3-15 to keep in agreement.
    //
    // THE TEXT INDEX (S5 T6, D5-4): this tab's handle is attached under the document's IDENTITY, so a
    // companion over the same file joins its leader's entry and extracts nothing the leader already has.
    // Text is in unrotated page space (D5-2), so whichever handle the index asks through extracts the same
    // text, and no model tree is needed to do it. The selection reads its pages from that entry.
    if (handle_.isValid())
        indexEntry_ = services_.documentIndex.attach(services_.backend.documentId(handle_), handle_, pageCount());
    selection_->setDocument(indexEntry_, pageCount());
    findBar_->setDocument(indexEntry_, pageCount(), selection_);
    reflow_->attach(indexEntry_, pageCount());
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

// ---------------------------------------------------------------------------------------------
// Reflow view (S5 T11, D5-16)
// ---------------------------------------------------------------------------------------------

bool DocumentWindow::isReflowing() const noexcept
{
    return reflow_ != nullptr && !reflow_->isHidden();
}

void DocumentWindow::setReflowing(bool on)
{
    if (reflow_ == nullptr || surface_ == nullptr || on == isReflowing()) return;
    PageView* pane = view();

    if (on) {
        if (pane == nullptr || pageCount() <= 0 || indexEntry_ == 0) return;
        // THE BLOCK AT THE TOP OF THE ACTIVE PANE: the page at its top edge, read where the pointer would read
        // it (`pagePointAt`) -- in the middle of the edge, so a two-up spread answers the page a reader is in
        // rather than the gutter -- and how far below that edge each point of the page is drawn. That is
        // linear in page space, so three points say it all, and it is said along the SCREEN's down: on a page
        // shown turned, the page-space height at the edge names no block at all (S5 guide I-06).
        const PagePoint top = pane->pagePointAt(QPoint(pane->viewport()->width() / 2, 0));
        const int page = top.isValid() ? top.page : pane->controller().currentPage();
        const std::optional<double> origin = pane->depthBelowTop(page, core::PdfPoint{0.0, 0.0});
        const std::optional<double> alongX = pane->depthBelowTop(page, core::PdfPoint{1.0, 0.0});
        const std::optional<double> alongY = pane->depthBelowTop(page, core::PdfPoint{0.0, 1.0});
        // A page not laid out has no edge to read: its first block.
        const core::text::DepthBelowEdge depth = origin && alongX && alongY
            ? core::text::DepthBelowEdge{*alongX - *origin, *alongY - *origin, *origin}
            : core::text::DepthBelowEdge::upright(1.0e9);
        reflow_->showAt(page, depth);
        surface_->hide();
        reflow_->show();
        reflow_->setFocus(Qt::OtherFocusReason);
        emit reflowChanged(true);
        return;
    }

    // BACK TO THE PAGE, AT THE REFLOW'S PLACE: the top of the block at the top of the reflow at the top of
    // the pane. A place still on its way -- the page's text not here yet -- goes to its page.
    const ReflowView::Place place = reflow_->place();
    // THE FOCUS FOLLOWS ONLY IF THE REFLOW HAD IT: a find hit leaves reflow while the person is still typing
    // in the find bar, and taking the focus from the field would send their next key to the page.
    const bool hadFocus = reflow_->hasFocus();
    reflow_->hide();
    surface_->show();
    if (pane != nullptr && place.page >= 0) {
        // THE BOX FROM THE REFLOW'S OWN TEXT: it holds its pages, and the index may have evicted this one.
        if (const std::optional<core::PdfRect> box = reflow_->blockBox(place)) {
            pane->putPageRectAtTop(place.page, *box);
        } else {
            pane->goToPage(place.page);
        }
    }
    if (pane != nullptr && hadFocus) pane->setFocus(Qt::OtherFocusReason);
    emit reflowChanged(false);
}

// ---------------------------------------------------------------------------------------------
// The split, as one string (S4 T13)
// ---------------------------------------------------------------------------------------------

namespace {

constexpr QLatin1String kSplitNone("none");
constexpr QLatin1String kSplitHorizontal("horizontal");
constexpr QLatin1String kSplitVertical("vertical");
constexpr QLatin1String kSplitSpreadsheet("spreadsheet");

QLatin1String splitModeName(SplitViewContainer::Mode mode)
{
    switch (mode) {
    case SplitViewContainer::Mode::Horizontal:  return kSplitHorizontal;
    case SplitViewContainer::Mode::Vertical:    return kSplitVertical;
    case SplitViewContainer::Mode::Spreadsheet: return kSplitSpreadsheet;
    case SplitViewContainer::Mode::None:        break;
    }
    return kSplitNone;
}

// No default in the switch above and none here: adding a fifth split mode must be a compiler
// warning in this file rather than a session that silently restores it as `None`.
std::optional<SplitViewContainer::Mode> splitModeFromName(QStringView name)
{
    if (name == kSplitHorizontal)  return SplitViewContainer::Mode::Horizontal;
    if (name == kSplitVertical)    return SplitViewContainer::Mode::Vertical;
    if (name == kSplitSpreadsheet) return SplitViewContainer::Mode::Spreadsheet;
    if (name == kSplitNone)        return SplitViewContainer::Mode::None;
    return std::nullopt;
}

} // namespace

QString DocumentWindow::splitState() const
{
    // An unsplit tab that scrolls its one pane in the ordinary way has nothing to say, and saying
    // nothing keeps the field out of the session file entirely for the common case.
    if (split_ == nullptr) return {};
    if (split_->mode() == SplitViewContainer::Mode::None && split_->synchronizedScrolling())
        return {};

    return QStringLiteral("mode=%1;sync=%2")
        .arg(splitModeName(split_->mode()))
        .arg(split_->synchronizedScrolling() ? 1 : 0);
}

void DocumentWindow::applySplitState(const QString& state)
{
    if (split_ == nullptr || state.isEmpty()) return;

    for (const QStringView part : QStringView(state).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype at = part.indexOf(QLatin1Char('='));
        if (at <= 0) continue;
        const QStringView key = part.left(at);
        const QStringView value = part.mid(at + 1);

        if (key == QLatin1String("mode")) {
            // An unknown mode leaves the tab unsplit rather than failing the restore: the same call
            // `SessionState` makes about an unknown layout-mode name, and for the same reason --
            // a small wrong value beats a large absent one.
            if (const std::optional<SplitViewContainer::Mode> mode = splitModeFromName(value))
                split_->setMode(*mode);
        } else if (key == QLatin1String("sync")) {
            split_->setSynchronizedScrolling(value == QLatin1String("1"));
        }
    }
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
    // ONE SELECTION FOR EVERY PANE (S5 T5, D5-8) -- and one selection of ANNOTATIONS (S6 T5). A pane's tool
    // decides what the bar shows, so a pane arming one re-asks it (a member slot: `UniqueConnection` is safe).
    pane->setSelectionOverlay(selection_);
    pane->setAnnotationOverlay(annotations_);
    connect(pane, &PageView::toolChanged, this, &DocumentWindow::refreshPropertiesBar, Qt::UniqueConnection);

    // EVERY PANE'S SNAPSHOT IS THIS TAB'S (S5 T10, D5-11), forwarded as the selection's copy is, so the one
    // `disconnect(document, ...)` `MainWindow` makes when a tab leaves it covers these too. Unique, because
    // a pane is handed the document again whenever the document is re-read; a member-to-signal connection,
    // never a lambda, which is where `Qt::UniqueConnection` is a debug-only fail-fast (S4 guide I-09).
    connect(pane, &PageView::snapshotCopied, this, &DocumentWindow::snapshotCopied, Qt::UniqueConnection);
    connect(pane, &PageView::snapshotFailed, this, &DocumentWindow::snapshotFailed, Qt::UniqueConnection);

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

    // A FROZEN PANE IS NEVER THE BOUND ONE (I-19): it takes no focus, so the active view stays the
    // body. But a guide is still under the pointer in it, and moving, locking or deleting that guide
    // there would otherwise change this pane alone -- so its guide edits and its guide menu are
    // listened to directly. `mirrorGuides`' guard is what stops the copies echoing between panes.
    if (split_->isFrozen(pane)) {
        connect(&pane->guides(), &GridGuideLayer::changed, this, [this, pane] { mirrorGuides(pane); });
        connect(pane, &PageView::guideContextMenuRequested, this,
                [this](int guideIndex, QPoint global) { raiseGuideMenu(guideIndex, global); });
    }
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
    connect(&pane->guides(), &GridGuideLayer::changed, this, [this, pane] { mirrorGuides(pane); });
}

void DocumentWindow::mirrorGuides(PageView* source)
{
    if (source == nullptr || mirroringGuides_) return;
    mirroringGuides_ = true;
    for (PageView* pane : split_->views())
        if (pane != source) pane->guides().setGuides(source->guides().guides());
    mirroringGuides_ = false;
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
    view.snapshotDpi       = settings.get(core::keys::pageDisplay::snapshotDpi);
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
    if (split_) {
        // A frozen pane's guide and menu connections (I-19) are bound to no active view, so the
        // unbind above does not reach them.
        for (PageView* pane : split_->views()) {
            disconnect(&pane->guides(), nullptr, this, nullptr);
            disconnect(pane, nullptr, this, nullptr);
        }
        disconnect(split_, nullptr, this, nullptr);
    }
    if (horizontalRuler_) disconnect(horizontalRuler_, nullptr, this, nullptr);
    if (verticalRuler_) disconnect(verticalRuler_, nullptr, this, nullptr);

    // THE FIND BAR FIRST (S5 T7): its search runs over this document's index entry and paints through the
    // selection overlay below, so it cancels and lets go before either does -- and its two signals are
    // lambdas holding `this` (convention 10).
    if (findBar_ != nullptr) {
        disconnect(findBar_, nullptr, this, nullptr);
        delete findBar_;
        findBar_ = nullptr;
    }

    // THE REFLOW TOO (S5 T11): it holds text requests on this document's index entry.
    if (reflow_ != nullptr) {
        delete reflow_;
        reflow_ = nullptr;
    }

    // THE ANNOTATIONS AND THEIR BAR (S6 T5): before the journal whose store the overlay reads goes with this
    // object's members, and with every pane let go of it first.
    if (propertiesBar_ != nullptr) {
        disconnect(propertiesBar_, nullptr, this, nullptr);
        delete propertiesBar_;
        propertiesBar_ = nullptr;
    }
    if (annotations_ != nullptr) {
        if (split_)
            for (PageView* pane : split_->views()) pane->setAnnotationOverlay(nullptr);
        disconnect(annotations_, nullptr, this, nullptr);
        delete annotations_;
        annotations_ = nullptr;
    }

    // THE SELECTION GOES BEFORE THE HANDLE CLOSES (S5 T5): it holds text tickets on this document and a
    // subscription to the scheduler, and every pane lets go of it first so none paints through a
    // pointer to a destroyed object.
    if (selection_ != nullptr) {
        if (split_)
            for (PageView* pane : split_->views()) pane->setSelectionOverlay(nullptr);
        delete selection_;
        selection_ = nullptr;
    }

    // AND THE HANDLE LEAVES THE TEXT INDEX (S5 T6) -- after the selection has let go of its asks, BEFORE the
    // scheduler's jobs on this handle are cancelled below: a request a companion's selection or a search is
    // waiting on is re-sent through the companion's handle here, where it would otherwise be thrown away
    // with this handle's jobs. The entry goes with its last handle.
    if (handle_.isValid()) services_.documentIndex.detach(handle_);
    indexEntry_ = 0;

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
    // S6 T5: an annotation the model no longer holds leaves the selection; the bar shows what it is now.
    if (annotations_ != nullptr) annotations_->modelChanged();
    refreshPropertiesBar();
    if (recovery_ && !recovery_->isRecoverable() && !recoveryReported_) {
        recoveryReported_ = true;
        emit recoveryUnavailable(catalog::translateError(QString::fromLatin1(recovery_->unrecoverableKey()),
                                                         recovery_->unrecoverableContext()));
    }
    emit mutated();
}

bool DocumentWindow::propertiesBarShown() const
{
    return propertiesBar_ != nullptr && !propertiesBar_->isHidden();
}

void DocumentWindow::togglePropertiesBar()
{
    if (propertiesBar_ == nullptr) return;
    propertiesBar_->setVisible(propertiesBar_->isHidden());
    refreshPropertiesBar();
    emit annotationStateChanged();
}

void DocumentWindow::refreshPropertiesBar()
{
    if (propertiesBar_ == nullptr || annotations_ == nullptr) return;
    if (annotations_->hasSelection()) {
        propertiesBar_->showSelection(annotations_->selected());
        return;
    }
    if (PageView* pane = view()) {
        if (const std::optional<AnnotationToolKind> armed = pane->tools().armedAnnotation()) {
            propertiesBar_->showToolDefaults(catalog::translateAction(armingAction(*armed)), sessionToolDefaults(*armed));
            return;
        }
    }
    propertiesBar_->showNothing();
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
