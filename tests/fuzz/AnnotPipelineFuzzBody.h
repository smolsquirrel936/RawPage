// The body of `fuzz_annot_pipeline` -- Stage 6 T2, decision D6-29 (`Stage6-Annotations-Plan.md` §3) --
// in a header so the libFuzzer target and the replay in `tests/unit/tst_annotstore.cpp` share exactly
// one copy, the arrangement `RoundtripFuzzBody.h` and `TextPipelineFuzzBody.h` document: libFuzzer needs
// clang and a Linux runner, and a body kept only in the target rots between the day it is written and
// the day anybody reads its output.
//
// -------------------------------------------------------------------------------------------
// THE PIPELINE, AS FAR AS T3 BUILDS IT
// -------------------------------------------------------------------------------------------
// bytes -> `pdf/cos/` (through `MutationSinkImpl::open`, the backend's own walk) ->
// `pdf/ops/AnnotationReader` -> `core::AnnotationStore` (threads, states, queries) -> every annotation's
// JSON round trip -> (T3) `AppearanceStreamBuilder` over every annotation of a kind it draws, the
// generated stream re-read, and the input itself re-read as a content stream -> (T3) a highlight added
// to page 1 and every annotation it listed removed, composed by `RevisionWriter` and VERIFIED, as a
// save would. **T13's EXCHANGE BRANCH** (D6-29 named it): the same bytes read as an exchange file --
// `pdf/ops/FdfCodec::readEither`, FDF or XFDF by their first bytes, as an import reads a file -- and,
// where they are one, written back in both syntaxes and read again, and merged into a page.
//
// THE INVARIANTS, each one a defect the reader or the store could plausibly have:
//   * LISTING   -- every listing entry names an annotation that exists, or none; every annotation is
//                  listed by the page that owns it;
//   * BOUNDED   -- no annotation keeps more than `AnnotationReader::kMaxCoordinates` numbers of geometry,
//                  and every number it keeps is finite;
//   * RECT      -- a finite rectangle is normalized;
//   * THREADS   -- every parent chain ends within `size()` steps (a cycle that survived would loop the
//                  Comments pane), and a parent is on its reply's page;
//   * PARTITION -- every annotation is exactly one of: top-level, under a parent, a state reply, a
//                  pop-up;
//   * QUERY     -- the unfiltered query is the top-level list, in some order, each once;
//   * JSON      -- `Annotation::fromJson(a.toJson()) == a`, which T3's journal relies on;
//   * APPEARANCE -- (T3) the builder either refuses an annotation by key or returns a stream
//                  `ContentStreamBuilder::check` accepts; and `check` survives any bytes at all;
//   * WRITE     -- (T3) a composed update over a highlight added and page 1's annotations removed is one
//                  `RevisionWriter::verify` accepts. A compose REFUSED is not a finding (a repaired file
//                  chains to nothing, D3-6); a compose that verify rejects is our writer writing a file
//                  our reader cannot read back as the sink meant it.
//   * EXCHANGE  -- (T13) an exchange read is NAMED (every annotation `Created` under a name, the same as its
//                  `/NM`, within the bound), BOUNDED (`kMaxExchangeCoordinates`, finite), its JSON round-trips;
//                  written as XFDF and as FDF it reads again with no more annotations than were written, every
//                  name one it was written under; and merged into an empty page it decides once for each
//                  annotation, adding each name once, every reference to a name it adds or to nothing.
//
// WHAT IS NOT A FINDING: an input the sink cannot walk. That is `fuzz_cos_parser`'s ground.

#ifndef RAWPAGE_TESTS_FUZZ_ANNOTPIPELINEFUZZBODY_H
#define RAWPAGE_TESTS_FUZZ_ANNOTPIPELINEFUZZBODY_H

#include "core/doc/AnnotationStore.h"
#include "core/doc/Fdf.h"
#include "core/doc/XfdfCodec.h"
#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/FdfCodec.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "pdf/ops/RevisionWriter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace rawpage::fuzz {

struct AnnotVerdict {
    bool ok = true;
    bool read = false;          // the sink walked a page tree, so the pipeline ran
    std::size_t annotations = 0;
    std::string failure;
};

// THE EXCHANGE BRANCH (T13): `bytes` as an import reads a file. Returns the failure, or nothing.
inline std::optional<std::string> exchangeFuzzOnce(const QByteArray& bytes, std::size_t* annotations = nullptr)
{
    core::Result<core::Fdf> read = pdf::ops::FdfCodec::readEither(bytes);
    if (!read) return std::nullopt;   // refused by key: what a hostile file is owed
    const core::Fdf& exchange = read.value();
    if (annotations != nullptr) *annotations = exchange.annotations.size();
    if (exchange.annotations.size() > core::kMaxExchangeAnnotations) return std::string("an exchange past its bound");
    std::set<QString> names;
    for (const core::Annotation& a : exchange.annotations) {
        // NAMED
        if (a.id.kind != core::AnnotationId::Kind::Created || a.id.nm.isEmpty() || a.nm != a.id.nm)
            return std::string("an exchange annotation without its name");
        if (a.nm.size() > core::kMaxExchangeNameLength + 6) return std::string("an exchange name past its bound");
        if (!names.insert(a.nm).second) return std::string("an exchange name given twice");
        // BOUNDED, finite
        std::size_t numbers = a.quadPoints.size() + a.vertices.size();
        for (const std::vector<double>& stroke : a.inkList) numbers += stroke.size();
        if (numbers > core::kMaxExchangeCoordinates) return std::string("exchange geometry not bounded");
        const auto finite = [](const std::vector<double>& values) {
            return std::all_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); });
        };
        if (!finite(a.quadPoints) || !finite(a.vertices) || !finite(a.line) || !finite(a.color) || !finite(a.callout))
            return std::string("a non-finite number kept from an exchange");
        // JSON
        const core::Result<core::Annotation> back = core::Annotation::fromJson(a.toJson());
        if (!back || !(back.value() == a)) return std::string("an exchange annotation's JSON round trip changed it");
    }
    // WRITTEN BOTH WAYS AND READ AGAIN
    for (const bool xfdf : {true, false}) {
        const QByteArray written = xfdf ? core::XfdfCodec::write(exchange) : QByteArray::fromStdString(pdf::ops::FdfCodec::write(exchange));
        core::Result<core::Fdf> again = pdf::ops::FdfCodec::readEither(written);
        if (!again) return std::string(xfdf ? "an XFDF we wrote that we cannot read" : "an FDF we wrote that we cannot read");
        if (again.value().annotations.size() > exchange.annotations.size()) return std::string("an exchange read back with more than it held");
        if (!xfdf) {
            for (const core::Annotation& a : again.value().annotations) {
                if (names.count(a.nm) == 0) return std::string("an FDF read back under a name it was not written under");
            }
        }
    }
    // MERGED INTO AN EMPTY PAGE
    core::AnnotationStore::Reading empty;
    empty.pages.resize(1);
    const core::AnnotationStore store = core::AnnotationStore::fromReading(std::move(empty));
    const core::FdfImport plan = store.importFdf(exchange);
    std::size_t comments = 0;
    for (const core::Annotation& a : exchange.annotations) comments += a.subtype == core::AnnotationSubtype::Popup ? 0 : 1;
    if (plan.result.entries.size() != comments) return std::string("a merge that did not decide once for each comment");
    std::set<QString> added;
    for (const core::Annotation& a : plan.added) {
        if (!added.insert(a.id.nm).second) return std::string("a merge that added one name twice");
    }
    for (const core::Annotation& a : plan.added) {
        for (const std::optional<core::AnnotationId>& target : {a.inReplyTo, a.popup, a.parent}) {
            if (target && (target->kind != core::AnnotationId::Kind::Created || added.count(target->nm) == 0))
                return std::string("a merge that kept a reference to nothing it added");
        }
    }
    return std::nullopt;
}

inline AnnotVerdict annotPipelineFuzzOnce(const std::uint8_t* data, std::size_t size)
{
    AnnotVerdict verdict;
    auto fail = [&verdict](std::string why) {
        verdict.ok = false;
        verdict.failure = std::move(why);
        return verdict;
    };

    // EXCHANGE (T13) -- before the walk, so a file that is no PDF is fuzzed too.
    if (const std::optional<std::string> failed =
            exchangeFuzzOnce(QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size))))
        return fail(*failed);

    auto sink = pdf::ops::MutationSinkImpl::open(
        std::string_view(reinterpret_cast<const char*>(data), size));
    if (!sink) return verdict;
    core::AnnotationStore::Reading reading =
        pdf::ops::AnnotationReader::read(sink.value()->table(), sink.value()->pages());
    verdict.read = true;
    const std::size_t count = reading.annotations.size();
    const int pages = static_cast<int>(reading.pages.size());

    // LISTING
    for (int page = 0; page < pages; ++page) {
        for (const core::AnnotationListingEntry& entry : reading.pages[static_cast<std::size_t>(page)]) {
            if (entry.annotation && *entry.annotation >= count) return fail("listing names no annotation");
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        const core::Annotation& a = reading.annotations[index];
        if (a.page < 0 || a.page >= pages) return fail("annotation owned by no page");
        const auto& listing = reading.pages[static_cast<std::size_t>(a.page)];
        if (std::none_of(listing.begin(), listing.end(),
                         [index](const core::AnnotationListingEntry& e) { return e.annotation == index; }))
            return fail("annotation not listed by the page that owns it");

        // BOUNDED, finite
        std::size_t numbers = a.quadPoints.size() + a.vertices.size();
        for (const std::vector<double>& stroke : a.inkList) numbers += stroke.size();
        if (numbers > pdf::ops::AnnotationReader::kMaxCoordinates) return fail("geometry not bounded");
        auto finite = [](const std::vector<double>& values) {
            return std::all_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); });
        };
        if (!finite(a.quadPoints) || !finite(a.vertices) || !finite(a.line) || !finite(a.color))
            return fail("a non-finite number kept");
        for (const std::vector<double>& stroke : a.inkList) {
            if (!finite(stroke)) return fail("a non-finite ink sample kept");
        }
        // RECT
        if (a.rectFinite && (a.rect.size.width < 0 || a.rect.size.height < 0))
            return fail("rectangle not normalized");
    }

    const core::AnnotationStore store = core::AnnotationStore::fromReading(std::move(reading));
    verdict.annotations = store.size();

    // THREADS and PARTITION
    for (std::size_t index = 0; index < store.size(); ++index) {
        std::size_t steps = 0;
        std::optional<std::size_t> at = store.parentOf(index);
        while (at) {
            if (++steps > store.size()) return fail("a parent chain that does not end");
            if (store.at(*at).page != store.at(index).page && steps == 1)
                return fail("a parent on another page");
            at = store.parentOf(*at);
        }
        const core::Annotation& a = store.at(index);
        const bool popup = a.subtype == core::AnnotationSubtype::Popup;
        const bool state = !popup && a.isStateReply();
        const bool child = !popup && !state && store.parentOf(index).has_value();
        const int places = int(store.isTopLevel(index)) + int(popup) + int(state) + int(child);
        if (places != 1) return fail("an annotation in no place, or in two");
    }

    // QUERY
    std::vector<std::size_t> listed = store.query({}, core::AnnotationStore::SortKey::Date);
    std::vector<std::size_t> top = store.topLevel();
    std::sort(listed.begin(), listed.end());
    if (listed != top) return fail("the unfiltered query is not the top-level list");

    // JSON
    for (const core::Annotation& a : store.all()) {
        const core::Result<core::Annotation> back = core::Annotation::fromJson(a.toJson());
        if (!back) return fail("an annotation's JSON does not read back");
        if (!(back.value() == a)) return fail("an annotation's JSON round trip changed it");
    }

    // APPEARANCE
    (void)pdf::cos::ContentStreamBuilder::check(std::string_view(reinterpret_cast<const char*>(data), size));
    for (const core::Annotation& a : store.all()) {
        if (!pdf::ops::AppearanceStreamBuilder::supports(a.subtype)) continue;
        const auto appearance = pdf::ops::AppearanceStreamBuilder::normal(a);
        if (appearance && !pdf::cos::ContentStreamBuilder::check(appearance.value().data))
            return fail("a generated appearance its own re-read refuses");
        if (appearance && !appearance.value().dictionary.isStream())
            return fail("a generated appearance that is not a stream");
    }

    // WRITE
    if (pages > 0) {
        pdf::ops::MutationSinkImpl& editing = *sink.value();
        std::vector<core::AnnotationPlacement> removals;
        const std::vector<pdf::ops::MutationSinkImpl::ListedEntry> listing = editing.desiredListing(0);
        for (std::size_t at = listing.size(); at > 0; --at) {
            if (listing[at - 1].id) removals.push_back(core::AnnotationPlacement{0, static_cast<int>(at - 1)});
        }
        if (!removals.empty() && !editing.removeAnnotations(removals)) return fail("the sink refused its own listing");
        core::Annotation added = core::newAnnotation(core::AnnotationSubtype::Highlight, 0, "Fuzz", 0);
        added.nm = "0123456789abcdef0123456789abcdef";   // fixed: a finding must reproduce byte for byte
        added.id = core::AnnotationId::ofCreated(added.nm);
        added.quadPoints = {10, 30, 110, 30, 10, 20, 110, 20};
        added.rect = *core::boundsOfPoints(added.quadPoints);
        const int end = static_cast<int>(editing.desiredListing(0).size());
        if (!editing.insertAnnotations({core::AnnotationPlacement{0, end}}, {added}))
            return fail("the sink refused a highlight at the end of page 1");
        auto composed = pdf::ops::RevisionWriter::compose(editing, false);
        if (composed) {
            const std::string_view written(composed.value().bytes.constData(),
                                           static_cast<std::size_t>(composed.value().bytes.size()));
            if (!pdf::ops::RevisionWriter::verify(editing.bytes(), written, editing))
                return fail("a composed annotation update its own verify refuses");
        }
    }
    return verdict;
}

} // namespace rawpage::fuzz

#endif // RAWPAGE_TESTS_FUZZ_ANNOTPIPELINEFUZZBODY_H
