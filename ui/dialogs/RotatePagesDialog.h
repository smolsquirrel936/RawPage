// ui/dialogs/RotatePagesDialog -- what `Ctrl+R` opens: a direction and a page set, and nothing else.
// Stage 3 T10, decision D3-13 (`Stage3-Mutation-Plan.md` §3).
//
// THE RULE IT LIVES UNDER (CLAUDE.md): *every dialog collects parameters and constructs a `Command`.* This
// one holds no `Document` and no journal. It answers a `Choice`; `planRotation` turns a choice into the
// commands -- one `RotatePageCommand` for the current page, or one per page inside ONE group for all of
// them, which is the first production caller of `beginGroup`/`endGroup` -- and `MainWindow` runs them.
//
// TWO PAGE CHOICES, NOT A RANGE. Ranges, odd/even and orientation filters are Stage 7's (D3-13); the
// dialog Features.md §8 names is brought forward only as far as this stage needs it.
//
// NOBODY TO ASK (convention 16). Under the offscreen platform a modal dialog is shown to no one and never
// returns (Stage 1's I-23). `canAsk()` is false there unless a test has installed an answer, and
// `page.rotate` is DISABLED rather than blocking. `setAnswerForTesting` is the parameterized twin a
// headless case drives; the widget itself is still built and driven by A-73's case.

#ifndef RAWPAGE_UI_DIALOGS_ROTATEPAGESDIALOG_H
#define RAWPAGE_UI_DIALOGS_ROTATEPAGESDIALOG_H

#include "core/cmd/Command.h"

#include <QDialog>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QRadioButton;

namespace rawpage::ui {

class RotatePagesDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        int degrees = 90;        // clockwise: 90, 180 or 270 ("90 degrees counter-clockwise")
        bool allPages = false;
        friend bool operator==(const Choice&, const Choice&) = default;
    };

    // `currentPage` is zero-based: the page "current page" rotates, named on its button as *"Current page
    // (page n)"* so a person need not guess which page the viewer counts as current (M-01's walk).
    explicit RotatePagesDialog(int currentPage, QWidget* parent = nullptr);

    Choice choice() const;

    // For a case that drives the widget itself.
    QRadioButton* clockwiseButton() const { return clockwise_; }
    QRadioButton* halfTurnButton() const { return half_; }
    QRadioButton* counterClockwiseButton() const { return counterClockwise_; }
    QRadioButton* currentPageButton() const { return currentPage_; }
    QRadioButton* allPagesButton() const { return allPages_; }

    // Whether anyone can answer: a person on a real platform, or a test's installed answer.
    static bool canAsk();

    // Shows the dialog and returns the choice, or nothing for Cancel -- or the test's answer, if one is
    // installed. Nothing, immediately, when `canAsk()` is false.
    static std::optional<Choice> ask(QWidget* parent, int currentPage);

    using Answer = std::function<std::optional<Choice>()>;
    static void setAnswerForTesting(Answer answer);

private:
    QRadioButton* clockwise_ = nullptr;
    QRadioButton* half_ = nullptr;
    QRadioButton* counterClockwise_ = nullptr;
    QRadioButton* currentPage_ = nullptr;
    QRadioButton* allPages_ = nullptr;
};

// The commands a choice means, for a document of `pageCount` pages whose current page is `currentPage`.
struct RotationPlan {
    std::optional<core::CatalogText> group;              // set when there is more than one command
    std::vector<std::unique_ptr<core::Command>> commands;
};
RotationPlan planRotation(const RotatePagesDialog::Choice& choice, int currentPage, int pageCount);

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_ROTATEPAGESDIALOG_H
