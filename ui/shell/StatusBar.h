// The status bar: page box, zoom, and the background-task indicator.
//
// EVERY CONTROL HERE IS BUILT AND DISABLED AT STAGE 1, and that is the point rather than a
// shortcut. There is no document to be on page 3 of and no renderer to be 125 % zoomed, so a
// status bar that showed "1 / 1" and "100 %" would be lying in a way that is very hard to notice
// later. A disabled control that is visibly present says exactly what is true: the shell exists,
// the capability does not yet. It is the same reasoning ActionRegistry uses for an ActionId that is
// declared and unregistered (Stage1-AppShell-Plan.md §5.1) -- render it, disable it, and let the
// tooltip say which stage brings it.
//
// Stage 2 gives the page box and the zoom controls a `ViewportController` to talk to, and Stage 2's
// `RenderScheduler` gives the progress indicator something to report.

#ifndef RAWPAGE_UI_SHELL_STATUSBAR_H
#define RAWPAGE_UI_SHELL_STATUSBAR_H

#include <QStatusBar>

#include <QString>

#include <functional>

class QLabel;
class QLineEdit;
class QProgressBar;

namespace rawpage::ui {

class StatusBar : public QStatusBar {
    Q_OBJECT

public:
    explicit StatusBar(QWidget* parent = nullptr);
    ~StatusBar() override;

    // The left-hand message. Empty restores the idle text, so a caller does not have to know what
    // idle looks like.
    void setMessage(const QString& text);

    // Enables or disables the document-dependent controls as one group. Stage 1 only ever called it
    // with false; Stage 2 T14 is the first caller with true.
    void setDocumentPresent(bool present);

    // THE REAL NUMBERS (S2 T14). `currentPage` is ZERO-BASED, as everything in `core/` is, and the
    // box shows it one-based -- the conversion happens here and nowhere else, because a
    // one-based page index leaking into the model is an off-by-one that reaches the tile cache.
    //
    // A `pageCount` of 0 puts the em dash back rather than showing "0": "1 of 0" is not a
    // measurement, and T9 refused to print "1 of 1 at 100 %" over a stub for the same reason.
    // S4 T12: `label` is the page's LOGICAL label from `/PageLabels` -- "iv", "A-3" -- and is empty
    // for a document that has none, which is most of them. When it is present the box shows the
    // LABEL and the total carries the physical index beside it, because a document with roman front
    // matter has two page 1s and a box showing "1" cannot say which one the reader is on.
    void setPageState(int currentPage, int pageCount, const QString& label = QString());

    // HOW A TYPED LABEL BECOMES A PAGE -- S4 T12. Returns a ZERO-BASED index, or -1 for text this
    // document does not label. The status bar cannot answer it itself: the labels live on
    // `core::PageTree`, which is the backend's, and a status bar that held a page tree would be a
    // widget holding a document.
    //
    // **THE LABEL WINS OVER THE PHYSICAL NUMBER, and that is a decision rather than an accident.**
    // The box DISPLAYS the label, so a person typing what they just read must land where they read
    // it; on `outline-labels.pdf` typing "1" therefore goes to index 3, which is the page called 1.
    // A number that is not a label is then read as a physical page, so a document with no labels
    // behaves exactly as it did before this task. **What this costs, stated so a later reader
    // subtracts it rather than adds it:** on a labelled document the physical page whose number
    // collides with a label is not reachable by typing its number -- *Go to Page* reaches the
    // label. Nothing in `Features.md` §1.4 chooses between the two, and this is the reading that
    // makes the box agree with itself.
    void setPageLabelResolver(std::function<int(const QString&)> resolver);
    void setZoom(double factor);

    int  displayedPageCount() const noexcept { return pageCount_; }
    int  displayedPage() const noexcept { return currentPage_; }

    // Background work. `total` of 0 means indeterminate. Hidden entirely when nothing is running --
    // a permanently-visible empty progress bar trains people to ignore the one place the
    // application has to tell them something is happening.
    void setTaskProgress(int done, int total);
    void clearTaskProgress();

    // Named accessors rather than findChild() at the call site: a test that looks a widget up by
    // object name is a test that keeps passing after the widget is replaced by a different one with
    // the same name.
    QLineEdit*    pageBox() const { return pageBox_; }
    QLabel*       message() const { return message_; }   // S5 T5: what a copy reported
    QLabel*       pageTotal() const { return pageTotal_; }
    QLabel*       zoomLabel() const { return zoomLabel_; }
    QLabel*       zoomValue() const { return zoomValue_; }
    QProgressBar* taskProgress() const { return taskProgress_; }

signals:
    // The page box was submitted. ONE-BASED as typed, because that is what the user typed; the
    // receiver converts. Emitted only for a number inside the document, so a typo does not scroll
    // anywhere -- the box is reset to the current page instead, which is the feedback.
    void pageRequested(int oneBasedPage);

private:
    void resetPageBox();

    std::function<int(const QString&)> labelResolver_;
    QString       currentLabel_;

    QLabel*       message_ = nullptr;
    int           currentPage_ = 0;
    int           pageCount_ = 0;
    QLineEdit*    pageBox_ = nullptr;
    QLabel*       pageTotal_ = nullptr;
    QLabel*       zoomLabel_ = nullptr;
    QLabel*       zoomValue_ = nullptr;
    QProgressBar* taskProgress_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_STATUSBAR_H
