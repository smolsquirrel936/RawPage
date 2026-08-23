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

    // Enables or disables the document-dependent controls as one group. Stage 1 only ever calls it
    // with false; Stage 2 is the first caller with true.
    void setDocumentPresent(bool present);

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
    QProgressBar* taskProgress() const { return taskProgress_; }

private:
    QLabel*       message_ = nullptr;
    QLineEdit*    pageBox_ = nullptr;
    QLabel*       pageTotal_ = nullptr;
    QLabel*       zoomLabel_ = nullptr;
    QProgressBar* taskProgress_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_STATUSBAR_H
