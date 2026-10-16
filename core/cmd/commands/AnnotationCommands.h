// core/cmd/commands/AnnotationCommands -- commands over a SET of annotations. Stage 6 T3: the first two
// of the eleven `Stage6-Annotations-Plan.md` §4 names, `AddAnnotationCommand` and
// `DeleteAnnotationCommand`; T5 the four that change annotations IN PLACE -- `EditAnnotationCommand`,
// `MoveAnnotationCommand`, `SetAnnotationAppearanceCommand` and `LockAnnotationCommand`; T9 the two that
// answer one -- `ReplyToAnnotationCommand` and `SetAnnotationStatusCommand` -- and T13 the import that does both an add
// and an edit as one step, `ImportAnnotationsCommand`: the eleventh of §4's names, and the last.
//
// ===========================================================================================
// A CHANGE IN PLACE IS A BEFORE AND AN AFTER (T5)
// ===========================================================================================
// The four are one shape: each works out, against the document it is applied to, what each named
// annotation BECOMES, keeps what it WAS, and tells the sink and the store the same pair
// (`MutationSink::replaceAnnotations`, `AnnotationStore::replace`). They differ only in what they carry --
// whole annotations, an offset, appearance properties, lock bits -- because each is what a gesture says:
// a drag says *this far*, not *these coordinates*, and a burst of nudges is one offset (D6-9). Each stamps
// `/M` with the instant it carries, so a replay writes the same date (D6-4).
//
// ===========================================================================================
// THE FLAGS ARE HONOURED BEFORE ANYTHING IS ASKED (D6-27)
// ===========================================================================================
// `permitted()` -- the journal's pre-flight -- refuses BY KEY: `Locked` refuses a move, a resize, any
// property change and a delete (ISO 32000-1 Table 165: *"do not allow the annotation to be deleted or its
// properties (including position and size) to be modified"*), and leaves the contents editable;
// `LockedContents` refuses changing the contents; `ReadOnly` refuses every change. `LockAnnotationCommand`
// alone is never refused for a flag -- it is how a lock comes off.
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
#include "core/doc/Fdf.h"
#include "core/settings/CommentingTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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
// A move's offset, in page units: past ISO 32000-1 Annex C's largest page (14,400 units) many times over.
inline constexpr double kMaxOffset = 1.0e6;
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

// What an in-place command worked out when it was applied: each annotation as it was and as it became,
// in the order the command names them. Kept for `revert()`, and for `scope()` -- both rectangles repaint.
struct AnnotationChange {
    std::vector<Annotation> before;
    std::vector<Annotation> after;
};

// REPLACES annotations with the given ones, matched by identity: any modelled field but the identity, the
// subtype and the page -- a resize is one of these (the overlay's drag), and so is new text (the
// properties bar). Each `after` must differ from the annotation it replaces.
class EditAnnotationCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"annot.edit"};

    explicit EditAnnotationCommand(std::vector<Annotation> after);

    const std::vector<Annotation>& annotations() const noexcept { return after_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.annot.edit, or command.annot.editMany: %1 count
    // The pages, and the rectangles before and after, once applied; the whole document before.
    Scope scope() const override;
    Operation operation() const override { return Operation::Annotate; }
    Status permitted(const Document& document) const override;   // + D6-27

    std::optional<Error> refusal() const override { return refusal_; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "annot.edit", "annotations": [ {...}, ... ]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<Annotation> after_;
    std::optional<AnnotationChange> change_;
    std::optional<Error> refusal_;   // the sink's, when it refused the last apply
};

// MOVES annotations by one offset in page space -- a drag, or a burst of nudges (D6-9): every coordinate
// of each, its rectangle included, plus (dx, dy), and `/M` stamped `at`.
class MoveAnnotationCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"annot.move"};

    MoveAnnotationCommand(std::vector<AnnotationId> ids, double dx, double dy, std::int64_t at);

    const std::vector<AnnotationId>& ids() const noexcept { return ids_; }
    double dx() const noexcept { return dx_; }
    double dy() const noexcept { return dy_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.annot.move, or command.annot.moveMany: %1 count
    Scope scope() const override;
    Operation operation() const override { return Operation::Annotate; }
    Status permitted(const Document& document) const override;

    std::optional<Error> refusal() const override { return refusal_; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "annot.move", "ids": [...], "dx": , "dy": , "at": }
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<AnnotationId> ids_;
    double dx_ = 0.0;
    double dy_ = 0.0;
    std::int64_t at_ = 0;
    std::optional<AnnotationChange> change_;
    std::optional<Error> refusal_;   // the sink's, when it refused the last apply
};

// The properties the properties bar sets (D6-9): each one present is set on every annotation of the set.
// Stage 6 T8 adds four, each applied only where the kind has it: the line style (`/BS /S` and `/D`, a dash
// scaled to the width), the blend (`/BM` -- the kind's own written as nothing), a Line's, a PolyLine's or a
// callout's endings (`/LE`), and a cloud's intensity (`/BE /I`). A drawn shape whose extent they change -- a
// wider line, a bigger head, larger bumps -- has its rectangle refitted (`core::fittedShape`), so nothing it
// draws is clipped.
struct AnnotationAppearance {
    std::optional<std::vector<double>> color;           // /C: 0, 1, 3 or 4 components
    std::optional<std::vector<double>> interiorColor;   // /IC
    std::optional<double> opacity;                      // /CA, 0 to 1
    std::optional<double> borderWidth;                  // /BS /W, 0 or more
    std::optional<LineStyle> lineStyle;                 // /BS /S, /D (T8)
    std::optional<ToolBlend> blendMode;                 // /BM (T8)
    std::optional<std::vector<QString>> lineEndings;    // /LE: two for a line or a polyline, one for a callout (T8)
    std::optional<double> cloudIntensity;               // /BE /I, above 0 and at most 2: a cloudy polygon's (T8)
    // Stage 6 T14 (D6-25): a redaction mark's overlay text and repeat -- drawn in its `/RO`. On a mark a fill also
    // turns `/DA`'s text colour to the one readable on it (`core::withRedactionFill`).
    std::optional<QString> overlayText;                 // /OverlayText, at most `kMaxOverlayTextLength`
    std::optional<bool> repeat;                         // /Repeat

    bool isEmpty() const noexcept
    {
        return !color && !interiorColor && !opacity && !borderWidth && !lineStyle && !blendMode && !lineEndings
               && !cloudIntensity && !overlayText && !repeat;
    }
    Annotation appliedTo(const Annotation& a) const;
    QJsonObject toJson() const;
    // nullopt for a property out of its range or of the wrong type.
    static std::optional<AnnotationAppearance> fromJson(const QJsonObject& json);
};

// SETS appearance properties on a set of annotations, `/M` stamped `at`.
class SetAnnotationAppearanceCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"annot.setAppearance"};

    SetAnnotationAppearanceCommand(std::vector<AnnotationId> ids, AnnotationAppearance appearance, std::int64_t at);

    const std::vector<AnnotationId>& ids() const noexcept { return ids_; }
    const AnnotationAppearance& appearance() const noexcept { return appearance_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.annot.setAppearance, or …Many: %1 count
    Scope scope() const override;
    Operation operation() const override { return Operation::Annotate; }
    Status permitted(const Document& document) const override;

    std::optional<Error> refusal() const override { return refusal_; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "annot.setAppearance", "ids": [...], "appearance": {...}, "at": }
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<AnnotationId> ids_;
    AnnotationAppearance appearance_;
    std::int64_t at_ = 0;
    std::optional<AnnotationChange> change_;
    std::optional<Error> refusal_;   // the sink's, when it refused the last apply
};

// SETS and CLEARS the three lock flags -- `Locked`, `LockedContents`, `ReadOnly` (D6-27) -- on a set of
// annotations, `/M` stamped `at`. Any other bit in either mask, or one bit in both, is malformed.
class LockAnnotationCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"annot.lock"};
    static constexpr std::uint32_t kLockFlags =
        AnnotationFlag::Locked | AnnotationFlag::LockedContents | AnnotationFlag::ReadOnly;

    LockAnnotationCommand(std::vector<AnnotationId> ids, std::uint32_t set, std::uint32_t clear, std::int64_t at);

    const std::vector<AnnotationId>& ids() const noexcept { return ids_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.annot.lock, or command.annot.lockMany: %1 count
    Scope scope() const override;
    Operation operation() const override { return Operation::Annotate; }
    Status permitted(const Document& document) const override;   // never refused for a flag

    std::optional<Error> refusal() const override { return refusal_; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "annot.lock", "ids": [...], "set": n, "clear": n, "at": }
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<AnnotationId> ids_;
    std::uint32_t set_ = 0;
    std::uint32_t clear_ = 0;
    std::int64_t at_ = 0;
    std::optional<AnnotationChange> change_;
    std::optional<Error> refusal_;   // the sink's, when it refused the last apply
};

// ---- Stage 6 T9: threads and statuses (D6-12) -------------------------------------------------------
// Both ADD annotations RawPage created, as `AddAnnotationCommand` does -- each at the end of its page's listing,
// one undo step for the set -- and each is a command of its own because each is a different promise about WHAT
// is added, checked against the document before the sink hears of it: a reply hangs under an annotation the
// document holds, ON ITS PAGE (a reply across pages is one the store would show top-level, D6-12); a state is a
// hidden state reply in a model ISO names. Neither changes the annotation it answers, so a `Locked` one may be
// answered; a `ReadOnly` one refuses every interaction but selection (D6-27) and is not.

// REPLIES -- `core::newReply`'s, each an `/IRT` `/RT /R` `Text` with text, under a parent the document holds on
// the same page. A reply to a reply is a reply: its parent is the reply.
class ReplyToAnnotationCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"annot.reply"};

    explicit ReplyToAnnotationCommand(std::vector<Annotation> replies);

    const std::vector<Annotation>& replies() const noexcept { return replies_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.annot.reply, or command.annot.replyMany: %1 count
    Scope scope() const override;
    Operation operation() const override { return Operation::Annotate; }
    Status permitted(const Document& document) const override;   // + the parent's ReadOnly

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "annot.reply", "annotations": [ {...}, ... ]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<Annotation> replies_;
    std::vector<AnnotationPlacement> placed_;
};

// A REVIEW STATUS or a CHECKMARK -- `core::newStateReply`'s, each a hidden state reply in the `Review` or the
// `Marked` model with a state that model allows, under a parent the document holds on the same page. The
// history is the list of them (D6-12): a status is never changed in place, it is answered again.
class SetAnnotationStatusCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"annot.setStatus"};

    explicit SetAnnotationStatusCommand(std::vector<Annotation> states);

    const std::vector<Annotation>& states() const noexcept { return states_; }

    QLatin1String typeId() const override { return kTypeId; }
    // command.annot.setStatus (…Many: %1), or command.annot.checkmark (…Many: %1) when every one is `Marked`'s.
    CatalogText name() const override;
    Scope scope() const override;
    Operation operation() const override { return Operation::Annotate; }
    Status permitted(const Document& document) const override;

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "annot.setStatus", "annotations": [ {...}, ... ]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<Annotation> states_;
    std::vector<AnnotationPlacement> placed_;
};

// ---- Stage 6 T13: an import (D6-20, D6-21) -----------------------------------------------------------------------
// IMPORTS annotations -- an FDF or XFDF file merged, or comments migrated from another revision -- as ONE command and
// one undo step (D6-1): it ADDS what `added` holds, each a `Created` annotation no annotation of the document already
// has, at the end of its page's listing as `AddAnnotationCommand` adds, and REPLACES IN PLACE what `replaced` holds,
// each matched by identity as `EditAnnotationCommand` replaces -- the identity, the subtype, the page and the `/NM`
// unchanged. WHAT to add and replace is `AnnotationStore::importFdf`'s merge, decided before the command exists (and
// `core/doc/CommentMigrator`'s, for a migration); the command checks its own promises again in `apply()` and in its
// parser, because a journal line is bytes on a disk. Replaced first, added second, so an added pop-up and the
// replaced parent that names it arrive in one compose; a failure part-way puts back what was done.
class ImportAnnotationsCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"annot.import"};

    ImportAnnotationsCommand(std::vector<Annotation> added, std::vector<Annotation> replaced);

    const std::vector<Annotation>& added() const noexcept { return added_; }
    const std::vector<Annotation>& replaced() const noexcept { return replaced_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.annot.import: %1 -- how many were added and replaced
    Scope scope() const override;           // the added annotations' pages, and the replaced ones' rectangles
    Operation operation() const override { return Operation::Annotate; }
    Status permitted(const Document& document) const override;   // + each page, + D6-27 for each replacement

    std::optional<Error> refusal() const override { return refusal_; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "annot.import", "added": [ {...}, ... ], "replaced": [ {...}, ... ]} -- either may be empty, not both.
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<Annotation> added_;
    std::vector<Annotation> replaced_;
    std::vector<AnnotationPlacement> placed_;
    std::optional<AnnotationChange> change_;
    std::optional<Error> refusal_;   // the sink's, when it refused the last apply
};

// THE CHECK AN IMPORT ASKS OF EACH ANNOTATION IT WOULD WRITE (`AnnotationStore::importFdf`'s `ImportCheck`): the shape
// a command carries -- a finite rectangle and geometry, colours in range, a bounded name, a drawable dash (refused by
// `error.exchange.geometry`, the field named) -- the document's permission for its page (D6-3), and `writable`: the
// backend's answer, whether this build can write it (`IDocumentBackend::annotationWritable`). Asked one by one, so a
// file holding one comment this build cannot write imports the rest.
ImportCheck importCheck(const Document& document, std::function<Status(const Annotation&)> writable);

// D6-27, as `permitted()` asks it: may `annotation` be changed this way? `properties` is anything but its
// text, `contents` its text. Refused by key, the page named (1-based): `error.edit.annotationReadOnly`,
// `error.edit.annotationLocked`, `error.edit.annotationContentsLocked`.
Status annotationChangeAllowed(const Annotation& annotation, bool properties, bool contents);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDS_ANNOTATIONCOMMANDS_H
