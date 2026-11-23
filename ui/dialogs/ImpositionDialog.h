// ui/dialogs/ImpositionDialog -- *Booklet* and *N-up*: how a document's pages go onto sheets. Stage 7 T16, decisions D7-8,
// D7-22 and D7-46 (`Stage7-Assembly-Plan.md` §3).
//
// THE DIALOG COLLECTS PARAMETERS AND NOTHING ELSE (CLAUDE.md's standing rule): it answers a `core::ImpositionSpec`;
// `ui/shell/MainWindowImpose` asks the document's snapshot to impose it, and the new document opens untitled (Q1). Whether
// the spec can be planned for THIS document's pages -- a cell too small, too much creep, marks without margin -- is
// `core::planImposition`'s answer, shown here before OK closes the dialog.
//
// THE PREVIEW draws `core::planImposition`'s plan for the document's pages, one side at a time: the sheet, each cell, each
// page where it is placed with its number (upside down on a side the plan turns), blanks shaded, and the marks where they
// go. Not the pages' content: the UI thread never rasterizes (CLAUDE.md).
//
// PROFILES (D7-22): `core::ProfileStore`'s imposition kind, the spec as `core::impositionToJson` writes it.
//
// `canAsk` / `ask` and a test seam, as every Stage 7 dialog has (convention 16).

#ifndef RAWPAGE_UI_DIALOGS_IMPOSITIONDIALOG_H
#define RAWPAGE_UI_DIALOGS_IMPOSITIONDIALOG_H

#include "core/Geometry.h"
#include "core/Result.h"
#include "core/doc/Imposition.h"

#include <QDialog>
#include <QString>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace rawpage::core {
class ProfileStore;
}

namespace rawpage::ui {

// One side of a plan, drawn: the sheet, its cells, each page's place and number, and the marks.
class ImpositionPreview : public QWidget {
    Q_OBJECT
public:
    explicit ImpositionPreview(QWidget* parent = nullptr);
    void show(std::optional<core::ImpositionPlan> plan, int side);
    const std::optional<core::ImpositionPlan>& plan() const noexcept { return plan_; }
    int side() const noexcept { return side_; }
    QSize sizeHint() const override { return {300, 300}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::optional<core::ImpositionPlan> plan_;
    int side_ = 0;
};

class ImpositionDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        core::ImpositionSpec spec;
        QString profile;   // the profile chosen when OK was pressed; empty for none
    };

    // `shown`: the size each page of the document shows, in order -- what the plan and the preview are made for.
    ImpositionDialog(const core::ImpositionSpec& initial, std::vector<core::PdfSize> shown, core::ProfileStore* profiles,
                     const QString& profile, QWidget* parent = nullptr);

    // What the fields say, checked: planned for the document's pages.
    core::Result<Choice> choice() const;
    core::ImpositionSpec specFromFields() const;
    void loadSpec(const core::ImpositionSpec& spec);

    QComboBox* layoutBox() const { return layout_; }
    QComboBox* perSheetBox() const { return perSheet_; }
    QComboBox* orderBox() const { return order_; }
    QCheckBox* reversedBox() const { return reversed_; }
    QComboBox* bindingBox() const { return binding_; }
    QDoubleSpinBox* creepField() const { return creep_; }
    QComboBox* paperBox() const { return paper_; }
    QDoubleSpinBox* widthField() const { return width_; }
    QDoubleSpinBox* heightField() const { return height_; }
    QComboBox* orientationBox() const { return orientation_; }
    QDoubleSpinBox* marginField() const { return margin_; }
    QDoubleSpinBox* gutterField() const { return gutter_; }
    QComboBox* duplexBox() const { return duplex_; }
    QCheckBox* cropBox() const { return crop_; }
    QCheckBox* registrationBox() const { return registration_; }
    QCheckBox* colourBarBox() const { return colourBar_; }
    QCheckBox* labelsBox() const { return labels_; }
    QCheckBox* framesBox() const { return frames_; }
    QComboBox* profileBox() const { return profile_; }
    QLineEdit* profileNameField() const { return profileName_; }
    QPushButton* saveProfileButton() const { return saveProfile_; }
    QPushButton* deleteProfileButton() const { return deleteProfile_; }
    QSpinBox* previewSideField() const { return previewSide_; }
    ImpositionPreview* preview() const { return preview_; }
    QLabel* summaryLabel() const { return summary_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, const core::ImpositionSpec& initial, std::vector<core::PdfSize> shown,
                                     core::ProfileStore* profiles, const QString& profile);
    using Answer = std::function<std::optional<Choice>(const core::ImpositionSpec& initial, const std::vector<core::PdfSize>& shown)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    void refresh();
    void reloadProfiles(const QString& select);
    void loadProfile(const QString& name);
    void showRefusal(const core::Error& error);

    std::vector<core::PdfSize> shown_;
    core::ProfileStore* profiles_ = nullptr;

    QComboBox* layout_ = nullptr;
    QGroupBox* nUpGroup_ = nullptr;
    QComboBox* perSheet_ = nullptr;
    QComboBox* order_ = nullptr;
    QCheckBox* reversed_ = nullptr;
    QGroupBox* bookletGroup_ = nullptr;
    QComboBox* binding_ = nullptr;
    QDoubleSpinBox* creep_ = nullptr;
    QComboBox* paper_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QComboBox* orientation_ = nullptr;
    QDoubleSpinBox* margin_ = nullptr;
    QDoubleSpinBox* gutter_ = nullptr;
    QComboBox* duplex_ = nullptr;
    QCheckBox* crop_ = nullptr;
    QCheckBox* registration_ = nullptr;
    QCheckBox* colourBar_ = nullptr;
    QCheckBox* labels_ = nullptr;
    QCheckBox* frames_ = nullptr;
    QComboBox* profile_ = nullptr;
    QLineEdit* profileName_ = nullptr;
    QPushButton* saveProfile_ = nullptr;
    QPushButton* deleteProfile_ = nullptr;
    QSpinBox* previewSide_ = nullptr;
    ImpositionPreview* preview_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* refusal_ = nullptr;
    bool loading_ = false;
};

// The sheet sizes the dialog offers, in points, portrait: Letter, Legal, Tabloid, A3, A4, A5.
struct ImpositionPaper {
    QLatin1String key;   // its catalog key
    core::PdfSize size;
};
const std::vector<ImpositionPaper>& impositionPapers();

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_IMPOSITIONDIALOG_H
