// ui/dialogs/WatermarkDialog -- see the header.

#include "ui/dialogs/WatermarkDialog.h"

#include "core/doc/PageRanges.h"
#include "core/doc/Stamps.h"
#include "core/settings/ProfileStore.h"
#include "ui/shell/Catalog.h"
#include "ui/viewer/tools/StampTool.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

bool aPersonCanAnswer()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

WatermarkDialog::Answer& installedWatermark()
{
    static WatermarkDialog::Answer answer;
    return answer;
}

WatermarkChooser::Answer& installedChooser()
{
    static WatermarkChooser::Answer answer;
    return answer;
}

constexpr core::WatermarkAnchor kAnchors[] = {core::WatermarkAnchor::TopLeft,    core::WatermarkAnchor::Top,
                                              core::WatermarkAnchor::TopRight,   core::WatermarkAnchor::Left,
                                              core::WatermarkAnchor::Centre,     core::WatermarkAnchor::Right,
                                              core::WatermarkAnchor::BottomLeft, core::WatermarkAnchor::Bottom,
                                              core::WatermarkAnchor::BottomRight};
constexpr core::WatermarkUsage kUsages[] = {core::WatermarkUsage::Always, core::WatermarkUsage::PrintOnly,
                                            core::WatermarkUsage::ScreenOnly};

QDoubleSpinBox* numberField(QWidget* parent, const QString& name, double low, double high, double step, int decimals = 1)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setObjectName(name);
    field->setRange(low, high);
    field->setDecimals(decimals);
    field->setSingleStep(step);
    return field;
}

QString translateFamily(const char* family, QLatin1String name)
{
    return catalog::translate(QLatin1String(family) + name);
}

// A page set as a person writes it: runs of consecutive pages, counted from 1.
QString pagesText(const std::vector<int>& pages)
{
    QStringList runs;
    for (std::size_t i = 0; i < pages.size();) {
        std::size_t j = i;
        while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1) ++j;
        runs << (i == j ? QString::number(pages[i] + 1) : QStringLiteral("%1-%2").arg(pages[i] + 1).arg(pages[j] + 1));
        i = j + 1;
    }
    return runs.join(QStringLiteral(", "));
}

} // namespace

// ==========================================================================================
// The preview
// ==========================================================================================

WatermarkPreview::WatermarkPreview(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("watermarkPreview"));
    setMinimumSize(180, 220);
}

void WatermarkPreview::show(core::PdfSize shown, std::optional<core::PageDecoration> decoration, QImage picture)
{
    shown_ = shown;
    decoration_ = std::move(decoration);
    picture_ = std::move(picture);
    update();
}

void WatermarkPreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF area = QRectF(rect()).adjusted(6, 6, -6, -6);
    if (shown_.width <= 0 || shown_.height <= 0 || area.isEmpty()) return;
    const double scale = std::min(area.width() / shown_.width, area.height() / shown_.height);
    const QRectF page(area.center().x() - shown_.width * scale / 2, area.center().y() - shown_.height * scale / 2,
                      shown_.width * scale, shown_.height * scale);
    painter.fillRect(page, palette().color(QPalette::Base));
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawRect(page);
    if (!decoration_ || !decoration_->watermark) return;
    const core::WatermarkDrawing& w = *decoration_->watermark;
    painter.setClipRect(page);
    // Shown space -> the widget: y up becomes y down.
    const QTransform toWidget(scale, 0, 0, -scale, page.left(), page.bottom());
    painter.setOpacity(w.opacity);
    for (const std::array<double, 6>& m : w.tiles) {
        const QTransform tile(m[0], m[1], m[2], m[3], m[4], m[5]);
        painter.setTransform(tile * toWidget);
        const QRectF box(0, 0, w.natural.width, w.natural.height);
        switch (w.source) {
        case core::WatermarkSource::Text:
            for (const core::DecorationLine& line : w.lines) {
                const QColor colour = QColor::fromRgbF(static_cast<float>(line.colour[0]), static_cast<float>(line.colour[1]),
                                                       static_cast<float>(line.colour[2]));
                painter.setPen(colour);
                QFont font(QStringLiteral("Helvetica"));
                font.setStyleHint(QFont::SansSerif);
                font.setPixelSize(std::max(1, static_cast<int>(std::lround(line.size))));
                painter.setFont(font);
                double x = line.matrix[4];
                for (const core::TypedRun& run : line.runs) {
                    painter.save();
                    painter.translate(x, line.matrix[5]);
                    painter.scale(1, -1);   // text reads upright in a y-up space
                    painter.drawText(QPointF(0, 0), run.text);
                    painter.restore();
                    x += run.width;   // the page's own advances
                }
            }
            break;
        case core::WatermarkSource::Image:
            if (!picture_.isNull()) {
                painter.save();
                painter.translate(0, w.natural.height);
                painter.scale(1, -1);
                painter.drawImage(box, picture_);
                painter.restore();
                break;
            }
            [[fallthrough]];
        case core::WatermarkSource::Page: {
            QPen frame(palette().color(QPalette::Text), 0);
            painter.setPen(frame);
            painter.setBrush(QColor(128, 128, 128, 90));
            painter.drawRect(box);
            painter.drawLine(box.topLeft(), box.bottomRight());
            painter.drawLine(box.bottomLeft(), box.topRight());
            painter.setBrush(Qt::NoBrush);
            break;
        }
        }
    }
}

// ==========================================================================================
// WatermarkDialog
// ==========================================================================================

WatermarkDialog::WatermarkDialog(Mode mode, const core::PageTree& pages, const core::WatermarkSpec& initial,
                                 core::ProfileStore* profiles, const QString& profile, PageReader reader, QWidget* parent)
    : QDialog(parent), pages_(pages), profiles_(profiles), reader_(std::move(reader)), han_(initial.han)
{
    setObjectName(QStringLiteral("watermarkDialog"));
    setWindowTitle(catalog::translate(mode == Mode::Add ? catalog::key::DecorateWatermarkAddTitle : catalog::key::DecorateWatermarkUpdateTitle));
    setModal(true);
    auto* columns = new QHBoxLayout(this);
    columns->setContentsMargins(16, 16, 16, 16);
    columns->setSpacing(16);
    auto* column = new QVBoxLayout();
    column->setSpacing(10);
    columns->addLayout(column, 3);

    // --- Profiles ---
    auto* profileRow = new QHBoxLayout();
    profileRow->addWidget(new QLabel(catalog::translate(catalog::key::DecorateProfile), this));
    profile_ = new QComboBox(this);
    profile_->setObjectName(QStringLiteral("profileBox"));
    profileRow->addWidget(profile_, 1);
    profileName_ = new QLineEdit(this);
    profileName_->setObjectName(QStringLiteral("profileNameField"));
    profileName_->setPlaceholderText(catalog::translate(catalog::key::DecorateProfileName));
    profileName_->setMaxLength(core::ProfileStore::kMaxNameLength);
    profileRow->addWidget(profileName_, 1);
    saveProfile_ = new QPushButton(catalog::translate(catalog::key::DecorateProfileSave), this);
    saveProfile_->setObjectName(QStringLiteral("saveProfileButton"));
    profileRow->addWidget(saveProfile_);
    deleteProfile_ = new QPushButton(catalog::translate(catalog::key::DecorateProfileDelete), this);
    deleteProfile_->setObjectName(QStringLiteral("deleteProfileButton"));
    profileRow->addWidget(deleteProfile_);
    column->addLayout(profileRow);
    for (QWidget* w : std::initializer_list<QWidget*>{profile_, profileName_, saveProfile_, deleteProfile_}) w->setEnabled(profiles_ != nullptr);

    // --- The source ---
    auto* sourceBox = new QGroupBox(catalog::translate(catalog::key::DecorateWatermarkSource), this);
    auto* sourceGrid = new QGridLayout(sourceBox);
    text_ = new QRadioButton(catalog::translate(catalog::key::DecorateWatermarkText), sourceBox);
    text_->setObjectName(QStringLiteral("textButton"));
    image_ = new QRadioButton(catalog::translate(catalog::key::DecorateWatermarkImage), sourceBox);
    image_->setObjectName(QStringLiteral("imageButton"));
    page_ = new QRadioButton(catalog::translate(catalog::key::DecorateWatermarkPage), sourceBox);
    page_->setObjectName(QStringLiteral("pageButton"));
    auto* group = new QButtonGroup(this);
    for (QRadioButton* b : {text_, image_, page_}) group->addButton(b);
    textField_ = new QLineEdit(sourceBox);
    textField_->setObjectName(QStringLiteral("textField"));
    textField_->setMaxLength(core::kMaxWatermarkTextLength);
    size_ = numberField(sourceBox, QStringLiteral("sizeField"), core::kMinWatermarkTextSize, core::kMaxWatermarkTextSize, 1.0);
    colourButton_ = new QPushButton(catalog::translate(catalog::key::DecorateColour), sourceBox);
    colourButton_->setObjectName(QStringLiteral("colourButton"));
    sourceGrid->addWidget(text_, 0, 0);
    sourceGrid->addWidget(textField_, 0, 1, 1, 2);
    sourceGrid->addWidget(new QLabel(catalog::translate(catalog::key::DecorateSize), sourceBox), 1, 1);
    auto* styleRow = new QHBoxLayout();
    styleRow->addWidget(size_);
    styleRow->addWidget(colourButton_);
    styleRow->addStretch(1);
    sourceGrid->addLayout(styleRow, 1, 2);
    chooseImage_ = new QPushButton(catalog::translate(catalog::key::DecorateWatermarkChoose), sourceBox);
    chooseImage_->setObjectName(QStringLiteral("chooseImageButton"));
    sourceGrid->addWidget(image_, 2, 0);
    sourceGrid->addWidget(chooseImage_, 2, 1);
    choosePage_ = new QPushButton(catalog::translate(catalog::key::DecorateWatermarkChoose), sourceBox);
    choosePage_->setObjectName(QStringLiteral("choosePageButton"));
    pageNumber_ = new QSpinBox(sourceBox);
    pageNumber_->setObjectName(QStringLiteral("pageNumberField"));
    pageNumber_->setRange(1, 100000);
    auto* pageRowLayout = new QHBoxLayout();
    pageRowLayout->addWidget(choosePage_);
    pageRowLayout->addWidget(new QLabel(catalog::translate(catalog::key::DecorateWatermarkPageNumber), sourceBox));
    pageRowLayout->addWidget(pageNumber_);
    sourceGrid->addWidget(page_, 3, 0);
    sourceGrid->addLayout(pageRowLayout, 3, 1, 1, 2);
    sourceLabel_ = new QLabel(sourceBox);
    sourceLabel_->setObjectName(QStringLiteral("sourceLabel"));
    sourceLabel_->setWordWrap(true);
    sourceGrid->addWidget(sourceLabel_, 4, 0, 1, 3);
    page_->setEnabled(static_cast<bool>(reader_));
    column->addWidget(sourceBox);

    // --- Appearance and place ---
    auto* placeBox = new QGroupBox(catalog::translate(catalog::key::DecorateWatermarkPlacement), this);
    auto* place = new QGridLayout(placeBox);
    opacity_ = numberField(placeBox, QStringLiteral("opacityField"), core::kMinWatermarkOpacity * 100.0, 100.0, 5.0, 0);
    opacity_->setSuffix(QStringLiteral(" %"));
    rotation_ = numberField(placeBox, QStringLiteral("rotationField"), -core::kMaxWatermarkRotation, core::kMaxWatermarkRotation, 15.0, 1);
    rotation_->setSuffix(QStringLiteral(" °"));
    scaleMode_ = new QComboBox(placeBox);
    scaleMode_->setObjectName(QStringLiteral("scaleModeBox"));
    scaleMode_->addItem(catalog::translate(QStringLiteral("decorate.watermark.scaleMode.relative")), static_cast<int>(core::WatermarkScale::Relative));
    scaleMode_->addItem(catalog::translate(QStringLiteral("decorate.watermark.scaleMode.absolute")), static_cast<int>(core::WatermarkScale::Absolute));
    scale_ = numberField(placeBox, QStringLiteral("scaleField"), core::kMinWatermarkScale * 100.0, core::kMaxWatermarkAbsoluteScale * 100.0, 5.0, 0);
    scale_->setSuffix(QStringLiteral(" %"));
    anchor_ = new QComboBox(placeBox);
    anchor_->setObjectName(QStringLiteral("anchorBox"));
    for (const core::WatermarkAnchor a : kAnchors)
        anchor_->addItem(translateFamily("decorate.watermark.anchor.", core::watermarkAnchorName(a)), static_cast<int>(a));
    offsetX_ = numberField(placeBox, QStringLiteral("offsetXField"), -core::kMaxWatermarkOffset, core::kMaxWatermarkOffset, 6.0);
    offsetY_ = numberField(placeBox, QStringLiteral("offsetYField"), -core::kMaxWatermarkOffset, core::kMaxWatermarkOffset, 6.0);
    tile_ = new QCheckBox(catalog::translate(catalog::key::DecorateWatermarkTile), placeBox);
    tile_->setObjectName(QStringLiteral("tileBox"));
    tileGap_ = numberField(placeBox, QStringLiteral("tileGapField"), 0.0, core::kMaxWatermarkTileGap, 6.0);
    side_ = new QComboBox(placeBox);
    side_->setObjectName(QStringLiteral("sideBox"));
    side_->addItem(catalog::translate(QStringLiteral("decorate.watermark.side.behind")), true);
    side_->addItem(catalog::translate(QStringLiteral("decorate.watermark.side.front")), false);
    usage_ = new QComboBox(placeBox);
    usage_->setObjectName(QStringLiteral("usageBox"));
    for (const core::WatermarkUsage u : kUsages)
        usage_->addItem(translateFamily("decorate.watermark.usage.", core::watermarkUsageName(u)), static_cast<int>(u));
    int row = 0;
    place->addWidget(new QLabel(catalog::translate(catalog::key::DecorateWatermarkOpacity), placeBox), row, 0);
    place->addWidget(opacity_, row, 1);
    place->addWidget(new QLabel(catalog::translate(catalog::key::DecorateWatermarkRotation), placeBox), row, 2);
    place->addWidget(rotation_, row++, 3);
    place->addWidget(new QLabel(catalog::translate(catalog::key::DecorateWatermarkScale), placeBox), row, 0);
    place->addWidget(scale_, row, 1);
    place->addWidget(scaleMode_, row++, 2, 1, 2);
    place->addWidget(new QLabel(catalog::translate(catalog::key::DecorateWatermarkAnchor), placeBox), row, 0);
    place->addWidget(anchor_, row++, 1, 1, 3);
    place->addWidget(new QLabel(catalog::translate(catalog::key::DecorateWatermarkOffset), placeBox), row, 0);
    place->addWidget(offsetX_, row, 1);
    place->addWidget(offsetY_, row++, 2);
    place->addWidget(tile_, row, 0, 1, 2);
    place->addWidget(tileGap_, row++, 2);
    place->addWidget(new QLabel(catalog::translate(catalog::key::DecorateWatermarkSide), placeBox), row, 0);
    place->addWidget(side_, row++, 1, 1, 3);
    place->addWidget(new QLabel(catalog::translate(catalog::key::DecorateWatermarkShow), placeBox), row, 0);
    place->addWidget(usage_, row++, 1, 1, 3);
    column->addWidget(placeBox);

    // --- Pages ---
    auto* pagesRow = new QHBoxLayout();
    pagesRow->addWidget(new QLabel(catalog::translate(catalog::key::DecoratePages), this));
    range_ = new QLineEdit(this);
    range_->setObjectName(QStringLiteral("rangeField"));
    range_->setPlaceholderText(QStringLiteral("1-%1").arg(pages_.count()));
    pagesRow->addWidget(range_, 1);
    parity_ = new QComboBox(this);
    parity_->setObjectName(QStringLiteral("parityBox"));
    for (const core::PageParity parity : {core::PageParity::Any, core::PageParity::Odd, core::PageParity::Even})
        parity_->addItem(translateFamily("decorate.parity.", core::pageParityName(parity)), static_cast<int>(parity));
    pagesRow->addWidget(parity_);
    column->addLayout(pagesRow);

    refusal_ = new QLabel(this);
    refusal_->setObjectName(QStringLiteral("refusal"));
    refusal_->setWordWrap(true);
    refusal_->hide();
    column->addWidget(refusal_);
    column->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);

    // --- The preview ---
    auto* previewColumn = new QVBoxLayout();
    previewColumn->addWidget(new QLabel(catalog::translate(catalog::key::DecoratePreview), this));
    preview_ = new WatermarkPreview(this);
    previewColumn->addWidget(preview_, 1);
    auto* pageRow = new QHBoxLayout();
    pageRow->addWidget(new QLabel(catalog::translate(catalog::key::DecoratePreviewPage), this));
    previewPage_ = new QSpinBox(this);
    previewPage_->setObjectName(QStringLiteral("previewPageField"));
    previewPage_->setRange(1, std::max(1, pages_.count()));
    pageRow->addWidget(previewPage_);
    pageRow->addStretch(1);
    previewColumn->addLayout(pageRow);
    columns->addLayout(previewColumn, 2);

    // Every field redraws the preview.
    for (QDoubleSpinBox* field : {size_, opacity_, rotation_, scale_, offsetX_, offsetY_, tileGap_})
        connect(field, &QDoubleSpinBox::valueChanged, this, &WatermarkDialog::refreshPreview);
    for (QComboBox* box : {scaleMode_, anchor_, side_, usage_, parity_})
        connect(box, &QComboBox::currentIndexChanged, this, &WatermarkDialog::refreshPreview);
    connect(tile_, &QCheckBox::toggled, this, &WatermarkDialog::refreshPreview);
    connect(textField_, &QLineEdit::textChanged, this, &WatermarkDialog::refreshPreview);
    connect(range_, &QLineEdit::textChanged, this, &WatermarkDialog::refreshPreview);
    connect(previewPage_, &QSpinBox::valueChanged, this, &WatermarkDialog::refreshPreview);
    for (QRadioButton* b : {text_, image_, page_}) {
        connect(b, &QRadioButton::toggled, this, [this](bool on) {
            if (on) refreshSource();
        });
    }
    connect(colourButton_, &QPushButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(colour_, this);
        if (chosen.isValid()) setColour(chosen);
    });
    connect(chooseImage_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, catalog::translate(catalog::key::DecorateWatermarkImage), QString(),
                                                          catalog::translate(catalog::key::DecorateWatermarkImageFilter));
        if (!path.isEmpty()) (void)setImageFile(path);
    });
    connect(choosePage_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, catalog::translate(catalog::key::DecorateWatermarkPage), QString(),
                                                          catalog::translate(catalog::key::DecorateWatermarkPdfFilter));
        if (!path.isEmpty()) (void)setPageFile(path, pageNumber_->value() - 1);
    });
    connect(pageNumber_, &QSpinBox::editingFinished, this, [this] {
        if (!pageName_.isEmpty() && reader_) (void)setPageFile(pageName_, pageNumber_->value() - 1);
    });

    // Profiles: chosen loads; save keeps the fields under the name typed; delete takes the chosen one away.
    connect(profile_, &QComboBox::activated, this, [this](int index) {
        const QString name = profile_->itemData(index).toString();
        if (!name.isEmpty()) loadProfile(name);
    });
    connect(profile_, &QComboBox::currentIndexChanged, this, [this](int index) { deleteProfile_->setEnabled(profiles_ != nullptr && index > 0); });
    connect(saveProfile_, &QPushButton::clicked, this, [this] {
        if (profiles_ == nullptr) return;
        const core::WatermarkSpec spec = specFromFields();
        if (!spec.keepSourceOf.isEmpty()) {
            showRefusal(core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.decorate.profileKeepsPicture"), QString()});
            return;
        }
        const QString name = profileName_->text().trimmed();
        if (const core::Status saved = profiles_->saveProfile(core::ProfileKind::Watermark, name, core::watermarkToJson(spec, true)); !saved) {
            showRefusal(saved.error());
            return;
        }
        reloadProfiles(name);
    });
    connect(deleteProfile_, &QPushButton::clicked, this, [this] {
        const QString name = profile_->currentData().toString();
        if (profiles_ == nullptr || name.isEmpty()) return;
        if (profiles_->removeProfile(core::ProfileKind::Watermark, name)) reloadProfiles(QString());
    });

    loadSpec(initial);
    reloadProfiles(profile);
    refreshPreview();
}

void WatermarkDialog::setColour(const QColor& colour)
{
    colour_ = colour.isValid() ? colour.toRgb() : QColor(Qt::gray);
    QPixmap swatch(12, 12);
    swatch.fill(colour_);
    colourButton_->setIcon(QIcon(swatch));
    refreshPreview();
}

void WatermarkDialog::showRefusal(const core::Error& error)
{
    refusal_->setText(catalog::translateError(QString(error.messageKey), error.context));
    refusal_->show();
}

core::Status WatermarkDialog::setImageFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > core::kMaxStampImageBytes) {
        const core::Error error{core::ErrorCode::IoFailure, QLatin1String("error.stamps.notAnImage"), QFileInfo(path).fileName()};
        showRefusal(error);
        return error;
    }
    const QByteArray bytes = file.readAll();
    core::Result<core::StampImage> decoded = decodeStampImage(bytes, QFileInfo(path).fileName());
    if (!decoded) {
        showRefusal(decoded.error());
        return decoded.error();
    }
    imageSource_ = std::move(decoded).value();
    imagePreview_ = QImage::fromData(bytes);
    imageName_ = QFileInfo(path).fileName();
    keepSourceOf_.clear();   // a picture chosen: no longer the one kept
    image_->setChecked(true);
    refreshSource();
    return core::Status{};
}

core::Status WatermarkDialog::setPageFile(const QString& path, int page)
{
    if (!reader_) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()};
    core::Result<core::StampPage> read = reader_(path, page);
    if (!read) {
        showRefusal(read.error());
        return read.error();
    }
    pageSource_ = std::move(read).value();
    pageName_ = path;
    pageNumber_->setValue(page + 1);
    keepSourceOf_.clear();
    page_->setChecked(true);
    refreshSource();
    return core::Status{};
}

void WatermarkDialog::refreshSource()
{
    const bool text = text_->isChecked();
    textField_->setEnabled(text);
    size_->setEnabled(text);
    colourButton_->setEnabled(text);
    QString shown;
    if (image_->isChecked()) {
        if (!imageSource_.isEmpty()) shown = imageName_;
        else if (!keepSourceOf_.isEmpty() && keptKind_ == core::WatermarkSource::Image) shown = catalog::translate(catalog::key::DecorateWatermarkKept).arg(keptName_);
    } else if (page_->isChecked()) {
        if (!pageSource_.isEmpty()) shown = QStringLiteral("%1 (%2)").arg(QFileInfo(pageName_).fileName()).arg(pageNumber_->value());
        else if (!keepSourceOf_.isEmpty() && keptKind_ == core::WatermarkSource::Page) shown = catalog::translate(catalog::key::DecorateWatermarkKept).arg(keptName_);
    }
    if (!text && shown.isEmpty()) shown = catalog::translate(catalog::key::DecorateWatermarkNoFile);
    sourceLabel_->setText(text ? QString() : shown);
    refreshPreview();
}

void WatermarkDialog::loadSpec(const core::WatermarkSpec& spec)
{
    loading_ = true;
    han_ = spec.han;
    textField_->setText(spec.text);
    size_->setValue(spec.size);
    opacity_->setValue(spec.opacity * 100.0);
    rotation_->setValue(spec.rotation);
    scaleMode_->setCurrentIndex(std::max(0, scaleMode_->findData(static_cast<int>(spec.scaleMode))));
    scale_->setValue(spec.scale * 100.0);
    anchor_->setCurrentIndex(std::max(0, anchor_->findData(static_cast<int>(spec.anchor))));
    offsetX_->setValue(spec.offsetX);
    offsetY_->setValue(spec.offsetY);
    tile_->setChecked(spec.tile);
    tileGap_->setValue(spec.tileGap);
    side_->setCurrentIndex(spec.behind ? 0 : 1);
    usage_->setCurrentIndex(std::max(0, usage_->findData(static_cast<int>(spec.usage))));
    range_->setText(spec.range);
    parity_->setCurrentIndex(std::max(0, parity_->findData(static_cast<int>(spec.parity))));
    keepSourceOf_ = spec.keepSourceOf;
    keptSize_ = spec.sourceSize;
    keptName_ = spec.sourceName;
    keptKind_ = spec.source;
    if (spec.source == core::WatermarkSource::Image && !spec.image.isEmpty()) {
        imageSource_ = spec.image;
        imageName_ = spec.sourceName;
        imagePreview_ = spec.image.encoding == core::StampImage::Encoding::Jpeg ? QImage::fromData(spec.image.data) : QImage();
    }
    if (spec.source == core::WatermarkSource::Page && !spec.page.isEmpty()) {
        pageSource_ = spec.page;
        pageName_ = spec.sourceName;
    }
    (spec.source == core::WatermarkSource::Text ? text_ : spec.source == core::WatermarkSource::Image ? image_ : page_)->setChecked(true);
    loading_ = false;
    setColour(QColor::fromRgbF(static_cast<float>(spec.colour[0]), static_cast<float>(spec.colour[1]), static_cast<float>(spec.colour[2])));
    refreshSource();
}

core::WatermarkSpec WatermarkDialog::specFromFields() const
{
    core::WatermarkSpec spec;
    spec.source = text_->isChecked() ? core::WatermarkSource::Text
                  : image_->isChecked() ? core::WatermarkSource::Image : core::WatermarkSource::Page;
    spec.text = textField_->text();
    spec.size = size_->value();
    spec.colour = {colour_.redF(), colour_.greenF(), colour_.blueF()};
    spec.han = han_;
    if (spec.source == core::WatermarkSource::Image) {
        if (!imageSource_.isEmpty()) {
            spec.image = imageSource_;
            spec.sourceName = imageName_;
        } else if (!keepSourceOf_.isEmpty() && keptKind_ == core::WatermarkSource::Image) {
            spec.keepSourceOf = keepSourceOf_;
            spec.sourceSize = keptSize_;
            spec.sourceName = keptName_;
        }
    } else if (spec.source == core::WatermarkSource::Page) {
        if (!pageSource_.isEmpty()) {
            spec.page = pageSource_;
            spec.sourceName = QFileInfo(pageName_).fileName();
        } else if (!keepSourceOf_.isEmpty() && keptKind_ == core::WatermarkSource::Page) {
            spec.keepSourceOf = keepSourceOf_;
            spec.sourceSize = keptSize_;
            spec.sourceName = keptName_;
        }
    }
    spec.opacity = opacity_->value() / 100.0;
    spec.rotation = rotation_->value();
    spec.scaleMode = static_cast<core::WatermarkScale>(scaleMode_->currentData().toInt());
    spec.scale = scale_->value() / 100.0;
    spec.anchor = static_cast<core::WatermarkAnchor>(anchor_->currentData().toInt());
    spec.offsetX = offsetX_->value();
    spec.offsetY = offsetY_->value();
    spec.tile = tile_->isChecked();
    spec.tileGap = tileGap_->value();
    spec.behind = side_->currentData().toBool();
    spec.usage = static_cast<core::WatermarkUsage>(usage_->currentData().toInt());
    spec.range = range_->text().trimmed();
    spec.parity = static_cast<core::PageParity>(parity_->currentData().toInt());
    return spec;
}

core::Result<WatermarkDialog::Choice> WatermarkDialog::choice() const
{
    const core::WatermarkSpec spec = specFromFields();
    if (core::Status valid = core::validate(spec); !valid) return valid.error();
    core::Result<std::vector<int>> at = core::watermarkPages(spec, pages_);
    if (!at) return at.error();
    // Every page it reaches laid out -- too many tiles on one of them is refused HERE, with the dialog still open.
    for (const int page : at.value()) {
        if (auto laid = core::layoutWatermark(spec, *pages_.at(page)); !laid) return laid.error();
    }
    return Choice{spec, profile_->currentData().toString()};
}

void WatermarkDialog::refreshPreview()
{
    if (loading_ || preview_ == nullptr) return;
    const core::WatermarkSpec spec = specFromFields();
    const int index = std::clamp(previewPage_->value() - 1, 0, std::max(0, pages_.count() - 1));
    const core::Page* page = pages_.at(index);
    if (page == nullptr) return;
    // THE PAGE AS SHOWN, unturned: the layout's tiles are then in the preview's own coordinates.
    const core::Page shown(0, core::PdfRect{core::PdfPoint{0, 0}, page->displaySize()});
    core::Result<std::vector<int>> at = core::watermarkPages(spec, pages_);
    auto laid = core::layoutWatermark(spec, shown);
    if (!laid) {
        showRefusal(laid.error());
        preview_->show(shown.displaySize(), std::nullopt, QImage());
        return;
    }
    if (!at) {
        showRefusal(at.error());
    } else {
        refusal_->hide();
    }
    const bool reached = at && std::find(at.value().begin(), at.value().end(), index) != at.value().end();
    // A page the watermark does not reach is previewed bare: what it will look like.
    preview_->show(shown.displaySize(), reached ? std::optional<core::PageDecoration>(std::move(laid).value()) : std::nullopt,
                   spec.source == core::WatermarkSource::Image ? imagePreview_ : QImage());
}

void WatermarkDialog::reloadProfiles(const QString& select)
{
    profile_->clear();
    profile_->addItem(catalog::translate(catalog::key::DecorateProfileNone), QString());
    if (profiles_ == nullptr) return;
    for (const QString& name : profiles_->profileNames(core::ProfileKind::Watermark)) profile_->addItem(name, name);
    const int found = profile_->findData(select);
    profile_->setCurrentIndex(found < 0 ? 0 : found);
    deleteProfile_->setEnabled(found > 0);
}

void WatermarkDialog::loadProfile(const QString& name)
{
    if (profiles_ == nullptr) return;
    core::Result<QJsonObject> json = profiles_->profile(core::ProfileKind::Watermark, name);
    core::Result<core::WatermarkSpec> spec = json ? core::watermarkFromJson(json.value(), true) : core::Result<core::WatermarkSpec>(json.error());
    if (!spec) {
        showRefusal(spec.error());
        return;
    }
    core::WatermarkSpec loaded = std::move(spec).value();
    loaded.han = han_;   // the person's language, not the profile maker's
    loadSpec(loaded);
    profileName_->setText(name);
}

void WatermarkDialog::accept()
{
    core::Result<Choice> chosen = choice();
    if (!chosen) {
        showRefusal(chosen.error());
        return;
    }
    QDialog::accept();
}

bool WatermarkDialog::canAsk()
{
    return static_cast<bool>(installedWatermark()) || aPersonCanAnswer();
}

std::optional<WatermarkDialog::Choice> WatermarkDialog::ask(QWidget* parent, Mode mode, const core::PageTree& pages,
                                                            const core::WatermarkSpec& initial, core::ProfileStore* profiles,
                                                            const QString& profile, PageReader reader)
{
    if (installedWatermark()) return installedWatermark()(mode, initial);
    if (!aPersonCanAnswer()) return std::nullopt;
    WatermarkDialog dialog(mode, pages, initial, profiles, profile, std::move(reader), parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<Choice> chosen = dialog.choice();
    if (!chosen) return std::nullopt;
    return std::move(chosen).value();
}

void WatermarkDialog::setAnswerForTesting(Answer answer)
{
    installedWatermark() = std::move(answer);
}

// ==========================================================================================
// WatermarkChooser
// ==========================================================================================

QString describeWatermark(const core::DecorationEntry& entry)
{
    core::Result<core::WatermarkSpec> spec = core::watermarkFromJson(entry.spec, false);
    QString what;
    if (!spec) {
        what = entry.id.left(8);
    } else {
        const core::WatermarkSpec& s = spec.value();
        QString name = s.source == core::WatermarkSource::Text ? s.text.simplified() : s.sourceName;
        if (name.size() > 40) name = name.left(39) + QChar(0x2026);
        what = translateFamily("decorate.watermark.kind.", core::watermarkSourceName(s.source)).arg(name);
        what += QStringLiteral(" — ") + translateFamily("decorate.watermark.side.", QLatin1String(s.behind ? "behind" : "front"));
        if (s.usage != core::WatermarkUsage::Always)
            what += QStringLiteral(", ") + translateFamily("decorate.watermark.usage.", core::watermarkUsageName(s.usage));
    }
    return catalog::translate(catalog::key::DecorateWatermarkEntry).arg(what, pagesText(entry.pages));
}

WatermarkChooser::WatermarkChooser(Purpose purpose, std::vector<core::DecorationEntry> entries, QWidget* parent)
    : QDialog(parent), entries_(std::move(entries))
{
    setObjectName(QStringLiteral("watermarkChooser"));
    setWindowTitle(catalog::translate(catalog::key::DecorateWatermarkChooserTitle));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(catalog::translate(purpose == Purpose::Update ? catalog::key::DecorateWatermarkChooseUpdate
                                                                               : catalog::key::DecorateWatermarkChooseRemove),
                                 this));
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("watermarkList"));
    list_->setSelectionMode(purpose == Purpose::Update ? QAbstractItemView::SingleSelection : QAbstractItemView::ExtendedSelection);
    for (const core::DecorationEntry& entry : entries_) list_->addItem(describeWatermark(entry));
    if (list_->count() > 0) list_->setCurrentRow(0);
    layout->addWidget(list_, 1);
    refusal_ = new QLabel(this);
    refusal_->setObjectName(QStringLiteral("refusal"));
    refusal_->hide();
    layout->addWidget(refusal_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (purpose == Purpose::Remove) {
        QPushButton* all = buttons->addButton(catalog::translate(catalog::key::DecorateWatermarkAll), QDialogButtonBox::ActionRole);
        all->setObjectName(QStringLiteral("allButton"));
        connect(all, &QPushButton::clicked, this, [this] { list_->selectAll(); });
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

core::Result<std::vector<QString>> WatermarkChooser::chosen() const
{
    std::vector<QString> out;
    for (int row = 0; row < list_->count(); ++row) {
        if (list_->item(row)->isSelected()) out.push_back(entries_[static_cast<std::size_t>(row)].id);
    }
    if (out.empty()) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.decorate.noneChosen")};
    return out;
}

void WatermarkChooser::accept()
{
    if (core::Result<std::vector<QString>> ids = chosen(); !ids) {
        refusal_->setText(catalog::translateError(QString(ids.error().messageKey), ids.error().context));
        refusal_->show();
        return;
    }
    QDialog::accept();
}

bool WatermarkChooser::canAsk()
{
    return static_cast<bool>(installedChooser()) || aPersonCanAnswer();
}

std::optional<std::vector<QString>> WatermarkChooser::ask(QWidget* parent, Purpose purpose, const std::vector<core::DecorationEntry>& entries)
{
    if (installedChooser()) return installedChooser()(purpose, entries);
    if (!aPersonCanAnswer()) return std::nullopt;
    WatermarkChooser dialog(purpose, entries, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    core::Result<std::vector<QString>> ids = dialog.chosen();
    if (!ids) return std::nullopt;
    return std::move(ids).value();
}

void WatermarkChooser::setAnswerForTesting(Answer answer)
{
    installedChooser() = std::move(answer);
}

} // namespace rawpage::ui
