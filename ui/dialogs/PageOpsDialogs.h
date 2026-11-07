// ui/dialogs/PageOpsDialogs -- what `page.extract` and `page.split` open. Stage 7 T7 (`Stage7-Assembly-Plan.md` §6,
// D7-5, D7-30); T8 added insert, delete and replace -- rotate's ranges went into `RotatePagesDialog`, which Stage 3 had
// already made -- and T9 crop (the five page boxes), resize and renumber.
//
// STAGE 7 T8'S THREE. Each starts at the pages a person picked -- the thumbnails' set, or the current page (D7-13):
//   * INSERT: where (before or after a page, first, last) and from what -- the pages of another PDF (a range, or every
//     page), `n` BLANK pages of a named size (`documents/defaultPageSize` first, guide A-45), or the pages ON THE
//     CLIPBOARD (`page.copy`'s, D7-13). Pages only: an IMAGE on the clipboard made into a page is not built here
//     (Q5(d) names it; see `Implementation-Plan.md` § Stage 7, T8's row).
//   * DELETE: a range. `Delete` in the thumbnails pane deletes the set with no question (`Features.md` §8); this is the
//     ribbon's and the tool finder's.
//   * REPLACE: this document's pages, and the file and FIRST page their replacements start at -- as many as are
//     replaced, so the two counts cannot disagree (D7-28's command refuses a mismatch all the same).
// The source file's page count is not the dialog's to know (it holds no backend), so a FILE's range is kept as typed
// and read by `MainWindow` once the file is counted; this document's ranges are checked before the dialog closes.
//
// THE RULE THEY LIVE UNDER (CLAUDE.md): *every dialog collects parameters*. Neither holds a document. Extract answers a
// page set and where it goes; split answers a rule and a folder. `MainWindow` turns each into the assembler's work --
// and, for extract-and-delete, into one `DeletePagesCommand` run only after the new document is written and verified
// (D7-5). Neither is a command: they change no open document.
//
// A PAGE RANGE IS CHECKED BEFORE THE DIALOG CLOSES: `core::parsePageRanges` reads it, and a range it refuses keeps the
// dialog open with the refusal under the field -- a range found wrong after the dialog closed would be a second
// question about the same thing.
//
// NOBODY TO ASK (convention 16). Under the offscreen platform a modal dialog is shown to no one; `canAsk()` is false
// there unless a test has installed an answer, and both commands are DISABLED rather than blocking. The folder a
// per-page extract or a split writes into is asked here too, through a folder picker; the installed answer carries it.

#ifndef RAWPAGE_UI_DIALOGS_PAGEOPSDIALOGS_H
#define RAWPAGE_UI_DIALOGS_PAGEOPSDIALOGS_H

#include "core/Geometry.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Page.h"
#include "core/doc/PaperSize.h"
#include "core/doc/SplitPlan.h"

#include <QDialog>
#include <QString>

#include <array>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace rawpage::ui {

// --- Extract ---------------------------------------------------------------------------------------------------

class ExtractPagesDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        std::vector<int> pages;     // zero-based, strictly ascending
        bool filePerPage = false;   // false: one new document, opened as an untitled tab
        bool deleteAfter = false;   // delete the pages from this document once the extract is written and verified
        QString folder;             // filePerPage: where the files go
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    // `pageCount` bounds the range; `initial` (zero-based) is what the field starts with -- the thumbnails' set, or the
    // current page (S7 T8, `Ctrl+Shift+X`); `folder` what the folder field does (the document's own folder).
    // `mayDelete` false -- a document this build will not edit -- greys the box.
    ExtractPagesDialog(int pageCount, const std::vector<int>& initial, bool mayDelete, const QString& folder, QWidget* parent = nullptr);

    // The choice the widgets hold, or the refusal that keeps the dialog open (`error.pages.badRange`, or
    // `error.assembly.badName` for a per-page extract with no folder).
    core::Result<Choice> choice() const;

    // For a case that drives the widget itself.
    QLineEdit* rangeField() const { return range_; }
    QRadioButton* oneDocumentButton() const { return one_; }
    QRadioButton* filePerPageButton() const { return perPage_; }
    QCheckBox* deleteBox() const { return delete_; }
    QLineEdit* folderField() const { return folder_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, int pageCount, const std::vector<int>& initial, bool mayDelete,
                                     const QString& folder);
    using Answer = std::function<std::optional<Choice>(int pageCount, const std::vector<int>& initial)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    int pageCount_ = 0;
    QLineEdit* range_ = nullptr;
    QRadioButton* one_ = nullptr;
    QRadioButton* perPage_ = nullptr;
    QCheckBox* delete_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QLabel* refusal_ = nullptr;
};

// --- Split -----------------------------------------------------------------------------------------------------

class SplitDocumentDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        core::SplitMethod method = core::SplitMethod::EveryN;
        int pagesPerFile = 1;
        int level = 2;
        double maxMegabytes = 10.0;
        QString pattern;
        bool regex = false;
        bool caseSensitive = false;
        QString folder;
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    // `outlineDepth` -- how deep the document's outline goes; a bookmark split of a document with none is disabled.
    SplitDocumentDialog(int pageCount, int outlineDepth, const QString& folder, QWidget* parent = nullptr);

    core::Result<Choice> choice() const;

    QRadioButton* methodButton(core::SplitMethod method) const;
    QSpinBox* pagesField() const { return pages_; }
    QSpinBox* levelField() const { return level_; }
    QLineEdit* sizeField() const { return size_; }
    QLineEdit* patternField() const { return pattern_; }
    QCheckBox* regexBox() const { return regex_; }
    QLineEdit* folderField() const { return folder_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, int pageCount, int outlineDepth, const QString& folder);
    using Answer = std::function<std::optional<Choice>(int pageCount)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    std::vector<std::pair<core::SplitMethod, QRadioButton*>> methods_;
    QSpinBox* pages_ = nullptr;
    QSpinBox* level_ = nullptr;
    QLineEdit* size_ = nullptr;
    QLineEdit* pattern_ = nullptr;
    QCheckBox* regex_ = nullptr;
    QCheckBox* case_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QLabel* refusal_ = nullptr;
};

// --- Insert (S7 T8) --------------------------------------------------------------------------------------------

class InsertPagesDialog : public QDialog {
    Q_OBJECT

public:
    enum class Source { File, Blank, Clipboard };
    enum class Where { Before, After, First, Last };

    struct Choice {
        Source source = Source::File;
        QString path;                 // File: the PDF
        QString range;                // File: its pages as typed, page numbers from 1; empty for every page
        int blankCount = 1;           // Blank
        core::PdfSize blankSize;      // Blank: points, as the size and orientation chose
        int at = 0;                   // where the first lands, as the document counts before the insert
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    // `pageCount` bounds the position; `page` (zero-based) is the page *before* and *after* name -- the set's last;
    // `paperSize` what the blank size starts at; `clipboardPages` the pages `page.copy` left on the clipboard, 0 for
    // none (the choice is then greyed).
    InsertPagesDialog(int pageCount, int page, core::PaperSize paperSize, int clipboardPages, QWidget* parent = nullptr);

    core::Result<Choice> choice() const;

    QRadioButton* sourceButton(Source source) const;
    QLineEdit* fileField() const { return file_; }
    QLineEdit* fileRangeField() const { return fileRange_; }
    QSpinBox* blankCountField() const { return blankCount_; }
    QComboBox* blankSizeBox() const { return blankSize_; }
    QCheckBox* landscapeBox() const { return landscape_; }
    QComboBox* whereBox() const { return where_; }
    QSpinBox* pageField() const { return page_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, int pageCount, int page, core::PaperSize paperSize, int clipboardPages);
    using Answer = std::function<std::optional<Choice>(int pageCount, int page, core::PaperSize paperSize, int clipboardPages)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    int pageCount_ = 0;
    std::vector<std::pair<Source, QRadioButton*>> sources_;
    QLineEdit* file_ = nullptr;
    QLineEdit* fileRange_ = nullptr;
    QSpinBox* blankCount_ = nullptr;
    QComboBox* blankSize_ = nullptr;
    QCheckBox* landscape_ = nullptr;
    QComboBox* where_ = nullptr;
    QSpinBox* page_ = nullptr;
    QLabel* refusal_ = nullptr;
};

// --- Delete (S7 T8) --------------------------------------------------------------------------------------------

class DeletePagesDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        std::vector<int> pages;   // zero-based, strictly ascending
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    DeletePagesDialog(int pageCount, const std::vector<int>& initial, QWidget* parent = nullptr);

    // Refused: a range that does not parse, and EVERY page (`error.edit.noPagesLeft` -- the command's own key, said
    // before the dialog closes rather than after).
    core::Result<Choice> choice() const;

    QLineEdit* rangeField() const { return range_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, int pageCount, const std::vector<int>& initial);
    using Answer = std::function<std::optional<Choice>(int pageCount, const std::vector<int>& initial)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    int pageCount_ = 0;
    QLineEdit* range_ = nullptr;
    QLabel* refusal_ = nullptr;
};

// --- Replace (S7 T8) -------------------------------------------------------------------------------------------

class ReplacePagesDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        std::vector<int> pages;   // this document's, zero-based, strictly ascending
        QString path;             // the PDF the replacements come from
        int sourceFirst = 0;      // its first page used, zero-based; the next `pages.size()` follow it
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    ReplacePagesDialog(int pageCount, const std::vector<int>& initial, QWidget* parent = nullptr);

    core::Result<Choice> choice() const;

    QLineEdit* rangeField() const { return range_; }
    QLineEdit* fileField() const { return file_; }
    QSpinBox* sourceFirstField() const { return sourceFirst_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, int pageCount, const std::vector<int>& initial);
    using Answer = std::function<std::optional<Choice>(int pageCount, const std::vector<int>& initial)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    int pageCount_ = 0;
    QLineEdit* range_ = nullptr;
    QLineEdit* file_ = nullptr;
    QSpinBox* sourceFirst_ = nullptr;
    QLabel* refusal_ = nullptr;
};

// --- Crop / page boxes (S7 T9) ---------------------------------------------------------------------------------
//
// STAGE 7 T9'S THREE (D7-15, D7-32), each starting at the pages a person picked:
//   * CROP -- *Set Page Boxes*: which box, and what it becomes -- INSET from each page's MediaBox by four margins
//     (what a dragged box fills in, `page.crop`'s tool), the page's CONTENT box with a margin (*remove white margins*,
//     `documents/cropRemoveWhiteMargins` first), or REMOVED so it defaults (*undo a crop*, the MediaBox shown); over a
//     range, or every page the size of the range's first (*apply to all pages of the same size*). Margins are in the
//     page's own space -- left, bottom, right, top as the page is written, before `/Rotate`.
//   * RESIZE: a size (the named ones, or any in points) as the page is SHOWN, whether the content is scaled to fit,
//     and where it is placed -- nine anchors.
//   * RENUMBER: a range, a style, a prefix and a start (`Features.md` §5.1).

class CropPagesDialog : public QDialog {
    Q_OBJECT

public:
    enum class How { Margins, WhiteMargins, Remove };

    struct Choice {
        std::vector<int> pages;   // zero-based, strictly ascending -- the range as typed
        bool sameSize = false;    // every page the size of `pages.front()`, instead of `pages`
        core::PageBoxKind box = core::PageBoxKind::Crop;
        How how = How::Margins;
        double left = 0.0;        // Margins: each edge's inset from the MediaBox, points
        double bottom = 0.0;
        double right = 0.0;
        double top = 0.0;
        double padding = 0.0;     // WhiteMargins: the white kept around the content, points
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    // `margins` -- left, bottom, right, top -- is what the margin fields start at: a dragged box's, or the first page's
    // CropBox measured from its MediaBox; `padding` is `documents/cropRemoveWhiteMargins`.
    CropPagesDialog(int pageCount, const std::vector<int>& initial, std::array<double, 4> margins, double padding,
                    QWidget* parent = nullptr);

    // Refused: a range that does not parse; *remove* of the MediaBox (`error.pages.mediaBoxRequired`).
    core::Result<Choice> choice() const;

    QLineEdit* rangeField() const { return range_; }
    QCheckBox* sameSizeBox() const { return sameSize_; }
    QComboBox* boxKindBox() const { return kind_; }
    QRadioButton* howButton(How how) const;
    QDoubleSpinBox* marginField(int edge) const { return margins_[static_cast<std::size_t>(edge)]; }   // 0 left ... 3 top
    QDoubleSpinBox* paddingField() const { return padding_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, int pageCount, const std::vector<int>& initial,
                                     std::array<double, 4> margins, double padding);
    using Answer = std::function<std::optional<Choice>(int pageCount, const std::vector<int>& initial,
                                                       std::array<double, 4> margins, double padding)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    int pageCount_ = 0;
    QLineEdit* range_ = nullptr;
    QCheckBox* sameSize_ = nullptr;
    QComboBox* kind_ = nullptr;
    std::vector<std::pair<How, QRadioButton*>> hows_;
    std::array<QDoubleSpinBox*, 4> margins_{};
    QDoubleSpinBox* padding_ = nullptr;
    QLabel* refusal_ = nullptr;
};

// --- Resize (S7 T9) --------------------------------------------------------------------------------------------

class ResizePagesDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        std::vector<int> pages;   // zero-based, strictly ascending
        core::PdfSize size;       // points, as the page is shown
        bool scale = true;
        core::ResizeAnchor anchor = core::ResizeAnchor::Centre;
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    // `paperSize` is what the size starts at (`documents/defaultPageSize`).
    ResizePagesDialog(int pageCount, const std::vector<int>& initial, core::PaperSize paperSize, QWidget* parent = nullptr);

    // Refused: a range that does not parse; a size outside 3 ... 14,400 points (`error.pages.badSize`).
    core::Result<Choice> choice() const;

    QLineEdit* rangeField() const { return range_; }
    QComboBox* sizeBox() const { return size_; }
    QDoubleSpinBox* widthField() const { return width_; }
    QDoubleSpinBox* heightField() const { return height_; }
    QCheckBox* landscapeBox() const { return landscape_; }
    QCheckBox* scaleBox() const { return scale_; }
    QComboBox* anchorBox() const { return anchor_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, int pageCount, const std::vector<int>& initial, core::PaperSize paperSize);
    using Answer = std::function<std::optional<Choice>(int pageCount, const std::vector<int>& initial)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    int pageCount_ = 0;
    QLineEdit* range_ = nullptr;
    QComboBox* size_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QCheckBox* landscape_ = nullptr;
    QCheckBox* scale_ = nullptr;
    QComboBox* anchor_ = nullptr;
    QLabel* refusal_ = nullptr;
};

// --- Renumber (S7 T9) ------------------------------------------------------------------------------------------

class RenumberPagesDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        std::vector<int> pages;   // zero-based, strictly ascending
        core::PageLabelStyle style = core::PageLabelStyle::Decimal;
        QString prefix;
        int start = 1;
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    RenumberPagesDialog(int pageCount, const std::vector<int>& initial, QWidget* parent = nullptr);

    // Refused: a range that does not parse.
    core::Result<Choice> choice() const;

    QLineEdit* rangeField() const { return range_; }
    QComboBox* styleBox() const { return style_; }
    QLineEdit* prefixField() const { return prefix_; }
    QSpinBox* startField() const { return start_; }
    QLabel* refusalLabel() const { return refusal_; }

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, int pageCount, const std::vector<int>& initial);
    using Answer = std::function<std::optional<Choice>(int pageCount, const std::vector<int>& initial)>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    int pageCount_ = 0;
    QLineEdit* range_ = nullptr;
    QComboBox* style_ = nullptr;
    QLineEdit* prefix_ = nullptr;
    QSpinBox* start_ = nullptr;
    QLabel* refusal_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_PAGEOPSDIALOGS_H
