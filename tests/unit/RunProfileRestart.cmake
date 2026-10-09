# Stage 6 T10 -- guide case A-79's restart (claude-plan/Stage6-Annotations-Testing-Guide.md §5.13): the stamp
# library written by ONE process and read back by ANOTHER, RunSettingsRestart.cmake's shape for
# core/settings/ProfileStore -- a category, a favourite signature and a favourite built-in.
#
# Two processes rather than one store loaded twice: what survives a restart is what is on disk, and a second
# store in the same process could be answered by nothing but what the first one wrote moments before -- the
# shape of Stage 4's I-15. Then the CONTROL: an EMPTY root must make the reader fail (convention 7).
#
# Required: -DTEST_BINARY=<rawpage_tests> -DROOT=<a scratch directory in the build tree>

foreach(required TEST_BINARY ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "RunProfileRestart.cmake: ${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${ROOT}")
set(ENV{RAWPAGE_PROFILE_PROBE_ROOT} "${ROOT}")

execute_process(COMMAND "${TEST_BINARY}" "profile store restart probe writes"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the WRITING process failed (${rc}):\n${out}")
endif()
file(GLOB written "${ROOT}/*")
list(LENGTH written count)
if(NOT count EQUAL 5)
    message(FATAL_ERROR "the writing process left ${count} files under ${ROOT}, not 5")
endif()

execute_process(COMMAND "${TEST_BINARY}" "profile store restart probe reads"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the READING process did not read the library back (${rc}):\n${out}")
endif()
message(STATUS "${out}")

# THE CONTROL: the same reader over a root that holds nothing must fail.
file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
execute_process(COMMAND "${TEST_BINARY}" "profile store restart probe reads"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(rc EQUAL 0)
    message(FATAL_ERROR "CONTROL: the reader passed over an EMPTY library -- it is not reading what it claims to")
endif()
message(STATUS "control: the reader refused an empty library, as it must (exit ${rc})")
file(REMOVE_RECURSE "${ROOT}")
