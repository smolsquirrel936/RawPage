# Finds the OpenSSL command-line tool that judges A4's integrity half, says WHICH one, and FAILS
# when there is none. Stage 3 precondition P-2 and guide case A-06
# (claude-plan/Stage3-Mutation-Plan.md §7, claude-plan/Stage3-Mutation-Testing-Guide.md §5.1).
#
# Invoked via `cmake -P`, like RunGateCase.cmake, so it needs no shell and behaves the same on the
# Windows runner and the Linux gates runner.
#
# WHY A FAILURE AND NEVER A SKIP. Decision D3-2 makes this tool the automated validator for A4.
# "A4 cannot be judged" reading as "A4 passed" is an S1, and a case that vanishes when its tool is
# missing has exactly the shape of Stage 1's I-30. Same reasoning as RequirePython.cmake; OpenSSL is
# a VERIFICATION dependency, never a build one -- `cmake --build` does not call it, and nothing in
# the product links it (guide case A-07 measures that).
#
# WHY RESOLVED AT TEST TIME, not at configure time. A-06's second half runs the case with a PATH
# that holds no OpenSSL and expects a failure; a path baked into the cache at configure time would
# survive that and prove nothing.
#
# WHY RAWPAGE_OPENSSL EXISTS. Measured 2026-09-14: on the self-hosted Windows runner NEITHER copy on
# the machine is on the registry PATH. Miniconda's arrives only through `conda init` in the user's
# PowerShell profile, and Git for Windows' sits in Git\mingw64\bin, which is not on PATH at all. A
# validator reached through a personal shell profile disappears under `-NoProfile`, a `cmd` step, or
# a runner installed as a service -- so CI names the executable instead, and when the variable is
# set it is the ONLY place looked. A variable that names a file that is not there is a failure, not
# a quiet fall-back to PATH, because a fall-back would run a validator nobody chose.
#
# Output always names the path, how it was found, and the version line, so every result names its
# validator (P-2).
#
# What this does NOT check: that the tool verifies CMS. `openssl version` answering "OpenSSL 3." is
# the precondition; the signed-corpus cases (Stage 3 T1) and their flipped-byte control are what
# prove it verifies, and rejects, a real signature.

set(env_override "$ENV{RAWPAGE_OPENSSL}")

if(NOT env_override STREQUAL "")
    set(openssl_exe "${env_override}")
    set(found_by "the RAWPAGE_OPENSSL environment variable")
    if(NOT EXISTS "${openssl_exe}" OR IS_DIRECTORY "${openssl_exe}")
        message(FATAL_ERROR
            "RAWPAGE_OPENSSL names '${openssl_exe}', which is not a file.\n"
            "When RAWPAGE_OPENSSL is set it is the only place looked -- PATH is not searched -- so "
            "A4's automated validator cannot run. Correct the variable or unset it.")
    endif()
else()
    # PATH only, and explicitly: CMake's default search also walks platform prefixes, which would
    # find a copy that no shell on the job would find.
    find_program(openssl_exe NAMES openssl PATHS ENV PATH NO_DEFAULT_PATH NO_CACHE)
    set(found_by "PATH")
    if(NOT openssl_exe)
        message(FATAL_ERROR
            "no OpenSSL command-line tool was found on PATH, and RAWPAGE_OPENSSL is not set.\n"
            "It is A4's automated signature validator (decision D3-2, "
            "claude-plan/Stage3-Mutation-Plan.md), and a validator that is missing FAILS this "
            "case rather than skipping it: 'A4 cannot be judged' must never read as 'A4 passed'.\n"
            "Install OpenSSL 3 and put it on PATH, or set RAWPAGE_OPENSSL to the executable.")
    endif()
endif()

execute_process(
    COMMAND "${openssl_exe}" version
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
string(STRIP "${out}${err}" version_line)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR
        "'${openssl_exe}' (from ${found_by}) exited ${rc} on `openssl version`:\n${version_line}")
endif()

if(NOT version_line MATCHES "^OpenSSL 3\\.")
    message(FATAL_ERROR
        "'${openssl_exe}' (from ${found_by}) is not OpenSSL 3: ${version_line}\n"
        "P-2 requires version 3; D3-2's `cms -verify` invocation is written against it.")
endif()

message("openssl validator: ${openssl_exe}")
message("  found by: ${found_by}")
message("  version:  ${version_line}")
