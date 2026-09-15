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
// `CommandJournal`, its `RecoveryJournal` and its `SaveCoordinator`. It still holds no split container
// and no pane set; those are Stage 4's.
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
#include "core/doc/Revision.h"
#include "core/doc/SaveCoordinator.h"

#include <map>
#include <memory>

#include <QString>
#include <QWidget>

namespace rawpage::core {
class CommandJournal;
class RecoveryJournal;
}

namespace rawpage::ui {

class PageView;

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
    ~DocumentWindow() override;

    // The absolute path this tab stands for. A Save As moves it.
    const QString& path() const { return path_; }

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

    // Through `SaveCoordinator` (D3-4). A Save As that succeeds makes `target` this tab's path.
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

    PageView* view() const noexcept { return view_; }

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

    // The tab's text and the window title's `%1`: the file's name, derived rather than stored, so
    // there is no second copy to disagree with the path after a rename.
    QString title() const;

signals:
    // A command, an undo or a redo changed the model; the revision has moved.
    void mutated();
    void dirtyChanged(bool dirty);
    // The recovery journal stopped recording -- said ONCE, the first time it is noticed (A-80).
    void recoveryUnavailable(const QString& reason);

private:
    void afterMutation();

    app::Services services_;
    QString path_;
    core::DocumentHandle handle_;
    core::Revision revision_ = core::Revision::initial();
    bool protected_ = false;
    PageView* view_ = nullptr;

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
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_DOCUMENTWINDOW_H
