# tests/fuzz -- libFuzzer targets over the first-party COS reader (Stage 2 T5 and T6) and, from
# Stage 3 T13, over the reader and the writer together (fuzz_cos_roundtrip, D3-17).
#
# ONLY BUILT BY `portable-fuzz`. MinGW GCC has no libFuzzer, so these cannot be Windows targets;
# the root CMakeLists guards the add_subdirectory on RAWPAGE_BUILD_FUZZERS, which only that preset
# sets. See Tech-Stack-Plan.md 6.3 and risk R7 for why the sanitizer and fuzz gates live on Linux
# at all, and CLAUDE.md's convention 11 corollary for what a finding here obliges: reproduce it in
# x64-debug before calling it fixed.
#
# WHAT KEEPS THESE FROM ROTTING between Linux runs. The invariants are in tests/fuzz/*FuzzBody.h,
# and tests/unit replays corpus/malformed/ through the same functions on every Windows ctest run --
# and, for the round trip (RoundtripFuzzBody.h), tests/roundtrip/tst_roundtrip.cpp replays every corpus
# file, and tests/fuzz/seeds/ builds the seed writer on every preset.
# That is not fuzzing and does not pretend to be -- it is what makes a target that this machine
# cannot build still compile-checked and still reproducible here.
#
# A NOTE ON THE FLAGS. The preset passes -fsanitize=fuzzer-no-link to CMAKE_CXX_FLAGS, so every
# translation unit in the build gets coverage instrumentation (which is what makes the LIBRARY
# code -- pdf/cos/ -- discoverable by the fuzzer) while only these targets link the driver. That is
# why `fuzzer` appears below on the link line and `fuzzer-no-link` in the preset, and getting the
# two the wrong way round produces a link error in every unrelated executable.

function(rawpage_add_fuzz_target name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}")
    target_link_libraries(${name} PRIVATE ${ARGN} rawpage_cos rawpage_core)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    # No rawpage_strict(): these are drivers, not product code, and the strict list turns the
    # sanitizer's own warnings into errors on some clang versions. The LIBRARY under test is strict.
endfunction()

rawpage_add_fuzz_target(fuzz_cos_lexer fuzz_cos_lexer.cpp)
rawpage_add_fuzz_target(fuzz_cos_parser fuzz_cos_parser.cpp)   # T6, and the PRIMARY target (A-26)

# Stage 3 T13, decision D3-17: the reader and the writer against each other. It links rawpage_ops --
# the portable mutation library (D3-3) -- which is the reason that library builds on Linux at all.
rawpage_add_fuzz_target(fuzz_cos_roundtrip fuzz_cos_roundtrip.cpp rawpage_ops)

# Guide case A-27 (Stage 2) and A-83(a) (Stage 3): "Expect exactly N fuzz targets. ZERO IS A FAILURE,
# NOT A PASS." That case exists because A-25 and A-26 -- "no crashes in ten minutes" -- are passed
# trivially by a target that builds nothing. The count is asserted here, at configure time, so the
# build graph cannot quietly become empty between now and the Linux run.
#
# It was 1 at Stage 2 T5, 2 from T6, and 3 from Stage 3 T13 with fuzz_cos_roundtrip. The next task to
# add or remove a target changes this number in the same commit. The assertion itself is
# cmake/RawPageFuzzTargets.cmake, where the gates.fuzz_count.* cases can prove it fires.
set(RAWPAGE_EXPECTED_FUZZ_TARGETS 3)
include("${CMAKE_SOURCE_DIR}/cmake/RawPageFuzzTargets.cmake")
rawpage_assert_fuzz_target_count(${RAWPAGE_EXPECTED_FUZZ_TARGETS})
