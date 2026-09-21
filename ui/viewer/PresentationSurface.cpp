#include "ui/viewer/PresentationSurface.h"

#include "ui/viewer/PageView.h"

#include <QCloseEvent>
#include <QElapsedTimer>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

// The laser dot, in logical pixels. Large enough to be seen from the back of a room on a projector
// and small enough to point at a word.
constexpr int kLaserRadius = 9;

// How often the transition repaints. 60 Hz is the frame budget every other measurement in this
// project is against (A2), so a transition that asked for more would be asking for frames nobody
// sees.
constexpr int kTransitionIntervalMs = 16;

} // namespace

// The thing painted OVER the page: the blank, the laser and the outgoing frame of a transition.
// A child widget rather than a paint inside `PageView`, so that none of it can reach the scheduler
// or a tile -- D4-11's argument at a third subject.
class PresentationOverlay : public QWidget {
public:
    explicit PresentationOverlay(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("presentationOverlay"));

        // IT MUST NOT EAT THE MOUSE. The page beneath still has a tool armed, and a presenter who
        // moved the pointer would otherwise be moving it over a pane that never sees it.
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);

        timer_.setInterval(kTransitionIntervalMs);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] {
            if (elapsed_.elapsed() >= durationMs_) {
                timer_.stop();
                outgoing_ = QPixmap();       // released the moment it stops being painted
            }
            update();
        });
    }

    void setBlank(int blank)
    {
        if (blank_ == blank) return;
        blank_ = blank;
        update();
    }

    void setLaser(bool on)
    {
        if (laser_ == on) return;
        laser_ = on;
        update();
    }

    bool laser() const noexcept { return laser_; }

    void setLaserAt(QPoint position)
    {
        laserAt_ = position;
        if (laser_) update();
    }

    void begin(core::TransitionStyle style, double seconds, const QPixmap& outgoing)
    {
        // `None` CAPTURES NOTHING, which is what makes the common case free: almost no document
        // asks for a transition and the default preference is `none`.
        if (style == core::TransitionStyle::None || outgoing.isNull()) {
            timer_.stop();
            outgoing_ = QPixmap();
            update();
            return;
        }

        style_ = style;
        outgoing_ = outgoing;
        durationMs_ = seconds > 0.0 ? static_cast<int>(seconds * 1000.0) : 1000;
        elapsed_.start();
        timer_.start();
        update();
    }

    bool running() const noexcept { return timer_.isActive(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);

        // THE OUTGOING FRAME FIRST, so the new page is what shows through it as it goes.
        if (!outgoing_.isNull() && durationMs_ > 0) {
            const double progress =
                std::min(1.0, static_cast<double>(elapsed_.elapsed()) / durationMs_);
            switch (style_) {
            case core::TransitionStyle::Fade:
            case core::TransitionStyle::Dissolve:
                // ONE PAINT FOR BOTH, and the difference is stated rather than faked: a true
                // dissolve is a per-pixel random threshold and this is a cross-fade. What a person
                // sees differs only on hard edges, and a second implementation for that would be
                // an animation nobody asked to tell apart. Named here so it is subtracted rather
                // than assumed (convention 19).
                painter.setOpacity(1.0 - progress);
                painter.drawPixmap(0, 0, outgoing_);
                break;
            case core::TransitionStyle::Wipe: {
                // The outgoing frame is CLIPPED BACK from the left, so the new page appears to
                // slide in over it. `/Dm` and `/Di` would choose the axis and direction; this
                // draws the horizontal case and the model carries what the file said, so the day
                // the other three are built there is a value to read.
                const int edge = static_cast<int>(width() * progress);
                painter.drawPixmap(QRect(edge, 0, width() - edge, height()), outgoing_,
                                   QRect(edge, 0, width() - edge, height()));
                break;
            }
            case core::TransitionStyle::None:
                break;
            }
            painter.setOpacity(1.0);
        }

        // THE BLANK COVERS EVERYTHING, the laser included: a blanked screen that still showed a
        // dot would be a screen that is not blank.
        if (blank_ != 0) {
            painter.fillRect(rect(), blank_ == 1 ? Qt::black : Qt::white);
            return;
        }

        if (laser_) {
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(220, 30, 30, 190));
            painter.drawEllipse(laserAt_, kLaserRadius, kLaserRadius);
        }
    }

private:
    int     blank_ = 0;                 // 0 none, 1 black, 2 white
    bool    laser_ = false;
    QPoint  laserAt_;

    core::TransitionStyle style_ = core::TransitionStyle::None;
    QPixmap               outgoing_;
    QTimer                timer_;
    QElapsedTimer         elapsed_;
    int                   durationMs_ = 0;
};

PresentationSurface::PresentationSurface(core::render::RenderScheduler& scheduler,
                                         const platform::IDisplayTopology* topology,
                                         QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("presentationSurface"));

    // BLACK, AND IT IS NOT A THEME COLOUR. Convention 20 asks that a colour be judged in both
    // themes; the surround of a presented page is judged in neither, because a projector shows
    // whatever is not the page and black is what every presentation tool in this domain puts
    // there. A palette colour would make the desk light up in a light theme.
    QPalette dark = palette();
    dark.setColor(QPalette::Window, Qt::black);
    setPalette(dark);
    setAutoFillBackground(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    view_ = new PageView(scheduler, topology, this);
    view_->setFrameShape(QFrame::NoFrame);

    // NO SCROLLBARS. A presented page fills the display and a scrollbar on a projector is chrome
    // the audience can see; paging is what moves between pages here, not scrolling.
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    column->addWidget(view_);

    overlay_ = new PresentationOverlay(this);
    overlay_->setGeometry(rect());
    overlay_->raise();

    // The laser follows the pointer over the PAGE, which is the only widget that receives moves --
    // the overlay is transparent to them by construction.
    connect(view_, &PageView::cursorMoved, this, [this](QPoint position, bool onSurface) {
        if (onSurface) overlay_->setLaserAt(view_->viewport()->mapTo(this, position));
    });
}

PresentationSurface::~PresentationSurface() = default;

void PresentationSurface::setBlank(int blank) { overlay_->setBlank(blank); }

void PresentationSurface::setLaserPointer(bool on) { overlay_->setLaser(on); }

bool PresentationSurface::laserPointer() const noexcept { return overlay_->laser(); }

void PresentationSurface::setCursorVisible(bool visible)
{
    // ON THE VIEWPORT, not on this window: the pointer is over the page, and a cursor set on the
    // parent is overridden by any child that has one of its own -- which `PageView` always does,
    // because its armed tool sets it.
    if (visible) view_->viewport()->unsetCursor();
    else         view_->viewport()->setCursor(Qt::BlankCursor);
}

void PresentationSurface::beginTransition(core::TransitionStyle style, double seconds)
{
    // CAPTURED BEFORE THE NEW PAGE IS DRAWN, which is why the caller turns the page after calling
    // this. `grab()` on the viewport is one viewport-sized pixmap and it is released the moment
    // the animation ends.
    QPixmap outgoing;
    if (style != core::TransitionStyle::None && view_->viewport()->width() > 0)
        outgoing = view_->viewport()->grab();
    overlay_->begin(style, seconds, outgoing);
}

bool PresentationSurface::transitionRunning() const noexcept { return overlay_->running(); }

void PresentationSurface::closeEvent(QCloseEvent* event)
{
    emit closed();
    QWidget::closeEvent(event);
}

void PresentationSurface::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // The overlay is not in the layout -- it sits ON the page rather than beside it -- so its
    // geometry is followed by hand. A layout would give it a share of the space instead.
    overlay_->setGeometry(rect());
    overlay_->raise();
}

} // namespace rawpage::ui
