#include "ui/shell/MessageBar.h"

#include "core/doc/RepairReport.h"
#include "ui/shell/Catalog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace rawpage::ui {
namespace {

// Beyond this many entries the detail list is a wall rather than a report. A file with 4,000
// reconstructed objects is not diagnosed by reading 4,000 lines, and the count is already in the
// banner's own summary -- so the list is truncated and SAYS it was truncated, rather than silently
// stopping and leaving the reader to wonder whether that was all of them.
constexpr int kMaxDetailEntries = 200;

} // namespace

const char* MessageBar::kindName(Kind kind) noexcept
{
    switch (kind) {
    case Kind::Repaired: return "Repaired";
    case Kind::Protected: return "Protected";
    case Kind::Signed: return "Signed";
    case Kind::HasForm: return "HasForm";
    case Kind::WriteMode: return "WriteMode";
    }
    return "Unknown";
}

MessageBar::MessageBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("messageBar"));

    column_ = new QVBoxLayout(this);
    column_->setContentsMargins(0, 0, 0, 0);
    column_->setSpacing(1);

    // HIDDEN WHEN EMPTY, and hidden rather than zero-height. A permanently-present empty strip
    // above the page trains people to ignore the one place the application tells them the document
    // is not what it appears to be -- the same argument `StatusBar` makes about its progress bar.
    setVisible(false);
}

MessageBar::~MessageBar() = default;

void MessageBar::showMessage(Message message)
{
    // ONE BANNER PER KIND. Replacing rather than stacking, because the second report of the same
    // condition is the same condition.
    dismiss(message.kind);

    auto* banner = new QWidget(this);
    banner->setObjectName(QString::fromLatin1("messageBanner.")
                          + QLatin1String(kindName(message.kind)));
    banner->setAutoFillBackground(true);

    // FROM THE PALETTE, not a hard-coded amber. A banner is exactly the kind of surface that gets
    // written against a light mock and turns out to be unreadable in dark -- which is what I-40 was
    // (convention 20). `AlternateBase` is one step off the window in both themes by construction.
    QPalette bannerPalette = banner->palette();
    bannerPalette.setColor(QPalette::Window, bannerPalette.color(QPalette::AlternateBase));
    banner->setPalette(bannerPalette);

    auto* row = new QHBoxLayout(banner);
    row->setContentsMargins(12, 6, 6, 6);
    row->setSpacing(8);

    auto* label = new QLabel(message.text, banner);
    label->setObjectName(QStringLiteral("messageText"));
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(label, 1);

    if (!message.actionLabel.isEmpty() && message.action) {
        auto* action = new QPushButton(message.actionLabel, banner);
        action->setObjectName(QStringLiteral("messageAction"));

        // The handler is copied into the connection and the button owns the connection, so it dies
        // with the banner -- convention 10's rule, in the form where the lambda would otherwise
        // outlive the widget that installed it.
        QObject::connect(action, &QPushButton::clicked, action,
                         [handler = message.action] { handler(); });
        row->addWidget(action, 0);
    }

    auto* close = new QToolButton(banner);
    close->setObjectName(QStringLiteral("messageDismiss"));
    close->setText(QStringLiteral("✕"));
    close->setAutoRaise(true);
    close->setToolTip(catalog::translate(catalog::key::MessageBarDismiss));
    const Kind kind = message.kind;
    QObject::connect(close, &QToolButton::clicked, close, [this, kind] { dismiss(kind); });
    row->addWidget(close, 0);

    entries_.push_back(Entry{kind, banner, message.text});
    rebuildOrder();
}

void MessageBar::rebuildOrder()
{
    // WORST FIRST, by the enumerator's own order -- see the header on why `Kind` is ordered by
    // consequence. A document that is both protected and repaired says both, and the one that
    // changes what a save does is on top.
    std::stable_sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });

    for (const Entry& entry : entries_) {
        column_->removeWidget(entry.widget);
    }
    for (const Entry& entry : entries_) {
        column_->addWidget(entry.widget);

        // SHOWN EXPLICITLY. A widget built with a parent that is already on screen starts hidden,
        // and a hidden widget is an empty layout item -- which is Stage 1's I-49 exactly, where
        // every Quick Access Toolbar customization produced invisible buttons and a 4-pixel strip.
        entry.widget->show();
    }

    setVisible(!entries_.empty());
    emit messageCountChanged(static_cast<int>(entries_.size()));
}

void MessageBar::dismiss(Kind kind)
{
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [kind](const Entry& entry) { return entry.kind == kind; });
    if (found == entries_.end()) return;

    QWidget* widget = found->widget;
    entries_.erase(found);
    column_->removeWidget(widget);
    widget->deleteLater();

    setVisible(!entries_.empty());
    emit messageCountChanged(static_cast<int>(entries_.size()));
}

void MessageBar::clear()
{
    for (const Entry& entry : entries_) {
        column_->removeWidget(entry.widget);
        entry.widget->deleteLater();
    }
    entries_.clear();
    setVisible(false);
    emit messageCountChanged(0);
}

bool MessageBar::isShowing(Kind kind) const
{
    return std::any_of(entries_.begin(), entries_.end(),
                       [kind](const Entry& entry) { return entry.kind == kind; });
}

int MessageBar::messageCount() const
{
    return static_cast<int>(entries_.size());
}

QString MessageBar::textFor(Kind kind) const
{
    for (const Entry& entry : entries_)
        if (entry.kind == kind) return entry.text;
    return {};
}

QString MessageBar::repairDetailText(const core::RepairReport& report)
{
    QString text = catalog::translate(catalog::key::RepairDetailIntro)
                       .arg(static_cast<int>(report.entries().size()));
    text += QLatin1String("\n\n");

    int shown = 0;
    for (const core::RepairEntry& entry : report.entries()) {
        if (shown >= kMaxDetailEntries) break;
        ++shown;

        // THE CODES ARE CODES. `repairKindName` and `repairSeverityName` are declared ASCII, stable
        // and non-localized by `core/doc/RepairReport.h` -- they are what the log holds and what a
        // bug report should quote, and translating them would put a different string in front of
        // every reporter. The frame around them is localized; this line is a diagnostic.
        text += QStringLiteral("%1  %2")
                    .arg(QLatin1String(core::repairKindName(entry.kind)), -24)
                    .arg(QLatin1String(core::repairSeverityName(entry.severity)));
        if (entry.objectNumber >= 0)
            text += QStringLiteral("  obj %1").arg(entry.objectNumber);
        text += QStringLiteral("  @%1").arg(static_cast<qulonglong>(entry.offset));
        text += QLatin1Char('\n');
    }

    if (shown < static_cast<int>(report.entries().size())) {
        text += QLatin1Char('\n');
        text += catalog::translate(catalog::key::RepairDetailTruncated)
                    .arg(static_cast<int>(report.entries().size()) - shown);
    }

    return text;
}

void MessageBar::showRepairReport(const core::RepairReport& report, const QString& documentName,
                                  QWidget* detailParent)
{
    // AN EMPTY REPORT SHOWS NOTHING. "Opening a clean file shows no bar" is half of guide case
    // A-58, and it is the half that would be lost by a bar that said "no repairs were needed" --
    // which is a sentence nobody needs and which would make the real banner unremarkable.
    if (report.isEmpty()) {
        dismiss(Kind::Repaired);
        return;
    }

    Message message;
    message.kind = Kind::Repaired;

    // The summary is a CATALOG KEY plus arguments, produced by `core/` and resolved here. That
    // split is the whole reason `RepairReport::summaryKey()` exists rather than a `summary()`
    // returning a sentence -- an English summary in `core/` is the single change that makes the
    // product unshippable in `zh-TW` while looking reasonable in review.
    QString text = catalog::translate(QString(report.summaryKey()));
    const QStringList arguments = report.summaryArguments();
    for (const QString& argument : arguments) text = text.arg(argument);

    message.text = documentName.isEmpty()
                       ? text
                       : catalog::translate(catalog::key::MessageBarDocumentPrefix)
                             .arg(documentName, text);

    message.actionLabel = catalog::translate(catalog::key::MessageBarDetails);

    // The report is COPIED into the closure. It belongs to the backend and the document can be
    // closed while this banner is on screen -- and a banner whose Details button reads a destroyed
    // report is a crash reached by clicking a button that looks harmless.
    const QString detail = repairDetailText(report);
    const QString title = catalog::translate(catalog::key::RepairDetailTitle);
    message.action = [detail, title, detailParent] {
        QMessageBox box(detailParent);
        box.setWindowTitle(title);
        box.setText(detail);
        box.setTextFormat(Qt::PlainText);
        box.setIcon(QMessageBox::Information);
        box.exec();
    };

    showMessage(std::move(message));
}

} // namespace rawpage::ui
