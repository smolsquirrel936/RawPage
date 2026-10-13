// ui/viewer/PropertiesBar -- implementation. Stage 6 T5; T8 the drawing kinds' properties. See the header for its
// two subjects and why it changes nothing itself.

#include "ui/viewer/PropertiesBar.h"

#include "core/doc/AnnotationGeometry.h"
#include "core/doc/DrawnShapes.h"
#include "core/doc/Stamps.h"
#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QSpinBox>

#include <algorithm>
#include <array>
#include <cmath>

namespace rawpage::ui {
namespace {

using core::AnnotationSubtype;

QColor toQColor(const std::vector<double>& colour)
{
    const auto component = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    switch (colour.size()) {
    case 1: return QColor(component(colour[0]), component(colour[0]), component(colour[0]));
    case 3: return QColor(component(colour[0]), component(colour[1]), component(colour[2]));
    case 4: {   // CMYK, naively: enough for a swatch
        const double k = colour[3];
        return QColor(component((1 - colour[0]) * (1 - k)), component((1 - colour[1]) * (1 - k)),
                      component((1 - colour[2]) * (1 - k)));
    }
    default: return QColor(Qt::transparent);
    }
}

QIcon swatchIcon(const std::vector<double>& colour)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(toQColor(colour));
    return QIcon(pixmap);
}

bool sameColour(const std::vector<double>& a, const std::vector<double>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) > 1e-3) return false;
    }
    return true;
}

// Whether a kind is drawn with an outline whose width -- and so whose line style -- means something.
bool hasBorder(AnnotationSubtype subtype)
{
    switch (subtype) {
    case AnnotationSubtype::Square:
    case AnnotationSubtype::Circle:
    case AnnotationSubtype::Line:
    case AnnotationSubtype::Polygon:
    case AnnotationSubtype::PolyLine:
    case AnnotationSubtype::Ink:
    case AnnotationSubtype::FreeText:
        return true;
    default:
        return false;
    }
}

// Whether a kind has an inside a fill colours -- or, for a line and a polyline, closed endings it fills.
bool hasFill(AnnotationSubtype subtype)
{
    switch (subtype) {
    case AnnotationSubtype::Square:
    case AnnotationSubtype::Circle:
    case AnnotationSubtype::Polygon:
    case AnnotationSubtype::Line:
    case AnnotationSubtype::PolyLine:
    case AnnotationSubtype::FreeText:
        return true;
    default:
        return false;
    }
}

bool isCallout(const core::Annotation& a)
{
    return a.subtype == AnnotationSubtype::FreeText && core::freeTextKind(a) == core::FreeTextKind::Callout;
}

QString token(std::string_view name)
{
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

constexpr std::array<core::LineStyle, 3> kLineStyles = {core::LineStyle::Solid, core::LineStyle::Dashed,
                                                        core::LineStyle::Dotted};
constexpr std::array<core::ToolBlend, 8> kBlends = {
    core::ToolBlend::Normal,  core::ToolBlend::Multiply, core::ToolBlend::Screen,     core::ToolBlend::Overlay,
    core::ToolBlend::Darken,  core::ToolBlend::Lighten,  core::ToolBlend::Difference, core::ToolBlend::Exclusion};

} // namespace

const std::vector<std::vector<double>>& PropertiesBar::swatches()
{
    // The reviewer's colours: the highlighter's yellow first, then what a markup pen offers.
    static const std::vector<std::vector<double>> colours{
        {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 1.0}, {1.0, 0.5, 0.0}, {1.0, 0.0, 1.0},
        {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0},
    };
    return colours;
}

const std::vector<QString>& PropertiesBar::endingNames()
{
    static const std::vector<QString> names{
        QStringLiteral("None"),        QStringLiteral("OpenArrow"), QStringLiteral("ClosedArrow"),
        QStringLiteral("ROpenArrow"),  QStringLiteral("RClosedArrow"), QStringLiteral("Square"),
        QStringLiteral("Circle"),      QStringLiteral("Diamond"),   QStringLiteral("Butt"),
        QStringLiteral("Slash"),
    };
    return names;
}

PropertiesBar::PropertiesBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("propertiesBar"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(6);

    label_ = new QLabel(this);
    row->addWidget(label_);

    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarColour), this));
    colour_ = new QComboBox(this);
    colour_->setObjectName(QStringLiteral("propertiesColour"));
    for (const std::vector<double>& swatch : swatches()) colour_->addItem(swatchIcon(swatch), QString());
    row->addWidget(colour_);

    // S6 T8: the fill -- its first item NO fill.
    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarFill), this));
    fill_ = new QComboBox(this);
    fill_->setObjectName(QStringLiteral("propertiesFill"));
    fill_->addItem(catalog::translate(catalog::key::PropertiesBarFillNone));
    for (const std::vector<double>& swatch : swatches()) fill_->addItem(swatchIcon(swatch), QString());
    row->addWidget(fill_);

    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarOpacity), this));
    opacity_ = new QSpinBox(this);
    opacity_->setObjectName(QStringLiteral("propertiesOpacity"));
    opacity_->setRange(0, 100);
    opacity_->setSingleStep(10);
    row->addWidget(opacity_);

    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarWidth), this));
    width_ = new QDoubleSpinBox(this);
    width_->setObjectName(QStringLiteral("propertiesWidth"));
    width_->setRange(0.0, core::kToolThicknessMax);
    width_->setSingleStep(0.5);
    width_->setDecimals(1);
    row->addWidget(width_);

    // S6 T8: the line style, the blend, the font size, the endings and the cloud.
    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarLineStyle), this));
    lineStyle_ = new QComboBox(this);
    lineStyle_->setObjectName(QStringLiteral("propertiesLineStyle"));
    for (const core::LineStyle style : kLineStyles)
        lineStyle_->addItem(catalog::translate(QStringLiteral("shell.propertiesBar.lineStyle.") + token(core::lineStyleName(style))));
    row->addWidget(lineStyle_);

    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarBlend), this));
    blend_ = new QComboBox(this);
    blend_->setObjectName(QStringLiteral("propertiesBlend"));
    for (const core::ToolBlend blend : kBlends)
        blend_->addItem(catalog::translate(QStringLiteral("shell.propertiesBar.blend.") + token(core::toolBlendName(blend))));
    row->addWidget(blend_);

    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarFontSize), this));
    fontSize_ = new QDoubleSpinBox(this);
    fontSize_->setObjectName(QStringLiteral("propertiesFontSize"));
    fontSize_->setRange(core::kToolFontSizeMin, core::kToolFontSizeMax);
    fontSize_->setDecimals(1);
    row->addWidget(fontSize_);

    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarEndings), this));
    startEnding_ = new QComboBox(this);
    startEnding_->setObjectName(QStringLiteral("propertiesStartEnding"));
    endEnding_ = new QComboBox(this);
    endEnding_->setObjectName(QStringLiteral("propertiesEndEnding"));
    for (const QString& name : endingNames()) {
        const QString label = catalog::translate(QStringLiteral("shell.propertiesBar.ending.") + name);
        startEnding_->addItem(label);
        endEnding_->addItem(label);
    }
    row->addWidget(startEnding_);
    row->addWidget(endEnding_);

    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarCloud), this));
    cloud_ = new QSpinBox(this);
    cloud_->setObjectName(QStringLiteral("propertiesCloud"));
    cloud_->setRange(1, 2);
    row->addWidget(cloud_);

    // S6 T10: a STAMP's turn, in degrees counter-clockwise -- a stamp is the one kind drawn at its natural size and
    // turned by its form's matrix (`core/doc/Stamps`), so any angle, not only the quarter turns.
    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarRotation), this));
    rotation_ = new QSpinBox(this);
    rotation_->setObjectName(QStringLiteral("propertiesRotation"));
    rotation_->setRange(0, 359);
    rotation_->setWrapping(true);
    rotation_->setKeyboardTracking(false);   // one command when the number is finished, not one per digit
    rotation_->setSuffix(QStringLiteral("\u00B0"));
    row->addWidget(rotation_);

    // S6 T12: the measurements made this session -- narrow, so the bar does not widen the window (guide I-16's lesson).
    row->addWidget(new QLabel(catalog::translate(catalog::key::MeasureHistory), this));
    history_ = new QComboBox(this);
    history_->setObjectName(QStringLiteral("propertiesMeasureHistory"));
    history_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    history_->setMinimumContentsLength(8);
    row->addWidget(history_);
    setMeasurementHistory({});

    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarText), this));
    text_ = new QLineEdit(this);
    text_->setObjectName(QStringLiteral("propertiesText"));
    text_->installEventFilter(this);
    row->addWidget(text_, 1);

    locked_ = new QCheckBox(catalog::translate(catalog::key::PropertiesBarLocked), this);
    contentsLocked_ = new QCheckBox(catalog::translate(catalog::key::PropertiesBarContentsLocked), this);
    readOnly_ = new QCheckBox(catalog::translate(catalog::key::PropertiesBarReadOnly), this);
    for (QCheckBox* box : {locked_, contentsLocked_, readOnly_}) row->addWidget(box);

    connect(colour_, &QComboBox::currentIndexChanged, this, [this] {
        if (refreshing_) return;
        core::AnnotationAppearance appearance;
        appearance.color = chosenColour(colour_, customColour_);
        emitAppearance(appearance);
    });
    connect(fill_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (refreshing_) return;
        core::AnnotationAppearance appearance;
        // Item 0 is no fill; the rest are the swatches, then a custom colour.
        appearance.interiorColor = index <= 0 ? std::vector<double>{} : chosenColour(fill_, customFill_);
        emitAppearance(appearance);
    });
    connect(opacity_, &QSpinBox::valueChanged, this, [this](int percent) {
        if (refreshing_) return;
        core::AnnotationAppearance appearance;
        appearance.opacity = percent / 100.0;
        emitAppearance(appearance);
    });
    connect(width_, &QDoubleSpinBox::valueChanged, this, [this](double width) {
        if (refreshing_) return;
        core::AnnotationAppearance appearance;
        appearance.borderWidth = width;
        emitAppearance(appearance);
    });
    connect(lineStyle_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (refreshing_ || index < 0) return;
        core::AnnotationAppearance appearance;
        appearance.lineStyle = kLineStyles[static_cast<std::size_t>(index)];
        emitAppearance(appearance);
    });
    connect(blend_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (refreshing_ || index < 0) return;
        core::AnnotationAppearance appearance;
        appearance.blendMode = kBlends[static_cast<std::size_t>(index)];
        emitAppearance(appearance);
    });
    connect(fontSize_, &QDoubleSpinBox::valueChanged, this, [this](double size) {
        if (refreshing_) return;
        if (subject_ == Subject::Selection) {
            emit fontSizeRequested(size);
        } else if (subject_ == Subject::ToolDefaults) {
            defaults_.fontSize = size;
            emit toolDefaultsChanged(defaults_);
        }
    });
    for (QComboBox* ending : {startEnding_, endEnding_}) {
        connect(ending, &QComboBox::currentIndexChanged, this, [this] {
            if (refreshing_ || subject_ != Subject::Selection) return;
            core::AnnotationAppearance appearance;
            appearance.lineEndings = chosenEndings(calloutSelected_);
            emitAppearance(appearance);
        });
    }
    connect(cloud_, &QSpinBox::valueChanged, this, [this](int intensity) {
        if (refreshing_ || subject_ != Subject::Selection) return;
        core::AnnotationAppearance appearance;
        appearance.cloudIntensity = static_cast<double>(intensity);
        emitAppearance(appearance);
    });
    connect(rotation_, &QSpinBox::valueChanged, this, [this](int degrees) {
        if (refreshing_ || subject_ != Subject::Selection) return;
        emit rotationRequested(degrees);
    });
    connect(text_, &QLineEdit::returnPressed, this, [this] {
        if (refreshing_ || subject_ != Subject::Selection || text_->text() == shownText_) return;
        shownText_ = text_->text();
        emit contentsRequested(text_->text());
    });
    connect(locked_, &QCheckBox::toggled, this, [this](bool on) { lockToggled(core::AnnotationFlag::Locked, on); });
    connect(contentsLocked_, &QCheckBox::toggled, this,
            [this](bool on) { lockToggled(core::AnnotationFlag::LockedContents, on); });
    connect(readOnly_, &QCheckBox::toggled, this, [this](bool on) { lockToggled(core::AnnotationFlag::ReadOnly, on); });

    showNothing();
}

bool PropertiesBar::eventFilter(QObject* watched, QEvent* event)
{
    // `Esc` IN THE TEXT FIELD puts the text back: the field's own key (guide case A-46).
    if (watched == text_ && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && text_->text() != shownText_) {
        text_->setText(shownText_);
        event->accept();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void PropertiesBar::setColour(QComboBox* box, std::vector<double>& custom, const std::vector<double>& colour)
{
    // A colour the swatches do not have is shown as an extra item at the end, so it reads back as itself. The fill
    // box's first item is NO fill, and an empty colour selects it.
    const int first = box == fill_ ? 1 : 0;
    const auto& colours = swatches();
    if (box->count() > static_cast<int>(colours.size()) + first) box->removeItem(box->count() - 1);
    custom.clear();
    if (colour.empty() && first == 1) {
        box->setCurrentIndex(0);
        return;
    }
    for (std::size_t i = 0; i < colours.size(); ++i) {
        if (sameColour(colours[i], colour)) {
            box->setCurrentIndex(static_cast<int>(i) + first);
            return;
        }
    }
    custom = colour;
    box->addItem(swatchIcon(colour), QString());
    box->setCurrentIndex(box->count() - 1);
}

std::vector<double> PropertiesBar::chosenColour(const QComboBox* box, const std::vector<double>& custom) const
{
    const int first = box == fill_ ? 1 : 0;
    const int index = box->currentIndex() - first;
    const auto& colours = swatches();
    if (index >= 0 && index < static_cast<int>(colours.size())) return colours[static_cast<std::size_t>(index)];
    return custom;
}

std::vector<QString> PropertiesBar::chosenEndings(bool callout) const
{
    const auto& names = endingNames();
    const auto nameAt = [&names](const QComboBox* box) {
        const int index = box->currentIndex();
        return index >= 0 && index < static_cast<int>(names.size()) ? names[static_cast<std::size_t>(index)]
                                                                      : QStringLiteral("None");
    };
    if (callout) return {nameAt(startEnding_)};
    return {nameAt(startEnding_), nameAt(endEnding_)};
}

void PropertiesBar::emitAppearance(core::AnnotationAppearance appearance)
{
    if (subject_ == Subject::Selection) {
        emit appearanceRequested(appearance);
        return;
    }
    if (subject_ == Subject::ToolDefaults) {
        if (appearance.color) defaults_.colour = *appearance.color;
        if (appearance.interiorColor) defaults_.fillColour = *appearance.interiorColor;
        if (appearance.opacity) defaults_.opacity = *appearance.opacity;
        if (appearance.borderWidth) defaults_.thickness = *appearance.borderWidth;
        if (appearance.lineStyle) defaults_.lineStyle = *appearance.lineStyle;
        if (appearance.blendMode) defaults_.blendMode = *appearance.blendMode;
        emit toolDefaultsChanged(defaults_);
    }
}

void PropertiesBar::lockToggled(std::uint32_t flag, bool on)
{
    if (refreshing_ || subject_ != Subject::Selection) return;
    emit locksRequested(on ? flag : 0, on ? 0 : flag);
}

void PropertiesBar::enableAll(bool enabled)
{
    for (QWidget* control : std::initializer_list<QWidget*>{colour_, fill_, opacity_, width_, lineStyle_, blend_, fontSize_,
                                                            startEnding_, endEnding_, cloud_, rotation_, text_, locked_,
                                                            contentsLocked_, readOnly_})
        control->setEnabled(enabled);
}

void PropertiesBar::showSelection(const std::vector<core::Annotation>& selection)
{
    if (selection.empty()) {
        showNothing();
        return;
    }
    refreshing_ = true;
    subject_ = Subject::Selection;
    tool_.reset();
    const core::Annotation& first = selection.front();
    label_->setText(selection.size() == 1 ? catalog::translate(catalog::key::PropertiesBarSelectionOne)
                                          : catalog::translate(catalog::key::PropertiesBarSelectionMany).arg(selection.size()));
    enableAll(true);

    const auto all = [&selection](std::uint32_t flag) {
        return std::all_of(selection.begin(), selection.end(), [flag](const core::Annotation& a) { return a.hasFlag(flag); });
    };
    const auto every = [&selection](auto predicate) { return std::all_of(selection.begin(), selection.end(), predicate); };
    const bool anyReadOnly = std::any_of(selection.begin(), selection.end(),
                                         [](const core::Annotation& a) { return a.hasFlag(core::AnnotationFlag::ReadOnly); });
    const bool anyLocked = std::any_of(selection.begin(), selection.end(),
                                       [](const core::Annotation& a) { return a.hasFlag(core::AnnotationFlag::Locked); });

    setColour(colour_, customColour_, first.color.empty() ? std::vector<double>{1.0, 1.0, 0.0} : first.color);
    setColour(fill_, customFill_, first.interiorColor);
    opacity_->setValue(static_cast<int>(std::lround(first.opacity.value_or(1.0) * 100.0)));
    width_->setValue(core::drawnBorderWidth(first));
    const core::LineStyle style = core::lineStyleOf(first.borderStyle, first.dashPattern);
    lineStyle_->setCurrentIndex(static_cast<int>(std::find(kLineStyles.begin(), kLineStyles.end(), style) - kLineStyles.begin()));
    const core::ToolBlend blend = first.blendMode.isEmpty()
                                      ? (first.subtype == AnnotationSubtype::Highlight ? core::ToolBlend::Multiply : core::ToolBlend::Normal)
                                      : core::toolBlendFromPdf(first.blendMode).value_or(core::ToolBlend::Normal);
    blend_->setCurrentIndex(static_cast<int>(std::find(kBlends.begin(), kBlends.end(), blend) - kBlends.begin()));
    fontSize_->setValue(first.subtype == AnnotationSubtype::FreeText ? core::freeTextStyle(first).size : core::kFreeTextDefaultSize);
    const auto endingIndex = [](const std::vector<QString>& endings, std::size_t at) {
        const QString name = at < endings.size() ? endings[at] : QStringLiteral("None");
        const auto& names = endingNames();
        const auto found = std::find(names.begin(), names.end(), name);
        return found == names.end() ? 0 : static_cast<int>(found - names.begin());
    };
    startEnding_->setCurrentIndex(endingIndex(first.lineEndings, 0));
    endEnding_->setCurrentIndex(endingIndex(first.lineEndings, 1));
    cloud_->setValue(static_cast<int>(std::lround(std::clamp(first.cloudIntensity.value_or(1.0), 1.0, 2.0))));
    rotation_->setValue(first.subtype == AnnotationSubtype::Stamp ? core::stampRotation(first) : 0);

    // WHAT APPLIES (T8): each control only where every annotation selected has the property.
    width_->setEnabled(every([](const core::Annotation& a) { return hasBorder(a.subtype); }));
    lineStyle_->setEnabled(every([](const core::Annotation& a) {
        return hasBorder(a.subtype) && !(a.subtype == AnnotationSubtype::Polygon && a.cloudIntensity);
    }));
    fill_->setEnabled(every([](const core::Annotation& a) { return hasFill(a.subtype); }));
    blend_->setEnabled(every([](const core::Annotation& a) { return a.subtype != AnnotationSubtype::Popup; }));
    fontSize_->setEnabled(every([](const core::Annotation& a) { return a.subtype == AnnotationSubtype::FreeText; }));
    const bool lines = every([](const core::Annotation& a) {
        return a.subtype == AnnotationSubtype::Line || a.subtype == AnnotationSubtype::PolyLine;
    });
    calloutSelected_ = every([](const core::Annotation& a) { return isCallout(a); });
    startEnding_->setEnabled(lines || calloutSelected_);
    endEnding_->setEnabled(lines);
    cloud_->setEnabled(every([](const core::Annotation& a) {
        return (a.subtype == AnnotationSubtype::Polygon || a.subtype == AnnotationSubtype::Square) && a.cloudIntensity;
    }));
    // S6 T10: a turn where every one selected is a stamp RawPage can redraw -- a foreign one keeps its producer's.
    rotation_->setEnabled(every([](const core::Annotation& a) { return core::stampKind(a) != core::StampKind::Foreign; }));
    // D6-27 shown where it applies: a lock greys what it forbids, and a read-only one everything but its locks.
    if (anyLocked || anyReadOnly) {
        for (QWidget* control : std::initializer_list<QWidget*>{colour_, fill_, opacity_, width_, lineStyle_, blend_,
                                                                fontSize_, startEnding_, endEnding_, cloud_, rotation_})
            control->setEnabled(false);
    }

    shownText_ = selection.size() == 1 ? first.contents : QString();
    text_->setText(shownText_);
    text_->setEnabled(selection.size() == 1 && !first.hasFlag(core::AnnotationFlag::LockedContents)
                      && !first.hasFlag(core::AnnotationFlag::ReadOnly));

    locked_->setChecked(all(core::AnnotationFlag::Locked));
    contentsLocked_->setChecked(all(core::AnnotationFlag::LockedContents));
    readOnly_->setChecked(all(core::AnnotationFlag::ReadOnly));
    refreshing_ = false;
}

void PropertiesBar::showToolDefaults(const QString& name, AnnotationToolKind tool, const core::ToolDefaults& values)
{
    refreshing_ = true;
    subject_ = Subject::ToolDefaults;
    tool_ = tool;
    defaults_ = values;
    label_->setText(catalog::translate(catalog::key::PropertiesBarTool).arg(name));
    setColour(colour_, customColour_, values.colour);
    setColour(fill_, customFill_, values.fillColour);
    opacity_->setValue(static_cast<int>(std::lround(values.opacity * 100.0)));
    width_->setValue(values.thickness);
    lineStyle_->setCurrentIndex(static_cast<int>(values.lineStyle));
    blend_->setCurrentIndex(static_cast<int>(values.blendMode));
    fontSize_->setValue(values.fontSize);
    shownText_.clear();
    text_->clear();
    enableAll(false);
    // Each control only where `ToolDefaults.def` declares the property for this tool (D6-13).
    const auto has = [tool](core::ToolProperty property) { return core::toolHasProperty(tool, property); };
    colour_->setEnabled(has(core::ToolProperty::Colour));
    fill_->setEnabled(has(core::ToolProperty::FillColour));
    opacity_->setEnabled(has(core::ToolProperty::Opacity));
    width_->setEnabled(has(core::ToolProperty::Thickness));
    lineStyle_->setEnabled(has(core::ToolProperty::LineStyle));
    blend_->setEnabled(has(core::ToolProperty::BlendMode));
    fontSize_->setEnabled(has(core::ToolProperty::FontSize));
    for (QCheckBox* box : {locked_, contentsLocked_, readOnly_}) box->setChecked(false);
    refreshing_ = false;
}

void PropertiesBar::setMeasurementHistory(const QStringList& readings)
{
    if (history_ == nullptr) return;
    history_->clear();
    if (readings.isEmpty()) {
        history_->addItem(catalog::translate(catalog::key::MeasureHistoryEmpty));
        history_->setEnabled(false);
        return;
    }
    history_->addItems(readings);
    history_->setEnabled(true);
}

void PropertiesBar::showNothing()
{
    refreshing_ = true;
    subject_ = Subject::Nothing;
    tool_.reset();
    label_->setText(catalog::translate(catalog::key::PropertiesBarNothing));
    shownText_.clear();
    text_->clear();
    enableAll(false);
    for (QCheckBox* box : {locked_, contentsLocked_, readOnly_}) box->setChecked(false);
    refreshing_ = false;
}

} // namespace rawpage::ui
