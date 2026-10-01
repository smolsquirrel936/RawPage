# Stage 5 T6 -- guide case A-68's restart (claude-plan/Stage5-TextSearch-Testing-Guide.md §5.8): the ten
# `search/*` values written by ONE process and read back by ANOTHER.
#
# Why two processes and not a reload: Stage 4's I-15 was a preference that survived every in-process reload
# and no real restart, because QSettings answers a reload from the cache of what the same process wrote. So
# this runs rawpage_tests twice -- the hidden case "search settings restart probe writes", then "... reads"
# -- with the file named by RAWPAGE_SETTINGS_PROBE_INI, and then the CONTROL: an EMPTY file, every key at its
# default, must make the reader fail (convention 7 -- a reader that passed on defaults would prove nothing,
# because every value written is off its default).
#
# Required: -DTEST_BINARY=<rawpage_tests> -DINI=<a scratch path in the build tree>

foreach(required TEST_BINARY INI)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "RunSettingsRestart.cmake: ${required} is required")
    endif()
endforeach()

get_filename_component(ini_dir "${INI}" DIRECTORY)
file(MAKE_DIRECTORY "${ini_dir}")
file(REMOVE "${INI}")
set(ENV{RAWPAGE_SETTINGS_PROBE_INI} "${INI}")

execute_process(COMMAND "${TEST_BINARY}" "search settings restart probe writes"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the WRITING process failed (${rc}):\n${out}")
endif()
if(NOT EXISTS "${INI}")
    message(FATAL_ERROR "the writing process left no settings file at ${INI}")
endif()

execute_process(COMMAND "${TEST_BINARY}" "search settings restart probe reads"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the READING process did not read the values back (${rc}):\n${out}")
endif()
message(STATUS "${out}")

# THE CONTROL: the same reader over a file that holds nothing must fail.
file(WRITE "${INI}" "")
execute_process(COMMAND "${TEST_BINARY}" "search settings restart probe reads"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(rc EQUAL 0)
    message(FATAL_ERROR "CONTROL: the reader passed over an EMPTY settings file -- it is not reading what it claims to")
endif()
message(STATUS "control: the reader refused an empty file, as it must (exit ${rc})")
file(REMOVE "${INI}")
