// core/doc/Merge -- many PDFs made one: what is asked for, and what comes back. Stage 7 T10, decision D7-16
// (`Stage7-Assembly-Plan.md` §3); guide cases A-66 ... A-72 (`Stage7-Assembly-Testing-Guide.md` §5.12).
//
// -------------------------------------------------------------------------------------------
// A MERGE IS NOT A COMMAND
// -------------------------------------------------------------------------------------------
// It changes no open document (D7-5): the inputs are FILES, read by the layer that owns the format
// (`IDocumentBackend::mergeFiles`, `pdf/ops/DocumentMerger`), and the result is a new document in memory -- one
// revision, verified -- which the window opens as an untitled tab, read-only until it is Saved As, as an extract's is
// (D7-30(b), kept for merge by the user's choice at T10).
//
// WHAT COMES WITH EACH FILE (D7-16): its pages through the copier, with their comments and with the links between
// them; its OUTLINE, every destination repointed at the copy, under a bookmark of its own when `bookmarks` asks for
// one; its FORM FIELDS, a top-level name another file already used renamed (`name` -> `name_2`) or merged on purpose
// into one field, as `renameCollidingFields` says, and either way reported; and its LAYERS, its groups added beside
// every other file's and its `/Order` gathered under a label of its own. An input the merge cannot take is SKIPPED and
// reported with the reason, by key -- the others are merged.
//
// Pure: QtCore and the standard library -- `rawpage_tests`, under `portable-asan` too.

#ifndef RAWPAGE_CORE_DOC_MERGE_H
#define RAWPAGE_CORE_DOC_MERGE_H

#include "core/doc/MergeBookmarks.h"
#include "core/doc/PageMap.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <vector>

namespace rawpage::core {

// One file to merge: its pages, zero-based and strictly ascending -- EMPTY for every page it has.
struct MergeInput {
    QString path;
    std::vector<int> pages;
    friend bool operator==(const MergeInput&, const MergeInput&) = default;
};

struct MergeOptions {
    MergeBookmarks bookmarks = MergeBookmarks::FileName;   // `documents/mergeBookmarkPerFile`
    bool renameCollidingFields = true;                     // `documents/renameCollidingFields`; false: merged on purpose
    bool separatorPages = false;                           // one blank page between inputs, at the size of the page before it
    friend bool operator==(const MergeOptions&, const MergeOptions&) = default;
};

// An input not merged, and why -- the reason a catalog key: `error.open.notPdf`, `error.open.notFound`,
// `error.pdf.encrypted`, `error.merge.repairedWithLoss`, `error.pdf.pageTreeDamaged`, `error.pdf.pageOutOfRange`,
// `error.copy.tooLarge`, or the reader's own refusal.
struct MergeSkip {
    int input = -1;
    QString path;
    QString reasonKey;
    QString context;
};

// A top-level field name an earlier input already used. `renamedTo` empty: merged into the earlier one.
struct FieldCollision {
    QString name;
    QString renamedTo;
    int input = -1;
    bool merged() const noexcept { return renamedTo.isEmpty(); }
};

struct MergeReport {
    int inputsMerged = 0;
    int pageCount = 0;
    int separators = 0;
    std::vector<MergeSkip> skipped;
    std::vector<FieldCollision> fields;
    // Per input, parallel to the inputs given: what its copy met (a link or a bookmark whose page was left out of the
    // range and lost its destination, a tagged page arriving untagged). Empty for an input skipped.
    std::vector<PageCommandReport> references;
};

struct MergedDocument {
    QByteArray bytes;
    MergeReport report;
};

// Called between steps -- an input read and copied, the document built, the document verified: `done` of `total`.
// Answering false stops the merge at that step, refused by `error.merge.cancelled`.
using MergeProgress = std::function<bool(int done, int total)>;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_MERGE_H
