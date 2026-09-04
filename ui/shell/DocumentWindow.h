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
// the open document's handle, its revision, and the viewer. What it still does NOT hold is a
// `CommandJournal`, a split container or a pane set -- Stages 3 and 4 -- because Stage 2 has no
// mutation of any kind and a journal with nothing to record would be a member built ahead of its
// first caller.
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

#include <QString>
#include <QWidget>

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

    // The absolute path this tab stands for.
    const QString& path() const { return path_; }

    core::DocumentHandle handle() const noexcept { return handle_; }

    // The revision this tab's document is at. `Revision::initial()` for every document Stage 2 can
    // open, because Stage 2 has no mutation -- it moves at Stage 3, and every tile key carries it
    // so that nothing rendered before a change can be shown after one (D2-12).
    core::Revision revision() const noexcept { return revision_; }

    int pageCount() const;

    // Whether a password was asked for on the way in. Set by `MainWindow::openFiles()`, which is
    // the only thing that knows: the backend does not report encryption separately and does not
    // need to -- the callback was invoked, so the file was protected. `MessageBar` reads this, and
    // Stage 10's `core/doc/Permissions` is what replaces it with something that can say WHAT the
    // protection allows rather than only that there was some.
    bool isProtected() const noexcept { return protected_; }
    void setProtected(bool value) noexcept { protected_ = value; }

    PageView* view() const noexcept { return view_; }

    // The tab's text and the window title's `%1`: the file's name, derived rather than stored, so
    // there is no second copy to disagree with the path after a rename.
    QString title() const;

private:
    app::Services services_;
    QString path_;
    core::DocumentHandle handle_;
    core::Revision revision_ = core::Revision::initial();
    bool protected_ = false;
    PageView* view_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_DOCUMENTWINDOW_H
