// ui/dialogs/ToolDefaultsEditor -- the Commenting page's editor of each annotation tool's DEFAULTS, the settings
// family `commenting/default<Property>.<tool>`. Stage 6 T8, decision D6-13 (`Stage6-Annotations-Plan.md` §3).
//
// WHY A WIDGET AND NOT GENERATED ROWS. A Preferences page is built from `SettingsKeys.def` (convention 17), and
// the family is not rows there: it is the tools crossed with the properties, kept where `ToolDefaults.def` has a
// row -- a hundred keys that would be a hundred rows nobody could find a tool among. So the page is its generated
// rows (`commenting/stickyTool`, `commenting/popupBehaviour`) and, below them, this: ONE tool chosen, and an
// editor for each property the tool has -- built from the TABLE (`core::toolHasProperty`), so a property
// `ToolDefaults.def` gives a tool appears here with no widget code, and one it does not is greyed.
//
// IT APPLIES IMMEDIATELY, as every Preferences page does (`PreferencesDialog`'s header): each change writes the
// tool's row through `core::Settings::setToolDefaults`, and the next annotation that tool makes uses it -- unless
// the properties bar changed the tool in this session, which a person sees on the bar (`ui/viewer/ToolDefaults`).
// `Reset` takes the chosen tool back to the table; the dialog's *Restore Defaults* takes every tool back.

#ifndef RAWPAGE_UI_DIALOGS_TOOLDEFAULTSEDITOR_H
#define RAWPAGE_UI_DIALOGS_TOOLDEFAULTSEDITOR_H

#include "core/settings/CommentingTypes.h"

#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace rawpage::core {
class Settings;
}

namespace rawpage::ui {

class ToolDefaultsEditor : public QWidget {
    Q_OBJECT

public:
    ToolDefaultsEditor(core::Settings& settings, QWidget* parent = nullptr);
    ~ToolDefaultsEditor() override;

    // The tools offered: every tool `ToolDefaults.def` gives a property, in `CommentingTools.def`'s order.
    static std::vector<core::CommentingTool> editableTools();

    void showTool(core::CommentingTool tool);
    core::CommentingTool currentTool() const;
    // Every tool's rows removed: each reads as the table's defaults again.
    void restoreDefaults();

    // For the cases, which drive the editor as a person does.
    QComboBox* toolBox() const noexcept { return tool_; }
    QComboBox* colourBox() const noexcept { return colour_; }
    QComboBox* fillBox() const noexcept { return fill_; }
    QSpinBox* opacityBox() const noexcept { return opacity_; }
    QDoubleSpinBox* thicknessBox() const noexcept { return thickness_; }
    QComboBox* lineStyleBox() const noexcept { return lineStyle_; }
    QComboBox* blendBox() const noexcept { return blend_; }
    QDoubleSpinBox* fontSizeBox() const noexcept { return fontSize_; }
    QLineEdit* overlayTextField() const noexcept { return overlayText_; }   // S6 T14
    QCheckBox* repeatBox() const noexcept { return repeat_; }
    QPushButton* resetButton() const noexcept { return reset_; }

private:
    void refresh();
    void write();

    core::Settings& settings_;
    bool refreshing_ = false;
    std::vector<double> customColour_;
    std::vector<double> customFill_;

    QComboBox* tool_ = nullptr;
    QComboBox* colour_ = nullptr;
    QComboBox* fill_ = nullptr;
    QSpinBox* opacity_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QComboBox* lineStyle_ = nullptr;
    QComboBox* blend_ = nullptr;
    QDoubleSpinBox* fontSize_ = nullptr;
    QLineEdit* overlayText_ = nullptr;   // S6 T14
    QCheckBox* repeat_ = nullptr;        // S6 T14
    QPushButton* reset_ = nullptr;
    int settingsToken_ = 0;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_TOOLDEFAULTSEDITOR_H
