// The body of `fuzz_annot_pipeline` -- Stage 6 T2, decision D6-29 (`Stage6-Annotations-Plan.md` §3) --
// in a header so the libFuzzer target and the replay in `tests/unit/tst_annotstore.cpp` share exactly
// one copy, the arrangement `RoundtripFuzzBody.h` and `TextPipelineFuzzBody.h` document: libFuzzer needs
// clang and a Linux runner, and a body kept only in the target rots between the day it is written and
// the day anybody reads its output.
//
// -------------------------------------------------------------------------------------------
// THE PIPELINE, AS FAR AS T2 BUILDS IT
// -------------------------------------------------------------------------------------------
// bytes -> `pdf/cos/` (through `MutationSinkImpl::open`, the backend's own walk) ->
// `pdf/ops/AnnotationReader` -> `core::AnnotationStore` (threads, states, queries) -> every annotation's
// JSON round trip. **T3 extends it** with `AppearanceStreamBuilder` and the generated stream re-read,
// and **T13** with the XFDF branch, as D6-29 lists; until then those halves are not fuzzed, and this
// comment is where that is said.
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
//   * JSON      -- `Annotation::fromJson(a.toJson()) == a`, which T3's journal relies on.
//
// WHAT IS NOT A FINDING: an input the sink cannot walk. That is `fuzz_cos_parser`'s ground.

#ifndef RAWPAGE_TESTS_FUZZ_ANNOTPIPELINEFUZZBODY_H
#define RAWPAGE_TESTS_FUZZ_ANNOTPIPELINEFUZZBODY_H

#include "core/doc/AnnotationStore.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/MutationSinkImpl.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
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

inline AnnotVerdict annotPipelineFuzzOnce(const std::uint8_t* data, std::size_t size)
{
    AnnotVerdict verdict;
    auto fail = [&verdict](std::string why) {
        verdict.ok = false;
        verdict.failure = std::move(why);
        return verdict;
    };

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
    return verdict;
}

} // namespace rawpage::fuzz

#endif // RAWPAGE_TESTS_FUZZ_ANNOTPIPELINEFUZZBODY_H
