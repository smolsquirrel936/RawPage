#include "ui/shell/StatusBar.h"

#include "ui/shell/Catalog.h"

#include <algorithm>

#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>

namespace rawpage::ui {

StatusBar::StatusBar(QWidget* parent)
    : QStatusBar(parent)
{
    setObjectName(QStringLiteral("statusBar"));
    setSizeGripEnabled(true);

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("statusMessage"));
    addWidget(message_, 1);

    taskProgress_ = new QProgressBar(this);
    taskProgress_->setObjectName(QStringLiteral("statusTaskProgress"));
    taskProgress_->setMaximumWidth(140);
    taskProgress_->setTextVisible(false);
    taskProgress_->hide();
    addPermanentWidget(taskProgress_);

    auto* pageLabel = new QLabel(catalog::translate(catalog::key::StatusPage), this);
    pageLabel->setObjectName(QStringLiteral("statusPageLabel"));
    addPermanentWidget(pageLabel);

    pageBox_ = new QLineEdit(this);
    pageBox_->setObjectName(QStringLiteral("statusPageBox"));
    pageBox_->setMaximumWidth(56);
    pageBox_->setAlignment(Qt::AlignRight);
    addPermanentWidget(pageBox_);

    pageTotal_ = new QLabel(this);
    pageTotal_->setObjectName(QStringLiteral("statusPageTotal"));
    addPermanentWidget(pageTotal_);

    // TYPING A PAGE NUMBER NAVIGATES, which is the only thing in the status bar that is an input
    // rather than a readout -- and it was an input with no consumer from T9 until now.
    connect(pageBox_, &QLineEdit::returnPressed, this, [this] {
        bool ok = false;
        const int typed = pageBox_->text().trimmed().toInt(&ok);
        if (!ok || pageCount_ <= 0 || typed < 1 || typed > pageCount_) {
            // A typo is not a navigation. Putting the current page back is the feedback: an empty
            // box, or one left holding "4000", would leave the user unsure whether it worked.
            resetPageBox();
            return;
        }
        emit pageRequested(typed);
    });

    zoomLabel_ = new QLabel(catalog::translate(catalog::key::StatusZoom), this);
    zoomLabel_->setObjectName(QStringLiteral("statusZoom"));
    addPermanentWidget(zoomLabel_);

    zoomValue_ = new QLabel(this);
    zoomValue_->setObjectName(QStringLiteral("statusZoomValue"));
    addPermanentWidget(zoomValue_);

    setDocumentPresent(false);
}

StatusBar::~StatusBar() = default;

void StatusBar::setMessage(const QString& text)
{
    message_->setText(text.isEmpty() ? catalog::translate(catalog::key::StatusReady) : text);
}

void StatusBar::resetPageBox()
{
    pageBox_->setText(pageCount_ > 0 ? QString::number(currentPage_ + 1) : QString());
}

void StatusBar::setPageState(int currentPage, int pageCount)
{
    pageCount_ = std::max(0, pageCount);
    currentPage_ = pageCount_ > 0 ? std::clamp(currentPage, 0, pageCount_ - 1) : 0;

    // THE EM DASH COMES BACK for a document with no pages. It is not a leftover from Stage 1: "of
    // 0" is a claim about a measurement that was not made, and the dash is the honest answer --
    // which is why it stayed a catalog entry rather than becoming a literal (i18n-lint, T9).
    pageTotal_->setText(pageCount_ > 0
                            ? catalog::translate(catalog::key::StatusPageTotal)
                                  .arg(pageCount_)
                            : catalog::translate(catalog::key::StatusPageCountUnknown));

    // ONE-BASED ON SCREEN, zero-based everywhere else. The conversion is here and nowhere else.
    resetPageBox();
}

void StatusBar::setZoom(double factor)
{
    // Rounded to whole percent. A zoom of 1.3333 reading "133.33 %" is a status bar that changes
    // width as you scroll, and nobody wants the second decimal of a zoom.
    zoomValue_->setText(catalog::translate(catalog::key::StatusZoomValue)
                            .arg(qRound(factor * 100.0)));
}

void StatusBar::setDocumentPresent(bool present)
{
    pageBox_->setEnabled(present);
    pageTotal_->setEnabled(present);
    zoomLabel_->setEnabled(present);
    zoomValue_->setEnabled(present);

    // The total reads as a dash rather than as "0" while there is nothing open: "0" is a
    // measurement, and there is no measurement to report. The dash is a CATALOG ENTRY rather than
    // a literal -- i18n-lint caught it at T9 and was right to: CJK typography sets a dash
    // differently, and "it is only punctuation" is how a Latin em dash ends up in the middle of a
    // Chinese status bar.
    // NOT BLANKED WHEN A DOCUMENT ARRIVES. `setPageState` fills it in, and blanking here would
    // put an empty gap on screen for however long it takes the caller to get there -- which at
    // Stage 1 was forever, because nothing ever called it with a count.
    if (!present) {
        pageCount_ = 0;
        currentPage_ = 0;
        pageTotal_->setText(catalog::translate(catalog::key::StatusPageCountUnknown));
        zoomValue_->clear();
    }

    if (present) {
        setMessage(QString());
    } else {
        pageBox_->clear();
        setMessage(catalog::translate(catalog::key::StatusNoDocument));
    }
}

void StatusBar::setTaskProgress(int done, int total)
{
    if (total <= 0) {
        taskProgress_->setRange(0, 0);   // Qt's spelling of "indeterminate"
    } else {
        taskProgress_->setRange(0, total);
        taskProgress_->setValue(done);
    }
    taskProgress_->show();
}

void StatusBar::clearTaskProgress()
{
    taskProgress_->reset();
    taskProgress_->hide();
    taskProgress_->setToolTip(catalog::translate(catalog::key::StatusTasksIdle));
}

} // namespace rawpage::ui
