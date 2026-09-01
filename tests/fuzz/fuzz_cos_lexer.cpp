// fuzz_cos_lexer -- libFuzzer over `pdf/cos/Lexer`. Stage 2 T5, guide cases A-25 and A-27.
//
// THE FIRST FUZZ TARGET THIS PROJECT HAS EVER HAD. `CMakePresets.json`'s `portable-fuzz` has said
// "NO TARGETS EXIST until Stage 2" since Stage 0, and the root CMakeLists printed the same thing at
// configure time -- deliberately, because a fuzz preset that silently builds nothing is
// indistinguishable from one that is broken. Both of those messages change in the same commit as
// this file.
//
// SEEDING. `corpus/malformed/` (Stage 2 T4) is the seed corpus, and it is a good one for a reason
// worth stating: every file in it is a VALID document with exactly one named fault, so the fuzzer
// starts from inputs that already reach deep into the lexer rather than from noise that dies at the
// first byte. Run it as:
//
//     ./bin/fuzz_cos_lexer corpus/malformed -max_total_time=600
//
// WHAT COUNTS AS A FINDING. Any crash, any leak, any timeout -- and, because the lexer's contract
// includes making progress, an invariant failure reported by `lexerFuzzOnce`. A finding here is
// reproduced in `x64-debug` on Windows before it counts as fixed (CLAUDE.md convention 11's
// corollary); `tests/unit/tst_cos_lexer.cpp` calls the same function, so reproducing one is adding
// the offending bytes to that file's table.

#include "tests/fuzz/LexerFuzzBody.h"

#include <cstdio>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const rawpage::fuzz::LexerVerdict verdict = rawpage::fuzz::lexerFuzzOnce(data, size);
    if (!verdict.ok) {
        // abort() rather than a return: libFuzzer only records an input as a crash if the process
        // dies on it. Returning non-zero would print nothing, save nothing, and let the run
        // continue past the very input we want written to disk.
        std::fprintf(stderr, "lexer invariant violated: %s\n", verdict.failure.c_str());
        std::abort();
    }
    return 0;
}
