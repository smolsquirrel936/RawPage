# RawPageSerialTests.cmake -- which Catch2 cases ctest must run ALONE, read from the case's own tag.
#
# The x64 test presets run ctest in parallel (`"jobs"` in CMakePresets.json). Measured 2026-10-28 on
# this machine: the release suite took 7 min serially and 60 s at -j 8, every case green. A case that
# GRADES A DURATION -- a p95, a bound in seconds, one piece's time against another's -- is measuring
# the machine, and seven others sharing it are part of what it measures. Those cases run alone.
#
# The mark lives AT THE CASE, as the Catch2 tag `[run-serial]`, rather than as a list of names here,
# because the person writing a timing case is the one who knows it is one, and a list kept in a
# CMakeLists drifts from the sources it names. A case name is not known at configure time -- it is
# discovered from the built binary -- so the mark cannot become a property here either. Instead each
# suite's directory gets one more CTest include file, run by ctest AFTER catch_discover_tests' own
# (TEST_INCLUDE_FILES keeps the order it was appended in): it lists the binary's `[run-serial]` cases
# through Catch2's JSON reporter, the listing CatchAddTests itself parses, and sets RUN_SERIAL on each.
#
# Cost: one listing per suite per ctest invocation, about 0.03 s each on x64-release.
#
# What it does NOT cover: a case that waits on a deadline without grading it (a wait_for of 3 s for a
# worker to start) is not tagged, and a loaded enough machine could still time one out. And an add_test
# that is not a Catch2 case -- the bench.* rows, ui.diagnostics_ignore_the_bus -- sets RUN_SERIAL itself.

include_guard(GLOBAL)

set(_RAWPAGE_SERIAL_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/RawPageSerialTestsInclude.cmake")

# rawpage_serial_tests(<target> PREFIX <ctest prefix> [DL_PATHS <dir>...])
# Call it after catch_discover_tests(<target>), with the same TEST_PREFIX and DL_PATHS: the listing
# launches the binary, and a binary that cannot find its DLLs lists nothing.
function(rawpage_serial_tests target)
    cmake_parse_arguments(arg "" "PREFIX" "DL_PATHS" ${ARGN})
    set(script "${CMAKE_CURRENT_BINARY_DIR}/${target}_run_serial.cmake")
    file(GENERATE OUTPUT "${script}" CONTENT
"set(RAWPAGE_SERIAL_BINARY [==[$<TARGET_FILE:${target}>]==])
set(RAWPAGE_SERIAL_PREFIX [==[${arg_PREFIX}]==])
set(RAWPAGE_SERIAL_DL_PATHS [==[$<TARGET_FILE_DIR:${target}>;${arg_DL_PATHS}]==])
include([==[${_RAWPAGE_SERIAL_INCLUDE}]==])
")
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${script}")
endfunction()
