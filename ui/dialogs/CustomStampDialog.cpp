// ui/dialogs/CustomStampDialog -- implementation. Stage 6 T10. See the header for the three sources and why none
// is a PDF.

#include "ui/dialogs/CustomStampDialog.h"

#include "core/settings/ProfileStore.h"
#include "ui/dialogs/StampCategoryDialog.h"
#include "ui/shell/Catalog.h"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace rawpage::ui {
namespace {

constexpr qint64 kMaxImageFile = core::ProfileStore::kMaxImageBytes;

// A picture as the bytes of a PNG, in MEMORY: `QImageWriter` opens the buffer itself. Not a file -- the library
// that keeps it is `core::ProfileStore`, which writes it (`core/settings/`, the layer that may).
QByteArray pngOf(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    QImageWriter writer(&buffer, "png");
    if (!writer.write(image)) return {};
    return bytes;
}

// The bounds of every pixel with any ink, grown by `margin`; null for none.
QRect inkBounds(const QImage& image, int margin)
{
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) == 0) continue;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    if (right < 0) return {};
    return QRect(QPoint(left, top), QPoint(right, bottom)).adjusted(-margin, -margin, margin, margin).intersected(image.rect());
}

// The list's name column is edited in a line edit held to the library's longest name, as the Name box is.
class NameDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto* line = qobject_cast<QLineEdit*>(editor)) line->setMaxLength(core::ProfileStore::kMaxNameLength);
        return editor;
    }
};

} // namespace

// ---- The pad ---------------------------------------------------------------------------------------

InitialsPad::InitialsPad(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("initialsPad"));
    setMinimumSize(240, 100);
    setCursor(Qt::CrossCursor);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void InitialsPad::clear()
{
    strokes_.clear();
    update();
    emit changed();
}

void InitialsPad::addStroke(const std::vector<QPointF>& points)
{
    if (points.empty()) return;
    strokes_.push_back(points);
    update();
    emit changed();
}

namespace {

void drawStrokes(QPainter& painter, const std::vector<std::vector<QPointF>>& strokes, double width)
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::black, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const std::vector<QPointF>& stroke : strokes) {
        if (stroke.size() == 1) {
            painter.drawPoint(stroke.front());
            continue;
        }
        QPainterPath path(stroke.front());
        for (std::size_t i = 1; i < stroke.size(); ++i) path.lineTo(stroke[i]);
        painter.drawPath(path);
    }
}

} // namespace

QImage InitialsPad::picture() const
{
    if (strokes_.empty()) return {};
    QImage image(size() * 2, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.scale(2.0, 2.0);
    drawStrokes(painter, strokes_, 2.5);
    painter.end();
    const QRect ink = inkBounds(image, 6);
    return ink.isEmpty() ? QImage() : image.copy(ink);
}

void InitialsPad::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    painter.setPen(QPen(palette().mid().color(), 1.0, Qt::DashLine));
    painter.drawLine(QPointF(8, height() * 0.75), QPointF(width() - 8, height() * 0.75));   // a line to sign on
    drawStrokes(painter, strokes_, 2.5);
}

void InitialsPad::mousePressEvent(QMouseEvent* event)
{
    strokes_.push_back({event->position()});
    update();
}

void InitialsPad::mouseMoveEvent(QMouseEvent* event)
{
    if (strokes_.empty() || !(event->buttons() & Qt::LeftButton)) return;
    strokes_.back().push_back(event->position());
    update();
}

void InitialsPad::mouseReleaseEvent(QMouseEvent*)
{
    emit changed();
}

// ---- The dialog ------------------------------------------------------------------------------------

QString CustomStampDialog::imageFilter()
{
    return catalog::translate(catalog::key::StampsDialogImageFilter) + QStringLiteral(" (*.png *.jpg *.jpeg *.bmp *.gif)");
}

CustomStampDialog::CustomStampDialog(const QStringList& categories, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("customStampDialog"));
    setWindowTitle(catalog::translate(catalog::key::StampsDialogTitle));
    auto* column = new QVBoxLayout(this);

    sources_ = new QTabWidget(this);
    // An image file.
    auto* filePage = new QWidget(sources_);
    auto* fileColumn = new QVBoxLayout(filePage);
    auto* fileRow = new QHBoxLayout;
    path_ = new QLineEdit(filePage);
    path_->setObjectName(QStringLiteral("stampImagePath"));
    auto* browseButton = new QPushButton(catalog::translate(catalog::key::StampsDialogBrowse), filePage);
    fileRow->addWidget(path_, 1);
    fileRow->addWidget(browseButton);
    fileColumn->addLayout(fileRow);
    names_ = new QTableWidget(0, 2, filePage);
    names_->setObjectName(QStringLiteral("stampImageNames"));
    names_->setHorizontalHeaderLabels({catalog::translate(catalog::key::StampsDialogColumnFile),
                                       catalog::translate(catalog::key::StampsDialogColumnName)});
    names_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    names_->horizontalHeader()->setStretchLastSection(true);
    names_->verticalHeader()->hide();
    names_->setSelectionBehavior(QAbstractItemView::SelectRows);
    names_->setSelectionMode(QAbstractItemView::SingleSelection);
    names_->setItemDelegateForColumn(1, new NameDelegate(names_));
    names_->hide();
    fileColumn->addWidget(names_, 1);
    preview_ = new QLabel(filePage);
    preview_->setObjectName(QStringLiteral("stampImagePreview"));
    preview_->setMinimumHeight(96);
    preview_->setAlignment(Qt::AlignCenter);
    fileColumn->addWidget(preview_, 1);
    sources_->addTab(filePage, catalog::translate(catalog::key::StampsDialogSourceImage));
    // Drawn initials.
    auto* drawPage = new QWidget(sources_);
    auto* drawColumn = new QVBoxLayout(drawPage);
    pad_ = new InitialsPad(drawPage);
    auto* clearButton = new QPushButton(catalog::translate(catalog::key::StampsDialogClear), drawPage);
    drawColumn->addWidget(pad_, 1);
    drawColumn->addWidget(clearButton, 0, Qt::AlignRight);
    sources_->addTab(drawPage, catalog::translate(catalog::key::StampsDialogSourceDrawn));
    // Typed initials.
    auto* typePage = new QWidget(sources_);
    auto* typeColumn = new QVBoxLayout(typePage);
    typed_ = new QLineEdit(typePage);
    typed_->setObjectName(QStringLiteral("stampTypedInitials"));
    typedPreview_ = new QLabel(typePage);
    typedPreview_->setMinimumHeight(96);
    typedPreview_->setAlignment(Qt::AlignCenter);
    typeColumn->addWidget(typed_);
    typeColumn->addWidget(typedPreview_, 1);
    sources_->addTab(typePage, catalog::translate(catalog::key::StampsDialogSourceTyped));
    column->addWidget(sources_, 1);

    form_ = new QFormLayout;
    QFormLayout* form = form_;
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("stampName"));
    name_->setMaxLength(core::ProfileStore::kMaxNameLength);
    category_ = new QComboBox(this);
    category_->setObjectName(QStringLiteral("stampCategory"));
    category_->setEditable(true);
    category_->addItem(catalog::translate(catalog::key::StampsCategorySignatures),
                       QString::fromLatin1(core::ProfileStore::kSignaturesCategory));
    for (const QString& category : categories) {
        // A built-in heading's name is no category to offer (I-42); the palette has moved any such stamp already.
        if (category != QLatin1String(core::ProfileStore::kSignaturesCategory) && !StampCategoryDialog::isReservedName(category))
            category_->addItem(category, category);
    }
    favourite_ = new QCheckBox(catalog::translate(catalog::key::StampsFavourite), this);
    favourite_->setObjectName(QStringLiteral("stampFavouriteOnMake"));
    form->addRow(catalog::translate(catalog::key::StampsDialogName), name_);
    form->addRow(catalog::translate(catalog::key::StampsDialogCategory), category_);
    form->addRow(QString(), favourite_);
    column->addLayout(form);

    refusal_ = new QLabel(this);
    refusal_->setObjectName(QStringLiteral("stampRefusal"));
    refusal_->setWordWrap(true);
    column->addWidget(refusal_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    column->addWidget(buttons_);
    okText_ = buttons_->button(QDialogButtonBox::Ok)->text();

    connect(buttons_, &QDialogButtonBox::accepted, this, &CustomStampDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked, this, [this] { browse(); });
    connect(path_, &QLineEdit::editingFinished, this, [this] {
        if (path_->text() != shownPaths_) (void)setImagePath(path_->text());
    });
    connect(names_, &QTableWidget::itemChanged, this, [this] { refresh(); });
    connect(names_, &QTableWidget::currentCellChanged, this, [this](int row) { showPreview(row); });
    connect(clearButton, &QPushButton::clicked, pad_, &InitialsPad::clear);
    connect(pad_, &InitialsPad::changed, this, [this] { refresh(); });
    connect(typed_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(name_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(category_, &QComboBox::currentTextChanged, this, [this] { refresh(); });
    connect(sources_, &QTabWidget::currentChanged, this, [this](int index) {
        // Initials go under Signatures unless the person chose otherwise.
        if (index != static_cast<int>(Source::ImageFile)) category_->setCurrentIndex(0);
        refresh();
    });
    // An image is filed under the person's first category, or one named for them; initials under Signatures.
    if (category_->count() > 1) category_->setCurrentIndex(1);
    else category_->setEditText(catalog::translate(catalog::key::StampsCategoryDefault));
    refresh();
}

void CustomStampDialog::setSource(Source source) { sources_->setCurrentIndex(static_cast<int>(source)); }

CustomStampDialog::Source CustomStampDialog::source() const { return static_cast<Source>(sources_->currentIndex()); }

void CustomStampDialog::setName(const QString& name) { name_->setText(name); }

void CustomStampDialog::setCategory(const QString& category)
{
    const int at = category_->findData(category);
    if (at >= 0) category_->setCurrentIndex(at);
    else category_->setEditText(category);
}

void CustomStampDialog::setFavourite(bool favourite) { favourite_->setChecked(favourite); }

void CustomStampDialog::setTypedInitials(const QString& text) { typed_->setText(text); }

void CustomStampDialog::browse()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, catalog::translate(catalog::key::StampsDialogBrowse),
                                                            QString(), imageFilter());
    if (!paths.isEmpty()) (void)setImagePaths(paths);
}

bool CustomStampDialog::setImagePath(const QString& path)
{
    return setImagePaths(QStringList{path});
}

bool CustomStampDialog::setImagePaths(const QStringList& paths)
{
    images_.clear();
    leftOut_.clear();
    preview_->clear();
    for (const QString& path : paths) {
        QFile file(path);
        QByteArray bytes;
        if (file.open(QIODevice::ReadOnly) && file.size() <= kMaxImageFile) bytes = file.readAll();
        const QImage image = QImage::fromData(bytes);
        if (bytes.isEmpty() || image.isNull()) {
            // NOT AN IMAGE -- a PDF among them: left out by name, and nothing made of it (A-78).
            leftOut_ << QFileInfo(path).fileName();
            continue;
        }
        // A PNG or a JPEG as it is -- a JPEG keeps its bytes into the document (A-77); any other image as a PNG of
        // itself.
        images_.push_back(Picked{path, core::ProfileStore::imageFormatOf(bytes).isEmpty() ? pngOf(image) : bytes, image});
    }
    if (paths.size() == 1) {
        shownPaths_ = paths.front();
    } else {
        QStringList quoted;
        for (const QString& path : paths) quoted << QLatin1Char('"') + path + QLatin1Char('"');
        shownPaths_ = quoted.join(QLatin1Char(' '));
    }
    path_->setText(shownPaths_);

    // TWO OR MORE: the list, each file beside its stamp's name, the file's own to start with (I-43).
    {
        const QSignalBlocker quiet(names_);
        names_->setRowCount(0);
        if (images_.size() >= 2) {
            names_->setRowCount(static_cast<int>(images_.size()));
            for (int row = 0; row < names_->rowCount(); ++row) {
                const QFileInfo file(images_[static_cast<std::size_t>(row)].path);
                auto* source = new QTableWidgetItem(file.fileName());
                source->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                names_->setItem(row, 0, source);
                names_->setItem(row, 1, new QTableWidgetItem(file.completeBaseName().left(core::ProfileStore::kMaxNameLength)));
            }
            names_->setCurrentCell(0, 1);
        } else if (images_.size() == 1 && name_->text().trimmed().isEmpty()) {
            name_->setText(QFileInfo(images_.front().path).completeBaseName());
        }
    }
    names_->setVisible(images_.size() >= 2);
    showPreview(0);
    refresh();
    return !images_.empty();
}

void CustomStampDialog::showPreview(int image)
{
    if (image < 0 || static_cast<std::size_t>(image) >= images_.size()) {
        preview_->clear();
        return;
    }
    preview_->setPixmap(QPixmap::fromImage(images_[static_cast<std::size_t>(image)].image.scaled(
        320, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

bool CustomStampDialog::nameBoxShown() const
{
    return !(source() == Source::ImageFile && images_.size() >= 2);
}

QImage CustomStampDialog::typedPicture() const
{
    const QString text = typed_->text().trimmed();
    if (text.isEmpty()) return {};
    QFont font;
    const QStringList families = QFontDatabase::families();
    if (families.contains(QStringLiteral("Segoe Script"))) font.setFamily(QStringLiteral("Segoe Script"));
    else font.setItalic(true);
    font.setPixelSize(72);
    const QFontMetrics metrics(font);
    QImage image(metrics.horizontalAdvance(text) + 48, metrics.height() + 24, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(image.rect(), Qt::AlignCenter, text);
    painter.end();
    const QRect ink = inkBounds(image, 6);
    return ink.isEmpty() ? QImage() : image.copy(ink);
}

std::optional<CustomStampDialog::Made> CustomStampDialog::made() const
{
    std::vector<Made> all = madeAll();
    if (all.size() != 1) return std::nullopt;
    return std::move(all.front());
}

std::vector<CustomStampDialog::Made> CustomStampDialog::madeAll() const
{
    std::vector<Made> out;
    if (!refusal().isEmpty()) return out;
    Made base;
    const int index = category_->currentIndex();
    const bool picked = index >= 0 && category_->itemText(index) == category_->currentText();
    base.category = picked ? category_->itemData(index).toString() : category_->currentText().trimmed();
    base.favourite = favourite_->isChecked();
    switch (source()) {
    case Source::ImageFile:
        for (std::size_t i = 0; i < images_.size(); ++i) {
            Made one = base;
            one.name = images_.size() >= 2 ? names_->item(static_cast<int>(i), 1)->text().trimmed() : name_->text().trimmed();
            one.image = images_[i].bytes;
            one.sourceName = QFileInfo(images_[i].path).fileName();
            out.push_back(std::move(one));
        }
        break;
    case Source::Drawn:
        base.name = name_->text().trimmed();
        base.image = pngOf(pad_->picture());
        out.push_back(std::move(base));
        break;
    case Source::Typed:
        base.name = name_->text().trimmed();
        base.image = pngOf(typedPicture());
        out.push_back(std::move(base));
        break;
    }
    return out;
}

QString CustomStampDialog::refusal() const
{
    switch (source()) {
    case Source::ImageFile:
        if (images_.empty()) {
            if (!leftOut_.isEmpty())
                return catalog::translateError(QStringLiteral("error.stamps.notAnImage"), leftOut_.join(QStringLiteral(", ")));
            return catalog::translate(catalog::key::StampsDialogNeedImage);
        }
        break;
    case Source::Drawn:
        if (pad_->isEmpty()) return catalog::translate(catalog::key::StampsDialogNeedDrawing);
        break;
    case Source::Typed:
        if (typed_->text().trimmed().isEmpty()) return catalog::translate(catalog::key::StampsDialogNeedText);
        break;
    }
    if (nameBoxShown()) {
        if (name_->text().trimmed().isEmpty()) return catalog::translate(catalog::key::StampsDialogNeedName);
    } else {
        for (int row = 0; row < names_->rowCount(); ++row) {
            const QTableWidgetItem* name = names_->item(row, 1);
            if (name == nullptr || name->text().trimmed().isEmpty()) return catalog::translate(catalog::key::StampsDialogNeedNames);
        }
    }
    const QString category = category_->currentText().trimmed();
    if (category.isEmpty() || (category.startsWith(QLatin1Char(':')) && category != QLatin1String(core::ProfileStore::kSignaturesCategory)))
        return catalog::translate(catalog::key::StampsDialogNeedCategory);
    // A heading's name TYPED is refused; Signatures chosen from the list is the reserved category (I-42).
    const int index = category_->currentIndex();
    const bool picked = index >= 0 && category_->itemText(index) == category_->currentText();
    if (!picked && StampCategoryDialog::isReservedName(category))
        return catalog::translateError(QStringLiteral("error.stamps.reservedCategory"), category);
    return QString();
}

QString CustomStampDialog::notice() const
{
    if (source() != Source::ImageFile || images_.empty() || leftOut_.isEmpty()) return QString();
    return catalog::translate(catalog::key::StampsDialogLeftOut).arg(leftOut_.join(QStringLiteral(", ")));
}

void CustomStampDialog::refresh()
{
    if (source() == Source::Typed) {
        const QImage typed = typedPicture();
        typedPreview_->setPixmap(typed.isNull() ? QPixmap()
                                                : QPixmap::fromImage(typed.scaled(320, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    form_->setRowVisible(name_, nameBoxShown());
    const QString why = refusal();
    refusal_->setText(why.isEmpty() ? notice() : why);
    if (QPushButton* ok = buttons_->button(QDialogButtonBox::Ok)) {
        ok->setEnabled(why.isEmpty());
        const bool several = source() == Source::ImageFile && images_.size() >= 2;
        ok->setText(several ? catalog::translate(catalog::key::StampsDialogMakeCount).arg(images_.size()) : okText_);
    }
}

void CustomStampDialog::accept()
{
    if (madeAll().empty()) {
        refresh();
        return;
    }
    QDialog::accept();
}

} // namespace rawpage::ui
