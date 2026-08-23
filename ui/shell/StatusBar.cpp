#include "ui/shell/StatusBar.h"

#include "ui/shell/Catalog.h"

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

    zoomLabel_ = new QLabel(catalog::translate(catalog::key::StatusZoom), this);
    zoomLabel_->setObjectName(QStringLiteral("statusZoom"));
    addPermanentWidget(zoomLabel_);

    setDocumentPresent(false);
}

StatusBar::~StatusBar() = default;

void StatusBar::setMessage(const QString& text)
{
    message_->setText(text.isEmpty() ? catalog::translate(catalog::key::StatusReady) : text);
}

void StatusBar::setDocumentPresent(bool present)
{
    pageBox_->setEnabled(present);
    pageTotal_->setEnabled(present);
    zoomLabel_->setEnabled(present);

    // The total reads as a dash rather than as "0" while there is nothing open: "0" is a
    // measurement, and there is no measurement to report. The dash is a CATALOG ENTRY rather than
    // a literal -- i18n-lint caught it at T9 and was right to: CJK typography sets a dash
    // differently, and "it is only punctuation" is how a Latin em dash ends up in the middle of a
    // Chinese status bar.
    pageTotal_->setText(present ? QString()
                                : catalog::translate(catalog::key::StatusPageCountUnknown));

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
