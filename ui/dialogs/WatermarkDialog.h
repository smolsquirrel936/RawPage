// ui/dialogs/WatermarkDialog -- *Add* and *Update* a watermark, and the list an *Update* or a *Remove* chooses from.
// Stage 7 T15, decisions D7-22 and D7-45 (`Stage7-Assembly-Plan.md` §3).
//
// EACH DIALOG COLLECTS PARAMETERS AND NOTHING ELSE (CLAUDE.md's standing rule): the watermark dialog answers a
// `core::WatermarkSpec`, the chooser the ids chosen; `ui/shell/MainWindowDecorate` makes the commands. Whether a spec can
// be drawn is `core::validate`'s answer and `core::layoutWatermark`'s on every page it reaches -- too many tiles, a
// Cyrillic letter -- shown in the dialog before OK closes it.
//
// THE SOURCE: text typed here; an IMAGE FILE, decoded once when chosen, as a stamp's picture is
// (`ui/viewer/tools/StampTool`'s `decodeStampImage`); or a PAGE of a PDF file, read through the reader the window gives
// (`IDocumentBackend::stampPage` -- the page copied as an insert copies it). An *Update* of a picture starts on THE
// PICTURE IT HAS (`keepSourceOf`), named by the file it came from, and keeps it unless another is chosen.
//
// THE PREVIEW draws the page chosen -- its shape as SHOWN -- and every copy of the watermark exactly where
// `core::layoutWatermark` places it, at its opacity: the text in the person's Helvetica substitute, an image as decoded,
// a page as a framed box. Not the page's content: the UI thread never rasterizes (CLAUDE.md).
//
// PROFILES (D7-22): the store's watermark kind, the picture carried inside (base64); a picture kept from the document is
// not a file's, so a profile of one is refused by key (`error.decorate.profileKeepsPicture`).
//
// CATALOG FAMILIES, read whole: `decorate.watermark.anchor.`, `decorate.watermark.scaleMode.`, `decorate.watermark.side.`,
// `decorate.watermark.usage.`, `decorate.watermark.kind.`.
//
// Each has `canAsk` / `ask` and a test seam, as every Stage 7 dialog does (convention 16).

#ifndef RAWPAGE_UI_DIALOGS_WATERMARKDIALOG_H
#define RAWPAGE_UI_DIALOGS_WATERMARKDIALOG_H

#include "core/Result.h"
#include "core/doc/PageTree.h"
#include "core/doc/Watermark.h"

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QString>
#include <QWidget>

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
class QRadioButton;
class QSpinBox;

namespace rawpage::core {
class ProfileStore;
}

namespace rawpage::ui {

// The page the preview draws and every copy of the watermark on it.
class WatermarkPreview : public QWidget {
    Q_OBJECT
public:
    explicit WatermarkPreview(QWidget* parent = nullptr);
    void show(core::PdfSize shown, std::optional<core::PageDecoration> decoration, QImage picture);
    const std::optional<core::PageDecoration>& decoration() const noexcept { return decoration_; }
    QSize sizeHint() const override { return {260, 320}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    core::PdfSize shown_{612, 792};
    std::optional<core::PageDecoration> decoration_;
    QImage picture_;
};

class WatermarkDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode { Add, Update };
    struct Choice {
        core::WatermarkSpec spec;
        QString profile;   // the profile chosen when OK was pressed; empty for none
    };
    // A page of a PDF file as a picture -- the window's backend (`IDocumentBackend::stampPage`); null: no page source.
    using PageReader = std::function<core::Result<core::StampPage>(const QString& path, int page)>;

    WatermarkDialog(Mode mode, const core::PageTree& pages, const core::WatermarkSpec& initial, core::ProfileStore* profiles,
                    const QString& profile, PageReader reader, QWidget* parent = nullptr);

    // What the fields say, checked: `core::validate`, the pages it reaches, and each laid out.
    core::Result<Choice> choice() const;
    core::WatermarkSpec specFromFields() const;
    void loadSpec(const core::WatermarkSpec& spec);
    // The two pictures, as their buttons set them -- a seam for the cases, which choose no file through a dialog.
    core::Status setImageFile(const QString& path);
    core::Status setPageFile(const QString& path, int page);

    QRadioButton* textButton() const { return text_; }
    QRadioButton* imageButton() const { return image_; }
    QRadioButton* pageButton() const { return page_; }
    QLineEdit* textField() const { return textField_; }
    QDoubleSpinBox* sizeField() const { return size_; }
    QPushButton* colourButton() const { return colourButton_; }
    QLabel* sourceLabel() const { return sourceLabel_; }
    QSpinBox* pageNumberField() const { return pageNumber_; }
    QDoubleSpinBox* opacityField() const { return opacity_; }        // percent
    QDoubleSpinBox* rotationField() const { return rotation_; }      // degrees
    QComboBox* scaleModeBox() const { return scaleMode_; }
    QDoubleSpinBox* scaleField() const { return scale_; }            // percent
    QComboBox* anchorBox() const { return anchor_; }
    QDoubleSpinBox* offsetXField() const { return offsetX_; }
    QDoubleSpinBox* offsetYField() const { return offsetY_; }
    QCheckBox* tileBox() const { return tile_; }
    QDoubleSpinBox* tileGapField() const { return tileGap_; }
    QComboBox* sideBox() const { return side_; }
    QComboBox* usageBox() const { return usage_; }
    QLineEdit* rangeField() const { return range_; }
    QComboBox* parityBox() const { return parity_; }
    QComboBox* profileBox() const { return profile_; }
    QLineEdit* profileNameField() const { return profileName_; }
    QPushButton* saveProfileButton() const { return saveProfile_; }
    QPushButton* deleteProfileButton() const { return deleteProfile_; }
    QSpinBox* previewPageField() const { return previewPage_; }
    WatermarkPreview* preview() const { return preview_; }
    QLabel* refusalLabel() const { return refusal_; }
    void setColour(const QColor& colour);

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, Mode mode, const core::PageTree& pages, const core::WatermarkSpec& initial,
                                     core::ProfileStore* profiles, const QString& profile, PageReader reader);
    using Answer = std::function<std::optional<Choice>(Mode mode, const core::WatermarkSpec& initial)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    void refreshPreview();
    void refreshSource();
    void reloadProfiles(const QString& select);
    void loadProfile(const QString& name);
    void showRefusal(const core::Error& error);

    const core::PageTree& pages_;
    core::ProfileStore* profiles_ = nullptr;
    PageReader reader_;
    core::TypedFont han_ = core::TypedFont::Simplified;
    // The pictures: an image decoded, a page read -- and an update's kept picture, by the watermark it is from.
    core::StampImage imageSource_;
    QImage imagePreview_;
    QString imageName_;
    core::StampPage pageSource_;
    QString pageName_;
    QString keepSourceOf_;
    core::PdfSize keptSize_;
    QString keptName_;
    core::WatermarkSource keptKind_ = core::WatermarkSource::Image;

    QRadioButton* text_ = nullptr;
    QRadioButton* image_ = nullptr;
    QRadioButton* page_ = nullptr;
    QLineEdit* textField_ = nullptr;
    QDoubleSpinBox* size_ = nullptr;
    QPushButton* colourButton_ = nullptr;
    QColor colour_ = Qt::gray;
    QPushButton* chooseImage_ = nullptr;
    QPushButton* choosePage_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QSpinBox* pageNumber_ = nullptr;
    QDoubleSpinBox* opacity_ = nullptr;
    QDoubleSpinBox* rotation_ = nullptr;
    QComboBox* scaleMode_ = nullptr;
    QDoubleSpinBox* scale_ = nullptr;
    QComboBox* anchor_ = nullptr;
    QDoubleSpinBox* offsetX_ = nullptr;
    QDoubleSpinBox* offsetY_ = nullptr;
    QCheckBox* tile_ = nullptr;
    QDoubleSpinBox* tileGap_ = nullptr;
    QComboBox* side_ = nullptr;
    QComboBox* usage_ = nullptr;
    QLineEdit* range_ = nullptr;
    QComboBox* parity_ = nullptr;
    QComboBox* profile_ = nullptr;
    QLineEdit* profileName_ = nullptr;
    QPushButton* saveProfile_ = nullptr;
    QPushButton* deleteProfile_ = nullptr;
    QSpinBox* previewPage_ = nullptr;
    WatermarkPreview* preview_ = nullptr;
    QLabel* refusal_ = nullptr;
    bool loading_ = false;
};

// ---- Choosing among the document's watermarks (M-09: update and remove act on the one chosen) --------------------------

class WatermarkChooser : public QDialog {
    Q_OBJECT

public:
    enum class Purpose { Update, Remove };
    // `entries` as the document lists them; an *Update* chooses one, a *Remove* one or more (or every one).
    WatermarkChooser(Purpose purpose, std::vector<core::DecorationEntry> entries, QWidget* parent = nullptr);

    // The ids chosen; refused by `error.decorate.noneChosen` with none.
    core::Result<std::vector<QString>> chosen() const;
    QListWidget* list() const { return list_; }

    static bool canAsk();
    static std::optional<std::vector<QString>> ask(QWidget* parent, Purpose purpose, const std::vector<core::DecorationEntry>& entries);
    using Answer = std::function<std::optional<std::vector<QString>>(Purpose purpose, const std::vector<core::DecorationEntry>& entries)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    std::vector<core::DecorationEntry> entries_;
    QListWidget* list_ = nullptr;
    QLabel* refusal_ = nullptr;
};

// What one watermark is, as the chooser lists it: its source, where it is, when it shows -- from its record.
QString describeWatermark(const core::DecorationEntry& entry);

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_WATERMARKDIALOG_H
