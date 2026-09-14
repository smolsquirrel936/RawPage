# Runs tests/gates/check-signatures.py against the OpenSSL that ResolveOpenSSL.cmake finds.
# Guide case A-15 (claude-plan/Stage3-Mutation-Testing-Guide.md §5.3), decision D3-2.
#
# Invoked via `cmake -P`, like ResolveOpenSSL.cmake, and for the same reason it is resolved at TEST
# time: the validator a case uses is the one on the job that runs it, never a path baked into a
# cache at configure time. INCLUDING the resolver rather than repeating it is what keeps "which
# OpenSSL judged A4" one answer -- `gates.validators.openssl` and this case cannot disagree about it,
# and a missing validator fails here with the resolver's own message rather than skipping.
#
# Optional: -DPYTHON=<interpreter>           default: find_package(Python3), as configure does
#           -DSCRIPT=<check-signatures.py>   default: the one beside this file
#           -DFILES=<a@@b@@...>              default: every corpus/signed/*.pdf
#
# Stage 3 T2's probe (guide case A-28) runs the same procedure over the files the writer produces,
# which is why the files are an argument. The defaults exist for CI's "A-15" steps, which call this
# script directly rather than through ctest: RunGateCase.cmake prints a case's output only when it
# FAILS, so a verbose ctest of a passing case shows neither the validator nor what each control said.
# Python is FOUND rather than taken from PATH because the self-hosted Windows runner's interpreter is
# reached through the registry, the way configure reaches it (measured at Stage 3 T1: a job PATH of
# System32 alone still finds it).

if(NOT DEFINED PYTHON)
    find_package(Python3 3.9 COMPONENTS Interpreter QUIET)
    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR
            "CheckSignatures.cmake: no Python 3.9+ interpreter was found, so A-15 cannot run -- "
            "and a check that cannot run FAILS rather than skipping (RequirePython.cmake's reason).")
    endif()
    set(PYTHON "${Python3_EXECUTABLE}")
endif()
if(NOT DEFINED SCRIPT)
    set(SCRIPT "${CMAKE_CURRENT_LIST_DIR}/check-signatures.py")
endif()
if(NOT DEFINED FILES)
    file(GLOB file_list "${CMAKE_CURRENT_LIST_DIR}/../../corpus/signed/*.pdf")
    list(SORT file_list)
    if(NOT file_list)
        message(FATAL_ERROR "CheckSignatures.cmake: no FILES given and corpus/signed/ holds no PDF")
    endif()
else()
    string(REPLACE "@@" ";" file_list "${FILES}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ResolveOpenSSL.cmake")

execute_process(
    COMMAND "${PYTHON}" "${SCRIPT}" --openssl "${openssl_exe}" ${file_list}
    RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "check-signatures.py exited ${rc}")
endif()
