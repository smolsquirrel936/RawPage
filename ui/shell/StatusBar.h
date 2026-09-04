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
    void setPageState(int currentPage, int pageCount);
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
