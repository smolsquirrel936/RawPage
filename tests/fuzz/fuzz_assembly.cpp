// fuzz_assembly -- Stage 7 T5, decision D7-23 (`Stage7-Assembly-Plan.md` §3): bytes through the page walk,
// `pdf/ops/ObjectGraphCopier` and the sink's insert and replace, under the invariants `tests/fuzz/AssemblyFuzzBody.h`
// states. Linux CI only (`portable-fuzz`, and the ten-minute soak); `tests/unit/tst_copier.cpp` replays the same body
// over the corpus on every `ctest` run.
//
//     ./bin/fuzz_assembly fuzz-work/assembly corpus/assembly -max_total_time=60

#include "tests/fuzz/AssemblyFuzzBody.h"

#include <cstdio>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const rawpage::fuzz::AssemblyVerdict verdict = rawpage::fuzz::assemblyFuzzOnce(data, size);
    if (!verdict.ok) {
        // abort(), as the parser target explains: libFuzzer records an input only if the process dies.
        std::fprintf(stderr, "assembly invariant violated: %s\n", verdict.failure.c_str());
        std::abort();
    }
    return 0;
}
