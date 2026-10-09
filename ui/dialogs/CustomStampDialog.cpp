// ui/dialogs/CustomStampDialog -- implementation. Stage 6 T10. See the header for the three sources and why none
// is a PDF.

#include "ui/dialogs/CustomStampDialog.h"

#include "core/settings/ProfileStore.h"
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
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTabWidget>
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

    auto* form = new QFormLayout;
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("stampName"));
    name_->setMaxLength(core::ProfileStore::kMaxNameLength);
    category_ = new QComboBox(this);
    category_->setObjectName(QStringLiteral("stampCategory"));
    category_->setEditable(true);
    category_->addItem(catalog::translate(catalog::key::StampsCategorySignatures),
                       QString::fromLatin1(core::ProfileStore::kSignaturesCategory));
    for (const QString& category : categories) {
        if (category != QLatin1String(core::ProfileStore::kSignaturesCategory)) category_->addItem(category, category);
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

    connect(buttons_, &QDialogButtonBox::accepted, this, &CustomStampDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked, this, [this] { browse(); });
    connect(path_, &QLineEdit::editingFinished, this, [this] {
        if (path_->text() != imageSource_) (void)setImagePath(path_->text());
    });
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
    const QString path = QFileDialog::getOpenFileName(this, catalog::translate(catalog::key::StampsDialogBrowse), QString(),
                                                      imageFilter());
    if (!path.isEmpty()) (void)setImagePath(path);
}

bool CustomStampDialog::setImagePath(const QString& path)
{
    const QString shown = QFileInfo(path).fileName();
    imageBytes_.clear();
    imageSource_ = path;
    imageRefusal_.clear();
    path_->setText(path);
    preview_->clear();
    QFile file(path);
    QByteArray bytes;
    if (file.open(QIODevice::ReadOnly) && file.size() <= kMaxImageFile) bytes = file.readAll();
    const QImage image = QImage::fromData(bytes);
    if (bytes.isEmpty() || image.isNull()) {
        // NOT AN IMAGE -- a PDF among them: refused by name, and nothing will be made (A-78).
        imageRefusal_ = catalog::translateError(QStringLiteral("error.stamps.notAnImage"), shown);
        refresh();
        return false;
    }
    // A PNG or a JPEG as it is -- a JPEG keeps its bytes into the document (A-77); any other image as a PNG of itself.
    imageBytes_ = core::ProfileStore::imageFormatOf(bytes).isEmpty() ? pngOf(image) : bytes;
    preview_->setPixmap(QPixmap::fromImage(image.scaled(320, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    if (name_->text().trimmed().isEmpty()) name_->setText(QFileInfo(path).completeBaseName());
    refresh();
    return true;
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
    if (!refusal().isEmpty()) return std::nullopt;
    Made out;
    out.name = name_->text().trimmed();
    const int index = category_->currentIndex();
    const bool picked = index >= 0 && category_->itemText(index) == category_->currentText();
    out.category = picked ? category_->itemData(index).toString() : category_->currentText().trimmed();
    out.favourite = favourite_->isChecked();
    switch (source()) {
    case Source::ImageFile:
        out.image = imageBytes_;
        out.sourceName = QFileInfo(imageSource_).fileName();
        break;
    case Source::Drawn: out.image = pngOf(pad_->picture()); break;
    case Source::Typed: out.image = pngOf(typedPicture()); break;
    }
    return out;
}

QString CustomStampDialog::refusal() const
{
    switch (source()) {
    case Source::ImageFile:
        if (!imageRefusal_.isEmpty()) return imageRefusal_;
        if (imageBytes_.isEmpty()) return catalog::translate(catalog::key::StampsDialogNeedImage);
        break;
    case Source::Drawn:
        if (pad_->isEmpty()) return catalog::translate(catalog::key::StampsDialogNeedDrawing);
        break;
    case Source::Typed:
        if (typed_->text().trimmed().isEmpty()) return catalog::translate(catalog::key::StampsDialogNeedText);
        break;
    }
    if (name_->text().trimmed().isEmpty()) return catalog::translate(catalog::key::StampsDialogNeedName);
    const QString category = category_->currentText().trimmed();
    if (category.isEmpty() || (category.startsWith(QLatin1Char(':')) && category != QLatin1String(core::ProfileStore::kSignaturesCategory)))
        return catalog::translate(catalog::key::StampsDialogNeedCategory);
    return QString();
}

void CustomStampDialog::refresh()
{
    if (source() == Source::Typed) {
        const QImage typed = typedPicture();
        typedPreview_->setPixmap(typed.isNull() ? QPixmap()
                                                : QPixmap::fromImage(typed.scaled(320, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    const QString why = refusal();
    refusal_->setText(why);
    if (QPushButton* ok = buttons_->button(QDialogButtonBox::Ok)) ok->setEnabled(why.isEmpty());
}

void CustomStampDialog::accept()
{
    if (!made()) {
        refresh();
        return;
    }
    QDialog::accept();
}

} // namespace rawpage::ui
