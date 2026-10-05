// ui/viewer/PropertiesBar -- the annotation properties bar `Ctrl+E` opens. Stage 6 T5, decision D6-9
// (`Stage6-Annotations-Plan.md` §3).
//
// ONE BAR, TWO SUBJECTS. §8 binds `Ctrl+E` twice -- `view.togglePropertiesBar` in the Viewer and
// `annot.properties` in Annotation tools -- and both open this bar. What it shows is decided by what is
// in front of the person: the SELECTION's properties when an annotation is selected, the ARMED TOOL's
// defaults when a tool is armed and nothing is selected, and nothing to change otherwise.
//
// IT COLLECTS AND DOES NOT CHANGE. A change to the selection's colour, opacity, border, text or locks is
// a SIGNAL; `ui/shell/DocumentWindow` hands it to the overlay, which constructs ONE command for it
// (`SetAnnotationAppearance`, `EditAnnotation`, `LockAnnotation`) -- the rule every dialog follows. A
// change to a tool's defaults is the SESSION's until T8 gives them settings keys (D6-13's family): the
// bar says what it was given and reports what changed.
//
// `Esc` in its text field puts the field back as it was, and is the field's: `edit.cancel` stays
// unregistered (Stage 4's I-06), so the key reaches the widget that has it (guide case A-46).

#ifndef RAWPAGE_UI_VIEWER_PROPERTIESBAR_H
#define RAWPAGE_UI_VIEWER_PROPERTIESBAR_H

#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/Annotation.h"
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

// A tool's defaults, as the bar edits them: the session's until T8's settings family (D6-13).
struct AnnotationToolDefaults {
    std::vector<double> colour{1.0, 1.0, 0.0};
    double opacity = 1.0;
    double borderWidth = 1.0;

    bool operator==(const AnnotationToolDefaults&) const = default;
};

// The armed tool's defaults FOR THIS SESSION, one per tool, shared by every window: what the bar shows with
// a tool armed and nothing selected, and what it changes. T8 replaces them with the settings family
// `commenting/default<Property>.<tool>` (D6-13); until then they last as long as the process.
AnnotationToolDefaults& sessionToolDefaults(AnnotationToolKind kind);

class PropertiesBar : public QWidget {
    Q_OBJECT

public:
    enum class Subject { Nothing, Selection, ToolDefaults };

    explicit PropertiesBar(QWidget* parent = nullptr);

    // What the bar shows. Each re-reads its subject; none emits a request.
    void showSelection(const std::vector<core::Annotation>& selection);
    void showToolDefaults(const QString& tool, const AnnotationToolDefaults& defaults);
    void showNothing();
    Subject subject() const noexcept { return subject_; }

    // The swatches the colour box offers, in its order.
    static const std::vector<std::vector<double>>& swatches();

    // For the cases, which drive the bar as a person does.
    QComboBox* colourBox() const noexcept { return colour_; }
    QSpinBox* opacityBox() const noexcept { return opacity_; }
    QDoubleSpinBox* widthBox() const noexcept { return width_; }
    QLineEdit* textField() const noexcept { return text_; }
    QCheckBox* lockedBox() const noexcept { return locked_; }
    QCheckBox* contentsLockedBox() const noexcept { return contentsLocked_; }
    QCheckBox* readOnlyBox() const noexcept { return readOnly_; }
    QLabel* subjectLabel() const noexcept { return label_; }

signals:
    // The selection's: one command each, constructed by the overlay.
    void appearanceRequested(const core::AnnotationAppearance& appearance);
    void contentsRequested(const QString& text);
    void locksRequested(std::uint32_t set, std::uint32_t clear);
    // The armed tool's defaults, changed.
    void toolDefaultsChanged(const AnnotationToolDefaults& defaults);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setColour(const std::vector<double>& colour);
    std::vector<double> chosenColour() const;
    void emitAppearance(core::AnnotationAppearance appearance);
    void lockToggled(std::uint32_t flag, bool on);

    Subject subject_ = Subject::Nothing;
    bool refreshing_ = false;
    QString shownText_;
    std::vector<double> customColour_;
    AnnotationToolDefaults defaults_;

    QLabel* label_ = nullptr;
    QComboBox* colour_ = nullptr;
    QSpinBox* opacity_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QLineEdit* text_ = nullptr;
    QCheckBox* locked_ = nullptr;
    QCheckBox* contentsLocked_ = nullptr;
    QCheckBox* readOnly_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_PROPERTIESBAR_H
