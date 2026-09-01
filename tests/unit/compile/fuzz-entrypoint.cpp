// A compile fixture, not a test binary: it proves `tests/fuzz/fuzz_cos_lexer.cpp` still COMPILES
// on the Windows toolchain, which is the one thing about that file this machine can check.
//
// WHY IT NEEDS CHECKING AT ALL. libFuzzer needs clang and a Linux runner (`portable-fuzz`), so the
// fuzz target is built by a preset that runs on neither this machine nor the Windows CI job. Guide
// case A-25's ten clean minutes is a claim made elsewhere; guide case A-27 exists because "no
// crashes" is passed trivially by a target that builds nothing. This fixture closes the smallest
// and likeliest gap between the two: a fuzz entry point that stopped compiling — a renamed type, a
// changed signature in `pdf/cos/Lexer.h` — and would not be noticed until someone ran the Linux
// job. Stage 1's I-30 is the precedent for how long that can be.
//
// It cannot LINK here (there is no libFuzzer to provide `main`), which is why this is a
// `-fsyntax-only` compile case rather than an executable target.
//
// Including a .cpp is deliberate and is the whole point: the subject is that translation unit.

#include "tests/fuzz/fuzz_cos_lexer.cpp"
