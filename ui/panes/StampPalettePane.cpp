// ui/panes/StampPalettePane -- implementation. Stage 6 T10. See the header for what is listed and what a choice
// does.

#include "ui/panes/StampPalettePane.h"

#include "core/cmd/ActionId.h"
#include "core/doc/Stamps.h"
#include "core/settings/ProfileStore.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DockEdge.h"
#include "ui/viewer/tools/StampTool.h"

#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
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
    column->addWidget(tree_, 1);

    auto* row = new QHBoxLayout;
    custom_ = new QPushButton(catalog::translateAction(core::ActionId::AnnotCustomStamp), body);
    custom_->setObjectName(QStringLiteral("stampCustom"));
    custom_->setEnabled(library_ != nullptr);
    row->addWidget(custom_);
    row->addStretch(1);
    favourite_ = new QToolButton(body);
    favourite_->setObjectName(QStringLiteral("stampFavourite"));
    favourite_->setText(catalog::translate(catalog::key::StampsFavourite));
    favourite_->setCheckable(true);
    refile_ = new QToolButton(body);
    refile_->setObjectName(QStringLiteral("stampRefile"));
    refile_->setText(catalog::translate(catalog::key::StampsRefile));
    delete_ = new QToolButton(body);
    delete_->setObjectName(QStringLiteral("stampDelete"));
    delete_->setText(catalog::translate(catalog::key::StampsDelete));
    row->addWidget(favourite_);
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
    connect(custom_, &QPushButton::clicked, this, [this] { emit customRequested(); });
    connect(favourite_, &QToolButton::clicked, this, [this](bool on) {
        if (core::Status done = setFavourite(on); !done) emit refused(done.error());
    });
    connect(refile_, &QToolButton::clicked, this, [this] { moveChosenTo(); });
    connect(delete_, &QToolButton::clicked, this, [this] {
        if (core::Status done = deleteChosen(); !done) emit refused(done.error());
    });

    // THE EDGE THAT FACES THE PAGE, drawn (`ui/shell/DockEdge`, the user's request at Stage 5, A-123).
    new DockEdge(this);
    rebuild();
}

void StampPalettePane::reload()
{
    rebuild();
}

void StampPalettePane::rebuild()
{
    rebuilding_ = true;
    tree_->clear();
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

    // FAVOURITES first, and only when there are any: a heading over nothing is a question nobody asked.
    QTreeWidgetItem* favourites = nullptr;
    const auto favouritesHeading = [&]() {
        if (favourites == nullptr) favourites = heading(catalog::translate(catalog::key::StampsCategoryFavourites));
        return favourites;
    };
    if (library_ != nullptr) {
        for (const QLatin1String name : core::standardStampNames()) {
            if (library_->isFavourite(QString(name))) builtIn(favouritesHeading(), standardKey(QString(name)), QString(name));
        }
        for (const QLatin1String name : core::dynamicStampNames()) {
            if (library_->isFavourite(QString(name))) builtIn(favouritesHeading(), dynamicKey(QString(name)), QString(name));
        }
        for (const core::CustomStamp& stamp : library_->stamps()) {
            if (stamp.favourite) custom(favouritesHeading(), stamp);
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
    // The choice survives a rebuild where it still exists; one deleted falls back to the first standard stamp.
    if (chosen_.isEmpty() || itemFor(chosen_) == nullptr) {
        chosen_.clear();
        const StampChoice& current = currentStamp();
        const QString key = current.kind == StampChoice::Kind::Custom ? customKey(current.libraryId)
                            : current.kind == StampChoice::Kind::Dynamic ? dynamicKey(current.name)
                                                                         : standardKey(current.name);
        if (itemFor(key) != nullptr) chosen_ = key;
    }
    if (QTreeWidgetItem* item = itemFor(chosen_)) tree_->setCurrentItem(item);
    rebuilding_ = false;
    updateButtons();
}

QTreeWidgetItem* StampPalettePane::itemFor(const QString& key) const
{
    if (key.isEmpty()) return nullptr;
    // The first occurrence under its OWN category, not under Favourites: a choice is shown where it lives.
    QTreeWidgetItem* first = nullptr;
    for (int top = 0; top < tree_->topLevelItemCount(); ++top) {
        QTreeWidgetItem* heading = tree_->topLevelItem(top);
        for (int i = 0; i < heading->childCount(); ++i) {
            if (heading->child(i)->data(0, kKeyRole).toString() == key) {
                if (top > 0 || tree_->topLevelItemCount() == 1) return heading->child(i);
                if (first == nullptr) first = heading->child(i);
            }
        }
    }
    return first;
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

core::Status StampPalettePane::setFavourite(bool favourite)
{
    if (library_ == nullptr) return noLibrary();
    if (chosen_.isEmpty()) return core::Error{core::ErrorCode::NotFound, QLatin1String("error.stamps.unknown"), QString()};
    const core::Status done = library_->setFavourite(libraryName(chosen_), favourite);
    if (done) rebuild();
    return done;
}

core::Status StampPalettePane::fileUnder(const QString& category)
{
    if (library_ == nullptr) return noLibrary();
    if (!chosen_.startsWith(QLatin1String("custom:")))
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.stamps.builtIn"), QString()};
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
    bool accepted = false;
    const QString category = QInputDialog::getText(this, catalog::translate(catalog::key::StampsRefile),
                                                   catalog::translate(catalog::key::StampsRefilePrompt), QLineEdit::Normal,
                                                   categoryTitle(stamp->category), &accepted);
    if (!accepted) return;
    // The reserved category is offered by its shown word, in whatever language that is.
    const QString target = category == categoryTitle(QLatin1String(core::ProfileStore::kSignaturesCategory))
                               ? QLatin1String(core::ProfileStore::kSignaturesCategory)
                               : category;
    if (core::Status done = fileUnder(target); !done) emit refused(done.error());
}

void StampPalettePane::updateButtons()
{
    const bool custom = library_ != nullptr && chosen_.startsWith(QLatin1String("custom:"));
    favourite_->setEnabled(library_ != nullptr && !chosen_.isEmpty());
    favourite_->setChecked(library_ != nullptr && !chosen_.isEmpty() && library_->isFavourite(libraryName(chosen_)));
    refile_->setEnabled(custom);
    delete_->setEnabled(custom);
}

} // namespace rawpage::ui
