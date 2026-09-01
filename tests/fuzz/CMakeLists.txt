# tests/fuzz -- libFuzzer targets over the first-party COS reader. Stage 2 T5 and T6.
#
# ONLY BUILT BY `portable-fuzz`. MinGW GCC has no libFuzzer, so these cannot be Windows targets;
# the root CMakeLists guards the add_subdirectory on RAWPAGE_BUILD_FUZZERS, which only that preset
# sets. See Tech-Stack-Plan.md 6.3 and risk R7 for why the sanitizer and fuzz gates live on Linux
# at all, and CLAUDE.md's convention 11 corollary for what a finding here obliges: reproduce it in
# x64-debug before calling it fixed.
#
# WHAT KEEPS THESE FROM ROTTING between Linux runs. The invariants are in tests/fuzz/*FuzzBody.h,
# and tests/unit replays corpus/malformed/ through the same functions on every Windows ctest run.
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
    target_link_libraries(${name} PRIVATE rawpage_cos rawpage_core)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    # No rawpage_strict(): these are drivers, not product code, and the strict list turns the
    # sanitizer's own warnings into errors on some clang versions. The LIBRARY under test is strict.
endfunction()

rawpage_add_fuzz_target(fuzz_cos_lexer fuzz_cos_lexer.cpp)

# Guide case A-27: "Enumerate the portable-fuzz build graph. Expect exactly two fuzz targets. ZERO
# IS A FAILURE, NOT A PASS." That case exists because A-25 and A-26 -- "no crashes in ten minutes"
# -- are passed trivially by a target that builds nothing. The count is asserted here, at configure
# time, so the build graph cannot quietly become empty between now and the Linux run.
#
# It is 1 at T5 and becomes 2 at T6 with fuzz_cos_parser. Bumping this number is part of T6.
set(RAWPAGE_EXPECTED_FUZZ_TARGETS 1)
get_property(_fuzz_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
list(LENGTH _fuzz_targets _fuzz_target_count)
if(NOT _fuzz_target_count EQUAL RAWPAGE_EXPECTED_FUZZ_TARGETS)
    message(FATAL_ERROR
        "\ntests/fuzz declares ${_fuzz_target_count} target(s); ${RAWPAGE_EXPECTED_FUZZ_TARGETS} "
        "expected.\n\n"
        "Guide case A-27 (Stage2-Rendering-Testing-Guide.md 5.4) says zero fuzz targets is a "
        "FAILURE rather than a pass, because \"ten minutes with no crashes\" is satisfied by a "
        "target that does not exist. If a target was added or removed on purpose, change "
        "RAWPAGE_EXPECTED_FUZZ_TARGETS in this file in the same commit.\n")
endif()

message(STATUS "  fuzz targets    ${_fuzz_targets}")
