// ui/dialogs/CompareDialog -- what `Ctrl+Alt+C` opens: the two documents to compare and how. Stage 5 T13,
// decisions D5-17, D5-18 and D5-20 (`Stage5-TextSearch-Plan.md` §3), guide case A-103.
//
// THE RULE IT LIVES UNDER (CLAUDE.md): *every dialog collects parameters.* It answers a `Choice` -- the
// older and the newer file, the sensitivity and the three ignore-lists -- and holds no document: the window
// writes the four options to their settings (D5-14's rule: the keys ARE the options, so the dialog, the
// comparison's bar and Preferences cannot disagree) and opens the comparison over the two paths.
//
// EITHER DOCUMENT MAY BE OPEN OR NOT (plan §5.5, "open tabs or paths"): each field offers the open tabs' files
// and takes any path typed or browsed to. The older defaults to the tab in front and the newer to the next
// tab, so comparing two open revisions is `Ctrl+Alt+C`, `Enter`.
//
// THE NEWER REMEMBERS (the user's request, 2026-10-04, S5 guide A-126): where the older file was compared
// before, the newer defaults instead to the file it was last compared against -- `core/settings/ComparePairs`,
// asked through `rememberedNewer` so the dialog holds no settings -- when that file still exists. It FOLLOWS
// THE OLDER: picking, browsing to or typing another older file offers that file's pair, or the default again,
// until the newer is chosen by hand; a newer a person picked, typed or browsed to is never replaced.
//
// NOBODY TO ASK (convention 16). Offscreen a modal dialog is shown to no one and never returns (Stage 1's
// I-23): `canAsk()` is false there unless a test has installed an answer, and `compare.documents` is then
// DISABLED rather than blocking. `setAnswerForTesting` is the parameterized twin a headless case drives; the
// widget itself is built and driven by `tst_comparison`'s own case.

#ifndef RAWPAGE_UI_DIALOGS_COMPAREDIALOG_H
#define RAWPAGE_UI_DIALOGS_COMPAREDIALOG_H

#include "core/text/DocumentComparer.h"

#include <QDialog>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QPushButton;

namespace rawpage::ui {

class CompareDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        QString olderPath;
        QString newerPath;
        core::text::CompareOptions options;   // the four that are settings; the rest at their defaults
    };

    // The newer file last compared against an older one, or an empty string (the header).
    using RememberedNewer = std::function<QString(const QString& older)>;

    // `openPaths` are the open tabs' files, the tab in front first; `options` what the four keys hold now.
    CompareDialog(const QStringList& openPaths, const core::text::CompareOptions& options, QWidget* parent = nullptr,
                  RememberedNewer rememberedNewer = {});

    Choice choice() const;

    // For a case that drives the widget itself.
    QComboBox* olderField() const { return older_; }
    QComboBox* newerField() const { return newer_; }
    QComboBox* sensitivityField() const { return sensitivity_; }
    QCheckBox* ignoreHeaderFooterBox() const { return ignoreHeaderFooter_; }
    QCheckBox* ignorePageNumbersBox() const { return ignorePageNumbers_; }
    QCheckBox* ignoreWhitespaceBox() const { return ignoreWhitespace_; }
    QPushButton* okButton() const;

    // Whether anyone can answer: a person on a real platform, or a test's installed answer.
    static bool canAsk();

    // Shows the dialog and returns the choice, or nothing for Cancel -- or the test's answer, if one is
    // installed. Nothing, immediately, when `canAsk()` is false.
    static std::optional<Choice> ask(QWidget* parent, const QStringList& openPaths,
                                     const core::text::CompareOptions& options, RememberedNewer rememberedNewer = {});

    using Answer = std::function<std::optional<Choice>()>;
    static void setAnswerForTesting(Answer answer);

private:
    void browse(QComboBox* field);
    void updateOk();
    void followOlder();

    QComboBox*        older_ = nullptr;
    QComboBox*        newer_ = nullptr;
    QComboBox*        sensitivity_ = nullptr;
    QCheckBox*        ignoreHeaderFooter_ = nullptr;
    QCheckBox*        ignorePageNumbers_ = nullptr;
    QCheckBox*        ignoreWhitespace_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    RememberedNewer   rememberedNewer_;
    QString           defaultNewer_;           // the next tab's file, or nothing
    bool              newerChosen_ = false;    // picked, typed or browsed to by a person
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_COMPAREDIALOG_H
