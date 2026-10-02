#include "ui/panes/DifferencesPane.h"

#include "ui/shell/Catalog.h"

#include <QHeaderView>
#include <QLabel>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <array>

namespace rawpage::ui {

using core::text::Difference;
using core::text::DifferenceKind;

namespace {

constexpr int kKindCount = 11;

int kindIndex(DifferenceKind kind)
{
    return static_cast<int>(kind);
}

QString kindName(DifferenceKind kind)
{
    switch (kind) {
    case DifferenceKind::Insert:       return catalog::translate(catalog::key::CompareKindInsert);
    case DifferenceKind::Delete:       return catalog::translate(catalog::key::CompareKindDelete);
    case DifferenceKind::Replace:      return catalog::translate(catalog::key::CompareKindReplace);
    case DifferenceKind::Move:         return catalog::translate(catalog::key::CompareKindMove);
    case DifferenceKind::Whitespace:   return catalog::translate(catalog::key::CompareKindWhitespace);
    case DifferenceKind::Layout:       return catalog::translate(catalog::key::CompareKindLayout);
    case DifferenceKind::Appearance:   return catalog::translate(catalog::key::CompareKindAppearance);
    case DifferenceKind::PageInserted: return catalog::translate(catalog::key::CompareKindPageInserted);
    case DifferenceKind::PageDeleted:  return catalog::translate(catalog::key::CompareKindPageDeleted);
    case DifferenceKind::PageMoved:    return catalog::translate(catalog::key::CompareKindPageMoved);
    case DifferenceKind::PageReplaced: return catalog::translate(catalog::key::CompareKindPageReplaced);
    }
    return QString();
}

// A page number as a person reads it -- one-based -- or the "no page" mark for the side a page-level
// change has no page on.
QString pageText(int page)
{
    return page >= 0 ? QString::number(page + 1) : catalog::translate(catalog::key::CompareViewNoPage);
}

} // namespace

DifferencesPane::DifferencesPane(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("differencesPane"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(6, 6, 6, 6);
    column->setSpacing(6);

    auto* title = new QLabel(catalog::translate(catalog::key::ComparePaneTitle), this);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    column->addWidget(title);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("differencesSummary"));
    summary_->setWordWrap(true);
    column->addWidget(summary_);

    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("differencesList"));
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setAllColumnsShowFocus(true);
    list_->setHeaderLabels({catalog::translate(catalog::key::ComparePaneColumnChange),
                            catalog::translate(catalog::key::ComparePaneColumnPages),
                            catalog::translate(catalog::key::ComparePaneColumnText)});
    list_->header()->setStretchLastSection(true);
    column->addWidget(list_, 1);

    // A row CHOSEN by the person -- the arrows, a click -- and `Enter` or a double-click on the row already
    // current, which is the only way to go back to a difference after scrolling away from it.
    connect(list_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) { onCurrentItemChanged(current); });
    connect(list_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        if (item != nullptr) emit differenceChosen(list_->indexOfTopLevelItem(item));
    });
}

DifferencesPane::~DifferencesPane()
{
    // Convention 10: the two lambdas above hold `this` on a child.
    disconnect(list_, nullptr, this, nullptr);
}

QString DifferencesPane::kindText(const Difference& difference)
{
    const QString kind = kindName(difference.kind);
    if (difference.region == core::text::Region::Header)
        return catalog::translate(catalog::key::ComparePaneInRegion)
            .arg(kind, catalog::translate(catalog::key::CompareRegionHeader));
    if (difference.region == core::text::Region::Footer)
        return catalog::translate(catalog::key::ComparePaneInRegion)
            .arg(kind, catalog::translate(catalog::key::CompareRegionFooter));
    return kind;
}

void DifferencesPane::setDifferences(const std::vector<Difference>& differences)
{
    settingCurrent_ = true;
    list_->clear();

    std::array<int, kKindCount> counts{};
    QList<QTreeWidgetItem*> rows;
    rows.reserve(static_cast<qsizetype>(differences.size()));
    for (const Difference& difference : differences) {
        ++counts[static_cast<std::size_t>(kindIndex(difference.kind))];

        // THE WORDS, as the kind reads them: an insertion has only an after, a deletion only a before, and
        // a replacement or a move both. A page-level or an appearance change has no words of its own.
        QString text;
        if (!difference.before.isEmpty() && !difference.after.isEmpty())
            text = catalog::translate(catalog::key::ComparePaneTextChange).arg(difference.before, difference.after);
        else if (!difference.after.isEmpty())
            text = difference.after;
        else
            text = difference.before;

        auto* row = new QTreeWidgetItem(
            QStringList{kindText(difference),
                        catalog::translate(catalog::key::ComparePanePagePair)
                            .arg(pageText(difference.basePage), pageText(difference.revisedPage)),
                        text.left(200)});
        row->setToolTip(2, text);
        rows.push_back(row);
    }
    list_->addTopLevelItems(rows);
    list_->setCurrentItem(nullptr);
    settingCurrent_ = false;

    // THE SUMMARY: the total, then each kind that occurred, in the enum's order.
    if (differences.empty()) {
        summary_->setText(catalog::translate(catalog::key::CompareStatusNone));
        return;
    }
    QStringList kinds;
    for (int i = 0; i < kKindCount; ++i) {
        if (counts[static_cast<std::size_t>(i)] == 0) continue;
        kinds << catalog::translate(catalog::key::ComparePaneKindCount)
                     .arg(kindName(static_cast<DifferenceKind>(i)))
                     .arg(counts[static_cast<std::size_t>(i)]);
    }
    summary_->setText(catalog::translate(catalog::key::CompareStatusCount).arg(differences.size())
                      + QLatin1Char('\n') + kinds.join(catalog::translate(catalog::key::ComparePaneListSeparator)));
}

void DifferencesPane::setStatus(const QString& text)
{
    summary_->setText(text);
}

void DifferencesPane::setCurrent(int index)
{
    settingCurrent_ = true;
    QTreeWidgetItem* item = index >= 0 ? list_->topLevelItem(index) : nullptr;
    list_->setCurrentItem(item);
    if (item != nullptr) list_->scrollToItem(item);
    settingCurrent_ = false;
}

int DifferencesPane::current() const
{
    QTreeWidgetItem* item = list_->currentItem();
    return item != nullptr ? list_->indexOfTopLevelItem(item) : -1;
}

int DifferencesPane::rowCount() const
{
    return list_->topLevelItemCount();
}

QString DifferencesPane::summary() const
{
    return summary_->text();
}

void DifferencesPane::onCurrentItemChanged(QTreeWidgetItem* current)
{
    if (settingCurrent_ || current == nullptr) return;
    emit differenceChosen(list_->indexOfTopLevelItem(current));
}

} // namespace rawpage::ui
