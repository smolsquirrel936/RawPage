// The body of `fuzz_cos_parser`, in a header so that TWO callers share exactly one copy of it --
// the same arrangement `tests/fuzz/LexerFuzzBody.h` documents at length, and for the same reason:
// libFuzzer needs clang and a Linux runner, so an entry point kept only in the fuzz target rots
// between the day it is written and the day anybody reads its output. Stage 1's I-30 is the
// precedent for what that costs.
//
//   * `tests/fuzz/fuzz_cos_parser.cpp` wraps this in `LLVMFuzzerTestOneInput` for `portable-fuzz`;
//   * `tests/unit/tst_cos_parser.cpp` replays all 60 corpus files through the SAME function on
//     every Windows `ctest` run.
//
// The second is not a substitute for the first. Replaying 60 known files finds nothing new by
// construction (CLAUDE.md convention 19), and guide case A-26's ten clean minutes is a separate
// claim made on a machine this one is not.
//
// WHAT IT CHECKS THAT ASan WOULD NOT. Bounds and use-after-free are the sanitizer's job. Three
// things here are not:
//
//   1. PROGRESS. A parser that stops advancing is a hang, and a hang is the one failure a fuzzer
//      reports badly -- it looks like a slow input rather than a bug.
//   2. THE DEPTH BOUND. `Parser::kMaxDepth` exists so that `corpus/malformed/deep-nesting.pdf`'s
//      4,000 levels cannot overflow the stack. A stack overflow under ASan is reported as a
//      SEGV whose stack trace is 4,000 identical frames, so proving the tree is SHALLOW is worth
//      more than watching for the crash -- and the walk that measures it is iterative, because a
//      recursive one would be the very bug it is looking for.
//   3. SPAN CONTAINMENT. Every object carries the byte range it was parsed from, and Stage 3's
//      incremental writer COPIES those bytes. A span that leaves the buffer is not a crash here;
//      it is someone else's memory in a saved PDF two stages from now.

#ifndef RAWPAGE_TESTS_FUZZ_PARSERFUZZBODY_H
#define RAWPAGE_TESTS_FUZZ_PARSERFUZZBODY_H

#include "pdf/cos/CosObject.h"
#include "pdf/cos/Parser.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rawpage::fuzz {

struct ParserVerdict {
    bool ok = true;
    std::string failure;  // empty when ok; names the invariant and the position
    std::size_t objects = 0;
    std::size_t notes = 0;
    std::size_t maxDepth = 0;
};

namespace detail {

// An ITERATIVE walk. The point of the exercise is that the tree is shallow; recursing to find out
// would reproduce the failure it is testing for.
inline bool checkObjectTree(const rawpage::pdf::cos::CosObject& root, std::size_t size,
                            std::size_t& deepest, std::string& failure)
{
    using rawpage::pdf::cos::CosObject;

    std::vector<std::pair<const CosObject*, std::size_t>> stack;
    stack.emplace_back(&root, std::size_t{0});

    while (!stack.empty()) {
        const auto [object, depth] = stack.back();
        stack.pop_back();

        if (depth > deepest)
            deepest = depth;

        // Two levels of headroom, no more. `kMaxDepth` is the parser's refusal point, and a
        // container built AT the limit still holds the Null that stands in for what was skipped,
        // so the tree can legitimately be one deeper than the limit itself.
        if (depth > static_cast<std::size_t>(rawpage::pdf::cos::Parser::kMaxDepth) + 2) {
            failure = "object nested deeper than kMaxDepth: " + std::to_string(depth);
            return false;
        }

        const auto& span = object->span();
        if (span.valid) {
            // Three comparisons rather than one sum: `offset + length` can wrap, and a wrapped
            // sum compares happily against the size.
            if (span.offset > size || span.length > size || span.length > size - span.offset) {
                failure = "object span leaves the buffer at " + std::to_string(span.offset);
                return false;
            }
        }

        if (object->isStream()) {
            const auto& data = object->streamData();
            if (data.dataOffset > size || data.dataLength > size
                || data.dataLength > size - data.dataOffset) {
                failure = "stream data leaves the buffer at " + std::to_string(data.dataOffset);
                return false;
            }
        }

        for (const CosObject& item : object->items())
            stack.emplace_back(&item, depth + 1);
        for (const auto& entry : object->entries())
            stack.emplace_back(&entry.value, depth + 1);
    }
    return true;
}

} // namespace detail

// Parse `size` bytes every way `pdf/cos/Parser` can be driven, and check every invariant its
// header promises. Never throws, never reads out of bounds, and always terminates.
inline ParserVerdict parserFuzzOnce(const std::uint8_t* data, std::size_t size)
{
    using rawpage::pdf::cos::CosObject;
    using rawpage::pdf::cos::IndirectObject;
    using rawpage::pdf::cos::Parser;

    ParserVerdict verdict;
    auto fail = [&verdict](std::string reason) {
        verdict.ok = false;
        verdict.failure = std::move(reason);
        return verdict;
    };

    // --- 1. Sequential objects, to the end of the buffer -------------------------------------
    {
        Parser parser(data, size);
        while (!parser.atEnd()) {
            const std::size_t before = parser.position();
            const CosObject object = parser.parseObject();
            ++verdict.objects;

            // THE LOAD-BEARING INVARIANT. Everything else here a sanitizer would also find; this
            // is the one whose failure mode is an infinite loop rather than a fault.
            if (parser.position() <= before) {
                if (!parser.atEnd())
                    return fail("no progress at " + std::to_string(before));
                break;
            }

            std::string failure;
            if (!detail::checkObjectTree(object, size, verdict.maxDepth, failure))
                return fail(failure);
        }

        for (const auto& record : parser.notes()) {
            if (record.offset > size)
                return fail(std::string("note offset past the end: ")
                            + rawpage::pdf::cos::parseNoteName(record.note));
        }
        verdict.notes = parser.notes().size();
    }

    // --- 2. As an indirect object at offset 0 -------------------------------------------------
    // A separate drive rather than a separate target: the header check, the `endobj` tolerance and
    // the stream path are only reachable this way, and the interesting inputs for them are the
    // same bytes.
    {
        Parser parser(data, size);
        const core::Result<IndirectObject> result = parser.parseIndirectObjectAt(0);
        if (result) {
            const IndirectObject& object = result.value();
            if (object.span.valid) {
                if (object.span.offset > size || object.span.length > size
                    || object.span.length > size - object.span.offset)
                    return fail("indirect object span leaves the buffer");
            }
            std::string failure;
            std::size_t deepest = 0;
            if (!detail::checkObjectTree(object.object, size, deepest, failure))
                return fail(failure);
            if (deepest > verdict.maxDepth)
                verdict.maxDepth = deepest;
        }
        // A failure is an ordinary outcome -- most inputs are not an object header -- and the
        // parser must have left the position where it found it, so a repair scan can try the next
        // candidate offset. That contract is asserted rather than assumed.
        if (!result && parser.position() != 0)
            return fail("a failed object header moved the position");
    }

    // --- 3. As an object stream payload -------------------------------------------------------
    // `/N` and `/First` come out of the input itself, so the fuzzer can steer them. This is where
    // a hostile `/N` would try to make the pair table reserve something enormous.
    if (size > 0) {
        const long long count = static_cast<long long>(data[0]) * 65536LL;
        const long long first = static_cast<long long>(size / 2);
        std::vector<rawpage::pdf::cos::ParseNoteRecord> notes;
        const auto packed = Parser::parseObjectStream(
            std::string_view(reinterpret_cast<const char*>(data), size), count, first, &notes);
        if (packed) {
            for (const IndirectObject& object : packed.value()) {
                std::string failure;
                std::size_t deepest = 0;
                if (!detail::checkObjectTree(object.object, size, deepest, failure))
                    return fail("in object stream: " + failure);
            }
        }
    }

    return verdict;
}

} // namespace rawpage::fuzz

#endif // RAWPAGE_TESTS_FUZZ_PARSERFUZZBODY_H
