// core/doc/Permissions -- may this document be changed, and if not, why not. Stage 3 T4 builds the
// PRE-FLIGHT half; the permission bits of an encrypted document are Stage 10's (decision D3-7,
// `Stage3-Mutation-Plan.md` §3).
//
// ONE PREDICATE, `may(Operation)`, that every command asks before it runs -- `CommandJournal::run`
// asks it first (T5), so a refusal leaves no trace. Stage 10 adds the permission bits to the same
// predicate rather than threading a second check through every command.
//
// -------------------------------------------------------------------------------------------
// WHAT STAGE 3 REFUSES, each by its own catalog key
// -------------------------------------------------------------------------------------------
//   * `error.edit.encrypted` -- the first-party reader declines an encrypted document (D2-6), so
//     there is no object model to write through. Refused UP FRONT, not at Save: a user who rotates
//     twenty pages and is then told the file cannot be saved has lost the work by design.
//   * `error.edit.noPageMap` -- the first-party reader could not walk a page tree at all.
//   * `error.edit.pageCountDisagrees`, `error.edit.pageRotationDisagrees`,
//     `error.edit.pageSizeDisagrees` -- **the engine and the first-party reader read the pages
//     differently.** A rotation is applied to the object `pdf/ops/PageOps` calls page n and drawn by
//     the engine's page n; if those are not the same page, the product saves a rotation onto a page
//     nobody saw. Neither reader exposes an object number the other shares, so agreement is judged
//     on what distinguishes pages -- the count, and per index the rotation and the media box size --
//     which Stage 3 guide case A-29 measured as sufficient over the corpus, and which caught the one
//     corpus file the two read differently (`malformed/pages-loop.pdf`, guide issue I-07).
//   * `error.edit.notAssessed` -- a default-constructed value. Nothing is editable until a backend
//     has said it is.
//
// -------------------------------------------------------------------------------------------
// WHAT STAGE 6 ADDS: `Operation::Annotate`, AND A PAGE WHERE THE TWO READERS DISAGREE (D6-3)
// -------------------------------------------------------------------------------------------
// An annotation is edited through the first-party reading (`pdf/ops/AnnotationReader`) and drawn by
// the engine, so the two must read each page's `/Annots` alike -- the same number of entries, and per
// entry the same subtype and the same rectangle (within the engine's `float`). `assessAnnotations()`
// compares them PER PAGE, and a page where they differ is READ-ONLY for annotations, refused by
// `error.edit.annotationsDisagree` with that page named; the rest of the document stays editable. It is
// D3-7's rule one level down: a comment written onto the entry the first-party reader calls the third
// would otherwise land on an annotation the engine draws as something else.
//
// WHY THE ASSESSMENT IS HERE AND NOT IN THE BACKEND. The comparison is a domain rule -- which facts
// must agree, and how closely -- and `core/` owns domain rules (convention 11). The backend supplies
// both readers' facts and calls `assess()`; `tst_permissions` plants each disagreement with no engine
// linked at all.

#ifndef RAWPAGE_CORE_DOC_PERMISSIONS_H
#define RAWPAGE_CORE_DOC_PERMISSIONS_H

#include "core/Geometry.h"
#include "core/Result.h"
#include "core/doc/Annotation.h"

#include <QLatin1String>
#include <QString>

#include <optional>
#include <vector>

namespace rawpage::core {

class PageTree;

// What a command may do. Stage 3 has one kind of edit; later stages add theirs as their commands
// arrive, and Stage 10 answers each against the permission bits as well. `Annotate` is Stage 6's
// (D6-3), asked per page through `mayAnnotate`.
enum class Operation { EditPages, Annotate };

// One entry of a page's `/Annots`, as either reader sees it -- what D6-3 compares.
struct AnnotationFact {
    bool dictionary = false;                                    // the entry resolves to a dictionary
    AnnotationSubtype subtype = AnnotationSubtype::Unknown;
    std::optional<PdfRect> rect;                                // normalized; absent or non-finite: none
};

// What the first-party reader could make of the document.
enum class CosReading { Read, Encrypted, NoPageMap };

// One page, as the first-party reader sees it.
struct CosPageFacts {
    int rotation = 0;                    // effective, normalized to 0 / 90 / 180 / 270
    std::optional<PdfSize> mediaSize;    // absent when no /MediaBox is written anywhere above it
};

class Permissions {
public:
    Permissions() = default;             // refused: not assessed

    // Media box sizes agree within this many points: the engine reports them through `float`.
    static constexpr double kSizeTolerance = 0.01;

    static Permissions assess(const PageTree& engine, CosReading reading,
                              const std::vector<CosPageFacts>& cos);

    Status may(Operation operation) const;

    // D6-3: compare each page's `/Annots` as the engine reads it (`engine`) with the first-party
    // reading (`cos`), one vector per page. A page that disagrees becomes read-only for annotations.
    // Meaningful only on an editable assessment; called once, at open, by the backend.
    void assessAnnotations(const std::vector<std::vector<AnnotationFact>>& engine,
                           const std::vector<std::vector<AnnotationFact>>& cos);
    // May annotations on `page` (0-based) be changed? The document's refusal first, then the page's.
    Status mayAnnotate(int page) const;
    const std::vector<int>& annotationDisagreements() const noexcept { return disagreeing_; }

    // Rectangles agree within this many points, or this fraction of the coordinate, whichever is
    // larger: the engine reports them through `float`, whose step at 10,000 pt is about 0.001.
    static constexpr double kRectTolerance = 0.01;
    static constexpr double kRectRelativeTolerance = 1e-6;

    bool canEditPages() const noexcept { return editable_; }
    QLatin1String refusalKey() const noexcept { return refusalKey_; }
    const QString& refusalContext() const noexcept { return refusalContext_; }

private:
    static Permissions refused(const char* key, QString context = {});

    bool editable_ = false;
    QLatin1String refusalKey_{"error.edit.notAssessed"};
    QString refusalContext_;
    std::vector<int> disagreeing_;   // 0-based pages whose /Annots the two readers read differently
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_PERMISSIONS_H
