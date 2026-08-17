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
# Required: -DCOMMAND_STR=... -DEXPECT_EXIT=... -DEXPECT_MATCH=...

if(NOT DEFINED COMMAND_STR OR NOT DEFINED EXPECT_EXIT)
    message(FATAL_ERROR "RunGateCase.cmake: COMMAND_STR and EXPECT_EXIT are required")
endif()

# Arguments are joined with '|' rather than spaces by the caller. A toolchain case passes
# things like  -DCMAKE_CXX_FLAGS=-U__GNUC_MINOR__ -D__GNUC_MINOR__=9  as ONE argument, and
# splitting on whitespace would tear it in half -- producing a test that fails for a reason
# unrelated to what it claims to check.
string(REPLACE "|" ";" command_list "${COMMAND_STR}")

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

message(STATUS "ok: exit ${actual_exit}, diagnostic matched")
