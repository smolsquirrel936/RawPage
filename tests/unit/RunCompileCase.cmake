# Compiles one fixture and asserts that it did -- or did not -- compile. Guide case A-41.
#
# WHY A CTEST CASE AND NOT A configure-time try_compile: a try_compile records its answer in the
# cache, so the assertion runs once on a fresh configure and never again. "An undeclared key does
# not compile" is a property of the code, and a property that is only ever checked on the machine
# that first configured the build is not enforced -- it is remembered.
#
# WHY -fsyntax-only: this is a claim about the LANGUAGE rejecting the name, not about linking. It
# also keeps the case fast enough that four of them are free.
#
# Required: -DCXX=<compiler> -DSOURCE=<file> -DROOT=<repo root> -DEXPECT_FAIL=<0|1>
# Optional: -DINCLUDES=<dirs joined by |>   Qt's include directories
#           -DEXPECT_MATCH=<regex>          the diagnostic must mention this

if(NOT DEFINED CXX OR NOT DEFINED SOURCE OR NOT DEFINED ROOT OR NOT DEFINED EXPECT_FAIL)
    message(FATAL_ERROR "RunCompileCase.cmake: CXX, SOURCE, ROOT and EXPECT_FAIL are required")
endif()

set(include_flags "-I${ROOT}")
if(DEFINED INCLUDES AND NOT INCLUDES STREQUAL "")
    string(REPLACE "|" ";" include_list "${INCLUDES}")
    foreach(dir IN LISTS include_list)
        if(NOT dir STREQUAL "")
            list(APPEND include_flags "-I${dir}")
        endif()
    endforeach()
endif()

execute_process(
    COMMAND "${CXX}" -std=c++20 -fsyntax-only ${include_flags} "${SOURCE}"
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE stdout_text
    ERROR_VARIABLE stderr_text
)

set(combined "${stdout_text}${stderr_text}")

if(EXPECT_FAIL)
    if(actual_exit EQUAL 0)
        message(FATAL_ERROR
            "${SOURCE} COMPILED.\n\n"
            "D1-10 is the guarantee that an undeclared settings key is a name that does not exist. "
            "If this compiles, the schema is a convention rather than a guarantee -- and a "
            "convention lasts until the first stage that is in a hurry.\n")
    endif()
    if(DEFINED EXPECT_MATCH AND NOT EXPECT_MATCH STREQUAL "")
        if(NOT combined MATCHES "${EXPECT_MATCH}")
            message(FATAL_ERROR
                "${SOURCE} failed to compile, but not for the stated reason: ${EXPECT_MATCH}\n"
                "A negative control that fails for the wrong reason proves nothing -- Stage 1 T3 "
                "already lost two probes to exactly that.\n"
                "--- output ---\n${combined}")
        endif()
    endif()
    message(STATUS "ok: rejected, and for the right reason")
else()
    if(NOT actual_exit EQUAL 0)
        message(FATAL_ERROR
            "the POSITIVE CONTROL ${SOURCE} did not compile.\n\n"
            "Without it, 'an undeclared key does not compile' is equally satisfied by a header that "
            "compiles for nobody.\n"
            "--- output ---\n${combined}")
    endif()
    message(STATUS "ok: compiled")
endif()
