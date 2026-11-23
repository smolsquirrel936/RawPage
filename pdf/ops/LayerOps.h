// pdf/ops/LayerOps -- the document's optional content, read and written first-party. Stage 7 T17, decision D7-9
// (`Stage7-Assembly-Plan.md` §3); the model is `core/doc/LayerTree`, whose header says what is a document change and
// what is view state.
//
// ===========================================================================================
// READ: THE ONLY READING (`read`)
// ===========================================================================================
// `/OCProperties /OCGs` in order, each group's `/Name` and `/Usage`, and the default configuration `/D` -- `/BaseState`,
// `/ON`, `/OFF`, `/Locked`, `/RBGroups`, the `/AS` applications and the `/Order` tree. Hostile input is bounded, not
// trusted: an `/Order` array that contains itself (directly or through others), one deeper than `kMaxLayerDepth` and a
// `/OCGs` longer than `kMaxLayers` are read up to the bound and the tree is READ-ONLY, saying which
// (`error.layer.orderCycle`, `error.layer.tooDeep`, `error.layer.tooMany`). A group named by `/Order` or `/D`'s lists
// and not in `/OCGs` is not a group of the document and is skipped; a group listed twice in `/Order` is shown once.
//
// ===========================================================================================
// COMPOSE: THE MODEL WHOLE, AND WHAT IT DOES NOT KNOW KEPT (`compose`)
// ===========================================================================================
// `/OCGs` and the lists of `/D` written from the model -- its groups, then every group listed now that the model
// neither holds nor removed (`removed`: deleted and merged), in the order listed, each keeping its place in `/ON`,
// `/OFF`, `/Locked`, `/RBGroups` and `/AS` and its top-level `/Order` entries, which follow the model's. That second
// kind is what a page inserted in this session brought with it (Stage 7 guide I-07), or what a watermark made: the
// model is read at open and only layer commands change it, so a group it never heard of is somebody else's to keep.
// Every key of `/D` and `/OCProperties` this file does not write is kept as it was.
//
// ===========================================================================================
// MERGE: EVERY REFERENCE TO THE MERGED GROUP (`repointed`)
// ===========================================================================================
// A reference to a group means that group wherever it is: a `/Properties` resource entry (what marked content names),
// an XObject's or an annotation's `/OC`, an optional-content membership dictionary's `/OCGs`, an alternate
// configuration's lists. Merging repoints them ALL at the group merged into -- D7-9's *every `/Properties` entry*,
// and the others for the same reason. No content stream is touched: its marked content names a resource key, and the
// key now resolves to the survivor.
//
// ===========================================================================================
// VIEW: A SECTION SAVE NEVER WRITES (`viewSection`)
// ===========================================================================================
// What the screen hides is handed to the engine as one more incremental section after the document's own: `/D` with
// `/BaseState /ON`, `/OFF` the hidden groups and no `/ON`, and every `/AS` application of the `View` event taken out --
// which would otherwise put a print-only group back off however a person showed it. PDFium draws optional content from
// the default configuration and nothing else (no public API names a group), so this is the one way to say it.

#ifndef RAWPAGE_PDF_OPS_LAYEROPS_H
#define RAWPAGE_PDF_OPS_LAYEROPS_H

#include "core/Result.h"
#include "core/doc/LayerTree.h"
#include "pdf/cos/CosObject.h"
#include "pdf/cos/XrefTable.h"

#include <QByteArray>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace rawpage::pdf::ops {

class LayerOps {
public:
    // The tree `table`'s document holds, every group named by its object number; an empty tree for a document with no
    // `/OCProperties`. See the header for what is refused.
    static core::LayerTree read(cos::XrefTable& table);

    // One group's name, usage and mark from its dictionary -- the id and `/D`'s fields left as the defaults.
    static core::Layer layerOf(cos::XrefTable& table, const cos::CosObject& dictionary);
    // `layer`'s name and usage written into `base` (a group's dictionary as it is, or empty for a new one), every other
    // key kept; `/Type /OCG` always.
    static cos::CosObject groupDictionary(const core::Layer& layer, const cos::CosObject& base);

    // THE COMPOSE (see the header). `properties` and `defaults` are `/OCProperties` and its `/D` as they stand now,
    // resolved; `refOf` answers each model group's reference (a created one's new number). Returns the `/OCGs` array and
    // the `/D` to write -- or the refusal of a model group with no reference.
    struct Composed {
        cos::CosObject groups;
        cos::CosObject defaults;
    };
    using RefOf = std::function<std::optional<cos::CosObject>(const core::LayerId&)>;
    static core::Result<Composed> compose(cos::XrefTable& table, const cos::CosObject& properties, const cos::CosObject& defaults,
                                          const core::LayerTree& model, const std::set<long long>& removed, const RefOf& refOf);

    // `value` with every reference to a number in `into` replaced by its mapped reference, at any depth; nullopt when it
    // names none. A stream's dictionary only -- its data is not looked at.
    static std::optional<cos::CosObject> repointed(const cos::CosObject& value, const std::map<long long, cos::CosObject>& into);
    // Every object `table` places whose value names one of `groups` -- what a merge may have to write. One walk of the
    // file; the groups' own dictionaries and the `/OCProperties` objects are left out (the compose writes those).
    static std::set<long long> referencing(cos::XrefTable& table, const std::set<long long>& groups);

    // THE VIEW SECTION (see the header) appended to `composed` -- the newest file on disk followed by its update. `hidden`
    // are object numbers in that file; one it does not list is skipped. `protect` re-writes what an append could change
    // the meaning of (`IncrementalWriter::protectOriginal`), needed only when `composed` ends with the file's own bytes.
    // Empty for a document with no `/OCProperties`.
    static core::Result<QByteArray> viewSection(std::string_view composed, const std::vector<long long>& hidden, bool protect);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_LAYEROPS_H
