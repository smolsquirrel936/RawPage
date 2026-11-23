// The Layers pane -- `Features.md` §5.6, Stage 7 T17, decision D7-9 (`Stage7-Assembly-Plan.md` §3).
//
// ===========================================================================================
// WHAT IT SHOWS, AND WHAT ITS TWO KINDS OF CHANGE ARE
// ===========================================================================================
// The tree `/D /Order` gives -- groups nested under groups, labelled collections as rows of their own -- then every
// group no order lists, at the top level (`core::LayerTree::shownTree`); a watermark's group never (D7-45). Each group's
// row carries an EYE -- its check box -- and says when it is locked.
//
// The eye is VIEW STATE: ticking it asks the window to show or hide the group on screen (`visibilityRequested`), which
// changes no byte and dirties nothing (D7-9). A LOCKED group's eye cannot be ticked: its visibility is the document's to
// keep. Everything else a row offers is a COMMAND -- a rename typed in place (`renameRequested`), and the menu's rows
// (`actionRequested`), each one registered action -- so the pane itself changes nothing.
//
// ===========================================================================================
// NO §8 TABLE, SO NO KeyContext
// ===========================================================================================
// `Features.md` §8 binds `Ctrl+Shift+L` to SHOW this pane (Global) and has no Layers table, so the pane declares no
// context -- `DestinationsPane`'s arrangement and reason. `F2` and `Enter` are the tree's own.

#ifndef RAWPAGE_UI_PANES_LAYERSPANE_H
#define RAWPAGE_UI_PANES_LAYERSPANE_H

#include "core/backend.h"
#include "core/cmd/ActionId.h"
#include "core/doc/LayerTree.h"

#include <QTreeWidget>

#include <functional>
#include <optional>
#include <set>

namespace rawpage::ui {

class LayersPane : public QTreeWidget {
    Q_OBJECT

public:
    explicit LayersPane(QWidget* parent = nullptr);
    ~LayersPane() override;

    // `tree` is the tab's -- the model's, or the backend's reading -- and must outlive the next call; null empties the
    // pane. `hidden` is what the screen hides now; `editable` whether a rename may be typed.
    void setDocument(core::DocumentHandle document, const core::LayerTree* tree, const std::set<core::LayerId>& hidden, bool editable);
    core::DocumentHandle document() const noexcept { return document_; }

    std::optional<core::LayerId> selectedLayer() const;
    bool selectLayer(const core::LayerId& id);
    // Starts the in-place rename of the selected group's row.
    void renameSelected();

    // The right-click menu's rows: each action's chord as it is now, and whether it may run now.
    void setChordText(std::function<QString(core::ActionId)> chord) { chordText_ = std::move(chord); }
    void setCommandEnabled(std::function<bool(core::ActionId)> enabled) { commandEnabled_ = std::move(enabled); }
    // The menu's rows, in order -- what a case asserts.
    static std::vector<core::ActionId> menuActions();

    // ---- What a case can assert, rather than a screenshot ------------------------------------------------------------
    int groupRowCount() const;                                   // rows that are groups, at every depth
    QString nameOf(const core::LayerId& id) const;               // as the row reads; empty for no row
    std::optional<bool> eyeOf(const core::LayerId& id) const;    // the eye: shown; nullopt for no row
    bool lockedRow(const core::LayerId& id) const;               // the row says locked, and its eye cannot be ticked
    // The depth of `id`'s row: 1 at the top level, more under a group or a collection; 0 for no row.
    int depthOf(const core::LayerId& id) const;
    QStringList collectionLabels() const;
    // Ticks `id`'s eye as a person's click would -- the row's check state set, the signal from it.
    bool clickEye(const core::LayerId& id);

signals:
    void visibilityRequested(const core::LayerId& id, bool shown);
    void renameRequested(const core::LayerId& id, const QString& name);
    void actionRequested(core::ActionId id);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    QTreeWidgetItem* rowOf(const core::LayerId& id) const;
    void addNodes(QTreeWidgetItem* parent, const std::vector<core::LayerNode>& nodes);

    core::DocumentHandle document_;
    const core::LayerTree* tree_ = nullptr;
    std::set<core::LayerId> hidden_;
    bool editable_ = false;
    bool filling_ = false;
    std::function<QString(core::ActionId)> chordText_;
    std::function<bool(core::ActionId)> commandEnabled_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_LAYERSPANE_H
