// ui/viewer/PropertiesBar -- implementation. Stage 6 T5. See the header for its two subjects and why it
// changes nothing itself.

#include "ui/viewer/PropertiesBar.h"

#include "core/doc/AnnotationGeometry.h"
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

// Whether a kind is drawn with an outline whose width means something.
bool hasBorder(core::AnnotationSubtype subtype)
{
    switch (subtype) {
    case core::AnnotationSubtype::Square:
    case core::AnnotationSubtype::Circle:
    case core::AnnotationSubtype::Line:
    case core::AnnotationSubtype::Polygon:
    case core::AnnotationSubtype::PolyLine:
    case core::AnnotationSubtype::Ink:
    case core::AnnotationSubtype::FreeText:
        return true;
    default:
        return false;
    }
}

} // namespace

AnnotationToolDefaults& sessionToolDefaults(AnnotationToolKind kind)
{
    // What a reviewer expects each to start as: the highlighter yellow and see-through, the pens red.
    static std::array<AnnotationToolDefaults, kAnnotationToolKindCount> table = [] {
        std::array<AnnotationToolDefaults, kAnnotationToolKindCount> t{};
        const auto set = [&t](AnnotationToolKind k, std::vector<double> colour, double opacity, double width) {
            t[static_cast<std::size_t>(k)] = AnnotationToolDefaults{std::move(colour), opacity, width};
        };
        set(AnnotationToolKind::StickyNote, {1.0, 1.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::Highlight, {1.0, 1.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::Underline, {0.0, 0.5, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::Strikeout, {1.0, 0.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::TextBox, {0.0, 0.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::Line, {1.0, 0.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::Rectangle, {1.0, 0.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::Oval, {1.0, 0.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::Polygon, {1.0, 0.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::Pencil, {1.0, 0.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::Stamp, {1.0, 0.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::MeasureDistance, {0.0, 0.0, 1.0}, 1.0, 1.0);
        set(AnnotationToolKind::MeasureArea, {0.0, 0.0, 1.0}, 1.0, 1.0);
        set(AnnotationToolKind::RedactMark, {0.0, 0.0, 0.0}, 1.0, 1.0);
        // S6 T6. The squiggly green and the text edits blue, as Acrobat starts them; the note's yellow is
        // the sticky note's own (above).
        set(AnnotationToolKind::Squiggly, {0.0, 0.6, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::ReplaceText, {0.0, 0.0, 1.0}, 1.0, 1.0);
        set(AnnotationToolKind::InsertText, {0.0, 0.0, 1.0}, 1.0, 1.0);
        // S6 T7. Typed text starts black, as a person's own writing does; a callout red, as Acrobat starts
        // it -- its text, outline and leader one colour (D6-34). A typewriter has no outline at all.
        set(AnnotationToolKind::Callout, {1.0, 0.0, 0.0}, 1.0, 1.0);
        set(AnnotationToolKind::Typewriter, {0.0, 0.0, 0.0}, 1.0, 0.0);
        return t;
    }();
    return table[static_cast<std::size_t>(kind)];
}

const std::vector<std::vector<double>>& PropertiesBar::swatches()
{
    // The reviewer's colours: the highlighter's yellow first, then what a markup pen offers.
    static const std::vector<std::vector<double>> colours{
        {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 1.0}, {1.0, 0.5, 0.0}, {1.0, 0.0, 1.0},
        {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0},
    };
    return colours;
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

    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarOpacity), this));
    opacity_ = new QSpinBox(this);
    opacity_->setObjectName(QStringLiteral("propertiesOpacity"));
    opacity_->setRange(0, 100);
    opacity_->setSingleStep(10);
    row->addWidget(opacity_);

    row->addWidget(new QLabel(catalog::translate(catalog::key::PropertiesBarWidth), this));
    width_ = new QDoubleSpinBox(this);
    width_->setObjectName(QStringLiteral("propertiesWidth"));
    width_->setRange(0.0, 24.0);
    width_->setSingleStep(0.5);
    width_->setDecimals(1);
    row->addWidget(width_);

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
        appearance.color = chosenColour();
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

void PropertiesBar::setColour(const std::vector<double>& colour)
{
    // A colour the swatches do not have is shown as an extra item at the end, so it reads back as itself.
    const auto& colours = swatches();
    if (colour_->count() > static_cast<int>(colours.size())) colour_->removeItem(colour_->count() - 1);
    customColour_.clear();
    for (std::size_t i = 0; i < colours.size(); ++i) {
        if (sameColour(colours[i], colour)) {
            colour_->setCurrentIndex(static_cast<int>(i));
            return;
        }
    }
    customColour_ = colour;
    colour_->addItem(swatchIcon(colour), QString());
    colour_->setCurrentIndex(colour_->count() - 1);
}

std::vector<double> PropertiesBar::chosenColour() const
{
    const int index = colour_->currentIndex();
    const auto& colours = swatches();
    if (index >= 0 && index < static_cast<int>(colours.size())) return colours[static_cast<std::size_t>(index)];
    return customColour_;
}

void PropertiesBar::emitAppearance(core::AnnotationAppearance appearance)
{
    if (subject_ == Subject::Selection) {
        emit appearanceRequested(appearance);
        return;
    }
    if (subject_ == Subject::ToolDefaults) {
        if (appearance.color) defaults_.colour = *appearance.color;
        if (appearance.opacity) defaults_.opacity = *appearance.opacity;
        if (appearance.borderWidth) defaults_.borderWidth = *appearance.borderWidth;
        emit toolDefaultsChanged(defaults_);
    }
}

void PropertiesBar::lockToggled(std::uint32_t flag, bool on)
{
    if (refreshing_ || subject_ != Subject::Selection) return;
    emit locksRequested(on ? flag : 0, on ? 0 : flag);
}

void PropertiesBar::showSelection(const std::vector<core::Annotation>& selection)
{
    if (selection.empty()) {
        showNothing();
        return;
    }
    refreshing_ = true;
    subject_ = Subject::Selection;
    const core::Annotation& first = selection.front();
    label_->setText(selection.size() == 1 ? catalog::translate(catalog::key::PropertiesBarSelectionOne)
                                          : catalog::translate(catalog::key::PropertiesBarSelectionMany).arg(selection.size()));
    for (QWidget* control : std::initializer_list<QWidget*>{colour_, opacity_, width_, text_, locked_, contentsLocked_, readOnly_})
        control->setEnabled(true);

    const auto all = [&selection](std::uint32_t flag) {
        return std::all_of(selection.begin(), selection.end(), [flag](const core::Annotation& a) { return a.hasFlag(flag); });
    };
    const bool anyReadOnly = std::any_of(selection.begin(), selection.end(),
                                         [](const core::Annotation& a) { return a.hasFlag(core::AnnotationFlag::ReadOnly); });
    const bool anyLocked = std::any_of(selection.begin(), selection.end(),
                                       [](const core::Annotation& a) { return a.hasFlag(core::AnnotationFlag::Locked); });

    setColour(first.color.empty() ? std::vector<double>{1.0, 1.0, 0.0} : first.color);
    opacity_->setValue(static_cast<int>(std::lround(first.opacity.value_or(1.0) * 100.0)));
    width_->setValue(core::drawnBorderWidth(first));
    width_->setEnabled(std::all_of(selection.begin(), selection.end(),
                                   [](const core::Annotation& a) { return hasBorder(a.subtype); }));
    // D6-27 shown where it applies: a lock greys what it forbids, and a read-only one everything but its locks.
    colour_->setEnabled(!anyLocked && !anyReadOnly);
    opacity_->setEnabled(!anyLocked && !anyReadOnly);
    if (anyLocked || anyReadOnly) width_->setEnabled(false);

    shownText_ = selection.size() == 1 ? first.contents : QString();
    text_->setText(shownText_);
    text_->setEnabled(selection.size() == 1 && !first.hasFlag(core::AnnotationFlag::LockedContents)
                      && !first.hasFlag(core::AnnotationFlag::ReadOnly));

    locked_->setChecked(all(core::AnnotationFlag::Locked));
    contentsLocked_->setChecked(all(core::AnnotationFlag::LockedContents));
    readOnly_->setChecked(all(core::AnnotationFlag::ReadOnly));
    refreshing_ = false;
}

void PropertiesBar::showToolDefaults(const QString& tool, const AnnotationToolDefaults& defaults)
{
    refreshing_ = true;
    subject_ = Subject::ToolDefaults;
    defaults_ = defaults;
    label_->setText(catalog::translate(catalog::key::PropertiesBarTool).arg(tool));
    setColour(defaults.colour);
    opacity_->setValue(static_cast<int>(std::lround(defaults.opacity * 100.0)));
    width_->setValue(defaults.borderWidth);
    shownText_.clear();
    text_->clear();
    for (QWidget* control : std::initializer_list<QWidget*>{colour_, opacity_, width_}) control->setEnabled(true);
    for (QWidget* control : std::initializer_list<QWidget*>{text_, locked_, contentsLocked_, readOnly_}) control->setEnabled(false);
    for (QCheckBox* box : {locked_, contentsLocked_, readOnly_}) box->setChecked(false);
    refreshing_ = false;
}

void PropertiesBar::showNothing()
{
    refreshing_ = true;
    subject_ = Subject::Nothing;
    label_->setText(catalog::translate(catalog::key::PropertiesBarNothing));
    shownText_.clear();
    text_->clear();
    for (QWidget* control : std::initializer_list<QWidget*>{colour_, opacity_, width_, text_, locked_, contentsLocked_, readOnly_})
        control->setEnabled(false);
    for (QCheckBox* box : {locked_, contentsLocked_, readOnly_}) box->setChecked(false);
    refreshing_ = false;
}

} // namespace rawpage::ui
