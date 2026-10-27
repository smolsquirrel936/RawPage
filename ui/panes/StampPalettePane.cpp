// ui/panes/StampPalettePane -- implementation. Stage 6 T10. See the header for what is listed and what a choice
// does.

#include "ui/panes/StampPalettePane.h"

#include "core/Log.h"
#include "core/cmd/ActionId.h"
#include "core/doc/Stamps.h"
#include "core/settings/ProfileStore.h"
#include "ui/dialogs/StampCategoryDialog.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DockEdge.h"
#include "ui/viewer/tools/StampTool.h"

#include <QAction>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

constexpr int kKeyRole = Qt::UserRole + 1;

QString standardKey(const QString& name) { return QStringLiteral("standard:") + name; }
QString dynamicKey(const QString& name) { return QStringLiteral("dynamic:") + name; }
QString customKey(const QString& id) { return QStringLiteral("custom:") + id; }

// The key of the stamp the tool places now.
QString keyOf(const StampChoice& choice)
{
    return choice.kind == StampChoice::Kind::Custom    ? customKey(choice.libraryId)
           : choice.kind == StampChoice::Kind::Dynamic ? dynamicKey(choice.name)
                                                        : standardKey(choice.name);
}

// The person's word for a built-in stamp: `stamps.name.<Name>` -- a dynamic one by its name less `Dynamic`, which is
// the same word with the time added.
QString nameOf(const QString& name)
{
    QString base = name;
    if (base.startsWith(QLatin1String("Dynamic"))) base = base.mid(7);
    return catalog::translate(QStringLiteral("stamps.name.") + base);
}

QString categoryTitle(const QString& category)
{
    if (category == QLatin1String(core::ProfileStore::kSignaturesCategory))
        return catalog::translate(catalog::key::StampsCategorySignatures);
    return category;
}

QColor colourOf(const std::vector<double>& rgb)
{
    if (rgb.size() != 3) return QColor(Qt::black);
    return QColor::fromRgbF(static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2]));
}

QIcon customIcon(core::ProfileStore* library, const QString& id)
{
    if (library == nullptr) return {};
    const core::Result<QByteArray> bytes = library->imageBytes(id);
    if (!bytes) return {};
    QImage image = QImage::fromData(bytes.value());
    if (image.isNull()) return {};
    return QIcon(QPixmap::fromImage(image.scaled(96, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

} // namespace

QPixmap StampPalettePane::builtInPreview(const QString& name, int height)
{
    // The stamp's own words, in its colour inside its border -- drawn the way `pdf/ops/AppearanceStreamBuilder`
    // draws it, small: a preview, never the stamp.
    const QString title = core::stampTitle(name);
    QFont font;
    font.setBold(true);
    font.setPixelSize(std::max(8, height * 5 / 10));
    const QFontMetrics metrics(font);
    const int width = metrics.horizontalAdvance(title) + height;
    QPixmap pixmap(width, height);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = colourOf(core::stampColour(name));
    QColor tint = ink;
    tint.setAlphaF(0.12f);
    QPainterPath border;
    border.addRoundedRect(QRectF(1.0, 1.0, width - 2.0, height - 2.0), 3.0, 3.0);
    painter.fillPath(border, tint);
    painter.setPen(QPen(ink, 1.5));
    painter.drawPath(border);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, width, height), Qt::AlignCenter, title);
    return pixmap;
}

StampPalettePane::StampPalettePane(core::ProfileStore* library, QWidget* parent)
    : QDockWidget(catalog::translate(catalog::key::StampsPaletteTitle), parent), library_(library)
{
    setObjectName(QStringLiteral("stampPalette"));
    auto* body = new QWidget(this);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(6, 6, 6, 6);
    column->setSpacing(4);

    tree_ = new QTreeWidget(body);
    tree_->setObjectName(QStringLiteral("stampTree"));
    tree_->setHeaderHidden(true);
    tree_->setIconSize(QSize(96, 32));
    tree_->setRootIsDecorated(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->viewport()->installEventFilter(this);
    column->addWidget(tree_, 1);
    itemMenu_ = new QMenu(this);
    itemMenu_->setObjectName(QStringLiteral("stampItemMenu"));

    auto* row = new QHBoxLayout;
    custom_ = new QPushButton(catalog::translateAction(core::ActionId::AnnotCustomStamp), body);
    custom_->setObjectName(QStringLiteral("stampCustom"));
    custom_->setEnabled(library_ != nullptr);
    row->addWidget(custom_);
    row->addStretch(1);
    refile_ = new QToolButton(body);
    refile_->setObjectName(QStringLiteral("stampRefile"));
    refile_->setText(catalog::translate(catalog::key::StampsRefile));
    delete_ = new QToolButton(body);
    delete_->setObjectName(QStringLiteral("stampDelete"));
    delete_->setText(catalog::translate(catalog::key::StampsDelete));
    row->addWidget(refile_);
    row->addWidget(delete_);
    column->addLayout(row);
    setWidget(body);

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (rebuilding_ || item == nullptr) return;
        const QString key = item->data(0, kKeyRole).toString();
        if (key.isEmpty()) return;   // a category heading
        if (choose(key)) emit chosen();
    });
    connect(tree_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { (void)showItemMenu(pos); });
    connect(custom_, &QPushButton::clicked, this, [this] { emit customRequested(); });
    connect(refile_, &QToolButton::clicked, this, [this] { moveChosenTo(); });
    connect(delete_, &QToolButton::clicked, this, [this] {
        if (core::Status done = deleteChosen(); !done) emit refused(done.error());
    });

    // THE EDGE THAT FACES THE PAGE, drawn (`ui/shell/DockEdge`, the user's request at Stage 5, A-123).
    new DockEdge(this);
    if (library_ != nullptr) (void)refileReservedCategories(*library_);
    rebuild();
}

int StampPalettePane::refileReservedCategories(core::ProfileStore& library)
{
    // Here, not where the library loads: the default category is a word of the person's language, and the
    // catalogs load after the library (`app/main.cpp`). A second window's palette finds nothing left to move.
    const QString target = catalog::translate(catalog::key::StampsCategoryDefault);
    int moved = 0;
    const std::vector<core::CustomStamp> stamps = library.stamps();   // a copy: each move rewrites the list
    for (const core::CustomStamp& stamp : stamps) {
        if (!StampCategoryDialog::isReservedName(stamp.category)) continue;
        if (const core::Status done = library.setCategory(stamp.id, target); !done) {
            RAWPAGE_LOG_WARNING(core::LogSubsystem::Ui) << "stamps: could not move" << stamp.name << "out of the category"
                                                        << stamp.category << "--" << QString(done.error().messageKey);
            continue;
        }
        RAWPAGE_LOG_INFO(core::LogSubsystem::Ui) << "stamps:" << stamp.name << "moved from" << stamp.category << "to"
                                                 << target << "-- a built-in heading's name (S6 guide I-42)";
        ++moved;
    }
    return moved;
}

void StampPalettePane::reload()
{
    rebuild();
}

void StampPalettePane::rebuild()
{
    rebuilding_ = true;
    tree_->clear();
    favourites_ = nullptr;
    const auto heading = [this](const QString& title) {
        auto* item = new QTreeWidgetItem(tree_, QStringList{title});
        item->setFlags(Qt::ItemIsEnabled);
        item->setExpanded(true);
        return item;
    };
    const auto builtIn = [](QTreeWidgetItem* parent, const QString& key, const QString& name) {
        auto* item = new QTreeWidgetItem(parent, QStringList{nameOf(name)});
        item->setIcon(0, QIcon(builtInPreview(name)));
        item->setData(0, kKeyRole, key);
        return item;
    };
    const auto custom = [this](QTreeWidgetItem* parent, const core::CustomStamp& stamp) {
        auto* item = new QTreeWidgetItem(parent, QStringList{stamp.name});
        item->setIcon(0, customIcon(library_, stamp.id));
        item->setData(0, kKeyRole, customKey(stamp.id));
        return item;
    };

    // FAVOURITES first, and there even when it holds nothing: a person told where favourites go finds the right
    // click that puts one there (I-41) -- the heading says so. Not without a library, which could hold none.
    if (library_ != nullptr) {
        favourites_ = heading(catalog::translate(catalog::key::StampsCategoryFavourites));
        favourites_->setToolTip(0, catalog::translate(catalog::key::StampsFavouritesHint));
        for (const QLatin1String name : core::standardStampNames()) {
            if (library_->isFavourite(QString(name))) builtIn(favourites_, standardKey(QString(name)), QString(name));
        }
        for (const QLatin1String name : core::dynamicStampNames()) {
            if (library_->isFavourite(QString(name))) builtIn(favourites_, dynamicKey(QString(name)), QString(name));
        }
        for (const core::CustomStamp& stamp : library_->stamps()) {
            if (stamp.favourite) custom(favourites_, stamp);
        }
    }
    QTreeWidgetItem* standard = heading(catalog::translate(catalog::key::StampsCategoryStandard));
    for (const QLatin1String name : core::standardStampNames()) builtIn(standard, standardKey(QString(name)), QString(name));
    QTreeWidgetItem* dynamic = heading(catalog::translate(catalog::key::StampsCategoryDynamic));
    for (const QLatin1String name : core::dynamicStampNames()) builtIn(dynamic, dynamicKey(QString(name)), QString(name));
    if (library_ != nullptr) {
        for (const QString& category : library_->categories()) {
            QTreeWidgetItem* under = heading(categoryTitle(category));
            for (const core::CustomStamp& stamp : library_->stamps()) {
                if (stamp.category == category) custom(under, stamp);
            }
        }
    }
    tree_->expandAll();
    // The choice survives a rebuild where it still exists. With the tool armed, a choice deleted falls back to the
    // stamp the tool places now -- the first standard stamp -- and with it put away nothing is highlighted (I-39).
    if (itemFor(chosen_) == nullptr) chosen_.clear();
    if (chosen_.isEmpty() && toolArmed_ && itemFor(keyOf(currentStamp())) != nullptr) chosen_ = keyOf(currentStamp());
    if (QTreeWidgetItem* item = itemFor(chosen_)) tree_->setCurrentItem(item);
    rebuilding_ = false;
    updateButtons();
}

void StampPalettePane::followTool(bool armed)
{
    toolArmed_ = armed;
    rebuilding_ = true;
    if (!armed) {
        // Nothing current: the next click on ANY stamp -- the one just used included -- is a change, and chooses.
        chosen_.clear();
        tree_->setCurrentItem(nullptr);
        tree_->clearSelection();
    } else if (chosen_ != keyOf(currentStamp())) {
        // Armed with no click here (`Ctrl+Alt+S`): show the stamp it places.
        if (QTreeWidgetItem* item = itemFor(keyOf(currentStamp()))) {
            chosen_ = keyOf(currentStamp());
            tree_->setCurrentItem(item);
        }
    }
    rebuilding_ = false;
    updateButtons();
}

QTreeWidgetItem* StampPalettePane::itemFor(const QString& key) const
{
    if (key.isEmpty()) return nullptr;
    // The first occurrence under its OWN category, not under Favourites: a choice is shown where it lives.
    QTreeWidgetItem* inFavourites = nullptr;
    for (int top = 0; top < tree_->topLevelItemCount(); ++top) {
        QTreeWidgetItem* heading = tree_->topLevelItem(top);
        for (int i = 0; i < heading->childCount(); ++i) {
            if (heading->child(i)->data(0, kKeyRole).toString() != key) continue;
            if (heading != favourites_) return heading->child(i);
            if (inFavourites == nullptr) inFavourites = heading->child(i);
        }
    }
    return inFavourites;
}

bool StampPalettePane::choose(const QString& key)
{
    StampChoice next;
    if (key.startsWith(QLatin1String("standard:"))) {
        next.kind = StampChoice::Kind::Standard;
        next.name = key.mid(9);
        if (!core::isStandardStampName(next.name)) {
            emit refused(core::Error{core::ErrorCode::NotFound, QLatin1String("error.edit.stampUnknown"), next.name});
            return false;
        }
    } else if (key.startsWith(QLatin1String("dynamic:"))) {
        next.kind = StampChoice::Kind::Dynamic;
        next.name = key.mid(8);
        if (!core::isDynamicStampName(next.name)) {
            emit refused(core::Error{core::ErrorCode::NotFound, QLatin1String("error.edit.stampUnknown"), next.name});
            return false;
        }
    } else if (key.startsWith(QLatin1String("custom:")) && library_ != nullptr) {
        const QString id = key.mid(7);
        const std::optional<core::CustomStamp> stamp = library_->find(id);
        if (!stamp) {
            emit refused(core::Error{core::ErrorCode::NotFound, QLatin1String("error.stamps.unknown"), id});
            return false;
        }
        const core::Result<QByteArray> bytes = library_->imageBytes(id);
        core::Result<core::StampImage> image = bytes ? decodeStampImage(bytes.value(), stamp->name) : core::Result<core::StampImage>(bytes.error());
        if (!image) {
            emit refused(image.error());
            return false;
        }
        next.kind = StampChoice::Kind::Custom;
        next.name = stamp->name;
        next.libraryId = id;
        next.image = std::move(image).value();
    } else {
        emit refused(core::Error{core::ErrorCode::NotFound, QLatin1String("error.edit.stampUnknown"), key});
        return false;
    }
    setCurrentStamp(std::move(next));
    chosen_ = key;
    if (QTreeWidgetItem* item = itemFor(key); item != nullptr && tree_->currentItem() != item) {
        rebuilding_ = true;
        tree_->setCurrentItem(item);
        rebuilding_ = false;
    }
    updateButtons();
    return true;
}

QString StampPalettePane::chosenKey() const { return chosen_; }

namespace {

// The library's name for a key's stamp: a built-in's `/Name`, a custom stamp's id; empty for none.
QString libraryName(const QString& key)
{
    const qsizetype colon = key.indexOf(QLatin1Char(':'));
    return colon < 0 ? QString() : key.mid(colon + 1);
}

core::Error noLibrary()
{
    return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.stamps.noLibrary"), QString()};
}

} // namespace

core::Status StampPalettePane::setFavourite(const QString& key, bool favourite)
{
    if (library_ == nullptr) return noLibrary();
    if (libraryName(key).isEmpty()) return core::Error{core::ErrorCode::NotFound, QLatin1String("error.stamps.unknown"), key};
    const core::Status done = library_->setFavourite(libraryName(key), favourite);
    if (done) rebuild();
    return done;
}

bool StampPalettePane::showItemMenu(const QPoint& pos)
{
    if (library_ == nullptr) return false;
    const QTreeWidgetItem* item = tree_->itemAt(pos);
    const QString key = item != nullptr ? item->data(0, kKeyRole).toString() : QString();
    if (key.isEmpty()) return false;   // nothing, or a heading
    const bool favourite = library_->isFavourite(libraryName(key));
    itemMenu_->clear();
    QAction* toggle = ActionFactory::addNonCommandMenuItem(
        itemMenu_, catalog::translate(favourite ? catalog::key::StampsFavouriteRemove : catalog::key::StampsFavouriteAdd));
    toggle->setObjectName(QStringLiteral("stampFavouriteToggle"));
    connect(toggle, &QAction::triggered, this, [this, key, favourite] {
        if (core::Status done = setFavourite(key, !favourite); !done) emit refused(done.error());
    });
    itemMenu_->popup(tree_->viewport()->mapToGlobal(pos));
    return true;
}

bool StampPalettePane::eventFilter(QObject* watched, QEvent* event)
{
    // A RIGHT PRESS IS THE MENU'S ALONE. The view would make the item under it current, and a current item is a
    // choice that arms the tool -- so asking for a stamp's menu would have picked it up (I-41). Windows opens the
    // menu on the release, which still reaches the view.
    if (watched == tree_->viewport()
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)
        && static_cast<QMouseEvent*>(event)->button() == Qt::RightButton)
        return true;
    return QDockWidget::eventFilter(watched, event);
}

core::Status StampPalettePane::fileUnder(const QString& category)
{
    if (library_ == nullptr) return noLibrary();
    if (!chosen_.startsWith(QLatin1String("custom:")))
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.stamps.builtIn"), QString()};
    if (StampCategoryDialog::isReservedName(category))
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.stamps.reservedCategory"), category.trimmed()};
    const core::Status done = library_->setCategory(libraryName(chosen_), category);
    if (done) rebuild();
    return done;
}

core::Status StampPalettePane::deleteChosen()
{
    if (library_ == nullptr) return noLibrary();
    if (!chosen_.startsWith(QLatin1String("custom:")))
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.stamps.builtIn"), QString()};
    const core::Status done = library_->remove(libraryName(chosen_));
    if (done) {
        chosen_.clear();
        setCurrentStamp(StampChoice{});   // back to the first standard stamp
        rebuild();
    }
    return done;
}

void StampPalettePane::moveChosenTo()
{
    if (library_ == nullptr || !chosen_.startsWith(QLatin1String("custom:"))) return;
    const std::optional<core::CustomStamp> stamp = library_->find(libraryName(chosen_));
    if (!stamp) return;
    // A list of the categories in use and *New…* (I-40), not a name to remember and retype.
    StampCategoryDialog dialog(library_->categories(), stamp->category, this);
    if (dialog.exec() != QDialog::Accepted) return;
    if (core::Status done = fileUnder(dialog.chosenCategory()); !done) emit refused(done.error());
}

void StampPalettePane::updateButtons()
{
    const bool custom = library_ != nullptr && chosen_.startsWith(QLatin1String("custom:"));
    refile_->setEnabled(custom);
    delete_->setEnabled(custom);
}

} // namespace rawpage::ui
