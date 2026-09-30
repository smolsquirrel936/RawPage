// fuzz_text_pipeline -- libFuzzer over `core/text/`: bytes -> a synthetic PageTextModel ->
// LayoutAnalyzer -> TextNormalizer, with their invariants checked. Stage 5 T4, decision D5-21, guide
// cases A-40 and A-41. The body and its reasoning are `tests/fuzz/TextPipelineFuzzBody.h`.
//
// It needs no PDF and no engine: `core/` is portable by rule, so this builds under `portable-fuzz`
// like the COS targets. Seeded, when run by CI, with `corpus/labels/` -- JSON the decoder reads as
// geometry records like any other bytes, which gives the fuzzer varied starting points rather than
// structure.
//
//     ./bin/fuzz_text_pipeline corpus/labels -max_total_time=600
//
// A finding is reproduced in `x64-debug` by adding its bytes to the replay table in
// `tests/unit/tst_layout.cpp`, which calls the same function.

#include "tests/fuzz/TextPipelineFuzzBody.h"

#include <cstdio>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const rawpage::fuzz::TextPipelineVerdict verdict = rawpage::fuzz::textPipelineFuzzOnce(data, size);
    if (!verdict.ok) {
        // abort(), for `fuzz_cos_lexer.cpp`'s reason: libFuzzer records an input only when the
        // process dies on it.
        std::fprintf(stderr, "text pipeline invariant violated: %s\n", verdict.failure.c_str());
        std::abort();
    }
    return 0;
}
