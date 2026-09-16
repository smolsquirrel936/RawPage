// The body of `fuzz_cos_roundtrip`, in a header so that THREE callers share exactly one copy of it --
// the arrangement `tests/fuzz/LexerFuzzBody.h` and `ParserFuzzBody.h` document, for the same reason:
// libFuzzer needs clang and a Linux runner, so an entry point kept only in the fuzz target rots
// between the day it is written and the day anybody reads its output. Stage 3 T13, decision D3-17
// (`Stage3-Mutation-Plan.md` §3).
//
//   * `tests/fuzz/fuzz_cos_roundtrip.cpp` wraps it in `LLVMFuzzerTestOneInput` for `portable-fuzz`;
//   * `tests/roundtrip/tst_roundtrip.cpp` replays every corpus file through it on every `ctest` run,
//     Windows and `portable-asan` alike, with three planted controls;
//   * `tests/fuzz/seeds/RoundtripSeeds.cpp` keeps what it wrote, which is how the fuzzer's seeds gain
//     the writer's own outputs.
//
// The replay is not a substitute for the fuzzer. Known files find nothing new by construction
// (convention 19); ten clean minutes is a claim made on a machine this one is not.
//
// -------------------------------------------------------------------------------------------
// THE PROPERTY: whatever we could read, we can append to and read again
// -------------------------------------------------------------------------------------------
// The writer is the riskiest first-party code in the project, and it consumes whatever the tolerant
// reader produced from a damaged file -- spans that stop short, objects recovered by scan, lengths
// that lied. This is the first target that tests two components against each other. For an input the
// product would let a user edit:
//
//   1. rotate its first page a quarter turn through `pdf/ops/MutationSinkImpl`, the product's sink;
//   2. compose the next revision through `pdf/ops/RevisionWriter::compose`, with the product's choice
//      of section: D3-6's COMPLETE section when the opening repair `requiresSaveAs()`, a `/Prev`
//      update otherwise -- `pdf/DocumentBackendImpl::writeRevision`'s rule;
//   3. require, checked HERE and not by asking the writer:
//        * PREFIX   -- every input byte unchanged at the start of the output;
//        * REPARSE  -- the output opens through `pdf/cos/openWithRepair`;
//        * OWN TABLE -- through its own cross-reference structures, with nothing that forces a
//                      Save-As (the product refuses a save that would, so a violation is a document
//                      the user can edit and never save);
//        * ROTATION -- the same number of pages, page 1 at the new rotation, every other page where
//                      it was;
//   4. and require `RevisionWriter::verify` -- the product's own guard before it replaces a file -- to
//      AGREE. Agreement is the finding in both directions: a guard that refuses a correct file costs
//      the user a save, and one that passes an incorrect file costs them the document.
//
// WHAT IS NOT A FINDING. An input the reader cannot open, whose page tree the sink cannot walk, with
// no page, or that is encrypted (declined by name, D2-6) is not editable, and the product never
// offers to save it. An input the WRITER refuses by key after the sink accepted it is recorded in
// `refusedKey` and IS a finding here: the sink said the page is there, so a refusal is the two
// components disagreeing, which is the thing this target exists to find. The replay requires zero.
//
// WHAT IT DOES NOT COVER (convention 19): the engine -- PDFium never sees these bytes, and whether it
// agrees with the first-party walk is `tests/render/tst_pageopsengine.cpp`'s; the disk, the flush and
// the replace (`RevisionWriter::write`, `SaveCoordinator`, `IDurableFiles`) -- this composes in
// memory, which is what lets it run a hundred thousand times a minute; `Permissions`' COS/engine
// agreement check, which needs the engine; and an undo after a save (`adopt`), which is A-32's.

#ifndef RAWPAGE_TESTS_FUZZ_ROUNDTRIPFUZZBODY_H
#define RAWPAGE_TESTS_FUZZ_ROUNDTRIPFUZZBODY_H

#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/RevisionWriter.h"

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawpage::fuzz {

struct RoundtripVerdict {
    bool ok = true;
    std::string failure;       // empty when ok; names the invariant

    bool editable = false;     // the sink opened it and it has a page
    bool written = false;      // a revision was composed and every check passed
    bool complete = false;     // D3-6's complete section was the one written
    std::string refusedKey;    // the writer's key when it refused an editable input (a finding)
    std::size_t pages = 0;

    QByteArray output;         // the composed file, only when `keepOutput` was asked for
};

// Rotate, compose, re-read. Never throws, never reads out of bounds, always terminates.
inline RoundtripVerdict roundtripFuzzOnce(const std::uint8_t* data, std::size_t size,
                                          bool keepOutput = false)
{
    using rawpage::pdf::ops::MutationSinkImpl;
    using rawpage::pdf::ops::PageOps;
    using rawpage::pdf::ops::RevisionWriter;

    RoundtripVerdict verdict;
    auto fail = [&verdict](std::string reason) {
        verdict.ok = false;
        verdict.written = false;
        verdict.failure = std::move(reason);
        return verdict;
    };

    // A null pointer with a length is a caller bug, and libFuzzer never passes one; it is refused
    // here so the view below cannot be one.
    const std::string_view input(data != nullptr ? reinterpret_cast<const char*>(data) : "",
                                 data != nullptr ? size : 0);

    // --- 1. Is it editable? ------------------------------------------------------------------
    auto opened = pdf::cos::openWithRepair(input);
    if (!opened)
        return verdict;
    auto sink = MutationSinkImpl::open(input);
    if (!sink)
        return verdict;
    const int pageCount = sink.value()->pageCount();
    if (pageCount <= 0)
        return verdict;
    verdict.editable = true;
    verdict.pages = static_cast<std::size_t>(pageCount);

    std::vector<int> before;
    before.reserve(static_cast<std::size_t>(pageCount));
    for (const auto& page : sink.value()->pages().pages)
        before.push_back(page.effectiveRotate());

    // --- 2. Rotate page 1 and compose -------------------------------------------------------
    const int target = (before.front() + 90) % 360;
    if (!sink.value()->setPageRotation(0, target))
        return fail("the sink refused a quarter turn of page 1");

    verdict.complete = opened.value().report.requiresSaveAs();
    auto composed = RevisionWriter::compose(*sink.value(), verdict.complete);
    if (!composed) {
        verdict.refusedKey = std::string(composed.error().messageKey.data(),
                                         static_cast<std::size_t>(composed.error().messageKey.size()));
        return fail("the writer refused an input the sink accepted: " + verdict.refusedKey);
    }
    const QByteArray& bytes = composed.value().bytes;
    const std::string_view written(bytes.constData(), static_cast<std::size_t>(bytes.size()));

    // The product's guard is asked FIRST and its answer held, so that a check below which rejects the
    // output can say whether the guard would have let it replace a user's file -- the worse direction.
    const core::Status guarded = RevisionWriter::verify(input, written, *sink.value());
    auto reject = [&fail, &guarded](std::string reason) {
        if (guarded)
            reason += " -- and RevisionWriter::verify ACCEPTED it";
        return fail(std::move(reason));
    };

    // --- 3. The checks, made here ------------------------------------------------------------
    if (written.size() < input.size()
        || (!input.empty() && std::memcmp(written.data(), input.data(), input.size()) != 0))
        return reject("prefix: an input byte changed in the output");

    auto reopened = pdf::cos::openWithRepair(written);
    if (!reopened)
        return reject("reparse: the output does not open");
    if (!reopened.value().usedOriginalXref)
        return reject("own table: the output needed its cross-reference rebuilt");
    if (reopened.value().report.requiresSaveAs())
        return reject("own table: the output's repair report forces a Save-As");

    auto walk = PageOps::walk(reopened.value().table);
    if (!walk)
        return reject("rotation: the output has no page tree");
    const auto& pages = walk.value().pages;
    if (pages.size() != before.size())
        return reject("rotation: the output has " + std::to_string(pages.size()) + " pages, not "
                    + std::to_string(before.size()));
    if (pages.front().effectiveRotate() != target)
        return reject("rotation: page 1 reads " + std::to_string(pages.front().effectiveRotate())
                    + ", not " + std::to_string(target));
    for (std::size_t index = 1; index < pages.size(); ++index) {
        if (pages[index].effectiveRotate() != before[index])
            return reject("rotation: page " + std::to_string(index + 1) + " moved");
    }

    // --- 4. The product's guard must agree ---------------------------------------------------
    if (!guarded)
        return fail("RevisionWriter::verify refused a file every check here accepts: "
                    + guarded.error().context.toStdString());

    verdict.written = true;
    if (keepOutput)
        verdict.output = bytes;
    return verdict;
}

} // namespace rawpage::fuzz

#endif // RAWPAGE_TESTS_FUZZ_ROUNDTRIPFUZZBODY_H
