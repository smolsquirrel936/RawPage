// ui/viewer/PropertiesBar -- the annotation properties bar `Ctrl+E` opens. Stage 6 T5, decision D6-9
// (`Stage6-Annotations-Plan.md` §3); T8 gave it the drawing kinds' properties and the tools' defaults their keys.
//
// ONE BAR, TWO SUBJECTS. §8 binds `Ctrl+E` twice -- `view.togglePropertiesBar` in the Viewer and
// `annot.properties` in Annotation tools -- and both open this bar. What it shows is decided by what is
// in front of the person: the SELECTION's properties when an annotation is selected, the ARMED TOOL's
// current properties when a tool is armed and nothing is selected, and nothing to change otherwise.
//
// IT COLLECTS AND DOES NOT CHANGE. A change to the selection's colour, fill, opacity, width, line style, blend,
// endings, cloud, text, font size or locks is a SIGNAL; `ui/shell/DocumentWindow` hands it to the overlay, which
// constructs ONE command for it (`SetAnnotationAppearance`, `EditAnnotation`, `LockAnnotation`) -- the rule every
// dialog follows. A change to a tool's properties is the SESSION's (`ui/viewer/ToolDefaults`), and `Ctrl+Alt+Enter`
// makes them the tool's defaults in the settings family (D6-13): the bar says what it was given and reports
// what changed.
//
// WHAT IS SHOWN IS WHAT APPLIES (T8): a control is enabled only where the subject has the property -- a fill for
// a kind with an inside, endings for a line, a polyline or a callout, a font size for typed text, a tool's
// controls where `ToolDefaults.def` declares the property for it.
//
// `Esc` in its text field puts the field back as it was, and is the field's: `edit.cancel` stays
// unregistered (Stage 4's I-06), so the key reaches the widget that has it (guide case A-46).

#ifndef RAWPAGE_UI_VIEWER_PROPERTIESBAR_H
#define RAWPAGE_UI_VIEWER_PROPERTIESBAR_H

#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/Annotation.h"
#include "core/doc/TypedText.h"
#include "core/settings/CommentingTypes.h"
#include "ui/viewer/tools/AnnotationTool.h"

#include <QString>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace rawpage::ui {

class PropertiesBar : public QWidget {
    Q_OBJECT

public:
    enum class Subject { Nothing, Selection, ToolDefaults };

    explicit PropertiesBar(QWidget* parent = nullptr);

    // What the bar shows. Each re-reads its subject; none emits a request.
    void showSelection(const std::vector<core::Annotation>& selection);
    void showToolDefaults(const QString& name, AnnotationToolKind tool, const core::ToolDefaults& values);
    void showNothing();
    Subject subject() const noexcept { return subject_; }

    // The swatches the colour boxes offer, in their order.
    static const std::vector<std::vector<double>>& swatches();
    // The ten line endings, in the order the two ending boxes offer them (ISO 32000-1 Table 176).
    static const std::vector<QString>& endingNames();

    // For the cases, which drive the bar as a person does.
    QComboBox* colourBox() const noexcept { return colour_; }
    QComboBox* fillBox() const noexcept { return fill_; }
    QSpinBox* opacityBox() const noexcept { return opacity_; }
    QDoubleSpinBox* widthBox() const noexcept { return width_; }
    QComboBox* lineStyleBox() const noexcept { return lineStyle_; }
    QComboBox* blendBox() const noexcept { return blend_; }
    QDoubleSpinBox* fontSizeBox() const noexcept { return fontSize_; }
    QComboBox* startEndingBox() const noexcept { return startEnding_; }
    QComboBox* endEndingBox() const noexcept { return endEnding_; }
    QSpinBox* cloudBox() const noexcept { return cloud_; }
    QSpinBox* rotationBox() const noexcept { return rotation_; }
    QLineEdit* textField() const noexcept { return text_; }
    QCheckBox* lockedBox() const noexcept { return locked_; }
    QCheckBox* contentsLockedBox() const noexcept { return contentsLocked_; }
    QCheckBox* readOnlyBox() const noexcept { return readOnly_; }
    QLabel* subjectLabel() const noexcept { return label_; }

signals:
    // The selection's: one command each, constructed by the overlay.
    void appearanceRequested(const core::AnnotationAppearance& appearance);
    void contentsRequested(const QString& text);
    void fontSizeRequested(double size);
    // S6 T10: the selected stamps turned to this many degrees.
    void rotationRequested(int degrees);
    void locksRequested(std::uint32_t set, std::uint32_t clear);
    // The armed tool's current properties, changed.
    void toolDefaultsChanged(const core::ToolDefaults& values);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setColour(QComboBox* box, std::vector<double>& custom, const std::vector<double>& colour);
    std::vector<double> chosenColour(const QComboBox* box, const std::vector<double>& custom) const;
    void emitAppearance(core::AnnotationAppearance appearance);
    void lockToggled(std::uint32_t flag, bool on);
    void enableAll(bool enabled);
    std::vector<QString> chosenEndings(bool callout) const;

    Subject subject_ = Subject::Nothing;
    bool refreshing_ = false;
    bool calloutSelected_ = false;
    QString shownText_;
    std::vector<double> customColour_;
    std::vector<double> customFill_;
    core::ToolDefaults defaults_;
    std::optional<AnnotationToolKind> tool_;

    QLabel* label_ = nullptr;
    QComboBox* colour_ = nullptr;
    QComboBox* fill_ = nullptr;
    QSpinBox* opacity_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QComboBox* lineStyle_ = nullptr;
    QComboBox* blend_ = nullptr;
    QDoubleSpinBox* fontSize_ = nullptr;
    QComboBox* startEnding_ = nullptr;
    QComboBox* endEnding_ = nullptr;
    QSpinBox* cloud_ = nullptr;
    QSpinBox* rotation_ = nullptr;   // S6 T10: a stamp's turn
    QLineEdit* text_ = nullptr;
    QCheckBox* locked_ = nullptr;
    QCheckBox* contentsLocked_ = nullptr;
    QCheckBox* readOnly_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_PROPERTIESBAR_H
