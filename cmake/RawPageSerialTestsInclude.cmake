# Run by CTEST, not by cmake: included from a suite's generated <target>_run_serial.cmake while ctest
# reads the test list, after catch_discover_tests has added that suite's cases. Sets RUN_SERIAL on every
# case the binary tags `[run-serial]`. Why, and what it does not cover: RawPageSerialTests.cmake.
#
# Inputs: RAWPAGE_SERIAL_BINARY, RAWPAGE_SERIAL_PREFIX, RAWPAGE_SERIAL_DL_PATHS.

# Not built yet: catch_discover_tests has added a NOT_BUILT placeholder and there is nothing to list.
if(NOT EXISTS "${RAWPAGE_SERIAL_BINARY}")
    return()
endif()

# The binary needs libstdc++ and, for some suites, pdfium.dll -- the same directories its cases are
# launched with. PATH is put back afterwards, because ctest's own environment is what every case inherits.
if(CMAKE_HOST_WIN32)
    set(_rawpage_sep ";")
else()
    set(_rawpage_sep ":")
endif()
set(_rawpage_saved_path "$ENV{PATH}")
string(REPLACE ";" "${_rawpage_sep}" _rawpage_dirs "${RAWPAGE_SERIAL_DL_PATHS}")
set(ENV{PATH} "${_rawpage_dirs}${_rawpage_sep}$ENV{PATH}")
execute_process(
    COMMAND "${RAWPAGE_SERIAL_BINARY}" "[run-serial]" --list-tests --reporter json
    OUTPUT_VARIABLE _rawpage_json
    RESULT_VARIABLE _rawpage_rc
    ERROR_QUIET)
set(ENV{PATH} "${_rawpage_saved_path}")

# A listing that failed must say so: silently running a timing case in parallel is the flake this exists
# to prevent, and it would look like a slow machine rather than a missing mark.
string(JSON _rawpage_count ERROR_VARIABLE _rawpage_error LENGTH "${_rawpage_json}" listings tests)
if(NOT _rawpage_rc EQUAL 0 OR _rawpage_error)
    message(WARNING "[run-serial] listing of ${RAWPAGE_SERIAL_BINARY} failed (exit ${_rawpage_rc}"
                    " ${_rawpage_error}); its timing cases will run in parallel")
    return()
endif()

if(_rawpage_count GREATER 0)
    math(EXPR _rawpage_last "${_rawpage_count} - 1")
    foreach(_rawpage_i RANGE ${_rawpage_last})
        string(JSON _rawpage_name GET "${_rawpage_json}" listings tests ${_rawpage_i} name)
        set_tests_properties("${RAWPAGE_SERIAL_PREFIX}${_rawpage_name}" PROPERTIES RUN_SERIAL TRUE)
    endforeach()
endif()
