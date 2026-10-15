// core/doc/CommentMigrator -- comments carried from one revision of a document to the next. Stage 6 T13, decision
// D6-21 (`Stage6-Annotations-Plan.md` §3), graded on guide §3.5.
//
// ===========================================================================================
// A CONSUMER OF STAGE 5'S ALIGNMENT, NOT A SECOND COMPARER (D6-21)
// ===========================================================================================
// Which page of the revision a page of the original became is `core/text/DocumentComparer`'s answer -- its page
// alignment, the same one the comparison window scrolls by. This file takes it as given, with both documents' pages
// as the comparer read them (`PageReading`s, Stage 5's layout analysis), and decides where each comment lands:
//
//   * TEXT MARKUP -- a highlight, an underline, a strikeout, a squiggly, a redaction mark over text -- is matched BY
//     THE TEXT IT COVERS. The words under its quadrilaterals on the original page (`textUnder`) are looked for on
//     the aligned page of the revision, white space taken as one space; where they occur more than once, the
//     occurrence nearest where they were wins (*"text content first, then position"*). Each quadrilateral moves by
//     what its own first character moved -- the glyph's ORIGIN, on its baseline, which is where the text was set --
//     so a phrase that moved down the page, or to another page, is marked where it now is.
//   * EVERY OTHER COMMENT -- a note, a box, a shape, a stamp -- keeps its place RELATIVE TO THE NEAREST LINE OF TEXT:
//     the line of a text block (not a table's) whose box is nearest the comment's centre on the original page, the
//     higher on a tie, found again -- the same words, once -- on the aligned page, and the comment moved by what that
//     line's first character moved (`labels/annots-migration.json`'s `anchorRule`).
//   * A REPLY, A STATUS, A GROUP MEMBER AND A POP-UP follow the comment they belong to, moved by what it moved.
//
// ===========================================================================================
// WHAT CANNOT BE PLACED IS REPORTED, AND NEVER PLACED
// ===========================================================================================
// A page the revision deleted, words it no longer has, a line it changed: each is REPORTED by key with the comment
// it concerns, and nothing of that comment's thread is placed. A comment put on the wrong paragraph of someone
// else's revision is worse than one left out and said to be (guide A-99, S1). The placed comments are an exchange
// (`core/doc/Fdf`) -- their names, threads and fields the original's -- which `AnnotationStore::importFdf` merges into
// the revision as ONE `ImportAnnotationsCommand`.
//
// Pure: QtCore only, no engine, no display -- `tst_migrate` in `rawpage_tests` over synthetic pages, and
// `tests/render/tst_migratecorpus` over `annots-report-v1.pdf` and `compare/report-v2.pdf` as the engine reads them.

#ifndef RAWPAGE_CORE_DOC_COMMENTMIGRATOR_H
#define RAWPAGE_CORE_DOC_COMMENTMIGRATOR_H

#include "core/Result.h"
#include "core/doc/Fdf.h"
#include "core/text/DocumentComparer.h"
#include "core/text/TextSelection.h"

#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace rawpage::core {

struct MigrationEntry {
    enum class Outcome : std::uint8_t { Placed, Reported };

    Outcome outcome = Outcome::Placed;
    QString name;                                  // the comment's name in the exchange
    AnnotationSubtype subtype = AnnotationSubtype::Unknown;
    int page = -1;                                 // on the original, 0-based
    int placedPage = -1;                           // on the revision, when placed
    QString text;                                  // what a markup covers on the original; empty for any other
    std::optional<Error> why;                      // Reported: the reason, by key
};

struct MigrationResult {
    Fdf placed;                                    // every placed comment and its thread, as they land
    std::vector<MigrationEntry> entries;           // one per top-level comment, in the exchange's order

    int count(MigrationEntry::Outcome outcome) const;
};

class CommentMigrator {
public:
    // One document's pages as the comparer read them, by index; a null page is one whose text could not be read.
    using Pages = std::vector<std::shared_ptr<const text::PageReading>>;

    // `original` is the original's comments as an exchange (`AnnotationStore::exportFdf`), `base` and `revised` its
    // pages and the revision's, `alignment` the comparer's page alignment between them (`ComparisonResult::pages`).
    static MigrationResult migrate(const Fdf& original, const Pages& base, const Pages& revised,
                                   const std::vector<text::PageMatch>& alignment);

    // ---- The pieces the cases reach ----------------------------------------------------------------------------
    // The words under `quadPoints` on `page`, in reading order, white space one space: every character whose box's
    // centre is inside a quadrilateral, and everything the page reads between the first and the last.
    static QString textUnder(const text::PageReading& page, const std::vector<double>& quadPoints);
    // The page's text in reading order, a break of any kind one space -- and, for each of its characters, the reading
    // position it is (-1 for a space it stands for). What both the matching and `textUnder` read.
    struct FlatText {
        QString text;
        std::vector<int> position;
    };
    static FlatText flatten(const text::PageReading& page);
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_COMMENTMIGRATOR_H
