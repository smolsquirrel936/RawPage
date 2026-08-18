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

function(recto_add_gate_test)
    cmake_parse_arguments(ARG "" "NAME;EXPECT_EXIT;EXPECT_MATCH;EXPECT_FILE;EXPECT_ABSENT" "COMMAND" ${ARGN})
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
                -P "${RUNNER}")
endfunction()

# --------------------------------------------------------------------------------------
# layer-lint
# --------------------------------------------------------------------------------------
set(LL $<TARGET_FILE:layer-lint>)
set(LL_RULES "${CMAKE_SOURCE_DIR}/tools/layer-lint/rules.json")

recto_add_gate_test(NAME gates.layer_lint.clean_tree
    COMMAND ${LL} --root ${CMAKE_SOURCE_DIR}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

recto_add_gate_test(NAME gates.layer_lint.outward_include
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/outward-include --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "outward-include")

recto_add_gate_test(NAME gates.layer_lint.ui_includes_pdf
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/ui-includes-pdf --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "outward-include")

recto_add_gate_test(NAME gates.layer_lint.fpdf_outside_pdf
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/fpdf-outside-pdf --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "fpdf")

recto_add_gate_test(NAME gates.layer_lint.write_outside_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/write-outside-allowed --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "filesystem-write")

recto_add_gate_test(NAME gates.layer_lint.qtwidgets_in_core
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/qtwidgets-in-core --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "external-include")

# NEGATIVE CONTROL: the inline waiver must actually waive.
recto_add_gate_test(NAME gates.layer_lint.write_escaped_is_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/write-escaped --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# A gate that could not run must not look like a gate that passed.
recto_add_gate_test(NAME gates.layer_lint.missing_rules_is_exit_2
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

recto_add_gate_test(NAME gates.license_audit.gpl_dependency
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-gpl.json --spdx-map ${LA_FIXMAP}
    EXPECT_EXIT 1 EXPECT_MATCH "not on the allowlist")

# THE SUBSTRING TEST. Bare GPL-3.0-or-later must fail...
recto_add_gate_test(NAME gates.license_audit.bare_gpl_without_exception
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-bare-gcc-gpl.json --spdx-map ${LA_FIXMAP}
    EXPECT_EXIT 1 EXPECT_MATCH "not on the allowlist")

# ...while the SAME identifier WITH the exception must pass. Without this control the
# previous case is satisfied by a tool that rejects everything.
recto_add_gate_test(NAME gates.license_audit.gcc_exception_is_allowed
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-gcc-exception.json --spdx-map ${LA_FIXMAP}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# LGPL-3.0 must not be caught by any "contains GPL" test.
recto_add_gate_test(NAME gates.license_audit.lgpl_is_allowed
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-lgpl.json --spdx-map ${LA_FIXMAP}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# Qt Charts is installed in this machine's Qt prefix, so this is a live hazard.
recto_add_gate_test(NAME gates.license_audit.qt_charts_link
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_MAP}
            --qt-modules ${FIXTURES}/license-audit/qt-modules-charts.txt
    EXPECT_EXIT 1 EXPECT_MATCH "GPL-3.0")

recto_add_gate_test(NAME gates.license_audit.qt_modules_allowed
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_MAP}
            --qt-modules ${FIXTURES}/license-audit/qt-modules-ok.txt
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

recto_add_gate_test(NAME gates.license_audit.compiler_plugin_loaded
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_MAP}
            --compiler-plugins ${FIXTURES}/license-audit/compiler-plugins-nonempty.txt
    EXPECT_EXIT 1 EXPECT_MATCH "Runtime Library Exception")

recto_add_gate_test(NAME gates.license_audit.missing_manifest_is_exit_2
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

recto_add_gate_test(NAME gates.license_audit.emit_skipped_when_incomplete
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_MAP}
            --emit ${LA_EMIT_SKIP}
    EXPECT_EXIT 0 EXPECT_MATCH "left unchanged"
    EXPECT_ABSENT ${LA_EMIT_SKIP})

# NEGATIVE CONTROL: with both inputs present the emission must actually happen. Without
# this, the case above is satisfied by a tool that never writes anything at all.
recto_add_gate_test(NAME gates.license_audit.emit_writes_when_complete
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_MAP}
            --qt-modules ${FIXTURES}/license-audit/qt-modules-ok.txt
            --vcpkg-installed ${FIXTURES}/license-audit
            --emit ${LA_EMIT_WRITE}
    EXPECT_EXIT 0 EXPECT_MATCH "clean"
    EXPECT_FILE ${LA_EMIT_WRITE})

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

function(recto_add_toolchain_test)
    cmake_parse_arguments(TC "" "NAME;EXPECT_MATCH;EXTRA" "" ${ARGN})
    recto_add_gate_test(NAME ${TC_NAME}
        COMMAND ${CMAKE_COMMAND} --fresh
                -S ${WC_PROJECT}
                -B ${CMAKE_BINARY_DIR}/gate-fixtures/${TC_NAME}
                -G Ninja
                -DCMAKE_TOOLCHAIN_FILE=${WC_TOOLCHAIN}
                -DRECTO_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                ${TC_EXTRA}
        EXPECT_EXIT 1 EXPECT_MATCH "${TC_EXPECT_MATCH}")
endfunction()

# The fixture must configure cleanly on the real toolchain, or every case below could be
# passing for the wrong reason.
recto_add_gate_test(NAME gates.wrong_compiler.baseline_is_clean
    COMMAND ${CMAKE_COMMAND} --fresh -S ${WC_PROJECT}
            -B ${CMAKE_BINARY_DIR}/gate-fixtures/baseline -G Ninja
            -DCMAKE_TOOLCHAIN_FILE=${WC_TOOLCHAIN}
            -DRECTO_SOURCE_DIR=${CMAKE_SOURCE_DIR}
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
recto_add_toolchain_test(NAME gates.wrong_compiler.wc1_wrong_version
    EXTRA "-DCMAKE_CXX_FLAGS=-U__GNUC_MINOR__ -D__GNUC_MINOR__=9"
    EXPECT_MATCH "together")

# WC-2 -- TA-6, a UCRT runtime. The assertion that actually catches an MSYS2 install.
recto_add_toolchain_test(NAME gates.wrong_compiler.wc2_ucrt_runtime
    EXTRA "-DCMAKE_CXX_FLAGS=-D_UCRT"
    EXPECT_MATCH "corrupts")

# WC-3 -- TA-6 must require __MSVCRT__ to be PRESENT, not merely _UCRT to be absent,
# or it passes vacuously whenever its own probe fails.
recto_add_toolchain_test(NAME gates.wrong_compiler.wc3_probe_self_check
    EXTRA "-DCMAKE_CXX_FLAGS=-U__MSVCRT__"
    EXPECT_MATCH "__MSVCRT__ is not defined")

# WC-4 -- the pinned root points nowhere.
recto_add_toolchain_test(NAME gates.wrong_compiler.wc4_bad_mingw_root
    EXTRA "-DRECTO_MINGW_ROOT=C:/nonexistent-gcc"
    # Deliberately avoids the "g++" in the message: EXPECT_MATCH is a CMake regex, and a
    # bare '+' there is a quantifier with nothing to repeat -- it fails to COMPILE rather
    # than fails to match, which reads like a broken gate instead of a broken pattern.
    EXPECT_MATCH "under RECTO_MINGW_ROOT")

# WC-5 -- a GCC plugin voids the Runtime Library Exception.
recto_add_toolchain_test(NAME gates.wrong_compiler.wc5_gcc_plugin
    EXTRA "-DCMAKE_CXX_FLAGS=-fplugin=evil.dll"
    EXPECT_MATCH "Runtime Library Exception")

# WC-6 -- static runtime linking breaches the LGPL replaceability obligation.
recto_add_toolchain_test(NAME gates.wrong_compiler.wc6_static_runtime
    EXTRA "-DCMAKE_CXX_FLAGS=-static-libstdc++"
    EXPECT_MATCH "static")

# --------------------------------------------------------------------------------------
# TA-8 and the Qt prefix (I-11).
#
# M-03 pointed RECTO_QT_PREFIX at a path that does not exist and was told "a second Qt
# install won the search" -- true of the code path, false of the situation, and it sends
# the reader after a rival install that is not there. Each case below asserts a word from
# the END of its sentence, for the reason wc1/wc2 do (I-09).
# --------------------------------------------------------------------------------------

# The toolchain file refuses a missing prefix outright, before Qt is ever searched.
recto_add_toolchain_test(NAME gates.wrong_compiler.qt_prefix_missing
    EXTRA "-DRECTO_QT_PREFIX=C:/nonexistent-qt"
    EXPECT_MATCH "mingw_64")

# I-10 -- the compiler is PRESENT but cannot run. fake-root/bin/g++.exe is a text file, which
# is deterministic where a second real toolchain would depend on what the machine happens to
# have installed. Without the preflight probe this surfaces as CMake's "not able to compile a
# simple test program", with no compiler output at all.
recto_add_toolchain_test(NAME gates.wrong_compiler.compiler_cannot_run
    EXTRA "-DRECTO_MINGW_ROOT=${FIXTURES}/wrong-compiler/fake-root"
    EXPECT_MATCH "cc1plus")

# TA-8's two prefix branches, with Qt6_VERSION and Qt6_DIR injected -- testing them for
# real would need a second Qt installed, which is M-03's job, not a unit test's.
# Follow the pin rather than restating it. RECTO_QT_PREFIX is a cache variable of the
# toolchain file, which the `gates` configuration does not load -- hence the fallback.
if(DEFINED RECTO_QT_PREFIX AND NOT RECTO_QT_PREFIX STREQUAL "")
    set(WC_QT_PREFIX "${RECTO_QT_PREFIX}")
else()
    set(WC_QT_PREFIX "C:/Qt/6.11.1/mingw_64")
endif()
set(WC_QT_DIR "${WC_QT_PREFIX}/lib/cmake/Qt6")

# Branch A: the prefix EXISTS but a different Qt won. Must NOT be the "does not exist" text.
recto_add_toolchain_test(NAME gates.wrong_compiler.qt_prefix_other_install
    EXTRA "-DRECTO_FIXTURE_ASSERT_QT=ON|-DQt6_VERSION=6.11.1|-DQt6_DIR=${WC_QT_DIR}|-DRECTO_QT_PREFIX=C:/Qt/Tools"
    EXPECT_MATCH "CMAKE_PREFIX_PATH")

# TA-8's version branch -- the one that catches a Qt upgrade, which is exactly how a 6.11.2
# install presents itself.
recto_add_toolchain_test(NAME gates.wrong_compiler.qt_version_mismatch
    EXTRA "-DRECTO_FIXTURE_ASSERT_QT=ON|-DQt6_VERSION=6.11.2|-DQt6_DIR=${WC_QT_DIR}"
    EXPECT_MATCH "same change")

# I-13 -- an unset VCPKG_ROOT. The presets build CMAKE_TOOLCHAIN_FILE out of it, and an
# undefined $env{} expands to empty, so CMake reports a toolchain file at
# "/scripts/buildsystems/vcpkg.cmake" -- a path that never existed, naming neither the
# variable nor the fix. `cmake -E env --unset=` is what makes this testable at all.
recto_add_gate_test(NAME gates.wrong_compiler.vcpkg_root_unset
    COMMAND ${CMAKE_COMMAND} -E env --unset=VCPKG_ROOT
            ${CMAKE_COMMAND} --fresh -S ${WC_PROJECT}
            -B ${CMAKE_BINARY_DIR}/gate-fixtures/vcpkg_root_unset -G Ninja
            -DCMAKE_TOOLCHAIN_FILE=${WC_TOOLCHAIN}
            -DRECTO_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DRECTO_FIXTURE_VCPKG=ON
    EXPECT_EXIT 1 EXPECT_MATCH "bootstrap.ps1")

# NEGATIVE CONTROL: a VCPKG_ROOT that is set but points at no clone must say something
# different from the unset case, and must name the path it looked for.
recto_add_gate_test(NAME gates.wrong_compiler.vcpkg_root_bogus
    COMMAND ${CMAKE_COMMAND} -E env VCPKG_ROOT=C:/nonexistent-vcpkg
            ${CMAKE_COMMAND} --fresh -S ${WC_PROJECT}
            -B ${CMAKE_BINARY_DIR}/gate-fixtures/vcpkg_root_bogus -G Ninja
            -DCMAKE_TOOLCHAIN_FILE=${WC_TOOLCHAIN}
            -DRECTO_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DRECTO_FIXTURE_VCPKG=ON
    EXPECT_EXIT 1 EXPECT_MATCH "nonexistent-vcpkg")

endif()  # WIN32 -- toolchain pin cases
