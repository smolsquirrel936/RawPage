// fuzz_cos_roundtrip -- libFuzzer over the reader AND the writer together. Stage 3 T13, decision D3-17
// (`Stage3-Mutation-Plan.md` §3), guide case A-83 (`Stage3-Mutation-Testing-Guide.md` §5.12).
//
// THE THIRD TARGET, and the first that tests two components against each other: whatever
// `pdf/cos/openWithRepair` and `pdf/ops/MutationSinkImpl` accept, `pdf/ops/RevisionWriter` must be able
// to append a rotation to, and the result must read back through its own table with the rotation in
// it and every input byte unchanged in front of it. `tests/fuzz/RoundtripFuzzBody.h` holds the checks
// and says what they leave out.
//
// SEEDING. Three directories after the scratch one, because an input that does not reach an editable
// page tree exercises only the reader, which the other two targets already cover:
//
//     ./bin/rawpage_fuzz_seeds seeds/roundtrip corpus/ordinary corpus/signed corpus/malformed
//     ./bin/fuzz_cos_roundtrip work/ seeds/roundtrip corpus/malformed corpus/ordinary
//         corpus/signed -max_total_time=600
//
// `seeds/roundtrip` is the writer's own outputs (D3-17's "seeds gain the writer's own outputs"): files
// that already carry an update section, a `/Prev` chain or a complete section written by this code,
// which no generator in `corpus/` produces.
//
// WHAT COUNTS AS A FINDING. Any crash, leak or timeout, and any failure `roundtripFuzzOnce` reports --
// including the writer refusing, by key, an input the sink accepted. A finding is reproduced in
// `x64-debug` on Windows before it counts as fixed; `tests/roundtrip/tst_roundtrip.cpp` calls the same
// function, so reproducing one is adding the offending bytes to a case there.

#include "tests/fuzz/RoundtripFuzzBody.h"

#include <cstdio>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const rawpage::fuzz::RoundtripVerdict verdict = rawpage::fuzz::roundtripFuzzOnce(data, size);
    if (!verdict.ok) {
        // abort(), as the parser target explains: libFuzzer records an input only if the process dies.
        std::fprintf(stderr, "roundtrip invariant violated: %s\n", verdict.failure.c_str());
        std::abort();
    }
    return 0;
}
