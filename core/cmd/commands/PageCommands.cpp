// core/cmd/commands/PageCommands -- implementation. Stage 3 T5; Stage 7 T2 the page-order commands. See the header.

#include "core/cmd/commands/PageCommands.h"

#include "core/backend.h"
#include "core/doc/Document.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>
#include <map>

namespace rawpage::core {
namespace {

int normalizeTurn(int degrees)
{
    return ((degrees % 360) + 360) % 360;
}

Error badCommand(const char* field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"),
                 QLatin1String(RotatePageCommand::kTypeId) + QLatin1Char('.') + QLatin1String(field)};
}

} // namespace

RotatePageCommand::RotatePageCommand(int pageIndex, int degreesClockwise)
    : pageIndex_(pageIndex), degrees_(degreesClockwise % 90 == 0 ? normalizeTurn(degreesClockwise) : -1)
{
}

bool RotatePageCommand::isValid() const noexcept
{
    return pageIndex_ >= 0 && pageIndex_ <= kMaxPageIndex
           && (degrees_ == 90 || degrees_ == 180 || degrees_ == 270);
}

CatalogText RotatePageCommand::name() const
{
    return CatalogText{QLatin1String("command.page.rotate"),
                       {QString::number(pageIndex_ + 1), QString::number(degrees_)}};
}

bool RotatePageCommand::turn(Document& document, MutationSink& sink, int degrees)
{
    // Validate first: nothing below may run for a page that does not exist.
    if (!isValid() || pageIndex_ >= document.pages().count())
        return false;

    const int target = normalizeTurn(static_cast<int>(document.pages().rotation(pageIndex_)) + degrees);
    if (!sink.setPageRotation(pageIndex_, target))
        return false;                       // the engine side refused; the model is untouched

    // Cannot fail for an index checked above -- and if it ever could, the sink would already hold the
    // new rotation, which is exactly the partial application CommandJournal exists to report.
    document.pages().setRotation(pageIndex_, target);
    return true;
}

bool RotatePageCommand::apply(Document& document, MutationSink& sink)
{
    return turn(document, sink, degrees_);
}

bool RotatePageCommand::revert(Document& document, MutationSink& sink)
{
    return turn(document, sink, 360 - degrees_);
}

QJsonObject RotatePageCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("page"), pageIndex_},
                       {QStringLiteral("degrees"), degrees_}};
}

Result<std::unique_ptr<Command>> RotatePageCommand::deserialize(const QJsonObject& json)
{
    // Every field checked for presence, type and range before a command exists: this is the recovery
    // journal's parser, and the journal is a file on disk (guide case A-45).
    const QJsonValue type = json.value(QStringLiteral("type"));
    if (!type.isString() || type.toString() != QLatin1String(kTypeId))
        return badCommand("type");

    const QJsonValue page = json.value(QStringLiteral("page"));
    const QJsonValue degrees = json.value(QStringLiteral("degrees"));
    if (!page.isDouble() || !degrees.isDouble())
        return badCommand(!page.isDouble() ? "page" : "degrees");

    // A JSON number is a double: it must be a whole number in range before it becomes an int.
    const double pageValue = page.toDouble();
    if (!std::isfinite(pageValue) || std::trunc(pageValue) != pageValue || pageValue < 0.0
        || pageValue > static_cast<double>(kMaxPageIndex)) {
        return badCommand("page");
    }
    const double degreeValue = degrees.toDouble();
    if (degreeValue != 90.0 && degreeValue != 180.0 && degreeValue != 270.0)
        return badCommand("degrees");

    return std::unique_ptr<Command>(
        new RotatePageCommand(static_cast<int>(pageValue), static_cast<int>(degreeValue)));
}

// ==========================================================================================
// The page-order commands -- Stage 7 T2. See the header.
// ==========================================================================================

namespace {

Error badPageCommand(QLatin1String type, const char* field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"),
                 QString(type) + QLatin1Char('.') + QLatin1String(field)};
}

// `{"pages": [...]}` as every page command writes it: whole numbers, strictly ascending, in range, bounded.
Result<std::vector<int>> pagesFrom(const QJsonObject& json, QLatin1String type)
{
    const QJsonValue value = json.value(QStringLiteral("pages"));
    if (!value.isArray()) return badPageCommand(type, "pages");
    const QJsonArray array = value.toArray();
    if (array.isEmpty() || array.size() > kMaxPagesPerCommand) return badPageCommand(type, "pages");
    std::vector<int> pages;
    pages.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue item : array) {
        if (!item.isDouble()) return badPageCommand(type, "pages");
        const double page = item.toDouble();
        if (!std::isfinite(page) || std::trunc(page) != page || page < 0.0
            || page > static_cast<double>(RotatePageCommand::kMaxPageIndex))
            return badPageCommand(type, "pages");
        if (!pages.empty() && static_cast<int>(page) <= pages.back()) return badPageCommand(type, "pages");
        pages.push_back(static_cast<int>(page));
    }
    return pages;
}

Result<QString> keyFrom(const QJsonObject& json, QLatin1String type)
{
    const QJsonValue value = json.value(QStringLiteral("key"));
    if (!value.isString()) return badPageCommand(type, "key");
    const QString key = value.toString();
    if (key.size() != 32) return badPageCommand(type, "key");
    for (const QChar c : key) {
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))))
            return badPageCommand(type, "key");
    }
    return key;
}

bool typeIs(const QJsonObject& json, QLatin1String type)
{
    const QJsonValue value = json.value(QStringLiteral("type"));
    return value.isString() && value.toString() == type;
}

QJsonArray pagesJson(const std::vector<int>& pages)
{
    QJsonArray array;
    for (const int page : pages) array.append(page);
    return array;
}

CatalogText pageCountName(const char* key, std::size_t pages)
{
    return CatalogText{QLatin1String(key), {QString::number(static_cast<qulonglong>(pages))}};
}

} // namespace

QString newCommandKey()
{
    return newAnnotationName();   // the same 128 random bits, the same spelling
}

Status pageCommandPermitted(const Document& document, const std::vector<int>& pages)
{
    if (Status status = document.permissions().mayRestructurePages(); !status) return status;
    for (const int page : pages) {
        if (Status status = document.permissions().mayEditPage(page); !status) return status;
    }
    return Status{};
}

bool applyPageMapToModel(Document& document, const PageMap& map, std::vector<Page> insertedPages,
                         std::vector<AnnotationStore::PageListing> insertedListings)
{
    // Each step is checked by the caller before the sink is told anything -- a map over the tree's count, an
    // inserted list of the map's size -- so none of these can refuse here; if one ever did, the sink already
    // holds the change, which is exactly the partial application `CommandJournal` reports.
    if (!document.pages().apply(map, std::move(insertedPages))) return false;
    if (!document.annotations().applyPageMap(map, std::move(insertedListings))) return false;
    document.permissions().applyPageMap(map);
    return true;
}

// ---- MovePagesCommand --------------------------------------------------------------------------

MovePagesCommand::MovePagesCommand(std::vector<int> pages, int to) : pages_(std::move(pages)), to_(to) {}

CatalogText MovePagesCommand::name() const
{
    return pageCountName("command.page.move", pages_.size());
}

Scope MovePagesCommand::scope() const
{
    return map_ ? Scope::pageCommand(*map_) : Scope::wholeDocument();
}

bool MovePagesCommand::apply(Document& document, MutationSink& sink)
{
    const std::optional<PageMap> map = PageMap::move(document.pages().count(), pages_, to_);
    refusal_.reset();
    if (!map || map->isIdentity()) return false;   // nothing to move is no command (guide A-17)
    auto report = sink.reorderPages(*map);
    if (!report) {
        refusal_ = report.error();
        return false;
    }
    report_ = std::move(report).value();
    map_ = map;
    return applyPageMapToModel(document, *map, {}, {});
}

bool MovePagesCommand::revert(Document& document, MutationSink& sink)
{
    if (!map_ || map_->newCount() != document.pages().count()) return false;
    const PageMap back = map_->inverse();
    if (!sink.reorderPages(back)) return false;
    return applyPageMapToModel(document, back, {}, {});
}

QJsonObject MovePagesCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("pages"), pagesJson(pages_)},
                       {QStringLiteral("to"), to_}};
}

Result<std::unique_ptr<Command>> MovePagesCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badPageCommand(kTypeId, "type");
    auto pages = pagesFrom(json, kTypeId);
    if (!pages) return pages.error();
    const QJsonValue to = json.value(QStringLiteral("to"));
    const double value = to.isDouble() ? to.toDouble() : -1.0;
    if (!std::isfinite(value) || std::trunc(value) != value || value < 0.0
        || value > static_cast<double>(RotatePageCommand::kMaxPageIndex))
        return badPageCommand(kTypeId, "to");
    return std::unique_ptr<Command>(new MovePagesCommand(std::move(pages).value(), static_cast<int>(value)));
}

// ---- DeletePagesCommand ------------------------------------------------------------------------

DeletePagesCommand::DeletePagesCommand(std::vector<int> pages, QString key)
    : pages_(std::move(pages)), key_(key.isEmpty() ? newCommandKey() : std::move(key))
{
}

CatalogText DeletePagesCommand::name() const
{
    return pageCountName("command.page.delete", pages_.size());
}

Scope DeletePagesCommand::scope() const
{
    return map_ ? Scope::pageCommand(*map_) : Scope::wholeDocument();
}

bool DeletePagesCommand::apply(Document& document, MutationSink& sink)
{
    refusal_.reset();
    if (!PageMap::isPageSet(document.pages().count(), pages_)) return false;
    const std::optional<PageMap> map = PageMap::removal(document.pages().count(), pages_);
    if (!map) return false;
    if (map->newCount() == 0) {
        // Every page: not a document (D7-21), refused by key before the sink hears of it.
        refusal_ = Error{ErrorCode::InvalidArgument, QLatin1String("error.edit.noPagesLeft"), QString()};
        return false;
    }
    std::vector<Page> removed = document.pages().removedBy(*map);
    std::vector<AnnotationStore::PageListing> listings = document.annotations().listingsRemovedBy(*map);

    auto report = sink.removePages(pages_, key_);
    if (!report) {
        refusal_ = report.error();
        return false;
    }
    report_ = std::move(report).value();
    map_ = map;
    removedPages_ = std::move(removed);
    removedListings_ = std::move(listings);
    return applyPageMapToModel(document, *map, {}, {});
}

bool DeletePagesCommand::revert(Document& document, MutationSink& sink)
{
    if (!map_ || map_->newCount() != document.pages().count()) return false;
    if (!sink.restorePages(pages_, key_)) return false;
    return applyPageMapToModel(document, map_->inverse(), removedPages_, removedListings_);
}

QJsonObject DeletePagesCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("pages"), pagesJson(pages_)},
                       {QStringLiteral("key"), key_}};
}

Result<std::unique_ptr<Command>> DeletePagesCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badPageCommand(kTypeId, "type");
    auto pages = pagesFrom(json, kTypeId);
    if (!pages) return pages.error();
    auto key = keyFrom(json, kTypeId);
    if (!key) return key.error();
    return std::unique_ptr<Command>(new DeletePagesCommand(std::move(pages).value(), std::move(key).value()));
}

// ---- DuplicatePagesCommand ---------------------------------------------------------------------

DuplicatePagesCommand::DuplicatePagesCommand(std::vector<int> pages, QString key)
    : pages_(std::move(pages)), key_(key.isEmpty() ? newCommandKey() : std::move(key))
{
}

QString DuplicatePagesCommand::copyName(int ordinal) const
{
    return key_ + QLatin1Char('-') + QString::number(ordinal);
}

CatalogText DuplicatePagesCommand::name() const
{
    return pageCountName("command.page.duplicate", pages_.size());
}

Scope DuplicatePagesCommand::scope() const
{
    return map_ ? Scope::pageCommand(*map_) : Scope::wholeDocument();
}

bool DuplicatePagesCommand::apply(Document& document, MutationSink& sink)
{
    refusal_.reset();
    if (!PageMap::isPageSet(document.pages().count(), pages_)) return false;
    const std::optional<PageMap> map = PageMap::duplication(document.pages().count(), pages_);
    if (!map) return false;
    const AnnotationStore& store = document.annotations();

    // Each page's annotations, copied: a new identity per copy, named from the key; its page the copy's; its
    // pop-up, parent and reply target the copies of those where they were copied too, and none where not. A
    // WIDGET is not copied -- a field's widgets are the field's (D7-16) -- and the report says so.
    std::vector<std::vector<AnnotationCopy>> copies;
    std::vector<AnnotationStore::PageListing> listings;
    PageCommandReport widgets;
    int ordinal = 0;
    for (const int page : pages_) {
        const int copyPage = map->newIndexOf(page) + 1;
        std::vector<AnnotationCopy> pageCopies;
        std::map<AnnotationId, AnnotationId> copied;
        for (const AnnotationListingEntry& entry : store.listing(page)) {
            if (!entry.annotation) continue;   // an entry that is no annotation: nothing to copy
            const Annotation& source = store.at(*entry.annotation);
            if (source.subtype == AnnotationSubtype::Widget) {
                widgets.references.push_back(PageReference{PageReference::Kind::WidgetNotCopied,
                                                           PageReference::Treatment::NotCopied, source.author,
                                                           source.id.kind == AnnotationId::Kind::Object ? source.id.object : -1,
                                                           page});
                continue;
            }
            if (copied.count(source.id) != 0) continue;   // listed twice on the page: one copy
            Annotation copy = source;
            copy.nm = copyName(ordinal++);
            copy.id = AnnotationId::ofCreated(copy.nm);
            copy.page = copyPage;
            copied.emplace(source.id, copy.id);
            pageCopies.push_back(AnnotationCopy{source.id, std::move(copy)});
        }
        for (AnnotationCopy& made : pageCopies) {
            for (std::optional<AnnotationId>* link : {&made.copy.popup, &made.copy.parent, &made.copy.inReplyTo}) {
                if (!*link) continue;
                const auto target = copied.find(**link);
                if (target == copied.end()) link->reset();
                else *link = target->second;
            }
        }
        AnnotationStore::PageListing listing;
        for (const AnnotationCopy& made : pageCopies) {
            AnnotationFact fact;
            fact.dictionary = true;
            fact.subtype = made.copy.subtype;
            if (made.copy.rectFinite) fact.rect = made.copy.rect;
            listing.entries.push_back({fact, made.copy});
        }
        listings.push_back(std::move(listing));
        copies.push_back(std::move(pageCopies));
    }

    auto duplicated = sink.duplicatePages(pages_, key_, copies);
    if (!duplicated) {
        refusal_ = duplicated.error();
        return false;
    }
    if (duplicated.value().labels.size() != pages_.size()) return false;   // the sink broke its contract

    std::vector<Page> made;
    made.reserve(pages_.size());
    for (std::size_t at = 0; at < pages_.size(); ++at) {
        Page page = *document.pages().at(pages_[at]);
        page.setLabel(duplicated.value().labels[at]);
        made.push_back(std::move(page));
    }
    report_ = std::move(duplicated.value().report);
    report_.references.insert(report_.references.end(), widgets.references.begin(), widgets.references.end());
    map_ = map;
    return applyPageMapToModel(document, *map, std::move(made), std::move(listings));
}

bool DuplicatePagesCommand::revert(Document& document, MutationSink& sink)
{
    if (!map_ || map_->newCount() != document.pages().count()) return false;
    // The copies go, KEPT under the key: a redo puts back the same pages (the sink's contract).
    auto removed = sink.removePages(map_->inserted(), key_);
    if (!removed) return false;
    return applyPageMapToModel(document, map_->inverse(), {}, {});
}

QJsonObject DuplicatePagesCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("pages"), pagesJson(pages_)},
                       {QStringLiteral("key"), key_}};
}

Result<std::unique_ptr<Command>> DuplicatePagesCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badPageCommand(kTypeId, "type");
    auto pages = pagesFrom(json, kTypeId);
    if (!pages) return pages.error();
    auto key = keyFrom(json, kTypeId);
    if (!key) return key.error();
    return std::unique_ptr<Command>(new DuplicatePagesCommand(std::move(pages).value(), std::move(key).value()));
}

} // namespace rawpage::core
