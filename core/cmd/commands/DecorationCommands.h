// core/cmd/commands/DecorationCommands -- headers, footers and Bates numbers, put on and taken off. Stage 7 T14,
// decisions D7-7, D7-14 and D7-44 (`Stage7-Assembly-Plan.md` §3). T15 adds the watermark's pair (D7-45), at the end.
//
// FOUR COMMANDS, EACH ONE UNDO STEP however many pages it decorates (D7-1). Each says what it means in the MODEL's
// terms -- the spec, the field values fixed when it was made, the pages it reaches as the document counts them now --
// and lays out every page's text HERE (`core/doc/HeaderFooter`), so the journal line says what was meant and a replay
// draws the same thing (D7-24). The sink writes it (`MutationSink::addPageDecorations`) and keeps nothing a command
// does not name by its key.
//
// *UPDATE* IS ONE COMMAND: `ApplyHeaderFooterCommand` with `replace` takes every RawPage header and footer off first,
// under the same key, and its undo puts them back (D7-7: *update is remove and apply, one command*).
//
// *REMOVE* NAMES NO PAGES: it takes every decoration of its kind RawPage wrote -- this session's or a saved one's --
// and answers what it found. Nothing found is a refusal by key (`error.decorate.noneFound`) with nothing changed, so a
// remove that would do nothing is never an undo step. A decoration another producer wrote is never one (D7-7).
//
// NO MODEL STATE. The model holds no decoration: what a page draws is the sink's, read by mark (D7-44(c)). So apply and
// revert leave `Document::modelHash()` as it was, and `tst_reversibility`'s second half -- the sink with nothing to
// write after the revert -- is the whole property.

#ifndef RAWPAGE_CORE_CMD_COMMANDS_DECORATIONCOMMANDS_H
#define RAWPAGE_CORE_CMD_COMMANDS_DECORATIONCOMMANDS_H

#include "core/cmd/Command.h"
#include "core/doc/HeaderFooter.h"
#include "core/doc/Watermark.h"

#include <QJsonObject>

#include <memory>
#include <optional>
#include <vector>

namespace rawpage::core {

// What both apply commands share: the spec laid out over the pages it reaches, the sink told, and the undo.
class DecorationApply {
public:
    DecorationApply(DecorationKind kind, HeaderFooterSpec spec, FieldValues values, bool replace, QString key);

    const HeaderFooterSpec& spec() const noexcept { return spec_; }
    const FieldValues& values() const noexcept { return values_; }
    bool replaces() const noexcept { return replace_; }
    const QString& key() const noexcept { return key_; }
    const std::vector<int>& pages() const noexcept { return pages_; }
    const std::vector<int>& replacedPages() const noexcept { return taken_; }

    Status permitted(const Document& document) const;
    Scope scope() const;
    bool apply(Document& document, MutationSink& sink, std::optional<Error>& refusal);
    bool revert(MutationSink& sink);

private:
    DecorationKind kind_;
    HeaderFooterSpec spec_;
    FieldValues values_;
    bool replace_ = false;
    QString key_;
    std::vector<int> pages_;   // from apply
    std::vector<int> taken_;   // with `replace`: the pages the old ones were on
};

class ApplyHeaderFooterCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"headerFooter.apply"};

    // `spec` on the pages it names, its fields expanded with `values`; with `replace`, every RawPage header and footer
    // taken off first (*update*). An empty `key` makes a new one.
    ApplyHeaderFooterCommand(HeaderFooterSpec spec, FieldValues values, bool replace = false, QString key = {});

    const DecorationApply& state() const noexcept { return state_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override { return state_.permitted(document); }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.headerFooter.apply / .update: %1 page count
    Scope scope() const override { return state_.scope(); }
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "headerFooter.apply", "key": "<hex>", "replace": false, "spec": {...}, "values": {...}}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    DecorationApply state_;
    std::optional<Error> refusal_;
};

class ApplyBatesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"bates.apply"};

    // `spec`, which must hold `<<bates>>` in an area, numbering the pages it names from `spec.bates.start` -- one
    // document's share of a run (`core/doc/BatesAllocator`). Refused by `error.decorate.batesOverflow` when the last
    // number needs more digits than the format has.
    ApplyBatesCommand(HeaderFooterSpec spec, FieldValues values, QString key = {});

    const DecorationApply& state() const noexcept { return state_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override { return state_.permitted(document); }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.bates.apply: %1 page count
    Scope scope() const override { return state_.scope(); }
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "bates.apply", "key": "<hex>", "spec": {...}, "values": {...}}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    DecorationApply state_;
    std::optional<Error> refusal_;
};

// Takes every RawPage decoration of one kind off every page -- see the header. Stage 7 T15: or only those named by id.
class DecorationRemove {
public:
    DecorationRemove(DecorationKind kind, QString key, std::vector<QString> only = {});
    const QString& key() const noexcept { return key_; }
    const std::vector<QString>& only() const noexcept { return only_; }
    const std::vector<int>& pages() const noexcept { return pages_; }
    Status permitted(const Document& document) const;
    Scope scope() const { return Scope::pages(pages_); }
    bool apply(MutationSink& sink, std::optional<Error>& refusal);
    bool revert(MutationSink& sink);

private:
    DecorationKind kind_;
    QString key_;
    std::vector<QString> only_;   // empty: every one of the kind
    std::vector<int> pages_;      // from apply
};

class RemoveHeaderFooterCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"headerFooter.remove"};
    explicit RemoveHeaderFooterCommand(QString key = {});

    const DecorationRemove& state() const noexcept { return state_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override { return state_.permitted(document); }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.headerFooter.remove: %1 page count
    Scope scope() const override { return state_.scope(); }
    std::optional<Error> refusal() const override { return refusal_; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;
    // {"type": "headerFooter.remove", "key": "<hex>"}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    DecorationRemove state_;
    std::optional<Error> refusal_;
};

class RemoveBatesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"bates.remove"};
    explicit RemoveBatesCommand(QString key = {});

    const DecorationRemove& state() const noexcept { return state_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override { return state_.permitted(document); }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.bates.remove: %1 page count
    Scope scope() const override { return state_.scope(); }
    std::optional<Error> refusal() const override { return refusal_; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;
    // {"type": "bates.remove", "key": "<hex>"}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    DecorationRemove state_;
    std::optional<Error> refusal_;
};

// ---- Watermarks -- Stage 7 T15 (D7-7, D7-9, D7-45) -----------------------------------------------------------------
//
// ONE WATERMARK A COMMAND, and a document may carry several, each its own key. *Update* and *Remove* act on the ones
// CHOSEN (`Features.md` §5.8, guide M-09): an update is `ApplyWatermarkCommand` with `replace` naming them -- each taken
// off first under this command's key and the new one added, one undo step -- and with `keepSourceOf` in its spec it
// draws the picture of one of them, by reference, rather than carrying it again. The source's bytes, when it carries
// them, travel in the journal line, as an image stamp's do (D7-24: a replay draws the same picture).

class ApplyWatermarkCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"watermark.apply"};

    // `spec` on the pages it names; with `replace`, the watermarks of those ids taken off first (*update*) -- refused by
    // `error.decorate.noneFound` when none of them is there. An empty `key` makes a new one.
    explicit ApplyWatermarkCommand(WatermarkSpec spec, std::vector<QString> replace = {}, QString key = {});

    const WatermarkSpec& spec() const noexcept { return spec_; }
    const std::vector<QString>& replaced() const noexcept { return replace_; }
    const QString& key() const noexcept { return key_; }
    const std::vector<int>& pages() const noexcept { return pages_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override;

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.watermark.apply / .update: %1 page count
    Scope scope() const override;
    std::optional<Error> refusal() const override { return refusal_; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    // {"type": "watermark.apply", "key": "<hex>", "replace": ["<hex>", ...], "spec": {... with the source's bytes}}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    WatermarkSpec spec_;
    std::vector<QString> replace_;
    QString key_;
    std::vector<int> pages_;   // from apply
    std::vector<int> taken_;   // with `replace`: the pages the old ones were on
    std::optional<Error> refusal_;
};

class RemoveWatermarkCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"watermark.remove"};
    // The watermarks of those ids -- none named: every RawPage watermark.
    explicit RemoveWatermarkCommand(std::vector<QString> ids = {}, QString key = {});

    const DecorationRemove& state() const noexcept { return state_; }
    Operation operation() const override { return Operation::EditPages; }
    Status permitted(const Document& document) const override { return state_.permitted(document); }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.watermark.remove: %1 page count
    Scope scope() const override { return state_.scope(); }
    std::optional<Error> refusal() const override { return refusal_; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;
    // {"type": "watermark.remove", "key": "<hex>", "ids": ["<hex>", ...]}
    QJsonObject serialize() const override;
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    DecorationRemove state_;
    std::optional<Error> refusal_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDS_DECORATIONCOMMANDS_H
