// pdf/ops/MutationSinkImpl -- implementation. Stage 3 T3. See the header for desired state.

#include "pdf/ops/MutationSinkImpl.h"

#include "core/doc/AnnotationGeometry.h"
#include "core/doc/Stamps.h"
#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationOps.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/EmbeddedFileReader.h"
#include "pdf/cos/filters/FlateFilter.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace rawpage::pdf::ops {
namespace {

core::Error sinkError(core::ErrorCode code, const char* key, long long detail = -1)
{
    return core::Error{code, QLatin1String(key),
                       detail < 0 ? QString() : QString::number(static_cast<qlonglong>(detail))};
}

// -90 -> 270, 450 -> 90; nullopt for anything that is not a whole number of quarter turns.
std::optional<int> normalizeDegrees(int degrees)
{
    if (degrees % 90 != 0)
        return std::nullopt;
    return ((degrees % 360) + 360) % 360;
}

// The table a sink compares against. Through `openWithRepair()`, the reader's one entry point, so a
// damaged file's sink sees the same objects the rest of the product does; whether a REPAIRED file's
// update is written as a complete section is the save path's decision (D3-6, T8), not this class's.
core::Result<cos::XrefTable> tableFor(std::string_view bytes)
{
    auto opened = cos::openWithRepair(bytes);
    if (!opened)
        return opened.error();
    return std::move(opened.value().table);
}

long long generationOf(const cos::XrefTable& table, long long number)
{
    const cos::XrefEntry* entry = table.find(number);
    return entry != nullptr && entry->type == cos::XrefEntryType::InFile ? entry->field3 : 0;
}

core::Error placementError(const core::AnnotationPlacement& at)
{
    return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pdf.annotationPlacement"),
                       QStringLiteral("%1:%2").arg(at.page).arg(at.position)};
}

core::Error listingMismatch(int page)
{
    return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.save.verifyFailed"),
                       QStringLiteral("annotations:%1").arg(page + 1)};
}

const std::vector<MutationSinkImpl::ListedEntry> kNoEntries;

// The page dictionary as it is now, ready to be changed: a loose stream keeps its data descriptor (its
// payload is copied when the delta is written), anything else is a plain dictionary -- the rule
// `PageOps::rotatedPage` follows for Stage 3 guide issue I-12.
core::Result<cos::CosObject> currentPage(cos::XrefTable& table, long long number)
{
    auto fetched = table.fetch(number);
    if (!fetched || !fetched.value().object.isDictionary())
        return sinkError(core::ErrorCode::NotFound, "error.pdf.objectMissing", number);
    const cos::CosObject& current = fetched.value().object;
    std::vector<cos::CosObject::Entry> entries(current.entries().begin(), current.entries().end());
    const cos::XrefEntry* placed = table.find(number);
    if (current.isStream() && placed != nullptr && placed->type == cos::XrefEntryType::InFile) {
        const std::size_t size = table.source().size();
        const cos::CosStreamData& data = current.streamData();
        if (data.dataOffset <= size && data.dataLength <= size - data.dataOffset)
            return cos::CosObject::makeStream(std::move(entries), data);
    }
    return cos::CosObject::makeDictionary(std::move(entries));
}

void countArrayUsers(const std::vector<MutationSinkImpl::PageListing>& listings, std::map<long long, int>& users)
{
    users.clear();
    for (const MutationSinkImpl::PageListing& listing : listings) {
        if (listing.arrayObject) ++users[*listing.arrayObject];
    }
}

bool sameListing(const std::vector<MutationSinkImpl::ListedEntry>& a, const std::vector<MutationSinkImpl::ListedEntry>& b)
{
    return a.size() == b.size()
           && std::equal(a.begin(), a.end(), b.begin(),
                         [](const MutationSinkImpl::ListedEntry& x, const MutationSinkImpl::ListedEntry& y) {
                             return x.sameEntry(y);
                         });
}

// T5 (D6-5): whether `now` is drawn exactly as `disk` is, but for where -- the same appearance carried into
// another rectangle. The fields a reader never draws are taken from `disk` first: who wrote it and when,
// its thread and state, its flags (a lock is not ink; `Hidden` hides the appearance it has), its pop-up --
// and its text, except for free text and a dynamic stamp, whose appearances draw their text.
bool drawnTheSame(const core::Annotation& disk, const core::Annotation& now)
{
    core::Annotation probe = now;
    probe.nm = disk.nm;
    probe.author = disk.author;
    probe.subject = disk.subject;
    probe.created = disk.created;
    probe.modified = disk.modified;
    probe.flags = disk.flags;
    probe.inReplyTo = disk.inReplyTo;
    probe.replyType = disk.replyType;
    probe.stateModel = disk.stateModel;
    probe.state = disk.state;
    probe.popup = disk.popup;
    probe.parent = disk.parent;
    probe.open = disk.open;
    probe.hasAppearance = disk.hasAppearance;
    // Stage 6 T10: and a DYNAMIC stamp's, whose appearance draws its `/Contents` -- the lines it baked -- as free text
    // draws its text (`core/doc/Stamps`).
    if (disk.subtype != core::AnnotationSubtype::FreeText && core::stampKind(disk) != core::StampKind::Dynamic) {
        probe.contents = disk.contents;
        probe.richText = disk.richText;
    }
    return core::isMappedCopy(disk, probe);
}

bool sameSize(const core::PdfRect& a, const core::PdfRect& b)
{
    return std::abs(a.size.width - b.size.width) <= 1e-9 && std::abs(a.size.height - b.size.height) <= 1e-9;
}

// Stage 6 T7: the objects appearances share -- a font, and since T10 an image -- written into THIS update, once per key however many
// appearances name them. One per compose: a font written by an earlier save is not reused, so an update is
// read whole without its predecessor's objects being assumed (a later save could be of a file that lost it).
class DeltaAppearanceObjects final : public AppearanceObjects {
public:
    explicit DeltaAppearanceObjects(cos::ObjectDelta& delta) : delta_(delta) {}
    cos::CosRef shared(const std::string& key, cos::CosObject object) override
    {
        if (const auto found = written_.find(key); found != written_.end()) return found->second;
        const cos::CosRef at{delta_.add(std::move(object)), 0};
        written_.emplace(key, at);
        return at;
    }
    // Stage 6 T10: an image stamp's picture and its mask -- one object per image however many stamps paint it.
    cos::CosRef sharedStream(const std::string& key, cos::CosObject dictionary, std::string data) override
    {
        if (const auto found = written_.find(key); found != written_.end()) return found->second;
        const cos::CosRef at{delta_.add(std::move(dictionary), std::move(data)), 0};
        written_.emplace(key, at);
        return at;
    }

private:
    cos::ObjectDelta& delta_;
    std::map<std::string, cos::CosRef> written_;
};

// Stage 6 T8: whether a dictionary on disk DRAWS something the model does not hold, so that our builder -- which
// draws from the model -- would lose it: a Line's leader lines, its caption or its leader offset (ISO 32000-1
// Table 175's `/LL`, `/LLE`, `/LLO`, `/Cap`), and a measurement's `/Measure` (T12's). Such an annotation is never
// redrawn: an edit that would need it redrawn is refused by key, and a move carries its producer's appearance.
bool drawsUnmodelled(const std::optional<cos::CosObject>& dictionary, core::AnnotationSubtype subtype)
{
    if (!dictionary || !dictionary->isDictionary()) return false;
    if (dictionary->has("Measure")) return true;
    if (subtype != core::AnnotationSubtype::Line) return false;
    for (const char* key : {"LL", "LLE", "LLO"}) {
        const cos::CosObject* value = dictionary->find(key);
        if (value != nullptr && (!value->isNumber() || value->numberValue() != 0.0)) return true;
    }
    const cos::CosObject* cap = dictionary->find("Cap");
    return cap != nullptr && cap->isBoolean() && cap->booleanValue();
}

// A `/Rect` read back as the four numbers written, within what a written real keeps.
bool rectReadsBack(const cos::CosObject& dictionary, const core::PdfRect& rect)
{
    const cos::CosObject* written = dictionary.find("Rect");
    if (written == nullptr || !written->isArray() || written->items().size() != 4) return false;
    const double want[4] = {rect.left(), rect.bottom(), rect.right(), rect.top()};
    for (std::size_t i = 0; i < 4; ++i) {
        const cos::CosObject& n = written->items()[i];
        if (!n.isNumber() || std::abs(n.numberValue() - want[i]) > 0.01) return false;
    }
    return true;
}

// Stage 6 T11: whether `specification` -- read from `written` -- is a file specification whose `/EF /F` stream holds
// exactly `flate`, the bytes RawPage compressed when the file was attached, under `/FlateDecode`. Byte for byte
// rather than decoded, so verifying a 50 MB attachment after every command is a comparison, not an inflate.
bool specificationHolds(cos::XrefTable& written, const cos::CosObject* specification, const std::string& flate)
{
    if (specification == nullptr || !specification->isReference()) return false;
    auto spec = written.fetch(specification->reference().number);
    if (!spec || !spec.value().object.isDictionary()) return false;
    const cos::CosObject& ef = spec.value().object.get("EF");
    const cos::CosObject& file = ef.get("F");
    if (!file.isReference()) return false;
    auto stream = written.fetch(file.reference().number);
    if (!stream || !stream.value().object.isStream()) return false;
    const cos::CosObject* filter = stream.value().object.find("Filter");
    if (filter == nullptr || !filter->isName() || filter->text() != "FlateDecode") return false;
    return stream.value().object.rawStreamData(written.source()) == std::string_view(flate);
}

// The bytes a key is written as, as a PDF string: a literal for printable ASCII, else UTF-16BE as hex --
// `core::nameTreeKeyBytes`, which is also what the model sorts by.
cos::CosObject keyString(const QString& key)
{
    const QByteArray bytes = core::nameTreeKeyBytes(key);
    const bool unicode = bytes.startsWith("\xFE\xFF");
    return cos::CosObject::makeString(bytes.toStdString(), unicode ? cos::StringForm::Hex : cos::StringForm::Literal);
}

// A file name as a byte string for `/F`: printable ASCII kept, every other character `_` -- `/UF` carries the
// name in Unicode (ISO 32000-1 §7.11.3), and `/F` is what a reader with no `/UF` shows.
cos::CosObject asciiFileName(const QString& name)
{
    std::string out;
    for (const QChar c : name) out += (c.unicode() >= 0x20 && c.unicode() < 0x7F) ? static_cast<char>(c.unicode()) : '_';
    return cos::CosObject::makeString(std::move(out));
}

} // namespace

MutationSinkImpl::MutationSinkImpl(std::string_view bytes, cos::XrefTable table, PageWalk walk)
    : bytes_(bytes), table_(std::move(table)), walk_(std::move(walk))
{
    disk_ = readListings(table_, walk_);
    countArrayUsers(disk_, arrayUsers_);
    diskFiles_ = readFiles(table_);
}

core::Result<std::unique_ptr<MutationSinkImpl>> MutationSinkImpl::open(std::string_view bytes)
{
    auto table = tableFor(bytes);
    if (!table)
        return table.error();
    auto walk = PageOps::walk(table.value());
    if (!walk)
        return walk.error();
    return std::unique_ptr<MutationSinkImpl>(
        new MutationSinkImpl(bytes, std::move(table).value(), std::move(walk).value()));
}

core::Status MutationSinkImpl::setPageRotation(int pageIndex, int degrees)
{
    // Validated in full before anything changes: A-33's "refused without trace".
    if (pageIndex < 0 || pageIndex >= pageCount())
        return sinkError(core::ErrorCode::InvalidArgument, "error.pdf.pageOutOfRange", pageIndex);
    const std::optional<int> normalized = normalizeDegrees(degrees);
    if (!normalized)
        return sinkError(core::ErrorCode::InvalidArgument, "error.pdf.rotationNotRightAngle",
                         degrees);

    const PageEntry& page = walk_.pages[static_cast<std::size_t>(pageIndex)];
    if (*normalized == page.effectiveRotate())
        desired_.erase(pageIndex);          // back to the disk: nothing to write for this page
    else
        desired_[pageIndex] = *normalized;
    return core::Status{};
}

int MutationSinkImpl::desiredRotation(int pageIndex) const
{
    if (const auto found = desired_.find(pageIndex); found != desired_.end())
        return found->second;
    if (pageIndex < 0 || pageIndex >= pageCount())
        return 0;
    return walk_.pages[static_cast<std::size_t>(pageIndex)].effectiveRotate();
}

core::Result<cos::ObjectDelta> MutationSinkImpl::delta()
{
    cos::ObjectDelta delta(table_);
    // A page dictionary is written ONCE, carrying every change to it: its rotation and, from Stage 6
    // T3, its /Annots -- `ObjectDelta` refuses one object replaced twice.
    std::map<long long, cos::CosObject> pageDictionaries;
    for (const auto& [pageIndex, degrees] : desired_) {
        const PageEntry& page = walk_.pages[static_cast<std::size_t>(pageIndex)];
        auto dictionary = PageOps::rotatedPage(table_, page, degrees);
        if (!dictionary)
            return dictionary.error();
        if (dictionary.value())
            pageDictionaries.emplace(page.number, std::move(*dictionary.value()));
    }
    if (core::Status annotations = composeAnnotations(delta, pageDictionaries); !annotations)
        return annotations.error();
    if (core::Status files = composeFiles(delta); !files)   // Stage 6 T11
        return files.error();

    for (auto& [number, dictionary] : pageDictionaries) {
        if (dictionary.isStream()) {
            // A page dictionary is a stream here only when its data lies in this buffer (issue I-12).
            const cos::CosStreamData& data = dictionary.streamData();
            std::string payload(bytes_.substr(data.dataOffset, data.dataLength));
            delta.replace(number, std::move(dictionary), std::move(payload));
        } else {
            delta.replace(number, std::move(dictionary));
        }
    }
    return delta;
}

// ==========================================================================================
// Annotations (Stage 6 T3)
// ==========================================================================================

bool MutationSinkImpl::ListedEntry::sameEntry(const ListedEntry& other) const
{
    if (kind != other.kind) return false;
    switch (kind) {
    case Kind::Reference: return reference == other.reference;
    case Kind::Value: return value == other.value;
    case Kind::Created: return nm == other.nm;
    }
    return false;
}

std::vector<MutationSinkImpl::PageListing> MutationSinkImpl::readListings(cos::XrefTable& table, const PageWalk& walk)
{
    std::vector<PageListing> out(walk.pages.size());
    for (std::size_t index = 0; index < walk.pages.size(); ++index) {
        auto page = table.fetch(walk.pages[index].number);
        if (!page || !page.value().object.isDictionary()) continue;
        const cos::CosObject* annots = page.value().object.find("Annots");
        if (annots == nullptr) continue;
        PageListing& listing = out[index];
        listing.present = true;

        std::optional<cos::IndirectObject> holder;
        const cos::CosObject* array = annots;
        if (annots->isReference()) {
            auto fetched = table.fetch(annots->reference().number);
            if (!fetched || !fetched.value().object.isArray()) continue;   // the reader notes it: nothing listed
            holder = std::move(fetched).value();
            array = &holder->object;
            listing.arrayObject = annots->reference().number;
        }
        if (!array->isArray()) continue;
        const std::vector<cos::CosObject>& items = array->items();
        listing.entries.reserve(items.size());
        for (std::size_t position = 0; position < items.size(); ++position) {
            ListedEntry entry;
            if (items[position].isReference()) {
                // Named as the reader names it -- by object number -- without fetching it: whether it is
                // a dictionary is the reader's question, and the sink never looks behind a reference.
                entry.kind = ListedEntry::Kind::Reference;
                entry.reference = items[position].reference();
                entry.id = core::AnnotationId::ofObject(entry.reference.number);
            } else {
                entry.kind = ListedEntry::Kind::Value;
                entry.value = items[position];
                entry.value.clearSpansRecursively();
                if (entry.value.isDictionary())
                    entry.id = core::AnnotationId::ofDirect(static_cast<int>(index), static_cast<int>(position));
            }
            listing.entries.push_back(std::move(entry));
        }
    }
    return out;
}

const std::vector<MutationSinkImpl::ListedEntry>& MutationSinkImpl::desiredListing(int page) const
{
    if (const auto found = desiredListings_.find(page); found != desiredListings_.end()) return found->second;
    if (page < 0 || page >= static_cast<int>(disk_.size())) return kNoEntries;
    return disk_[static_cast<std::size_t>(page)].entries;
}

const core::Annotation* MutationSinkImpl::createdAnnotation(const QString& nm) const
{
    const auto found = created_.find(nm);
    return found == created_.end() ? nullptr : &found->second;
}

std::vector<int> MutationSinkImpl::pagesWithAnnotationChanges() const
{
    std::vector<int> pages;
    for (const auto& [page, listing] : desiredListings_) pages.push_back(page);
    return pages;
}

std::vector<MutationSinkImpl::ListedEntry>& MutationSinkImpl::listingToChange(
    int page, std::map<int, std::vector<ListedEntry>>& scratch) const
{
    auto found = scratch.find(page);
    if (found == scratch.end()) found = scratch.emplace(page, desiredListing(page)).first;
    return found->second;
}

void MutationSinkImpl::commitListings(std::map<int, std::vector<ListedEntry>> changed)
{
    for (auto& [page, listing] : changed) {
        if (sameListing(listing, disk_[static_cast<std::size_t>(page)].entries))
            desiredListings_.erase(page);   // back to the disk: nothing to write for this page
        else
            desiredListings_[page] = std::move(listing);
    }
    // A created annotation no desired listing names any more is not written (add, then undo).
    std::set<QString> listed;
    for (const auto& [page, listing] : desiredListings_) {
        for (const ListedEntry& entry : listing) {
            if (entry.kind == ListedEntry::Kind::Created) listed.insert(entry.nm);
        }
    }
    for (auto it = created_.begin(); it != created_.end();) {
        if (listed.count(it->first) == 0) it = created_.erase(it);
        else ++it;
    }
}

core::Status MutationSinkImpl::insertAnnotations(const std::vector<core::AnnotationPlacement>& at,
                                                 const std::vector<core::Annotation>& annotations)
{
    if (at.empty() || at.size() != annotations.size())
        return sinkError(core::ErrorCode::InvalidArgument, "error.pdf.annotationPlacement",
                         static_cast<long long>(annotations.size()));

    // Everything on scratch copies of the touched listings: refused part-way, nothing has changed.
    std::map<int, std::vector<ListedEntry>> scratch;
    std::map<QString, core::Annotation> created;
    for (std::size_t i = 0; i < at.size(); ++i) {
        const core::AnnotationPlacement& place = at[i];
        const core::Annotation& annotation = annotations[i];
        if (place.page < 0 || place.page >= pageCount()) return placementError(place);
        std::vector<ListedEntry>& listing = listingToChange(place.page, scratch);
        if (place.position < 0 || static_cast<std::size_t>(place.position) > listing.size())
            return placementError(place);

        ListedEntry entry;
        entry.id = annotation.id;
        if (const auto removed = removedEntries_.find(annotation.id); removed != removedEntries_.end()) {
            entry = removed->second;                        // listed again exactly as it was written
            entry.id = annotation.id;
        } else if (annotation.id.kind == core::AnnotationId::Kind::Object) {
            const cos::XrefEntry* placed = table_.find(annotation.id.object);
            if (placed == nullptr || placed->type == cos::XrefEntryType::Free)
                return sinkError(core::ErrorCode::NotFound, "error.pdf.annotationUnknown", annotation.id.object);
            entry.kind = ListedEntry::Kind::Reference;      // the object on disk, unmodelled keys and all
            entry.reference = cos::CosRef{annotation.id.object, generationOf(table_, annotation.id.object)};
        } else if (annotation.id.kind == core::AnnotationId::Kind::Created) {
            if (const auto saved = createdOnDisk_.find(annotation.id.nm); saved != createdOnDisk_.end()) {
                entry.kind = ListedEntry::Kind::Reference;  // created, and since saved: list that object
                entry.reference = saved->second;
            } else {
                // Written new -- so it must be a kind this build can draw (D6-6), asked now, not at save. A
                // pop-up is drawn by no appearance at all (T6).
                if (!AppearanceStreamBuilder::drawsNothing(annotation)) {
                    auto appearance = AppearanceStreamBuilder::normal(annotation);
                    if (!appearance) return appearance.error();
                }
                // T11: a FileAttachment RawPage creates carries the file it attaches -- there is nothing to write
                // as its /FS otherwise.
                if (annotation.subtype == core::AnnotationSubtype::FileAttachment
                    && (!annotation.attachment || !annotation.attachment->carried))
                    return sinkError(core::ErrorCode::InvalidArgument, "error.attachment.noData", place.page);
                entry.kind = ListedEntry::Kind::Created;
                entry.nm = annotation.id.nm;
                created.insert_or_assign(entry.nm, annotation);
            }
        } else {
            // A direct entry the sink never removed: there is nothing on disk to list again.
            return sinkError(core::ErrorCode::NotFound, "error.pdf.annotationUnknown", annotation.id.position);
        }
        listing.insert(listing.begin() + place.position, std::move(entry));
    }

    for (auto& [nm, annotation] : created) created_.insert_or_assign(nm, std::move(annotation));
    commitListings(std::move(scratch));
    return core::Status{};
}

core::Status MutationSinkImpl::removeAnnotations(const std::vector<core::AnnotationPlacement>& at)
{
    if (at.empty()) return sinkError(core::ErrorCode::InvalidArgument, "error.pdf.annotationPlacement", 0);
    std::map<int, std::vector<ListedEntry>> scratch;
    std::vector<std::pair<core::AnnotationId, ListedEntry>> removed;
    for (const core::AnnotationPlacement& place : at) {
        if (place.page < 0 || place.page >= pageCount()) return placementError(place);
        std::vector<ListedEntry>& listing = listingToChange(place.page, scratch);
        if (place.position < 0 || static_cast<std::size_t>(place.position) >= listing.size())
            return placementError(place);
        const ListedEntry& entry = listing[static_cast<std::size_t>(place.position)];
        if (!entry.id) return placementError(place);   // not an annotation: not the model's to drop
        // Kept when nothing else could list it again: a direct entry, or one a save promoted (T5) -- an
        // object number names itself, and a created annotation is the sink's own.
        if (entry.kind != ListedEntry::Kind::Created && entry.id->kind == core::AnnotationId::Kind::Direct)
            removed.emplace_back(*entry.id, entry);
        listing.erase(listing.begin() + place.position);
    }
    for (auto& [id, entry] : removed) removedEntries_.insert_or_assign(id, std::move(entry));
    commitListings(std::move(scratch));
    return core::Status{};
}

const MutationSinkImpl::Edit* MutationSinkImpl::editOf(const core::AnnotationId& id) const
{
    const auto found = edits_.find(id);
    return found == edits_.end() ? nullptr : &found->second;
}

std::optional<cos::CosObject> MutationSinkImpl::dictionaryOnDisk(const core::AnnotationId& id)
{
    for (int page = 0; page < pageCount(); ++page) {
        for (const ListedEntry& entry : disk_[static_cast<std::size_t>(page)].entries) {
            if (!entry.id || !(*entry.id == id)) continue;
            if (entry.kind == ListedEntry::Kind::Value) return entry.value;
            if (entry.kind != ListedEntry::Kind::Reference) return std::nullopt;
            auto fetched = table_.fetch(entry.reference.number);
            if (!fetched || !fetched.value().object.isDictionary()) return std::nullopt;
            cos::CosObject object = fetched.value().object;
            object.clearSpansRecursively();
            return object;
        }
    }
    return std::nullopt;
}

core::Status MutationSinkImpl::replaceAnnotations(const std::vector<core::Annotation>& before,
                                                  const std::vector<core::Annotation>& after)
{
    if (before.empty() || before.size() != after.size())
        return sinkError(core::ErrorCode::InvalidArgument, "error.pdf.annotationUnknown",
                         static_cast<long long>(after.size()));

    // Everything on scratch copies: refused part-way, nothing has changed.
    std::map<core::AnnotationId, Edit> edits = edits_;
    std::map<QString, core::Annotation> created;
    for (std::size_t i = 0; i < after.size(); ++i) {
        const core::Annotation& was = before[i];
        const core::Annotation& now = after[i];
        const auto where = static_cast<long long>(i);
        if (!(was.id == now.id) || was.subtype != now.subtype || !now.id.isValid())
            return sinkError(core::ErrorCode::InvalidArgument, "error.pdf.annotationUnknown", where);

        // A created annotation not yet saved is drawn anew from the model at compose: the change is the model's.
        if (now.id.kind == core::AnnotationId::Kind::Created && created_.count(now.id.nm) != 0) {
            if (!AppearanceStreamBuilder::drawsNothing(now)) {
                auto appearance = AppearanceStreamBuilder::normal(now);
                if (!appearance) return appearance.error();
            }
            created.insert_or_assign(now.id.nm, now);
            continue;
        }

        // Anything else must be something the file holds that this sink can name.
        bool known = false;
        switch (now.id.kind) {
        case core::AnnotationId::Kind::Object: {
            const cos::XrefEntry* placed = table_.find(now.id.object);
            known = placed != nullptr && placed->type != cos::XrefEntryType::Free;
            break;
        }
        case core::AnnotationId::Kind::Created:
            known = createdOnDisk_.count(now.id.nm) != 0;
            break;
        case core::AnnotationId::Kind::Direct:
            for (int page = 0; page < pageCount() && !known; ++page) {
                for (const ListedEntry& entry : desiredListing(page)) {
                    if (entry.id && *entry.id == now.id) known = true;
                }
            }
            known = known || removedEntries_.count(now.id) != 0;
            break;
        }
        if (!known) return sinkError(core::ErrorCode::NotFound, "error.pdf.annotationUnknown", where);

        const auto existing = edits.find(now.id);
        const core::Annotation disk = existing != edits.end() ? existing->second.disk : was;
        if (now == disk) {
            edits.erase(now.id);   // back to the disk: nothing to write for it
            continue;
        }
        // The dictionary on disk, remembered the first time it is edited (see the header).
        if (existing == edits.end()) {
            std::vector<std::pair<core::Annotation, cos::CosObject>>& held = versions_[now.id];
            const bool seen = std::any_of(held.begin(), held.end(), [&disk](const auto& v) { return v.first == disk; });
            if (!seen) {
                if (std::optional<cos::CosObject> dictionary = dictionaryOnDisk(now.id))
                    held.emplace_back(disk, std::move(*dictionary));
            }
        }
        if (const auto remembered = versions_.find(now.id); remembered != versions_.end()) {
            const auto version = std::find_if(remembered->second.begin(), remembered->second.end(),
                                              [&now](const auto& v) { return v.first == now; });
            if (version != remembered->second.end()) {
                // A version the file has held: written again as it was, its own appearance with it.
                Edit edit{disk, now, false, version->second};
                edits.insert_or_assign(now.id, std::move(edit));
                continue;
            }
        }
        // REDRAWN only where what it draws changed -- or where it was scaled and this build can draw it,
        // since a stretched border is the producer's line made wider than they drew it. One with no `/AP`
        // keeps none: every reader draws it from the dictionary, which is rewritten. A STAMP scaled is not
        // redrawn (T10): it is a picture, drawn at its natural size and mapped onto whatever rectangle it has
        // (`core/doc/Stamps`), so the producer's own is carried -- an image stamp read from a file, whose pixels
        // the model does not hold, resizes like any other.
        Edit edit{disk, now, false, std::nullopt};
        const bool carried = drawnTheSame(disk, now);
        const bool picture = now.subtype == core::AnnotationSubtype::Stamp;
        if (disk.hasAppearance && (!carried || (!picture && !sameSize(disk.rect, now.rect)))) {
            if (AppearanceStreamBuilder::supports(now.subtype) && !drawsUnmodelled(dictionaryOnDisk(now.id), now.subtype)) {
                auto appearance = AppearanceStreamBuilder::normal(now);
                if (!appearance) return appearance.error();
                edit.redraw = true;
            } else if (!carried) {
                return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.pdf.appearanceUnsupported"),
                                   QString(core::annotationSubtypeName(now.subtype))};
            }
        }
        edits.insert_or_assign(now.id, std::move(edit));
    }

    edits_ = std::move(edits);
    for (auto& [nm, annotation] : created) created_.insert_or_assign(nm, std::move(annotation));
    return core::Status{};
}

std::vector<int> MutationSinkImpl::pagesToRewrite() const
{
    std::set<int> pages;
    for (const auto& [page, listing] : desiredListings_) pages.insert(page);
    if (!edits_.empty()) {
        for (int page = 0; page < pageCount(); ++page) {
            for (const ListedEntry& entry : desiredListing(page)) {
                if (entry.kind == ListedEntry::Kind::Value && entry.id && edits_.count(*entry.id) != 0) {
                    pages.insert(page);
                    break;
                }
            }
        }
    }
    return std::vector<int>(pages.begin(), pages.end());
}

std::optional<cos::CosRef> MutationSinkImpl::resolveReference(const core::AnnotationId& id,
                                                              const std::map<QString, long long>& written) const
{
    switch (id.kind) {
    case core::AnnotationId::Kind::Object:
        return cos::CosRef{id.object, generationOf(table_, id.object)};
    case core::AnnotationId::Kind::Created:
        if (const auto here = written.find(id.nm); here != written.end()) return cos::CosRef{here->second, 0};
        if (const auto saved = createdOnDisk_.find(id.nm); saved != createdOnDisk_.end()) return saved->second;
        return std::nullopt;   // listed nowhere: nothing writes it
    case core::AnnotationId::Kind::Direct:
        break;
    }
    return std::nullopt;   // PDF can only point at an indirect object
}

core::Result<cos::CosObject> MutationSinkImpl::editedFrom(const cos::CosObject& onDisk, const Edit& edit,
                                                          cos::ObjectDelta& delta,
                                                          const std::map<QString, long long>& written,
                                                          AppearanceObjects& shared)
{
    if (!onDisk.isDictionary())
        return sinkError(core::ErrorCode::NotFound, "error.pdf.annotationUnknown", edit.now.page);
    if (edit.restore) return *edit.restore;   // a version the file held, verbatim
    const ReferenceResolver resolve = [this, &written](const core::AnnotationId& id) { return resolveReference(id, written); };
    cos::CosObject edited = AnnotationOps::editedDictionary(onDisk, edit.disk, edit.now, resolve);
    if (edit.redraw) {
        auto appearance = AppearanceStreamBuilder::normal(edit.now, shared);
        if (!appearance) return appearance.error();
        const long long number =
            delta.add(std::move(appearance.value().dictionary), std::move(appearance.value().data));
        edited = AnnotationOps::withEntry(edited, "AP",
                                          cos::CosObject::makeDictionary({{"N", cos::CosObject::makeReference(number, 0)}}));
    }
    return edited;
}

long long MutationSinkImpl::referenceFloor()
{
    if (!referenceFloor_) referenceFloor_ = cos::IncrementalWriter::largestReference(table_);
    return *referenceFloor_;
}

core::Status MutationSinkImpl::composeAnnotations(cos::ObjectDelta& delta,
                                                  std::map<long long, cos::CosObject>& pageDictionaries)
{
    const std::vector<int> rewrite = pagesToRewrite();
    if (rewrite.empty() && edits_.empty()) return core::Status{};
    // Before the first number is handed out: a number the file references without defining must not
    // be given a meaning by an annotation (Stage 3 guide issue I-12 -- `protectOriginal` raises the same
    // floor, but only after the delta is built, which is too late for the objects added here).
    delta.reserveNumbersThrough(referenceFloor());

    std::map<QString, long long> written;   // nm -> the number this delta gave it
    DeltaAppearanceObjects shared(delta);   // T7: fonts, once each
    std::map<core::AnnotationId, long long> promoted;   // T5: a direct dictionary edited -> its new object

    // T6: EVERY created annotation's number first, so two that name each other can both be written -- each
    // defined below, in the loop that lists it, or the compose fails and the delta is discarded.
    for (const int page : rewrite) {
        for (const ListedEntry& entry : desiredListing(page)) {
            if (entry.kind == ListedEntry::Kind::Created && written.count(entry.nm) == 0)
                written.emplace(entry.nm, delta.reserve());
        }
    }
    const ReferenceResolver resolve = [this, &written](const core::AnnotationId& id) { return resolveReference(id, written); };
    std::set<QString> defined;

    for (const int page : rewrite) {
        const std::vector<ListedEntry>& listing = desiredListing(page);
        const PageEntry& pageEntry = walk_.pages[static_cast<std::size_t>(page)];
        const cos::CosRef pageRef{pageEntry.number, generationOf(table_, pageEntry.number)};

        std::vector<cos::CosObject> items;
        items.reserve(listing.size());
        for (const ListedEntry& entry : listing) {
            switch (entry.kind) {
            case ListedEntry::Kind::Reference:
                items.push_back(cos::CosObject::makeReference(entry.reference.number, entry.reference.generation));
                break;
            case ListedEntry::Kind::Value: {
                const auto edit = entry.id ? edits_.find(*entry.id) : edits_.end();
                if (edit == edits_.end()) {
                    items.push_back(entry.value);
                    break;
                }
                // A direct dictionary first edited becomes an object (D6-4), and the page lists that.
                auto number = promoted.find(*entry.id);
                if (number == promoted.end()) {
                    auto dictionary = editedFrom(entry.value, edit->second, delta, written, shared);
                    if (!dictionary) return dictionary.error();
                    number = promoted.emplace(*entry.id, delta.add(std::move(dictionary).value())).first;
                }
                items.push_back(cos::CosObject::makeReference(number->second, 0));
                break;
            }
            case ListedEntry::Kind::Created: {
                const auto number = written.find(entry.nm);
                if (number == written.end())
                    return sinkError(core::ErrorCode::NotFound, "error.pdf.annotationUnknown", page);
                if (defined.insert(entry.nm).second) {
                    const core::Annotation* annotation = createdAnnotation(entry.nm);
                    if (annotation == nullptr)
                        return sinkError(core::ErrorCode::NotFound, "error.pdf.annotationUnknown", page);
                    std::optional<cos::CosRef> appearanceRef;
                    if (!AppearanceStreamBuilder::drawsNothing(*annotation)) {
                        auto appearance = AppearanceStreamBuilder::normal(*annotation, shared);
                        if (!appearance) return appearance.error();
                        appearanceRef = cos::CosRef{
                            delta.add(std::move(appearance.value().dictionary), std::move(appearance.value().data)), 0};
                    }
                    auto dictionary = AnnotationOps::createdDictionary(*annotation, pageRef, appearanceRef, resolve);
                    if (!dictionary) return dictionary.error();
                    // T11: a FileAttachment's file, as its /FS -- its stream compressed once (`compressedFor`).
                    if (annotation->subtype == core::AnnotationSubtype::FileAttachment && annotation->attachment
                        && annotation->attachment->carried) {
                        const std::string* flate = compressedFor(QStringLiteral("nm:") + entry.nm, annotation->attachment->data);
                        if (flate == nullptr) return sinkError(core::ErrorCode::Corrupted, "error.attachment.undecodable", page);
                        const cos::CosRef spec = addFileSpecification(delta, *annotation->attachment, *flate);
                        dictionary = AnnotationOps::withEntry(dictionary.value(), "FS",
                                                              cos::CosObject::makeReference(spec.number, spec.generation));
                    }
                    if (!delta.define(number->second, std::move(dictionary).value()))
                        return sinkError(core::ErrorCode::Corrupted, "error.pdf.annotationUnknown", page);
                }
                items.push_back(cos::CosObject::makeReference(number->second, 0));
                break;
            }
            }
        }
        const bool empty = items.empty();
        cos::CosObject array = cos::CosObject::makeArray(std::move(items));

        // Into the page's own indirect array when it has one nobody else lists; else into the page. A
        // page left with NOTHING listed loses the key -- the value a page with no annotation had before
        // one was added and saved, and the one every reader reads an empty array as anyway (guide A-25).
        const PageListing& disk = disk_[static_cast<std::size_t>(page)];
        if (!empty && disk.arrayObject && arrayUsers_[*disk.arrayObject] == 1) {
            delta.replace(*disk.arrayObject, std::move(array));
            continue;
        }
        auto base = pageDictionaries.find(pageEntry.number);
        if (base == pageDictionaries.end()) {
            auto current = currentPage(table_, pageEntry.number);
            if (!current) return current.error();
            base = pageDictionaries.emplace(pageEntry.number, std::move(current).value()).first;
        }
        base->second = empty ? AnnotationOps::withoutEntry(base->second, "Annots")
                             : AnnotationOps::withEntry(base->second, "Annots", std::move(array));
    }

    // T5: each edited annotation an object number names, rewritten ONCE however many pages list it -- and
    // only while some page lists it: an edit to an annotation since deleted is not what the update needs.
    std::set<core::AnnotationId> done;
    for (int page = 0; page < pageCount() && !edits_.empty(); ++page) {
        for (const ListedEntry& entry : desiredListing(page)) {
            if (entry.kind != ListedEntry::Kind::Reference || !entry.id) continue;
            const auto edit = edits_.find(*entry.id);
            if (edit == edits_.end() || !done.insert(*entry.id).second) continue;
            auto onDisk = table_.fetch(entry.reference.number);
            if (!onDisk) return onDisk.error();
            auto dictionary = editedFrom(onDisk.value().object, edit->second, delta, written, shared);
            if (!dictionary) return dictionary.error();
            delta.replace(entry.reference.number, std::move(dictionary).value());
        }
    }
    // Every number reserved is defined: a created annotation is listed on a page this compose rewrote.
    if (defined.size() != written.size())
        return sinkError(core::ErrorCode::Corrupted, "error.pdf.annotationUnknown", static_cast<long long>(written.size()));
    return core::Status{};
}

core::Result<MutationSinkImpl::Reconciled> MutationSinkImpl::reconcile(cos::XrefTable& written, const PageWalk& walk,
                                                                       bool strict) const
{
    Reconciled out;
    out.listings = readListings(written, walk);
    if (out.listings.size() != disk_.size()) return listingMismatch(static_cast<int>(out.listings.size()));

    const std::vector<int> rewrite = pagesToRewrite();
    for (std::size_t index = 0; index < out.listings.size(); ++index) {
        const int page = static_cast<int>(index);
        std::vector<ListedEntry>& actual = out.listings[index].entries;
        const bool touched = std::binary_search(rewrite.begin(), rewrite.end(), page);
        const std::vector<ListedEntry>& expected = desiredListing(page);

        if (!touched) {
            // A page this sink did not change: the same listing, whose entries keep their names.
            if (!sameListing(actual, expected)) {
                if (strict) return listingMismatch(page);
                continue;   // a file this sink did not compose: read as it is
            }
            for (std::size_t at = 0; at < actual.size(); ++at) actual[at].id = expected[at].id;
            continue;
        }

        if (actual.size() != expected.size()) return listingMismatch(page);
        for (std::size_t at = 0; at < actual.size(); ++at) {
            const ListedEntry& want = expected[at];
            ListedEntry& got = actual[at];
            if (want.kind == ListedEntry::Kind::Value && want.id && edits_.count(*want.id) != 0) {
                // T5: a direct dictionary edited -- listed now as the object it became.
                if (got.kind != ListedEntry::Kind::Reference) return listingMismatch(page);
                got.id = want.id;
                continue;
            }
            if (want.kind != ListedEntry::Kind::Created) {
                if (!got.sameEntry(want)) return listingMismatch(page);
                got.id = want.id;
                continue;
            }
            // A created annotation: a reference to a dictionary that IS it -- its /NM, its /Subtype, its
            // page as /P -- with a well-formed normal appearance of our own, unfiltered.
            const core::Annotation* annotation = createdAnnotation(want.nm);
            if (got.kind != ListedEntry::Kind::Reference || annotation == nullptr) return listingMismatch(page);
            auto object = written.fetch(got.reference.number);
            if (!object || object.value().object.type() != cos::CosType::Dictionary) return listingMismatch(page);
            const cos::CosObject& d = object.value().object;
            const cos::CosObject* nm = d.find("NM");
            const cos::CosObject* subtype = d.find("Subtype");
            const cos::CosObject* owner = d.find("P");
            if (nm == nullptr || !nm->isString() || EmbeddedFileReader::decodeTextString(nm->text()) != want.nm
                || subtype == nullptr || !subtype->isName()
                || QString::fromStdString(subtype->text()) != QString(core::annotationSubtypeName(annotation->subtype))
                || owner == nullptr || !owner->isReference() || owner->reference().number != walk.pages[index].number)
                return listingMismatch(page);
            if (AppearanceStreamBuilder::drawsNothing(*annotation)) {
                if (d.has("AP")) return listingMismatch(page);   // T6: a pop-up is written with none
            } else {
                const cos::CosObject& normal = d.get("AP").get("N");
                if (!normal.isReference()) return listingMismatch(page);
                auto stream = written.fetch(normal.reference().number);
                if (!stream || !stream.value().object.isStream() || stream.value().object.has("Filter"))
                    return listingMismatch(page);
                if (!cos::ContentStreamBuilder::check(stream.value().object.rawStreamData(written.source())))
                    return listingMismatch(page);
            }
            // T11: a created FileAttachment's /FS holds the very bytes compressed when it was made.
            if (annotation->subtype == core::AnnotationSubtype::FileAttachment) {
                const auto cached = compressed_.find(QStringLiteral("nm:") + want.nm);
                if (cached == compressed_.end() || !specificationHolds(written, d.find("FS"), cached->second.flate))
                    return listingMismatch(page);
            }
            got.id = want.id;
            out.created.insert_or_assign(want.nm, got.reference);
        }
    }

    // T6: every reference a created annotation makes to another created one reads back as the object that one
    // was written at -- a note's `/Popup`, its pop-up's `/Parent`, a strikeout's `/IRT` to its caret -- and one
    // whose target nothing wrote is absent rather than pointing somewhere.
    for (const auto& [nm, at] : out.created) {
        const core::Annotation* annotation = createdAnnotation(nm);
        if (annotation == nullptr) continue;
        auto object = written.fetch(at.number);
        if (!object || !object.value().object.isDictionary()) return listingMismatch(annotation->page);
        const cos::CosObject& d = object.value().object;
        for (const auto& [key, target] : {std::pair{"IRT", annotation->inReplyTo}, std::pair{"Popup", annotation->popup},
                                          std::pair{"Parent", annotation->parent}}) {
            if (!target || target->kind != core::AnnotationId::Kind::Created) continue;
            std::optional<cos::CosRef> want;
            if (const auto here = out.created.find(target->nm); here != out.created.end()) want = here->second;
            else if (const auto saved = createdOnDisk_.find(target->nm); saved != createdOnDisk_.end()) want = saved->second;
            const cos::CosObject* value = d.find(key);
            if (!want) {
                if (value != nullptr) return listingMismatch(annotation->page);
                continue;
            }
            if (value == nullptr || !value->isReference() || value->reference().number != want->number)
                return listingMismatch(annotation->page);
        }
    }

    // T5: every edited annotation reads back edited -- the dictionary its listing names has its subtype and
    // its new rectangle, and a well-formed appearance of our own where it was redrawn.
    for (std::size_t index = 0; index < out.listings.size() && !edits_.empty(); ++index) {
        for (const ListedEntry& entry : out.listings[index].entries) {
            if (entry.kind != ListedEntry::Kind::Reference || !entry.id) continue;
            const auto edit = edits_.find(*entry.id);
            if (edit == edits_.end()) continue;
            const int page = static_cast<int>(index);
            auto object = written.fetch(entry.reference.number);
            if (!object || !object.value().object.isDictionary()) return listingMismatch(page);
            const cos::CosObject& d = object.value().object;
            const cos::CosObject* subtype = d.find("Subtype");
            if (subtype == nullptr || !subtype->isName()
                || QString::fromStdString(subtype->text()) != QString(core::annotationSubtypeName(edit->second.now.subtype))
                || !rectReadsBack(d, edit->second.now.rect))
                return listingMismatch(page);
            if (edit->second.redraw) {
                const cos::CosObject& normal = d.get("AP").get("N");
                if (!normal.isReference()) return listingMismatch(page);
                auto stream = written.fetch(normal.reference().number);
                if (!stream || !stream.value().object.isStream() || stream.value().object.has("Filter")
                    || !cos::ContentStreamBuilder::check(stream.value().object.rawStreamData(written.source())))
                    return listingMismatch(page);
            }
        }
    }
    return out;
}

// ==========================================================================================
// Attachments (Stage 6 T11) -- the header's rule over `/Names /EmbeddedFiles`
// ==========================================================================================

bool MutationSinkImpl::ListedFile::sameEntry(const ListedFile& other) const
{
    if (kind != other.kind) return false;
    if (kind == Kind::Disk) return keyObject == other.keyObject && value == other.value;
    return key == other.key && created == other.created;
}

std::vector<MutationSinkImpl::ListedFile> MutationSinkImpl::readFiles(cos::XrefTable& table)
{
    std::vector<ListedFile> out;
    for (NameTreeEntry& entry : EmbeddedFileReader::entries(table)) {
        ListedFile file;
        file.kind = ListedFile::Kind::Disk;
        if (entry.key.isString()) {
            file.key = EmbeddedFileReader::decodeTextString(entry.key.text());
            file.keyBytes = QByteArray(entry.key.text().data(), static_cast<qsizetype>(entry.key.text().size()));
        }
        file.keyObject = std::move(entry.key);
        file.value = std::move(entry.value);
        out.push_back(std::move(file));
    }
    return out;
}

const std::vector<MutationSinkImpl::ListedFile>& MutationSinkImpl::desiredFiles() const
{
    return desiredFiles_ ? *desiredFiles_ : diskFiles_;
}

void MutationSinkImpl::commitFiles(std::vector<ListedFile> files)
{
    const bool asOnDisk = files.size() == diskFiles_.size()
                          && std::equal(files.begin(), files.end(), diskFiles_.begin(),
                                        [](const ListedFile& a, const ListedFile& b) { return a.sameEntry(b); });
    if (asOnDisk) desiredFiles_.reset();   // back to the disk: nothing to write
    else desiredFiles_ = std::move(files);
    // A carried file nothing desired names any more is not compressed for any compose (attach, then undo).
    const std::vector<ListedFile>& now = desiredFiles();
    for (auto it = compressed_.begin(); it != compressed_.end();) {
        const bool named = !it->first.startsWith(QLatin1String("key:"))
                           || std::any_of(now.begin(), now.end(), [&it](const ListedFile& f) {
                                  return f.kind == ListedFile::Kind::Created && QStringLiteral("key:") + f.key == it->first;
                              });
        if (named) ++it;
        else it = compressed_.erase(it);
    }
}

core::Status MutationSinkImpl::insertAttachments(const std::vector<core::DocumentAttachment>& files)
{
    if (files.empty()) return sinkError(core::ErrorCode::InvalidArgument, "error.attachment.unknown", 0);
    // Everything on scratch copies: refused part-way, nothing has changed.
    std::vector<ListedFile> scratch = desiredFiles();
    std::map<QString, RemovedFile> removed = removedFiles_;
    std::vector<const core::DocumentAttachment*> restores;
    std::vector<const core::DocumentAttachment*> attached;
    std::set<QString> keys;
    for (const core::DocumentAttachment& file : files) {
        if (file.key.isEmpty() || !keys.insert(file.key).second)
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.attachment.duplicateKey"), file.key};
        if (std::any_of(scratch.begin(), scratch.end(), [&file](const ListedFile& f) { return f.key == file.key; }))
            return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.attachment.duplicateKey"), file.key};
        if (removed.count(file.key) != 0) restores.push_back(&file);
        else if (file.file.carried) attached.push_back(&file);
        else return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.attachment.noData"), file.key};
    }

    // The pairs put back first, the LATEST removal first, each at the place it had -- undo is last-in-first-out,
    // so each place is the one its removal left.
    std::sort(restores.begin(), restores.end(), [&removed](const core::DocumentAttachment* a, const core::DocumentAttachment* b) {
        return removed.at(a->key).sequence > removed.at(b->key).sequence;
    });
    for (const core::DocumentAttachment* file : restores) {
        const RemovedFile was = removed.at(file->key);
        scratch.insert(scratch.begin() + static_cast<std::ptrdiff_t>(std::min(was.position, scratch.size())), was.entry);
        removed.erase(file->key);
    }
    // Then each new file where its key sorts.
    for (const core::DocumentAttachment* file : attached) {
        ListedFile entry;
        entry.kind = ListedFile::Kind::Created;
        entry.key = file->key;
        entry.keyBytes = core::nameTreeKeyBytes(file->key);
        entry.created = *file;
        const auto at = std::find_if(scratch.begin(), scratch.end(),
                                     [&entry](const ListedFile& f) { return entry.keyBytes < f.keyBytes; });
        scratch.insert(at, std::move(entry));
    }
    removedFiles_ = std::move(removed);
    commitFiles(std::move(scratch));
    return core::Status{};
}

core::Status MutationSinkImpl::removeAttachments(const std::vector<QString>& keys)
{
    if (keys.empty()) return sinkError(core::ErrorCode::InvalidArgument, "error.attachment.unknown", 0);
    std::vector<ListedFile> scratch = desiredFiles();
    std::map<QString, RemovedFile> removed = removedFiles_;
    long long sequence = removalSequence_;
    for (const QString& key : keys) {
        const auto at = std::find_if(scratch.begin(), scratch.end(),
                                     [&key](const ListedFile& f) { return !key.isEmpty() && f.key == key; });
        if (at == scratch.end()) return core::Error{core::ErrorCode::NotFound, QLatin1String("error.attachment.unknown"), key};
        // A pair the file holds is kept, so an undo lists it again; an attached one is its command's to carry.
        if (at->kind == ListedFile::Kind::Disk)
            removed.insert_or_assign(key, RemovedFile{*at, static_cast<std::size_t>(at - scratch.begin()), ++sequence});
        scratch.erase(at);
    }
    removedFiles_ = std::move(removed);
    removalSequence_ = sequence;
    commitFiles(std::move(scratch));
    return core::Status{};
}

const std::string* MutationSinkImpl::compressedFor(const QString& cacheKey, const QByteArray& data)
{
    if (const auto found = compressed_.find(cacheKey); found != compressed_.end() && found->second.data == data)
        return &found->second.flate;
    auto flate = cos::filters::flateEncode(std::string_view(data.constData(), static_cast<std::size_t>(data.size())));
    if (!flate) return nullptr;
    Compressed entry{data, std::string(flate.value().begin(), flate.value().end())};
    return &compressed_.insert_or_assign(cacheKey, std::move(entry)).first->second.flate;
}

cos::CosRef MutationSinkImpl::addFileSpecification(cos::ObjectDelta& delta, const core::EmbeddedFile& file,
                                                   std::string compressed)
{
    using cos::CosObject;
    std::vector<CosObject::Entry> params;
    const long long size = file.carried ? static_cast<long long>(file.data.size()) : file.size;
    if (size >= 0) params.push_back({"Size", CosObject::makeInteger(size)});
    if (file.checksum.size() == 16)
        params.push_back({"CheckSum", CosObject::makeString(file.checksum.toStdString(), cos::StringForm::Hex)});
    if (!file.created.raw.isEmpty()) params.push_back({"CreationDate", AnnotationOps::textString(file.created.raw)});
    if (!file.modified.raw.isEmpty()) params.push_back({"ModDate", AnnotationOps::textString(file.modified.raw)});

    std::vector<CosObject::Entry> stream{{"Type", CosObject::makeName("EmbeddedFile")}};
    if (!file.mime.isEmpty()) stream.push_back({"Subtype", CosObject::makeName(file.mime.toStdString())});
    stream.push_back({"Params", CosObject::makeDictionary(std::move(params))});
    stream.push_back({"Filter", CosObject::makeName("FlateDecode")});
    const long long streamNumber = delta.add(CosObject::makeStream(std::move(stream), cos::CosStreamData{}), std::move(compressed));

    std::vector<CosObject::Entry> spec{
        {"Type", CosObject::makeName("Filespec")},
        {"F", asciiFileName(file.name)},
        {"UF", AnnotationOps::textString(file.name)},
    };
    if (!file.description.isEmpty()) spec.push_back({"Desc", AnnotationOps::textString(file.description)});
    spec.push_back({"EF", CosObject::makeDictionary({{"F", CosObject::makeReference(streamNumber, 0)},
                                                     {"UF", CosObject::makeReference(streamNumber, 0)}})});
    return cos::CosRef{delta.add(CosObject::makeDictionary(std::move(spec))), 0};
}

core::Status MutationSinkImpl::composeFiles(cos::ObjectDelta& delta)
{
    if (!desiredFiles_) return core::Status{};
    delta.reserveNumbersThrough(referenceFloor());

    // The tree, flat: one `/Names` array in the order the sink keeps -- every key's place decided when it was put
    // there, by the bytes it is written as.
    std::vector<cos::CosObject> pairs;
    pairs.reserve(desiredFiles_->size() * 2);
    for (const ListedFile& file : *desiredFiles_) {
        if (file.kind == ListedFile::Kind::Disk) {
            pairs.push_back(file.keyObject);
            pairs.push_back(file.value);
            continue;
        }
        const std::string* flate = compressedFor(QStringLiteral("key:") + file.key, file.created.file.data);
        if (flate == nullptr) return sinkError(core::ErrorCode::Corrupted, "error.attachment.undecodable", -1);
        const cos::CosRef spec = addFileSpecification(delta, file.created.file, *flate);
        pairs.push_back(keyString(file.key));
        pairs.push_back(cos::CosObject::makeReference(spec.number, spec.generation));
    }
    std::optional<cos::CosObject> tree;
    if (!pairs.empty()) {
        const long long number = delta.add(cos::CosObject::makeDictionary({{"Names", cos::CosObject::makeArray(std::move(pairs))}}));
        tree = cos::CosObject::makeReference(number, 0);
    }
    const auto withTree = [&tree](const cos::CosObject& names) {
        return tree ? AnnotationOps::withEntry(names, "EmbeddedFiles", *tree) : AnnotationOps::withoutEntry(names, "EmbeddedFiles");
    };

    // The catalog's `/Names`: into the names dictionary's own object when it has one, else into the catalog.
    const cos::CosObject& root = table_.trailer().get("Root");
    if (!root.isReference()) return sinkError(core::ErrorCode::Corrupted, "error.pdf.objectMissing", -1);
    auto catalog = table_.fetch(root.reference().number);
    if (!catalog || !catalog.value().object.isDictionary())
        return sinkError(core::ErrorCode::NotFound, "error.pdf.objectMissing", root.reference().number);
    cos::CosObject catalogDictionary = catalog.value().object;
    catalogDictionary.clearSpansRecursively();
    const cos::CosObject* names = catalogDictionary.find("Names");
    if (names != nullptr && names->isReference()) {
        auto namesObject = table_.fetch(names->reference().number);
        if (namesObject && namesObject.value().object.isDictionary() && !namesObject.value().object.isStream()) {
            cos::CosObject current = namesObject.value().object;
            current.clearSpansRecursively();
            delta.replace(names->reference().number, withTree(current));
            return core::Status{};
        }
    }
    const cos::CosObject current = (names != nullptr && names->isDictionary()) ? *names : cos::CosObject::makeDictionary({});
    delta.replace(root.reference().number, AnnotationOps::withEntry(catalogDictionary, "Names", withTree(current)));
    return core::Status{};
}

core::Result<std::vector<MutationSinkImpl::ListedFile>> MutationSinkImpl::reconcileFiles(cos::XrefTable& written, bool strict) const
{
    const auto mismatch = [] {
        return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.save.verifyFailed"), QStringLiteral("attachments")};
    };
    std::vector<ListedFile> actual = readFiles(written);
    const std::vector<ListedFile>& expected = desiredFiles();
    if (!desiredFiles_) {
        // A tree this sink did not change: the same pairs, or -- adopting a file it did not compose -- as it is.
        const bool same = actual.size() == expected.size()
                          && std::equal(actual.begin(), actual.end(), expected.begin(),
                                        [](const ListedFile& a, const ListedFile& b) { return a.sameEntry(b); });
        if (!same && strict) return mismatch();
        return actual;
    }
    if (actual.size() != expected.size()) return mismatch();
    for (std::size_t at = 0; at < actual.size(); ++at) {
        const ListedFile& want = expected[at];
        const ListedFile& got = actual[at];
        if (want.kind == ListedFile::Kind::Disk) {
            if (!got.sameEntry(want)) return mismatch();
            continue;
        }
        // An attached file: under its key, a specification whose stream is the bytes compressed at attach.
        const auto cached = compressed_.find(QStringLiteral("key:") + want.key);
        if (got.keyBytes != want.keyBytes || cached == compressed_.end()
            || !specificationHolds(written, &got.value, cached->second.flate))
            return mismatch();
    }
    return actual;
}

core::Result<QByteArray> MutationSinkImpl::attachmentData(const core::AttachmentRef& attachment)
{
    const auto notFound = [&attachment] {
        return core::Error{core::ErrorCode::NotFound, QLatin1String("error.attachment.unknown"),
                           attachment.kind == core::AttachmentRef::Kind::Document ? attachment.key
                                                                                 : attachment.annotation.toString()};
    };
    if (attachment.kind == core::AttachmentRef::Kind::Document) {
        for (const ListedFile& file : desiredFiles()) {
            if (attachment.key.isEmpty() || file.key != attachment.key) continue;
            if (file.kind == ListedFile::Kind::Created) return file.created.file.data;
            return EmbeddedFileReader::dataOf(table_, &file.value);
        }
        return notFound();
    }
    // A FileAttachment: one created and not saved carries its file; one the file lists has its /FS on disk.
    if (attachment.annotation.kind == core::AnnotationId::Kind::Created) {
        if (const core::Annotation* created = createdAnnotation(attachment.annotation.nm)) {
            if (created->attachment && created->attachment->carried) return created->attachment->data;
            return notFound();
        }
    }
    const std::optional<cos::CosObject> dictionary = dictionaryOnDisk(attachment.annotation);
    if (!dictionary) return notFound();
    return EmbeddedFileReader::dataOf(table_, dictionary->find("FS"));
}

core::Status MutationSinkImpl::adopt(std::string_view bytes)
{
    auto table = tableFor(bytes);
    if (!table)
        return table.error();
    auto walk = PageOps::walk(table.value());
    if (!walk)
        return walk.error();
    if (walk.value().pages.size() != walk_.pages.size())
        return sinkError(core::ErrorCode::Corrupted, "error.pdf.pageTreeChanged",
                         static_cast<long long>(walk.value().pages.size()));

    // The annotations: every listing this sink changed must read back as it composed it, and each
    // entry keeps the name the model gives it -- a created annotation is now the object the save put it at.
    auto reconciled = reconcile(table.value(), walk.value(), false);
    if (!reconciled)
        return reconciled.error();
    auto files = reconcileFiles(table.value(), false);   // T11: the tree, as it composed it
    if (!files)
        return files.error();

    bytes_ = bytes;
    table_ = std::move(table).value();
    walk_ = std::move(walk).value();
    disk_ = std::move(reconciled.value().listings);
    countArrayUsers(disk_, arrayUsers_);
    for (auto& [nm, reference] : reconciled.value().created) createdOnDisk_.insert_or_assign(nm, reference);
    desiredListings_.clear();
    created_.clear();
    edits_.clear();
    referenceFloor_.reset();
    diskFiles_ = std::move(files).value();
    desiredFiles_.reset();
    compressed_.clear();   // every carried file is now the file's: its bytes are read from the stream
    for (auto it = desired_.begin(); it != desired_.end();) {
        if (it->second == walk_.pages[static_cast<std::size_t>(it->first)].effectiveRotate())
            it = desired_.erase(it);
        else
            ++it;
    }
    return core::Status{};
}

} // namespace rawpage::pdf::ops
