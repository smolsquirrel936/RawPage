// core/cmd/commands/PageCommands -- implementation. Stage 3 T5; Stage 7 T2 the page-order commands. See the header.

#include "core/cmd/commands/PageCommands.h"

#include "core/backend.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <iterator>
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

// `{"pages": [...]}` as every page command writes it -- defined with the page-order commands below.
Result<std::vector<int>> pagesFrom(const QJsonObject& json, QLatin1String type);

} // namespace

RotatePageCommand::RotatePageCommand(int pageIndex, int degreesClockwise)
    : RotatePageCommand(std::vector<int>{pageIndex}, degreesClockwise)
{
}

RotatePageCommand::RotatePageCommand(std::vector<int> pages, int degreesClockwise)
    : pages_(std::move(pages)), degrees_(degreesClockwise % 90 == 0 ? normalizeTurn(degreesClockwise) : -1)
{
}

bool RotatePageCommand::isValid() const noexcept
{
    if (pages_.empty() || static_cast<long long>(pages_.size()) > kMaxPagesPerCommand) return false;
    for (std::size_t at = 0; at < pages_.size(); ++at) {
        if (pages_[at] < 0 || pages_[at] > kMaxPageIndex) return false;
        if (at > 0 && pages_[at] <= pages_[at - 1]) return false;
    }
    return degrees_ == 90 || degrees_ == 180 || degrees_ == 270;
}

CatalogText RotatePageCommand::name() const
{
    if (pages_.size() == 1)
        return CatalogText{QLatin1String("command.page.rotate"), {QString::number(pageIndex() + 1), QString::number(degrees_)}};
    return CatalogText{QLatin1String("command.page.rotatePages"),
                       {QString::number(static_cast<qulonglong>(pages_.size())), QString::number(degrees_)}};
}

bool RotatePageCommand::turn(Document& document, MutationSink& sink, int degrees)
{
    // Validate first: nothing below may run for a page that does not exist.
    if (!isValid() || pages_.back() >= document.pages().count())
        return false;

    std::vector<int> before;
    before.reserve(pages_.size());
    for (const int page : pages_) before.push_back(static_cast<int>(document.pages().rotation(page)));
    for (std::size_t at = 0; at < pages_.size(); ++at) {
        if (sink.setPageRotation(pages_[at], normalizeTurn(before[at] + degrees))) continue;
        // THE ENGINE SIDE REFUSED part-way: the pages already turned are turned back, so the sink holds what it held
        // and the model was never touched -- `Command.h`'s contract for a `false` return.
        for (std::size_t back = 0; back < at; ++back) (void)sink.setPageRotation(pages_[back], before[back]);
        return false;
    }

    // Cannot fail for an index checked above -- and if it ever could, the sink would already hold the
    // new rotation, which is exactly the partial application CommandJournal exists to report.
    for (std::size_t at = 0; at < pages_.size(); ++at) document.pages().setRotation(pages_[at], normalizeTurn(before[at] + degrees));
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
    // ONE PAGE IS STAGE 3'S LINE, byte for byte; a set is the page-order commands' `pages` array (S7 T8).
    if (pages_.size() == 1)
        return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                           {QStringLiteral("page"), pages_.front()},
                           {QStringLiteral("degrees"), degrees_}};
    QJsonArray pages;
    for (const int page : pages_) pages.append(page);
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("pages"), pages},
                       {QStringLiteral("degrees"), degrees_}};
}

Result<std::unique_ptr<Command>> RotatePageCommand::deserialize(const QJsonObject& json)
{
    // Every field checked for presence, type and range before a command exists: this is the recovery
    // journal's parser, and the journal is a file on disk (guide case A-45).
    const QJsonValue type = json.value(QStringLiteral("type"));
    if (!type.isString() || type.toString() != QLatin1String(kTypeId))
        return badCommand("type");

    const QJsonValue degrees = json.value(QStringLiteral("degrees"));
    if (!degrees.isDouble()) return badCommand("degrees");
    const double degreeValue = degrees.toDouble();
    if (degreeValue != 90.0 && degreeValue != 180.0 && degreeValue != 270.0)
        return badCommand("degrees");

    // EITHER FORM, NEVER BOTH (S7 T8): a line naming a page and a set at once says two things, and neither is taken.
    const bool hasPage = json.contains(QStringLiteral("page"));
    if (hasPage == json.contains(QStringLiteral("pages")))
        return badCommand("page");
    if (!hasPage) {
        Result<std::vector<int>> pages = pagesFrom(json, kTypeId);
        if (!pages) return pages.error();
        return std::unique_ptr<Command>(new RotatePageCommand(std::move(pages).value(), static_cast<int>(degreeValue)));
    }

    // A JSON number is a double: it must be a whole number in range before it becomes an int.
    const QJsonValue page = json.value(QStringLiteral("page"));
    if (!page.isDouble()) return badCommand("page");
    const double pageValue = page.toDouble();
    if (!std::isfinite(pageValue) || std::trunc(pageValue) != pageValue || pageValue < 0.0
        || pageValue > static_cast<double>(kMaxPageIndex)) {
        return badCommand("page");
    }

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

// ==========================================================================================
// Pages from elsewhere -- Stage 7 T5. See the header.
// ==========================================================================================

namespace {

QString graphJson(const QByteArray& graph)
{
    return QString::fromLatin1(graph.toBase64());
}

// `{"graph": "<base64>"}` as an insert and a replace write it: strict base64, not empty, inside the cap.
Result<QByteArray> graphFrom(const QJsonObject& json, QLatin1String type)
{
    const QJsonValue value = json.value(QStringLiteral("graph"));
    if (!value.isString()) return badPageCommand(type, "graph");
    const QString text = value.toString();
    if (text.isEmpty() || text.size() > (kMaxCopiedGraphBytes / 3 + 1) * 4) return badPageCommand(type, "graph");
    auto decoded = QByteArray::fromBase64Encoding(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > kMaxCopiedGraphBytes) return badPageCommand(type, "graph");
    return decoded.decoded;
}

// A whole number in [low, high], or nothing.
std::optional<int> wholeIn(const QJsonValue& value, double low, double high)
{
    if (!value.isDouble()) return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number || number < low || number > high) return std::nullopt;
    return static_cast<int>(number);
}

} // namespace

// ---- InsertPagesCommand ------------------------------------------------------------------------

InsertPagesCommand::InsertPagesCommand(int at, CopiedPages pages, QString key)
    : at_(at), pages_(std::move(pages)), key_(key.isEmpty() ? newCommandKey() : std::move(key))
{
}

CatalogText InsertPagesCommand::name() const
{
    return pageCountName("command.page.insert", static_cast<std::size_t>(std::max(pages_.pageCount, 0)));
}

Scope InsertPagesCommand::scope() const
{
    return map_ ? Scope::pageCommand(*map_) : Scope::wholeDocument();
}

bool InsertPagesCommand::apply(Document& document, MutationSink& sink)
{
    refusal_.reset();
    const int count = document.pages().count();
    if (pages_.isEmpty() || at_ < 0 || at_ > count) return false;
    auto inserted = sink.insertPages(at_, key_, pages_.graph);
    if (!inserted) {
        refusal_ = inserted.error();
        return false;
    }
    // The map is the SINK's count -- what the payload holds -- so a payload whose stated count is wrong cannot make
    // the model and the sink disagree.
    const std::size_t made = inserted.value().pages.size();
    const std::optional<PageMap> map = PageMap::insertion(count, at_, static_cast<int>(made));
    if (!map || inserted.value().listings.size() != made) return false;   // the sink broke its contract
    report_ = pages_.report;
    report_.references.insert(report_.references.end(), inserted.value().report.references.begin(),
                              inserted.value().report.references.end());
    map_ = map;
    return applyPageMapToModel(document, *map, std::move(inserted.value().pages), std::move(inserted.value().listings));
}

bool InsertPagesCommand::revert(Document& document, MutationSink& sink)
{
    if (!map_ || map_->newCount() != document.pages().count()) return false;
    // The pages go, KEPT under the key: a redo puts back the same ones (the sink's contract).
    if (!sink.removePages(map_->inserted(), key_)) return false;
    return applyPageMapToModel(document, map_->inverse(), {}, {});
}

QJsonObject InsertPagesCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("at"), at_},
                       {QStringLiteral("key"), key_},
                       {QStringLiteral("pages"), pages_.pageCount},
                       {QStringLiteral("graph"), graphJson(pages_.graph)}};
}

Result<std::unique_ptr<Command>> InsertPagesCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badPageCommand(kTypeId, "type");
    const std::optional<int> at = wholeIn(json.value(QStringLiteral("at")), 0.0, RotatePageCommand::kMaxPageIndex);
    if (!at) return badPageCommand(kTypeId, "at");
    auto key = keyFrom(json, kTypeId);
    if (!key) return key.error();
    const std::optional<int> count = wholeIn(json.value(QStringLiteral("pages")), 1.0, kMaxPagesPerCommand);
    if (!count) return badPageCommand(kTypeId, "pages");
    auto graph = graphFrom(json, kTypeId);
    if (!graph) return graph.error();
    CopiedPages pages;
    pages.graph = std::move(graph).value();
    pages.pageCount = *count;
    return std::unique_ptr<Command>(new InsertPagesCommand(*at, std::move(pages), std::move(key).value()));
}

// ---- ReplacePagesCommand -----------------------------------------------------------------------

ReplacePagesCommand::ReplacePagesCommand(std::vector<int> pages, CopiedPages copied, QString key)
    : pages_(std::move(pages)), copied_(std::move(copied)), key_(key.isEmpty() ? newCommandKey() : std::move(key))
{
}

CatalogText ReplacePagesCommand::name() const
{
    return pageCountName("command.page.replace", pages_.size());
}

Status ReplacePagesCommand::permitted(const Document& document) const
{
    for (const int page : pages_) {
        if (Status status = document.permissions().mayEditPage(page); !status) return status;
        if (Status status = document.permissions().mayAnnotate(page); !status) return status;
    }
    return Status{};
}

bool ReplacePagesCommand::apply(Document& document, MutationSink& sink)
{
    refusal_.reset();
    if (copied_.isEmpty() || !PageMap::isPageSet(document.pages().count(), pages_)) return false;
    std::vector<Page> before;
    for (const int page : pages_) before.push_back(*document.pages().at(page));

    auto replaced = sink.replacePages(pages_, key_, copied_.graph);
    if (!replaced) {
        refusal_ = replaced.error();
        return false;
    }
    if (replaced.value().pages.size() != pages_.size() || replaced.value().listings.size() != pages_.size())
        return false;   // the sink broke its contract

    // The model's half, which cannot refuse for a set checked above: each page's facts, and the payload page's
    // annotations listed after the page's own.
    std::vector<std::pair<AnnotationPlacement, Annotation>> entries;
    std::vector<AnnotationPlacement> added;
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        const int page = pages_[i];
        int position = static_cast<int>(document.annotations().listing(page).size());
        for (const AnnotationStore::PageListing::Entry& entry : replaced.value().listings[i].entries) {
            if (!entry.annotation) continue;
            entries.emplace_back(AnnotationPlacement{page, position}, *entry.annotation);
            added.push_back(AnnotationPlacement{page, position++});
        }
    }
    if (!entries.empty() && !document.annotations().insert(entries)) return false;
    for (std::size_t i = 0; i < pages_.size(); ++i) document.pages().setPage(pages_[i], replaced.value().pages[i]);
    before_ = std::move(before);
    added_ = std::move(added);
    report_ = copied_.report;
    report_.references.insert(report_.references.end(), replaced.value().report.references.begin(),
                              replaced.value().report.references.end());
    return true;
}

bool ReplacePagesCommand::revert(Document& document, MutationSink& sink)
{
    if (before_.size() != pages_.size() || !PageMap::isPageSet(document.pages().count(), pages_)) return false;
    if (!sink.restoreReplacedPages(pages_, key_)) return false;
    // The annotations it listed, last first -- each placement then names the entry it named when it was made.
    if (!added_.empty()) {
        std::vector<AnnotationPlacement> removal(added_.rbegin(), added_.rend());
        if (!document.annotations().remove(removal)) return false;
    }
    for (std::size_t i = 0; i < pages_.size(); ++i) document.pages().setPage(pages_[i], before_[i]);
    return true;
}

QJsonObject ReplacePagesCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("pages"), pagesJson(pages_)},
                       {QStringLiteral("key"), key_},
                       {QStringLiteral("graph"), graphJson(copied_.graph)}};
}

Result<std::unique_ptr<Command>> ReplacePagesCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badPageCommand(kTypeId, "type");
    auto pages = pagesFrom(json, kTypeId);
    if (!pages) return pages.error();
    auto key = keyFrom(json, kTypeId);
    if (!key) return key.error();
    auto graph = graphFrom(json, kTypeId);
    if (!graph) return graph.error();
    CopiedPages copied;
    copied.graph = std::move(graph).value();
    copied.pageCount = static_cast<int>(pages.value().size());
    return std::unique_ptr<Command>(new ReplacePagesCommand(std::move(pages).value(), std::move(copied), std::move(key).value()));
}

// ==========================================================================================
// Boxes, crop, resize, renumber and thumbnails -- Stage 7 T9. See the header.
// ==========================================================================================

namespace {

constexpr double kMaxPageBoxPoints = 14'400.0;   // ISO 32000-1 Annex C, as `blankPages` bounds a page

Error boxError(const char* key, const QString& context = QString())
{
    return Error{ErrorCode::InvalidArgument, QLatin1String(key), context};
}

// A box at a ten-thousandth of a point -- what a content stream writes (`ContentStreamBuilder::number`), far below a
// device pixel at any zoom -- so what a command asks is what reads back from the file, to the bit.
PdfRect quantized(PdfRect box)
{
    const auto at = [](double value) { return std::round(value * 10000.0) / 10000.0; };
    const double left = at(box.left());
    const double bottom = at(box.bottom());
    return PdfRect{PdfPoint{left, bottom}, PdfSize{at(box.right()) - left, at(box.top()) - bottom}};
}

PageBoxes quantized(const PageBoxes& boxes)
{
    PageBoxes out = boxes;
    out.media = quantized(out.media);
    for (std::optional<PdfRect>* box : {&out.crop, &out.bleed, &out.trim, &out.art}) {
        if (*box) **box = quantized(**box);
    }
    return out;
}

bool tooSmall(PdfRect box)
{
    return box.size.width < kMinPageBoxPoints || box.size.height < kMinPageBoxPoints;
}

QJsonArray rectJson(PdfRect rect)
{
    return QJsonArray{rect.left(), rect.bottom(), rect.right(), rect.top()};
}

// `[left, bottom, right, top]`, finite and inside Annex C's coordinate range, as every box is written.
std::optional<PdfRect> rectFrom(const QJsonValue& value)
{
    if (!value.isArray()) return std::nullopt;
    const QJsonArray array = value.toArray();
    if (array.size() != 4) return std::nullopt;
    double edges[4];
    for (int i = 0; i < 4; ++i) {
        if (!array[i].isDouble()) return std::nullopt;
        edges[i] = array[i].toDouble();
        if (!std::isfinite(edges[i]) || std::abs(edges[i]) > 1.0e7) return std::nullopt;
    }
    if (edges[2] < edges[0] || edges[3] < edges[1]) return std::nullopt;
    return PdfRect{PdfPoint{edges[0], edges[1]}, PdfSize{edges[2] - edges[0], edges[3] - edges[1]}};
}

const char* const kBoxNames[] = {"media", "crop", "bleed", "trim", "art"};

QJsonObject boxesJson(const PageBoxes& boxes)
{
    QJsonObject out{{QStringLiteral("media"), rectJson(boxes.media)}};
    const std::optional<PdfRect>* others[] = {&boxes.crop, &boxes.bleed, &boxes.trim, &boxes.art};
    for (int i = 0; i < 4; ++i) {
        if (*others[i]) out.insert(QLatin1String(kBoxNames[i + 1]), rectJson(**others[i]));
    }
    return out;
}

std::optional<PageBoxes> boxesFrom(const QJsonValue& value)
{
    if (!value.isObject()) return std::nullopt;
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (std::none_of(std::begin(kBoxNames), std::end(kBoxNames), [&](const char* name) { return it.key() == QLatin1String(name); }))
            return std::nullopt;   // a key no box has: a line that says something this build does not understand
    }
    PageBoxes boxes;
    const std::optional<PdfRect> media = rectFrom(object.value(QStringLiteral("media")));
    if (!media) return std::nullopt;
    boxes.media = *media;
    std::optional<PdfRect>* others[] = {&boxes.crop, &boxes.bleed, &boxes.trim, &boxes.art};
    for (int i = 0; i < 4; ++i) {
        const QLatin1String name(kBoxNames[i + 1]);
        if (!object.contains(name)) continue;
        *others[i] = rectFrom(object.value(name));
        if (!*others[i]) return std::nullopt;
    }
    return boxes;
}

// The model's labels, every page.
std::vector<QString> labelsOf(const Document& document)
{
    std::vector<QString> out;
    out.reserve(static_cast<std::size_t>(document.pages().count()));
    for (const Page& page : document.pages().pages()) out.push_back(page.label());
    return out;
}

// Page `pages[i]` of the model takes `boxes[i]`. Cannot fail for a set checked first.
void setModelBoxes(Document& document, const std::vector<int>& pages, const std::vector<PageBoxes>& boxes)
{
    for (std::size_t i = 0; i < pages.size(); ++i) {
        Page page = *document.pages().at(pages[i]);
        setBoxes(page, boxes[i]);
        document.pages().setPage(pages[i], std::move(page));
    }
}

void restoreModelPages(Document& document, const std::vector<int>& pages, const std::vector<Page>& before)
{
    for (std::size_t i = 0; i < pages.size() && i < before.size(); ++i) document.pages().setPage(pages[i], before[i]);
}

// A page-box command may name every page it changes: each may be edited (D7-21's per-page rule, as a rotation is).
Status mayEditEach(const Document& document, const std::vector<int>& pages)
{
    for (const int page : pages) {
        if (Status status = document.permissions().mayEditPage(page); !status) return status;
    }
    return Status{};
}

const char* styleName(PageLabelStyle style)
{
    switch (style) {
    case PageLabelStyle::None: return "";
    case PageLabelStyle::Decimal: return "D";
    case PageLabelStyle::RomanUpper: return "R";
    case PageLabelStyle::RomanLower: return "r";
    case PageLabelStyle::LettersUpper: return "A";
    case PageLabelStyle::LettersLower: return "a";
    }
    return "D";
}

std::optional<PageLabelStyle> styleFrom(const QString& name)
{
    for (const PageLabelStyle style : {PageLabelStyle::None, PageLabelStyle::Decimal, PageLabelStyle::RomanUpper,
                                       PageLabelStyle::RomanLower, PageLabelStyle::LettersUpper, PageLabelStyle::LettersLower}) {
        if (name == QLatin1String(styleName(style))) return style;
    }
    return std::nullopt;
}

} // namespace

Result<PageBoxes> clipToMediaBox(const PageBoxes& boxes, std::vector<PageBoxKind>* clipped)
{
    if (tooSmall(boxes.media) || boxes.media.size.width > kMaxPageBoxPoints || boxes.media.size.height > kMaxPageBoxPoints)
        return boxError("error.pages.boxTooSmall", QStringLiteral("media"));
    PageBoxes out = quantized(boxes);
    std::optional<PdfRect>* others[] = {&out.crop, &out.bleed, &out.trim, &out.art};
    const PageBoxKind kinds[] = {PageBoxKind::Crop, PageBoxKind::Bleed, PageBoxKind::Trim, PageBoxKind::Art};
    for (int i = 0; i < 4; ++i) {
        if (!*others[i]) continue;
        const PdfRect asked = **others[i];
        const PdfRect inside = quantized(intersect(asked, out.media));
        if (inside.size.width <= 0.0 || inside.size.height <= 0.0) return boxError("error.pages.boxOutside", QLatin1String(kBoxNames[i + 1]));
        if (i == 0 && tooSmall(inside)) return boxError("error.pages.boxTooSmall", QStringLiteral("crop"));
        if (!(inside == asked) && clipped != nullptr) clipped->push_back(kinds[i]);
        **others[i] = inside;
    }
    return out;
}

PdfRect whiteMarginBox(PdfRect content, double padding, PdfRect cropBox)
{
    const double pad = std::max(0.0, padding);
    const PdfRect padded{PdfPoint{content.left() - pad, content.bottom() - pad},
                         PdfSize{content.size.width + 2.0 * pad, content.size.height + 2.0 * pad}};
    const PdfRect inside = intersect(padded, cropBox);
    return inside.size.width > 0.0 && inside.size.height > 0.0 ? inside : cropBox;
}

std::vector<int> pagesOfSameSize(const PageTree& pages, int page)
{
    std::vector<int> out;
    const Page* reference = pages.at(page);
    if (reference == nullptr) return out;
    const PdfSize want = reference->displaySize();
    for (int i = 0; i < pages.count(); ++i) {
        const PdfSize size = pages.displaySize(i);
        if (std::abs(size.width - want.width) < 0.01 && std::abs(size.height - want.height) < 0.01) out.push_back(i);
    }
    return out;
}

Result<ResizePlan> planResize(const Page& page, PdfSize target, bool scale, ResizeAnchor anchor)
{
    if (!std::isfinite(target.width) || !std::isfinite(target.height) || target.width < kMinPageBoxPoints
        || target.height < kMinPageBoxPoints)
        return boxError("error.pages.boxTooSmall", QStringLiteral("target"));
    if (target.width > kMaxPageBoxPoints || target.height > kMaxPageBoxPoints) return boxError("error.pages.badSize");

    // THE TARGET AND THE ANCHOR ARE AS THE PAGE IS SHOWN; the boxes are in its own, unturned space. A quarter turn swaps
    // the target's sides, and the anchor -- a fraction across and down the shown page -- is read back through the turn.
    const int turn = static_cast<int>(page.rotation());
    const bool quarter = turn == 90 || turn == 270;
    const double width = quarter ? target.height : target.width;
    const double height = quarter ? target.width : target.height;
    const int at = static_cast<int>(anchor);
    const double across = (at % 3) * 0.5;   // 0 left, 0.5 centre, 1 right -- as shown
    const double down = (at / 3) * 0.5;     // 0 top, 0.5 middle, 1 bottom -- as shown
    double ux = across;                     // in the page's own space: 0 left ... 1 right
    double uy = 1.0 - down;                 //                          0 bottom ... 1 top
    switch (turn) {
    case 90: ux = down; uy = across; break;
    case 180: ux = 1.0 - across; uy = down; break;
    case 270: ux = 1.0 - down; uy = 1.0 - across; break;
    default: break;
    }

    const PdfRect shown = page.cropBox();
    ResizePlan plan;
    if (!scale) {
        const double left = shown.left() - (width - shown.size.width) * ux;
        const double bottom = shown.bottom() - (height - shown.size.height) * uy;
        plan.boxes.media = quantized(PdfRect{PdfPoint{left, bottom}, PdfSize{width, height}});
        return plan;
    }
    if (shown.size.width <= 0.0 || shown.size.height <= 0.0) return boxError("error.pages.boxTooSmall", QStringLiteral("crop"));
    const double s = std::min(width / shown.size.width, height / shown.size.height);
    const double left = (width - shown.size.width * s) * ux;
    const double bottom = (height - shown.size.height * s) * uy;
    plan.boxes.media = quantized(PdfRect{PdfPoint{0.0, 0.0}, PdfSize{width, height}});
    plan.wrap = PageContentWrap{{s, 0.0, 0.0, s, left - s * shown.left(), bottom - s * shown.bottom()}, shown};
    return plan;
}

// ---- CropPagesCommand --------------------------------------------------------------------------

CropPagesCommand::CropPagesCommand(std::vector<int> pages, std::vector<PageBoxes> boxes, QString key)
    : pages_(std::move(pages)), boxes_(std::move(boxes)), key_(key.isEmpty() ? newCommandKey() : std::move(key))
{
}

CatalogText CropPagesCommand::name() const
{
    return pageCountName("command.page.crop", pages_.size());
}

Status CropPagesCommand::permitted(const Document& document) const
{
    return mayEditEach(document, pages_);
}

bool CropPagesCommand::apply(Document& document, MutationSink& sink)
{
    refusal_.reset();
    if (!PageMap::isPageSet(document.pages().count(), pages_)) return false;
    if (boxes_.size() != 1 && boxes_.size() != pages_.size()) return false;
    std::vector<PageBoxes> clipped;
    PageCommandReport report;
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        std::vector<PageBoxKind> cut;
        Result<PageBoxes> inside = clipToMediaBox(boxes_[boxes_.size() == 1 ? 0 : i], &cut);
        if (!inside) {
            refusal_ = inside.error();
            return false;
        }
        for (const PageBoxKind kind : cut)
            report.references.push_back(PageReference{PageReference::Kind::PageBox, PageReference::Treatment::Clipped,
                                                      QLatin1String(kBoxNames[static_cast<int>(kind)]), -1, pages_[i]});
        clipped.push_back(std::move(inside).value());
    }
    if (Status set = sink.setPageBoxes(pages_, clipped, key_); !set) {
        refusal_ = set.error();
        return false;
    }
    before_.clear();
    for (const int page : pages_) before_.push_back(*document.pages().at(page));
    setModelBoxes(document, pages_, clipped);
    report_ = std::move(report);
    return true;
}

bool CropPagesCommand::revert(Document& document, MutationSink& sink)
{
    if (before_.size() != pages_.size() || !PageMap::isPageSet(document.pages().count(), pages_)) return false;
    if (!sink.restorePageBoxes(pages_, key_)) return false;
    restoreModelPages(document, pages_, before_);
    return true;
}

QJsonObject CropPagesCommand::serialize() const
{
    QJsonArray boxes;
    for (const PageBoxes& box : boxes_) boxes.append(boxesJson(box));
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("pages"), pagesJson(pages_)},
                       {QStringLiteral("key"), key_},
                       {QStringLiteral("boxes"), boxes}};
}

Result<std::unique_ptr<Command>> CropPagesCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badPageCommand(kTypeId, "type");
    auto pages = pagesFrom(json, kTypeId);
    if (!pages) return pages.error();
    auto key = keyFrom(json, kTypeId);
    if (!key) return key.error();
    const QJsonValue value = json.value(QStringLiteral("boxes"));
    if (!value.isArray()) return badPageCommand(kTypeId, "boxes");
    const QJsonArray array = value.toArray();
    if (array.size() != 1 && array.size() != static_cast<qsizetype>(pages.value().size())) return badPageCommand(kTypeId, "boxes");
    std::vector<PageBoxes> boxes;
    for (const QJsonValue item : array) {
        std::optional<PageBoxes> box = boxesFrom(item);
        if (!box) return badPageCommand(kTypeId, "boxes");
        boxes.push_back(*box);
    }
    return std::unique_ptr<Command>(new CropPagesCommand(std::move(pages).value(), std::move(boxes), std::move(key).value()));
}

// ---- ResizePagesCommand ------------------------------------------------------------------------

ResizePagesCommand::ResizePagesCommand(std::vector<int> pages, PdfSize target, bool scale, ResizeAnchor anchor, QString key)
    : pages_(std::move(pages)), target_(target), scale_(scale), anchor_(anchor),
      key_(key.isEmpty() ? newCommandKey() : std::move(key))
{
}

CatalogText ResizePagesCommand::name() const
{
    return pageCountName("command.page.resize", pages_.size());
}

Status ResizePagesCommand::permitted(const Document& document) const
{
    return mayEditEach(document, pages_);
}

bool ResizePagesCommand::apply(Document& document, MutationSink& sink)
{
    refusal_.reset();
    if (!PageMap::isPageSet(document.pages().count(), pages_)) return false;
    std::vector<PageBoxes> boxes;
    std::vector<PageContentWrap> wraps;
    PageCommandReport report;
    for (const int page : pages_) {
        Result<ResizePlan> plan = planResize(*document.pages().at(page), target_, scale_, anchor_);
        if (!plan) {
            refusal_ = plan.error();
            return false;
        }
        boxes.push_back(plan.value().boxes);
        if (plan.value().wrap) wraps.push_back(*plan.value().wrap);
        // A COMMENT KEEPS ITS RECTANGLE while the content beneath it is scaled (D7-32): said, per page that has any.
        if (scale_ && !document.annotations().listing(page).empty())
            report.references.push_back(PageReference{PageReference::Kind::PageComments, PageReference::Treatment::Reported,
                                                      QString(), -1, page});
    }
    if (Status set = sink.setPageBoxes(pages_, boxes, key_); !set) {
        refusal_ = set.error();
        return false;
    }
    if (scale_) {
        if (Status wrapped = sink.wrapPageContents(pages_, wraps, key_); !wrapped) {
            // THE SECOND HALF REFUSED: the first is taken back, so a refusal changes nothing (`Command.h`).
            (void)sink.restorePageBoxes(pages_, key_);
            refusal_ = wrapped.error();
            return false;
        }
    }
    before_.clear();
    for (const int page : pages_) before_.push_back(*document.pages().at(page));
    setModelBoxes(document, pages_, boxes);
    report_ = std::move(report);
    return true;
}

bool ResizePagesCommand::revert(Document& document, MutationSink& sink)
{
    if (before_.size() != pages_.size() || !PageMap::isPageSet(document.pages().count(), pages_)) return false;
    if (scale_ && !sink.unwrapPageContents(pages_, key_)) return false;
    if (!sink.restorePageBoxes(pages_, key_)) return false;
    restoreModelPages(document, pages_, before_);
    return true;
}

QJsonObject ResizePagesCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("pages"), pagesJson(pages_)},
                       {QStringLiteral("key"), key_},
                       {QStringLiteral("width"), target_.width},
                       {QStringLiteral("height"), target_.height},
                       {QStringLiteral("scale"), scale_},
                       {QStringLiteral("anchor"), static_cast<int>(anchor_)}};
}

Result<std::unique_ptr<Command>> ResizePagesCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badPageCommand(kTypeId, "type");
    auto pages = pagesFrom(json, kTypeId);
    if (!pages) return pages.error();
    auto key = keyFrom(json, kTypeId);
    if (!key) return key.error();
    PdfSize target;
    for (const auto& [field, into] : {std::pair{"width", &target.width}, std::pair{"height", &target.height}}) {
        const QJsonValue value = json.value(QLatin1String(field));
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < kMinPageBoxPoints
            || value.toDouble() > kMaxPageBoxPoints)
            return badPageCommand(kTypeId, field);
        *into = value.toDouble();
    }
    const QJsonValue scale = json.value(QStringLiteral("scale"));
    if (!scale.isBool()) return badPageCommand(kTypeId, "scale");
    const std::optional<int> anchor = wholeIn(json.value(QStringLiteral("anchor")), 0.0, 8.0);
    if (!anchor) return badPageCommand(kTypeId, "anchor");
    return std::unique_ptr<Command>(new ResizePagesCommand(std::move(pages).value(), target, scale.toBool(),
                                                           static_cast<ResizeAnchor>(*anchor), std::move(key).value()));
}

// ---- RenumberPagesCommand ----------------------------------------------------------------------

RenumberPagesCommand::RenumberPagesCommand(std::vector<int> pages, PageLabelStyle style, QString prefix, int start, QString key)
    : pages_(std::move(pages)), style_(style), prefix_(std::move(prefix)), start_(start),
      key_(key.isEmpty() ? newCommandKey() : std::move(key))
{
}

CatalogText RenumberPagesCommand::name() const
{
    return pageCountName("command.page.renumber", pages_.size());
}

Status RenumberPagesCommand::permitted(const Document& document) const
{
    return mayEditEach(document, pages_);
}

bool RenumberPagesCommand::apply(Document& document, MutationSink& sink)
{
    refusal_.reset();
    if (!PageMap::isPageSet(document.pages().count(), pages_)) return false;
    if (start_ < 1 || start_ > kMaxStart || prefix_.size() > kMaxPrefix) return false;
    if (static_cast<long long>(start_) + static_cast<long long>(pages_.size()) - 1 > kMaxStart) return false;
    std::vector<PageLabelSpec> labels;
    labels.reserve(pages_.size());
    for (std::size_t i = 0; i < pages_.size(); ++i) labels.push_back(PageLabelSpec{style_, prefix_, start_ + static_cast<int>(i)});
    Result<std::vector<QString>> shown = sink.setPageLabels(pages_, labels, key_);
    if (!shown) {
        refusal_ = shown.error();
        return false;
    }
    if (static_cast<int>(shown.value().size()) != document.pages().count()) {
        (void)sink.restorePageLabels(key_);   // the sink broke its contract: nothing it did is kept
        return false;
    }
    before_ = labelsOf(document);
    for (int page = 0; page < document.pages().count(); ++page) {
        Page changed = *document.pages().at(page);
        changed.setLabel(shown.value()[static_cast<std::size_t>(page)]);
        document.pages().setPage(page, std::move(changed));
    }
    return true;
}

bool RenumberPagesCommand::revert(Document& document, MutationSink& sink)
{
    if (static_cast<int>(before_.size()) != document.pages().count()) return false;
    Result<std::vector<QString>> shown = sink.restorePageLabels(key_);
    if (!shown) return false;
    for (int page = 0; page < document.pages().count(); ++page) {
        Page changed = *document.pages().at(page);
        changed.setLabel(before_[static_cast<std::size_t>(page)]);
        document.pages().setPage(page, std::move(changed));
    }
    return true;
}

QJsonObject RenumberPagesCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("pages"), pagesJson(pages_)},
                       {QStringLiteral("key"), key_},
                       {QStringLiteral("style"), QLatin1String(styleName(style_))},
                       {QStringLiteral("prefix"), prefix_},
                       {QStringLiteral("start"), start_}};
}

Result<std::unique_ptr<Command>> RenumberPagesCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badPageCommand(kTypeId, "type");
    auto pages = pagesFrom(json, kTypeId);
    if (!pages) return pages.error();
    auto key = keyFrom(json, kTypeId);
    if (!key) return key.error();
    const QJsonValue style = json.value(QStringLiteral("style"));
    const std::optional<PageLabelStyle> parsed = style.isString() ? styleFrom(style.toString()) : std::nullopt;
    if (!parsed) return badPageCommand(kTypeId, "style");
    const QJsonValue prefix = json.value(QStringLiteral("prefix"));
    if (!prefix.isString() || prefix.toString().size() > kMaxPrefix) return badPageCommand(kTypeId, "prefix");
    const std::optional<int> start = wholeIn(json.value(QStringLiteral("start")), 1.0, kMaxStart);
    if (!start || static_cast<long long>(*start) + static_cast<long long>(pages.value().size()) - 1 > kMaxStart)
        return badPageCommand(kTypeId, "start");
    return std::unique_ptr<Command>(new RenumberPagesCommand(std::move(pages).value(), *parsed, prefix.toString(), *start,
                                                             std::move(key).value()));
}

// ---- SetEmbeddedThumbnailsCommand --------------------------------------------------------------

SetEmbeddedThumbnailsCommand::SetEmbeddedThumbnailsCommand(std::vector<int> pages, std::vector<PageThumbnail> thumbnails,
                                                           QString key)
    : pages_(std::move(pages)), thumbnails_(std::move(thumbnails)), key_(key.isEmpty() ? newCommandKey() : std::move(key))
{
}

CatalogText SetEmbeddedThumbnailsCommand::name() const
{
    return pageCountName(thumbnails_.empty() ? "command.page.removeThumbnails" : "command.page.embedThumbnails", pages_.size());
}

Status SetEmbeddedThumbnailsCommand::permitted(const Document& document) const
{
    return mayEditEach(document, pages_);
}

bool SetEmbeddedThumbnailsCommand::apply(Document& document, MutationSink& sink)
{
    refusal_.reset();
    if (!PageMap::isPageSet(document.pages().count(), pages_)) return false;
    if (!thumbnails_.empty() && thumbnails_.size() != pages_.size()) return false;
    for (const PageThumbnail& thumbnail : thumbnails_) {
        if (!thumbnail.isWellFormed()) return false;
    }
    const std::vector<PageThumbnail> none(pages_.size());
    if (Status set = sink.setPageThumbnails(pages_, thumbnails_.empty() ? none : thumbnails_, key_); !set) {
        refusal_ = set.error();
        return false;
    }
    return true;   // the model holds no thumbnail: a reader shows the page
}

bool SetEmbeddedThumbnailsCommand::revert(Document& document, MutationSink& sink)
{
    if (!PageMap::isPageSet(document.pages().count(), pages_)) return false;
    return static_cast<bool>(sink.restorePageThumbnails(pages_, key_));
}

QJsonObject SetEmbeddedThumbnailsCommand::serialize() const
{
    QJsonObject out{{QStringLiteral("type"), QLatin1String(kTypeId)},
                    {QStringLiteral("pages"), pagesJson(pages_)},
                    {QStringLiteral("key"), key_}};
    if (thumbnails_.empty()) return out;
    QJsonArray thumbnails;
    for (const PageThumbnail& thumbnail : thumbnails_) {
        thumbnails.append(QJsonObject{{QStringLiteral("width"), thumbnail.width},
                                      {QStringLiteral("height"), thumbnail.height},
                                      {QStringLiteral("rgb"), QString::fromLatin1(qCompress(thumbnail.rgb).toBase64())}});
    }
    out.insert(QStringLiteral("thumbnails"), thumbnails);
    return out;
}

Result<std::unique_ptr<Command>> SetEmbeddedThumbnailsCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badPageCommand(kTypeId, "type");
    auto pages = pagesFrom(json, kTypeId);
    if (!pages) return pages.error();
    auto key = keyFrom(json, kTypeId);
    if (!key) return key.error();
    std::vector<PageThumbnail> thumbnails;
    if (json.contains(QStringLiteral("thumbnails"))) {
        const QJsonValue value = json.value(QStringLiteral("thumbnails"));
        if (!value.isArray() || value.toArray().size() != static_cast<qsizetype>(pages.value().size()))
            return badPageCommand(kTypeId, "thumbnails");
        for (const QJsonValue item : value.toArray()) {
            if (!item.isObject()) return badPageCommand(kTypeId, "thumbnails");
            const QJsonObject object = item.toObject();
            const std::optional<int> width = wholeIn(object.value(QStringLiteral("width")), 1.0, kMaxThumbnailEdge);
            const std::optional<int> height = wholeIn(object.value(QStringLiteral("height")), 1.0, kMaxThumbnailEdge);
            const QJsonValue rgb = object.value(QStringLiteral("rgb"));
            if (!width || !height || !rgb.isString()) return badPageCommand(kTypeId, "thumbnails");
            auto packed = QByteArray::fromBase64Encoding(rgb.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            // `qUncompress` allocates what its first four bytes CLAIM: they must claim exactly the image, before it runs.
            const qsizetype want = static_cast<qsizetype>(*width) * *height * 3;
            if (!packed || packed.decoded.size() < 5) return badPageCommand(kTypeId, "thumbnails");
            const QByteArray& bytes = packed.decoded;
            const quint32 claimed = (static_cast<quint32>(static_cast<unsigned char>(bytes[0])) << 24)
                                    | (static_cast<quint32>(static_cast<unsigned char>(bytes[1])) << 16)
                                    | (static_cast<quint32>(static_cast<unsigned char>(bytes[2])) << 8)
                                    | static_cast<quint32>(static_cast<unsigned char>(bytes[3]));
            if (static_cast<qsizetype>(claimed) != want) return badPageCommand(kTypeId, "thumbnails");
            PageThumbnail thumbnail{*width, *height, qUncompress(bytes)};
            if (!thumbnail.isWellFormed()) return badPageCommand(kTypeId, "thumbnails");
            thumbnails.push_back(std::move(thumbnail));
        }
    }
    return std::unique_ptr<Command>(new SetEmbeddedThumbnailsCommand(std::move(pages).value(), std::move(thumbnails),
                                                                     std::move(key).value()));
}

} // namespace rawpage::core
