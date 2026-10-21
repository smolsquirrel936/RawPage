// The application frame.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS CLASS IS ALLOWED TO KNOW.
//
// It receives `app::Services` -- interfaces only -- and it holds NO document logic. Every command
// it offers resolves to an `ActionId` and goes through `ActionRegistry::dispatch`; nothing here is
// wired straight from a widget to a behaviour, because the headless batch runner (flow K, Stage 14)
// has to be able to reach every feature and it has no widgets to click.
//
// WHAT IT DOES NOT HAVE YET, and where each arrives. Saying so here is cheaper than a reader
// searching for a ribbon that is not missing so much as not yet built:
//
//   * a real page surface, which is Stage 2 -- a document tab is a stub under decision D1-2.
//
// Chord input arrived at T13 and is `ui/shell/ShortcutBinder`, one per window: it computes the
// §8 context from the focus chain, resolves the chord through `KeyBindingStore`, and dispatches
// through the same `ActionRegistry` call the ribbon makes. NOTHING here sets a `QAction`
// shortcut -- see `ShortcutBinder.h` for why Qt's own shortcut system cannot express §8's
// per-context bindings, and `showEvent` for the check that keeps it that way.
//
// ---------------------------------------------------------------------------------------------
// THERE CAN BE MORE THAN ONE OF THESE, AND THE ACTION LAYER HAD TO LEARN THAT AT T12.
//
// Tearing a tab out makes a second `MainWindow`, and `ActionRegistry` is one object shared by all
// of them: registration is not idempotent (property 3), so a second window cannot register
// `view.toggleSidebar` again -- and if the first window's handler had captured `this`, every window
// in the application would go on toggling the FIRST window's sidebar, including after that window
// was closed and its handler left pointing at freed memory.
//
// So the handlers below capture nothing. Each resolves `MainWindow::activeShell()` at DISPATCH time
// and acts on that window. Registration happens once per registry, whichever window gets there
// first, and every window afterwards finds the commands already present and leaves them alone.
// This is what makes a Global command mean "do it to the window I am looking at", which is what a
// Global command has always meant.
//
// The central widget is an explicit placeholder that says which of those is which. An empty grey
// rectangle reads as a broken build; a surface that names what is missing reads as a stage.
// ---------------------------------------------------------------------------------------------

#ifndef RAWPAGE_UI_SHELL_MAINWINDOW_H
#define RAWPAGE_UI_SHELL_MAINWINDOW_H

#include "app/Services.h"
#include "core/backend.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/doc/Annotation.h"
#include "core/doc/Fdf.h"
#include "core/text/SearchEngine.h"
#include "ui/dialogs/CompareDialog.h"
#include "ui/dialogs/CustomStampDialog.h"
#include "ui/dialogs/DocumentPropertiesDialog.h"
#include "ui/dialogs/ExchangeReportDialog.h"
#include "ui/dialogs/RotatePagesDialog.h"
#include "ui/dialogs/UnsavedChangesDialog.h"

#include <QImage>
#include <QMainWindow>
#include <QPoint>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QCloseEvent;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace rawpage::ui {

class ActionFactory;
class DocumentTabBar;
class DocumentWindow;
class FullScreenController;
class AnnotationOverlay;
class BookmarksPane;
class AttachmentsPane;
class CommentsPane;
class CommentMigration;
class ComparisonView;
class DestinationsPane;
class LogPane;
class MeasurementScaleDialog;
class SearchPane;
class SoundPlayer;
class StampPalettePane;
class ThumbnailsPane;
class MessageBar;
class PageView;
class PreferencesDialog;
class QuickAccessToolbar;
class Ribbon;
class ShortcutBinder;
class SidebarDock;
class StatusBar;
class ThemeManager;
class ToolFinder;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const app::Services& services, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Flow B's landing point: a file list from the command line, or forwarded by a secondary
    // instance over `platform::ISingleInstanceBus`.
    //
    // DECISION D1-2 GOVERNS THIS FUNCTION. It checks that a path EXISTS and ends in `.pdf`, and it
    // does not read a single byte of the file. Deciding whether bytes are a PDF is
    // `IDocumentBackend`'s job at Stage 2, and pre-empting that boundary inside `ui/` is precisely
    // the violation this architecture exists to prevent -- guide case A-58 checks for it.
    void openFiles(const QStringList& paths);

    // ---------------------------------------------------------------------------------------
    // Documents -- S1 T12, and a real one since S2 T13: a handle, a page tree and a `PageView`.
    // ---------------------------------------------------------------------------------------
    // Adds a tab for a document the BACKEND HAS ALREADY OPENED, or focuses the one that already
    // has this path. The tab takes ownership of the handle and closes it (see `DocumentWindow`),
    // which is why the open happens in `openFiles()` and the handle is passed in rather than
    // acquired here: a function that both opened and might discard would have to close on one of
    // its two paths, and that is the leak.
    //
    // The same document twice in one window is not a feature anyone asked for, and the path is the
    // identity until `DocumentId` arrives with the rest of `core/doc/` at Stage 3.
    DocumentWindow* openDocument(const QString& absolutePath, core::DocumentHandle handle);

    int             documentCount() const;
    DocumentWindow* documentAt(int index) const;
    DocumentWindow* currentDocument() const;
    int             indexOfPath(const QString& absolutePath) const;

    void activateDocument(int index);

    // CLOSING A DIRTY DOCUMENT NEEDS A DISPOSITION (Stage 3 T10, D3-14). With none given, the user is
    // asked; where there is nobody to ask (convention 16) the close is REFUSED with `Unavailable` and the
    // document stays open and dirty -- never closed with its changes silently dropped, never blocked on a
    // dialog nobody can see. Cancel refuses with `Cancelled`; Discard removes the recovery directory and
    // closes; Save closes only once the save -- or the Save-As it turned into -- succeeded. A clean
    // document closes without a question, whatever is passed.
    core::Status closeDocument(int index, std::optional<CloseDisposition> disposition = std::nullopt);

    // Every document, first to last, STOPPING at the first refusal -- a Cancel on the second of three
    // leaves the second and the third open.
    core::Status closeAllDocuments();

    // ---------------------------------------------------------------------------------------
    // Recovery -- Stage 3 T11 (D3-11). `app/RecoveryPrompt` discovers and asks; this acts on one answer.
    // ---------------------------------------------------------------------------------------
    // Opens the candidate's source through the backend and replays its journal into a new tab: under the
    // source's own path when the source is still the file the changes were made to, and otherwise into an
    // UNTITLED document whose source is never written. The tab says what happened in a `Recovered` banner --
    // the steps restored, the step *k* that could not be and why, a last change that was still being made.
    // Once the document holds its steps the crashed session's directory is removed; a refusal -- a damaged
    // journal, a source that is gone, a journal that cannot be replayed at all, the same file already open
    // here -- opens nothing and KEEPS it, so nothing is lost by a recovery that did not happen.
    core::Result<DocumentWindow*> recoverDocument(const core::RecoveryCandidate& candidate);

    // ---------------------------------------------------------------------------------------
    // Mutation and save -- Stage 3 T10.
    // ---------------------------------------------------------------------------------------
    // Runs a Rotate Pages choice through the document's journal: one command, or one group (D3-13).
    core::Status rotatePages(DocumentWindow* document, const RotatePagesDialog::Choice& choice);

    // Save in place, through `SaveCoordinator`. A document that must be saved as a new file -- repaired,
    // changed on disk since it was opened, or refused by the file system (read-only, held by another
    // process, a directory that cannot hold the temporary file) -- first shows WHY, as a `SaveAsRequired`
    // banner, and then goes to `saveDocumentAs` (D3-6, D3-16; the last of the three since Stage 3 guide
    // I-15). A failure no other path could answer is a `SaveFailed` banner as well as the result.
    core::Status saveDocument(DocumentWindow* document);
    core::Status saveDocumentAs(DocumentWindow* document);

    // WHERE A SAVE AS GOES, and the parameterized twin a headless case drives (convention 16). The
    // answer is handed the path the dialog would propose -- the document's own, or for an untitled document
    // `DocumentWindow::suggestedSavePath()`; nothing means the user cancelled. `canAskSavePath()`
    // is false offscreen unless an answer is installed, and `file.saveAs` is then disabled.
    using SaveAsAnswer = std::function<std::optional<QString>(const QString& currentPath)>;
    static void setSaveAsAnswerForTesting(SaveAsAnswer answer);
    static bool canAskSavePath();

    // `Ctrl+Tab` / `Ctrl+Shift+Tab` once T13 wires the chords. Wraps at both ends, which is what
    // every tab strip does and what makes the chord usable without looking.
    void stepDocument(int delta);

    // ---------------------------------------------------------------------------------------
    // Tear-off and re-dock -- the structural behaviour D1-2 exists to let us build now.
    // ---------------------------------------------------------------------------------------
    // Takes the document OUT without destroying it. The caller owns it until it is adopted, which
    // is what makes moving one between windows a move rather than a close and a re-open -- the tab
    // keeps its identity, and at Stage 2 it will keep its journal and its scroll position with it.
    DocumentWindow* releaseDocument(int index);
    void            adoptDocument(DocumentWindow* document, int index = -1);

    // Release into a brand-new window, positioned near where the drag ended. Returns it.
    MainWindow* detachDocument(int index, const QPoint& globalPos = QPoint());

    // A window emptied by a MOVE has done its job and goes (issue I-35). Called after a document is
    // adopted away by another window -- a tear-off drop, and since S4 T11 *Merge All Windows* --
    // and only then: a window emptied by a CLOSE stays, because the user is still standing in it.
    // **Only a window that owns itself can go**; the rule and its reason are at the definition, and
    // `ui/shell/WindowArranger` calls this rather than repeating it.
    void closeIfEmptied();

    // ---------------------------------------------------------------------------------------
    // A SECOND VIEW OF ONE DOCUMENT -- S4 T11, `window.new`, D4-13's exception.
    // ---------------------------------------------------------------------------------------
    // Opens the tab in front A SECOND TIME, through the backend, into a brand-new window whose tab
    // is a COMPANION of this one: its own `DocumentHandle` -- which two windows need, because each
    // runs the three-step close protocol -- and no journal, so there is one undo stack and one
    // Save for the file. `DocumentWindow`'s companion constructor argues both halves.
    //
    // Refused, with the reason, for a document that has no file of its own (an untitled recovered
    // one has nothing to re-open) or that the backend will not open a second time. **With no
    // document in front it opens an EMPTY window**, which is what `window.new` did from Stage 1 T12
    // to Stage 4 T10 and is still the right answer when there is nothing to make a view of.
    core::Result<MainWindow*> openSecondView();

    DocumentTabBar* documentTabs() const { return tabs_; }

    // ---------------------------------------------------------------------------------------
    // The window set -- T12.
    // ---------------------------------------------------------------------------------------
    // In creation order. `Ctrl+F6` walks it; a Global command dispatches into `activeShell()`.
    static const QList<MainWindow*>& shellWindows();

    // The window a Global command applies to: the last one activated, falling back to the last one
    // created. Never a window that has been destroyed -- the destructor takes it out of the set,
    // which is the whole reason the set exists rather than a raw `QApplication::activeWindow()`
    // cast, since that answers null whenever the application is in the background.
    static MainWindow* activeShell();

    static void stepWindow(int delta);

    // S4 T11 -- A2's named work (`Tech-Stack-Plan.md` §3.3, *A2 at Stage 4*). Tells every window
    // whether it is the one in front, which decides the lane its viewport queues in
    // (`ui/viewer/TilePresenter::setBackground`). Driven from `activeShell()` rather than from
    // `isActiveWindow()`, and the reason is the same one `raiseToForeground` records: under
    // `offscreen` the activation event never arrives, so a rule read from the platform would make
    // every window a background window and the headless suite could not judge the behaviour at all.
    //
    // WITH ONE WINDOW IT IS A NO-OP by construction: `activeShell()` is that window. So nothing in
    // the product changes until there are two, which is exactly when the measurement said it must.
    static void refreshWindowFocusState();

    // Called by the primary instance when a secondary hands off. Separate from `openFiles()`
    // because raising is a window operation and opening is not, and Stage 4's window set will need
    // to choose WHICH window to raise.
    void raiseToForeground();

    // WHAT A HANDOFF DOES: raise, THEN open. The order is the whole of this method (Stage 2 guide
    // issue I-30). Opening an encrypted file blocks inside a modal password prompt, so raising
    // AFTER the open left the prompt to be shown by a background process -- which Windows does not
    // bring forward -- and the window came up only once somebody had found the prompt and answered
    // it. A method rather than two lines in `app/main.cpp`'s listener so the order has a case.
    void openHandoff(const QStringList& files);

    // The window's keyboard. `activeContext()` is what attributes every dispatch -- a ribbon
    // click's as much as a chord's -- to the §8 context the focus is actually in.
    ShortcutBinder*     shortcutBinder() const { return shortcuts_.get(); }

    Ribbon*             ribbon() const { return ribbon_; }
    QuickAccessToolbar* quickAccessToolbar() const { return qat_; }
    ToolFinder*         toolFinder() const { return toolFinder_; }
    SidebarDock*        sidebar() const { return sidebar_; }

    // The log pane -- T14. Built with the window and HIDDEN, because a shell that opens with a
    // log pane showing is a shell that looks like it is in trouble. `pane.log` toggles it.
    LogPane*            logPane() const { return logPane_; }
    // S5 T9 (D5-13): Advanced Search, its own dock along the right; `Ctrl+Shift+F` shows it.
    SearchPane*         searchPane() const { return searchPane_; }

    // ---- STAMPS (S6 T10, D6-16) ---------------------------------------------------------------------
    // The stamp palette, its own dock along the right; `Ctrl+Alt+S` arms the stamp tool and shows it.
    StampPalettePane*   stampPalette() const { return stampPalette_; }
    void showStampPalette();
    // A custom stamp, as the dialog made it, added to the library -- marked a favourite when asked -- listed,
    // chosen and its tool armed. `error.stamps.noLibrary` in a window composed with none.
    core::Status addCustomStamp(const CustomStampDialog::Made& made);
    // D6-22's image half: `image` placed as an image stamp at the middle of what the tab in front shows -- ONE
    // command. What `edit.paste` does with an image on the clipboard.
    core::Status pasteImage(const QImage& image);
    // An Advanced Search result, shown: its file opened through `openFiles` -- or its tab brought forward,
    // so a file already open is not opened twice (guide A-87) -- and its hits shown in that tab's find bar,
    // this one current and revealed.
    void openSearchResult(const QString& path, const QString& pattern,
                          const std::vector<core::text::SearchHit>& hits, int current);

    // ---------------------------------------------------------------------------------------
    // THE COMPARISON -- S5 T13, D5-18. `compare.documents` asks `CompareDialog` and lands here.
    // ---------------------------------------------------------------------------------------
    // Writes the choice's four options to their settings -- the keys ARE the options (D5-14's rule) -- then
    // opens BOTH files through the backend, each a handle of the comparison's own (a file open in a tab is
    // opened again, as `window.new` does, so its journal is never reached), and shows a `ComparisonView`
    // owned by this window, replacing any comparison already open. A file the backend refuses opens nothing
    // and is said, with the engine's reason, on the status bar and the message bar.
    core::Result<ComparisonView*> compareDocuments(const CompareDialog::Choice& choice);
    // The open comparison, or null. One per window; it closes itself, and with this window.
    ComparisonView* comparison() const;

    // The document panes the sidebar hosts -- Stage 4 T5. ONE PER WINDOW, re-pointed at the tab in
    // front by `updateDocumentPanes()`; see the construction comment for why it is not one per
    // document. Null only before `buildCentralSurface()` has run.
    ThumbnailsPane*     thumbnailsPane() const { return thumbnails_; }

    // S4 T10 (D4-12). ONE per window, built with it, and the only thing that knows whether this
    // window is at level 1, level 2 or neither. Never null.
    FullScreenController* fullScreen() const noexcept { return fullScreen_.get(); }
    BookmarksPane*      bookmarksPane() const { return bookmarks_; }
    DestinationsPane*   destinationsPane() const { return destinations_; }
    // S6 T9 (D6-12): the Comments pane, one per window like the others, over the store of the tab in front.
    CommentsPane*       commentsPane() const { return comments_; }

    // ---- ATTACHMENTS (S6 T11, D6-17) ------------------------------------------------------------------
    // The Attachments pane, in the sidebar; `Ctrl+Shift+A` (`pane.attachments`) shows it. Its four commands are
    // these, over the tab in front -- each reachable from the pane, the tool finder and a case alike.
    AttachmentsPane*    attachmentsPane() const { return attachments_; }
    // OPEN: an attached PDF in a tab of its own, from its bytes, read-only (no file written); an EXECUTABLE type
    // refused before anything is read or written (`core::isExecutableName`, the policy hook Stage 10's TrustPolicy
    // replaces); anything else ASKED FIRST, then written into this session's folder and handed to the shell.
    core::Status openAttachment(const core::AttachmentRef& attachment);
    // EXTRACT: where the person chooses, through the backend's write -- an executable too, which is saved, not run.
    core::Status extractAttachment(const core::AttachmentRef& attachment);
    // ADD: the file the person chooses (or `path`), attached to the document as a whole -- ONE command.
    core::Status addAttachment();
    core::Status addAttachmentFrom(const QString& path, const QString& description = {});
    // DELETE: a document's file by `DeleteAttachmentCommand`; a pushpin as the annotation it is, with its pop-up.
    core::Status deleteAttachment(const core::AttachmentRef& attachment);
    // What a headless case answers in place of the three questions (convention 16): which file to attach, where to
    // extract one, and whether to open a file that is not a PDF. An empty member asks the person -- or, with
    // nobody to ask, answers nothing.
    struct AttachmentAnswers {
        std::function<std::optional<QString>()> attachPath;
        std::function<std::optional<QString>(const QString& proposed)> extractPath;
        std::function<bool(const QString& name)> confirmOpen;
    };
    static void setAttachmentAnswersForTesting(AttachmentAnswers answers);

    // ---- DELETING A COMMENT ASKS FIRST (S6 guide I-32) ---------------------------------------------------
    // Every delete of comments -- the Comments pane's `Delete` and its menu's, *Delete All*, *Delete the Filtered
    // Comments*, and `Delete` on what the page has selected -- asks `question` and goes ahead only on a yes. Where
    // nobody can be asked and no answer is installed, each is DISABLED (convention 16); the pane's and the overlay's
    // own methods are the twins that ask nobody.
    bool confirmDelete(const QString& question);
    bool canAskToDelete() const;
    // What a case answers in the dialog's place, per window: given the question, whether to delete. Empty: asked.
    using DeleteAnswer = std::function<bool(const QString& question)>;
    void setDeleteAnswerForTesting(DeleteAnswer answer) { deleteAnswer_ = std::move(answer); }

    // ---- MEASUREMENT (S6 T12, D6-14) --------------------------------------------------------------------
    // The scale dialog asked -- from two lengths, or (`measuredPoints`) to CALIBRATE from a length measured on the
    // page -- and the three `measuring/*` keys written from what it chose. False when nothing was chosen.
    bool askMeasurementScale(std::optional<double> measuredPoints);
    // What a headless case answers in the dialog's place: it fills the fields as a person would and says whether it
    // pressed OK. Empty: the person is asked -- or, with nobody to ask, nothing is.
    using MeasurementScaleAnswer = std::function<bool(MeasurementScaleDialog&)>;
    static void setMeasurementScaleAnswerForTesting(MeasurementScaleAnswer answer);

    // ---- COMMENTS IN AND OUT (S6 T13, D6-20, D6-21, D6-22) -------------------------------------------------
    // Defined in `ui/shell/MainWindowExchange.cpp`. Each over the tab in front, each reachable from the ribbon, the tool
    // finder and a case alike, each a command where it changes the document -- and none asks a person where nobody can
    // be asked (convention 16).
    //
    // IMPORT: the FDF or XFDF chosen (or `path`), merged into the tab by `/NM` (`AnnotationStore::importFdf`) as ONE
    // `ImportAnnotationsCommand` -- the counts on the status bar, and the report shown when anything was not brought
    // over. A file the backend refuses imports nothing and is said, by key.
    core::Result<core::MergeResult> importComments();
    core::Result<core::MergeResult> importCommentsFrom(const QString& path);
    // EXPORT: what the Comments pane lists -- everything, unless the pane is filtered (D6-20's *export only the
    // filtered*) -- with each one's thread, to the file chosen, its format by its suffix, written by the backend.
    core::Status exportComments();
    core::Status exportCommentsTo(const QString& path);
    // MIGRATE: the older revision chosen (or `olderPath`) opened, both documents' text read and aligned off the UI
    // thread (`CommentMigration`), and what lands merged into the tab as ONE `ImportAnnotationsCommand`, the report
    // shown. Returns once the run has STARTED; `migrationFinished` says when it is done.
    core::Status migrateComments();
    core::Status migrateCommentsFrom(const QString& olderPath);
    CommentMigration* migration() const;
    // COPY, CUT, PASTE ANNOTATIONS (D6-22): the selected annotations -- with their pop-ups -- on the clipboard under
    // `kAnnotationsMimeType`, and their words as text; a cut is that and ONE `DeleteAnnotationCommand`; a paste adds them
    // to the page in front, where they were when the page is the size theirs was, else round the middle of what is
    // shown -- each a new `/NM`, everything else as copied -- as ONE `AddAnnotationCommand`.
    static constexpr const char* kAnnotationsMimeType = "application/x-rawpage-annotations";
    core::Status copyAnnotations();
    core::Status cutAnnotations();
    core::Status pasteAnnotations(const QByteArray& payload);
    // What the last import or migration reported, for a case; the dialog shows it where a person can see it.
    const std::optional<ExchangeReport>& lastExchangeReport() const noexcept { return lastExchangeReport_; }

    // ---- A SOUND COMMENT (S6 T14, D6-18) -- defined in `ui/shell/MainWindowAudio.cpp` ---------------------------
    // The recorder asked (`ui/viewer/AudioRecorder`) and what it recorded; the WAV `annot.attachAudio` is to read. Each
    // answers nothing where nobody can be asked (convention 16) or the person stopped short.
    std::optional<core::SoundData> askRecording();
    std::optional<QString> askSoundPath();
    // Sound `id` of the tab in front PLAYED -- its samples carried, or read from the document. Changes no byte.
    core::Status playSound(const core::AnnotationId& id);
    // The sound last handed to the player, its samples read -- for a case, where the player may have no device.
    const std::optional<core::SoundData>& lastPlayed() const noexcept { return lastPlayed_; }
    // What a headless case answers in place of the recorder and the WAV question. Empty: the person is asked.
    struct AudioAnswers {
        std::function<std::optional<core::SoundData>()> recording;
        std::function<std::optional<QString>()> soundPath;
    };
    static void setAudioAnswersForTesting(AudioAnswers answers);
    // What a headless case answers in place of the three file questions. An empty member asks the person -- or, with
    // nobody to ask, answers nothing.
    struct ExchangeAnswers {
        std::function<std::optional<QString>()> importPath;
        std::function<std::optional<QString>(const QString& proposed)> exportPath;
        std::function<std::optional<QString>()> migratePath;
    };
    static void setExchangeAnswersForTesting(ExchangeAnswers answers);

    // ---- THE SIDEBAR AS ONE OPAQUE STRING (S4 T13, D4-9) -------------------------------------
    // What `core::SessionWindow::paneLayout` holds, encoded and decoded HERE because it is a Qt
    // fact about widgets -- which dock edge, how wide, which pane in front -- and `core/` is not
    // taught a dock vocabulary to carry a value it never reads (`core/settings/SessionState.h`
    // says the same thing from the other side).
    //
    // **An EMPTY string is a layout, not an absence**: it means *no panes*, and applying it closes
    // the sidebar. That is what `general/rememberPaneLayout` off restores, and the reason it is
    // stated rather than left to the constructor's default -- a window is built with the sidebar
    // open, so "do nothing" and "remember nothing" would be two visibly different results from one
    // switch being off.
    QString paneLayoutState() const;
    void    applyPaneLayoutState(const QString& state);

    // The Preferences dialog, built on first use and kept. Null until `file.preferences` has been
    // dispatched once. Modeless, so this is not a blocking call's return value -- see
    // `ui/dialogs/PreferencesDialog.h` for why modeless is the load-bearing choice.
    PreferencesDialog*  preferences() const { return preferences_; }
    PreferencesDialog*  showPreferences();

    // READING MODE -- S4 T12, `Ctrl+L`, `Features.md` §1.2. The chrome goes and the WINDOW stays: a
    // title bar, a border and a taskbar button, unlike `F11`, which is the window state and is
    // `FullScreenController`'s level 1 (D4-12). The two are independent and compose -- a person may
    // read full-screen with no ribbon, and leaving one does not leave the other.
    //
    // **IT RESTORES WHAT IT HID, NOT A DEFAULT.** Which panes were open, how wide the sidebar was
    // and which edge it was docked to are recorded on the way in; a second press that put the
    // sidebar back at its default width on the left would be a reading mode that rearranges the
    // window, which is what guide case A-70 asserts against.
    void setReadingMode(bool on);
    void toggleReadingMode() { setReadingMode(!readingMode_); }
    bool isReadingMode() const noexcept { return readingMode_; }

    // Document Properties (`Ctrl+D`, D4-4). Public so a case can compose the facts without going
    // through a modal dialog, which under the offscreen platform is shown to nobody and never
    // returns (convention 16, Stage 1's I-23).
    DocumentPropertiesDialog::Properties documentProperties() const;
    void showDocumentProperties();

    StatusBar*          statusBar() const { return status_; }
    MessageBar*         messageBar() const { return messageBar_; }

    // The one place a `QAction` is constructed (D1-7). Exposed for the suite, which needs an action
    // the FACTORY built in order to assert what `refresh()` does to it -- a hand-made `QAction`
    // could not be built anyway, because `layer-lint`'s `bare-qaction` rule is non-escapable.
    ActionFactory*      actionFactory() const { return actionFactory_.get(); }

    // The ruler's and the guide's context menu (S4 T8). It lives here rather than on
    // `DocumentWindow` because every item in it is an `ActionId` through `ActionFactory`
    // (D1-7(a)), and the factory belongs to the window. `guideIndex` < 0 is a right-click on a
    // ruler, which offers the same commands minus the two that need a guide.
    void showGuideMenu(int guideIndex, QPoint global);

    // The viewer of the tab in front, or null when there is no document. Every §8 Viewer command
    // resolves through here at DISPATCH time -- convention 14: a handler that captured a view
    // would act for a tab that has since been closed or torn off into another window.
    PageView*           currentView() const;
    ThemeManager*       theme() const { return theme_.get(); }

signals:
    // S6 T13: a migration started by `migrateCommentsFrom` has ended -- merged, refused or cancelled. What it did is
    // `lastExchangeReport()` and the status bar.
    void migrationFinished();

protected:
    // Activation is what `activeShell()` follows. Offscreen it may never arrive, which is why
    // `raiseToForeground()` sets the same state directly -- a test that raises a window must get
    // the same answer a desktop does.
    void changeEvent(QEvent* event) override;

    // EVERY WAY A WINDOW CLOSES -- its close box, `file.quit`, a torn-off window's close -- asks about each
    // dirty document through `closeDocument` (D3-14, guide case A-78), and a refusal keeps the window.
    // Clean documents stay in the window as they always did; only a dirty one needs a disposition.
    void closeEvent(QCloseEvent* event) override;

    // The debug half of D1-7, in both of its forms -- no direct connection, and no QAction that
    // resolves its own key. Runs at SHOW time rather than at construction because an action can
    // be added to the window after the constructor -- a dialog's, a pane's -- and the check is worth
    // having on the tree as the user actually sees it.
    void showEvent(QShowEvent* event) override;

    // REACHING FOR AN AUTO-HIDDEN PANE IS A HOVER, and the edge strips are the only widgets this
    // filters. A strip is a `QToolButton` and a button has no "pointer arrived" signal, so the
    // window watches the two of them for `QEvent::Enter` -- see `buildAutoHideStrips()` for why the
    // filter is removed in the destructor rather than left to Qt.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildHeader();
    void buildCentralSurface();
    void buildAutoHideStrips();
    void registerActions();
    // S6 T9: the Comments pane's half of `registerActions` and of the document wiring.
    void registerCommentActions();
    // S6 T11: the Attachments pane's.
    void registerAttachmentActions();
    std::optional<QString> askAttachPath();
    // S6 T13: import, export, migrate, and the annotations' half of cut, copy and paste.
    void registerExchangeActions();
    void onMigrationFinished();
    void showExchangeReport(ExchangeReport report);
    void updateCommentNumbers();
    void revealComment(const core::AnnotationId& id, int page, const core::PdfRect& rect);
    // S6 guide I-34: the pop-up a click opened goes when the person presses, or moves the focus, anywhere in this
    // window but the page -- whose own presses decide -- and a row of the Comments list. `target` is the widget that
    // received the press (or the focus); `pos` the press in its coordinates, none for a focus change.
    void dismissCommentPopup(QWidget* target, std::optional<QPoint> pos);
    // S6 guide I-32: the question a delete of `items` comments asks -- one, one with replies, or many.
    static QString deleteQuestion(std::size_t items, bool replies);
    void updateAutoHideStrips();

    // The Quick Access Toolbar's two placements (`Features.md` §1.1) are two positions in the
    // header's column, above the ribbon or below it. Re-inserting the same widget is what makes the
    // placement a property of the toolbar rather than two toolbars.
    void applyQatPlacement();

    // The QAT half of the UI customization, in both directions. Kept here rather than inside
    // `QuickAccessToolbar` because a widget that reads and writes `Settings` on its own behalf is a
    // widget that cannot be tested without one -- and because the window is what knows that a
    // change is worth persisting at all.
    void loadUiCustomization();
    void saveUiCustomization();

    // Tab strip visibility, the body's current page, the window title and the status bar all follow
    // from the document set. One function, called after every change to it: four call sites each
    // updating three of the four is how a title bar ends up naming a closed document.
    void updateDocumentChrome();

    // Points the sidebar's panes at the document in front -- Stage 4 T5. Called from
    // `updateDocumentChrome()`, so a tab switch, an open and a close all take one path.
    void updateDocumentPanes();

    // The tab texts and the window title, with the dirty marker (Stage 3 T10). Separate from
    // `updateDocumentChrome()` because a mutation moves the marker and must NOT re-raise the document's
    // banners: one the user dismissed would come back with every command (guide case A-80).
    void updateDocumentTitles();

    // Pushes the current view's page number and zoom into the status bar, and re-asks
    // `ActionFactory` for every action's enabled state and tooltip (I-53). One function, because
    // the three move together: a page changes, the Next Page button greys out, and its tooltip has
    // to stop saying "not available for this document" and start saying why.
    void updateViewChrome();

    // THE CHROME FOLLOWS THE TOOL (S6 guide I-24): a tool can change with no command run -- an annotation tool goes
    // back to the Selection tool after its use (`PageView::annotationToolUsed`), `Esc` puts one away -- and until
    // then the ribbon went on showing the tool put away as armed. `view`'s `toolChanged` re-asks every action, as a
    // command does; disconnected first, as `viewChanged` is, because a lambda cannot be a unique connection.
    void followTool(PageView* view);

    // Raises the banners a document's own condition earns -- repaired, protected. Called on every
    // tab change, because the condition belongs to the tab and not to the window.
    void showDocumentConditions(DocumentWindow* document);

    // The five `window.*` arranging commands -- S4 T11, D4-17. A separate function from
    // `registerActions()` for the reason `registerViewerActions` is: that one was already 250 lines,
    // and the mechanism -- once per registry, nothing captured, the window resolved at dispatch
    // (convention 14) -- is identical and argued there.
    void registerArrangeActions();

    // `window.list`'s popup -- S4 T11. Every item is NAVIGATION rather than a command and goes
    // through `ActionFactory::addNonCommandMenuItem`, on the ribbon overflow's precedent: a window
    // has no `ActionId` and there is nothing for `ActionRegistry` to route. The command is opening
    // the list.
    void showWindowList();

    // The §8 Viewer commands. Separate from `registerActions()` only because that function was
    // already 250 lines: the registration mechanism, the once-per-registry guard and the
    // resolve-at-dispatch rule are all identical and all argued there.
    void registerViewerActions();
    // S6 T5 (D6-9): the fourteen arming chords, `K`, `Delete` and both `Ctrl+E` rows.
    void registerAnnotationActions();

    // S4 T10 (D4-12). The seven §8 Full Screen rows, every one of them DECLARED at Stage 1 with a
    // chord and registered by nothing until now -- so that whole table resolved `NotFound` and
    // left its keys unconsumed. A separate function from `registerViewerActions` because they
    // resolve in a different `KeyContext` and act on a different object.
    void registerPresentationActions();

    // `edit.undo`, `edit.redo`, `page.rotate`, `file.save` and `file.saveAs` -- Stage 3 T10.
    void registerMutationActions();

    // A failed save, as a banner the document keeps (Stage 3 T10).
    void reportSaveFailure(DocumentWindow* document, const core::Error& error);

    // The two Viewer commands that ask the user a question. Both are refused where there is no one
    // to ask (convention 16, Stage 1's I-23), and both end in the same `PageView` call a chord or
    // the status bar's page box would take -- one behaviour, three routes.
    void promptForZoom();
    void promptForPage();

    // `file.open`'s dialog. A modal file picker is the one place Stage 1 asks the user a question,
    // and it collects a parameter and calls `openFiles` -- no document logic, per the standing rule
    // about dialogs.
    void promptForFiles();

    void toggleSidebar();

    // S4 T10: `view.fullScreen` now reaches `FullScreenController` level 1. It was *"the WINDOW
    // STATE ONLY"* from Stage 1 to Stage 4 T9 and said so at its definition; what it reaches is
    // now the controller, and the `ActionId` is unchanged (D4-12).
    void toggleFullScreen();

    void toggleRibbon();

    app::Services services_;

    // Constructed before the action factory, because the factory's context provider asks it which
    // §8 context a dispatch happened in, and destroyed first, because it filters the application's
    // events and must stop doing so before the window it answers for comes apart (I-12's lesson,
    // reached through a different door).
    std::unique_ptr<ShortcutBinder> shortcuts_;

    // Constructed before the ribbon and destroyed after it: every RawPageAction the ribbon shows
    // was made here, and the lambda each one carries holds this factory by reference.
    std::unique_ptr<ActionFactory> actionFactory_;

    Ribbon*             ribbon_ = nullptr;
    QuickAccessToolbar* qat_ = nullptr;
    ToolFinder*         toolFinder_ = nullptr;

    // The menu-widget slot holds a COLUMN, not the ribbon directly: the QAT sits above or below the
    // ribbon and both have to be in the one slot QMainWindow puts above the dock areas, or a docked
    // pane can land between them.
    QWidget*      header_ = nullptr;
    QVBoxLayout*  headerColumn_ = nullptr;

    // True only while `loadUiCustomization()` is restoring. Without it the restore would write the
    // factory defaults back as though the user had chosen them, and "never customized" would stop
    // being distinguishable from "customized to look exactly like the default".
    bool suppressCustomizationSave_ = false;

    // The central area: an edge strip, the document column, an edge strip. The column is the tab
    // strip over a two-page stack -- the "no documents" placeholder, and the documents themselves.
    DocumentTabBar* tabs_ = nullptr;
    QStackedWidget* body_ = nullptr;        // page 0: the placeholder. page 1: documents_
    QStackedWidget* documents_ = nullptr;
    QWidget*        placeholder_ = nullptr;

    SidebarDock*      sidebar_ = nullptr;
    ThumbnailsPane*   thumbnails_ = nullptr;
    BookmarksPane*    bookmarks_ = nullptr;
    DestinationsPane* destinations_ = nullptr;
    CommentsPane*     comments_ = nullptr;
    AttachmentsPane*  attachments_ = nullptr;   // S6 T11
    // S6 T9: the overlay the pane's sequence numbers were last given -- cleared when the tab in front changes.
    QPointer<AnnotationOverlay> numberedOverlay_;
    // S6 guide I-32 and I-34: a case's answer to the delete question, and what watches the application's presses.
    DeleteAnswer deleteAnswer_;
    QObject* pressWatcher_ = nullptr;
    LogPane*          logPane_ = nullptr;
    SearchPane*       searchPane_ = nullptr;
    StampPalettePane* stampPalette_ = nullptr;   // S6 T10
    // S5 T13: an owned window, deleted on close -- so a guarded pointer, never a raw one.
    QPointer<ComparisonView> comparison_;
    // S6 T13: the migration in flight, and the tab it lands in; the last import's or migration's report.
    QPointer<CommentMigration> migration_;
    QPointer<DocumentWindow> migrationTarget_;
    std::optional<ExchangeReport> lastExchangeReport_;
    // S6 T14: the one player, made on the first sound played, and what it was last given.
    SoundPlayer* player_ = nullptr;
    std::optional<core::SoundData> lastPlayed_;
    StatusBar*   status_ = nullptr;
    MessageBar*  messageBar_ = nullptr;

    // Created on first use rather than with the window: three quarters of sessions never open it,
    // and building three pages and a 248-row table for every window that might be torn off is a
    // cost nobody asked for. Parented to this window, so it dies with it.
    PreferencesDialog* preferences_ = nullptr;
    std::unique_ptr<ThemeManager> theme_;

    // S4 T10 (D4-12). Declared AFTER the widgets it points at so it is destroyed BEFORE them: its
    // destructor is the last exit path and it tears down a presentation surface that holds a
    // `PageView` over this window's document.
    std::unique_ptr<FullScreenController> fullScreen_;

    // The edge strip an auto-hidden pane collapses into. It belongs to the WINDOW rather than to
    // the dock: a dock widget that placed a sibling in its parent's layout would be a class that
    // cannot be constructed without that parent, and therefore one that cannot be tested alone.
    // S4 T12. What reading mode hid, so leaving it puts back the same window rather than a default
    // one. `active` is what distinguishes *nothing was recorded* from *everything was hidden*.
    struct ChromeState {
        bool active = false;
        bool header = true;
        bool sidebar = false;
        bool sidebarFloating = false;
        Qt::DockWidgetArea sidebarArea = Qt::LeftDockWidgetArea;
        int  sidebarWidth = 0;
        bool logPane = false;
        bool statusBar = true;
    };
    bool readingMode_ = false;
    ChromeState beforeReading_;

    QToolButton* leftStrip_ = nullptr;
    QToolButton* rightStrip_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_MAINWINDOW_H
