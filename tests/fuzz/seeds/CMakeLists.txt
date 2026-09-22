# tests/fuzz/seeds -- rawpage_fuzz_seeds, the writer's own outputs as seeds for fuzz_cos_roundtrip.
# Stage 3 T13, decision D3-17. See RoundtripSeeds.cpp's header.
#
# Added from the root CMakeLists on EVERY preset but `gates` -- not from tests/fuzz/CMakeLists.txt,
# which only portable-fuzz reads -- so the tool and its CTest case run on Windows and under
# portable-asan, and a broken seed writer is found before the Linux fuzz job needs it. And a
# subdirectory, because cmake/RawPageFuzzTargets.cmake counts tests/fuzz's own targets only: this is
# a helper, and counting it would let a missing fuzz target hide behind it.

add_executable(rawpage_fuzz_seeds RoundtripSeeds.cpp)
target_include_directories(rawpage_fuzz_seeds PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(rawpage_fuzz_seeds PRIVATE rawpage_ops rawpage_cos rawpage_core)

# S4 T12 -- AND IT WAS MISSING FROM T13 UNTIL THIS TASK. `rawpage_common_flags` is what sets
# CXX_STANDARD 20; without it this target compiled at the compiler's DEFAULT standard (gnu++17 for
# GCC 13) while every other target in the tree was C++20. It built anyway because the headers it
# happened to include used no C++20 feature that needs the flag -- and it stopped building the day
# `core/backend.h` gained `core/Geometry.h`, whose `operator==() = default` does.
#
# So the gap was real for as long as it was invisible: this target was not compiling the language
# the rest of the product is written in, and the next header it pulled in was going to find out.
# Recorded as **I-08** in `Stage4-ViewingSurface-Testing-Guide.md` §8.
rawpage_common_flags(rawpage_fuzz_seeds)

# Guide case A-83's seed half: the tool writes at least one seed from the corpus and meets no finding.
# The verdict is the EXIT CODE -- no PASS_REGULAR_EXPRESSION, which would make CTest ignore it
# (tests/gates/RunGateCase.cmake records the trap). `unit.` because it links what rawpage_tests may.
add_test(NAME unit.fuzz.roundtrip_seeds
    COMMAND rawpage_fuzz_seeds
            "${CMAKE_BINARY_DIR}/fuzz-seeds/roundtrip"
            "${CMAKE_SOURCE_DIR}/corpus/ordinary"
            "${CMAKE_SOURCE_DIR}/corpus/signed"
            "${CMAKE_SOURCE_DIR}/corpus/malformed")
if(WIN32)
    set_tests_properties(unit.fuzz.roundtrip_seeds PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin")
endif()
