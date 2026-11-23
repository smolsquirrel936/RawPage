// core/doc/LayerTree -- the document's optional-content groups as the domain sees them. Stage 7 T17, decision D7-9
// (`Stage7-Assembly-Plan.md` §3): `/OCProperties`' `/OCGs`, and its default configuration `/D` -- the `/Order` tree
// (nesting and labelled collections), `/RBGroups`, `/Locked`, the default state (`/BaseState`, `/ON`, `/OFF`) and the
// usage each group declares (`/Usage`) and `/AS` applies.
//
// ---------------------------------------------------------------------------------------------
// READ FIRST-PARTY, BECAUSE NOTHING ELSE CAN
// ---------------------------------------------------------------------------------------------
// No header of PDFium's public API names an optional-content group, its state or a configuration (searched at
// planning, 2026-10-28), so `pdf/ops/LayerOps::read` is the only reading -- there is no second one to hold it to, as
// the outline and the named destinations are held (D7-10). What it cannot read whole it says: a cycle in `/Order`, a
// list past `kMaxLayers` or an `/Order` deeper than `kMaxLayerDepth` leaves a tree that is SHOWN AND READ-ONLY
// (`status()`), with what was read before the bound.
//
// ---------------------------------------------------------------------------------------------
// WHAT IS A DOCUMENT CHANGE AND WHAT IS NOT (D7-9)
// ---------------------------------------------------------------------------------------------
// A layer's name, its default state, its lock, its usage, where it sits in `/Order`, its radio group, creating one,
// deleting one and merging one into another are CHANGES TO THE DOCUMENT: commands (`core/cmd/commands/
// StructureCommands`), each handing the sink this tree WHOLE, as the outline's commands hand theirs (D7-10's shape).
// SHOWING AND HIDING ONE IS NOT: what the screen draws is VIEW STATE -- `LayerView` below -- which the engine is handed
// as a view-only update that Save never writes, and which dirties nothing (Acrobat's behaviour).
//
// ---------------------------------------------------------------------------------------------
// WHAT A DELETE AND A MERGE DO TO CONTENT (D7-9, Q2)
// ---------------------------------------------------------------------------------------------
// Nothing here rewrites a content stream: that parser is Stage 10's. A DELETED group leaves `/OCGs` and every list of
// `/D`, and the content that names it is ORPHANED -- drawn as a reader draws a group no `/OCGs` lists, which T17
// MEASURED rather than assumed (Stage 7 guide A-107 for PDFium, M-10 for Acrobat). A MERGED group leaves them too, and
// every `/Properties` resource entry -- and every `/OC` and membership dictionary -- naming it is repointed at the group
// it was merged into (`merges()`), so its content draws as that group's.
//
// ---------------------------------------------------------------------------------------------
// A WATERMARK'S GROUP IS LISTED AND NEVER SHOWN (D7-45)
// ---------------------------------------------------------------------------------------------
// A print- or screen-only watermark owns a group of its own (T15). It is in `/OCGs` -- it must be, for its usage to
// apply -- and in no `/Order`, so a reader's layers panel does not list it. It is read here, marked `decoration`, so a
// tree written whole keeps it; the Layers pane never shows it and no layer command touches it.

#ifndef RAWPAGE_CORE_DOC_LAYERTREE_H
#define RAWPAGE_CORE_DOC_LAYERTREE_H

#include "core/Result.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace rawpage::core {

// THE BOUNDS a reader, a command and a replay share -- far past any real document's, far below what takes noticeable
// time on the UI thread. A file past one is read up to it and refused for editing (see the header).
inline constexpr std::size_t kMaxLayers = 10000;
inline constexpr int kMaxLayerDepth = 32;
inline constexpr qsizetype kMaxLayerNameLength = 1024;

// WHICH GROUP. A group the file holds is named by its object number for the life of the document; one RawPage created
// by a key it keeps for its life (D6-4's rule, as `OutlineItemId`), the sink remembering the number a save gave it.
struct LayerId {
    long long object = -1;
    QString created;

    static LayerId ofObject(long long number);
    static LayerId ofCreated(QString key);

    bool isValid() const noexcept { return object > 0 || !created.isEmpty(); }
    bool isCreated() const noexcept { return !created.isEmpty(); }

    // `o:<number>` or `c:<key>` -- the form a command carries. `fromString` refuses anything else: a key of 1 ... 64
    // characters of `[0-9a-z-]`, a number of 1 ... 2^31.
    QString toString() const;
    static std::optional<LayerId> fromString(const QString& text);

    friend bool operator==(const LayerId& a, const LayerId& b) { return a.object == b.object && a.created == b.created; }
    friend bool operator!=(const LayerId& a, const LayerId& b) { return !(a == b); }
    friend bool operator<(const LayerId& a, const LayerId& b)
    {
        return a.object != b.object ? a.object < b.object : a.created < b.created;
    }
};

// A usage state (`/PrintState`, `/ViewState`, `/ExportState`): none declared, or ON or OFF.
enum class LayerUsage : std::uint8_t { Unset, On, Off };
const char* layerUsageName(LayerUsage usage) noexcept;   // `unset`, `on`, `off`
std::optional<LayerUsage> layerUsageFromName(const QString& name) noexcept;

// ONE GROUP AND WHAT THE DEFAULT CONFIGURATION SAYS OF IT.
struct Layer {
    LayerId id;
    QString name;
    bool defaultOn = true;     // `/D`'s default state: `/BaseState`, then `/ON` and `/OFF`
    bool locked = false;       // in `/D /Locked`: a viewer may not change its visibility
    // `/Usage` -- what the group declares for printing, for the screen and for export -- and whether `/D /AS` APPLIES
    // that declaration at that event. A declaration no `/AS` applies changes nothing a reader draws, so the two are
    // kept apart: a tree read and written back unchanged says exactly what the file said.
    LayerUsage print = LayerUsage::Unset;
    LayerUsage view = LayerUsage::Unset;
    LayerUsage exportUsage = LayerUsage::Unset;
    bool autoPrint = false;
    bool autoView = false;
    bool autoExport = false;
    bool decoration = false;   // a watermark's group (D7-45; see the header)

    // VISIBLE ON SCREEN WHEN THE DOCUMENT OPENS: the view usage where `/AS` applies it, else the default state.
    bool shownAtOpen() const noexcept { return autoView && view != LayerUsage::Unset ? view == LayerUsage::On : defaultOn; }

    friend bool operator==(const Layer& a, const Layer& b)
    {
        return a.id == b.id && a.name == b.name && a.defaultOn == b.defaultOn && a.locked == b.locked && a.print == b.print
               && a.view == b.view && a.exportUsage == b.exportUsage && a.autoPrint == b.autoPrint && a.autoView == b.autoView
               && a.autoExport == b.autoExport && a.decoration == b.decoration;
    }

    // `{"id", "name", "on", "locked", "print", "view", "export"}` -- what a create carries; the `auto*` flags follow the
    // usage (a usage set is applied). `fromJson` is a parser of hostile input (a journal line), bounded.
    QJsonObject toJson() const;
    static Result<Layer> fromJson(const QJsonObject& json);
};

// ONE ENTRY OF `/D /Order`: a group with the groups nested under it, or a COLLECTION -- an array whose first element is
// a text string (labelled) or one following no group (unlabelled) -- which groups without being a group.
struct LayerNode {
    LayerId layer;            // invalid: a collection
    QString label;            // a collection's label; empty for an unlabelled one
    std::vector<LayerNode> children;

    bool isCollection() const noexcept { return !layer.isValid(); }
    friend bool operator==(const LayerNode& a, const LayerNode& b)
    {
        return a.layer == b.layer && a.label == b.label && a.children == b.children;
    }
};

// WHAT A PROPERTIES COMMAND SETS (D7-9: name, lock, default state, print and export usage, grouping, radio groups).
struct LayerProperties {
    QString name;
    bool defaultOn = true;
    bool locked = false;
    LayerUsage print = LayerUsage::Unset;
    LayerUsage view = LayerUsage::Unset;
    LayerUsage exportUsage = LayerUsage::Unset;
    // GROUPING: the group it is nested under -- invalid for the top level. A collection is transparent: a layer inside
    // a labelled collection at the top level has no parent, and stays where it is unless this names another.
    LayerId parent;
    // ITS RADIO GROUP: the layers it is mutually exclusive with, after the command -- empty for none. The layer leaves
    // every group it was in, so do these, and the layer and these become one group.
    std::vector<LayerId> exclusiveWith;

    static LayerProperties of(const class LayerTree& tree, const LayerId& id);
    friend bool operator==(const LayerProperties& a, const LayerProperties& b)
    {
        return a.name == b.name && a.defaultOn == b.defaultOn && a.locked == b.locked && a.print == b.print && a.view == b.view
               && a.exportUsage == b.exportUsage && a.parent == b.parent && a.exclusiveWith == b.exclusiveWith;
    }
    QJsonObject toJson() const;
    static Result<LayerProperties> fromJson(const QJsonObject& json);
};

class LayerTree {
public:
    // Every group in `/OCGs` order, watermarks' included.
    const std::vector<Layer>& layers() const noexcept { return layers_; }
    std::vector<Layer>& layers() noexcept { return layers_; }
    const std::vector<LayerNode>& order() const noexcept { return order_; }
    std::vector<LayerNode>& order() noexcept { return order_; }
    const std::vector<std::vector<LayerId>>& radioGroups() const noexcept { return radioGroups_; }
    std::vector<std::vector<LayerId>>& radioGroups() noexcept { return radioGroups_; }
    // MERGED, in the order made: each group gone into another (see the header). What the sink repoints.
    const std::vector<std::pair<LayerId, LayerId>>& merges() const noexcept { return merges_; }
    std::vector<std::pair<LayerId, LayerId>>& merges() noexcept { return merges_; }
    // `/BaseState /OFF`: every group off unless `/ON` names it. Kept as read, so `/ON` and `/OFF` are written as before.
    bool baseOff() const noexcept { return baseOff_; }
    void setBaseOff(bool off) noexcept { baseOff_ = off; }

    bool isEmpty() const noexcept { return layers_.empty(); }
    // The groups a person sees and edits -- every one but a watermark's.
    std::size_t visibleCount() const;

    // WHETHER IT MAY BE EDITED: success, or the refusal -- nothing read it (an encrypted file), or what was read stopped
    // at a bound (see the header). A default tree is an assessed, empty one.
    const Status& status() const noexcept { return status_; }
    void setStatus(Status status) { status_ = std::move(status); }
    static LayerTree refused(Error why);

    const Layer* find(const LayerId& id) const;
    Layer* find(const LayerId& id);
    // The node `id` is in `/Order`; null for a group the order does not list.
    const LayerNode* nodeOf(const LayerId& id) const;
    // The nearest GROUP above `id` in `/Order` -- collections are transparent; invalid for none.
    LayerId parentOf(const LayerId& id) const;
    // True when `id` is `ancestor` or lies under it in `/Order`.
    bool isWithin(const LayerId& id, const LayerId& ancestor) const;
    // THE PANE'S ROWS: `/Order`, then each group no order lists, at the top level -- a watermark's never.
    std::vector<LayerNode> shownTree() const;
    // The radio group `id` is in, other members only; empty for none.
    std::vector<LayerId> exclusiveWith(const LayerId& id) const;

    // ---- The editing half: each refused changing nothing ------------------------------------------------------------
    // A new group, under `parent` (invalid: the top level) at the end. Refused: an id here already or invalid, a parent
    // not here, a name a command may not write, a tree past the bounds.
    Status insert(Layer layer, const LayerId& parent);
    // `id` out of `/OCGs` and every list; its children in `/Order` take its place. Refused: an id not here, a watermark's.
    Status remove(const LayerId& id);
    // `from` gone into `into` (see the header): `from` removed, the merge recorded. Refused: either not here, the same
    // group, a watermark's.
    Status merge(const LayerId& from, const LayerId& into);
    Status setProperties(const LayerId& id, const LayerProperties& properties);

    // The bounds; every id valid and unique; every id the order, a radio group and a merge names present (a merge's
    // `from` absent, its `into` present or itself merged on); no group twice in the order; no name past its bound.
    Status validate() const;

    // Every group, every field, the order, the radio groups and the merges, in order, and whether it may be edited --
    // `Document::modelHash`'s input.
    QByteArray digest() const;

private:
    std::vector<Layer> layers_;
    std::vector<LayerNode> order_;
    std::vector<std::vector<LayerId>> radioGroups_;
    std::vector<std::pair<LayerId, LayerId>> merges_;
    bool baseOff_ = false;
    Status status_;
};

// A name a command may give a layer: 1 ... `kMaxLayerNameLength` characters, not all white space, no control character.
bool isWritableLayerName(const QString& name);

// =====================================================================================================================
// LayerView -- WHAT THE SCREEN DRAWS: view state, not the document's (D7-9; see the header)
// =====================================================================================================================
// The set of groups HIDDEN on screen, beginning as the document opens them and changed only by a person showing or
// hiding one. A command never changes it; one that removes a group leaves a stale id here, which names nothing.
namespace LayerView {

// The groups hidden when the document opens: each whose `shownAtOpen` is false.
std::set<LayerId> atOpen(const LayerTree& tree);

// `id` shown or hidden in `hidden`. Showing a member of a radio group hides the group's others. Refused, changing
// nothing: a group not here (`error.layer.notFound`), a LOCKED one (`error.layer.locked` -- its visibility is the
// document's to keep), a watermark's.
Result<std::set<LayerId>> setShown(const LayerTree& tree, std::set<LayerId> hidden, const LayerId& id, bool shown);

// THE RENDER KEY (`RenderRequest::hiddenLayers`): two views draw the same pixels exactly when their keys are equal. A
// group the file holds is its object number; one created is a negative number from its key. Sorted.
std::vector<long long> key(const std::set<LayerId>& hidden);

} // namespace LayerView

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_LAYERTREE_H
