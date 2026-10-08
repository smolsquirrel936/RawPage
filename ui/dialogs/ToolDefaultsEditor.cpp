// ui/dialogs/ToolDefaultsEditor -- implementation. Stage 6 T8. See the header for why it is a widget.

#include "ui/dialogs/ToolDefaultsEditor.h"

#include "core/settings/Settings.h"
#include "ui/shell/Catalog.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/viewer/tools/AnnotationTool.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

QIcon swatchIcon(const std::vector<double>& colour)
{
    const auto c = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    QPixmap pixmap(16, 16);
    if (colour.size() == 1) pixmap.fill(QColor(c(colour[0]), c(colour[0]), c(colour[0])));
    else if (colour.size() >= 3) pixmap.fill(QColor(c(colour[0]), c(colour[1]), c(colour[2])));
    else pixmap.fill(Qt::transparent);
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

QString token(std::string_view name)
{
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

constexpr core::LineStyle kLineStyles[] = {core::LineStyle::Solid, core::LineStyle::Dashed, core::LineStyle::Dotted};
constexpr core::ToolBlend kBlends[] = {core::ToolBlend::Normal,  core::ToolBlend::Multiply, core::ToolBlend::Screen,
                                       core::ToolBlend::Overlay, core::ToolBlend::Darken,   core::ToolBlend::Lighten,
                                       core::ToolBlend::Difference, core::ToolBlend::Exclusion};

// A colour box: the swatches (after NO FILL in a fill box), and a colour they lack as one more item.
void showColour(QComboBox* box, bool fill, std::vector<double>& custom, const std::vector<double>& colour)
{
    const int first = fill ? 1 : 0;
    const auto& colours = PropertiesBar::swatches();
    if (box->count() > static_cast<int>(colours.size()) + first) box->removeItem(box->count() - 1);
    custom.clear();
    if (fill && colour.empty()) {
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

std::vector<double> chosen(const QComboBox* box, bool fill, const std::vector<double>& custom)
{
    const int index = box->currentIndex() - (fill ? 1 : 0);
    const auto& colours = PropertiesBar::swatches();
    if (fill && box->currentIndex() == 0) return {};
    if (index >= 0 && index < static_cast<int>(colours.size())) return colours[static_cast<std::size_t>(index)];
    return custom;
}

} // namespace

std::vector<core::CommentingTool> ToolDefaultsEditor::editableTools()
{
    std::vector<core::CommentingTool> out;
    for (int t = 0; t < core::kCommentingToolCount; ++t) {
        const auto tool = static_cast<core::CommentingTool>(t);
        for (int p = 0; p < core::kToolPropertyCount; ++p) {
            if (core::toolHasProperty(tool, static_cast<core::ToolProperty>(p))) {
                out.push_back(tool);
                break;
            }
        }
    }
    return out;
}

ToolDefaultsEditor::ToolDefaultsEditor(core::Settings& settings, QWidget* parent) : QWidget(parent), settings_(settings)
{
    setObjectName(QStringLiteral("toolDefaultsEditor"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 8, 0, 0);
    column->addWidget(new QLabel(catalog::translate(catalog::key::PrefsToolDefaultsTitle), this));
    auto* form = new QFormLayout;
    column->addLayout(form);

    tool_ = new QComboBox(this);
    tool_->setObjectName(QStringLiteral("toolDefaultsTool"));
    for (const core::CommentingTool tool : editableTools())
        tool_->addItem(catalog::translateAction(armingAction(tool)), static_cast<int>(tool));
    form->addRow(catalog::translate(catalog::key::PrefsToolDefaultsTool), tool_);

    colour_ = new QComboBox(this);
    colour_->setObjectName(QStringLiteral("toolDefaultsColour"));
    fill_ = new QComboBox(this);
    fill_->setObjectName(QStringLiteral("toolDefaultsFill"));
    fill_->addItem(catalog::translate(catalog::key::PropertiesBarFillNone));
    for (const std::vector<double>& swatch : PropertiesBar::swatches()) {
        colour_->addItem(swatchIcon(swatch), QString());
        fill_->addItem(swatchIcon(swatch), QString());
    }
    form->addRow(catalog::translate(catalog::key::PropertiesBarColour), colour_);
    form->addRow(catalog::translate(catalog::key::PropertiesBarFill), fill_);

    opacity_ = new QSpinBox(this);
    opacity_->setObjectName(QStringLiteral("toolDefaultsOpacity"));
    opacity_->setRange(0, 100);
    opacity_->setSingleStep(10);
    form->addRow(catalog::translate(catalog::key::PropertiesBarOpacity), opacity_);

    thickness_ = new QDoubleSpinBox(this);
    thickness_->setObjectName(QStringLiteral("toolDefaultsThickness"));
    thickness_->setRange(0.0, core::kToolThicknessMax);
    thickness_->setSingleStep(0.5);
    thickness_->setDecimals(1);
    form->addRow(catalog::translate(catalog::key::PropertiesBarWidth), thickness_);

    lineStyle_ = new QComboBox(this);
    lineStyle_->setObjectName(QStringLiteral("toolDefaultsLineStyle"));
    for (const core::LineStyle style : kLineStyles)
        lineStyle_->addItem(catalog::translate(QStringLiteral("shell.propertiesBar.lineStyle.") + token(core::lineStyleName(style))));
    form->addRow(catalog::translate(catalog::key::PropertiesBarLineStyle), lineStyle_);

    blend_ = new QComboBox(this);
    blend_->setObjectName(QStringLiteral("toolDefaultsBlend"));
    for (const core::ToolBlend blend : kBlends)
        blend_->addItem(catalog::translate(QStringLiteral("shell.propertiesBar.blend.") + token(core::toolBlendName(blend))));
    form->addRow(catalog::translate(catalog::key::PropertiesBarBlend), blend_);

    fontSize_ = new QDoubleSpinBox(this);
    fontSize_->setObjectName(QStringLiteral("toolDefaultsFontSize"));
    fontSize_->setRange(core::kToolFontSizeMin, core::kToolFontSizeMax);
    fontSize_->setDecimals(1);
    form->addRow(catalog::translate(catalog::key::PropertiesBarFontSize), fontSize_);

    reset_ = new QPushButton(catalog::translate(catalog::key::PrefsToolDefaultsReset), this);
    reset_->setObjectName(QStringLiteral("toolDefaultsReset"));
    column->addWidget(reset_, 0, Qt::AlignLeft);

    connect(tool_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    for (QComboBox* box : {colour_, fill_, lineStyle_, blend_})
        connect(box, &QComboBox::currentIndexChanged, this, [this] { write(); });
    connect(opacity_, &QSpinBox::valueChanged, this, [this] { write(); });
    connect(thickness_, &QDoubleSpinBox::valueChanged, this, [this] { write(); });
    connect(fontSize_, &QDoubleSpinBox::valueChanged, this, [this] { write(); });
    connect(reset_, &QPushButton::clicked, this, [this] {
        settings_.resetToolDefaults(currentTool());
        refresh();
    });
    // A row written elsewhere -- `Ctrl+Alt+Enter`, another window -- shows here at once.
    settingsToken_ = settings_.subscribe([this](const core::Settings::Change& change) {
        if (change.group == core::SettingsGroup::Commenting && !refreshing_) refresh();
    });
    refresh();
}

ToolDefaultsEditor::~ToolDefaultsEditor()
{
    settings_.unsubscribe(settingsToken_);
}

core::CommentingTool ToolDefaultsEditor::currentTool() const
{
    return static_cast<core::CommentingTool>(tool_->currentData().toInt());
}

void ToolDefaultsEditor::showTool(core::CommentingTool tool)
{
    const int index = tool_->findData(static_cast<int>(tool));
    if (index >= 0) tool_->setCurrentIndex(index);
}

void ToolDefaultsEditor::refresh()
{
    refreshing_ = true;
    const core::CommentingTool tool = currentTool();
    const core::ToolDefaults values = settings_.toolDefaults(tool);
    showColour(colour_, false, customColour_, values.colour);
    showColour(fill_, true, customFill_, values.fillColour);
    opacity_->setValue(static_cast<int>(std::lround(values.opacity * 100.0)));
    thickness_->setValue(values.thickness);
    lineStyle_->setCurrentIndex(static_cast<int>(values.lineStyle));
    blend_->setCurrentIndex(static_cast<int>(values.blendMode));
    fontSize_->setValue(values.fontSize);
    // FROM THE TABLE: an editor only where `ToolDefaults.def` gives this tool the property.
    const auto has = [tool](core::ToolProperty property) { return core::toolHasProperty(tool, property); };
    colour_->setEnabled(has(core::ToolProperty::Colour));
    fill_->setEnabled(has(core::ToolProperty::FillColour));
    opacity_->setEnabled(has(core::ToolProperty::Opacity));
    thickness_->setEnabled(has(core::ToolProperty::Thickness));
    lineStyle_->setEnabled(has(core::ToolProperty::LineStyle));
    blend_->setEnabled(has(core::ToolProperty::BlendMode));
    fontSize_->setEnabled(has(core::ToolProperty::FontSize));
    refreshing_ = false;
}

void ToolDefaultsEditor::write()
{
    if (refreshing_) return;
    const core::CommentingTool tool = currentTool();
    core::ToolDefaults values = settings_.toolDefaults(tool);
    values.colour = chosen(colour_, false, customColour_);
    values.fillColour = chosen(fill_, true, customFill_);
    values.opacity = opacity_->value() / 100.0;
    values.thickness = thickness_->value();
    values.lineStyle = kLineStyles[std::clamp(lineStyle_->currentIndex(), 0, 2)];
    values.blendMode = kBlends[std::clamp(blend_->currentIndex(), 0, 7)];
    values.fontSize = fontSize_->value();
    refreshing_ = true;   // our own write's change notification is not a refresh
    settings_.setToolDefaults(tool, values);
    refreshing_ = false;
}

void ToolDefaultsEditor::restoreDefaults()
{
    refreshing_ = true;
    for (const core::CommentingTool tool : editableTools()) settings_.resetToolDefaults(tool);
    refreshing_ = false;
    refresh();
}

} // namespace rawpage::ui
