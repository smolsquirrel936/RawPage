// ui/panes/AttachmentsPane -- implementation. Stage 6 T11. See the header.

#include "ui/panes/AttachmentsPane.h"

#include "ui/shell/Catalog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QTimeZone>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace rawpage::ui {
namespace {

enum Column { NameColumn, DescriptionColumn, SizeColumn, ModifiedColumn, WhereColumn, ColumnCount };

QString sizeText(long long size)
{
    if (size < 0) return catalog::translate(catalog::key::AttachmentsSizeUnknown);
    return QLocale().formattedDataSize(size, 1, QLocale::DataSizeTraditionalFormat);
}

QString dateText(const core::PdfDate& date)
{
    if (!date.utcSeconds) return QString();
    return QLocale().toString(QDateTime::fromSecsSinceEpoch(*date.utcSeconds, QTimeZone::UTC).toLocalTime(), QLocale::ShortFormat);
}

// `Enter` on the list opens the current attachment, as a double click does -- the item view's own `activated`
// fires on platform-dependent keys, so the key is taken here (the Comments pane's arrangement).
class AttachmentsList final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void()> onEnter;

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers() == Qt::NoModifier
            && currentItem() != nullptr && onEnter) {
            onEnter();
            event->accept();
            return;
        }
        QTreeWidget::keyPressEvent(event);
    }
};

} // namespace

AttachmentsPane::AttachmentsPane(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("attachmentsPane"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(6, 6, 6, 6);
    column->setSpacing(6);

    // THE PANE'S COMMANDS -- each asks the window, which runs the registered command.
    // SHORT WORDS, AND BUTTONS THAT GIVE WAY: the four action labels side by side asked the sidebar -- and so the
    // window -- for 490 more pixels than it had, and widened a 1400-pixel window to 1892 (found by `tst_restore`
    // at T11, the find bar's S5 I-10 in a new place). Each button says one word and carries its command's full
    // label as its tooltip, and none holds the pane wider than the list beside it.
    auto* buttons = new QHBoxLayout;
    open_ = button(core::ActionId::AttachmentOpen, catalog::key::AttachmentsButtonOpen);
    extract_ = button(core::ActionId::AttachmentExtract, catalog::key::AttachmentsButtonExtract);
    add_ = button(core::ActionId::AttachmentAdd, catalog::key::AttachmentsButtonAdd);
    delete_ = button(core::ActionId::AttachmentDelete, catalog::key::AttachmentsButtonDelete);
    for (QToolButton* b : {open_, extract_, add_, delete_}) buttons->addWidget(b);
    buttons->addStretch(1);
    column->addLayout(buttons);

    auto* list = new AttachmentsList(this);
    list_ = list;
    list_->setObjectName(QStringLiteral("attachmentsList"));
    list_->setColumnCount(ColumnCount);
    list_->setHeaderLabels({catalog::translate(catalog::key::AttachmentsColumnName),
                            catalog::translate(catalog::key::AttachmentsColumnDescription),
                            catalog::translate(catalog::key::AttachmentsColumnSize),
                            catalog::translate(catalog::key::AttachmentsColumnModified),
                            catalog::translate(catalog::key::AttachmentsColumnWhere)});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(NameColumn, QHeaderView::Interactive);
    list_->header()->setSectionResizeMode(DescriptionColumn, QHeaderView::Stretch);
    column->addWidget(list_, 1);
    list->onEnter = [this] { emit actionRequested(core::ActionId::AttachmentOpen); };
    connect(list_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem*, int) { emit actionRequested(core::ActionId::AttachmentOpen); });
    connect(list_, &QTreeWidget::currentItemChanged, this, [this] {
        updateButtons();
        emit currentChanged();
    });

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("attachmentsStatus"));
    status_->setWordWrap(true);
    column->addWidget(status_);

    rebuild();
}

QToolButton* AttachmentsPane::button(core::ActionId id, QLatin1String word)
{
    auto* b = new QToolButton(this);
    b->setText(catalog::translate(word));
    b->setToolTip(catalog::translateAction(id));
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);   // its minimum is no claim on the pane's width
    connect(b, &QToolButton::clicked, this, [this, id] { emit actionRequested(id); });
    return b;
}

void AttachmentsPane::setDocument(const core::Attachments* files, const core::AnnotationStore* annotations, bool canChange)
{
    files_ = files;
    annotations_ = annotations;
    canChange_ = canChange;
    rebuild();
}

void AttachmentsPane::refresh()
{
    rebuild();
}

std::optional<AttachmentsPane::Row> AttachmentsPane::current() const
{
    const QTreeWidgetItem* item = list_->currentItem();
    if (item == nullptr) return std::nullopt;
    const int index = item->data(NameColumn, Qt::UserRole).toInt();
    if (index < 0 || index >= static_cast<int>(rows_.size())) return std::nullopt;
    return rows_[static_cast<std::size_t>(index)];
}

bool AttachmentsPane::select(const core::AttachmentRef& ref)
{
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = list_->topLevelItem(i);
        const int index = item->data(NameColumn, Qt::UserRole).toInt();
        if (rows_[static_cast<std::size_t>(index)].ref == ref) {
            list_->setCurrentItem(item);
            list_->scrollToItem(item);
            return true;
        }
    }
    return false;
}

bool AttachmentsPane::selectByName(const QString& name)
{
    for (const Row& row : rows_) {   // the document's files first: they are listed first
        if (row.file.name == name) return select(row.ref);
    }
    return false;
}

void AttachmentsPane::rebuild()
{
    const std::optional<Row> was = current();
    rows_.clear();
    list_->clear();

    if (files_ != nullptr && files_->status()) {
        for (const core::DocumentAttachment& file : files_->files())
            rows_.push_back(Row{core::AttachmentRef::ofDocument(file.key), file.file, -1});
    }
    if (annotations_ != nullptr && annotations_->status()) {
        std::vector<Row> pinned;
        for (const core::Annotation& a : annotations_->all()) {
            if (a.subtype != core::AnnotationSubtype::FileAttachment || !a.attachment) continue;
            if (annotations_->placementsOf(a.id).empty()) continue;   // listed nowhere: deleted
            pinned.push_back(Row{core::AttachmentRef::ofAnnotation(a.id), *a.attachment, a.page});
        }
        std::stable_sort(pinned.begin(), pinned.end(), [](const Row& x, const Row& y) { return x.page < y.page; });
        rows_.insert(rows_.end(), pinned.begin(), pinned.end());
    }

    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const Row& row = rows_[index];
        auto* item = new QTreeWidgetItem(list_);
        item->setText(NameColumn, row.file.name);
        item->setToolTip(NameColumn, row.file.name);
        item->setText(DescriptionColumn, row.file.description);
        item->setToolTip(DescriptionColumn, row.file.description);
        item->setText(SizeColumn, sizeText(row.file.size));
        item->setTextAlignment(SizeColumn, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(ModifiedColumn, dateText(row.file.modified));
        item->setText(WhereColumn, row.page < 0 ? catalog::translate(catalog::key::AttachmentsWhereDocument)
                                                : catalog::translate(catalog::key::AttachmentsWherePage).arg(row.page + 1));
        item->setData(NameColumn, Qt::UserRole, static_cast<int>(index));
    }
    for (int c = 0; c < ColumnCount; ++c) {
        if (c != DescriptionColumn) list_->resizeColumnToContents(c);
    }

    if (files_ == nullptr) {
        status_->setText(catalog::translate(catalog::key::AttachmentsNoDocument));
    } else if (!files_->status()) {
        status_->setText(catalog::translate(catalog::key::AttachmentsUnavailable)
                             .arg(catalog::translateError(QString(files_->status().error().messageKey),
                                                          files_->status().error().context)));
    } else if (rows_.empty()) {
        status_->setText(catalog::translate(catalog::key::AttachmentsEmpty));
    } else {
        status_->setText(catalog::translate(catalog::key::AttachmentsCount).arg(rows_.size()));
    }

    if (was) select(was->ref);
    updateButtons();
}

void AttachmentsPane::updateButtons()
{
    const bool chosen = current().has_value();
    open_->setEnabled(chosen);
    extract_->setEnabled(chosen);
    add_->setEnabled(canChange());
    // A document-level file by `DeleteAttachmentCommand`; a pushpin is an annotation, deleted as one (`MainWindow`).
    delete_->setEnabled(canChange() && chosen);
}

} // namespace rawpage::ui
