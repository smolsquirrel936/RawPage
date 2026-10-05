// core/cmd/commands/AnnotationCommands -- implementation. Stage 6 T3. See the header for the set, the
// placements and why the sink is asked first.

#include "core/cmd/commands/AnnotationCommands.h"

#include "core/backend.h"
#include "core/doc/AnnotationGeometry.h"
#include "core/doc/Document.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace rawpage::core {
namespace {

namespace Limits = AnnotationCommandLimits;

Error badCommand(QLatin1String typeId, const QString& field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"),
                 QString(typeId) + QLatin1Char('.') + field};
}

bool allFinite(const std::vector<double>& values)
{
    return std::all_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); });
}

// What a serialized annotation must be before a command carries it: a created identity, a page, a
// finite rectangle, finite and bounded geometry. `Annotation::fromJson` reads leniently -- it is also
// the labels' reader -- so the command's parser is where hostile input stops.
std::optional<QString> invalidShape(const Annotation& a);

std::optional<QString> invalidCreated(const Annotation& a)
{
    if (a.id.kind != AnnotationId::Kind::Created || !a.id.isValid() || a.nm != a.id.nm)
        return QStringLiteral("annotation.id");
    if (a.subtype == AnnotationSubtype::Unknown) return QStringLiteral("annotation.subtype");
    return invalidShape(a);
}

// What any annotation a command carries must be (T5 split it out of `invalidCreated`, for the edits): a
// bounded `/NM`, a page, a finite rectangle, finite and bounded geometry, colours and an opacity in range.
std::optional<QString> invalidShape(const Annotation& a)
{
    if (a.nm.size() > Limits::kMaxNameLength) return QStringLiteral("annotation.nm");
    if (!a.id.isValid()) return QStringLiteral("annotation.id");
    if (a.id.kind == AnnotationId::Kind::Direct && a.id.page > Limits::kMaxPageIndex) return QStringLiteral("annotation.id");
    if (a.id.kind == AnnotationId::Kind::Created && a.id.nm.size() > Limits::kMaxNameLength)
        return QStringLiteral("annotation.id");
    if (a.page < 0 || a.page > Limits::kMaxPageIndex) return QStringLiteral("annotation.page");
    if (!a.rectFinite || !std::isfinite(a.rect.left()) || !std::isfinite(a.rect.bottom())
        || !std::isfinite(a.rect.right()) || !std::isfinite(a.rect.top()))
        return QStringLiteral("annotation.rect");

    std::size_t coordinates = a.quadPoints.size() + a.vertices.size() + a.line.size() + a.callout.size()
                              + a.rectDifferences.size();
    for (const std::vector<double>& stroke : a.inkList) coordinates += stroke.size();
    if (coordinates > Limits::kMaxCoordinates) return QStringLiteral("annotation.geometry");
    bool finite = allFinite(a.quadPoints) && allFinite(a.vertices) && allFinite(a.line)
                  && allFinite(a.callout) && allFinite(a.rectDifferences) && allFinite(a.color)
                  && allFinite(a.interiorColor);
    for (const std::vector<double>& stroke : a.inkList) finite = finite && allFinite(stroke);
    if (!finite) return QStringLiteral("annotation.geometry");
    if (a.color.size() > 4 || a.interiorColor.size() > 4) return QStringLiteral("annotation.color");
    if (a.opacity && (!std::isfinite(*a.opacity) || *a.opacity < 0.0 || *a.opacity > 1.0))
        return QStringLiteral("annotation.opacity");
    if (a.borderWidth && (!std::isfinite(*a.borderWidth) || *a.borderWidth < 0.0))
        return QStringLiteral("annotation.borderWidth");
    return std::nullopt;
}

// The rectangle of a page an annotation's change can repaint, or the WHOLE page when that is not known
// (S6 T4, the named work of A2 at Stage 6). An appearance is drawn inside its `/Rect` (ISO 32000-1
// 12.5.5 maps its box into it), so the rectangle -- padded a point for anti-aliasing -- is all a
// change repaints, EXCEPT where a reader draws the annotation somewhere else: a popup (its own
// rectangle, and the parent's popup when open), an annotation with one, and one drawn at a fixed size
// or orientation (`NoZoom`, `NoRotate`). Those, and a rectangle that is not a finite non-empty box, are
// the whole page: claiming too much costs a repaint, claiming too little is a stale tile.
std::optional<PdfRect> repaintedRect(const Annotation& a)
{
    if (a.subtype == AnnotationSubtype::Popup || a.popup || a.hasFlag(AnnotationFlag::NoZoom)
        || a.hasFlag(AnnotationFlag::NoRotate))
        return std::nullopt;
    const PdfRect& r = a.rect;
    if (!std::isfinite(r.left()) || !std::isfinite(r.bottom()) || !std::isfinite(r.size.width)
        || !std::isfinite(r.size.height) || r.size.width <= 0.0 || r.size.height <= 0.0)
        return std::nullopt;
    constexpr double kPad = 1.0;
    return PdfRect{PdfPoint{r.left() - kPad, r.bottom() - kPad}, PdfSize{r.size.width + 2 * kPad, r.size.height + 2 * kPad}};
}

// ANNOTATIONS of these pages (S6 T4, D6-10): their tiles go, their extracted text stays -- and where every
// changed annotation's rectangle is known, only the tiles those rectangles reach go.
Scope annotationsScope(const std::vector<std::pair<int, const Annotation*>>& changed)
{
    if (changed.empty()) return Scope::nothing();
    std::vector<std::pair<int, PdfRect>> regions;
    std::vector<int> wholePages;
    for (const auto& [page, annotation] : changed) {
        if (const std::optional<PdfRect> rect = repaintedRect(*annotation))
            regions.emplace_back(page, *rect);
        else
            wholePages.push_back(page);
    }
    const Scope wholes = Scope::annotationsOf(std::move(wholePages));
    const Scope parts = Scope::annotationRegions(std::move(regions));
    return wholes.merged(parts);
}

// ---- T5: the shape every in-place command shares -------------------------------------------------

// The annotations `ids` name, as the store holds them now, in the order named -- or nothing for an id
// the store does not hold, or one named twice.
std::optional<std::vector<Annotation>> currentOf(const AnnotationStore& store, const std::vector<AnnotationId>& ids)
{
    if (!store.status() || ids.empty() || ids.size() > Limits::kMaxAnnotations) return std::nullopt;
    std::set<AnnotationId> seen;
    std::vector<Annotation> out;
    out.reserve(ids.size());
    for (const AnnotationId& id : ids) {
        const std::optional<std::size_t> index = store.find(id);
        if (!index || !seen.insert(id).second) return std::nullopt;
        out.push_back(store.at(*index));
    }
    return out;
}

// SINK FIRST, MODEL SECOND, as the two listing commands: the sink is the one that can refuse (an
// appearance it cannot draw yet), and the store's replace cannot fail for a pair the store produced.
bool applyChange(Document& document, MutationSink& sink, AnnotationChange change, std::optional<AnnotationChange>& kept,
                 std::optional<Error>& refusal)
{
    refusal.reset();
    if (change.before.empty() || change.before.size() != change.after.size()) return false;
    if (change.before == change.after) return false;   // changes nothing: not a step anyone can undo
    if (Status sunk = sink.replaceAnnotations(change.before, change.after); !sunk) {
        refusal = sunk.error();
        return false;
    }
    if (!document.annotations().replace(change.after)) {
        (void)sink.replaceAnnotations(change.after, change.before);
        return false;
    }
    kept = std::move(change);
    return true;
}

// The model must be the one apply() left -- each annotation exactly as it made it -- or the revert is
// refused: undo is last-in-first-out, so anything else is a history that went wrong.
bool revertChange(Document& document, MutationSink& sink, const std::optional<AnnotationChange>& kept)
{
    if (!kept) return false;
    const AnnotationStore& store = document.annotations();
    for (const Annotation& a : kept->after) {
        const std::optional<std::size_t> index = store.find(a.id);
        if (!index || !(store.at(*index) == a)) return false;
    }
    if (!sink.replaceAnnotations(kept->after, kept->before)) return false;
    if (!document.annotations().replace(kept->before)) {
        (void)sink.replaceAnnotations(kept->before, kept->after);
        return false;
    }
    return true;
}

// Both rectangles of every changed annotation repaint: where it was, and where it is.
Scope changeScope(const std::optional<AnnotationChange>& kept)
{
    if (!kept) return Scope::wholeDocument();
    std::vector<std::pair<int, const Annotation*>> changed;
    for (const Annotation& a : kept->before) changed.emplace_back(a.page, &a);
    for (const Annotation& a : kept->after) changed.emplace_back(a.page, &a);
    return annotationsScope(changed);
}

Error refusalFor(const char* key, const Annotation& a)
{
    return Error{ErrorCode::Unavailable, QLatin1String(key), QString::number(a.page + 1)};
}

// The pre-flight every in-place command shares: editing at all, each page that lists one, and D6-27.
Status permittedChange(const Document& document, const std::vector<AnnotationId>& ids,
                       const std::function<std::pair<bool, bool>(const Annotation&)>& touches)
{
    if (Status allowed = document.permissions().may(Operation::Annotate); !allowed) return allowed;
    const AnnotationStore& store = document.annotations();
    for (const AnnotationId& id : ids) {
        const std::optional<std::size_t> index = store.find(id);
        if (!index) continue;   // apply() refuses an id the store does not hold
        for (const AnnotationPlacement& at : store.placementsOf(id)) {
            if (Status page = document.permissions().mayAnnotate(at.page); !page) return page;
        }
        const Annotation& a = store.at(*index);
        const auto [properties, contents] = touches(a);
        if (Status flags = annotationChangeAllowed(a, properties, contents); !flags) return flags;
    }
    return Status{};
}

std::vector<AnnotationId> idsOf(const std::vector<Annotation>& annotations)
{
    std::vector<AnnotationId> ids;
    ids.reserve(annotations.size());
    for (const Annotation& a : annotations) ids.push_back(a.id);
    return ids;
}

// Whether `after` changes anything but `before`'s text (`/M` aside), and whether it changes the text.
std::pair<bool, bool> whatChanges(const Annotation& before, const Annotation& after)
{
    const bool contents = before.contents != after.contents || before.richText != after.richText;
    Annotation others = after;
    others.contents = before.contents;
    others.richText = before.richText;
    others.modified = before.modified;
    return {!(others == before), contents};
}

QJsonArray idArray(const std::vector<AnnotationId>& ids)
{
    QJsonArray array;
    for (const AnnotationId& id : ids) array.append(id.toString());
    return array;
}

// The "ids" member of a serialized command: a non-empty, bounded array of identities, each bounded
// before it is parsed -- a hostile journal can carry a 10 MB string.
Result<std::vector<AnnotationId>> parseIds(const QJsonObject& json, QLatin1String typeId)
{
    const QJsonValue list = json.value(QStringLiteral("ids"));
    if (!list.isArray()) return badCommand(typeId, QStringLiteral("ids"));
    const QJsonArray array = list.toArray();
    if (array.isEmpty() || static_cast<std::size_t>(array.size()) > Limits::kMaxAnnotations)
        return badCommand(typeId, QStringLiteral("ids"));
    std::vector<AnnotationId> ids;
    ids.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue item : array) {
        if (!item.isString() || item.toString().size() > Limits::kMaxNameLength + 8)
            return badCommand(typeId, QStringLiteral("id"));
        const std::optional<AnnotationId> id = AnnotationId::fromString(item.toString());
        if (!id || !id->isValid() || (id->kind == AnnotationId::Kind::Direct && id->page > Limits::kMaxPageIndex))
            return badCommand(typeId, QStringLiteral("id"));
        ids.push_back(*id);
    }
    return ids;
}

bool typeIs(const QJsonObject& json, QLatin1String typeId)
{
    const QJsonValue type = json.value(QStringLiteral("type"));
    return type.isString() && type.toString() == QLatin1String(typeId);
}

// The "at" instant: whole seconds from 1970 to the end of year 9999.
std::optional<std::int64_t> parseInstant(const QJsonObject& json)
{
    const QJsonValue at = json.value(QStringLiteral("at"));
    if (!at.isDouble()) return std::nullopt;
    const double value = at.toDouble();
    constexpr double kLast = 253402300799.0;   // 9999-12-31T23:59:59Z
    if (!std::isfinite(value) || value < 0.0 || value > kLast || std::floor(value) != value) return std::nullopt;
    return static_cast<std::int64_t>(value);
}

bool colourInRange(const std::vector<double>& colour)
{
    if (colour.size() != 0 && colour.size() != 1 && colour.size() != 3 && colour.size() != 4) return false;
    return std::all_of(colour.begin(), colour.end(), [](double v) { return std::isfinite(v) && v >= 0.0 && v <= 1.0; });
}

std::optional<std::vector<double>> parseColour(const QJsonValue& value)
{
    if (!value.isArray()) return std::nullopt;
    const QJsonArray array = value.toArray();
    if (array.size() > 4) return std::nullopt;
    std::vector<double> colour;
    for (const QJsonValue item : array) {
        if (!item.isDouble()) return std::nullopt;
        colour.push_back(item.toDouble());
    }
    if (!colourInRange(colour)) return std::nullopt;
    return colour;
}

QJsonArray numberArray(const std::vector<double>& values)
{
    QJsonArray array;
    for (const double v : values) array.append(v);
    return array;
}

CatalogText countedName(const char* one, const char* many, std::size_t count)
{
    if (count == 1) return CatalogText{QLatin1String(one), {}};
    return CatalogText{QLatin1String(many), {QString::number(count)}};
}

} // namespace

// ==========================================================================================
// AddAnnotationCommand
// ==========================================================================================

AddAnnotationCommand::AddAnnotationCommand(std::vector<Annotation> annotations)
    : annotations_(std::move(annotations))
{
}

CatalogText AddAnnotationCommand::name() const
{
    if (annotations_.size() == 1) return CatalogText{QLatin1String("command.annot.add"), {}};
    return CatalogText{QLatin1String("command.annot.addMany"), {QString::number(annotations_.size())}};
}

Scope AddAnnotationCommand::scope() const
{
    std::vector<std::pair<int, const Annotation*>> changed;
    for (const Annotation& a : annotations_) changed.emplace_back(a.page, &a);
    return annotationsScope(changed);
}

Status AddAnnotationCommand::permitted(const Document& document) const
{
    if (Status allowed = document.permissions().may(Operation::Annotate); !allowed) return allowed;
    for (const Annotation& a : annotations_) {
        if (Status page = document.permissions().mayAnnotate(a.page); !page) return page;
    }
    return Status{};
}

bool AddAnnotationCommand::apply(Document& document, MutationSink& sink)
{
    const AnnotationStore& store = document.annotations();
    if (!store.status() || annotations_.empty() || annotations_.size() > Limits::kMaxAnnotations)
        return false;

    // Everything checked against the model before the sink hears of it.
    std::vector<AnnotationPlacement> placements;
    placements.reserve(annotations_.size());
    std::set<QString> names;
    std::map<int, int> addedOnPage;
    for (const Annotation& a : annotations_) {
        if (invalidCreated(a) || a.page >= store.pageCount() || store.find(a.id)
            || !names.insert(a.id.nm).second)
            return false;
        const int position = static_cast<int>(store.listing(a.page).size()) + addedOnPage[a.page]++;
        placements.push_back(AnnotationPlacement{a.page, position});
    }

    if (!sink.insertAnnotations(placements, annotations_)) return false;   // the model is untouched

    std::vector<std::pair<AnnotationPlacement, Annotation>> entries;
    entries.reserve(annotations_.size());
    for (std::size_t i = 0; i < annotations_.size(); ++i) entries.emplace_back(placements[i], annotations_[i]);
    if (!document.annotations().insert(entries)) {
        // Cannot happen for a set validated above. If it ever could, the sink is put back, so a false
        // here still means nothing changed.
        std::vector<AnnotationPlacement> back(placements.rbegin(), placements.rend());
        (void)sink.removeAnnotations(back);
        return false;
    }
    placed_ = std::move(placements);
    return true;
}

bool AddAnnotationCommand::revert(Document& document, MutationSink& sink)
{
    if (placed_.size() != annotations_.size()) return false;
    const AnnotationStore& store = document.annotations();
    // The listing must be the one apply() left: each placement names this command's annotation.
    for (std::size_t i = 0; i < placed_.size(); ++i) {
        const std::vector<AnnotationListingEntry>& listing = store.listing(placed_[i].page);
        const std::size_t position = static_cast<std::size_t>(placed_[i].position);
        if (position >= listing.size() || !listing[position].annotation
            || store.at(*listing[position].annotation).id != annotations_[i].id)
            return false;
    }
    const std::vector<AnnotationPlacement> back(placed_.rbegin(), placed_.rend());
    if (!sink.removeAnnotations(back)) return false;
    if (!document.annotations().remove(back)) {
        (void)sink.insertAnnotations(placed_, annotations_);
        return false;
    }
    return true;
}

QJsonObject AddAnnotationCommand::serialize() const
{
    QJsonArray annotations;
    for (const Annotation& a : annotations_) annotations.append(a.toJson());
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("annotations"), annotations}};
}

Result<std::unique_ptr<Command>> AddAnnotationCommand::deserialize(const QJsonObject& json)
{
    const QJsonValue type = json.value(QStringLiteral("type"));
    if (!type.isString() || type.toString() != QLatin1String(kTypeId))
        return badCommand(kTypeId, QStringLiteral("type"));
    const QJsonValue list = json.value(QStringLiteral("annotations"));
    if (!list.isArray()) return badCommand(kTypeId, QStringLiteral("annotations"));
    const QJsonArray array = list.toArray();
    if (array.isEmpty() || static_cast<std::size_t>(array.size()) > Limits::kMaxAnnotations)
        return badCommand(kTypeId, QStringLiteral("annotations"));

    std::vector<Annotation> annotations;
    annotations.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue item : array) {
        if (!item.isObject()) return badCommand(kTypeId, QStringLiteral("annotation"));
        auto annotation = Annotation::fromJson(item.toObject());
        if (!annotation) return badCommand(kTypeId, QStringLiteral("annotation"));
        if (const std::optional<QString> wrong = invalidCreated(annotation.value()))
            return badCommand(kTypeId, *wrong);
        annotations.push_back(std::move(annotation).value());
    }
    return std::unique_ptr<Command>(new AddAnnotationCommand(std::move(annotations)));
}

// ==========================================================================================
// DeleteAnnotationCommand
// ==========================================================================================

DeleteAnnotationCommand::DeleteAnnotationCommand(std::vector<AnnotationId> ids) : ids_(std::move(ids)) {}

CatalogText DeleteAnnotationCommand::name() const
{
    if (ids_.size() == 1) return CatalogText{QLatin1String("command.annot.delete"), {}};
    return CatalogText{QLatin1String("command.annot.deleteMany"), {QString::number(ids_.size())}};
}

Scope DeleteAnnotationCommand::scope() const
{
    if (!applied_) return Scope::wholeDocument();
    std::vector<std::pair<int, const Annotation*>> changed;
    for (const auto& [at, annotation] : removed_) changed.emplace_back(at.page, &annotation);
    return annotationsScope(changed);
}

Status DeleteAnnotationCommand::permitted(const Document& document) const
{
    if (Status allowed = document.permissions().may(Operation::Annotate); !allowed) return allowed;
    for (const AnnotationId& id : ids_) {
        for (const AnnotationPlacement& at : document.annotations().placementsOf(id)) {
            if (Status page = document.permissions().mayAnnotate(at.page); !page) return page;
        }
        // D6-27 (T5): a Locked or ReadOnly annotation is not deleted.
        if (const std::optional<std::size_t> index = document.annotations().find(id)) {
            if (Status flags = annotationChangeAllowed(document.annotations().at(*index), true, false); !flags)
                return flags;
        }
    }
    return Status{};
}

bool DeleteAnnotationCommand::apply(Document& document, MutationSink& sink)
{
    const AnnotationStore& store = document.annotations();
    if (!store.status() || ids_.empty() || ids_.size() > Limits::kMaxAnnotations) return false;

    std::set<AnnotationId> seen;
    std::vector<std::pair<AnnotationPlacement, Annotation>> removed;
    for (const AnnotationId& id : ids_) {
        const std::optional<std::size_t> index = store.find(id);
        if (!index || !seen.insert(id).second) return false;
        for (const AnnotationPlacement& at : store.placementsOf(id)) removed.emplace_back(at, store.at(*index));
    }
    if (removed.empty()) return false;
    std::sort(removed.begin(), removed.end(), [](const auto& a, const auto& b) {
        return a.first.page != b.first.page ? a.first.page < b.first.page : a.first.position < b.first.position;
    });

    // Removed from the LAST entry back, so no removal moves an entry a later one names.
    std::vector<AnnotationPlacement> order;
    order.reserve(removed.size());
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) order.push_back(it->first);

    if (!sink.removeAnnotations(order)) return false;   // the model is untouched
    if (!document.annotations().remove(order)) {
        std::vector<AnnotationPlacement> at;
        std::vector<Annotation> annotations;
        for (const auto& [placement, annotation] : removed) {
            at.push_back(placement);
            annotations.push_back(annotation);
        }
        (void)sink.insertAnnotations(at, annotations);
        return false;
    }
    removed_ = std::move(removed);
    applied_ = true;
    return true;
}

bool DeleteAnnotationCommand::revert(Document& document, MutationSink& sink)
{
    if (!applied_ || removed_.empty()) return false;
    // Re-listed from the FIRST entry forward, each at the position it held: the inverse of apply's order.
    std::vector<AnnotationPlacement> at;
    std::vector<Annotation> annotations;
    at.reserve(removed_.size());
    annotations.reserve(removed_.size());
    for (const auto& [placement, annotation] : removed_) {
        at.push_back(placement);
        annotations.push_back(annotation);
    }
    if (!sink.insertAnnotations(at, annotations)) return false;
    if (!document.annotations().insert(removed_)) {
        std::vector<AnnotationPlacement> back(at.rbegin(), at.rend());
        (void)sink.removeAnnotations(back);
        return false;
    }
    return true;
}

QJsonObject DeleteAnnotationCommand::serialize() const
{
    QJsonArray ids;
    for (const AnnotationId& id : ids_) ids.append(id.toString());
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("ids"), ids}};
}

Result<std::unique_ptr<Command>> DeleteAnnotationCommand::deserialize(const QJsonObject& json)
{
    const QJsonValue type = json.value(QStringLiteral("type"));
    if (!type.isString() || type.toString() != QLatin1String(kTypeId))
        return badCommand(kTypeId, QStringLiteral("type"));
    const QJsonValue list = json.value(QStringLiteral("ids"));
    if (!list.isArray()) return badCommand(kTypeId, QStringLiteral("ids"));
    const QJsonArray array = list.toArray();
    if (array.isEmpty() || static_cast<std::size_t>(array.size()) > Limits::kMaxAnnotations)
        return badCommand(kTypeId, QStringLiteral("ids"));

    std::vector<AnnotationId> ids;
    ids.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue item : array) {
        // Bounded before it is parsed: a hostile journal can carry a 10 MB string.
        if (!item.isString() || item.toString().size() > Limits::kMaxNameLength + 8)
            return badCommand(kTypeId, QStringLiteral("id"));
        const std::optional<AnnotationId> id = AnnotationId::fromString(item.toString());
        if (!id || !id->isValid() || (id->kind == AnnotationId::Kind::Direct && id->page > Limits::kMaxPageIndex))
            return badCommand(kTypeId, QStringLiteral("id"));
        ids.push_back(*id);
    }
    return std::unique_ptr<Command>(new DeleteAnnotationCommand(std::move(ids)));
}

// ==========================================================================================
// D6-27 -- the flags, as every in-place command and the delete ask them (T5)
// ==========================================================================================

Status annotationChangeAllowed(const Annotation& annotation, bool properties, bool contents)
{
    if (!properties && !contents) return Status{};
    if (annotation.hasFlag(AnnotationFlag::ReadOnly)) return refusalFor("error.edit.annotationReadOnly", annotation);
    if (properties && annotation.hasFlag(AnnotationFlag::Locked))
        return refusalFor("error.edit.annotationLocked", annotation);
    if (contents && annotation.hasFlag(AnnotationFlag::LockedContents))
        return refusalFor("error.edit.annotationContentsLocked", annotation);
    return Status{};
}

// ==========================================================================================
// EditAnnotationCommand
// ==========================================================================================

EditAnnotationCommand::EditAnnotationCommand(std::vector<Annotation> after) : after_(std::move(after)) {}

CatalogText EditAnnotationCommand::name() const
{
    return countedName("command.annot.edit", "command.annot.editMany", after_.size());
}

Scope EditAnnotationCommand::scope() const
{
    return changeScope(change_);
}

Status EditAnnotationCommand::permitted(const Document& document) const
{
    std::map<AnnotationId, const Annotation*> wanted;
    for (const Annotation& a : after_) wanted.emplace(a.id, &a);
    return permittedChange(document, idsOf(after_), [&wanted](const Annotation& now) {
        const auto found = wanted.find(now.id);
        return found == wanted.end() ? std::pair<bool, bool>{false, false} : whatChanges(now, *found->second);
    });
}

bool EditAnnotationCommand::apply(Document& document, MutationSink& sink)
{
    const std::optional<std::vector<Annotation>> before = currentOf(document.annotations(), idsOf(after_));
    if (!before) return false;
    for (std::size_t i = 0; i < after_.size(); ++i) {
        const Annotation& was = (*before)[i];
        const Annotation& now = after_[i];
        // An edit changes fields, never which annotation it is or where it is listed.
        if (invalidShape(now) || now.subtype != was.subtype || now.page != was.page || now.nm != was.nm) return false;
    }
    return applyChange(document, sink, AnnotationChange{*before, after_}, change_, refusal_);
}

bool EditAnnotationCommand::revert(Document& document, MutationSink& sink)
{
    return revertChange(document, sink, change_);
}

QJsonObject EditAnnotationCommand::serialize() const
{
    QJsonArray annotations;
    for (const Annotation& a : after_) annotations.append(a.toJson());
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("annotations"), annotations}};
}

Result<std::unique_ptr<Command>> EditAnnotationCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badCommand(kTypeId, QStringLiteral("type"));
    const QJsonValue list = json.value(QStringLiteral("annotations"));
    if (!list.isArray()) return badCommand(kTypeId, QStringLiteral("annotations"));
    const QJsonArray array = list.toArray();
    if (array.isEmpty() || static_cast<std::size_t>(array.size()) > Limits::kMaxAnnotations)
        return badCommand(kTypeId, QStringLiteral("annotations"));

    std::vector<Annotation> annotations;
    annotations.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue item : array) {
        if (!item.isObject()) return badCommand(kTypeId, QStringLiteral("annotation"));
        auto annotation = Annotation::fromJson(item.toObject());
        if (!annotation) return badCommand(kTypeId, QStringLiteral("annotation"));
        if (const std::optional<QString> wrong = invalidShape(annotation.value())) return badCommand(kTypeId, *wrong);
        annotations.push_back(std::move(annotation).value());
    }
    return std::unique_ptr<Command>(new EditAnnotationCommand(std::move(annotations)));
}

// ==========================================================================================
// MoveAnnotationCommand
// ==========================================================================================

MoveAnnotationCommand::MoveAnnotationCommand(std::vector<AnnotationId> ids, double dx, double dy, std::int64_t at)
    : ids_(std::move(ids)), dx_(dx), dy_(dy), at_(at)
{
}

CatalogText MoveAnnotationCommand::name() const
{
    return countedName("command.annot.move", "command.annot.moveMany", ids_.size());
}

Scope MoveAnnotationCommand::scope() const
{
    return changeScope(change_);
}

Status MoveAnnotationCommand::permitted(const Document& document) const
{
    return permittedChange(document, ids_, [](const Annotation&) { return std::pair<bool, bool>{true, false}; });
}

bool MoveAnnotationCommand::apply(Document& document, MutationSink& sink)
{
    if (isNoMove(dx_, dy_) || std::abs(dx_) > Limits::kMaxOffset || std::abs(dy_) > Limits::kMaxOffset) return false;
    const std::optional<std::vector<Annotation>> before = currentOf(document.annotations(), ids_);
    if (!before) return false;
    AnnotationChange change{*before, {}};
    change.after.reserve(before->size());
    for (const Annotation& a : *before) {
        Annotation moved = translated(a, dx_, dy_);
        moved.modified = PdfDate::fromUtcSeconds(at_);
        if (invalidShape(moved)) return false;
        change.after.push_back(std::move(moved));
    }
    return applyChange(document, sink, std::move(change), change_, refusal_);
}

bool MoveAnnotationCommand::revert(Document& document, MutationSink& sink)
{
    return revertChange(document, sink, change_);
}

QJsonObject MoveAnnotationCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("ids"), idArray(ids_)},
                       {QStringLiteral("dx"), dx_},
                       {QStringLiteral("dy"), dy_},
                       {QStringLiteral("at"), static_cast<double>(at_)}};
}

Result<std::unique_ptr<Command>> MoveAnnotationCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badCommand(kTypeId, QStringLiteral("type"));
    auto ids = parseIds(json, kTypeId);
    if (!ids) return ids.error();
    const QJsonValue dx = json.value(QStringLiteral("dx"));
    const QJsonValue dy = json.value(QStringLiteral("dy"));
    if (!dx.isDouble() || !dy.isDouble() || !std::isfinite(dx.toDouble()) || !std::isfinite(dy.toDouble())
        || std::abs(dx.toDouble()) > Limits::kMaxOffset || std::abs(dy.toDouble()) > Limits::kMaxOffset
        || isNoMove(dx.toDouble(), dy.toDouble()))
        return badCommand(kTypeId, QStringLiteral("offset"));
    const std::optional<std::int64_t> at = parseInstant(json);
    if (!at) return badCommand(kTypeId, QStringLiteral("at"));
    return std::unique_ptr<Command>(new MoveAnnotationCommand(std::move(ids).value(), dx.toDouble(), dy.toDouble(), *at));
}

// ==========================================================================================
// SetAnnotationAppearanceCommand
// ==========================================================================================

Annotation AnnotationAppearance::appliedTo(const Annotation& a) const
{
    Annotation out = a;
    if (color) out.color = *color;
    if (interiorColor) out.interiorColor = *interiorColor;
    if (opacity) out.opacity = *opacity;
    if (borderWidth) out.borderWidth = *borderWidth;
    return out;
}

QJsonObject AnnotationAppearance::toJson() const
{
    QJsonObject json;
    if (color) json.insert(QStringLiteral("color"), numberArray(*color));
    if (interiorColor) json.insert(QStringLiteral("interiorColor"), numberArray(*interiorColor));
    if (opacity) json.insert(QStringLiteral("opacity"), *opacity);
    if (borderWidth) json.insert(QStringLiteral("borderWidth"), *borderWidth);
    return json;
}

std::optional<AnnotationAppearance> AnnotationAppearance::fromJson(const QJsonObject& json)
{
    // A border wider than any page could show is not a border anyone asked for.
    constexpr double kMaxBorder = 1000.0;
    AnnotationAppearance out;
    for (auto it = json.begin(); it != json.end(); ++it) {
        const QString key = it.key();
        if (key == QLatin1String("color")) {
            out.color = parseColour(it.value());
            if (!out.color) return std::nullopt;
        } else if (key == QLatin1String("interiorColor")) {
            out.interiorColor = parseColour(it.value());
            if (!out.interiorColor) return std::nullopt;
        } else if (key == QLatin1String("opacity")) {
            const double v = it.value().toDouble(-1.0);
            if (!it.value().isDouble() || !std::isfinite(v) || v < 0.0 || v > 1.0) return std::nullopt;
            out.opacity = v;
        } else if (key == QLatin1String("borderWidth")) {
            const double v = it.value().toDouble(-1.0);
            if (!it.value().isDouble() || !std::isfinite(v) || v < 0.0 || v > kMaxBorder) return std::nullopt;
            out.borderWidth = v;
        } else {
            return std::nullopt;   // an unknown property is a journal this build cannot replay faithfully
        }
    }
    return out;
}

SetAnnotationAppearanceCommand::SetAnnotationAppearanceCommand(std::vector<AnnotationId> ids,
                                                               AnnotationAppearance appearance, std::int64_t at)
    : ids_(std::move(ids)), appearance_(std::move(appearance)), at_(at)
{
}

CatalogText SetAnnotationAppearanceCommand::name() const
{
    return countedName("command.annot.setAppearance", "command.annot.setAppearanceMany", ids_.size());
}

Scope SetAnnotationAppearanceCommand::scope() const
{
    return changeScope(change_);
}

Status SetAnnotationAppearanceCommand::permitted(const Document& document) const
{
    return permittedChange(document, ids_, [](const Annotation&) { return std::pair<bool, bool>{true, false}; });
}

bool SetAnnotationAppearanceCommand::apply(Document& document, MutationSink& sink)
{
    if (appearance_.isEmpty()) return false;
    if ((appearance_.color && !colourInRange(*appearance_.color))
        || (appearance_.interiorColor && !colourInRange(*appearance_.interiorColor)))
        return false;
    const std::optional<std::vector<Annotation>> before = currentOf(document.annotations(), ids_);
    if (!before) return false;
    AnnotationChange change{*before, {}};
    for (const Annotation& a : *before) {
        Annotation after = appearance_.appliedTo(a);
        if (!(after == a)) after.modified = PdfDate::fromUtcSeconds(at_);
        if (invalidShape(after)) return false;
        change.after.push_back(std::move(after));
    }
    return applyChange(document, sink, std::move(change), change_, refusal_);
}

bool SetAnnotationAppearanceCommand::revert(Document& document, MutationSink& sink)
{
    return revertChange(document, sink, change_);
}

QJsonObject SetAnnotationAppearanceCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("ids"), idArray(ids_)},
                       {QStringLiteral("appearance"), appearance_.toJson()},
                       {QStringLiteral("at"), static_cast<double>(at_)}};
}

Result<std::unique_ptr<Command>> SetAnnotationAppearanceCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badCommand(kTypeId, QStringLiteral("type"));
    auto ids = parseIds(json, kTypeId);
    if (!ids) return ids.error();
    const QJsonValue value = json.value(QStringLiteral("appearance"));
    if (!value.isObject()) return badCommand(kTypeId, QStringLiteral("appearance"));
    std::optional<AnnotationAppearance> appearance = AnnotationAppearance::fromJson(value.toObject());
    if (!appearance || appearance->isEmpty()) return badCommand(kTypeId, QStringLiteral("appearance"));
    const std::optional<std::int64_t> at = parseInstant(json);
    if (!at) return badCommand(kTypeId, QStringLiteral("at"));
    return std::unique_ptr<Command>(
        new SetAnnotationAppearanceCommand(std::move(ids).value(), std::move(*appearance), *at));
}

// ==========================================================================================
// LockAnnotationCommand
// ==========================================================================================

LockAnnotationCommand::LockAnnotationCommand(std::vector<AnnotationId> ids, std::uint32_t set, std::uint32_t clear,
                                             std::int64_t at)
    : ids_(std::move(ids)), set_(set), clear_(clear), at_(at)
{
}

CatalogText LockAnnotationCommand::name() const
{
    return countedName("command.annot.lock", "command.annot.lockMany", ids_.size());
}

Scope LockAnnotationCommand::scope() const
{
    return changeScope(change_);
}

Status LockAnnotationCommand::permitted(const Document& document) const
{
    // Never refused for a flag: this is how one comes off. Editing at all, and the pages, still apply.
    return permittedChange(document, ids_, [](const Annotation&) { return std::pair<bool, bool>{false, false}; });
}

bool LockAnnotationCommand::apply(Document& document, MutationSink& sink)
{
    if ((set_ | clear_) == 0 || ((set_ | clear_) & ~kLockFlags) != 0 || (set_ & clear_) != 0) return false;
    const std::optional<std::vector<Annotation>> before = currentOf(document.annotations(), ids_);
    if (!before) return false;
    AnnotationChange change{*before, {}};
    for (const Annotation& a : *before) {
        Annotation after = a;
        after.flags = (a.flags | set_) & ~clear_;
        if (after.flags != a.flags) after.modified = PdfDate::fromUtcSeconds(at_);
        change.after.push_back(std::move(after));
    }
    return applyChange(document, sink, std::move(change), change_, refusal_);
}

bool LockAnnotationCommand::revert(Document& document, MutationSink& sink)
{
    return revertChange(document, sink, change_);
}

QJsonObject LockAnnotationCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("ids"), idArray(ids_)},
                       {QStringLiteral("set"), static_cast<double>(set_)},
                       {QStringLiteral("clear"), static_cast<double>(clear_)},
                       {QStringLiteral("at"), static_cast<double>(at_)}};
}

Result<std::unique_ptr<Command>> LockAnnotationCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badCommand(kTypeId, QStringLiteral("type"));
    auto ids = parseIds(json, kTypeId);
    if (!ids) return ids.error();
    const auto mask = [&json](const char* key) -> std::optional<std::uint32_t> {
        const QJsonValue value = json.value(QLatin1String(key));
        if (!value.isDouble()) return std::nullopt;
        const double v = value.toDouble();
        if (!std::isfinite(v) || v < 0.0 || v > static_cast<double>(kLockFlags) || std::floor(v) != v) return std::nullopt;
        const auto bits = static_cast<std::uint32_t>(v);
        if ((bits & ~kLockFlags) != 0) return std::nullopt;
        return bits;
    };
    const std::optional<std::uint32_t> set = mask("set");
    const std::optional<std::uint32_t> clear = mask("clear");
    if (!set || !clear || (*set | *clear) == 0 || (*set & *clear) != 0) return badCommand(kTypeId, QStringLiteral("flags"));
    const std::optional<std::int64_t> at = parseInstant(json);
    if (!at) return badCommand(kTypeId, QStringLiteral("at"));
    return std::unique_ptr<Command>(new LockAnnotationCommand(std::move(ids).value(), *set, *clear, *at));
}

} // namespace rawpage::core
