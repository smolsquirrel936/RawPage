// fuzz_cos_parser -- libFuzzer over `pdf/cos/Parser`. Stage 2 T6, guide cases A-26 and A-27.
//
// THE PRIMARY TARGET, and the guide says so. `fuzz_cos_lexer` (T5) covers a tokenizer that cannot
// fail by construction; this one covers the layer that recurses, that allocates containers sized
// from the input, and that trusts a `/Length` written by whoever produced the file. Everything a
// COS reader has historically been exploited through is on this side of the boundary.
//
// SEEDING. `corpus/malformed/` (Stage 2 T4) again, and it is a better corpus for this target than
// for the lexer: every file is a VALID document with exactly one named fault, so the fuzzer starts
// from inputs that already reach a stream, a dictionary and an object header rather than from
// noise that dies at the first byte. Run it as:
//
//     ./bin/fuzz_cos_parser corpus/malformed -max_total_time=600
//
// WHAT COUNTS AS A FINDING. Any crash, any leak, any timeout -- and any invariant failure
// reported by `parserFuzzOnce`, of which the two that matter most are PROGRESS (a parser that
// stops advancing is a hang) and SPAN CONTAINMENT (a span that leaves the buffer becomes someone
// else's memory inside a saved PDF once Stage 3's incremental writer starts copying those bytes).
//
// A finding here is reproduced in `x64-debug` on Windows before it counts as fixed (CLAUDE.md
// convention 11's corollary); `tests/unit/tst_cos_parser.cpp` calls the same function, so
// reproducing one is adding the offending bytes to that file's table.

#include "tests/fuzz/ParserFuzzBody.h"

#include <cstdio>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const rawpage::fuzz::ParserVerdict verdict = rawpage::fuzz::parserFuzzOnce(data, size);
    if (!verdict.ok) {
        // abort() rather than a return: libFuzzer only records an input as a crash if the process
        // dies on it. Returning non-zero would print nothing, save nothing, and let the run
        // continue past the very input we want written to disk.
        std::fprintf(stderr, "parser invariant violated: %s\n", verdict.failure.c_str());
        std::abort();
    }
    return 0;
}
