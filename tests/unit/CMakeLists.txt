# tests/unit -- the headless suite.
#
# THE STANDING PROOF: this target links rawpage_core and rawpage_cos and NOTHING ELSE. If the
# full domain suite cannot pass without a display and without a PDF engine, the layer
# boundary has already leaked -- and it will have leaked quietly, months before anyone
# notices. Tech-Stack-Plan.md 9 names this as a verification step in its own right.
#
# It is asserted below rather than left as a convention, because a convention survives
# exactly until someone needs one widget class in a test.
#
# Stage 2 T1 moved the assertion itself into cmake/RawPageHeadless.cmake and added rawpage_pdfium
# to its forbidden list (D2-2). It is included rather than written here so that the gate fixture at
# tests/gates/fixtures/headless-assert/ exercises the REAL function -- a fixture carrying its own
# copy would prove that the copy fires.

find_package(Catch2 3 REQUIRED)

add_executable(rawpage_tests
    tst_result.cpp
    tst_units.cpp
    tst_geometry.cpp
    tst_log.cpp
    tst_actionregistry.cpp        # Stage 1 T3
    tst_keybindings.cpp           # Stage 1 T4
    tst_settings.cpp              # Stage 1 T5
    tst_recentfiles.cpp           # Stage 1 T5
    tst_commandline.cpp           # Stage 1 T8
    tst_logbuffer.cpp             # Stage 1 T14
    tst_singleinstance.cpp        # Stage 1 T15 -- guide case A-61
    tst_backend.cpp               # Stage 2 T1  -- guide case A-46, the engine-interface census
    tst_cos_lexer.cpp             # Stage 2 T5  -- guide case A-15, and the fuzz body's
                                  #                only Windows caller (tests/fuzz)
    tst_cos_parser.cpp            # Stage 2 T6  -- guide cases A-16 and A-17

    # THE PARSER ITSELF, compiled in rather than linked from the executable.
    #
    # app/CommandLine is a pure function from a QStringList to an intent -- no filesystem, no Qt
    # GUI type, no side effect -- which is the whole reason Stage1-AppShell-Plan.md 5.7 puts its
    # test in the HEADLESS suite even though the file lives in app/. Note what this does NOT do:
    # it adds no target_link_libraries entry, so rawpage_assert_headless() below still resolves to
    # rawpage_core + rawpage_cos and guide case A-64 still holds. The day this file needs
    # rawpage_ui or rawpage_platform to compile, configure fails -- and the finding is about the
    # parser, not about the test.
    ../../app/CommandLine.cpp

    # And app/LogFile, compiled in for exactly the same reason and under the same constraint: it
    # names QtCore and nothing else -- no widget, no Win32 -- so the rolling log file is testable
    # without the executable, and the day it needs more than QtCore, configure fails below and the
    # finding is about the log file rather than about the test.
    ../../app/LogFile.cpp

    # And app/SingleInstance, on the same terms and for guide case A-61 (T15). It names core/ and
    # platform/platform.h -- an INTERFACE header that includes no OS header, which is its own
    # layer-lint layer entry -- so the policy half of "does a second launch forward?" is testable
    # against a recording bus with no Win32 object created anywhere. It adds no
    # target_link_libraries entry, so rawpage_assert_headless() below still resolves to
    # rawpage_core + rawpage_cos and guide case A-83 is untouched.
    ../../app/SingleInstance.cpp
)

# D1-9: tst_keybindings PARSES Features.md 8 and asserts the store matches it row for row, so the
# document stays the single source of truth and a doc edit the code has not absorbed breaks the
# build. The path is a compile definition rather than a runtime lookup because a test that silently
# skips when it cannot find its input is a test that passes for the wrong reason.
target_compile_definitions(rawpage_tests PRIVATE
    RAWPAGE_FEATURES_MD="${CMAKE_SOURCE_DIR}/claude-plan/Features.md"
    # The corpus, by absolute source path, for tst_cos_lexer's replay of corpus/malformed/. Same
    # mechanism as the line above and for the same reason: a test that guessed a relative path
    # would pass or fail depending on the working directory CTest happened to use.
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus")

target_include_directories(rawpage_tests PRIVATE "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_tests PRIVATE
    rawpage_core
    rawpage_cos
    Catch2::Catch2WithMain
)

rawpage_common_flags(rawpage_tests)

# --- The boundary assertion ---------------------------------------------------------
# Defined in cmake/RawPageHeadless.cmake, for the reason that file's header gives: the gate fixture
# that proves it fires has to exercise the real one. Forbidden today: rawpage_ui, rawpage_platform
# and -- since Stage 2 T1 -- rawpage_pdfium.
include(RawPageHeadless)

rawpage_assert_headless(rawpage_tests)

# catch_discover_tests registers each TEST_CASE with CTest by name, so `ctest -R tst_journal`
# selects a suite exactly as CLAUDE.md and Tech-Stack-Plan.md 9 document it.
include(Catch)
catch_discover_tests(rawpage_tests
    TEST_PREFIX "unit."
    # The built binaries are dynamically linked against libstdc++ and libgcc, so the
    # toolchain bin directory has to be reachable or every test fails to LAUNCH with
    # 0xC0000135 -- a failure that looks nothing like a test failure.
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin"
)

# --- A-41: an undeclared settings key does not compile --------------------------------
# D1-10's guarantee is a COMPILE-TIME one, so it cannot be asserted from inside a binary that by
# definition compiled. Each case drives the compiler at the fixture directly and checks the exit
# code AND the diagnostic -- a rejection for an unrelated reason is not the rejection we claim.
#
# Four fixtures, three spellings of the mistake plus a positive control, because I-15's finding was
# that a rule tested against one spelling catches one spelling.
set(COMPILE_RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/RunCompileCase.cmake")
set(COMPILE_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/compile")

# Qt's own include directories, from the variable find_package set rather than from a target
# property: a property would arrive as a generator expression that `cmake -P` cannot evaluate.
list(JOIN Qt6Core_INCLUDE_DIRS "|" RAWPAGE_QTCORE_INCLUDES)

function(rawpage_add_compile_case)
    cmake_parse_arguments(ARG "" "NAME;SOURCE;EXPECT_FAIL;EXPECT_MATCH" "" ${ARGN})
    add_test(NAME ${ARG_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DCXX=${CMAKE_CXX_COMPILER}"
                "-DSOURCE=${COMPILE_FIXTURES}/${ARG_SOURCE}"
                "-DROOT=${CMAKE_SOURCE_DIR}"
                "-DINCLUDES=${RAWPAGE_QTCORE_INCLUDES}"
                "-DEXPECT_FAIL=${ARG_EXPECT_FAIL}"
                "-DEXPECT_MATCH=${ARG_EXPECT_MATCH}"
                -P "${COMPILE_RUNNER}")
endfunction()

rawpage_add_compile_case(NAME unit.settings.compile.undeclared_key
    SOURCE undeclared-key.cpp EXPECT_FAIL 1 EXPECT_MATCH "uiFontScal")

rawpage_add_compile_case(NAME unit.settings.compile.string_key
    SOURCE string-key.cpp EXPECT_FAIL 1 EXPECT_MATCH "get")

rawpage_add_compile_case(NAME unit.settings.compile.wrong_type
    SOURCE wrong-type.cpp EXPECT_FAIL 1 EXPECT_MATCH "set")

# THE CONTROL. Without it every case above is satisfied by a header nobody can include.
rawpage_add_compile_case(NAME unit.settings.compile.declared_key_control
    SOURCE declared-key.cpp EXPECT_FAIL 0)

# --- I-04: the command-line parser cannot be handed argv ------------------------------------
# Structural, because the runtime failure -- a path silently transcoded through the ANSI code page
# -- produces a perfectly valid QString that no assertion can tell from a correct one. The other
# half of the rule is tools/layer-lint's non-escapable `argv-path-read` token rule.
rawpage_add_compile_case(NAME unit.commandline.compile.argv_overload
    SOURCE argv-overload.cpp EXPECT_FAIL 1 EXPECT_MATCH "parse")

# --- Stage 2 T5: the fuzz entry point still compiles ----------------------------------------
# `fuzz_cos_lexer` is built only by `portable-fuzz` -- clang, Linux, libFuzzer -- so nothing on
# this machine or in the Windows CI job would notice it breaking. This case compiles that exact
# translation unit with `-fsyntax-only`; it cannot link, because there is no libFuzzer here to
# supply main(), and linking is not what is at risk. What IS at risk is a signature in
# pdf/cos/Lexer.h changing under an entry point nobody builds, and waiting for a Linux run to
# find out. Guide case A-27's concern, one layer earlier.
rawpage_add_compile_case(NAME unit.cos.compile.fuzz_entrypoint
    SOURCE fuzz-entrypoint.cpp EXPECT_FAIL 0)

# The same for T6's target, which is the PRIMARY one: `fuzz_cos_parser` covers the layer that
# recurses, that sizes containers from the input and that trusts a producer-written `/Length`.
rawpage_add_compile_case(NAME unit.cos.compile.fuzz_entrypoint_parser
    SOURCE fuzz-entrypoint-parser.cpp EXPECT_FAIL 0)

rawpage_add_compile_case(NAME unit.commandline.compile.arguments_list_control
    SOURCE arguments-list.cpp EXPECT_FAIL 0)
