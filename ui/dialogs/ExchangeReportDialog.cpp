// ui/dialogs/ExchangeReportDialog -- implementation. Stage 6 T13. See the header for what it shows and why it asks nothing.

#include "ui/dialogs/ExchangeReportDialog.h"

#include "ui/shell/Catalog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace rawpage::ui {

ExchangeReportDialog::ExchangeReportDialog(const ExchangeReport& report, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("exchangeReport"));
    setWindowTitle(report.title);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* rows = new QVBoxLayout(this);
    auto* summary = new QLabel(report.summary, this);
    summary->setObjectName(QStringLiteral("exchangeReportSummary"));
    summary->setWordWrap(true);
    rows->addWidget(summary);

    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("exchangeReportList"));
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->setHeaderLabels({catalog::translate(catalog::key::ExchangeReportColumnComment),
                            catalog::translate(catalog::key::ExchangeReportColumnPage),
                            catalog::translate(catalog::key::ExchangeReportColumnOutcome),
                            catalog::translate(catalog::key::ExchangeReportColumnReason)});
    for (const ExchangeReport::Row& row : report.rows) {
        auto* item = new QTreeWidgetItem(list_);
        item->setText(0, row.text.isEmpty() ? row.kind : row.kind + QStringLiteral(" — “") + row.text + QStringLiteral("”"));
        item->setToolTip(0, row.name);
        item->setText(1, row.page >= 0 ? QString::number(row.page + 1) : QString());
        item->setText(2, row.outcome);
        item->setText(3, row.reason);
        item->setToolTip(3, row.reason);
    }
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    rows->addWidget(list_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    rows->addWidget(buttons);
    resize(720, 360);
}

} // namespace rawpage::ui
