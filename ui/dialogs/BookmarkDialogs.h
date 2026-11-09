// ui/dialogs/BookmarkDialogs -- the questions the bookmarks' commands ask. Stage 7 T11 (`Stage7-Assembly-Plan.md`
// D7-10, D7-11; `Features.md` §5.4).
//
// EVERY DIALOG COLLECTS PARAMETERS AND NOTHING ELSE (CLAUDE.md): what each answers, the window turns into commands
// (`ui/shell/MainWindowBookmarks.cpp`) -- a delete's choice about children, a bookmark's properties and target, a
// generator's mode, a bookmark file's path. None reads a page or writes a byte.
//
// NOBODY TO ASK (convention 16). Under the offscreen platform a modal dialog is shown to no one; `canAsk()` is false
// there unless a test has installed an answer, and an action that needs one is DISABLED rather than blocking -- the
// pattern `ui/dialogs/PageOpsDialogs` keeps.

#ifndef RAWPAGE_UI_DIALOGS_BOOKMARKDIALOGS_H
#define RAWPAGE_UI_DIALOGS_BOOKMARKDIALOGS_H

#include "core/cmd/commands/BookmarkCommands.h"
#include "core/doc/Outline.h"
#include "core/doc/OutlineExchange.h"
#include "core/doc/OutlineGenerator.h"

#include <QDialog>
#include <QString>

#include <functional>
#include <optional>
#include <utility>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace rawpage::ui {

// --- Delete: what becomes of the children -------------------------------------------------------------------------

class DeleteBookmarkDialog {
public:
    // True: keep the children, promoted into the bookmark's place; false: delete them with it; none: cancelled.
    static bool canAsk();
    static std::optional<bool> ask(QWidget* parent, const QString& title, int children);
    using Answer = std::function<std::optional<bool>(const QString& title, int children)>;
    static void setAnswerForTesting(Answer answer);
};

// --- Properties: title, colour, style, open state, target ---------------------------------------------------------

class BookmarkPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        core::EditBookmarkCommand::Properties properties;
        core::OutlineAction action;   // what the bookmark does -- written, never run (D7-12)
    };

    // `currentView`: what *Go to the current view* means; `pageCount`: the pages a destination may name.
    BookmarkPropertiesDialog(const core::OutlineItem& item, int pageCount, const core::Destination& currentView,
                             QWidget* parent = nullptr);
    Choice choice() const;

    static bool canAsk();
    static std::optional<Choice> ask(QWidget* parent, const core::OutlineItem& item, int pageCount,
                                     const core::Destination& currentView);
    using Answer = std::function<std::optional<Choice>(const core::OutlineItem& item, int pageCount, const core::Destination& currentView)>;
    static void setAnswerForTesting(Answer answer);

private:
    void showFieldsFor(int kind);

    core::OutlineItem item_;
    core::Destination currentView_;
    QLineEdit* title_ = nullptr;
    QCheckBox* coloured_ = nullptr;
    QComboBox* colour_ = nullptr;
    QCheckBox* bold_ = nullptr;
    QCheckBox* italic_ = nullptr;
    QCheckBox* open_ = nullptr;
    QComboBox* target_ = nullptr;
    QLineEdit* text_ = nullptr;
    QSpinBox* page_ = nullptr;
};

// --- Generate: from headings, by font size and style, by a pattern ------------------------------------------------

class GenerateBookmarksDialog : public QDialog {
    Q_OBJECT

public:
    explicit GenerateBookmarksDialog(QWidget* parent = nullptr);
    core::OutlineGeneratorOptions choice() const;

    static bool canAsk();
    static std::optional<core::OutlineGeneratorOptions> ask(QWidget* parent);
    using Answer = std::function<std::optional<core::OutlineGeneratorOptions>()>;
    static void setAnswerForTesting(Answer answer);

private:
    QRadioButton* headings_ = nullptr;
    QRadioButton* fonts_ = nullptr;
    QRadioButton* pattern_ = nullptr;
    QDoubleSpinBox* minimum_ = nullptr;
    QCheckBox* boldOnly_ = nullptr;
    QLineEdit* expression_ = nullptr;
};

// --- Import and export: the bookmark file's path, and the syntax an export writes ---------------------------------

class BookmarkFileDialog {
public:
    static bool canAsk();
    static std::optional<QString> askImport(QWidget* parent);
    static std::optional<std::pair<QString, core::OutlineFormat>> askExport(QWidget* parent);
    using ImportAnswer = std::function<std::optional<QString>()>;
    using ExportAnswer = std::function<std::optional<std::pair<QString, core::OutlineFormat>>()>;
    static void setAnswersForTesting(ImportAnswer importAnswer, ExportAnswer exportAnswer);
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_BOOKMARKDIALOGS_H
