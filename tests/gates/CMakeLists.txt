# The gates, tested against deliberately bad input.
#
# A gate that has only ever been run on a clean tree is an assumption. Every case here
# asserts the exact exit code AND the named diagnostic, so a gate that fails for an
# unrelated reason does not count as passing.
#
# Note the NEGATIVE CONTROLS (write-escaped, gcc-exception, lgpl). Without them, every
# positive case is equally satisfied by a gate that rejects everything.

set(FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/RunGateCase.cmake")

function(rawpage_add_gate_test)
    cmake_parse_arguments(ARG "" "NAME;EXPECT_EXIT;EXPECT_MATCH;EXPECT_FILE;EXPECT_ABSENT" "COMMAND;EXPECT_FILE_MATCH" ${ARGN})
    # Patterns travel joined by @@, because | is already the argument separator below.
    string(REPLACE ";" "@@" file_match_str "${ARG_EXPECT_FILE_MATCH}")
    # '|' rather than ' ' as the separator: a toolchain case passes an argument that itself
    # contains spaces, and joining on space would split it back into two.
    string(REPLACE ";" "|" command_str "${ARG_COMMAND}")
    add_test(NAME ${ARG_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DCOMMAND_STR=${command_str}"
                "-DEXPECT_EXIT=${ARG_EXPECT_EXIT}"
                "-DEXPECT_MATCH=${ARG_EXPECT_MATCH}"
                "-DEXPECT_FILE=${ARG_EXPECT_FILE}"
                "-DEXPECT_ABSENT=${ARG_EXPECT_ABSENT}"
                "-DEXPECT_FILE_MATCH=${file_match_str}"
                -P "${RUNNER}")
endfunction()

# --------------------------------------------------------------------------------------
# layer-lint
# --------------------------------------------------------------------------------------
set(LL $<TARGET_FILE:layer-lint>)
set(LL_RULES "${CMAKE_SOURCE_DIR}/tools/layer-lint/rules.json")

rawpage_add_gate_test(NAME gates.layer_lint.clean_tree
    COMMAND ${LL} --root ${CMAKE_SOURCE_DIR}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

rawpage_add_gate_test(NAME gates.layer_lint.outward_include
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/outward-include --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "outward-include")

rawpage_add_gate_test(NAME gates.layer_lint.ui_includes_pdf
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/ui-includes-pdf --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "outward-include")

rawpage_add_gate_test(NAME gates.layer_lint.fpdf_outside_pdf
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/fpdf-outside-pdf --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "fpdf")

rawpage_add_gate_test(NAME gates.layer_lint.write_outside_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/write-outside-allowed --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "filesystem-write")

rawpage_add_gate_test(NAME gates.layer_lint.qtwidgets_in_core
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/qtwidgets-in-core --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "external-include")

# I-20. A waiver must be APPLIED, not merely mentioned. layer-lint counted its own
# documentation as three active waivers, so --explain reported 3 where the truth was 0 --
# and D0-4's whole value is that this census can be trusted to show waivers creeping from a
# handful to a hundred.
rawpage_add_gate_test(NAME gates.layer_lint.quoted_waiver_does_not_waive
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/write-escape-quoted --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "filesystem-write")

rawpage_add_gate_test(NAME gates.layer_lint.unknown_waiver_kind_does_not_waive
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/write-escape-unknown-kind --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "filesystem-write")

# NEGATIVE CONTROL: the inline waiver must actually waive.
rawpage_add_gate_test(NAME gates.layer_lint.write_escaped_is_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/write-escaped --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# I-15. The same violations, spelled the other ways a developer actually writes them. All
# three compiled and all three passed clean, because every fixture above used the one
# canonical spelling -- the gate was only ever tested against the form the fixtures used.
rawpage_add_gate_test(NAME gates.layer_lint.outward_include_relative
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/outward-include-relative --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "outward-include")

rawpage_add_gate_test(NAME gates.layer_lint.outward_include_angled
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/outward-include-angled --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "outward-include")

# Windows only, and not for convenience: on a case-sensitive filesystem
# #include "Pdf/Cos/CosDict.h" does not resolve at all, so it is a compile error rather than a
# layering violation and there is nothing for the gate to find. Running it on the Linux gates
# runner produced a red job for a scenario that cannot happen there -- and a red CI job that
# everyone learns to ignore protects nothing.
if(CMAKE_HOST_WIN32)
    rawpage_add_gate_test(NAME gates.layer_lint.outward_include_casemix
        COMMAND ${LL} --root ${FIXTURES}/layer-lint/outward-include-casemix --rules ${LL_RULES}
        EXPECT_EXIT 1 EXPECT_MATCH "outward-include")
endif()

rawpage_add_gate_test(NAME gates.layer_lint.qt_write_outside_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/qt-write --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "filesystem-write")

# NEGATIVE CONTROL for the widened matching: prose that NAMES the forbidden things must stay
# legal. Without this, the fix for I-15 could just as well have been "flag everything".
rawpage_add_gate_test(NAME gates.layer_lint.mention_in_comment_is_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/mention-in-comment --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# Stage 1 T7. platform/platform.h is declared as its OWN layer with an OS-header deny list,
# because it is included by core/, ui/ and app/: a windows.h reaching it would give all three a
# platform-SDK dependency, and the failure would surface not here but as the Linux portable-asan
# job no longer being able to build core/ -- which is the mechanical proof that core/ is portable.
rawpage_add_gate_test(NAME gates.layer_lint.platform_header_includes_windows
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/platform-header-includes-windows --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "external-include")

# Stage 1 T8. Issue I-04's rule made mechanical: nothing may read a path out of argv. The fixture is
# the exact shape of the mistake -- a command-line parser building a QStringList from argv -- because
# that is the one place it would ever be written, and on a machine whose code page cannot represent
# the path it compiles, runs, and opens the wrong file.
rawpage_add_gate_test(NAME gates.layer_lint.argv_path_read
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/argv-path-read --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "argv-path-read")

# THE CONTROL. app/main.cpp holds argv legitimately: it hands it to QApplication and prints it for
# the --print-args measurement that PRODUCED this rule. A gate that forbade argv everywhere would
# have made its own evidence unwritable.
rawpage_add_gate_test(NAME gates.layer_lint.argv_in_entry_point_is_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/argv-path-read-entry-point --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# Stage 1 T10 -- decision D1-7(a), guide case A-12. FOUR SPELLINGS AND TWO CONTROLS.
#
# The four are the ways a QAction actually gets built: on the heap, on the stack, and through the
# two addAction() conveniences -- QMenu's and QToolBar's -- that construct one without the word
# `new` appearing anywhere. Stage 0's third S1 was a gate that passed real violations because every
# fixture used the same spelling, and this is that lesson spent rather than remembered.
rawpage_add_gate_test(NAME gates.layer_lint.qaction_new
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/qaction-new --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "bare-qaction")

rawpage_add_gate_test(NAME gates.layer_lint.qaction_stack
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/qaction-stack --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "bare-qaction")

rawpage_add_gate_test(NAME gates.layer_lint.qaction_menu_literal
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/qaction-menu-literal --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "bare-qaction")

rawpage_add_gate_test(NAME gates.layer_lint.qaction_toolbar_literal
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/qaction-toolbar-literal --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "bare-qaction")

# CONTROL ONE. ui/shell/ActionFactory.cpp is the one file allowed to construct an action, and it
# must pass -- otherwise "fires on every spelling" is equally satisfied by a rule that rejects
# everything and makes the mechanism it protects impossible to write.
rawpage_add_gate_test(NAME gates.layer_lint.qaction_in_factory_is_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/qaction-factory-allowed --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# CONTROL TWO. A file that discusses QAction in prose and constructs none. The gates read the part
# of a line the compiler sees; a rule that also matched comments would make its own documentation
# unwritable, which is Stage 0's I-20 wearing a different hat.
rawpage_add_gate_test(NAME gates.layer_lint.qaction_mention_in_comment_is_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/qaction-mention-in-comment --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# Stage 1 T9. ui/ is granted exactly two single-file looks outward -- platform/platform.h and
# app/Services.h -- and the whole value of that is that it is two files rather than two layers.
rawpage_add_gate_test(NAME gates.layer_lint.ui_includes_app_internals
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/ui-includes-app-internals --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "outward-include")

# THE CONTROL for the pair, exercised together: without it, "ui/ may include core/ and nothing else"
# reads the positive case above just as well, and the shell could not be built at all.
rawpage_add_gate_test(NAME gates.layer_lint.ui_may_include_the_two_interface_headers
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/ui-may-include-services --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# THE CONTROL. platform/win/ is the one place in the project that MAY include the OS, and a rule
# that forbade windows.h anywhere under platform/ would make the layer unable to do its only job.
rawpage_add_gate_test(NAME gates.layer_lint.platform_impl_may_include_windows
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/platform-impl-may-include-windows --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# A gate that could not run must not look like a gate that passed.
rawpage_add_gate_test(NAME gates.layer_lint.missing_rules_is_exit_2
    COMMAND ${LL} --root ${CMAKE_SOURCE_DIR} --rules ${FIXTURES}/does-not-exist.json
    EXPECT_EXIT 2 EXPECT_MATCH "cannot open")

# --------------------------------------------------------------------------------------
# license-audit
# --------------------------------------------------------------------------------------
set(LA $<TARGET_FILE:license-audit>)
set(LA_MAP "${CMAKE_SOURCE_DIR}/tools/license-audit/spdx-map.json")
# Fixture components live in their own map: putting them in the production one would
# ship the gate's test data in the licence report.
set(LA_FIXMAP "${FIXTURES}/license-audit/spdx-map-fixture.json")

rawpage_add_gate_test(NAME gates.license_audit.gpl_dependency
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-gpl.json --spdx-map ${LA_FIXMAP}
    EXPECT_EXIT 1 EXPECT_MATCH "not on the allowlist")

# THE SUBSTRING TEST. Bare GPL-3.0-or-later must fail...
rawpage_add_gate_test(NAME gates.license_audit.bare_gpl_without_exception
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-bare-gcc-gpl.json --spdx-map ${LA_FIXMAP}
    EXPECT_EXIT 1 EXPECT_MATCH "not on the allowlist")

# ...while the SAME identifier WITH the exception must pass. Without this control the
# previous case is satisfied by a tool that rejects everything.
rawpage_add_gate_test(NAME gates.license_audit.gcc_exception_is_allowed
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-gcc-exception.json --spdx-map ${LA_FIXMAP}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# LGPL-3.0 must not be caught by any "contains GPL" test.
rawpage_add_gate_test(NAME gates.license_audit.lgpl_is_allowed
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-lgpl.json --spdx-map ${LA_FIXMAP}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# Qt Charts is installed in this machine's Qt prefix, so this is a live hazard.
rawpage_add_gate_test(NAME gates.license_audit.qt_charts_link
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_MAP}
            --qt-modules ${FIXTURES}/license-audit/qt-modules-charts.txt
    EXPECT_EXIT 1 EXPECT_MATCH "GPL-3.0")

rawpage_add_gate_test(NAME gates.license_audit.qt_modules_allowed
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_MAP}
            --qt-modules ${FIXTURES}/license-audit/qt-modules-ok.txt
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

rawpage_add_gate_test(NAME gates.license_audit.compiler_plugin_loaded
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_MAP}
            --compiler-plugins ${FIXTURES}/license-audit/compiler-plugins-nonempty.txt
    EXPECT_EXIT 1 EXPECT_MATCH "Runtime Library Exception")

rawpage_add_gate_test(NAME gates.license_audit.missing_manifest_is_exit_2
    COMMAND ${LA} --manifest ${FIXTURES}/does-not-exist.json --spdx-map ${LA_MAP}
    EXPECT_EXIT 2 EXPECT_MATCH "cannot open")

# THIRD-PARTY.md is committed, so a gate run that cannot see the whole inventory must not
# rewrite it (I-08). Without --qt-modules and --vcpkg-installed the report loses every Qt
# row and every exact version, and the result still looks like a valid file -- which is why
# the pair below asserts the FILE, not just the message.
# One probe file per case: sharing a mutable path between two tests is a flake waiting for
# the first person who runs ctest with -j.
set(LA_EMIT_SKIP  "${CMAKE_CURRENT_BINARY_DIR}/emit-probe-skip.md")
set(LA_EMIT_WRITE "${CMAKE_CURRENT_BINARY_DIR}/emit-probe-write.md")

rawpage_add_gate_test(NAME gates.license_audit.emit_skipped_when_incomplete
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_MAP}
            --emit ${LA_EMIT_SKIP}
    EXPECT_EXIT 0 EXPECT_MATCH "left unchanged"
    EXPECT_ABSENT ${LA_EMIT_SKIP})

# I-17. The four NOTICE states in one emitted file. Rendered output had no coverage at all:
# "**MISSING**" meant both "a required notice is absent" -- a release blocker -- and "this
# configuration does not build it, so nothing was looked for", and the document an auditor reads
# is exactly where those two must not look alike.
set(LA_EMIT_STATES "${CMAKE_CURRENT_BINARY_DIR}/emit-notice-states.md")

rawpage_add_gate_test(NAME gates.license_audit.notice_states_render
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-notice-states.json
            --spdx-map ${LA_FIXMAP}
            --qt-modules ${FIXTURES}/license-audit/qt-modules-ok.txt
            --vcpkg-installed ${FIXTURES}/license-audit/installed-tree
            --emit ${LA_EMIT_STATES}
    # Exit 1, not 0: fixture-notice-absent is a REAL blocker -- Apache-2.0 with the installed
    # tree present and no NOTICE in it. The emission still happens, deliberately, so the reader
    # sees the whole inventory next to what failed.
    EXPECT_EXIT 1 EXPECT_MATCH "no NOTICE file"
    EXPECT_FILE ${LA_EMIT_STATES}
    EXPECT_FILE_MATCH
        "fixture-notice-present.*licence text and NOTICE"
        "fixture-notice-absent.*NOTICE MISSING"
        "fixture-notice-unbuilt.*NOTICE not checked"
        "fixture-lgpl-lib.*licence text"
        "Reading this table")

# NEGATIVE CONTROL: with both inputs present the emission must actually happen. Without
# this, the case above is satisfied by a tool that never writes anything at all.
rawpage_add_gate_test(NAME gates.license_audit.emit_writes_when_complete
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_MAP}
            --qt-modules ${FIXTURES}/license-audit/qt-modules-ok.txt
            --vcpkg-installed ${FIXTURES}/license-audit
            --emit ${LA_EMIT_WRITE}
    EXPECT_EXIT 0 EXPECT_MATCH "clean"
    EXPECT_FILE ${LA_EMIT_WRITE})

# --------------------------------------------------------------------------------------
# i18n-lint (Stage 1 T6) -- guide cases A-13 .. A-17
#
# The literal rule is a HEURISTIC, so it gets the same treatment layer-lint's write rule got after
# I-15: one fixture per SPELLING of the same defect, and a control file that must stay clean. A rule
# proved against one spelling catches one spelling; a rule with no control is equally satisfied by a
# tool that rejects everything.
# --------------------------------------------------------------------------------------
set(IL $<TARGET_FILE:i18n-lint>)
set(IL_RULES "${CMAKE_SOURCE_DIR}/tools/i18n-lint/rules.json")
set(IL_FIX "${FIXTURES}/i18n-lint")

# A-71: the real tree, with every input named explicitly so a missing one is exit 2.
rawpage_add_gate_test(NAME gates.i18n_lint.clean_tree
    COMMAND ${IL} --root ${CMAKE_SOURCE_DIR} --rules ${IL_RULES}
            --locales ${CMAKE_SOURCE_DIR}/i18n/locales.json
            --actions ${CMAKE_SOURCE_DIR}/core/cmd/ActionId.def
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# A-13. Five spellings of one defect, in one tree, so the count itself is asserted: a tool that
# found four of the five would otherwise pass this case.
rawpage_add_gate_test(NAME gates.i18n_lint.literal_spellings
    COMMAND ${IL} --root ${IL_FIX}/literal-spellings --rules ${IL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "5 finding")

# THE CONTROL for A-13: a wrapped literal, an object name, a catalog key, punctuation, and a
# mention in a comment must all stay legal.
rawpage_add_gate_test(NAME gates.i18n_lint.literal_controls_are_clean
    COMMAND ${IL} --root ${IL_FIX}/literal-controls --rules ${IL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# A-14.
rawpage_add_gate_test(NAME gates.i18n_lint.missing_key
    COMMAND ${IL} --root ${IL_FIX}/missing-key --rules ${IL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "missing-key: locale zh_TW has no entry for action.file.save")

# A-16.
rawpage_add_gate_test(NAME gates.i18n_lint.action_label
    COMMAND ${IL} --root ${IL_FIX}/action-label --rules ${IL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "action-label: no catalog entry for action.file.unnamed")

# orphan-key is S3: REPORTED and never blocking. The exit code is the whole assertion -- a key that
# leads its use by a commit must not fail a build.
rawpage_add_gate_test(NAME gates.i18n_lint.orphan_key_is_a_note_not_a_failure
    COMMAND ${IL} --root ${IL_FIX}/orphan-key --rules ${IL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "note: orphan-key: action.file.longGone")

# A-17, transplanted from Stage 0's I-20. The waiver must waive...
rawpage_add_gate_test(NAME gates.i18n_lint.waiver_applies
    COMMAND ${IL} --root ${IL_FIX}/waiver-applied --rules ${IL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# ...and the three shapes that are NOT applied waivers must not.
rawpage_add_gate_test(NAME gates.i18n_lint.quoted_waiver_does_not_waive
    COMMAND ${IL} --root ${IL_FIX}/waiver-quoted --rules ${IL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "unwrapped-literal")

rawpage_add_gate_test(NAME gates.i18n_lint.unknown_waiver_kind_does_not_waive
    COMMAND ${IL} --root ${IL_FIX}/waiver-unknown-kind --rules ${IL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "unwrapped-literal")

rawpage_add_gate_test(NAME gates.i18n_lint.waiver_without_code_does_not_waive
    COMMAND ${IL} --root ${IL_FIX}/waiver-no-code --rules ${IL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "unwrapped-literal")

# A-15 (D1-14). The unshipped locales must be listed WITH their reason: a silent skip is
# indistinguishable from a locale somebody forgot, which is the distinction D1-14 exists to keep.
rawpage_add_gate_test(NAME gates.i18n_lint.explain_names_unshipped_locales
    COMMAND ${IL} --root ${CMAKE_SOURCE_DIR} --rules ${IL_RULES} --explain
    EXPECT_EXIT 0 EXPECT_MATCH "zh_CN  declared, NOT shipped -- Stage 15")

# A gate that could not run must not look like a gate that passed -- for each of its three inputs.
rawpage_add_gate_test(NAME gates.i18n_lint.missing_rules_is_exit_2
    COMMAND ${IL} --root ${CMAKE_SOURCE_DIR} --rules ${FIXTURES}/does-not-exist.json
    EXPECT_EXIT 2 EXPECT_MATCH "cannot open")

rawpage_add_gate_test(NAME gates.i18n_lint.missing_locales_is_exit_2
    COMMAND ${IL} --root ${CMAKE_SOURCE_DIR} --rules ${IL_RULES}
            --locales ${FIXTURES}/does-not-exist.json
    EXPECT_EXIT 2 EXPECT_MATCH "cannot open")

# A locale declared SHIPPED whose catalog is absent is exit 2, not a warning: it is the state
# missing-key exists to prevent, arrived at by a route that would otherwise report zero keys missing.
rawpage_add_gate_test(NAME gates.i18n_lint.shipped_locale_without_catalog_is_exit_2
    COMMAND ${IL} --root ${IL_FIX}/shipped-catalog-missing --rules ${IL_RULES}
    EXPECT_EXIT 2 EXPECT_MATCH "declared shipped but its catalog is missing")

# --------------------------------------------------------------------------------------
# The toolchain pin (TA-1 .. TA-9), against a minimal fixture project.
#
# These were written and passing by hand at T3; wiring them here is what the plan assigns
# to T9, and it is what makes the testing guide's `ctest -R gates.wrong_compiler` real.
#
# Each drives the compiler to REPORT the wrong thing, which tests the assertion's logic and
# its message. None of them reproduces the SCENARIO -- a genuinely different toolchain on
# the machine -- which stays with manual case M-02. See the fixture README for why both
# halves matter.
# --------------------------------------------------------------------------------------
# Windows only: these configure against cmake/toolchain-mingw.cmake, which pins a
# Windows compiler by definition. On the Linux gates runner there is nothing to pin, and a
# case that cannot run must be absent rather than failing -- a red CI job that everyone
# learns to ignore protects nothing.
if(WIN32)

set(WC_PROJECT "${FIXTURES}/wrong-compiler/project")
set(WC_TOOLCHAIN "${CMAKE_SOURCE_DIR}/cmake/toolchain-mingw.cmake")

function(rawpage_add_toolchain_test)
    cmake_parse_arguments(TC "" "NAME;EXPECT_MATCH;EXTRA" "" ${ARGN})
    rawpage_add_gate_test(NAME ${TC_NAME}
        COMMAND ${CMAKE_COMMAND} --fresh
                -S ${WC_PROJECT}
                -B ${CMAKE_BINARY_DIR}/gate-fixtures/${TC_NAME}
                -G Ninja
                -DCMAKE_TOOLCHAIN_FILE=${WC_TOOLCHAIN}
                -DRAWPAGE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                ${TC_EXTRA}
        EXPECT_EXIT 1 EXPECT_MATCH "${TC_EXPECT_MATCH}")
endfunction()

# The fixture must configure cleanly on the real toolchain, or every case below could be
# passing for the wrong reason.
rawpage_add_gate_test(NAME gates.wrong_compiler.baseline_is_clean
    COMMAND ${CMAKE_COMMAND} --fresh -S ${WC_PROJECT}
            -B ${CMAKE_BINARY_DIR}/gate-fixtures/baseline -G Ninja
            -DCMAKE_TOOLCHAIN_FILE=${WC_TOOLCHAIN}
            -DRAWPAGE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    EXPECT_EXIT 0 EXPECT_MATCH "all toolchain assertions passed")

# WC-1 -- TA-2, wrong compiler version.
# The EXPECT_MATCH deliberately reaches the END of the fix sentence. Matching only the
# label "TA-2 compiler version" passed happily while the advice after the first quoted
# fragment was being dropped on the floor (I-09) -- a gate whose message is half a sentence
# is the failure this file exists to prevent.
#
# It matches a SINGLE WORD, not a phrase: message(FATAL_ERROR) hard-wraps its text, so any
# multi-word pattern can be split across a line break and fail for a reason that has nothing
# to do with the diagnostic.
rawpage_add_toolchain_test(NAME gates.wrong_compiler.wc1_wrong_version
    EXTRA "-DCMAKE_CXX_FLAGS=-U__GNUC_MINOR__ -D__GNUC_MINOR__=9"
    EXPECT_MATCH "together")

# WC-2 -- TA-6, a UCRT runtime. The assertion that actually catches an MSYS2 install.
rawpage_add_toolchain_test(NAME gates.wrong_compiler.wc2_ucrt_runtime
    EXTRA "-DCMAKE_CXX_FLAGS=-D_UCRT"
    EXPECT_MATCH "corrupts")

# WC-3 -- TA-6 must require __MSVCRT__ to be PRESENT, not merely _UCRT to be absent,
# or it passes vacuously whenever its own probe fails.
rawpage_add_toolchain_test(NAME gates.wrong_compiler.wc3_probe_self_check
    EXTRA "-DCMAKE_CXX_FLAGS=-U__MSVCRT__"
    EXPECT_MATCH "__MSVCRT__ is not defined")

# WC-4 -- the pinned root points nowhere.
rawpage_add_toolchain_test(NAME gates.wrong_compiler.wc4_bad_mingw_root
    EXTRA "-DRAWPAGE_MINGW_ROOT=C:/nonexistent-gcc"
    # Deliberately avoids the "g++" in the message: EXPECT_MATCH is a CMake regex, and a
    # bare '+' there is a quantifier with nothing to repeat -- it fails to COMPILE rather
    # than fails to match, which reads like a broken gate instead of a broken pattern.
    EXPECT_MATCH "under RAWPAGE_MINGW_ROOT")

# WC-5 -- a GCC plugin voids the Runtime Library Exception.
rawpage_add_toolchain_test(NAME gates.wrong_compiler.wc5_gcc_plugin
    EXTRA "-DCMAKE_CXX_FLAGS=-fplugin=evil.dll"
    EXPECT_MATCH "Runtime Library Exception")

# WC-6 -- static runtime linking breaches the LGPL replaceability obligation.
rawpage_add_toolchain_test(NAME gates.wrong_compiler.wc6_static_runtime
    EXTRA "-DCMAKE_CXX_FLAGS=-static-libstdc++"
    EXPECT_MATCH "static")

# --------------------------------------------------------------------------------------
# TA-8 and the Qt prefix (I-11).
#
# M-03 pointed RAWPAGE_QT_PREFIX at a path that does not exist and was told "a second Qt
# install won the search" -- true of the code path, false of the situation, and it sends
# the reader after a rival install that is not there. Each case below asserts a word from
# the END of its sentence, for the reason wc1/wc2 do (I-09).
# --------------------------------------------------------------------------------------

# The toolchain file refuses a missing prefix outright, before Qt is ever searched.
rawpage_add_toolchain_test(NAME gates.wrong_compiler.qt_prefix_missing
    EXTRA "-DRAWPAGE_QT_PREFIX=C:/nonexistent-qt"
    EXPECT_MATCH "mingw_64")

# I-10 -- the compiler is PRESENT but cannot run. fake-root/bin/g++.exe is a text file, which
# is deterministic where a second real toolchain would depend on what the machine happens to
# have installed. Without the preflight probe this surfaces as CMake's "not able to compile a
# simple test program", with no compiler output at all.
rawpage_add_toolchain_test(NAME gates.wrong_compiler.compiler_cannot_run
    EXTRA "-DRAWPAGE_MINGW_ROOT=${FIXTURES}/wrong-compiler/fake-root"
    EXPECT_MATCH "cc1plus")

# TA-8's two prefix branches, with Qt6_VERSION and Qt6_DIR injected -- testing them for
# real would need a second Qt installed, which is M-03's job, not a unit test's.
# Follow the pin rather than restating it. RAWPAGE_QT_PREFIX is a cache variable of the
# toolchain file, which the `gates` configuration does not load -- hence the fallback.
if(DEFINED RAWPAGE_QT_PREFIX AND NOT RAWPAGE_QT_PREFIX STREQUAL "")
    set(WC_QT_PREFIX "${RAWPAGE_QT_PREFIX}")
else()
    set(WC_QT_PREFIX "C:/Qt/6.11.1/mingw_64")
endif()
set(WC_QT_DIR "${WC_QT_PREFIX}/lib/cmake/Qt6")

# Branch A: the prefix EXISTS but a different Qt won. Must NOT be the "does not exist" text.
rawpage_add_toolchain_test(NAME gates.wrong_compiler.qt_prefix_other_install
    EXTRA "-DRAWPAGE_FIXTURE_ASSERT_QT=ON|-DQt6_VERSION=6.11.1|-DQt6_DIR=${WC_QT_DIR}|-DRAWPAGE_QT_PREFIX=C:/Qt/Tools"
    EXPECT_MATCH "CMAKE_PREFIX_PATH")

# TA-8's version branch -- the one that catches a Qt upgrade, which is exactly how a 6.11.2
# install presents itself.
rawpage_add_toolchain_test(NAME gates.wrong_compiler.qt_version_mismatch
    EXTRA "-DRAWPAGE_FIXTURE_ASSERT_QT=ON|-DQt6_VERSION=6.11.2|-DQt6_DIR=${WC_QT_DIR}"
    EXPECT_MATCH "same change")

# I-13 -- an unset VCPKG_ROOT. The presets build CMAKE_TOOLCHAIN_FILE out of it, and an
# undefined $env{} expands to empty, so CMake reports a toolchain file at
# "/scripts/buildsystems/vcpkg.cmake" -- a path that never existed, naming neither the
# variable nor the fix. `cmake -E env --unset=` is what makes this testable at all.
rawpage_add_gate_test(NAME gates.wrong_compiler.vcpkg_root_unset
    COMMAND ${CMAKE_COMMAND} -E env --unset=VCPKG_ROOT
            ${CMAKE_COMMAND} --fresh -S ${WC_PROJECT}
            -B ${CMAKE_BINARY_DIR}/gate-fixtures/vcpkg_root_unset -G Ninja
            -DCMAKE_TOOLCHAIN_FILE=${WC_TOOLCHAIN}
            -DRAWPAGE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DRAWPAGE_FIXTURE_VCPKG=ON
    EXPECT_EXIT 1 EXPECT_MATCH "bootstrap.ps1")

# NEGATIVE CONTROL: a VCPKG_ROOT that is set but points at no clone must say something
# different from the unset case, and must name the path it looked for.
rawpage_add_gate_test(NAME gates.wrong_compiler.vcpkg_root_bogus
    COMMAND ${CMAKE_COMMAND} -E env VCPKG_ROOT=C:/nonexistent-vcpkg
            ${CMAKE_COMMAND} --fresh -S ${WC_PROJECT}
            -B ${CMAKE_BINARY_DIR}/gate-fixtures/vcpkg_root_bogus -G Ninja
            -DCMAKE_TOOLCHAIN_FILE=${WC_TOOLCHAIN}
            -DRAWPAGE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DRAWPAGE_FIXTURE_VCPKG=ON
    EXPECT_EXIT 1 EXPECT_MATCH "nonexistent-vcpkg")

endif()  # WIN32 -- toolchain pin cases
