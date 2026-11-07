// ui/dialogs/RotatePagesDialog -- what `Ctrl+R` opens: a direction and a page set, and nothing else.
// Stage 3 T10, decision D3-13 (`Stage3-Mutation-Plan.md` §3); its ranges and filters Stage 7 T8's
// (`Stage7-Assembly-Plan.md` D7-13, §6; `Features.md` §5.1).
//
// THE RULE IT LIVES UNDER (CLAUDE.md): *every dialog collects parameters and constructs a `Command`.* This
// one holds no `Document` and no journal. It answers a `Choice`; `planRotation` turns a choice into the
// command -- ONE `RotatePageCommand` over the page set, one undo step -- and `MainWindow` runs it.
//
// THE PAGE SET, SINCE STAGE 7 T8 (D3-13 named this stage for it): the current page, the SELECTED pages (the
// thumbnails' set, offered and chosen first when it holds more than one page -- `Ctrl+R` on a set, D7-13), all
// pages, or a range typed as extract's is (`core::parsePageRanges`); then narrowed to odd or even pages and to one
// orientation (`core::filterPages`). Stage 3 ran one command per page inside a group for *all pages*; a set is one
// command now, and Stage 3's journal lines still replay (`RotatePageCommand`'s header).
//
// A RANGE IS CHECKED BEFORE THE DIALOG CLOSES, as extract's is; a filter that leaves NO page is refused there too
// (`error.pages.noneMatch`) -- a rotation of nothing would be an undo step that changed nothing.
//
// NOBODY TO ASK (convention 16). Under the offscreen platform a modal dialog is shown to no one and never
// returns (Stage 1's I-23). `canAsk()` is false there unless a test has installed an answer, and
// `page.rotate` is DISABLED rather than blocking. `setAnswerForTesting` is the parameterized twin a
// headless case drives; the widget itself is still built and driven by A-73's case.

#ifndef RAWPAGE_UI_DIALOGS_ROTATEPAGESDIALOG_H
#define RAWPAGE_UI_DIALOGS_ROTATEPAGESDIALOG_H

#include "core/Result.h"
#include "core/cmd/Command.h"
#include "core/doc/PageRanges.h"

#include <QDialog>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;

namespace rawpage::core {
class PageTree;
}

namespace rawpage::ui {

class RotatePagesDialog : public QDialog {
    Q_OBJECT

public:
    enum class Pages { Current, Selected, All, Range };

    struct Choice {
        // A direction and a page set, the filters left open -- what most callers mean. Constructors rather than
        // an aggregate, so a call naming two fields is not `-Wmissing-field-initializers` for the other three.
        Choice() = default;
        Choice(int turn, Pages set) : degrees(turn), pages(set) {}

        int degrees = 90;        // clockwise: 90, 180 or 270 ("90 degrees counter-clockwise")
        Pages pages = Pages::Current;
        QString range;           // `Pages::Range`: as typed, page numbers from 1
        core::PageParity parity = core::PageParity::Any;
        core::PageOrientation orientation = core::PageOrientation::Any;
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    // `currentPage` is zero-based: the page "current page" rotates, named on its button as *"Current page
    // (page n)"* so a person need not guess which page the viewer counts as current (M-01's walk).
    // `selected` is the thumbnails' set; with more than one page in it, *Selected pages* is offered and checked.
    RotatePagesDialog(int currentPage, std::vector<int> selected, int pageCount, QWidget* parent = nullptr);

    // The choice the widgets hold, or why not: a range `core::parsePageRanges` refuses.
    core::Result<Choice> choice() const;

    // For a case that drives the widget itself.
    QRadioButton* clockwiseButton() const { return clockwise_; }
    QRadioButton* halfTurnButton() const { return half_; }
    QRadioButton* counterClockwiseButton() const { return counterClockwise_; }
    QRadioButton* currentPageButton() const { return currentPage_; }
    QRadioButton* selectedPagesButton() const { return selectedPages_; }
    QRadioButton* allPagesButton() const { return allPages_; }
    QRadioButton* rangeButton() const { return range_; }
    QLineEdit* rangeField() const { return rangeField_; }
    QComboBox* parityBox() const { return parity_; }
    QComboBox* orientationBox() const { return orientation_; }
    QLabel* refusalLabel() const { return refusal_; }

    // Whether anyone can answer: a person on a real platform, or a test's installed answer.
    static bool canAsk();

    // Shows the dialog and returns the choice, or nothing for Cancel -- or the test's answer, if one is
    // installed. Nothing, immediately, when `canAsk()` is false.
    static std::optional<Choice> ask(QWidget* parent, int currentPage, const std::vector<int>& selected, int pageCount);

    using Answer = std::function<std::optional<Choice>()>;
    static void setAnswerForTesting(Answer answer);

    void accept() override;

private:
    int pageCount_ = 0;
    QRadioButton* clockwise_ = nullptr;
    QRadioButton* half_ = nullptr;
    QRadioButton* counterClockwise_ = nullptr;
    QRadioButton* currentPage_ = nullptr;
    QRadioButton* selectedPages_ = nullptr;
    QRadioButton* allPages_ = nullptr;
    QRadioButton* range_ = nullptr;
    QLineEdit* rangeField_ = nullptr;
    QComboBox* parity_ = nullptr;
    QComboBox* orientation_ = nullptr;
    QLabel* refusal_ = nullptr;
};

// THE COMMAND a choice means, over `pages` (the model's tree, for the count and each page's orientation) whose current
// page is `currentPage` and whose thumbnails' set is `selected`: one `RotatePageCommand`. Refused by key for a range
// that does not parse (`error.pages.badRange`) and a set the filters leave empty (`error.pages.noneMatch`).
core::Result<std::unique_ptr<core::Command>> planRotation(const RotatePagesDialog::Choice& choice, int currentPage,
                                                          const std::vector<int>& selected, const core::PageTree& pages);

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_ROTATEPAGESDIALOG_H
