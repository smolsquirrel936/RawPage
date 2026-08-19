# Runs one gate against one fixture and checks BOTH the exit code and the diagnostic.
#
# Invoked via `cmake -P`, so it needs no shell and behaves identically on Windows and the
# Linux CI runner.
#
# WHY NOT CTest's WILL_FAIL: that property passes on ANY non-zero exit, so a gate that
# crashed (exit 2) would be indistinguishable from a gate that correctly found a violation
# (exit 1). The whole point of separating those codes is lost if the test harness merges
# them again. And why not PASS_REGULAR_EXPRESSION alone: it ignores the exit code entirely,
# so a tool that printed the right words and then returned 0 would pass.
#
# Required: -DCOMMAND_STR=... -DEXPECT_EXIT=...
# Optional: -DEXPECT_MATCH=<regex>   the diagnostic must mention this
#           -DEXPECT_FILE=<path>     the gate must have written this file
#           -DEXPECT_ABSENT=<path>   the gate must NOT have written this file
#           -DEXPECT_FILE_MATCH=<patterns>  every pattern must match some LINE of EXPECT_FILE,
#                                    several joined by @@. Line-scoped on purpose: a pattern
#                                    checked against the whole file lets '.' cross newlines and
#                                    match halves of two different rows.
#
# EXPECT_FILE / EXPECT_ABSENT exist because a gate that only PRINTS what it is about to do
# has not been tested. Both paths are removed before the run, so the result is the gate's
# behaviour and not a leftover from a previous one.

if(NOT DEFINED COMMAND_STR OR NOT DEFINED EXPECT_EXIT)
    message(FATAL_ERROR "RunGateCase.cmake: COMMAND_STR and EXPECT_EXIT are required")
endif()

# Arguments are joined with '|' rather than spaces by the caller. A toolchain case passes
# things like  -DCMAKE_CXX_FLAGS=-U__GNUC_MINOR__ -D__GNUC_MINOR__=9  as ONE argument, and
# splitting on whitespace would tear it in half -- producing a test that fails for a reason
# unrelated to what it claims to check.
string(REPLACE "|" ";" command_list "${COMMAND_STR}")

foreach(path IN ITEMS "${EXPECT_FILE}" "${EXPECT_ABSENT}")
    if(NOT path STREQUAL "")
        file(REMOVE "${path}")
    endif()
endforeach()

execute_process(
    COMMAND ${command_list}
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE stdout_text
    ERROR_VARIABLE stderr_text
)

set(combined "${stdout_text}${stderr_text}")

if(NOT actual_exit EQUAL EXPECT_EXIT)
    message(FATAL_ERROR
        "expected exit ${EXPECT_EXIT}, got ${actual_exit}\n"
        "--- command ---\n${COMMAND_STR}\n"
        "--- output ---\n${combined}")
endif()

if(DEFINED EXPECT_MATCH AND NOT EXPECT_MATCH STREQUAL "")
    if(NOT combined MATCHES "${EXPECT_MATCH}")
        message(FATAL_ERROR
            "exit code ${actual_exit} was correct, but the diagnostic did not mention: ${EXPECT_MATCH}\n"
            "A gate that fails for the wrong reason has not been tested.\n"
            "--- output ---\n${combined}")
    endif()
endif()

if(DEFINED EXPECT_ABSENT AND NOT EXPECT_ABSENT STREQUAL "")
    if(EXISTS "${EXPECT_ABSENT}")
        message(FATAL_ERROR
            "the gate wrote ${EXPECT_ABSENT} after saying it would leave it alone.
"
            "A message without the behaviour behind it is not the behaviour.
"
            "--- output ---
${combined}")
    endif()
endif()

if(DEFINED EXPECT_FILE AND NOT EXPECT_FILE STREQUAL "")
    if(NOT EXISTS "${EXPECT_FILE}")
        message(FATAL_ERROR
            "the gate did not write ${EXPECT_FILE}.
"
            "--- output ---
${combined}")
    endif()
endif()

# Content assertions on the produced file. Rendered output had no coverage at all until
# I-17: the NOTICE column and its legend are what an outside auditor reads, and nothing
# checked either of them.
if(DEFINED EXPECT_FILE_MATCH AND NOT EXPECT_FILE_MATCH STREQUAL "")
    if(NOT EXISTS "${EXPECT_FILE}")
        message(FATAL_ERROR "EXPECT_FILE_MATCH needs EXPECT_FILE, and ${EXPECT_FILE} is not there.")
    endif()
    file(STRINGS "${EXPECT_FILE}" produced_lines)
    string(REPLACE "@@" ";" wanted_patterns "${EXPECT_FILE_MATCH}")

    foreach(pattern IN LISTS wanted_patterns)
        set(found FALSE)
        foreach(line IN LISTS produced_lines)
            if(line MATCHES "${pattern}")
                set(found TRUE)
                break()
            endif()
        endforeach()
        if(NOT found)
            message(FATAL_ERROR
                "no line of ${EXPECT_FILE} matches: ${pattern}")
        endif()
    endforeach()
endif()

message(STATUS "ok: exit ${actual_exit}, diagnostic matched")
