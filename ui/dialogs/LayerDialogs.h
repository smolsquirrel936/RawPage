// ui/dialogs/LayerDialogs -- the questions the layer commands ask. Stage 7 T17, decision D7-9 (`Stage7-Assembly-Plan.md`).
//
// Each COLLECTS PARAMETERS and constructs nothing (CLAUDE.md's standing rule): the name of a new group, the group to merge
// into, a group's properties -- `ui/shell/MainWindowLayers.cpp` turns the answer into a command. Each answers `nullopt`
// with nobody to ask (convention 16 -- the action is disabled there, `canAsk`), and a case installs its answer.

#ifndef RAWPAGE_UI_DIALOGS_LAYERDIALOGS_H
#define RAWPAGE_UI_DIALOGS_LAYERDIALOGS_H

#include "core/doc/LayerTree.h"

#include <QDialog>

#include <functional>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;

namespace rawpage::ui {

// A NEW GROUP'S NAME -- `initial` suggested.
class LayerNameDialog {
public:
    static bool canAsk();
    static std::optional<QString> ask(QWidget* parent, const QString& title, const QString& initial);
    using Answer = std::function<std::optional<QString>(const QString& initial)>;
    static void setAnswerForTesting(Answer answer);
};

// WHICH GROUP `from` GOES INTO -- every other group a person sees, by name.
class LayerMergeDialog {
public:
    static bool canAsk();
    static std::optional<core::LayerId> ask(QWidget* parent, const core::LayerTree& tree, const core::LayerId& from);
    using Answer = std::function<std::optional<core::LayerId>(const std::vector<core::LayerId>& choices)>;
    static void setAnswerForTesting(Answer answer);
    // The groups offered, in the order the combo lists them: the tree's, a watermark's and `from` left out.
    static std::vector<core::LayerId> choices(const core::LayerTree& tree, const core::LayerId& from);
};

// ONE GROUP'S PROPERTIES (D7-9): name, default state, lock, usage when printed, on screen and when exported, the group it
// is nested under, the groups it is exclusive with.
class LayerPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    LayerPropertiesDialog(const core::LayerTree& tree, const core::LayerId& id, QWidget* parent = nullptr);
    core::LayerProperties properties() const;

    QLineEdit* nameField() const noexcept { return name_; }
    QCheckBox* shownBox() const noexcept { return shown_; }
    QCheckBox* lockedBox() const noexcept { return locked_; }
    QComboBox* printBox() const noexcept { return print_; }
    QComboBox* viewBox() const noexcept { return view_; }
    QComboBox* exportBox() const noexcept { return export_; }
    QComboBox* parentBox() const noexcept { return parent_; }
    QListWidget* exclusiveList() const noexcept { return exclusive_; }

    static bool canAsk();
    static std::optional<core::LayerProperties> ask(QWidget* parent, const core::LayerTree& tree, const core::LayerId& id);
    using Answer = std::function<std::optional<core::LayerProperties>(const core::LayerProperties& initial)>;
    static void setAnswerForTesting(Answer answer);

private:
    std::vector<core::LayerId> parents_;      // the parent combo's rows after the first (the top level)
    std::vector<core::LayerId> exclusives_;   // the list's rows
    QLineEdit* name_ = nullptr;
    QCheckBox* shown_ = nullptr;
    QCheckBox* locked_ = nullptr;
    QComboBox* print_ = nullptr;
    QComboBox* view_ = nullptr;
    QComboBox* export_ = nullptr;
    QComboBox* parent_ = nullptr;
    QListWidget* exclusive_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_DIALOGS_LAYERDIALOGS_H
