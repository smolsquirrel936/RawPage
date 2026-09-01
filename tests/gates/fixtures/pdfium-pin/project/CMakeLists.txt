# The fixture for cmake/PdfiumAssert.cmake -- PA-1 .. PA-5, guide case A-05 (Stage 2 T2, D2-3).
#
# WHAT IT PROVES. That the version pin fails configure on a mismatch, names BOTH versions when it
# does, refuses a V8-enabled archive, and still passes on a correct tree. The last of those is not a
# formality: an assertion that rejects everything would satisfy every other case here and make the
# project unbuildable.
#
# IT INCLUDES THE REAL FILE rather than a copy, for the reason tests/gates/fixtures/headless-assert/
# gives and Stage 0's I-15 taught: a fixture carrying its own copy proves the copy fires.
#
# LANGUAGES NONE, so it runs on the bare `gates` runner with no Qt, no vcpkg and no compiler -- and,
# more to the point, WITH NO PDFium FETCHED. The trees it points at are four-line text files
# committed beside it, which is what lets the pin be tested on a machine that has no engine at all.
# That is worth stating plainly, because it is the difference between testing the assertion and
# testing this developer's third_party/ directory.

cmake_minimum_required(VERSION 3.25)
project(PdfiumPinFixture LANGUAGES NONE)

if(NOT DEFINED RAWPAGE_SOURCE_DIR OR NOT DEFINED FIXTURE_TREE)
    message(FATAL_ERROR "RAWPAGE_SOURCE_DIR and FIXTURE_TREE must be set")
endif()

include("${RAWPAGE_SOURCE_DIR}/cmake/PdfiumAssert.cmake")

rawpage_assert_pdfium("${FIXTURE_TREE}")

message(STATUS "pdfium-pin fixture: accepted ${RAWPAGE_PDFIUM_VERSION}")
