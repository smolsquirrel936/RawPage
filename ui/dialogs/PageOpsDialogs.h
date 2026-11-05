// ui/dialogs/PageOpsDialogs -- what `page.extract` and `page.split` open. Stage 7 T7 (`Stage7-Assembly-Plan.md` §6,
// D7-5, D7-30); T8 and T9 add insert, delete, replace, crop, resize, renumber and rotate's ranges to this family.
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

#include "core/doc/SplitPlan.h"

#include <QDialog>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QCheckBox;
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

    // `pageCount` bounds the range; `currentPage` (zero-based) is what the field starts with; `folder` what the folder
    // field does (the document's own folder). `mayDelete` false -- a document this build will not edit -- greys the box.
    ExtractPagesDialog(int pageCount, int currentPage, bool mayDelete, const QString& folder, QWidget* parent = nullptr);

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
    static std::optional<Choice> ask(QWidget* parent, int pageCount, int currentPage, bool mayDelete, const QString& folder);
    using Answer = std::function<std::optional<Choice>(int pageCount, int currentPage)>;
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

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_PAGEOPSDIALOGS_H
