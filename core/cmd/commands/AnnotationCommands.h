// core/cmd/commands/AnnotationCommands -- commands over a SET of annotations. Stage 6 T3: the first two
// of the eleven `Stage6-Annotations-Plan.md` §4 names, `AddAnnotationCommand` and
// `DeleteAnnotationCommand`; edit, move, reply, status, appearance, lock and import arrive with the
// tasks that need them.
//
// ===========================================================================================
// ONE COMMAND, MANY ANNOTATIONS, ONE UNDO STEP (D6-1)
// ===========================================================================================
// *Delete everything matching the filter*, *duplicate onto all pages*, an import: each is one gesture
// and one `Ctrl+Z`, so each command carries a set. Both halves of a change are told the same thing in
// the same words -- the store (`AnnotationStore::insert`/`remove`) and the sink
// (`MutationSink::insertAnnotations`/`removeAnnotations`) -- as PLACEMENTS in a page's `/Annots`
// listing, because the two hold that listing entry for entry.
//
// SINK FIRST, MODEL SECOND, as `RotatePageCommand` does: everything that can be checked is checked
// against the model, then the sink is asked (it is the one that can refuse -- an annotation of a kind
// it cannot write yet), and only then does the model change, which for a validated set cannot fail.
//
// ===========================================================================================
// WHAT A COMMAND NAMES, AND WHAT IT WORKS OUT (D6-4)
// ===========================================================================================
// A command names annotations by IDENTITY, never by a number a compose allocated: the object number
// of one read from the file, (page, position) for a direct dictionary, and the `/NM` RawPage generated
// for one it created -- which is generated ONCE, in `core::newAnnotation`, and serialized with the
// command, so a replay, an undo and a redo all name the same annotation. WHERE in a listing each goes
// is worked out by `apply()` against the document it is applied to, and kept for `revert()`: undo is
// last-in-first-out, so the listing a revert meets is the one its apply left.

#ifndef RAWPAGE_CORE_CMD_COMMANDS_ANNOTATIONCOMMANDS_H
#define RAWPAGE_CORE_CMD_COMMANDS_ANNOTATIONCOMMANDS_H

#include "core/Result.h"
#include "core/cmd/Command.h"
#include "core/doc/Annotation.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace rawpage::core {

// Shared bounds of a serialized annotation command -- the recovery journal is a file on disk, so its
// parser is a parser of hostile input (Stage 3's guide case A-45).
namespace AnnotationCommandLimits {
// Annotations one command may carry: past any real gesture (`annots-900.pdf` holds 9,000 in all).
inline constexpr std::size_t kMaxAnnotations = 100000;
// Numbers of geometry one annotation may carry -- `pdf/ops/AnnotationReader::kMaxCoordinates`.
inline constexpr std::size_t kMaxCoordinates = 200000;
// A page index past ISO 32000-1 Annex C's limit on indirect objects names no page (`RotatePageCommand`).
inline constexpr int kMaxPageIndex = 8'388'606;
// A `/NM` RawPage writes is 32 characters; one an import brings is the producer's, and bounded here.
inline constexpr int kMaxNameLength = 256;
} // namespace AnnotationCommandLimits

// ADDS annotations RawPage created (`core::newAnnotation`), each at the END of its page's listing, in
// the order given. Every one must carry a `Created` identity no annotation of the document already has.
class AddAnnotationCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"annot.add"};

    explicit AddAnnotationCommand(std::vector<Annotation> annotations);

    const std::vector<Annotation>& annotations() const noexcept { return annotations_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.annot.add, or command.annot.addMany: %1 count
    Scope scope() const override;           // the pages the annotations land on
    Operation operation() const override { return Operation::Annotate; }
    Status permitted(const Document& document) const override;   // + mayAnnotate(page), each page

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "annot.add", "annotations": [ {...}, ... ]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<Annotation> annotations_;
    std::vector<AnnotationPlacement> placed_;   // where apply() listed each, in order
};

// DELETES annotations by identity: every listing of each -- an annotation two pages list leaves both
// -- and the dictionary stays in the file, listed nowhere (an incremental update removes no object).
// What else a person means by *delete this comment* -- its pop-up, its replies -- is the caller's to
// put in the set (T5's overlay, T9's pane); this command deletes exactly what it names.
class DeleteAnnotationCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"annot.delete"};

    explicit DeleteAnnotationCommand(std::vector<AnnotationId> ids);

    const std::vector<AnnotationId>& ids() const noexcept { return ids_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.annot.delete, or command.annot.deleteMany: %1 count
    // The pages its apply() took listings from -- and, before it has been applied, the whole document:
    // which pages list an annotation is the document's to say, and an unknown scope rounds OUTWARD.
    Scope scope() const override;
    Operation operation() const override { return Operation::Annotate; }
    Status permitted(const Document& document) const override;

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "annot.delete", "ids": ["obj:12", "nm:…", …]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<AnnotationId> ids_;
    // Kept by apply() for revert(): every listing removed, ascending, with the annotation it named.
    std::vector<std::pair<AnnotationPlacement, Annotation>> removed_;
    bool applied_ = false;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDS_ANNOTATIONCOMMANDS_H
