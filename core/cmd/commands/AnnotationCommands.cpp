// core/cmd/commands/AnnotationCommands -- implementation. Stage 6 T3. See the header for the set, the
// placements and why the sink is asked first.

#include "core/cmd/commands/AnnotationCommands.h"

#include "core/backend.h"
#include "core/doc/Document.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
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
std::optional<QString> invalidCreated(const Annotation& a)
{
    if (a.id.kind != AnnotationId::Kind::Created || !a.id.isValid() || a.nm != a.id.nm)
        return QStringLiteral("annotation.id");
    if (a.nm.size() > Limits::kMaxNameLength) return QStringLiteral("annotation.nm");
    if (a.subtype == AnnotationSubtype::Unknown) return QStringLiteral("annotation.subtype");
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

} // namespace rawpage::core
