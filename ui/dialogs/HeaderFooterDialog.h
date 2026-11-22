// ui/dialogs/HeaderFooterDialog -- *Add* and *Update* a header and footer, and the Bates numbering dialog. Stage 7 T14,
// decisions D7-14, D7-22 and D7-44 (`Stage7-Assembly-Plan.md` §3).
//
// EACH DIALOG COLLECTS PARAMETERS AND NOTHING ELSE (CLAUDE.md's standing rule): the header/footer dialog answers a
// `core::HeaderFooterSpec`, the Bates dialog the documents chosen in order and a spec; `ui/shell/MainWindowDecorate`
// makes the commands. Whether a spec can be drawn is `core::validate`'s answer, shown in the dialog before OK closes it
// -- a Cyrillic letter refused BY NAME at input (guide A-96) -- and the fields' expansion is `core::layoutHeaderFooter`'s.
//
// THE PREVIEW (D7-44(f)) draws the page chosen -- its shape as SHOWN, its rotation applied -- and every line exactly
// where `core::layoutHeaderFooter` places it, expanded for that page, in the size, colour and underline asked for; with
// *shrink*, the box the page's content will be scaled into. It does not draw the page's content: the UI thread never
// rasterizes (CLAUDE.md), and the layout is what the person is choosing. Text is set in the person's Helvetica
// substitute at the advances the page will be set at, so a line's width and place are the page's.
//
// PROFILES (D7-22): the store's header/footer kind, chosen from a list, saved under a name, deleted; a profile the
// dialog cannot read is refused by key and the fields keep what they had.
//
// Each has `canAsk` / `ask` and a test seam, as every Stage 7 dialog does (convention 16: with nobody to ask, the
// action is disabled).

#ifndef RAWPAGE_UI_DIALOGS_HEADERFOOTERDIALOG_H
#define RAWPAGE_UI_DIALOGS_HEADERFOOTERDIALOG_H

#include "core/Result.h"
#include "core/doc/BatesAllocator.h"
#include "core/doc/HeaderFooter.h"
#include "core/doc/PageTree.h"

#include <QColor>
#include <QDialog>
#include <QString>
#include <QWidget>

#include <array>
#include <functional>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace rawpage::core {
class ProfileStore;
}

namespace rawpage::ui {

// The page the preview draws and what goes on it.
class HeaderFooterPreview : public QWidget {
    Q_OBJECT
public:
    explicit HeaderFooterPreview(QWidget* parent = nullptr);
    // `page` as shown, and what `spec` lays out on it -- or a refusal, drawn as nothing.
    void show(const core::Page& page, std::optional<core::PageDecoration> decoration);
    const std::optional<core::PageDecoration>& decoration() const noexcept { return decoration_; }
    QSize sizeHint() const override { return {260, 320}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    core::PdfSize shown_{612, 792};
    std::optional<core::PageDecoration> decoration_;
};

class HeaderFooterDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode { Add, Update };
    struct Choice {
        core::HeaderFooterSpec spec;
        QString profile;   // the profile chosen when OK was pressed; empty for none
    };

    // `pages` is the document's tree (sizes, turns, labels) and `values` the fields fixed when the dialog opened; the
    // fields start at `initial`. `profiles` may be null: no list, no save.
    HeaderFooterDialog(Mode mode, const core::PageTree& pages, core::FieldValues values, const core::HeaderFooterSpec& initial,
                       core::ProfileStore* profiles, const QString& profile, QWidget* parent = nullptr);

    // What the fields say, checked: `core::validate`, and the pages it reaches.
    core::Result<Choice> choice() const;
    core::HeaderFooterSpec specFromFields() const;
    void loadSpec(const core::HeaderFooterSpec& spec);

    QLineEdit* areaField(core::HeaderFooterArea area) const { return areas_[static_cast<std::size_t>(area)]; }
    QComboBox* fieldBox() const { return field_; }
    QPushButton* insertButton() const { return insert_; }
    QDoubleSpinBox* sizeField() const { return size_; }
    QPushButton* colourButton() const { return colourButton_; }
    QCheckBox* underlineBox() const { return underline_; }
    QDoubleSpinBox* marginField(int edge) const { return margins_[static_cast<std::size_t>(edge)]; }   // top, bottom, left, right
    QCheckBox* shrinkBox() const { return shrink_; }
    QLineEdit* rangeField() const { return range_; }
    QComboBox* parityBox() const { return parity_; }
    QComboBox* profileBox() const { return profile_; }
    QLineEdit* profileNameField() const { return profileName_; }
    QPushButton* saveProfileButton() const { return saveProfile_; }
    QPushButton* deleteProfileButton() const { return deleteProfile_; }
    QSpinBox* previewPageField() const { return previewPage_; }
    HeaderFooterPreview* preview() const { return preview_; }
    QLabel* refusalLabel() const { return refusal_; }
    QColor colour() const { return colour_; }
    void setColour(const QColor& colour);

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, Mode mode, const core::PageTree& pages, const core::FieldValues& values,
                                     const core::HeaderFooterSpec& initial, core::ProfileStore* profiles, const QString& profile);
    using Answer = std::function<std::optional<Choice>(Mode mode, const core::HeaderFooterSpec& initial)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refreshPreview();
    void reloadProfiles(const QString& select);
    void loadProfile(const QString& name);

    const core::PageTree& pages_;
    core::FieldValues values_;
    core::TypedFont han_ = core::TypedFont::Simplified;
    core::BatesFormat bates_;
    core::ProfileStore* profiles_ = nullptr;
    std::array<QLineEdit*, core::kHeaderFooterAreas> areas_{};
    QLineEdit* lastArea_ = nullptr;
    QComboBox* field_ = nullptr;
    QPushButton* insert_ = nullptr;
    QDoubleSpinBox* size_ = nullptr;
    QPushButton* colourButton_ = nullptr;
    QColor colour_ = Qt::black;
    QCheckBox* underline_ = nullptr;
    std::array<QDoubleSpinBox*, 4> margins_{};
    QCheckBox* shrink_ = nullptr;
    QLineEdit* range_ = nullptr;
    QComboBox* parity_ = nullptr;
    QComboBox* profile_ = nullptr;
    QLineEdit* profileName_ = nullptr;
    QPushButton* saveProfile_ = nullptr;
    QPushButton* deleteProfile_ = nullptr;
    QSpinBox* previewPage_ = nullptr;
    HeaderFooterPreview* preview_ = nullptr;
    QLabel* refusal_ = nullptr;
    bool loading_ = false;
};

// ---- Bates numbering across the open documents (D7-14) -------------------------------------------------------------

class BatesDialog : public QDialog {
    Q_OBJECT

public:
    struct Document {
        QString name;    // what the list and the report call it
        int pages = 0;
        bool editable = true;
    };
    struct Choice {
        std::vector<std::size_t> order;   // indices into the documents, chosen, in numbering order
        core::HeaderFooterSpec spec;      // `<<bates>>` in one area, and the run's format in `spec.bates`
    };

    // `documents` in tab order, `current` checked first; `format` the settings' (`documents/bates*`).
    BatesDialog(std::vector<Document> documents, std::size_t current, const core::BatesFormat& format, QWidget* parent = nullptr);

    // Refused: no document chosen (`error.decorate.noDocuments`); a prefix or suffix no face can show; a run whose last
    // number needs more digits than chosen (`error.decorate.batesOverflow`).
    core::Result<Choice> choice() const;

    QListWidget* documentList() const { return list_; }
    QPushButton* upButton() const { return up_; }
    QPushButton* downButton() const { return down_; }
    QLineEdit* prefixField() const { return prefix_; }
    QLineEdit* suffixField() const { return suffix_; }
    QSpinBox* digitsField() const { return digits_; }
    QSpinBox* startField() const { return start_; }
    QComboBox* positionBox() const { return position_; }
    QDoubleSpinBox* sizeField() const { return size_; }
    QLabel* sampleLabel() const { return sample_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, std::vector<Document> documents, std::size_t current, const core::BatesFormat& format);
    using Answer = std::function<std::optional<Choice>(const std::vector<Document>& documents, std::size_t current)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    void move(int step);
    void refreshSample();

    std::vector<Document> documents_;
    QListWidget* list_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
    QLineEdit* prefix_ = nullptr;
    QLineEdit* suffix_ = nullptr;
    QSpinBox* digits_ = nullptr;
    QSpinBox* start_ = nullptr;
    QComboBox* position_ = nullptr;
    QDoubleSpinBox* size_ = nullptr;
    QDoubleSpinBox* edge_ = nullptr;
    QDoubleSpinBox* side_ = nullptr;
    QLabel* sample_ = nullptr;
    QLabel* refusal_ = nullptr;
};

// The report a Bates run gives (`Features.md` §5.7): each document's first and last number, or why it was not numbered.
struct BatesReportLine {
    QString document;
    std::optional<core::BatesShare> share;
    QString refusal;   // translated, when not numbered
};
QString batesReportText(const std::vector<BatesReportLine>& lines, const core::BatesFormat& format);

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_HEADERFOOTERDIALOG_H
