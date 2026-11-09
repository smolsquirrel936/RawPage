// core/cmd/commands/PageCommands -- commands over pages. Stage 3 T5: `RotatePageCommand`. Stage 7 T2:
// `MovePagesCommand`, `DeletePagesCommand` and `DuplicatePagesCommand` (`Stage7-Assembly-Plan.md` D7-1 ... D7-3);
// Stage 7 T5: `InsertPagesCommand` and `ReplacePagesCommand` (D7-4, D7-6); Stage 7 T9: `CropPagesCommand`,
// `ResizePagesCommand`, `RenumberPagesCommand` and `SetEmbeddedThumbnailsCommand` (D7-15, D7-32).
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
//
// OVER A PAGE SET SINCE STAGE 7 T8 (`Stage7-Assembly-Plan.md` D7-13, §4): `Ctrl+R` on the thumbnails' selection, a
// range, odd or even pages, one orientation -- ONE command and one undo step, where Stage 3 ran one command per page
// inside a group (D3-13). Its line GREW rather than changed: one page is still written `{"page": n}`, byte for byte
// Stage 3's, and several are `{"pages": [...]}`; both are read, so a journal Stage 3 wrote still replays (guide A-59).
// A sink that refuses part-way through the set is told the pages it already turned turn back, so a refusal changes
// nothing.

#ifndef RAWPAGE_CORE_CMD_COMMANDS_PAGECOMMANDS_H
#define RAWPAGE_CORE_CMD_COMMANDS_PAGECOMMANDS_H

#include "core/Result.h"
#include "core/cmd/Command.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/CopiedPages.h"
#include "core/doc/Outline.h"
#include "core/doc/Page.h"
#include "core/doc/PageMap.h"

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rawpage::core {

class PageTree;   // core/doc/PageTree.h

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
    // A page SET, strictly ascending and not empty -- anything else is not valid (S7 T8).
    RotatePageCommand(std::vector<int> pages, int degreesClockwise);

    bool isValid() const noexcept;
    int pageIndex() const noexcept { return pages_.empty() ? -1 : pages_.front(); }   // the first page of the set
    const std::vector<int>& pages() const noexcept { return pages_; }
    int degrees() const noexcept { return degrees_; }

    QLatin1String typeId() const override { return kTypeId; }
    // command.page.rotate: %1 page number, %2 degrees; for several, command.page.rotatePages: %1 page count, %2 degrees
    CatalogText name() const override;
    Scope scope() const override { return Scope::pages(pages_); }
    Operation operation() const override { return Operation::EditPages; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "page.rotate", "page": n, "degrees": d} for one page -- Stage 3's line -- and
    // {"type": "page.rotate", "pages": [...], "degrees": d} for several
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    bool turn(Document& document, MutationSink& sink, int degrees);

    std::vector<int> pages_;
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
    // S7 T11 (D7-3): the outline before the removal, put back by the revert -- the removal leaves a destination to a
    // removed page unresolved, and the inverse map cannot say which page it named.
    std::optional<Outline> outlineBefore_;
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

// ===========================================================================================
// BOXES, CROP, RESIZE, RENUMBER AND THUMBNAILS -- Stage 7 T9 (`Stage7-Assembly-Plan.md` D7-15, D7-32)
// ===========================================================================================
// Four commands that change what a page SHOWS and not where it is: no page map, a `Scope::pages` of the pages they
// name. Each has a key, under which the sink keeps what it replaced (`MutationSink::setPageBoxes` and its three
// siblings), so an undo puts back exactly that -- before a save and after one -- and a refusal part-way through a
// command's two sink calls is taken back by the first call's restore. Sink first, model second.
//
// WHAT EACH MEANS IS DECIDED HERE, from the MODEL, before the sink hears of it -- a crop's boxes clipped to the
// MediaBox, a resize's boxes and content matrix, a renumbering's labels -- so the journal line says what was meant and
// a replay against the same document computes the same thing (D7-24).

enum class PageBoxKind : std::uint8_t { Media, Crop, Bleed, Trim, Art };

// Where a resized page's content goes in its new size: one of nine places, as the page is SHOWN (after `/Rotate`).
enum class ResizeAnchor : std::uint8_t { TopLeft, Top, TopRight, Left, Centre, Right, BottomLeft, Bottom, BottomRight };

// The smallest box a page may be given, in points: ISO 32000-1 Annex C's 3 units, the size `blankPages` refuses below.
inline constexpr double kMinPageBoxPoints = 3.0;

// `boxes` with every box but the MediaBox intersected with the MediaBox -- and, in `clipped`, which were cut (D7-15:
// *a box outside the MediaBox is clipped to it and the report says so*). Refused by `error.pages.boxTooSmall` for a
// MediaBox or CropBox under `kMinPageBoxPoints` on a side, before or after the clip, and by `error.pages.boxOutside`
// for any box that does not overlap the MediaBox at all.
Result<PageBoxes> clipToMediaBox(const PageBoxes& boxes, std::vector<PageBoxKind>* clipped = nullptr);

// REMOVE WHITE MARGINS (D7-15): the CropBox that shows `content` -- the engine's content box (Stage 4's) -- with
// `padding` points around it, inside `cropBox`, the box the page shows now. Never larger than what the page shows.
PdfRect whiteMarginBox(PdfRect content, double padding, PdfRect cropBox);

// The pages of `pages` whose SHOWN size is `page`'s, within a hundredth of a point -- *apply to all pages of the same
// size*. In order.
std::vector<int> pagesOfSameSize(const PageTree& pages, int page);

// A RESIZE OF ONE PAGE (D7-15, D7-32): `target` as the page will be SHOWN, in points. With `scale`, the content that was
// shown is scaled to fit -- the larger of the two that fit, up or down -- placed at `anchor` and clipped to what it
// showed; the MediaBox is the target and the other four boxes are removed. Without, the MediaBox is the target placed
// at `anchor` around what the page showed, the content where it was. Refused by `error.pages.boxTooSmall` for a
// target under `kMinPageBoxPoints`, and by `error.pages.badSize` for one past ISO 32000-1 Annex C's 14,400.
struct ResizePlan {
    PageBoxes boxes;
    std::optional<PageContentWrap> wrap;   // with scaling only
};
Result<ResizePlan> planResize(const Page& page, PdfSize target, bool scale, ResizeAnchor anchor);

class CropPagesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.crop"};

    // Page `pages[i]` takes `boxes[i]` -- each box given set, each absent one removed. `boxes` of ONE applies it to
    // every page. A crop UNDONE puts back what each page had; *undo a crop* the person asks for (`Features.md` §5.1)
    // is this command with each CropBox absent -- the MediaBox shown again.
    CropPagesCommand(std::vector<int> pages, std::vector<PageBoxes> boxes, QString key = {});

    const std::vector<int>& pages() const noexcept { return pages_; }
    const std::vector<PageBoxes>& boxes() const noexcept { return boxes_; }
    const QString& key() const noexcept { return key_; }
    // Each box clipped to its MediaBox (`PageReference::Kind::PageBox`, `Clipped`), per page.
    const PageCommandReport& report() const noexcept { return report_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override;

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.crop: %1 page count
    Scope scope() const override { return Scope::pages(pages_); }
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "page.crop", "pages": [...], "key": "<hex>", "boxes": [{"media": [l, b, r, t], "crop": [...], ...}]}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<int> pages_;
    std::vector<PageBoxes> boxes_;
    QString key_;
    PageCommandReport report_;
    std::optional<Error> refusal_;
    std::vector<Page> before_;   // the model's pages, from apply
};

class ResizePagesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.resize"};

    ResizePagesCommand(std::vector<int> pages, PdfSize target, bool scale, ResizeAnchor anchor, QString key = {});

    const std::vector<int>& pages() const noexcept { return pages_; }
    PdfSize target() const noexcept { return target_; }
    bool scales() const noexcept { return scale_; }
    ResizeAnchor anchor() const noexcept { return anchor_; }
    const QString& key() const noexcept { return key_; }
    // With scaling: each page whose comments keep their rectangles (`PageReference::Kind::PageComments`, D7-32).
    const PageCommandReport& report() const noexcept { return report_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override;

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.resize: %1 page count
    Scope scope() const override { return Scope::pages(pages_); }
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "page.resize", "pages": [...], "key": "<hex>", "width": w, "height": h, "scale": b, "anchor": 0..8}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<int> pages_;
    PdfSize target_;
    bool scale_ = true;
    ResizeAnchor anchor_ = ResizeAnchor::Centre;
    QString key_;
    PageCommandReport report_;
    std::optional<Error> refusal_;
    std::vector<Page> before_;
};

class RenumberPagesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.renumber"};

    // `pages` numbered `start`, `start + 1`, ... in order, in `style` after `prefix` (`Features.md` §5.1: *a page-label
    // style, a prefix and a start value, per page range*). Every other page keeps the label it shows.
    RenumberPagesCommand(std::vector<int> pages, PageLabelStyle style, QString prefix, int start, QString key = {});

    const std::vector<int>& pages() const noexcept { return pages_; }
    PageLabelStyle style() const noexcept { return style_; }
    const QString& prefix() const noexcept { return prefix_; }
    int start() const noexcept { return start_; }
    const QString& key() const noexcept { return key_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override;

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.renumber: %1 page count
    Scope scope() const override { return Scope::nothing(); }   // a label draws nothing on the page
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "page.renumber", "pages": [...], "key": "<hex>", "style": "D", "prefix": "...", "start": n}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    // The largest start a label may have, and the longest prefix -- a hostile journal line's bound.
    static constexpr int kMaxStart = 1'000'000;
    static constexpr int kMaxPrefix = 256;

private:
    std::vector<int> pages_;
    PageLabelStyle style_ = PageLabelStyle::Decimal;
    QString prefix_;
    int start_ = 1;
    QString key_;
    std::optional<Error> refusal_;
    std::vector<QString> before_;   // every page's label, from apply
};

class SetEmbeddedThumbnailsCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.thumbnails"};

    // Page `pages[i]` carries `thumbnails[i]` as its embedded thumbnail; `thumbnails` EMPTY removes every page's
    // (`Features.md` §5.2: *embed page thumbnails into the file, or remove embedded thumbnails*).
    SetEmbeddedThumbnailsCommand(std::vector<int> pages, std::vector<PageThumbnail> thumbnails, QString key = {});

    const std::vector<int>& pages() const noexcept { return pages_; }
    const std::vector<PageThumbnail>& thumbnails() const noexcept { return thumbnails_; }
    bool removes() const noexcept { return thumbnails_.empty(); }
    const QString& key() const noexcept { return key_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override;

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.embedThumbnails / command.page.removeThumbnails: %1 page count
    Scope scope() const override { return Scope::nothing(); }   // a reader shows a page, not its thumbnail
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "page.thumbnails", "pages": [...], "key": "<hex>", "thumbnails": [{"width": w, "height": h,
    // "rgb": "<base64 of qCompress>"}]} -- no "thumbnails" for a removal
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<int> pages_;
    std::vector<PageThumbnail> thumbnails_;
    QString key_;
    std::optional<Error> refusal_;
};

// 128 random bits as 32 lowercase hex digits -- a page command's key.
QString newCommandKey();

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDS_PAGECOMMANDS_H
