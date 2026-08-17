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
    cmake_parse_arguments(ARG "" "NAME;EXPECT_EXIT;EXPECT_MATCH" "COMMAND" ${ARGN})
    string(REPLACE ";" " " command_str "${ARG_COMMAND}")
    add_test(NAME ${ARG_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DCOMMAND_STR=${command_str}"
                "-DEXPECT_EXIT=${ARG_EXPECT_EXIT}"
                "-DEXPECT_MATCH=${ARG_EXPECT_MATCH}"
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
