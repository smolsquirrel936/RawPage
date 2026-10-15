// core/doc/Fdf -- implementation. Stage 6 T13. See the header for the names, the merge and why one model serves both
// syntaxes. `AnnotationStore::importFdf` and `exportFdf` are defined here, beside the model they speak.

#include "core/doc/Fdf.h"

#include "core/doc/AnnotationStore.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace rawpage::core {
namespace {

Error exchangeError(const char* key, const QString& context)
{
    return Error{ErrorCode::Unavailable, QLatin1String(key), context};
}

// The instant a merge compares: `/M`, else `/CreationDate`; nothing for an annotation that is dated by neither, which
// is older than any dated one.
std::optional<std::int64_t> instantOf(const Annotation& a)
{
    if (a.modified.utcSeconds) return a.modified.utcSeconds;
    return a.created.utcSeconds;
}

bool newer(const Annotation& incoming, const Annotation& held)
{
    const std::optional<std::int64_t> in = instantOf(incoming);
    const std::optional<std::int64_t> was = instantOf(held);
    if (!in) return false;
    if (!was) return true;
    return *in > *was;
}

// D6-27, as `ImportAnnotationsCommand::permitted` will ask it of a replacement (`core/cmd/commands/AnnotationCommands`'
// `annotationChangeAllowed` -- the same keys): asked HERE first, so a locked comment is refused alone and the import
// goes on, rather than refusing the whole command at its pre-flight.
std::optional<Error> lockRefusal(const Annotation& held, const Annotation& after)
{
    const bool contents = held.contents != after.contents || held.richText != after.richText;
    Annotation others = after;
    others.contents = held.contents;
    others.richText = held.richText;
    others.modified = held.modified;
    const bool properties = !(others == held);
    if (!properties && !contents) return std::nullopt;
    const QString page = QString::number(held.page + 1);
    if (held.hasFlag(AnnotationFlag::ReadOnly))
        return Error{ErrorCode::Unavailable, QLatin1String("error.edit.annotationReadOnly"), page};
    if (properties && held.hasFlag(AnnotationFlag::Locked))
        return Error{ErrorCode::Unavailable, QLatin1String("error.edit.annotationLocked"), page};
    if (contents && held.hasFlag(AnnotationFlag::LockedContents))
        return Error{ErrorCode::Unavailable, QLatin1String("error.edit.annotationContentsLocked"), page};
    return std::nullopt;
}

bool finiteRect(const Annotation& a)
{
    const PdfRect& r = a.rect;
    return a.rectFinite && std::isfinite(r.left()) && std::isfinite(r.bottom()) && std::isfinite(r.size.width)
           && std::isfinite(r.size.height);
}

} // namespace

std::optional<ExchangeFormat> exchangeFormatOfPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("fdf")) return ExchangeFormat::Fdf;
    if (suffix == QLatin1String("xfdf")) return ExchangeFormat::Xfdf;
    return std::nullopt;
}

const char* exchangeNoteName(ExchangeNoteKind kind) noexcept
{
    switch (kind) {
    case ExchangeNoteKind::UnknownElement: return "UnknownElement";
    case ExchangeNoteKind::GeometryTruncated: return "GeometryTruncated";
    case ExchangeNoteKind::DuplicateName: return "DuplicateName";
    case ExchangeNoteKind::NameTooLong: return "NameTooLong";
    case ExchangeNoteKind::TextTooLong: return "TextTooLong";
    case ExchangeNoteKind::ReferenceDropped: return "ReferenceDropped";
    case ExchangeNoteKind::PopupWithoutParent: return "PopupWithoutParent";
    case ExchangeNoteKind::NotExported: return "NotExported";
    }
    return "?";
}

QString popupNameFor(const QString& parentName)
{
    return parentName + QStringLiteral("-popup");
}

QString contentNameOf(const Annotation& annotation)
{
    QJsonObject json = annotation.toJson();
    for (const char* key : {"id", "nm", "inReplyTo", "popup", "parent", "hasAppearance", "image", "attachment"})
        json.remove(QLatin1String(key));
    const QByteArray digest =
        QCryptographicHash::hash(QJsonDocument(json).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
    return QStringLiteral("rawpage-content-") + QString::fromLatin1(digest.toHex().left(32));
}

QString exchangeNameOf(const Annotation& annotation)
{
    return annotation.nm.isEmpty() ? contentNameOf(annotation) : annotation.nm;
}

bool isCommentSubtype(AnnotationSubtype subtype)
{
    switch (subtype) {
    case AnnotationSubtype::Text:
    case AnnotationSubtype::FreeText:
    case AnnotationSubtype::Line:
    case AnnotationSubtype::Square:
    case AnnotationSubtype::Circle:
    case AnnotationSubtype::Polygon:
    case AnnotationSubtype::PolyLine:
    case AnnotationSubtype::Highlight:
    case AnnotationSubtype::Underline:
    case AnnotationSubtype::Squiggly:
    case AnnotationSubtype::StrikeOut:
    case AnnotationSubtype::Stamp:
    case AnnotationSubtype::Caret:
    case AnnotationSubtype::Ink:
    case AnnotationSubtype::FileAttachment:
    case AnnotationSubtype::Sound:
    case AnnotationSubtype::Redact:
        return true;
    default:
        return false;
    }
}

int MergeResult::count(MergeEntry::Outcome outcome) const
{
    int n = 0;
    for (const MergeEntry& e : entries) {
        if (e.outcome == outcome) ++n;
    }
    return n;
}

// ==========================================================================================
// The merge
// ==========================================================================================

FdfImport AnnotationStore::importFdf(const Fdf& exchange, const ImportCheck& check) const
{
    FdfImport out;
    out.result.notes = exchange.notes;
    const auto note = [&out](ExchangeNoteKind kind, const QString& name, int page) {
        out.result.notes.push_back(ExchangeNote{kind, name, page});
    };

    // THE DOCUMENT'S NAMES: each annotation's `/NM`, else the name its content gives it (the header) -- the first
    // annotation with a name keeps it, as the store's own lookups keep the first. A pop-up is never matched.
    std::map<QString, std::size_t> byName;
    for (std::size_t i = 0; i < annotations_.size(); ++i) {
        const Annotation& a = annotations_[i];
        if (a.subtype == AnnotationSubtype::Popup) continue;
        byName.emplace(exchangeNameOf(a), i);
    }

    // THE EXCHANGE'S POP-UPS, by their parent's name -- each follows its parent -- and every comment's name, which a
    // pop-up's (`popupNameFor`) must not also be.
    std::map<QString, const Annotation*> popupOf;
    std::set<QString> commentNames;
    for (const Annotation& a : exchange.annotations) {
        if (a.subtype != AnnotationSubtype::Popup) {
            commentNames.insert(a.id.nm);
            continue;
        }
        if (a.parent && a.parent->kind == AnnotationId::Kind::Created) popupOf.emplace(a.parent->nm, &a);
    }

    // ---- First, what each one IS: added, replaced, kept or refused -- before any reference is resolved, because a
    // reference may only name something that will exist.
    struct Decision {
        const Annotation* incoming = nullptr;
        std::optional<std::size_t> held;   // the document's annotation it is matched with
        Annotation built;                  // what is written: references left out until the second pass
    };
    std::vector<Decision> decisions;
    std::vector<std::size_t> entryOf;      // decision -> its entry in the result
    std::set<QString> seen;
    for (const Annotation& a : exchange.annotations) {
        if (a.subtype == AnnotationSubtype::Popup) continue;
        MergeEntry entry;
        entry.name = a.id.nm;
        entry.subtype = a.subtype;
        entry.page = a.page;
        const auto refuse = [&entry](Error why) {
            entry.outcome = MergeEntry::Outcome::Refused;
            entry.why = std::move(why);
        };

        if (!status()) {
            refuse(status().error());
        } else if (entry.name.isEmpty() || entry.name.size() > kMaxExchangeNameLength) {
            refuse(exchangeError("error.exchange.badName", entry.name.left(64)));
        } else if (!seen.insert(entry.name).second) {
            refuse(exchangeError("error.exchange.duplicateName", entry.name));
        } else if (a.page < 0 || a.page >= pageCount()) {
            refuse(exchangeError("error.exchange.pageMissing", QString::number(a.page + 1)));
        } else if (!finiteRect(a) || a.subtype == AnnotationSubtype::Unknown) {
            refuse(exchangeError("error.exchange.geometry", entry.name));
        } else if (const auto found = byName.find(entry.name); found != byName.end()) {
            const Annotation& held = annotations_[found->second];
            if (held.subtype != a.subtype || held.page != a.page) {
                refuse(exchangeError("error.exchange.conflict", entry.name));
            } else if (!newer(a, held)) {
                entry.outcome = MergeEntry::Outcome::Kept;
            } else {
                // A REPLACEMENT: every modelled field the exchange's, in the place the document gives it. What the
                // model holds of the document's own and the exchange cannot say stays the document's: its pop-up,
                // its file's description of itself, a stamp's pixels, whether it has an appearance.
                Annotation after = a;
                after.id = held.id;
                after.nm = held.nm;
                after.page = held.page;
                after.inReplyTo.reset();
                after.popup = held.popup;
                after.parent = held.parent;
                after.hasAppearance = held.hasAppearance;
                after.attachment = held.attachment;
                after.image = held.image;
                if (const std::optional<Error> locked = lockRefusal(held, after)) {
                    refuse(*locked);
                } else if (const Status writable = check ? check(after) : Status{}; !writable) {
                    refuse(writable.error());
                } else {
                    entry.outcome = MergeEntry::Outcome::Replaced;
                    decisions.push_back(Decision{&a, found->second, std::move(after)});
                }
            }
        } else {
            Annotation built = a;
            built.id = AnnotationId::ofCreated(entry.name);
            built.nm = entry.name;
            built.inReplyTo.reset();
            built.popup.reset();
            built.parent.reset();
            // RawPage writes the appearance of everything it adds (D6-6) -- but a state reply, which no reader draws.
            built.hasAppearance = !built.isStateReply();
            if (const Status writable = check ? check(built) : Status{}; !writable) {
                refuse(writable.error());
            } else {
                entry.outcome = MergeEntry::Outcome::Added;
                decisions.push_back(Decision{&a, std::nullopt, std::move(built)});
            }
        }
        if (entry.outcome == MergeEntry::Outcome::Added || entry.outcome == MergeEntry::Outcome::Replaced)
            entryOf.push_back(out.result.entries.size());
        out.result.entries.push_back(std::move(entry));
    }

    // ---- Then the references: to what this import adds, or to what the document holds and keeps.
    std::set<QString> added;
    for (const Decision& d : decisions) {
        if (!d.held) added.insert(d.built.nm);
    }
    const auto resolve = [&](const QString& name) -> std::optional<AnnotationId> {
        if (added.count(name) != 0) return AnnotationId::ofCreated(name);
        if (const auto found = byName.find(name); found != byName.end()) {
            const AnnotationId& id = annotations_[found->second].id;
            if (id.kind != AnnotationId::Kind::Direct) return id;   // nothing can point at a direct dictionary
        }
        return std::nullopt;
    };

    for (std::size_t k = 0; k < decisions.size(); ++k) {
        Decision& d = decisions[k];
        const Annotation& incoming = *d.incoming;
        if (incoming.inReplyTo) {
            if (incoming.inReplyTo->kind == AnnotationId::Kind::Created) d.built.inReplyTo = resolve(incoming.inReplyTo->nm);
            if (!d.built.inReplyTo) note(ExchangeNoteKind::ReferenceDropped, d.built.nm, incoming.page);
        }

        // Its pop-up: an added comment takes the exchange's; a replaced one keeps the document's, or takes the
        // exchange's when it had none.
        std::optional<Annotation> popup;
        // ... and never under a name the exchange or the document already gives something else.
        const QString popupName = popupNameFor(d.built.nm);
        const bool needsPopup = (!d.held || !d.built.popup) && commentNames.count(popupName) == 0
                                && byName.count(popupName) == 0 && !find(AnnotationId::ofCreated(popupName));
        if (const auto found = popupOf.find(incoming.id.nm); needsPopup && found != popupOf.end()) {
            Annotation p = *found->second;
            p.id = AnnotationId::ofCreated(popupNameFor(d.built.nm));
            p.nm = p.id.nm;
            p.page = d.built.page;
            p.parent = d.built.id;
            p.popup.reset();
            p.inReplyTo.reset();
            p.hasAppearance = false;
            if (!check || check(p)) {
                d.built.popup = p.id;
                popup = std::move(p);
            }
        }

        if (d.held) {
            if (d.built == annotations_[*d.held]) {
                // Newer by its date and the same in every field: nothing to replace.
                out.result.entries[entryOf[k]].outcome = MergeEntry::Outcome::Kept;
                continue;
            }
            out.replaced.push_back(d.built);
            if (popup) out.added.push_back(std::move(*popup));
        } else {
            out.added.push_back(d.built);
            if (popup) out.added.push_back(std::move(*popup));
        }
    }
    return out;
}

// ==========================================================================================
// The export
// ==========================================================================================

Fdf AnnotationStore::exportFdf(const std::vector<std::size_t>& items) const
{
    Fdf out;
    if (!status()) return out;

    std::set<std::size_t> chosen;
    for (const std::size_t item : items) {
        if (item >= annotations_.size()) continue;
        for (const std::size_t member : threadOf(item)) chosen.insert(member);
    }

    // Each annotation's name once, so a reference and its target agree.
    std::map<std::size_t, QString> names;
    const auto nameOf = [&](std::size_t index) -> const QString& {
        auto found = names.find(index);
        if (found == names.end()) found = names.emplace(index, exchangeNameOf(annotations_[index])).first;
        return found->second;
    };

    std::set<std::size_t> left;   // chosen, and left out: a pop-up whose parent is not exported goes with it
    for (const std::size_t index : chosen) {
        const Annotation& a = annotations_[index];
        if (a.subtype == AnnotationSubtype::Popup) continue;
        if (!isCommentSubtype(a.subtype)) {
            left.insert(index);
        } else if (a.subtype == AnnotationSubtype::Sound) {
            // A sound's samples are its stream's, and the model does not hold them (T14): an exchange that named the
            // sound and carried no sound would be a comment that plays nothing.
            out.notes.push_back(ExchangeNote{ExchangeNoteKind::NotExported, nameOf(index), a.page});
            left.insert(index);
        }
    }

    for (const std::size_t index : chosen) {
        if (left.count(index) != 0) continue;
        const Annotation& a = annotations_[index];
        Annotation x = a;
        if (a.subtype == AnnotationSubtype::Popup) {
            const std::optional<std::size_t> parent = a.parent ? find(*a.parent) : std::nullopt;
            if (!parent || chosen.count(*parent) == 0 || left.count(*parent) != 0) continue;
            x.id = AnnotationId::ofCreated(popupNameFor(nameOf(*parent)));
            x.nm = x.id.nm;
            x.parent = AnnotationId::ofCreated(nameOf(*parent));
        } else {
            x.id = AnnotationId::ofCreated(nameOf(index));
            x.nm = x.id.nm;
            x.parent.reset();
            x.inReplyTo.reset();
            x.popup.reset();
            if (a.inReplyTo) {
                if (const std::optional<std::size_t> target = find(*a.inReplyTo))
                    x.inReplyTo = AnnotationId::ofCreated(nameOf(*target));
            }
            if (a.popup) {
                if (const std::optional<std::size_t> popup = find(*a.popup); popup && chosen.count(*popup) != 0)
                    x.popup = AnnotationId::ofCreated(popupNameFor(nameOf(index)));
            }
        }
        x.hasAppearance = false;   // an exchange carries no appearance: the reader that imports it draws its own
        x.image = StampImage{};    // nor an image stamp's pixels (XFDF and FDF name a stamp; its picture is its /AP)
        out.annotations.push_back(std::move(x));
        out.sources.push_back(a.id);
    }
    return out;
}

} // namespace rawpage::core
