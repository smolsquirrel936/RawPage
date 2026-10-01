#include "ui/panes/SearchPane.h"

#include "core/settings/Settings.h"
#include "core/text/SearchQuery.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/ShortcutBinder.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace rawpage::ui {
namespace {

using core::text::SearchResults;

// The roles a result row carries: its index into `rows_`, and the snippet's three parts.
constexpr int kRowRole = Qt::UserRole + 1;
constexpr int kBeforeRole = Qt::UserRole + 2;
constexpr int kMatchRole = Qt::UserRole + 3;
constexpr int kAfterRole = Qt::UserRole + 4;

enum Column { FileColumn = 0, PageColumn = 1, SnippetColumn = 2 };

// WHETHER THERE IS ANYBODY TO ASK (Stage 1's I-23): a modal dialog under the offscreen platform is shown to
// nobody and never returns. `MainWindow`'s own predicate, for this pane's two dialogs.
bool canPromptTheUser()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

// THE HIT, MARKED (guide A-82): the snippet's match in bold between its words, elided at the right.
class SnippetDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (!index.data(kMatchRole).isValid()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem plain = option;
        initStyleOption(&plain, index);
        plain.text.clear();
        plain.widget->style()->drawControl(QStyle::CE_ItemViewItem, &plain, painter, plain.widget);

        const QString before = index.data(kBeforeRole).toString();
        const QString match = index.data(kMatchRole).toString();
        const QString after = index.data(kAfterRole).toString();
        QFont bold = option.font;
        bold.setBold(true);
        const QFontMetrics regularMetrics(option.font);
        const QFontMetrics boldMetrics(bold);
        QRect area = option.rect.adjusted(4, 0, -4, 0);
        painter->save();
        painter->setPen(option.palette.color(option.state & QStyle::State_Selected ? QPalette::HighlightedText
                                                                                   : QPalette::Text));
        const auto draw = [&](const QString& text, const QFont& font, const QFontMetrics& metrics) {
            if (area.width() <= 0 || text.isEmpty()) return;
            const QString shown = metrics.elidedText(text, Qt::ElideRight, area.width());
            painter->setFont(font);
            painter->drawText(area, Qt::AlignLeft | Qt::AlignVCenter, shown);
            area.setLeft(area.left() + metrics.horizontalAdvance(shown));
        };
        draw(before, option.font, regularMetrics);
        draw(match, bold, boldMetrics);
        draw(after, option.font, regularMetrics);
        painter->restore();
    }
};

std::optional<QDate> dateFrom(const QString& text)
{
    if (text.trimmed().isEmpty()) return std::nullopt;
    const QDate date = QDate::fromString(text.trimmed(), Qt::ISODate);
    if (!date.isValid()) return std::nullopt;
    return date;
}

} // namespace

SearchPane::SearchPane(app::Services services, QWidget* parent)
    : QDockWidget(catalog::translate(catalog::key::SearchPaneTitle), parent), services_(services)
{
    setObjectName(QStringLiteral("searchPane"));
    folder_ = std::make_unique<core::text::FolderSearch>(services_.backend, services_.renderScheduler,
                                                          services_.documentIndex, services_.searchEngine);
    // THE FOLDER SEARCH'S PUMP: its helper thread's results are posted to this object and delivered on the
    // UI thread -- a posted event for a destroyed object is dropped, which is what makes a wake still in
    // flight at teardown harmless.
    folder_->setWakeHandler([this] {
        QMetaObject::invokeMethod(this, [this] { if (folder_) folder_->deliverReady(); }, Qt::QueuedConnection);
    });

    auto* body = new QWidget(this);
    // §8's FIND / SEARCH TABLE applies anywhere inside it (the header).
    ShortcutBinder::declareContext(body, core::KeyContext::FindSearch);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(6, 6, 6, 6);
    column->setSpacing(4);

    auto* queryRow = new QHBoxLayout;
    query_ = new QLineEdit(body);
    query_->setObjectName(QStringLiteral("searchQuery"));
    query_->setPlaceholderText(catalog::translate(catalog::key::SearchPaneQueryPlaceholder));
    searchButton_ = new QPushButton(catalog::translate(catalog::key::SearchPaneSearch), body);
    stopButton_ = new QPushButton(catalog::translate(catalog::key::SearchPaneStop), body);
    stopButton_->setEnabled(false);
    queryRow->addWidget(query_, 1);
    queryRow->addWidget(searchButton_);
    queryRow->addWidget(stopButton_);
    column->addLayout(queryRow);

    auto* folderRow = new QHBoxLayout;
    folderField_ = new QLineEdit(body);
    folderField_->setObjectName(QStringLiteral("searchFolder"));
    folderField_->setPlaceholderText(catalog::translate(catalog::key::SearchPaneFolderPlaceholder));
    browse_ = new QPushButton(catalog::translate(catalog::key::SearchPaneBrowse), body);
    browse_->setEnabled(canPromptTheUser());
    recursive_ = new QCheckBox(catalog::translate(catalog::key::SearchPaneRecursive), body);
    recursive_->setChecked(true);
    folderRow->addWidget(folderField_, 1);
    folderRow->addWidget(browse_);
    folderRow->addWidget(recursive_);
    column->addLayout(folderRow);

    auto* filterGrid = new QGridLayout;
    sort_ = new QComboBox(body);
    sort_->addItem(catalog::translate(catalog::key::SearchPaneSortRelevance));
    sort_->addItem(catalog::translate(catalog::key::SearchPaneSortPage));
    sort_->addItem(catalog::translate(catalog::key::SearchPaneSortFile));
    author_ = new QLineEdit(body);
    author_->setPlaceholderText(catalog::translate(catalog::key::SearchPaneAuthor));
    subject_ = new QLineEdit(body);
    subject_->setPlaceholderText(catalog::translate(catalog::key::SearchPaneSubject));
    from_ = new QLineEdit(body);
    from_->setPlaceholderText(catalog::translate(catalog::key::SearchPaneFrom));
    to_ = new QLineEdit(body);
    to_->setPlaceholderText(catalog::translate(catalog::key::SearchPaneTo));
    refineButton_ = new QPushButton(catalog::translate(catalog::key::SearchPaneRefine), body);
    exportButton_ = new QPushButton(catalog::translate(catalog::key::SearchPaneExport), body);
    filterGrid->addWidget(sort_, 0, 0);
    filterGrid->addWidget(author_, 0, 1);
    filterGrid->addWidget(subject_, 0, 2);
    filterGrid->addWidget(from_, 1, 0);
    filterGrid->addWidget(to_, 1, 1);
    auto* actions = new QHBoxLayout;
    actions->addWidget(refineButton_);
    actions->addWidget(exportButton_);
    filterGrid->addLayout(actions, 1, 2);
    column->addLayout(filterGrid);

    list_ = new QTreeWidget(body);
    list_->setObjectName(QStringLiteral("searchResults"));
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setHeaderLabels({catalog::translate(catalog::key::SearchPaneColumnFile),
                            catalog::translate(catalog::key::SearchPaneColumnPage),
                            catalog::translate(catalog::key::SearchPaneColumnSnippet)});
    list_->setItemDelegateForColumn(SnippetColumn, new SnippetDelegate(list_));
    list_->header()->setStretchLastSection(true);
    column->addWidget(list_, 1);

    status_ = new QLabel(body);
    status_->setObjectName(QStringLiteral("searchStatus"));
    status_->setWordWrap(true);
    column->addWidget(status_);
    setWidget(body);

    // PANE-LOCAL CONTROLS, the find bar's scope combo's precedent: none of these is a command §8 or the
    // action table names. `Enter` from the query and from the list goes through `find.jumpToResult`.
    connect(searchButton_, &QPushButton::clicked, this, [this] { (void)search(); });
    connect(stopButton_, &QPushButton::clicked, this, [this] { stop(); });
    connect(refineButton_, &QPushButton::clicked, this, [this] { (void)refine(query_->text()); });
    connect(exportButton_, &QPushButton::clicked, this, [this] {
        if (!canPromptTheUser()) return;
        const QString path = QFileDialog::getSaveFileName(this, catalog::translate(catalog::key::SearchPaneExportTitle),
                                                          QString(), QStringLiteral("CSV (*.csv)"));
        if (!path.isEmpty()) (void)exportTo(path);
    });
    connect(browse_, &QPushButton::clicked, this, [this] {
        if (!canPromptTheUser()) return;
        const QString folder =
            QFileDialog::getExistingDirectory(this, catalog::translate(catalog::key::SearchPaneChooseFolder), folderField_->text());
        if (!folder.isEmpty()) folderField_->setText(QDir::toNativeSeparators(folder));
    });
    connect(sort_, &QComboBox::currentIndexChanged, this, [this](int index) {
        setSort(static_cast<SearchResults::Sort>(std::clamp(index, 0, 2)));
    });
    for (QLineEdit* field : {author_, subject_, from_, to_})
        connect(field, &QLineEdit::editingFinished, this, [this] { readFilter(); });
    connect(list_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        if (item != nullptr && item->data(FileColumn, kRowRole).isValid()) activate(item->data(FileColumn, kRowRole).toInt());
    });

    updateStatus();
}

SearchPane::~SearchPane()
{
    // THE FOLDER SEARCH GOES FIRST, its helper joined, while this object can still be posted to (the
    // constructor's note on the pump).
    if (folder_) folder_->setWakeHandler({});
    folder_.reset();
}

// =====================================================================================================
// Showing it
// =====================================================================================================

void SearchPane::showAndFocus()
{
    show();
    raise();
    query_->setFocus(Qt::ShortcutFocusReason);
    query_->selectAll();
}

bool SearchPane::hasFocusWithin() const
{
    const QWidget* focus = QApplication::focusWidget();
    return focus != nullptr && isAncestorOf(focus);
}

void SearchPane::closePane()
{
    hide();
    emit closed();
}

// =====================================================================================================
// Searching
// =====================================================================================================

void SearchPane::setFolder(const QString& folder)
{
    folderField_->setText(folder);
}

void SearchPane::setRecursive(bool recursive)
{
    recursive_->setChecked(recursive);
}

void SearchPane::setQuery(const QString& query)
{
    query_->setText(query);
}

bool SearchPane::isSearching() const
{
    return folder_->isRunning();
}

core::Status SearchPane::search()
{
    core::text::FolderSearch::Request request;
    request.folder = QDir::fromNativeSeparators(folderField_->text().trimmed());
    request.recursive = recursive_->isChecked();
    // THE FLAGS, THE CLASSES AND THE BOUND FROM THE SETTINGS -- the one reader (`SearchQuery::fromSettings`),
    // so this pane, the find bar and Preferences agree.
    request.query = core::text::SearchQuery::fromSettings(services_.settings, query_->text());
    results_.clear();
    rows_.clear();
    refineTo_.clear();
    refining_ = false;
    return run(std::move(request), false);
}

core::Status SearchPane::refine(const QString& query)
{
    // OVER THE CURRENT RESULTS' FILES, a second query, and only the pages BOTH found (D5-13).
    core::text::FolderSearch::Request request;
    const std::map<QString, std::set<int>> pages = results_.hitPages();
    for (const auto& [path, set] : pages) {
        (void)set;
        request.files << path;
    }
    if (request.files.isEmpty()) return core::Error{core::ErrorCode::NotFound, QLatin1String("error.search.nothingToRefine")};
    request.query = core::text::SearchQuery::fromSettings(services_.settings, query);
    refineTo_ = pages;
    incoming_.clear();
    refining_ = true;
    return run(std::move(request), true);
}

core::Status SearchPane::run(core::text::FolderSearch::Request request, bool refining)
{
    error_.clear();
    note_.clear();
    limitReached_ = false;
    filesDone_ = filesTotal_ = 0;
    core::text::FolderSearch::Handlers handlers;
    handlers.file = [this](core::text::FileResult file) { onFile(std::move(file)); };
    handlers.progress = [this](const core::text::FolderSearch::Progress& progress) {
        filesDone_ = progress.filesDone;
        filesTotal_ = progress.filesTotal;
        updateStatus();
    };
    handlers.finished = [this](const core::text::FolderSearch::Summary& summary) { onFinished(summary); };
    const QString pattern = request.query.pattern;
    const core::Status started = folder_->start(std::move(request), std::move(handlers));
    if (!started) {
        // REFUSED AND SAID (the find bar's rule): an empty or invalid pattern, a folder that is not there.
        error_ = catalog::translateError(QString(started.error().messageKey), started.error().context);
        refining_ = false;
        rebuildList();
        updateStatus();
        return started;
    }
    if (!refining) pattern_ = pattern;
    searchButton_->setEnabled(false);
    refineButton_->setEnabled(false);
    stopButton_->setEnabled(true);
    rebuildList();
    updateStatus();
    return {};
}

void SearchPane::stop()
{
    folder_->cancel();
    refining_ = false;
    searchButton_->setEnabled(true);
    refineButton_->setEnabled(true);
    stopButton_->setEnabled(false);
    updateStatus();
}

void SearchPane::onFile(core::text::FileResult file)
{
    if (refining_) {
        incoming_.add(std::move(file));
        return;
    }
    // STREAMED: each file's results join the list as it finishes (D5-13).
    results_.add(std::move(file));
    rebuildList();
}

void SearchPane::onFinished(const core::text::FolderSearch::Summary& summary)
{
    limitReached_ = summary.limitReached;
    if (refining_) {
        incoming_.keepOnly(refineTo_);
        results_ = std::move(incoming_);
        incoming_ = core::text::SearchResults{};
        refining_ = false;
    }
    searchButton_->setEnabled(true);
    refineButton_->setEnabled(true);
    stopButton_->setEnabled(false);
    rebuildList();
    updateStatus();
}

// =====================================================================================================
// The list
// =====================================================================================================

void SearchPane::setSort(SearchResults::Sort sort)
{
    sortOrder_ = sort;
    if (sort_->currentIndex() != static_cast<int>(sort)) sort_->setCurrentIndex(static_cast<int>(sort));
    rebuildList();
}

void SearchPane::setFilter(const SearchResults::Filter& filter)
{
    filter_ = filter;
    author_->setText(filter.author);
    subject_->setText(filter.subject);
    from_->setText(filter.from ? filter.from->toString(Qt::ISODate) : QString());
    to_->setText(filter.to ? filter.to->toString(Qt::ISODate) : QString());
    rebuildList();
}

void SearchPane::readFilter()
{
    SearchResults::Filter filter;
    filter.author = author_->text().trimmed();
    filter.subject = subject_->text().trimmed();
    filter.from = dateFrom(from_->text());
    filter.to = dateFrom(to_->text());
    // A DATE THAT DOES NOT PARSE IS SAID, not taken as no limit: a filter silently ignored reads as a list
    // that matched it.
    note_.clear();
    if ((!from_->text().trimmed().isEmpty() && !filter.from) || (!to_->text().trimmed().isEmpty() && !filter.to))
        note_ = catalog::translate(catalog::key::SearchPaneBadDate);
    filter_ = filter;
    rebuildList();
    updateStatus();
}

void SearchPane::rebuildList()
{
    list_->clear();
    rows_ = results_.rows(sortOrder_, filter_);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const SearchResults::Row& row = rows_[i];
        auto* item = new QTreeWidgetItem(list_);
        item->setText(FileColumn, QDir::toNativeSeparators(row.file->path));
        item->setToolTip(FileColumn, QDir::toNativeSeparators(row.file->path));
        const int page = row.hit->hit.page;
        item->setText(PageColumn, page >= 0 ? QString::number(page + 1) : QString());
        item->setData(FileColumn, kRowRole, static_cast<int>(i));
        item->setData(SnippetColumn, kBeforeRole, row.hit->snippet.before);
        item->setData(SnippetColumn, kMatchRole, row.hit->snippet.match);
        item->setData(SnippetColumn, kAfterRole, row.hit->snippet.after);
        item->setToolTip(SnippetColumn, row.hit->snippet.plain());
    }
    // THE FILES NOT SEARCHED, after the hits and with their reason (guide A-81): an encrypted file says so,
    // and that a folder search never asks for a password.
    for (const core::text::FileResult& file : results_.files()) {
        if (file.wasSearched()) continue;
        auto* item = new QTreeWidgetItem(list_);
        item->setText(FileColumn, QDir::toNativeSeparators(file.path));
        item->setText(SnippetColumn, file.isEncrypted()
                                         ? catalog::translate(catalog::key::SearchPaneSkippedEncrypted)
                                         : catalog::translateError(QString(file.refused->messageKey),
                                                                   file.refused->context));
        item->setForeground(SnippetColumn, palette().color(QPalette::Disabled, QPalette::Text));
    }
    updateStatus();
}

int SearchPane::currentRow() const
{
    const QTreeWidgetItem* item = list_->currentItem();
    if (item == nullptr || !item->data(FileColumn, kRowRole).isValid()) return -1;
    return item->data(FileColumn, kRowRole).toInt();
}

void SearchPane::selectNext()
{
    if (rows_.empty()) return;
    const int next = currentRow() < 0 ? 0 : (currentRow() + 1) % static_cast<int>(rows_.size());
    list_->setCurrentItem(list_->topLevelItem(next));
}

void SearchPane::selectPrevious()
{
    if (rows_.empty()) return;
    const int count = static_cast<int>(rows_.size());
    const int previous = currentRow() <= 0 ? count - 1 : currentRow() - 1;
    list_->setCurrentItem(list_->topLevelItem(previous));
}

void SearchPane::jumpToCurrent()
{
    // `Enter` (§8): from the query it searches; from the list it opens the current result.
    if (query_->hasFocus() || rows_.empty()) {
        (void)search();
        return;
    }
    activate(currentRow() < 0 ? 0 : currentRow());
}

void SearchPane::activate(int row)
{
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    const SearchResults::Row& chosen = rows_[static_cast<std::size_t>(row)];
    // EVERY HIT OF ITS FILE, in document order, and which one this is: the window shows them highlighted
    // and this one current, as the find bar does.
    std::vector<core::text::SearchHit> hits;
    int current = 0;
    for (const core::text::ResultHit& hit : chosen.file->hits) {
        if (&hit == chosen.hit) current = static_cast<int>(hits.size());
        hits.push_back(hit.hit);
    }
    emit resultActivated(chosen.file->path, pattern_, hits, current);
}

// =====================================================================================================
// What it says, and the export
// =====================================================================================================

QString SearchPane::statusText() const
{
    return status_->text();
}

void SearchPane::updateStatus()
{
    QStringList parts;
    if (!error_.isEmpty()) {
        parts << error_;
    } else if (folder_->isRunning()) {
        parts << catalog::translate(catalog::key::SearchPaneSearching).arg(filesDone_).arg(filesTotal_);
    }
    if (error_.isEmpty() && (!results_.files().empty() || !folder_->isRunning())) {
        if (!results_.files().empty() || !pattern_.isEmpty())
            parts << catalog::translate(catalog::key::SearchPaneCount)
                         .arg(static_cast<int>(rows_.size()))
                         .arg(results_.searchedCount());
        if (results_.refusedCount() > 0)
            parts << catalog::translate(catalog::key::SearchPaneNotSearched).arg(results_.refusedCount());
        if (limitReached_)
            parts << catalog::translate(catalog::key::SearchPaneLimitReached).arg(results_.hitCount());
    }
    if (!note_.isEmpty()) parts << note_;
    status_->setText(parts.join(QStringLiteral(" · ")));
}

core::Status SearchPane::exportTo(const QString& path)
{
    const QByteArray bytes = SearchResults::toCsv(
        rows_, {catalog::translate(catalog::key::SearchPaneColumnFile), catalog::translate(catalog::key::SearchPaneColumnPage),
                catalog::translate(catalog::key::SearchPaneColumnSnippet)});
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);   // layer-lint: allow-filesystem-write(the Advanced Search result list, exported as CSV to a file the person chose -- D5-13's one planned waiver)
    if (!opened || file.write(bytes) != bytes.size()) {
        const core::Error error{core::ErrorCode::IoFailure, QLatin1String("error.search.exportFailed"), path};
        note_ = catalog::translateError(QString(error.messageKey), error.context);
        updateStatus();
        return error;
    }
    file.close();
    note_ = catalog::translate(catalog::key::SearchPaneExported).arg(static_cast<int>(rows_.size())).arg(QDir::toNativeSeparators(path));
    updateStatus();
    return {};
}

} // namespace rawpage::ui
