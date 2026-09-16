// A compile fixture, not a test binary: it proves `tests/fuzz/fuzz_cos_roundtrip.cpp` still COMPILES on
// the Windows toolchain. Stage 3 T13. `fuzz-entrypoint.cpp`, the lexer's twin, carries the full argument.
//
// It cannot LINK here (there is no libFuzzer to provide `main`), which is why this is a `-fsyntax-only`
// compile case rather than an executable target. The body itself also runs on Windows, through
// `tests/roundtrip/tst_roundtrip.cpp`'s replay and `rawpage_fuzz_seeds`; this case is about the entry
// point's translation unit.
//
// Including a .cpp is deliberate and is the whole point: the subject is that translation unit.

#include "tests/fuzz/fuzz_cos_roundtrip.cpp"
