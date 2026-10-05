// pdf/ops/MutationSinkImpl -- implementation. Stage 3 T3. See the header for desired state.

#include "pdf/ops/MutationSinkImpl.h"

#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationOps.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <algorithm>
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

} // namespace

MutationSinkImpl::MutationSinkImpl(std::string_view bytes, cos::XrefTable table, PageWalk walk)
    : bytes_(bytes), table_(std::move(table)), walk_(std::move(walk))
{
    disk_ = readListings(table_, walk_);
    countArrayUsers(disk_, arrayUsers_);
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
        if (const auto removed = removedDirect_.find(annotation.id); removed != removedDirect_.end()) {
            entry.kind = ListedEntry::Kind::Value;          // a direct entry, listed again as written
            entry.value = removed->second;
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
                // Written new -- so it must be a kind this build can draw (D6-6), asked now, not at save.
                auto appearance = AppearanceStreamBuilder::normal(annotation);
                if (!appearance) return appearance.error();
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
    std::vector<std::pair<core::AnnotationId, cos::CosObject>> removedDirect;
    for (const core::AnnotationPlacement& place : at) {
        if (place.page < 0 || place.page >= pageCount()) return placementError(place);
        std::vector<ListedEntry>& listing = listingToChange(place.page, scratch);
        if (place.position < 0 || static_cast<std::size_t>(place.position) >= listing.size())
            return placementError(place);
        const ListedEntry& entry = listing[static_cast<std::size_t>(place.position)];
        if (!entry.id) return placementError(place);   // not an annotation: not the model's to drop
        if (entry.kind == ListedEntry::Kind::Value) removedDirect.emplace_back(*entry.id, entry.value);
        listing.erase(listing.begin() + place.position);
    }
    for (auto& [id, value] : removedDirect) removedDirect_.insert_or_assign(id, std::move(value));
    commitListings(std::move(scratch));
    return core::Status{};
}

long long MutationSinkImpl::referenceFloor()
{
    if (!referenceFloor_) referenceFloor_ = cos::IncrementalWriter::largestReference(table_);
    return *referenceFloor_;
}

core::Status MutationSinkImpl::composeAnnotations(cos::ObjectDelta& delta,
                                                  std::map<long long, cos::CosObject>& pageDictionaries)
{
    if (desiredListings_.empty()) return core::Status{};
    // Before the first number is handed out: a number the file references without defining must not
    // be given a meaning by an annotation (Stage 3 guide issue I-12 -- `protectOriginal` raises the same
    // floor, but only after the delta is built, which is too late for the objects added here).
    delta.reserveNumbersThrough(referenceFloor());

    std::map<QString, long long> written;   // nm -> the number this delta gave it
    for (const auto& [page, listing] : desiredListings_) {
        const PageEntry& pageEntry = walk_.pages[static_cast<std::size_t>(page)];
        const cos::CosRef pageRef{pageEntry.number, generationOf(table_, pageEntry.number)};

        std::vector<cos::CosObject> items;
        items.reserve(listing.size());
        for (const ListedEntry& entry : listing) {
            switch (entry.kind) {
            case ListedEntry::Kind::Reference:
                items.push_back(cos::CosObject::makeReference(entry.reference.number, entry.reference.generation));
                break;
            case ListedEntry::Kind::Value:
                items.push_back(entry.value);
                break;
            case ListedEntry::Kind::Created: {
                auto number = written.find(entry.nm);
                if (number == written.end()) {
                    const core::Annotation* annotation = createdAnnotation(entry.nm);
                    if (annotation == nullptr)
                        return sinkError(core::ErrorCode::NotFound, "error.pdf.annotationUnknown", page);
                    auto appearance = AppearanceStreamBuilder::normal(*annotation);
                    if (!appearance) return appearance.error();
                    const long long apNumber =
                        delta.add(std::move(appearance.value().dictionary), std::move(appearance.value().data));
                    auto dictionary = AnnotationOps::createdDictionary(*annotation, pageRef, cos::CosRef{apNumber, 0});
                    if (!dictionary) return dictionary.error();
                    number = written.emplace(entry.nm, delta.add(std::move(dictionary).value())).first;
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
    return core::Status{};
}

core::Result<MutationSinkImpl::Reconciled> MutationSinkImpl::reconcile(cos::XrefTable& written, const PageWalk& walk,
                                                                       bool strict) const
{
    Reconciled out;
    out.listings = readListings(written, walk);
    if (out.listings.size() != disk_.size()) return listingMismatch(static_cast<int>(out.listings.size()));

    for (std::size_t index = 0; index < out.listings.size(); ++index) {
        const int page = static_cast<int>(index);
        std::vector<ListedEntry>& actual = out.listings[index].entries;
        const bool touched = desiredListings_.count(page) != 0;
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
            const cos::CosObject& normal = d.get("AP").get("N");
            if (!normal.isReference()) return listingMismatch(page);
            auto stream = written.fetch(normal.reference().number);
            if (!stream || !stream.value().object.isStream() || stream.value().object.has("Filter"))
                return listingMismatch(page);
            if (!cos::ContentStreamBuilder::check(stream.value().object.rawStreamData(written.source())))
                return listingMismatch(page);
            got.id = want.id;
            out.created.insert_or_assign(want.nm, got.reference);
        }
    }
    return out;
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

    bytes_ = bytes;
    table_ = std::move(table).value();
    walk_ = std::move(walk).value();
    disk_ = std::move(reconciled.value().listings);
    countArrayUsers(disk_, arrayUsers_);
    for (auto& [nm, reference] : reconciled.value().created) createdOnDisk_.insert_or_assign(nm, reference);
    desiredListings_.clear();
    created_.clear();
    referenceFloor_.reset();
    for (auto it = desired_.begin(); it != desired_.end();) {
        if (it->second == walk_.pages[static_cast<std::size_t>(it->first)].effectiveRotate())
            it = desired_.erase(it);
        else
            ++it;
    }
    return core::Status{};
}

} // namespace rawpage::pdf::ops
