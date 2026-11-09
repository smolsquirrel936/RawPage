#include "ui/dialogs/MergeDialog.h"

#include "core/doc/PageRanges.h"
#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace rawpage::ui {
namespace {

MergeDialog::Answer& installedMerge()
{
    static MergeDialog::Answer answer;
    return answer;
}

bool aPersonCanAnswer()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

constexpr int kPathRole = Qt::UserRole;        // on a row's file item: its path
constexpr int kCountRole = Qt::UserRole + 1;   // on a row's count item: its page count, or -1

QString mergeBookmarksKey(core::MergeBookmarks value)
{
    const std::string_view name = core::mergeBookmarksName(value);
    return QStringLiteral("prefs.choice.mergeBookmarks.") + QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

// The local paths a drag carries -- files only, in the order dragged.
QStringList pathsOf(const QMimeData* data)
{
    QStringList out;
    if (data == nullptr || !data->hasUrls()) return out;
    for (const QUrl& url : data->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) out << url.toLocalFile();
    }
    return out;
}

} // namespace

MergeDialog::MergeDialog(const core::MergeOptions& defaults, Counter counter, QWidget* parent)
    : QDialog(parent), counter_(std::move(counter))
{
    setObjectName(QStringLiteral("mergeDialog"));
    setWindowTitle(catalog::translate(catalog::key::MergeTitle));
    setModal(true);
    setAcceptDrops(true);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);

    column->addWidget(new QLabel(catalog::translate(catalog::key::MergeDropHint), this));
    auto* listRow = new QHBoxLayout();
    table_ = new QTableWidget(0, 3, this);
    table_->setObjectName(QStringLiteral("mergeInputs"));
    table_->setHorizontalHeaderLabels({catalog::translate(catalog::key::MergeColumnFile), catalog::translate(catalog::key::MergeColumnPages),
                                       catalog::translate(catalog::key::MergeColumnCount)});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(FileColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(RangeColumn, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(CountColumn, QHeaderView::ResizeToContents);
    table_->setMinimumSize(560, 240);
    listRow->addWidget(table_, 1);
    auto* buttons = new QVBoxLayout();
    add_ = new QPushButton(catalog::translate(catalog::key::MergeAdd), this);
    remove_ = new QPushButton(catalog::translate(catalog::key::MergeRemove), this);
    up_ = new QPushButton(catalog::translate(catalog::key::MergeUp), this);
    down_ = new QPushButton(catalog::translate(catalog::key::MergeDown), this);
    for (QPushButton* button : {add_, remove_, up_, down_}) buttons->addWidget(button);
    buttons->addStretch(1);
    listRow->addLayout(buttons);
    column->addLayout(listRow);
    connect(add_, &QPushButton::clicked, this, [this] {
        const QStringList chosen = QFileDialog::getOpenFileNames(this, catalog::translate(catalog::key::MergeChooseFiles), QString(),
                                                                 catalog::translate(catalog::key::DialogPdfFilter));
        addFiles(chosen);
    });
    connect(remove_, &QPushButton::clicked, this, &MergeDialog::removeSelected);
    connect(up_, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, &MergeDialog::updateButtons);

    auto* options = new QGroupBox(catalog::translate(catalog::key::MergeOptionsGroup), this);
    auto* optionsColumn = new QVBoxLayout(options);
    auto* bookmarkRow = new QHBoxLayout();
    bookmarkRow->addWidget(new QLabel(catalog::translate(catalog::key::MergeBookmarksLabel), options));
    bookmarks_ = new QComboBox(options);
    bookmarks_->setObjectName(QStringLiteral("bookmarksBox"));
    for (const core::MergeBookmarks value : core::kMergeBookmarks) {
        bookmarks_->addItem(catalog::translate(mergeBookmarksKey(value)), static_cast<int>(value));
        if (value == defaults.bookmarks) bookmarks_->setCurrentIndex(bookmarks_->count() - 1);
    }
    bookmarkRow->addWidget(bookmarks_, 1);
    optionsColumn->addLayout(bookmarkRow);
    rename_ = new QRadioButton(catalog::translate(catalog::key::MergeFieldsRename), options);
    merge_ = new QRadioButton(catalog::translate(catalog::key::MergeFieldsMerge), options);
    (defaults.renameCollidingFields ? rename_ : merge_)->setChecked(true);
    optionsColumn->addWidget(rename_);
    optionsColumn->addWidget(merge_);
    separator_ = new QCheckBox(catalog::translate(catalog::key::MergeSeparator), options);
    separator_->setChecked(defaults.separatorPages);
    optionsColumn->addWidget(separator_);
    column->addWidget(options);

    refusal_ = new QLabel(this);
    refusal_->setObjectName(QStringLiteral("refusal"));
    refusal_->setWordWrap(true);
    refusal_->hide();
    column->addWidget(refusal_);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(catalog::translate(catalog::key::MergeAccept));
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(box);
    updateButtons();
}

void MergeDialog::addFiles(const QStringList& paths)
{
    for (const QString& path : paths) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* file = new QTableWidgetItem(QFileInfo(path).fileName());
        file->setData(kPathRole, path);
        file->setToolTip(QDir::toNativeSeparators(path));
        file->setFlags(file->flags() & ~Qt::ItemIsEditable);
        auto* range = new QTableWidgetItem();
        range->setToolTip(catalog::translate(catalog::key::MergeAllPages));
        auto* count = new QTableWidgetItem();
        count->setFlags(count->flags() & ~Qt::ItemIsEditable);
        // What the merge will find there: its pages, or why it will be skipped.
        const core::Result<int> pages = counter_ ? counter_(path) : core::Result<int>(core::Error{core::ErrorCode::Unsupported, QLatin1String("error.open.notFound"), path});
        if (pages) {
            count->setData(kCountRole, pages.value());
            count->setText(catalog::translate(catalog::key::MergePageCount).arg(pages.value()));
        } else {
            count->setData(kCountRole, -1);
            count->setText(catalog::translate(catalog::key::MergeWillSkip)
                               .arg(catalog::translateError(QString(pages.error().messageKey), pages.error().context)));
        }
        table_->setItem(row, FileColumn, file);
        table_->setItem(row, RangeColumn, range);
        table_->setItem(row, CountColumn, count);
    }
    refusal_->hide();
    updateButtons();
}

void MergeDialog::moveSelected(int by)
{
    std::set<int> rows;
    for (const QModelIndex& index : table_->selectionModel()->selectedRows()) rows.insert(index.row());
    if (rows.empty() || (by < 0 && *rows.begin() == 0) || (by > 0 && *rows.rbegin() == table_->rowCount() - 1)) return;
    const auto swap = [this](int a, int b) {
        for (int column = 0; column < table_->columnCount(); ++column) {
            QTableWidgetItem* first = table_->takeItem(a, column);
            QTableWidgetItem* second = table_->takeItem(b, column);
            table_->setItem(a, column, second);
            table_->setItem(b, column, first);
        }
    };
    std::vector<int> order(rows.begin(), rows.end());
    if (by > 0) std::reverse(order.begin(), order.end());
    for (const int row : order) swap(row, row + by);
    table_->clearSelection();
    for (const int row : rows) {
        for (int column = 0; column < table_->columnCount(); ++column) table_->item(row + by, column)->setSelected(true);
    }
}

void MergeDialog::removeSelected()
{
    std::set<int> rows;
    for (const QModelIndex& index : table_->selectionModel()->selectedRows()) rows.insert(index.row());
    for (auto row = rows.rbegin(); row != rows.rend(); ++row) table_->removeRow(*row);
    updateButtons();
}

void MergeDialog::updateButtons()
{
    const bool selected = table_->selectionModel() != nullptr && !table_->selectionModel()->selectedRows().isEmpty();
    remove_->setEnabled(selected);
    up_->setEnabled(selected);
    down_->setEnabled(selected);
}

core::Result<MergeDialog::Choice> MergeDialog::choice() const
{
    if (table_->rowCount() == 0) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.merge.noInputs"), QString()};
    Choice out;
    for (int row = 0; row < table_->rowCount(); ++row) {
        core::MergeInput input;
        input.path = table_->item(row, FileColumn)->data(kPathRole).toString();
        const int count = table_->item(row, CountColumn)->data(kCountRole).toInt();
        const QString range = table_->item(row, RangeColumn)->text().trimmed();
        // A file the merge will skip is listed all the same: its range is no one's to check.
        if (!range.isEmpty() && count > 0) {
            core::Result<std::vector<int>> pages = core::parsePageRanges(range, count);
            if (!pages) {
                return core::Error{pages.error().code, QLatin1String("error.merge.badRange"),
                                   catalog::translate(catalog::key::MergeRangeRefused)
                                       .arg(table_->item(row, FileColumn)->text(),
                                            catalog::translateError(QString(pages.error().messageKey), pages.error().context))};
            }
            input.pages = std::move(pages).value();
        }
        out.inputs.push_back(std::move(input));
    }
    out.options.bookmarks = static_cast<core::MergeBookmarks>(bookmarks_->currentData().toInt());
    out.options.renameCollidingFields = rename_->isChecked();
    out.options.separatorPages = separator_->isChecked();
    return out;
}

void MergeDialog::accept()
{
    const core::Result<Choice> chosen = choice();
    if (!chosen) {
        refusal_->setText(catalog::translateError(QString(chosen.error().messageKey), chosen.error().context));
        refusal_->show();
        return;
    }
    QDialog::accept();
}

void MergeDialog::dragEnterEvent(QDragEnterEvent* event)
{
    if (!pathsOf(event->mimeData()).isEmpty()) event->acceptProposedAction();
}

void MergeDialog::dropEvent(QDropEvent* event)
{
    const QStringList paths = pathsOf(event->mimeData());
    if (paths.isEmpty()) return;
    addFiles(paths);
    event->acceptProposedAction();
}

bool MergeDialog::canAsk()
{
    return static_cast<bool>(installedMerge()) || aPersonCanAnswer();
}

std::optional<MergeDialog::Choice> MergeDialog::ask(QWidget* parent, const core::MergeOptions& defaults, Counter counter,
                                                    const QStringList& initial)
{
    if (installedMerge()) return installedMerge()(defaults);
    if (!aPersonCanAnswer()) return std::nullopt;
    MergeDialog dialog(defaults, std::move(counter), parent);
    dialog.addFiles(initial);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void MergeDialog::setAnswerForTesting(Answer answer)
{
    installedMerge() = std::move(answer);
}

} // namespace rawpage::ui
