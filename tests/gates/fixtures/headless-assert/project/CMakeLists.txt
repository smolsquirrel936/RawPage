# The fixture for rawpage_assert_headless() -- guide case A-03 (Stage2-Rendering-Testing-Guide.md).
#
# WHAT IT PROVES. That the assertion protecting the headless suite actually fires when a forbidden
# library enters the closure, and does NOT fire when one does not. Both halves matter: an assertion
# that rejects everything passes the first case and is useless.
#
# WHY IT INCLUDES THE REAL FUNCTION rather than a copy. Convention 7 -- a control that does not
# exercise the thing is worse than none, and Stage 0's I-15 is the same lesson. cmake/
# RawPageHeadless.cmake is the file tests/unit/CMakeLists.txt includes, and it is the file below
# includes; if the two ever diverge, this fixture stops testing the product.
#
# LANGUAGES NONE, because the assertion is pure CMake. This fixture must configure on the bare
# `gates` runner, which has no Qt, no vcpkg and no pinned compiler -- the same constraint every
# other gate case is under.

cmake_minimum_required(VERSION 3.25)
project(HeadlessAssertFixture LANGUAGES NONE)

if(NOT DEFINED RAWPAGE_SOURCE_DIR)
    message(FATAL_ERROR "RAWPAGE_SOURCE_DIR must be set -- this fixture includes the REAL assertion")
endif()

include("${RAWPAGE_SOURCE_DIR}/cmake/RawPageHeadless.cmake")

# Stand-ins for the real libraries. INTERFACE targets need no compiler and no sources.
#
# THEY CARRY THEIR DEPENDENCIES IN A DIFFERENT PROPERTY, and finding that out is what these cases
# were worth on their first run: an INTERFACE library records edges in INTERFACE_LINK_LIBRARIES and
# leaves LINK_LIBRARIES unset, so the assertion -- which read only the latter -- walked one step,
# found nothing, and reported a clean closure. That was a real hole rather than a fixture artifact:
# an IMPORTED target does the same, Qt6::Core included, so the walk would have stopped dead at the
# first edge of the wrong kind. The function now reads both. The STATIC-library branch is exercised
# by the real tests/unit call at every configure; these cases exercise the other one.
add_library(rawpage_core INTERFACE)
add_library(rawpage_cos INTERFACE)
add_library(rawpage_ui INTERFACE)
add_library(rawpage_platform INTERFACE)
target_link_libraries(rawpage_cos INTERFACE rawpage_core)

# rawpage_pdfium is deliberately NOT defined by default. On the portable presets it does not exist
# either, and the assertion has to reject the NAME rather than a resolved target -- otherwise it is
# silent on exactly the configuration whose portability it defends. FIXTURE_DEFINE_PDFIUM turns it
# into a real target so the other half is covered too.
if(FIXTURE_DEFINE_PDFIUM)
    add_library(rawpage_pdfium INTERFACE)
endif()

# rawpage_ops (Stage 3 T3, decision D3-3): the portable mutation layer, which rawpage_tests links for
# real. It links rawpage_cos and must never carry an engine in with it.
add_library(rawpage_ops INTERFACE)
target_link_libraries(rawpage_ops INTERFACE rawpage_core rawpage_cos)

add_library(rawpage_tests INTERFACE)
target_link_libraries(rawpage_tests INTERFACE rawpage_core rawpage_cos)

# FIXTURE_LEAK names the library the suite is made to link, directly or through rawpage_cos.
if(FIXTURE_LEAK STREQUAL "ops")
    # The real Stage 3 shape: allowed, and the case that proves the assertion did not start refusing
    # the new edge along with the forbidden ones.
    target_link_libraries(rawpage_tests INTERFACE rawpage_ops)
elseif(FIXTURE_LEAK STREQUAL "ops_pdfium")
    # The leak D3-3 exists to prevent, arriving the way it would: rawpage_ops grows an engine edge
    # (say, because pdf/ops/ drifted toward DocumentBackendImpl) and nobody touches tests/unit.
    target_link_libraries(rawpage_tests INTERFACE rawpage_ops)
    target_link_libraries(rawpage_ops INTERFACE rawpage_pdfium)
elseif(FIXTURE_LEAK STREQUAL "direct")
    target_link_libraries(rawpage_tests INTERFACE rawpage_pdfium)
elseif(FIXTURE_LEAK STREQUAL "transitive")
    # The way this actually happens: nobody edits the test's own CMakeLists at all.
    target_link_libraries(rawpage_cos INTERFACE rawpage_pdfium)
elseif(FIXTURE_LEAK STREQUAL "ui")
    target_link_libraries(rawpage_tests INTERFACE rawpage_ui)
endif()

rawpage_assert_headless(rawpage_tests)

message(STATUS "headless-assert fixture: closure is clean")
