// fuzz_annot_pipeline -- Stage 6 T2, decision D6-29 (`Stage6-Annotations-Plan.md` §3): bytes through
// the backend's first-party walk, `pdf/ops/AnnotationReader` and `core::AnnotationStore`, under the
// invariants `tests/fuzz/AnnotPipelineFuzzBody.h` states. Linux CI only (`portable-fuzz`, and the
// ten-minute soak); `tests/unit/tst_annotstore.cpp` replays the same body over the corpus on every
// `ctest` run.
//
//     ./bin/fuzz_annot_pipeline fuzz-work/annot corpus/ordinary -max_total_time=60

#include "tests/fuzz/AnnotPipelineFuzzBody.h"

#include <cstdio>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const rawpage::fuzz::AnnotVerdict verdict = rawpage::fuzz::annotPipelineFuzzOnce(data, size);
    if (!verdict.ok) {
        // abort(), as the parser target explains: libFuzzer records an input only if the process dies.
        std::fprintf(stderr, "annotation pipeline invariant violated: %s\n", verdict.failure.c_str());
        std::abort();
    }
    return 0;
}
