# The fixture for rawpage_assert_fuzz_target_count() -- Stage 3 guide case A-83(a)
# (`Stage3-Mutation-Testing-Guide.md` §5.12), and Stage 2 guide case A-27's assertion proven to FIRE.
#
# From Stage 2 T5 to Stage 3 T13 the assertion lived inline in tests/fuzz/CMakeLists.txt, which only the
# `portable-fuzz` preset reads, so nothing had ever shown it refuses a wrong count. This fixture INCLUDES
# cmake/RawPageFuzzTargets.cmake -- the file tests/fuzz includes, not a copy (convention 7).
#
# LANGUAGES NONE and custom targets, because the count is of build-system targets and needs no compiler:
# it runs on the bare Linux gates runner and on Windows alike.
#
#   FIXTURE_TARGETS   how many targets this directory declares
#   FIXTURE_EXPECTED  what the assertion is told to expect
#   FIXTURE_HELPER    ON adds a subdirectory with a target of its own, which must NOT be counted

cmake_minimum_required(VERSION 3.25)
project(FuzzCountFixture LANGUAGES NONE)

if(NOT DEFINED RAWPAGE_SOURCE_DIR)
    message(FATAL_ERROR "RAWPAGE_SOURCE_DIR must be set -- this fixture includes the REAL assertion")
endif()

include("${RAWPAGE_SOURCE_DIR}/cmake/RawPageFuzzTargets.cmake")

if(FIXTURE_TARGETS GREATER 0)
    foreach(index RANGE 1 ${FIXTURE_TARGETS})
        add_custom_target(fixture_fuzz_${index})
    endforeach()
endif()

if(FIXTURE_HELPER)
    add_subdirectory(helper)
endif()

rawpage_assert_fuzz_target_count(${FIXTURE_EXPECTED})
