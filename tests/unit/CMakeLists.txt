# tests/unit -- the headless suite.
#
# THE STANDING PROOF: this target links rawpage_core, rawpage_cos and -- from Stage 3 T3 -- the
# portable rawpage_ops (D3-3), and NOTHING ELSE. If the
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
    tst_actionalias.cpp           # Stage 2 T18 -- Stage 1 guide case A-26, finally run. The alias
                                  #   MECHANISM has had zero rows since T3, and the two cases in
                                  #   tst_actionregistry that were meant to cover it loop over an
                                  #   empty array. This drives the resolution algorithm with real
                                  #   rows from tests/unit/fixtures/ActionAliasFixture.def (D2-10).
    tst_keybindings.cpp           # Stage 1 T4
    tst_settings.cpp

    # S2 T15 -- the sixteen pageDisplay/ and performance/ rows. Separate from tst_settings.cpp
    # because that file tests the MECHANISM (typed get/set, clamping, migration, the codecs) and
    # every one of its cases would pass with an empty table; this one tests the TABLE, including
    # that the four defaults which used to live in C++ structs still agree with the rows that took
    # them over.
    tst_stage2settings.cpp        # Stage 2 T15 -- guide case A-63
    tst_recentfiles.cpp           # Stage 1 T5

    # S4 T3 -- session restore, the one Stage 4 feature that fails BETWEEN runs, and the only
    # stage-4 subject a headless suite can reach in full. tst_fingerprint.cpp is separate because
    # its subject is ARITHMETIC over synthetic MonitorInfo values -- the hash and the clamp -- and
    # every case in it would pass with an empty session file.
    tst_sessionstate.cpp
    tst_fingerprint.cpp

    # S4 T8 -- the snap resolver's arithmetic (D4-6) and the guide round trip. HEADLESS by design:
    # the plan puts tst_snap in this target precisely because the precedence and the device-pixel
    # threshold must be provable with no widget; the ruler, the drag and the repaint are
    # tests/ui/tst_rulers.cpp's half.
    tst_snap.cpp

    # S4 T12 -- core/render/FitPolicy (D4-14), which closes Stage 2's I-28. HEADLESS for the same
    # reason tst_snap is: which rectangle a fit measures and what zoom satisfies it are arithmetic,
    # and the assertion is on the NUMBER rather than on the mode -- the whole of I-28's lesson. The
    # engine half, where a real page's objects are unioned, is tests/render/tst_contentbox.cpp.
    tst_fitpolicy.cpp

    # S4 T4 -- the read-only models VOCABULARY: counting a tree, finding a name, parsing a PDF
    # date. The claims about real documents are tests/render/tst_docmodels.cpp's, because they are
    # claims about a READER and this target may never link the engine.
    tst_outline.cpp
    tst_metadata.cpp
    tst_commandline.cpp           # Stage 1 T8
    tst_logbuffer.cpp             # Stage 1 T14
    tst_singleinstance.cpp        # Stage 1 T15 -- guide case A-61
    tst_backend.cpp               # Stage 2 T1  -- guide case A-46, the engine-interface census
    tst_cos_lexer.cpp             # Stage 2 T5  -- guide case A-15, and the fuzz body's
                                  #                only Windows caller (tests/fuzz)
    tst_cos_parser.cpp            # Stage 2 T6  -- guide cases A-16 and A-17
    tst_cos_filters.cpp           # Stage 2 T7  -- guide cases A-18 and A-19
    tst_xreftable.cpp             # Stage 2 T7  -- guide case A-20
    tst_xrefrepair.cpp            # Stage 2 T8  -- guide cases A-21 and A-22, the exit criterion
    tst_docvocabulary.cpp         # Stage 2 T8  -- core/doc/Revision and Scope (D2-5)
    tst_pagetree.cpp              # Stage 2 T9  -- core/doc/Page, PageTree, RenderRequest
    tst_dpipolicy.cpp             # Stage 2 T10 -- guide cases A-28, A-29, A-30
    tst_tilegrid.cpp              # Stage 2 T10 -- guide case A-31
    tst_presentation.cpp          # Stage 2 T10 -- guide cases A-32, A-33, A-34
    tst_rendercache.cpp           # Stage 2 T11 -- guide cases A-40, A-41
    tst_renderscheduler.cpp       # Stage 2 T11 -- guide cases A-36, A-37, A-38, A-39's core/ half;
                                  #   Stage 5 T2 adds the TEXT JOB's cases (D5-3): A-24's headless
                                  #   half and the strict-priority half of A-29
    tst_textmodel.cpp             # Stage 5 T2 -- core/text/PageTextModel (D5-2), headless: the
                                  #   unmapped rule, the font table, extractable-or-not
    tst_normalize.cpp             # Stage 5 T4 -- core/text/TextNormalizer (D5-5): guide A-32 ... A-36
    tst_layout.cpp                # Stage 5 T4 -- core/text/LayoutAnalyzer (D5-6) on synthetic
                                  #   geometry: guide A-37, A-39 and A-41, each rule with its control
    tst_selection.cpp             # Stage 5 T5 -- core/text/TextSelection (D5-8, D5-9) on synthetic
                                  #   geometry: the arithmetic halves of guide A-43 ... A-47, A-49, A-21's
                                  #   copy and A-41's selection clause; the INK is tst_selecttool's
    tst_fontsubstitution.cpp      # Stage 2 T16 -- the headless half of guide case A-66: WHICH
                                  #   face substitutes, asserted with no engine and no Windows
    tst_objectallocator.cpp       # Stage 3 T2  -- guide case A-23
    tst_incrementalwriter.cpp     # Stage 3 T2  -- guide cases A-19 ... A-25, A-27, and A-28's
                                  #   writing half (the probe; OpenSSL's half is registered below)
    tst_pageops.cpp               # Stage 3 T3  -- guide cases A-29's headless half and A-30's PageOps half
    tst_mutationsink.cpp          # Stage 3 T3  -- guide cases A-30 ... A-33 and T3's two controls
    tst_document.cpp              # Stage 3 T4  -- guide cases A-34's headless half and A-35
    tst_permissions.cpp           # Stage 3 T4  -- guide case A-36's planted half (D3-7)
    tst_journal.cpp               # Stage 3 T5  -- guide cases A-38 ... A-41 (D3-10)
    tst_command_serialize.cpp     # Stage 3 T5  -- guide cases A-42 and A-45 (D3-12)
    tst_reversibility.cpp         # Stage 3 T5  -- guide cases A-43 and A-44, the property and its control
    tst_savecoordinator.cpp       # Stage 3 T8  -- guide case A-50, and A-60's coordinator half
    tst_mutationrender.cpp        # Stage 3 T8  -- guide case A-59: a journal rotation reaching the scheduler and cache
    tst_recovery.cpp              # Stage 3 T9  -- guide cases A-62 ... A-67's in-process halves and A-70 (D3-11)
    tst_stage3settings.cpp        # Stage 3 T12 -- guide case A-82's key half: documents/recoveryDir and warnOnRepairedSave (D3-9)

    # Stage 3 T8 -- THE ROUND TRIP, a source directory compiled in rather than a sixth target (D3-18):
    # it links exactly what this target may, and so it runs under portable-asan too.
    ../roundtrip/tst_roundtrip.cpp   # guide cases A-51 ... A-56

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
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus"
    # Guide case A-19 asserts that `CryptFilter` is ABSENT, which is a claim about the source tree
    # rather than about any compiled symbol -- so the directory has to be reachable at runtime, by
    # the same mechanism and for the same reason as the two paths above.
    RAWPAGE_FILTERS_DIR="${CMAKE_SOURCE_DIR}/pdf/cos/filters"
    # Stage 3 T2: where the A4 probe writes its four outputs. In the BUILD tree, never the source
    # tree -- they are measurements, regenerated every run, and a copy beside corpus/ would look like
    # a fixture somebody should commit.
    RAWPAGE_A4_PROBE_DIR="${CMAKE_BINARY_DIR}/a4-probe"
    # Stage 3 T5: the catalog SOURCES, for guide case A-42 -- every command's `name()` key is present
    # in both shipped locales. Read as text, the same way tools/i18n-lint reads them.
    RAWPAGE_I18N_DIR="${CMAKE_SOURCE_DIR}/i18n")

target_include_directories(rawpage_tests PRIVATE "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_tests PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_ops        # Stage 3 T3 (D3-3): portable, no engine -- gates.headless_assert.ops_* prove it
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

# --- A-28: risk A4's probe, judged by OpenSSL (Stage 3 T2, decision D3-1) --------------------
# The writing case is also discovered above and runs on its own; this one runs it FIRST and then
# asks OpenSSL, through tests/gates/CheckSignatures.cmake -- the same resolver, extractor and
# flipped-byte controls as guide case A-15. A missing OpenSSL or Python FAILS it, never skips it.
# Not WIN32-only: portable-asan runs it too, which is the writer under ASan on the probe's inputs.
add_test(NAME unit.a4_probe.openssl
    COMMAND ${CMAKE_COMMAND}
            "-DTEST_BINARY=$<TARGET_FILE:rawpage_tests>"
            "-DPROBE_DIR=${CMAKE_BINARY_DIR}/a4-probe"
            "-DCHECKS_DIR=${CMAKE_SOURCE_DIR}/tests/gates"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunA4Probe.cmake")
if(WIN32)
    set_tests_properties(unit.a4_probe.openssl PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin")
endif()
# The verdict is the EXIT CODE: CheckSignatures.cmake fails unless every control is rejected and
# every signature verifies. No PASS_REGULAR_EXPRESSION, which would make CTest ignore the exit code
# -- tests/gates/RunGateCase.cmake's header records why that is a trap.

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

# And Stage 3 T13's (D3-17): `fuzz_cos_roundtrip` reaches into pdf/ops/ as well, so a signature change
# in the sink or RevisionWriter is one more way for an entry point nobody builds here to break.
rawpage_add_compile_case(NAME unit.cos.compile.fuzz_entrypoint_roundtrip
    SOURCE fuzz-entrypoint-roundtrip.cpp EXPECT_FAIL 0)

rawpage_add_compile_case(NAME unit.commandline.compile.arguments_list_control
    SOURCE arguments-list.cpp EXPECT_FAIL 0)
