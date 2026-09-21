// The unit a document tab represents.
//
// ---------------------------------------------------------------------------------------------
// THE STUB IS GONE (Stage 2 T13). WHAT IT WAS FOR, AND WHAT REPLACED IT.
//
// At Stage 1 this class held a path and two labels and nothing else -- decision D1-2, whose
// argument was that the shell's genuinely hard parts (tear-off, re-dock, reorder, close, overflow,
// the Explorer -> single-instance -> `openFiles` handoff) are STRUCTURAL, cheap to get right before
// a real document hangs off them and expensive to reshape afterwards. The risk it accepted in
// return was that the stub would calcify.
//
// It did not. This class now holds exactly what `Architecture-FileMap.md` §3.13 said it would grow:
// the open document's handle, its revision, and the viewer -- and, from Stage 3 T10, the document's
// `CommandJournal`, its `RecoveryJournal` and its `SaveCoordinator`. **From Stage 4 T9 it holds the
// split container too**, which is the last of the two things the paragraph above said it did not;
// the pane set is the window's rather than the tab's and stayed with `ui/shell/SidebarDock` at T5.
//
// ---------------------------------------------------------------------------------------------
// ONE DOCUMENT, N VIEWS (Stage 4 T9, D4-13).
//
// The page surface is a `ui/viewer/SplitViewContainer` rather than a bare `PageView`, and the
// container owns between one and four of them over this tab's ONE handle, ONE `Document`, ONE
// `CommandJournal` and ONE revision. What that costs this class is that three things which used to
// name a single view now fan out over `split_->views()` -- the two settings groups and every
// mutation's `modelChanged` -- and one thing narrows to the ACTIVE view: the rulers, which measure
// the pane a person is in.
//
// `view()` therefore means *the active view*, which is what kept every caller of it -- the status
// bar, the twenty-five §8 Viewer commands, `MainWindow::currentView()` -- working unchanged.
//
// ---------------------------------------------------------------------------------------------
// THE JOURNAL (Stage 3 T10, D3-10, D3-11, D3-15).
//
// It is the only holder of a mutable `Document`: `ui/` sees `journal()->document()` as a const
// reference and changes the document by handing the journal a `Command`. Every mutation does three
// things here, in order: tells `RenderScheduler` the scope and the new revision (so a tile rendered
// before it is dropped and the rest are re-keyed), lays the view out again from the MODEL's page tree
// -- geometry before pixels, Stage 1's I-36 -- and says so, so the window can re-ask every action.
// The view is handed the engine's tree as well, and each tile request carries the model's rotation
// minus the engine's as its delta (D3-15).
//
// A document the backend refuses to edit still gets a journal -- its permissions refuse every run by
// key -- and no recovery journal, because nothing it does needs recovering.
//
// ---------------------------------------------------------------------------------------------
// A RECOVERED DOCUMENT (Stage 3 T11, D3-11).
//
// The second constructor is the first one plus a replay: a crashed session's journal is replayed into the
// freshly hydrated journal BEFORE a recorder or a subscription is attached -- replay refuses a recording
// journal, and a view laid out once per replayed step would be work nobody sees -- and the new recovery
// journal is then SEEDED with the lines replay applied, so a second crash loses nothing the first did not.
//
// A document recovered into a NEW FILE (its source changed, or it never had a file of its own) is
// UNTITLED: its bytes came from `sourcePath()`, its `path()` is empty, Save goes to Save As, and its
// recovery meta says `untitled` until the first successful save gives it a file.
//
// ---------------------------------------------------------------------------------------------
// IT OWNS THE HANDLE, AND CLOSING IT IS A THREE-STEP PROTOCOL RATHER THAN A CALL.
//
// A `DocumentHandle` is the backend's; this class is what closes it, because the tab is what the
// user closes and a tab torn off into another window takes its document with it. The destructor
// must do three things in order, and the order is not decoration:
//
//   1. **Cancel this document's render jobs and wait for the scheduler to go idle.** A worker
//      inside `FPDF_RenderPageBitmap` on a document that is about to be destroyed is a
//      use-after-free in PDFium, and `IDocumentBackend::close()` says so at its declaration.
//   2. **Drop the document's tiles from the cache.** Nothing else can: `RenderScheduler` holds the
//      cache privately and has no "forget this document" of its own, so a closed 900-page file
//      would otherwise keep its megabytes for the life of the process.
//   3. **Close the handle.**
//
// Guide case A-39 is exactly this sequence -- *a `PageView` destroyed with tiles in flight* -- and
// its `core/` half was already green at T11 with the widget half owed here.

#ifndef RAWPAGE_UI_SHELL_DOCUMENTWINDOW_H
#define RAWPAGE_UI_SHELL_DOCUMENTWINDOW_H

#include "app/Services.h"
#include "core/backend.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/doc/Revision.h"
#include "core/doc/SaveCoordinator.h"

#include <map>
#include <memory>
#include <optional>

#include <QString>
#include <QWidget>

class QLabel;

namespace rawpage::core {
class CommandJournal;
}

namespace rawpage::ui {

class PageView;
class RulerWidget;
class SplitViewContainer;

class DocumentWindow : public QWidget {
    Q_OBJECT

public:
    // `handle` must be an OPEN document from `services.backend`; this class takes over closing it.
    // An invalid handle is legitimate and yields an empty viewer -- which is what a test that only
    // cares about the tab strip passes.
    // `Services` is taken BY VALUE, as `MainWindow` takes it. It is a bundle of references and
    // copying it costs nothing; holding a reference to one would dangle the moment a caller passed
    // a temporary, which every fixture in `tests/ui` does and which segfaulted the first run of
    // this class's own case.
    DocumentWindow(app::Services services, QString path, core::DocumentHandle handle,
                   QWidget* parent = nullptr);

    // Stage 3 T11. `handle` is `sourcePath` opened by the backend; `directory` is the crashed session's
    // recovery directory, which this class READS and never removes -- `MainWindow::recoverDocument` discards
    // it once the document holds its steps.
    struct Recovery {
        QString directory;
        QString sourcePath;
        bool untitled = false;     // recover into a new, untitled document rather than into the source
    };
    DocumentWindow(app::Services services, core::DocumentHandle handle, const Recovery& recovery,
                   QWidget* parent = nullptr);
    ~DocumentWindow() override;

    // The absolute path this tab stands for. A Save As moves it. EMPTY for an untitled document.
    const QString& path() const { return path_; }

    // The file the document's bytes came from: `path()`, except for an untitled recovered document, whose
    // bytes are its source's and whose `path()` is empty (Stage 3 T11).
    const QString& sourcePath() const { return sourcePath_; }
    bool isUntitled() const noexcept { return path_.isEmpty() && !sourcePath_.isEmpty(); }

    // Where a Save As proposes to write: the path, or for an untitled document "<source name> (recovered).pdf"
    // beside its source -- never the source itself, which the user chose not to write into.
    QString suggestedSavePath() const;

    // What replaying the crashed session did (Stage 3 T11): a report -- steps applied, and where and why it
    // stopped -- or the error that kept it from starting. Both empty for a document that was not recovered.
    const std::optional<core::ReplayReport>& recoveryReport() const noexcept { return recoveryReport_; }
    const std::optional<core::Error>& recoveryFailure() const noexcept { return recoveryFailure_; }

    core::DocumentHandle handle() const noexcept { return handle_; }

    // The revision this tab's document is at: the journal's, which every run, undo and redo moves (Stage 3
    // T10). Every tile key carries it, so nothing rendered before a change can be shown after one (D2-12).
    core::Revision revision() const noexcept;

    // Stage 3 T10. Null only for an invalid handle.
    core::CommandJournal* journal() const noexcept { return journal_.get(); }
    core::RecoveryJournal* recovery() const noexcept { return recovery_.get(); }

    // Whether this build may change the document: a sink, and permissions that allow editing pages.
    bool isEditable() const;
    bool isDirty() const;

    // Through `SaveCoordinator` (D3-4). A Save As that succeeds makes `target` this tab's path. An untitled
    // document's Save is `SaveAsRequired` by `error.save.untitled`, before the coordinator is asked anything.
    core::Result<core::SaveOutcome> save();
    core::Result<core::SaveOutcome> saveAs(const QString& target);

    // The user discarded the document's changes (D3-14): its recovery directory goes with them.
    void discardRecovery();

    // THE DOCUMENT'S OWN BANNERS (Stage 3 T10), by `MessageBar::Kind` value: a failed save, a forced
    // Save-As's explanation, recovery unavailable. Kept HERE so they survive a tab switch -- the window
    // clears every document banner when the tab in front changes and shows the new tab's from this.
    void setCondition(int kind, const QString& text) { conditions_[kind] = text; }
    void clearCondition(int kind) { conditions_.erase(kind); }
    const std::map<int, QString>& conditions() const noexcept { return conditions_; }

    int pageCount() const;

    // Whether a password was asked for on the way in. Set by `MainWindow::openFiles()`, which is
    // the only thing that knows: the backend does not report encryption separately and does not
    // need to -- the callback was invoked, so the file was protected. `MessageBar` reads this, and
    // Stage 10's `core/doc/Permissions` is what replaces it with something that can say WHAT the
    // protection allows rather than only that there was some.
    bool isProtected() const noexcept { return protected_; }
    void setProtected(bool value) noexcept { protected_ = value; }

    // THE ACTIVE VIEW (S4 T9), which is the only view there is until someone splits the window.
    // Every §8 Viewer command reaches the pane a person is in through this and nothing else.
    PageView* view() const noexcept;

    // The split itself: its mode, its panes and whether they scroll together. `MainWindow`'s five
    // `window.*` split commands are the only product caller.
    SplitViewContainer* split() const noexcept { return split_; }

    // Reads the preferences a VIEW obeys and hands them to it. Public because it is what a case
    // drives after changing a setting, and because a settings change and a document open take the
    // same path rather than two that could differ (S2 T15).
    //
    // **TWO GROUPS, NOT ONE.** The eleven `pageDisplay/` keys, plus `performance/prefetchPages` --
    // which is a `performance/` key by its own nature and is read by the viewer all the same. The
    // first version of this subscribed to `PageDisplay` alone and the prefetch preference reached
    // nothing; the case that found it is `ui.performance/prefetchPages asks for pages beyond the
    // viewport`, which is why it asserts a HELD PAGE COUNT rather than that the value was stored.
    void applyViewSettings();

    // The ten `units/*` keys (S4 T8): the rulers' visibility and unit, the grid's appearance, and
    // the two snap switches. A second method rather than a tenth of `applyViewSettings`, because
    // the two groups are subscribed to separately and a `pageDisplay/` change must not repaint a
    // grid it did not move.
    void applyGuideSettings();

    RulerWidget* horizontalRuler() const noexcept { return horizontalRuler_; }
    RulerWidget* verticalRuler() const noexcept { return verticalRuler_; }

    // The tab's text and the window title's `%1`: the file's name, derived rather than stored, so
    // there is no second copy to disagree with the path after a rename.
    QString title() const;

signals:
    // A right-click on a ruler (`guideIndex` -1) or on a guide. `MainWindow` builds the menu; the
    // TARGET is already recorded on the view's `GridGuideLayer`, so the commands in it need no
    // argument -- see `GridGuideLayer::setContextGuide`.
    void guideMenuRequested(int guideIndex, QPoint global);

    // The split's focused pane changed, or a split was made or removed (S4 T9). `MainWindow`
    // re-points the status bar's `viewChanged` connection at it, because the chrome reports on the
    // view a person is in and a connection to a retired pane would report on nothing.
    void activeViewChanged(PageView* view);

    // A command, an undo or a redo changed the model; the revision has moved.
    void mutated();
    void dirtyChanged(bool dirty);
    // The recovery journal stopped recording -- said ONCE, the first time it is noticed (A-80).
    void recoveryUnavailable(const QString& reason);

private:
    void build(const Recovery* recovery);
    void afterMutation();

    // ---- The split (S4 T9) -------------------------------------------------------------------
    // A pane the container has just built: it is handed the document, both settings groups, and
    // the place its source pane is looking at. Splitting a window must show the same page twice,
    // and `PageView::setDocument` deliberately starts at page 1 -- so a pane that was adopted and
    // not positioned would send the person back to the top of the document.
    void adoptView(PageView* view);

    // The MODEL's pages for layout and the ENGINE's for each tile's rotation delta (D3-15), to one
    // pane. One function because a document reaches a pane from two places -- `build()` and
    // `adoptView()` -- and two spellings of D3-15 is two readings of it to keep in agreement.
    void handDocumentTo(PageView* view);

    // The rulers, the cursor readout and the guide menu follow ONE pane. Bound to the active view
    // and rebound when it changes; `unbindActiveView` is convention 10, because every one of those
    // connections is a lambda holding `this` on a child that outlives neither.
    void bindActiveView(PageView* view);
    void unbindActiveView();

    // Copies the active pane's guides onto the others. A guide is a PAGE fact in PDF points
    // (D4-7), so it belongs to the document and shows in every pane; what is per-view is the DRAG,
    // which is why each `PageView` has a `GridGuideLayer` of its own in the first place.
    void mirrorGuides();

    // ---- The rulers (S4 T8) ----------------------------------------------------------------
    // Recomputes both mappings from the page in front of the person. Called on every `viewChanged`
    // -- which a scroll emits as well as a zoom, because `scrollContentsBy` writes the controller
    // and the controller's `changed` reaches `refresh()`.
    void updateRulerMappings();

    // The offset of the ACTIVE view's viewport within the split container -- which is what the two
    // rulers are aligned to in the grid. A scroll area draws a frame, so the two differ by a pixel
    // or two even with one pane, and a ruler out by two pixels reads the wrong number at exactly
    // the edge a person measures to. **With a split it is also the pane's own position** (S4 T9):
    // the bottom pane of a horizontal split starts partway down the vertical ruler, and that is
    // the number the ruler must use.
    QPoint viewportOffset() const;

    // A right-click on a ruler or on a guide, turned into `guideMenuRequested`. The MENU is
    // `MainWindow`'s: every item in it is an `ActionId` through `ActionFactory` (D1-7(a)), and the
    // factory belongs to the window rather than to a tab.
    void raiseGuideMenu(int guideIndex, QPoint global);

    app::Services services_;
    QString path_;
    QString sourcePath_;
    core::DocumentHandle handle_;
    core::Revision revision_ = core::Revision::initial();
    bool protected_ = false;

    // S4 T9. The page surface: one view, or up to four over the same document. Never null after
    // `build()`, and `view()` is its active pane.
    SplitViewContainer* split_ = nullptr;

    // The pane the rulers and the cursor readout are connected to. A raw pointer to a child, kept
    // only so the connections can be undone -- which is the whole of convention 10.
    PageView* boundView_ = nullptr;

    // S4 T8. Always constructed, shown only while `units/showRulers` is on: a ruler that is built
    // when the preference is turned on would have no mapping until the next view change, so the
    // first thing a person saw after `Ctrl+U` would be two blank strips.
    RulerWidget* horizontalRuler_ = nullptr;
    RulerWidget* verticalRuler_ = nullptr;
    QLabel*      rulerCorner_ = nullptr;

    // The `Settings` subscription, released in the destructor. Convention 10: a subscriber that
    // outlives the object it was installed for is a call into freed memory, and `Settings` outlives
    // every tab by construction -- it belongs to the composition root.
    int settingsSubscription_ = 0;

    // Stage 3 T10. Declared AFTER the view so they are destroyed BEFORE it: the journal's subscriptions
    // call into the view, and a journal outliving it would call into a destroyed widget.
    std::unique_ptr<core::CommandJournal> journal_;
    std::unique_ptr<core::RecoveryJournal> recovery_;
    std::unique_ptr<core::SaveCoordinator> coordinator_;
    bool recoveryReported_ = false;
    std::map<int, QString> conditions_;
    std::optional<core::ReplayReport> recoveryReport_;
    std::optional<core::Error> recoveryFailure_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_DOCUMENTWINDOW_H
