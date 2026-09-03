// A compile fixture, not a test binary: it proves `tests/fuzz/fuzz_cos_parser.cpp` still COMPILES
// on the Windows toolchain, which is the one thing about that file this machine can check. The
// lexer's twin, `fuzz-entrypoint.cpp`, carries the full argument; the short version is that guide
// case A-26's ten clean minutes happen on a Linux runner with clang, so nothing here and nothing
// in the Windows CI job would otherwise notice this entry point breaking.
//
// It cannot LINK here (there is no libFuzzer to provide `main`), which is why this is a
// `-fsyntax-only` compile case rather than an executable target.
//
// Including a .cpp is deliberate and is the whole point: the subject is that translation unit.

#include "tests/fuzz/fuzz_cos_parser.cpp"
