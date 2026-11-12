// ui/dialogs/LinkPropertiesDialog -- where a link goes and how it looks; a named destination's name; the question asked
// before a link opens anything outside the document. Stage 7 T12 (`Stage7-Assembly-Plan.md` D7-12, D7-35; `Features.md`
// §5.10).
//
// EVERY DIALOG COLLECTS PARAMETERS (CLAUDE.md): `LinkPropertiesDialog` answers an action and an appearance, and the window
// makes the command -- an `AddAnnotationCommand` for a new link, an `EditAnnotationCommand` for one edited. A target is
// in `core/doc/Outline`'s vocabulary, so a link offers what a bookmark does and more: a named destination, the form
// actions. **Written, never run** (D7-12): choosing a script here writes a script.
//
// `LinkOpenDialog` is the one question D7-12 asks before a link opens a web address, another PDF or a file -- with nobody
// to answer it (the offscreen platform, a case with no answer installed) the answer is NO, and nothing is opened.
//
// Each has `setAnswerForTesting`, as T11's dialogs do: a case installs what a person would have answered.

#ifndef RAWPAGE_UI_DIALOGS_LINKPROPERTIESDIALOG_H
#define RAWPAGE_UI_DIALOGS_LINKPROPERTIESDIALOG_H

#include "core/doc/Annotation.h"
#include "core/doc/Destinations.h"
#include "core/doc/LinkActions.h"
#include "core/doc/Outline.h"

#include <QDialog>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

namespace rawpage::ui {

class LinkPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        core::OutlineAction action;          // where it goes -- written, never run
        double width = 0.0;                  // 0: invisible
        QString style = QStringLiteral("S");   // S, D, U, B or I (ISO 32000-1 Table 166)
        std::vector<double> colour;          // RGB; empty with no border
        QString highlight;                   // N, I, O or P; empty for the default, invert

        // `link` with this applied -- its target, border and highlight.
        core::Annotation appliedTo(core::Annotation link) const;
        static Choice of(const core::Annotation& link);
    };

    // `link`: the link as it is (a new one: as `newLink` made it); `names`: the document's named destinations;
    // `currentView`: what *Go to the current view* means; `pageCount`: the pages a destination may name.
    LinkPropertiesDialog(const core::Annotation& link, int pageCount, const QStringList& names, const core::Destination& currentView,
                         QWidget* parent = nullptr);
    Choice choice() const;

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, const core::Annotation& link, int pageCount, const QStringList& names,
                                     const core::Destination& currentView);
    using Answer = std::function<std::optional<Choice>(const core::Annotation& link, int pageCount, const QStringList& names,
                                                       const core::Destination& currentView)>;
    static void setAnswerForTesting(Answer answer);

private:
    void showFieldsFor(int kind);

    core::Annotation link_;
    core::Destination currentView_;
    QComboBox* target_ = nullptr;
    QSpinBox* page_ = nullptr;
    QComboBox* named_ = nullptr;
    QLineEdit* text_ = nullptr;
    QCheckBox* visible_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QComboBox* style_ = nullptr;
    QComboBox* colour_ = nullptr;
    QComboBox* highlight_ = nullptr;
};

// A named destination's name -- a new one's, or a rename's. Nothing when cancelled or nobody can answer.
class DestinationNameDialog {
public:
    static bool canAsk();
    static std::optional<QString> ask(QWidget* parent, const QString& title, const QString& initial);
    using Answer = std::function<std::optional<QString>(const QString& initial)>;
    static void setAnswerForTesting(Answer answer);
};

// THE QUESTION BEFORE A LINK OPENS ANYTHING (D7-12): a web address, another PDF, a file. True only when a person said yes.
class LinkOpenDialog {
public:
    static bool ask(QWidget* parent, core::LinkFollow::Step step, const QString& what);
    using Answer = std::function<bool(core::LinkFollow::Step step, const QString& what)>;
    static void setAnswerForTesting(Answer answer);
};

// The verify's report: each broken link, in words. Shown to a person; nothing with nobody to show it to.
class LinkReportDialog {
public:
    static void show(QWidget* parent, const QStringList& lines);
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_LINKPROPERTIESDIALOG_H
