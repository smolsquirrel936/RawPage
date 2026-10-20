// ui/panes/CommentsPane -- implementation. Stage 6 T9. See the header for why the list is the store's query
// and why the model is the pane's own.

#include "ui/panes/CommentsPane.h"

#include "core/cmd/commands/AnnotationCommands.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/AnnotationTextEditor.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QShowEvent>
#include <QTimeZone>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace rawpage::ui {

using core::Annotation;
using core::AnnotationId;
using core::AnnotationStore;
using core::AnnotationSubtype;

namespace {

constexpr int kColumnComment = 0;
constexpr int kColumnAuthor = 1;
constexpr int kColumnPage = 2;
constexpr int kColumnDate = 3;
constexpr int kColumnStatus = 4;
constexpr int kColumnCount = 5;

// The instant an item is dated by -- the store's rule: `/M`, else `/CreationDate`.
std::optional<std::int64_t> dateOf(const Annotation& a)
{
    if (a.modified.utcSeconds) return a.modified.utcSeconds;
    return a.created.utcSeconds;
}

QString typeWord(AnnotationSubtype subtype)
{
    switch (subtype) {
    case AnnotationSubtype::Text:
    case AnnotationSubtype::FreeText:
    case AnnotationSubtype::Line:
    case AnnotationSubtype::Square:
    case AnnotationSubtype::Circle:
    case AnnotationSubtype::Polygon:
    case AnnotationSubtype::PolyLine:
    case AnnotationSubtype::Highlight:
    case AnnotationSubtype::Underline:
    case AnnotationSubtype::Squiggly:
    case AnnotationSubtype::StrikeOut:
    case AnnotationSubtype::Stamp:
    case AnnotationSubtype::Caret:
    case AnnotationSubtype::Ink:
    case AnnotationSubtype::FileAttachment:
    case AnnotationSubtype::Sound:
    case AnnotationSubtype::Redact:
        return catalog::translate(QStringLiteral("comments.type.") + QString(core::annotationSubtypeName(subtype)));
    default:
        return catalog::translate(catalog::key::CommentsTypeOther);
    }
}

QString statusWord(const QString& state)
{
    static const QStringList kKnown{QStringLiteral("Accepted"), QStringLiteral("Rejected"), QStringLiteral("Cancelled"),
                                    QStringLiteral("Completed"), QStringLiteral("None")};
    // A state no model names is a producer's own: shown as written.
    if (!kKnown.contains(state)) return state;
    return catalog::translate(QStringLiteral("comments.status.") + state);
}

QString firstLine(const QString& text)
{
    const qsizetype end = text.indexOf(QRegularExpression(QStringLiteral("[\\r\\n]")));
    return (end < 0 ? text : text.left(end)).trimmed();
}

} // namespace

// ==========================================================================================
// CommentsModel -- the store's grouped query as rows: groups, their comments, each comment's replies.
// ==========================================================================================

class CommentsModel : public QAbstractItemModel {
public:
    struct Node {
        bool group = false;
        int parent = -1;            // node index, -1 for the root
        int row = 0;
        std::vector<int> children;
        std::size_t index = 0;      // an item's annotation in the store, when the model was built
        AnnotationId id;            // ... and WHICH annotation: a stale model is resolved by this, never the index
        QString key;                // a group's
    };

    explicit CommentsModel(CommentsPane& pane) : QAbstractItemModel(&pane), pane_(pane) {}

    void build(const AnnotationStore* store, const std::vector<AnnotationStore::Group>& groups,
               AnnotationStore::GroupKey grouping, const QString& identity)
    {
        beginResetModel();
        nodes_.clear();
        roots_.clear();
        store_ = store;
        grouping_ = grouping;
        identity_ = identity;
        byAnnotation_.clear();
        if (store != nullptr) {
            // The item and, beneath it, its replies at any depth -- walked iteratively, depth first, so each reply's
            // own replies come before its next sibling. `kTop` marks the item itself, listed in `siblings`.
            constexpr int kTop = -2;
            const auto addItem = [&](std::size_t index, int parent, std::vector<int>& siblings) {
                std::vector<std::pair<std::size_t, int>> pending{{index, kTop}};
                while (!pending.empty()) {
                    const auto [at, owner] = pending.back();
                    pending.pop_back();
                    const int me = static_cast<int>(nodes_.size());
                    Node node;
                    node.parent = owner == kTop ? parent : owner;
                    node.index = at;
                    node.id = store->at(at).id;
                    nodes_.push_back(std::move(node));   // before `into` is taken: this may move every node
                    std::vector<int>& into = owner == kTop ? siblings : nodes_[static_cast<std::size_t>(owner)].children;
                    nodes_[static_cast<std::size_t>(me)].row = static_cast<int>(into.size());
                    into.push_back(me);
                    byAnnotation_.emplace(at, me);
                    const std::vector<std::size_t> replies = store->replies(at);
                    for (auto r = replies.rbegin(); r != replies.rend(); ++r) pending.emplace_back(*r, me);
                }
            };
            if (grouping == AnnotationStore::GroupKey::None) {
                for (const AnnotationStore::Group& g : groups) {
                    for (const std::size_t index : g.items) addItem(index, -1, roots_);
                }
            } else {
                for (const AnnotationStore::Group& g : groups) {
                    Node node;
                    node.group = true;
                    node.row = static_cast<int>(roots_.size());
                    node.key = g.key;
                    const int me = static_cast<int>(nodes_.size());
                    nodes_.push_back(std::move(node));
                    roots_.push_back(me);
                    std::vector<int> children;
                    for (const std::size_t index : g.items) addItem(index, me, children);
                    nodes_[static_cast<std::size_t>(me)].children = std::move(children);
                }
            }
        }
        endResetModel();
    }

    const Node* node(const QModelIndex& index) const
    {
        if (!index.isValid()) return nullptr;
        const auto id = static_cast<std::size_t>(index.internalId());
        return id >= 1 && id <= nodes_.size() ? &nodes_[id - 1] : nullptr;
    }
    QModelIndex indexOfNode(int node, int column = 0) const
    {
        if (node < 0 || static_cast<std::size_t>(node) >= nodes_.size()) return {};
        return createIndex(nodes_[static_cast<std::size_t>(node)].row, column, static_cast<quintptr>(node + 1));
    }
    QModelIndex indexOfAnnotation(std::size_t annotation) const
    {
        const auto found = byAnnotation_.find(annotation);
        return found == byAnnotation_.end() ? QModelIndex() : indexOfNode(found->second);
    }
    const std::vector<int>& roots() const noexcept { return roots_; }
    const std::vector<Node>& nodes() const noexcept { return nodes_; }

    QModelIndex index(int row, int column, const QModelIndex& parent) const override
    {
        if (row < 0 || column < 0 || column >= kColumnCount) return {};
        const std::vector<int>* list = &roots_;
        if (parent.isValid()) {
            const Node* p = node(parent);
            if (p == nullptr) return {};
            list = &p->children;
        }
        if (static_cast<std::size_t>(row) >= list->size()) return {};
        return createIndex(row, column, static_cast<quintptr>((*list)[static_cast<std::size_t>(row)] + 1));
    }
    QModelIndex parent(const QModelIndex& child) const override
    {
        const Node* n = node(child);
        if (n == nullptr || n->parent < 0) return {};
        return indexOfNode(n->parent);
    }
    int rowCount(const QModelIndex& parent) const override
    {
        if (!parent.isValid()) return static_cast<int>(roots_.size());
        if (parent.column() != 0) return 0;
        const Node* n = node(parent);
        return n ? static_cast<int>(n->children.size()) : 0;
    }
    int columnCount(const QModelIndex&) const override { return kColumnCount; }

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        const Node* n = node(index);
        if (n == nullptr) return Qt::NoItemFlags;
        Qt::ItemFlags out = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (!n->group && index.column() == kColumnComment && pane_.canChange()) out |= Qt::ItemIsUserCheckable;
        return out;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        switch (section) {
        case kColumnComment: return catalog::translate(catalog::key::CommentsColumnComment);
        case kColumnAuthor: return catalog::translate(catalog::key::CommentsColumnAuthor);
        case kColumnPage: return catalog::translate(catalog::key::CommentsColumnPage);
        case kColumnDate: return catalog::translate(catalog::key::CommentsColumnDate);
        case kColumnStatus: return catalog::translate(catalog::key::CommentsColumnStatus);
        default: return {};
        }
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        const Node* n = node(index);
        if (n == nullptr || store_ == nullptr) return {};
        if (n->group) {
            if (role != Qt::DisplayRole || index.column() != kColumnComment) return {};
            return catalog::translate(catalog::key::CommentsGroupCount)
                .arg(groupLabel(n->key))
                .arg(static_cast<qulonglong>(n->children.size()));
        }
        // A model built before the store changed -- the pane is rebuilt only while shown -- answers nothing for a
        // row whose annotation is not where it was.
        if (n->index >= store_->size() || !(store_->at(n->index).id == n->id)) return {};
        const Annotation& a = store_->at(n->index);
        switch (role) {
        case Qt::DisplayRole: return display(*n, a, index.column());
        case Qt::ToolTipRole:
            if (index.column() == kColumnStatus) return history(n->index);
            if (index.column() == kColumnComment) return a.contents;
            return {};
        case Qt::CheckStateRole:
            if (index.column() != kColumnComment) return {};
            return static_cast<int>(store_->isMarkedBy(n->index, identity_) ? Qt::Checked : Qt::Unchecked);
        default: return {};
        }
    }

    bool setData(const QModelIndex& index, const QVariant&, int role) override
    {
        const Node* n = node(index);
        if (n == nullptr || n->group || role != Qt::CheckStateRole) return false;
        // NOT HERE: the command resets this model, and the view is still inside its own click. Queued, so the view
        // has finished with the index first.
        const AnnotationId id = n->id;
        QMetaObject::invokeMethod(&pane_, [pane = &pane_, id] {
            if (pane->store_ == nullptr) return;
            if (const std::optional<std::size_t> at = pane->store_->find(id)) (void)pane->toggleCheckmarkOf(*at);
        }, Qt::QueuedConnection);
        return false;
    }

    QString displayOf(std::size_t annotation, int column) const
    {
        const auto found = byAnnotation_.find(annotation);
        if (found == byAnnotation_.end() || store_ == nullptr || annotation >= store_->size()) return {};
        return display(nodes_[static_cast<std::size_t>(found->second)], store_->at(annotation), column);
    }

private:
    QString groupLabel(const QString& key) const
    {
        switch (grouping_) {
        case AnnotationStore::GroupKey::Page: return catalog::translate(catalog::key::CommentsGroupPage).arg(key.toInt() + 1);
        case AnnotationStore::GroupKey::Author:
            return key.isEmpty() ? catalog::translate(catalog::key::CommentsGroupNoAuthor) : key;
        case AnnotationStore::GroupKey::Date:
            return key.isEmpty() ? catalog::translate(catalog::key::CommentsGroupUndated) : key;
        case AnnotationStore::GroupKey::Type:
            return typeWord(core::annotationSubtypeFromName(key.toStdString()));
        case AnnotationStore::GroupKey::ReviewStatus: return statusWord(key);
        case AnnotationStore::GroupKey::Checkmark:
            return catalog::translate(key == QLatin1String("Marked") ? catalog::key::CommentsCheckMarked
                                                                      : catalog::key::CommentsCheckUnmarked);
        case AnnotationStore::GroupKey::None: return key;
        }
        return key;
    }

    QString display(const Node& n, const Annotation& a, int column) const
    {
        switch (column) {
        case kColumnComment: {
            QString text = catalog::translate(catalog::key::CommentsItem).arg(typeWord(a.subtype), firstLine(a.contents));
            const std::vector<int>& numbers = pane_.numbers();
            if (n.index < numbers.size() && numbers[n.index] > 0)
                text = catalog::translate(catalog::key::CommentsNumbered).arg(numbers[n.index]).arg(text);
            // Listed, and said to be hidden (D6-27): not drawn on the page, so the list is where it is found.
            if (a.hasFlag(core::AnnotationFlag::Hidden) || a.hasFlag(core::AnnotationFlag::NoView))
                text += QLatin1Char(' ') + catalog::translate(catalog::key::CommentsHidden);
            return text;
        }
        case kColumnAuthor: return a.author;
        case kColumnPage: return QString::number(a.page + 1);
        case kColumnDate: {
            const std::optional<std::int64_t> at = dateOf(a);
            if (!at) return {};
            return QDateTime::fromSecsSinceEpoch(*at + pane_.utcOffset_, QTimeZone::UTC)
                .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        }
        case kColumnStatus: return statusWord(store_->reviewStatus(n.index));
        default: return {};
        }
    }

    QString history(std::size_t index) const
    {
        QStringList lines;
        for (const core::AnnotationStateChange& change : store_->reviewHistory(index)) {
            const QString when = change.date.utcSeconds
                                     ? QDateTime::fromSecsSinceEpoch(*change.date.utcSeconds + pane_.utcOffset_, QTimeZone::UTC)
                                           .toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                     : QString();
            lines << catalog::translate(catalog::key::CommentsHistoryEntry).arg(statusWord(change.state), change.author, when);
        }
        return lines.join(QLatin1Char('\n'));
    }

    CommentsPane& pane_;
    const AnnotationStore* store_ = nullptr;
    AnnotationStore::GroupKey grouping_ = AnnotationStore::GroupKey::None;
    QString identity_;
    std::vector<Node> nodes_;
    std::vector<int> roots_;
    std::map<std::size_t, int> byAnnotation_;
};

// ==========================================================================================
// CommentsPane
// ==========================================================================================

CommentsPane::CommentsPane(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("commentsPane"));
    ShortcutBinder::declareContext(this, core::KeyContext::Comments);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(6, 6, 6, 6);
    column->setSpacing(6);

    // THE SEARCH, THE FILTER AND THE PANE'S OWN COMMANDS.
    auto* top = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("commentsSearch"));
    search_->setPlaceholderText(catalog::translate(catalog::key::CommentsSearchPlaceholder));
    search_->setClearButtonEnabled(true);
    ActionFactory::adoptWidgetActions(search_);   // the clear button is a QAction Qt made (T11, I-22)
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) { setSearchText(text); });
    top->addWidget(search_, 1);

    filterMenu_ = new QMenu(this);
    connect(filterMenu_, &QMenu::aboutToShow, this, [this] { populateFilterMenu(); });
    filterButton_ = new QToolButton(this);
    filterButton_->setText(catalog::translate(catalog::key::CommentsFilterButton));
    filterButton_->setMenu(filterMenu_);
    filterButton_->setPopupMode(QToolButton::InstantPopup);
    top->addWidget(filterButton_);

    // The pane's menu asks the WINDOW for registered commands -- the ribbon's, the tool finder's -- rather than
    // calling a method of its own: one behaviour, reachable from every surface (the action layer's rule).
    moreMenu_ = new QMenu(this);
    QAction* numberingAction = ActionFactory::addNonCommandMenuItem(moreMenu_, catalog::translateAction(core::ActionId::CommentNumbering));
    numberingAction->setCheckable(true);
    connect(numberingAction, &QAction::triggered, this, [this] { emit actionRequested(core::ActionId::CommentNumbering); });
    connect(moreMenu_, &QMenu::aboutToShow, this, [this, numberingAction] { numberingAction->setChecked(numbering_); });
    connect(ActionFactory::addNonCommandMenuItem(moreMenu_, catalog::translateAction(core::ActionId::CommentDeleteMatching)), &QAction::triggered, this,
            [this] { emit actionRequested(core::ActionId::CommentDeleteMatching); });
    connect(ActionFactory::addNonCommandMenuItem(moreMenu_, catalog::translateAction(core::ActionId::CommentDeleteAll)), &QAction::triggered, this,
            [this] { emit actionRequested(core::ActionId::CommentDeleteAll); });
    moreButton_ = new QToolButton(this);
    moreButton_->setText(catalog::translate(catalog::key::CommentsMoreButton));
    moreButton_->setMenu(moreMenu_);
    moreButton_->setPopupMode(QToolButton::InstantPopup);
    top->addWidget(moreButton_);
    column->addLayout(top);

    // GROUP AND SORT.
    auto* second = new QHBoxLayout;
    second->addWidget(new QLabel(catalog::translate(catalog::key::CommentsGroupByLabel), this));
    groupBox_ = new QComboBox(this);
    groupBox_->setObjectName(QStringLiteral("commentsGroup"));
    const std::pair<AnnotationStore::GroupKey, QLatin1String> groups[] = {
        {AnnotationStore::GroupKey::None, catalog::key::CommentsGroupByNone},
        {AnnotationStore::GroupKey::Page, catalog::key::CommentsGroupByPage},
        {AnnotationStore::GroupKey::Author, catalog::key::CommentsGroupByAuthor},
        {AnnotationStore::GroupKey::Date, catalog::key::CommentsGroupByDate},
        {AnnotationStore::GroupKey::Type, catalog::key::CommentsGroupByType},
        {AnnotationStore::GroupKey::ReviewStatus, catalog::key::CommentsGroupByStatus},
        {AnnotationStore::GroupKey::Checkmark, catalog::key::CommentsGroupByCheckmark}};
    for (const auto& [key, label] : groups) groupBox_->addItem(catalog::translate(label), static_cast<int>(key));
    connect(groupBox_, &QComboBox::activated, this,
            [this](int at) { setGroupKey(static_cast<AnnotationStore::GroupKey>(groupBox_->itemData(at).toInt())); });
    second->addWidget(groupBox_, 1);
    second->addWidget(new QLabel(catalog::translate(catalog::key::CommentsSortByLabel), this));
    sortBox_ = new QComboBox(this);
    sortBox_->setObjectName(QStringLiteral("commentsSort"));
    const std::pair<AnnotationStore::SortKey, QLatin1String> sorts[] = {
        {AnnotationStore::SortKey::Page, catalog::key::CommentsSortByPage},
        {AnnotationStore::SortKey::Date, catalog::key::CommentsSortByDate},
        {AnnotationStore::SortKey::Author, catalog::key::CommentsSortByAuthor},
        {AnnotationStore::SortKey::Subtype, catalog::key::CommentsSortByType}};
    for (const auto& [key, label] : sorts) sortBox_->addItem(catalog::translate(label), static_cast<int>(key));
    connect(sortBox_, &QComboBox::activated, this,
            [this](int at) { setSortKey(static_cast<AnnotationStore::SortKey>(sortBox_->itemData(at).toInt())); });
    second->addWidget(sortBox_, 1);
    column->addLayout(second);

    // THE DATE RANGE, shown when the filter asks for one.
    dateRow_ = new QWidget(this);
    auto* dates = new QHBoxLayout(dateRow_);
    dates->setContentsMargins(0, 0, 0, 0);
    dates->addWidget(new QLabel(catalog::translate(catalog::key::CommentsFilterFrom), dateRow_));
    fromEdit_ = new QDateEdit(dateRow_);
    fromEdit_->setCalendarPopup(true);
    fromEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dates->addWidget(fromEdit_, 1);
    dates->addWidget(new QLabel(catalog::translate(catalog::key::CommentsFilterTo), dateRow_));
    toEdit_ = new QDateEdit(dateRow_);
    toEdit_->setCalendarPopup(true);
    toEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dates->addWidget(toEdit_, 1);
    auto* anyDate = new QToolButton(dateRow_);
    anyDate->setText(catalog::translate(catalog::key::CommentsFilterClearDates));
    connect(anyDate, &QToolButton::clicked, this, [this] { setDateRange(std::nullopt, std::nullopt); });
    dates->addWidget(anyDate);
    // A day is the host's calendar day: from its first second to its last, at the pane's offset.
    const auto applyDates = [this] {
        if (dateRow_->isHidden()) return;
        const auto start = [this](const QDate& day) {
            return QDateTime(day, QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch() - utcOffset_;
        };
        setDateRange(start(fromEdit_->date()), start(toEdit_->date().addDays(1)) - 1);
    };
    connect(fromEdit_, &QDateEdit::dateChanged, this, applyDates);
    connect(toEdit_, &QDateEdit::dateChanged, this, applyDates);
    dateRow_->hide();
    column->addWidget(dateRow_);

    // THE LIST.
    model_ = new CommentsModel(*this);
    view_ = new QTreeView(this);
    view_->setObjectName(QStringLiteral("commentsList"));
    view_->setModel(model_);
    view_->setUniformRowHeights(true);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setAllColumnsShowFocus(true);
    view_->header()->setStretchLastSection(false);
    view_->header()->setSectionResizeMode(kColumnComment, QHeaderView::Stretch);
    for (int c = 1; c < kColumnCount; ++c) view_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    // A CURRENT ROW FROM NOTHING CURRENT IS NOT A GESTURE: Qt makes the first row current when the list takes the
    // focus, and opening the pane must not send the page to its first comment. A CLICK is a gesture whatever was
    // current before, and so is every move from one row to another.
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex& previous) {
                if (previous.isValid()) onCurrentChanged(current);
            });
    connect(view_, &QTreeView::clicked, this, [this](const QModelIndex& index) { onCurrentChanged(index); });
    column->addWidget(view_, 1);

    // THE EDITOR -- a reply's, or an edit in place -- below the list, made when asked for.
    editorTitle_ = new QLabel(this);
    editorTitle_->setWordWrap(true);
    editorTitle_->hide();
    column->addWidget(editorTitle_);
    editorSlot_ = new QVBoxLayout;
    editorSlot_->setContentsMargins(0, 0, 0, 0);
    column->addLayout(editorSlot_);

    count_ = new QLabel(this);
    count_->setObjectName(QStringLiteral("commentsCount"));
    column->addWidget(count_);

    // THE FIVE REVIEW STATES (`Ctrl+Shift+K`), ISO 32000-1 Table 172's.
    statusMenu_ = new QMenu(this);
    statusMenu_->setObjectName(QStringLiteral("commentsStatusMenu"));
    for (const char* state : {"Accepted", "Rejected", "Cancelled", "Completed", "None"}) {
        const QString name = QString::fromLatin1(state);
        QAction* action = ActionFactory::addNonCommandMenuItem(statusMenu_, statusWord(name));
        action->setData(name);
        connect(action, &QAction::triggered, this, [this, name] {
            if (const core::Status done = setStatus(name); !done) emit refused(done.error());
        });
    }

    rebuild();
}

CommentsPane::~CommentsPane()
{
    // The editor's signals reach this pane; it goes first (convention 10).
    if (editor_) disconnect(editor_, nullptr, this, nullptr);
}

void CommentsPane::setDocument(const core::AnnotationStore* store, Runner runner, std::vector<core::PdfRect> pageBoxes,
                               std::vector<int> rotations)
{
    if (editor_) editor_->abandon();
    store_ = store;
    runner_ = std::move(runner);
    pageBoxes_ = std::move(pageBoxes);
    rotations_ = std::move(rotations);
    stale_ = true;
    rebuild();
}

void CommentsPane::setPages(std::vector<core::PdfRect> pageBoxes, std::vector<int> rotations)
{
    pageBoxes_ = std::move(pageBoxes);
    rotations_ = std::move(rotations);
}

void CommentsPane::setIdentity(Identity identity)
{
    identity_ = std::move(identity);
}

QString CommentsPane::identity() const
{
    return identity_ ? identity_() : QString();
}

void CommentsPane::setClock(Clock clock)
{
    clock_ = std::move(clock);
}

void CommentsPane::setUtcOffset(int seconds)
{
    if (utcOffset_ == seconds) return;
    utcOffset_ = seconds;
    refresh();
}

std::int64_t CommentsPane::now() const
{
    if (clock_) return clock_();
    return QDateTime::currentSecsSinceEpoch();
}

void CommentsPane::refresh()
{
    stale_ = true;
    // The numbers are the page's too, so they are kept current whether or not the list is shown.
    if (numbering_ || isVisible()) rebuild();
}

void CommentsPane::ensureFresh()
{
    if (stale_) rebuild();
}

void CommentsPane::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ensureFresh();
}

AnnotationStore::Filter CommentsPane::filter() const
{
    AnnotationStore::Filter f;
    f.authors = authors_;
    f.subtypes = types_;
    f.text = text_;
    f.statuses = statuses_;
    f.checked = checked_;
    f.identity = identity();
    f.from = from_;
    f.to = to_;
    return f;
}

void CommentsPane::rebuild()
{
    QElapsedTimer timer;
    timer.start();
    const std::optional<AnnotationId> keep = currentComment();
    const bool readable = store_ != nullptr && store_->status();

    numbers_.clear();
    if (numbering_ && readable) numbers_ = store_->sequenceNumbers(rotations_);

    std::vector<AnnotationStore::Group> groups;
    if (readable) groups = store_->grouped(filter(), group_, sort_, utcOffset_);
    silent_ = true;
    model_->build(readable ? store_ : nullptr, groups, group_, identity());
    // Groups and threads open: a collapsed group is a gesture, not a default.
    for (std::size_t n = 0; n < model_->nodes().size(); ++n) {
        if (!model_->nodes()[n].children.empty()) view_->setExpanded(model_->indexOfNode(static_cast<int>(n)), true);
    }
    if (keep) {
        const QModelIndex at = indexOf(*keep);
        if (at.isValid()) view_->setCurrentIndex(at);
    }
    silent_ = false;
    stale_ = false;
    updateCount();
    lastRebuildMs_ = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    if (numbering_) emit numbersChanged();
}

void CommentsPane::updateCount()
{
    if (store_ == nullptr) {
        count_->setText(catalog::translate(catalog::key::PanesCommentsEmpty));
        return;
    }
    if (!store_->status()) {
        count_->setText(catalog::translate(catalog::key::PanesCommentsUnreadable)
                            .arg(catalog::translateError(QString(store_->status().error().messageKey),
                                                         store_->status().error().context)));
        return;
    }
    count_->setText(catalog::translate(catalog::key::CommentsCount)
                        .arg(listedCount())
                        .arg(static_cast<qulonglong>(store_->topLevel().size())));
}

// ---- View state ------------------------------------------------------------------------------

void CommentsPane::setGroupKey(AnnotationStore::GroupKey key)
{
    group_ = key;
    if (const int at = groupBox_->findData(static_cast<int>(key)); at >= 0) groupBox_->setCurrentIndex(at);
    rebuild();
}

void CommentsPane::setSortKey(AnnotationStore::SortKey key)
{
    sort_ = key;
    if (const int at = sortBox_->findData(static_cast<int>(key)); at >= 0) sortBox_->setCurrentIndex(at);
    rebuild();
}

void CommentsPane::setSearchText(const QString& text)
{
    if (text_ == text) return;
    text_ = text;
    if (search_->text() != text) search_->setText(text);
    rebuild();
}

void CommentsPane::setAuthorFilter(std::vector<QString> authors)
{
    authors_ = std::move(authors);
    rebuild();
}

void CommentsPane::setTypeFilter(std::vector<AnnotationSubtype> types)
{
    types_ = std::move(types);
    rebuild();
}

void CommentsPane::setStatusFilter(std::vector<QString> statuses)
{
    statuses_ = std::move(statuses);
    rebuild();
}

void CommentsPane::setCheckmarkFilter(std::optional<bool> checked)
{
    checked_ = checked;
    rebuild();
}

void CommentsPane::setDateRange(std::optional<std::int64_t> from, std::optional<std::int64_t> to)
{
    from_ = from;
    to_ = to;
    if (!from && !to) dateRow_->hide();
    rebuild();
}

void CommentsPane::clearFilter()
{
    authors_.clear();
    types_.clear();
    statuses_.clear();
    checked_.reset();
    from_.reset();
    to_.reset();
    dateRow_->hide();
    if (!search_->text().isEmpty()) search_->clear();   // which rebuilds
    text_.clear();
    rebuild();
}

void CommentsPane::setNumbering(bool on)
{
    if (numbering_ == on) return;
    numbering_ = on;
    rebuild();
    if (!on) emit numbersChanged();
}

void CommentsPane::populateFilterMenu()
{
    filterMenu_->clear();
    const auto toggle = [](auto& list, const auto& value) {
        const auto at = std::find(list.begin(), list.end(), value);
        if (at == list.end()) list.push_back(value);
        else list.erase(at);
    };

    // AUTHORS: each one checkable; none checked is every author.
    QMenu* authors = filterMenu_->addMenu(catalog::translate(catalog::key::CommentsFilterAuthors));
    if (store_ != nullptr && store_->status()) {
        for (const QString& author : store_->authors()) {
            QAction* action = ActionFactory::addNonCommandMenuItem(authors, author);
            action->setCheckable(true);
            action->setChecked(std::find(authors_.begin(), authors_.end(), author) != authors_.end());
            connect(action, &QAction::triggered, this, [this, author, toggle] {
                std::vector<QString> next = authors_;
                toggle(next, author);
                setAuthorFilter(std::move(next));
            });
        }
    }
    // TYPES: the kinds the document's comments are.
    QMenu* types = filterMenu_->addMenu(catalog::translate(catalog::key::CommentsFilterTypes));
    if (store_ != nullptr && store_->status()) {
        std::set<int> present;
        for (const std::size_t index : store_->topLevel()) present.insert(static_cast<int>(store_->at(index).subtype));
        for (const int value : present) {
            const auto subtype = static_cast<AnnotationSubtype>(value);
            QAction* action = ActionFactory::addNonCommandMenuItem(types, typeWord(subtype));
            action->setCheckable(true);
            action->setChecked(std::find(types_.begin(), types_.end(), subtype) != types_.end());
            connect(action, &QAction::triggered, this, [this, subtype, toggle] {
                std::vector<AnnotationSubtype> next = types_;
                toggle(next, subtype);
                setTypeFilter(std::move(next));
            });
        }
    }
    // STATUSES.
    QMenu* statuses = filterMenu_->addMenu(catalog::translate(catalog::key::CommentsFilterStatuses));
    for (const char* state : {"Accepted", "Rejected", "Cancelled", "Completed", "None"}) {
        const QString name = QString::fromLatin1(state);
        QAction* action = ActionFactory::addNonCommandMenuItem(statuses, statusWord(name));
        action->setCheckable(true);
        action->setChecked(std::find(statuses_.begin(), statuses_.end(), name) != statuses_.end());
        connect(action, &QAction::triggered, this, [this, name, toggle] {
            std::vector<QString> next = statuses_;
            toggle(next, name);
            setStatusFilter(std::move(next));
        });
    }
    // THE CHECKMARK -- the current identity's.
    QMenu* check = filterMenu_->addMenu(catalog::translate(catalog::key::CommentsFilterCheckmark));
    const std::pair<std::optional<bool>, QLatin1String> checks[] = {{std::nullopt, catalog::key::CommentsFilterAny},
                                                                   {true, catalog::key::CommentsFilterChecked},
                                                                   {false, catalog::key::CommentsFilterUnchecked}};
    for (const auto& [value, label] : checks) {
        QAction* action = ActionFactory::addNonCommandMenuItem(check, catalog::translate(label));
        action->setCheckable(true);
        action->setChecked(checked_ == value);
        connect(action, &QAction::triggered, this, [this, value] { setCheckmarkFilter(value); });
    }
    // THE DATE RANGE: a row of two days below the group and sort.
    connect(ActionFactory::addNonCommandMenuItem(filterMenu_, catalog::translate(catalog::key::CommentsFilterDateRange)), &QAction::triggered, this, [this] {
        const QDate today = QDateTime::currentDateTimeUtc().addSecs(utcOffset_).date();
        const QSignalBlocker blockFrom(fromEdit_);
        const QSignalBlocker blockTo(toEdit_);
        if (!from_) fromEdit_->setDate(today.addDays(-30));
        if (!to_) toEdit_->setDate(today);
        dateRow_->show();
        const auto start = [this](const QDate& day) {
            return QDateTime(day, QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch() - utcOffset_;
        };
        setDateRange(start(fromEdit_->date()), start(toEdit_->date().addDays(1)) - 1);
    });
    connect(ActionFactory::addNonCommandMenuItem(filterMenu_, catalog::translate(catalog::key::CommentsFilterShowAll)), &QAction::triggered, this,
            [this] { clearFilter(); });
}

void CommentsPane::showFilterMenu()
{
    populateFilterMenu();
    filterMenu_->popup(filterButton_->mapToGlobal(QPoint(0, filterButton_->height())));
}

void CommentsPane::showSortMenu()
{
    sortBox_->setFocus(Qt::OtherFocusReason);
    sortBox_->showPopup();
}

// ---- The current comment -----------------------------------------------------------------------

std::optional<std::size_t> CommentsPane::currentIndex() const
{
    const CommentsModel::Node* n = model_->node(view_->currentIndex());
    if (n == nullptr || n->group || store_ == nullptr) return std::nullopt;
    return store_->find(n->id);
}

std::optional<std::size_t> CommentsPane::currentTopLevel() const
{
    std::optional<std::size_t> at = currentIndex();
    if (!at || store_ == nullptr) return at;
    while (const std::optional<std::size_t> parent = store_->parentOf(*at)) at = parent;
    return at;
}

std::optional<AnnotationId> CommentsPane::currentComment() const
{
    if (const std::optional<std::size_t> at = currentIndex()) return store_->at(*at).id;
    return std::nullopt;
}

QModelIndex CommentsPane::indexOf(const AnnotationId& id) const
{
    if (store_ == nullptr || !store_->status()) return {};
    const std::optional<std::size_t> at = store_->find(id);
    return at ? model_->indexOfAnnotation(*at) : QModelIndex();
}

bool CommentsPane::selectComment(const AnnotationId& id)
{
    ensureFresh();
    const QModelIndex at = indexOf(id);
    if (!at.isValid()) return false;
    makeCurrent(at, false);
    return true;
}

void CommentsPane::makeCurrent(const QModelIndex& index, bool activate)
{
    // Made current quietly, and then -- for a gesture -- activated ONCE, whatever was current before.
    silent_ = true;
    view_->setCurrentIndex(index);
    view_->scrollTo(index);
    silent_ = false;
    if (activate) onCurrentChanged(view_->currentIndex());
}

void CommentsPane::onCurrentChanged(const QModelIndex& current)
{
    if (silent_) return;
    const CommentsModel::Node* n = model_->node(current);
    if (n == nullptr || n->group || store_ == nullptr) return;
    const std::optional<std::size_t> at = store_->find(n->id);
    if (!at) return;
    const Annotation& a = store_->at(*at);
    emit activated(a.id, a.page, a.rect);
}

std::vector<AnnotationId> CommentsPane::listedComments() const
{
    std::vector<AnnotationId> out;
    if (store_ == nullptr) return out;
    const auto& nodes = model_->nodes();
    const auto take = [&](int node) {
        const CommentsModel::Node& n = nodes[static_cast<std::size_t>(node)];
        if (!n.group) out.push_back(n.id);
    };
    for (const int root : model_->roots()) {
        const CommentsModel::Node& r = nodes[static_cast<std::size_t>(root)];
        if (r.group) {
            for (const int child : r.children) take(child);
        } else {
            take(root);
        }
    }
    return out;
}

int CommentsPane::listedCount() const
{
    return static_cast<int>(listedComments().size());
}

int CommentsPane::groupCount() const
{
    int groups = 0;
    for (const int root : model_->roots()) groups += model_->nodes()[static_cast<std::size_t>(root)].group ? 1 : 0;
    return groups;
}

int CommentsPane::visibleRowCount() const
{
    int rows = 0;
    for (QModelIndex at = model_->index(0, 0, QModelIndex()); at.isValid(); at = view_->indexBelow(at)) ++rows;
    return rows;
}

QString CommentsPane::textOf(const AnnotationId& id, int column) const
{
    if (store_ == nullptr) return {};
    const std::optional<std::size_t> at = store_->find(id);
    return at ? model_->displayOf(*at, column) : QString();
}

// ---- §8's rows ---------------------------------------------------------------------------------

core::Status CommentsPane::run(std::unique_ptr<core::Command> command)
{
    if (!runner_) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.edit.notAssessed"), QString()};
    return runner_(std::move(command));
}

void CommentsPane::openEditor()
{
    if (const std::optional<AnnotationId> id = currentComment()) emit openEditorRequested(*id);
}

void CommentsPane::openTextEditor(bool replying)
{
    const std::optional<std::size_t> at = currentIndex();
    if (!at || !canChange()) return;
    if (editor_) editor_->abandon();
    const Annotation target = store_->at(*at);

    auto* editor = new AnnotationTextEditor(this);
    editor->setObjectName(replying ? QStringLiteral("commentsReplyEditor") : QStringLiteral("commentsEditEditor"));
    editor->setMinimumHeight(60);
    if (!replying) editor->setPlainText(target.contents);
    editorTitle_->setText(replying ? catalog::translate(catalog::key::CommentsEditorReply).arg(target.author)
                                   : catalog::translate(catalog::key::CommentsEditorEdit));
    editorTitle_->show();
    editorSlot_->addWidget(editor);
    editor_ = editor;

    const AnnotationId targetId = target.id;
    const auto finished = [this] {
        editorTitle_->hide();
        if (view_->isVisible()) view_->setFocus(Qt::OtherFocusReason);
    };
    connect(editor, &AnnotationTextEditor::committed, this, [this, replying, targetId, finished](const QString& text) {
        finished();
        if (store_ == nullptr) return;
        const std::optional<std::size_t> held = store_->find(targetId);
        if (!held) return;
        const Annotation& answered = store_->at(*held);
        core::Status done;
        if (replying) {
            if (text.trimmed().isEmpty()) return;   // an empty reply is no reply
            const core::PdfRect box = answered.page >= 0 && static_cast<std::size_t>(answered.page) < pageBoxes_.size()
                                          ? pageBoxes_[static_cast<std::size_t>(answered.page)]
                                          : core::PdfRect{core::PdfPoint{0, 0}, core::PdfSize{612, 792}};
            Annotation reply = core::newReply(answered, box, text, core::kReplyColour, identity(), this->now());
            done = run(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<Annotation>{std::move(reply)}));
        } else {
            if (text == answered.contents) return;
            Annotation after = answered;
            after.contents = text;
            after.modified = core::PdfDate::fromUtcSeconds(this->now());
            done = run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{std::move(after)}));
        }
        if (!done) emit refused(done.error());
    });
    connect(editor, &AnnotationTextEditor::abandoned, this, finished);
    editor->setFocus(Qt::OtherFocusReason);
}

bool CommentsPane::editInPlace()
{
    if (!currentIndex() || !canChange()) return false;
    openTextEditor(false);
    return editor_ != nullptr;
}

bool CommentsPane::reply()
{
    if (!currentIndex() || !canChange()) return false;
    openTextEditor(true);
    return editor_ != nullptr;
}

core::Status CommentsPane::deleteThreads(const std::vector<std::size_t>& items)
{
    std::set<std::size_t> all;
    for (const std::size_t item : items) {
        for (const std::size_t member : store_->threadOf(item)) all.insert(member);
    }
    if (all.empty()) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.edit.noComment"), QString()};
    std::vector<AnnotationId> ids;
    ids.reserve(all.size());
    for (const std::size_t at : all) ids.push_back(store_->at(at).id);
    return run(std::make_unique<core::DeleteAnnotationCommand>(std::move(ids)));
}

core::Status CommentsPane::deleteCurrent()
{
    const std::optional<std::size_t> at = currentIndex();
    if (!at || !canChange()) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.edit.noComment"), QString()};
    return deleteThreads({*at});
}

core::Status CommentsPane::deleteAll()
{
    if (!canChange()) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.edit.noComment"), QString()};
    return deleteThreads(store_->topLevel());
}

core::Status CommentsPane::deleteMatching()
{
    if (!canChange()) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.edit.noComment"), QString()};
    ensureFresh();
    std::vector<std::size_t> items;
    for (const AnnotationId& id : listedComments()) {
        if (const std::optional<std::size_t> at = store_->find(id)) items.push_back(*at);
    }
    return deleteThreads(items);
}

core::Status CommentsPane::toggleCheckmarkOf(std::size_t index)
{
    if (!canChange() || index >= store_->size())
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.edit.noComment"), QString()};
    const QString who = identity();
    const Annotation& target = store_->at(index);
    const QString state = store_->isMarkedBy(index, who) ? QStringLiteral("Unmarked") : QStringLiteral("Marked");
    Annotation mark = core::newStateReply(target, QStringLiteral("Marked"), state, who, now());
    const core::Status done = run(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<Annotation>{std::move(mark)}));
    if (!done) emit refused(done.error());
    return done;
}

core::Status CommentsPane::toggleCheckmark()
{
    const std::optional<std::size_t> at = currentIndex();
    if (!at) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.edit.noComment"), QString()};
    return toggleCheckmarkOf(*at);
}

bool CommentsPane::showStatusMenu()
{
    if (!currentIndex() || !canChange()) return false;
    const QRect row = view_->visualRect(view_->currentIndex());
    statusMenu_->popup(view_->viewport()->mapToGlobal(row.bottomLeft()));
    return true;
}

core::Status CommentsPane::setStatus(const QString& state)
{
    const std::optional<std::size_t> at = currentIndex();
    if (!at || !canChange()) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.edit.noComment"), QString()};
    Annotation status = core::newStateReply(store_->at(*at), QStringLiteral("Review"), state, identity(), now());
    return run(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<Annotation>{std::move(status)}));
}

void CommentsPane::focusSearch()
{
    search_->setFocus(Qt::ShortcutFocusReason);
    search_->selectAll();
}

CommentsPane::Step CommentsPane::step(int direction, int fromPage)
{
    ensureFresh();
    const std::vector<AnnotationId> list = listedComments();
    if (list.empty() || store_ == nullptr) return Step::Nothing;
    const int count = static_cast<int>(list.size());
    int target = -1;
    bool wrapped = false;
    if (const std::optional<std::size_t> top = currentTopLevel()) {
        const AnnotationId id = store_->at(*top).id;
        const auto found = std::find(list.begin(), list.end(), id);
        if (found != list.end()) {
            target = static_cast<int>(found - list.begin()) + (direction >= 0 ? 1 : -1);
            if (target >= count) {
                target = 0;
                wrapped = true;
            } else if (target < 0) {
                target = count - 1;
                wrapped = true;
            }
        }
    }
    if (target < 0) {
        // Nothing current: from where the reader is.
        const auto pageOf = [this](const AnnotationId& id) {
            const std::optional<std::size_t> at = store_->find(id);
            return at ? store_->at(*at).page : -1;
        };
        if (direction >= 0) {
            for (int i = 0; i < count && target < 0; ++i)
                if (pageOf(list[static_cast<std::size_t>(i)]) >= fromPage) target = i;
            if (target < 0) target = 0;
        } else {
            for (int i = count - 1; i >= 0 && target < 0; --i)
                if (pageOf(list[static_cast<std::size_t>(i)]) <= fromPage) target = i;
            if (target < 0) target = count - 1;
        }
    }
    makeCurrent(indexOf(list[static_cast<std::size_t>(target)]), true);
    return wrapped ? Step::Wrapped : Step::Moved;
}

} // namespace rawpage::ui
