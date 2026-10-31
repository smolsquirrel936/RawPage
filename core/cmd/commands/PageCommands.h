// core/cmd/commands/PageCommands -- commands over pages. Stage 3 T5: `RotatePageCommand`. Stage 7 T2:
// `MovePagesCommand`, `DeletePagesCommand` and `DuplicatePagesCommand` (`Stage7-Assembly-Plan.md` D7-1 ... D7-3);
// Stage 7 T5: `InsertPagesCommand` and `ReplacePagesCommand` (D7-4, D7-6); crop, resize and renumber (T9) arrive
// with their task.
//
// RotatePageCommand is the first-milestone mutation and the shape every later command copies, so the
// choices below are made for the hundredth command as much as for this one.
//
// A RELATIVE QUARTER TURN, NOT A TARGET ANGLE. Flow D writes `RotatePageCommand{page, +90}`: the
// command carries a page and a turn of 90, 180 or 270 degrees clockwise. Its inverse is then exact
// in any state -- revert turns the other way -- which a target angle is not: "set page 3 to 90"
// cannot be undone without also remembering what page 3 was, and a remembered "from" goes wrong the
// moment a recovery replay meets a document the journal did not expect. The dialog's "90° counter-
// clockwise" is a turn of 270.
//
// SINK FIRST, MODEL SECOND. `apply()` checks the page exists, asks the sink to take the new rotation,
// and only when the sink accepts does it change the model -- which, for an index already checked,
// cannot fail. So a `false` return has changed neither, which is the contract `Command.h` states and
// `CommandJournal` checks.

#ifndef RAWPAGE_CORE_CMD_COMMANDS_PAGECOMMANDS_H
#define RAWPAGE_CORE_CMD_COMMANDS_PAGECOMMANDS_H

#include "core/Result.h"
#include "core/cmd/Command.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/CopiedPages.h"
#include "core/doc/Page.h"
#include "core/doc/PageMap.h"

#include <QString>

#include <memory>
#include <vector>

namespace rawpage::core {

class RotatePageCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.rotate"};

    // The largest page index a serialized command may name. ISO 32000-1 Annex C.1 puts the
    // implementation limit on indirect objects at 8,388,607 (2^23 - 1), and every page is one, so an
    // index at or beyond it names no page in any conforming file -- and a recovery journal claiming one
    // is damaged or hostile (guide case A-45).
    static constexpr int kMaxPageIndex = 8'388'606;

    // `quarterTurns` is 90, 180 or 270 -- clockwise. -90 is accepted and normalized to 270; anything
    // that is not a whole quarter turn, or is a full turn, is not a rotation, and `isValid()` says so.
    RotatePageCommand(int pageIndex, int degreesClockwise);

    bool isValid() const noexcept;
    int pageIndex() const noexcept { return pageIndex_; }
    int degrees() const noexcept { return degrees_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.rotate: %1 page number, %2 degrees
    Scope scope() const override { return Scope::page(pageIndex_); }
    Operation operation() const override { return Operation::EditPages; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "page.rotate", "page": n, "degrees": d}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    bool turn(Document& document, MutationSink& sink, int degrees);

    int pageIndex_ = -1;
    int degrees_ = 0;
};

// ===========================================================================================
// THE PAGE-ORDER COMMANDS -- Stage 7 T2
// ===========================================================================================
// Each names a SET of pages by index as the model counts them when it runs (D7-24: the journal's order
// is the order they ran, so an index is unambiguous at a replay) and is ONE undo step however many pages
// it touches (D7-1). Each changes three parts of the model the same way -- the page tree, the annotation
// store's listings and the per-page refusals -- through one `PageMap` (D7-3), and tells the sink the same
// thing in the same indices. Sink first, model second, as `RotatePageCommand` does: the sink refuses by
// key with nothing changed, and the model's half cannot fail for a set checked first.
//
// THE PRE-FLIGHT NAMES EVERY PAGE (D7-21): `permitted()` asks `Permissions::mayRestructurePages` -- a page tree
// the walk noted anything about keeps its order -- and `Permissions::mayEditPage` of each page the command
// names, so a page whose two readings disagree is refused by key before anything runs.
//
// THE KEY. A delete and a duplicate carry a key -- 128 random bits, lowercase hex, made once when the
// command is made and serialized with it -- under which the sink keeps what it takes away, so an undo puts
// back THOSE pages, and a replay names the same copies (D6-4's rule for a created annotation's /NM, applied
// to a page command). A move needs none: its inverse is arithmetic.

// The largest page set a serialized page command may carry, and the bound a hostile journal line meets.
inline constexpr int kMaxPagesPerCommand = 1'000'000;

// The two halves every page command shares, as functions rather than a base class: `tools/mutation-lint`
// reads `Commands.def` and the class declarations AS TEXT and requires each command to derive from `Command`
// itself (D3-12), so the sharing is by call.
//
// The pre-flight: the page tree may be restructured, and every page in `pages` may be named (D7-21).
Status pageCommandPermitted(const Document& document, const std::vector<int>& pages);
// The model's half: the page tree, the annotation store's listings and the per-page refusals, each as `map`
// says. Checked by the caller before the sink is told anything, so it cannot refuse for a valid map.
bool applyPageMapToModel(Document& document, const PageMap& map, std::vector<Page> insertedPages,
                         std::vector<AnnotationStore::PageListing> insertedListings);

class MovePagesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.move"};

    // `pages` move, keeping their order, so that the first lands at index `to` of the document AFTER the
    // move (`PageMap::move`). A move to where the pages already are is a valid command that refuses to
    // apply -- the map is the identity -- so a surface asks `PageMap::move(...).isIdentity()` and makes none.
    MovePagesCommand(std::vector<int> pages, int to);

    const std::vector<int>& pages() const noexcept { return pages_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override { return pageCommandPermitted(document, pages_); }
    int to() const noexcept { return to_; }
    const PageCommandReport& report() const noexcept { return report_; }
    std::optional<Error> refusal() const override { return refusal_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.move: %1 page count
    Scope scope() const override;           // WholeDocument, with the map once applied (D7-3)

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "page.move", "pages": [...], "to": n}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<int> pages_;
    int to_ = 0;
    PageCommandReport report_;
    std::optional<Error> refusal_;
    std::optional<PageMap> map_;   // set by apply: the map of THIS run
};

class DeletePagesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.delete"};

    // An empty `key` makes a new one (`newCommandKey`).
    explicit DeletePagesCommand(std::vector<int> pages, QString key = {});

    const std::vector<int>& pages() const noexcept { return pages_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override { return pageCommandPermitted(document, pages_); }
    const QString& key() const noexcept { return key_; }
    // What the last apply cleared, removed, unlinked or reported (D7-2) -- the message bar's, at T8.
    const PageCommandReport& report() const noexcept { return report_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.delete: %1 page count
    Scope scope() const override;
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "page.delete", "pages": [...], "key": "<hex>"}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<int> pages_;
    QString key_;
    PageCommandReport report_;
    std::optional<Error> refusal_;
    std::optional<PageMap> map_;
    std::vector<Page> removedPages_;                            // what revert puts back, from apply
    std::vector<AnnotationStore::PageListing> removedListings_;
};

class DuplicatePagesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.duplicate"};

    explicit DuplicatePagesCommand(std::vector<int> pages, QString key = {});

    const std::vector<int>& pages() const noexcept { return pages_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override { return pageCommandPermitted(document, pages_); }
    const QString& key() const noexcept { return key_; }
    const PageCommandReport& report() const noexcept { return report_; }

    // The `/NM` of the `ordinal`th annotation this command copies, counted over the pages in order --
    // derived from the key, so a replay names the same copies.
    QString copyName(int ordinal) const;

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.duplicate: %1 page count
    Scope scope() const override;
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "page.duplicate", "pages": [...], "key": "<hex>"}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<int> pages_;
    QString key_;
    PageCommandReport report_;
    std::optional<Error> refusal_;
    std::optional<PageMap> map_;
};

// ===========================================================================================
// PAGES FROM ELSEWHERE -- Stage 7 T5 (`Stage7-Assembly-Plan.md` D7-4, D7-6)
// ===========================================================================================
// Each carries the COPY, not the source file: `CopiedPages::graph`, the payload `IDocumentBackend::copyPages` or
// `blankPages` made when the pages were picked, serialized with the command -- the recovery journal puts a payload
// over 64 KiB in a side file named by its SHA-256 (D6-23) -- so a replay needs no source (D7-6). Each has a key, as
// a delete has: its copies of the payload's annotations are named from it (`<key>-0`, ...), and the sink keeps what
// an undo takes away under it, so a redo puts back THE SAME pages.
//
// What the copy met (`CopiedPages::report`) is not serialized: a replay reports only what the sink meets.

class InsertPagesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.insert"};

    // The payload's pages land at index `at` (0 ... the page count), in order; the page at `at` and those after it
    // follow them. An empty `key` makes a new one.
    InsertPagesCommand(int at, CopiedPages pages, QString key = {});

    int at() const noexcept { return at_; }
    const CopiedPages& pages() const noexcept { return pages_; }
    const QString& key() const noexcept { return key_; }
    // What the copy and the sink met (D7-2) -- a link whose page stayed behind, a widget, a tag tree.
    const PageCommandReport& report() const noexcept { return report_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override { return pageCommandPermitted(document, {}); }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.insert: %1 page count
    Scope scope() const override;           // WholeDocument, with the map once applied (D7-3)
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "page.insert", "at": n, "key": "<hex>", "pages": count, "graph": "<base64>"}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    int at_ = 0;
    CopiedPages pages_;
    QString key_;
    PageCommandReport report_;
    std::optional<Error> refusal_;
    std::optional<PageMap> map_;
};

class ReplacePagesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.replace"};

    // Page `pages[i]` draws the payload's page `i` -- as many pages as the payload holds. Each KEEPS ITS OBJECT (the
    // user's choice, 2026-10-31): what points at it still lands on it, no repair needed. What it draws is the
    // payload page's, its own annotations stay, and the payload page's are added after them. A page no save has
    // written yet is refused (`error.edit.pageNotSaved`): it is replaced by deleting it and inserting.
    ReplacePagesCommand(std::vector<int> pages, CopiedPages copied, QString key = {});

    const std::vector<int>& pages() const noexcept { return pages_; }
    const CopiedPages& copied() const noexcept { return copied_; }
    const QString& key() const noexcept { return key_; }
    const PageCommandReport& report() const noexcept { return report_; }
    Operation operation() const override { return Operation::EditPages; }
    // Every page it names may be edited and annotated: annotations are added to each (D7-21).
    Status permitted(const Document& document) const override;

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.replace: %1 page count
    Scope scope() const override { return Scope::pages(pages_); }
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "page.replace", "pages": [...], "key": "<hex>", "graph": "<base64>"}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<int> pages_;
    CopiedPages copied_;
    QString key_;
    PageCommandReport report_;
    std::optional<Error> refusal_;
    std::vector<Page> before_;                      // what each page was, from apply
    std::vector<AnnotationPlacement> added_;        // where apply listed the payload's annotations
};

// 128 random bits as 32 lowercase hex digits -- a page command's key.
QString newCommandKey();

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDS_PAGECOMMANDS_H
