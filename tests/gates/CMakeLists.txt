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
    cmake_parse_arguments(ARG "" "NAME;EXPECT_EXIT;EXPECT_MATCH;EXPECT_NOT_MATCH;EXPECT_FILE;EXPECT_ABSENT" "COMMAND;EXPECT_FILE_MATCH" ${ARGN})
    # EVERY EXPECT_MATCH AND EVERY EXPECT_NOT_MATCH, not the last (S5 guide I-09). They are one-value keywords
    # to `cmake_parse_arguments`, which keeps only the LAST of a repeated keyword -- so a case that named two
    # diagnostics asserted one, and nothing said so. Each value is read here from `ARGV<n>`, which holds the
    # argument whole (a pattern may contain `;`, which a list would split), and joined by @@ for the runner.
    set(match_str "")
    set(not_match_str "")
    math(EXPR last_index "${ARGC} - 1")
    foreach(i RANGE 0 ${last_index})
        math(EXPR value_index "${i} + 1")
        if(value_index GREATER last_index)
            break()
        endif()
        if("${ARGV${i}}" STREQUAL "EXPECT_MATCH")
            string(APPEND match_str "${ARGV${value_index}}@@")
        elseif("${ARGV${i}}" STREQUAL "EXPECT_NOT_MATCH")
            string(APPEND not_match_str "${ARGV${value_index}}@@")
        endif()
    endforeach()
    # Patterns travel joined by @@, because | is already the argument separator below.
    string(REPLACE ";" "@@" file_match_str "${ARG_EXPECT_FILE_MATCH}")
    # '|' rather than ' ' as the separator: a toolchain case passes an argument that itself
    # contains spaces, and joining on space would split it back into two.
    string(REPLACE ";" "|" command_str "${ARG_COMMAND}")
    add_test(NAME ${ARG_NAME}
        COMMAND ${CMAKE_COMMAND}
                "-DCOMMAND_STR=${command_str}"
                "-DEXPECT_EXIT=${ARG_EXPECT_EXIT}"
                "-DEXPECT_MATCH=${match_str}"
                "-DEXPECT_NOT_MATCH=${not_match_str}"
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

# --- D2-9's narrowing, Stage 2 T2 ------------------------------------------------------
# The rule above has said "not outside pdf/" since Stage 0. The four cases here are about the line
# moving INWARD: pdf/cos/ is first-party by rule -- the half of D3 that never learns the engine
# exists, and half of what portable-asan and portable-fuzz build on Linux where PDFium is not there
# at all -- and it lives inside the directory the old rule allowed.
#
# A-10: the call, inside pdf/, which used to pass clean.
rawpage_add_gate_test(NAME gates.layer_lint.fpdf_in_cos
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/fpdf-in-cos --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "fpdf")

# A-12: the HANDLE, in core/, with three spellings in one struct. No FPDF_ call appears anywhere in
# that fixture -- which is the point: a type can hold the engine's object graph without ever calling
# it, and that is the leak the boundary exists to prevent.
# --- T16's widening, and the narrowing that is the point of it -------------------------
# `pdf/render/` may include platform/platform.h from Stage 2 T16, because FontResolverBridge is
# where the engine boundary and the OS boundary physically meet -- PDFium's font hook asks, and
# platform::IFontResolver answers (guide case A-66). The widening is on a `pdf-render` layer entry
# rather than on `pdf`, so that pdf/cos/ does NOT inherit it: that layer is portable BY RULE and is
# half of what portable-asan and portable-fuzz build on Linux.
#
# The two cases below are the same shape as D2-9's pair above, and for the same reason: a rules file
# that had put the permission on `pdf` instead of on `pdf-render` passes the control and fails
# nothing, so the control alone would not notice.
rawpage_add_gate_test(NAME gates.layer_lint.cos_includes_platform
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/cos-includes-platform --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "platform/platform.h")

# --- D3-3's `pdf-cos` entry, Stage 3 T3 (guide case A-05) --------------------------------
# pdf/cos/ is the bottom of pdf/: the mutation layer (pdf/ops/) is built on it and the facade
# (pdf/render/) beside it, and neither may be included from it. The two positives were run against a
# rules file WITHOUT the entry and both came back clean, so they fail for this rule and no other
# (convention 7). The control is the permitted direction, without which both positives are equally
# satisfied by a rules file that forbade pdf/cos/ headers to everyone.
rawpage_add_gate_test(NAME gates.layer_lint.pdf_cos_includes_ops
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/cos-includes-ops --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "pdf-cos/ includes pdf/ops/")

rawpage_add_gate_test(NAME gates.layer_lint.pdf_cos_includes_render
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/cos-includes-render --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "pdf-cos/ includes pdf/render/")

rawpage_add_gate_test(NAME gates.layer_lint.pdf_cos_ops_may_include_cos
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/ops-may-include-cos --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

rawpage_add_gate_test(NAME gates.layer_lint.pdf_render_may_include_platform
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/pdf-render-may-include-platform --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

rawpage_add_gate_test(NAME gates.layer_lint.fpdf_handle_outside_render
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/fpdf-handle-outside-render --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "fpdf-handle")

# A-12 again, one directory deeper and a fourth spelling: a first-party xref table that knows what
# an FPDF_DOCUMENT is has stopped being first-party.
rawpage_add_gate_test(NAME gates.layer_lint.fpdf_handle_in_cos
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/fpdf-handle-in-cos --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "fpdf-handle")

# THE SPELLING THAT IS NOT REDUNDANT, and the reason the handle rule is worth having at all: PDFium's
# own include/cpp/ defines ScopedFPDFDocument and the FPDFDocumentDeleter family, which contain no
# underscore after FPDF and are therefore invisible to a search for `FPDF_`. Every other handle
# pattern is caught twice; this one is caught once, by this rule, or not at all.
rawpage_add_gate_test(NAME gates.layer_lint.fpdf_scoper_in_core
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/fpdf-scoper-in-core --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "fpdf-handle")

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

# Stage 3 T7 (guide A-09): the three Win32 calls platform/win/WinDurableFiles makes, each spelled in a
# different layer, and a control naming all three in comments. One fixture per spelling, because a
# pattern list tested against one spelling catches one spelling (I-15).
rawpage_add_gate_test(NAME gates.layer_lint.win32_replace_outside_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/win32-replace-write --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "filesystem-write: found 'ReplaceFileW")

rawpage_add_gate_test(NAME gates.layer_lint.win32_move_outside_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/win32-move-write --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "filesystem-write: found 'MoveFileExW")

rawpage_add_gate_test(NAME gates.layer_lint.win32_delete_outside_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/win32-delete-write --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "filesystem-write: found 'DeleteFileW")

# Stage 4 guide I-31: the rename WinDurableFiles makes by handle since ReplaceFileW left it.
rawpage_add_gate_test(NAME gates.layer_lint.win32_setinfo_outside_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/win32-setinfo-write --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "filesystem-write: found 'SetFileInformationByHandle")

rawpage_add_gate_test(NAME gates.layer_lint.win32_write_in_comment_is_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/win32-write-in-comment --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

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

# Stage 2 T1. The same treatment for the OTHER interface header -- and the fixture is deliberately
# about the ONE thing the core-backend entry adds that the core entry did not already give it.
#
# THE FIRST VERSION OF THIS CASE WAS WORTHLESS AND IS WORTH RECORDING AS SUCH. It put an fpdfview.h
# include in core/backend.h and asserted a failure -- which the `core` layer has denied since Stage 0,
# so it passed with the new entry deleted. A fixture that cannot fail for the reason it claims to
# test is convention 7's defect, and it is exactly the shape of Stage 0's I-15.
#
# What core-backend actually narrows is the outward look: core/ at large may include
# platform/platform.h and the engine contract may not. The fixture's own core/settings/Store.cpp
# includes the same header legitimately, so the failure is attributable to WHICH file reached for the
# OS boundary rather than to the include being denied everywhere under core/.
rawpage_add_gate_test(NAME gates.layer_lint.backend_header_includes_platform
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/backend-header-includes-platform --rules ${LL_RULES}
    EXPECT_EXIT 1 EXPECT_MATCH "outward-include")

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

# THE MISSING CONTROL FOR THE fpdf RULE (Stage 2 T1). `fpdf_outside_pdf` above has proved since
# Stage 0 that naming PDFium outside pdf/ fails, and nothing has ever proved that naming it INSIDE
# pdf/ succeeds -- so that case was equally satisfied by a gate rejecting every mention of PDFium in
# the tree, which would make the facade unable to do its only job. The fixture also has a
# core/backend.h holding buffers and interfaces, which is what the real one looks like: the engine
# contract and the engine, in one tree, with the line between them where it belongs.
rawpage_add_gate_test(NAME gates.layer_lint.pdf_impl_may_include_pdfium
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/pdf-impl-may-include-pdfium --rules ${LL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "clean")

# A-11 -- THE CONTROL FOR THE NARROWED RULE (Stage 2 T2). pdf/render/ is the facade, and its only
# job is to call the engine: four call spellings and, in the tree above it, the handles it owns.
# Without this case, "FPDF_ outside pdf/render/ fails" is equally satisfied by a rule that rejects
# PDFium everywhere -- which is not a stricter version of the rule, it is a rule that forbids the
# product.
rawpage_add_gate_test(NAME gates.layer_lint.fpdf_render_allowed
    COMMAND ${LL} --root ${FIXTURES}/layer-lint/fpdf-render-allowed --rules ${LL_RULES}
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

# --- I-02: the gate audits the RESOLVED CLOSURE, not the declared manifest ------------
# Guide case A-75. Before Stage 2 T7, `Inventory.cpp` built its list from vcpkg.json's
# `dependencies` and `features` arrays plus the non-vcpkg overrides, and never enumerated what
# vcpkg actually installs -- so `brotli`, `bzip2`, `liblzma` and `libiconv`, all of which the
# `codecs` and `text` features pull in, had no licence verdict at all. Nothing found was GPL, so
# there was no breach; the problem was that the gate's documented claim was broader than its
# behaviour, and Stage 15's release compliance gate is built directly on that claim.
#
# The fixture tree holds ONE port that vcpkg.json never names. The manifest is the empty one.
rawpage_add_gate_test(NAME gates.license_audit.undeclared_transitive_gpl
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_FIXMAP}
            --vcpkg-installed ${FIXTURES}/license-audit/installed-tree-transitive
    EXPECT_EXIT 1 EXPECT_MATCH "fixture-undeclared-gpl")

# THE CONTROL, and it is the half that makes the case above mean something: the same shape of
# tree with a permissive licence must PASS. Without it, the case is equally satisfied by a tool
# that fails on any installed tree at all -- a worse defect than the one being repaired.
# It matches the COMPONENT COUNT rather than the port name because a clean run prints a summary
# rather than a roster (license-audit has no --explain; that is layer-lint's and i18n-lint's). The
# count is still the discriminator that matters: this manifest declares NOTHING, so the run
# reported "0 components" before the closure loop existed and reports "1 components" after. A
# control that only checked exit 0 would have passed either way.
rawpage_add_gate_test(NAME gates.license_audit.undeclared_transitive_permissive
    COMMAND ${LA} --manifest ${FIXTURES}/license-audit/manifest-minimal.json --spdx-map ${LA_FIXMAP}
            --vcpkg-installed ${FIXTURES}/license-audit/installed-tree-permissive
    EXPECT_EXIT 0 EXPECT_MATCH "1 components")

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

# S5 T7 -- guide issue I-05's rule, BLOCKING. One misplaced entry and two in their own contexts: the
# fixture reports the one and NOT the two, so a rule that fired on every entry fails it as surely as one
# that fired on none.
rawpage_add_gate_test(NAME gates.i18n_lint.wrong_context
    COMMAND ${IL} --root ${IL_FIX}/wrong-context --rules ${IL_RULES}
    EXPECT_EXIT 1
    EXPECT_MATCH "wrong-context: search.bar.count is in context prefs, not search"
    EXPECT_NOT_MATCH "wrong-context: prefs.setting"
    EXPECT_NOT_MATCH "wrong-context: search.bar.noMatches")

# orphan-key is S3: REPORTED and never blocking. The exit code is the whole assertion -- a key that
# leads its use by a commit must not fail a build.
rawpage_add_gate_test(NAME gates.i18n_lint.orphan_key_is_a_note_not_a_failure
    COMMAND ${IL} --root ${IL_FIX}/orphan-key --rules ${IL_RULES}
    EXPECT_EXIT 0 EXPECT_MATCH "note: orphan-key: action.file.longGone")

# T14. A quoted reference ENDING IN A DOT is a namespace reference and covers every key beneath it,
# because several catalog keys in this product are built at runtime from a table and never appear
# whole in any source. Both halves are asserted in one fixture: the generated key under the
# referenced namespace is NOT reported, and a key outside it still is -- without the second half the
# rule is equally satisfied by an orphan check that stopped checking.
rawpage_add_gate_test(NAME gates.i18n_lint.namespace_reference_covers_generated_keys
    COMMAND ${IL} --root ${IL_FIX}/orphan-key-namespace --rules ${IL_RULES}
    EXPECT_EXIT 0
    EXPECT_MATCH "note: orphan-key: prefs.window.title"
    EXPECT_NOT_MATCH "orphan-key: prefs.context.Viewer")

# T16 -- issue I-04's rule, and it is BLOCKING where orphan-key is a note. One fixture carries four
# references and exactly one is the defect, because the three that must NOT fire are what the rule's
# precision consists of: a key that IS defined, a namespace reference ending in a dot, and a dotted
# literal that is a file name. Anchoring on the declared `keyFamilies` is what separates them, and a
# rule that fired on every dotted literal would fail this build on settings.ini.
rawpage_add_gate_test(NAME gates.i18n_lint.unknown_key
    COMMAND ${IL} --root ${IL_FIX}/unknown-key --rules ${IL_RULES}
    EXPECT_EXIT 1
    EXPECT_MATCH "unknown-key: error.font.missingFace"
    EXPECT_NOT_MATCH "unknown-key: error.font.noSubstitute")

# S5 T13 -- guide issue I-08 (`Stage5-TextSearch-Testing-Guide.md` §8). An action table row names ONE catalog
# key, its label, and a ribbon control's "action" names none; an action id is a permanent name in a namespace
# that may also be a family (`compare.` since D5-20). Neither id is reported -- and the family is still
# enforced elsewhere, which the fixture's undefined key is: a rule that stopped reading the family would
# pass the first half.
rawpage_add_gate_test(NAME gates.i18n_lint.action_ids_are_not_catalog_keys
    COMMAND ${IL} --root ${IL_FIX}/unknown-key-action-ids --rules ${IL_RULES}
    EXPECT_EXIT 1
    EXPECT_MATCH "unknown-key: compare.view.missing"
    EXPECT_NOT_MATCH "unknown-key: compare.documents"
    EXPECT_NOT_MATCH "unknown-key: compare.showReport"
    EXPECT_NOT_MATCH "unknown-key: compare.view.title")

rawpage_add_gate_test(NAME gates.i18n_lint.unknown_key_ignores_namespaces_and_filenames
    COMMAND ${IL} --root ${IL_FIX}/unknown-key --rules ${IL_RULES}
    EXPECT_EXIT 1
    EXPECT_NOT_MATCH "unknown-key: error.repair."
    EXPECT_NOT_MATCH "unknown-key: settings.ini")

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
# mutation-lint (Stage 3 T6, decision D3-12) -- Stage 3 guide cases A-10 and A-11
#
# One fixture per rule, and each must fail FOR ITS OWN RULE AND ITS OWN CLASS (convention 7): every
# failing tree holds three commands and exactly one defect, so EXPECT_NOT_MATCH on a sibling class is
# what shows the finding is precise rather than a gate that reports everything in the tree. The
# defective tag in each tree also appears where a test does NOT -- a comment, an INFO() string, a
# case-misspelling, a file under tests/gates/ -- and those must not satisfy the rule (convention 22).
# The gate proves a test exists; whether it tests anything is tst_reversibility's (guide A-44).
# --------------------------------------------------------------------------------------
set(ML $<TARGET_FILE:mutation-lint>)
set(ML_FIX "${FIXTURES}/mutation-lint")

# A-11, the real tree: every command, named -- and so the roster D6-1 closes is pinned here, not
# remembered. Twelve since Stage 6 T13 -- `Stage6-Annotations-Plan.md` §4's end, the twelfth
# `ImportAnnotationsCommand` -- eleven since T11, nine since T9, seven since T5, three since T3, one until then.
rawpage_add_gate_test(NAME gates.mutation_lint.clean_tree
    COMMAND ${ML} --root ${CMAKE_SOURCE_DIR}
    EXPECT_EXIT 0 EXPECT_MATCH "clean -- 15 command\\(s\\): AddAnnotationCommand DeleteAnnotationCommand EditAnnotationCommand MoveAnnotationCommand SetAnnotationAppearanceCommand LockAnnotationCommand ReplyToAnnotationCommand SetAnnotationStatusCommand ImportAnnotationsCommand AddAttachmentCommand DeleteAttachmentCommand RotatePageCommand MovePagesCommand DeletePagesCommand DuplicatePagesCommand;")

# A-11, THE CONTROL: three spellings of "derives from Command" (plain, qualified across two lines,
# second of two bases), each complete -- and a CommandHistory base and an `enum class X : int`,
# which are not commands. A roster of exactly three is the assertion.
rawpage_add_gate_test(NAME gates.mutation_lint.complete_command_is_clean
    COMMAND ${ML} --root ${ML_FIX}/complete
    EXPECT_EXIT 0
    EXPECT_MATCH "clean -- 3 command\\(s\\): FixtureStampCommand FixtureTiltCommand FixtureShearCommand;")

# A-10, rule 1. The missing row is the TWO-LINE spelling, and the table carries it in a comment.
rawpage_add_gate_test(NAME gates.mutation_lint.no_row
    COMMAND ${ML} --root ${ML_FIX}/no-row
    EXPECT_EXIT 1
    EXPECT_MATCH "unregistered-command: FixtureTiltCommand derives from Command"
    EXPECT_NOT_MATCH "(FixtureStampCommand|FixtureShearCommand) derives|no-serialize-test|no-revert-test")

# A-10, rule 2. The serialize tag is present in a comment, in an INFO() string and under tests/gates/.
rawpage_add_gate_test(NAME gates.mutation_lint.no_serialize_test
    COMMAND ${ML} --root ${ML_FIX}/no-serialize-test
    EXPECT_EXIT 1
    EXPECT_MATCH "no-serialize-test: FixtureShearCommand has no test case tagged \\[serialize:FixtureShearCommand\\]"
    EXPECT_NOT_MATCH "unregistered-command|no-revert-test|no-serialize-test: Fixture(Stamp|Tilt)")

# A-10, rule 3. The revert tag is present misspelled in case, and in a comment on the test's own line.
rawpage_add_gate_test(NAME gates.mutation_lint.no_revert_test
    COMMAND ${ML} --root ${ML_FIX}/no-revert-test
    EXPECT_EXIT 1
    EXPECT_MATCH "no-revert-test: FixtureStampCommand has no test case tagged \\[revert:FixtureStampCommand\\]"
    EXPECT_NOT_MATCH "unregistered-command|no-serialize-test|no-revert-test: Fixture(Tilt|Shear)")

# The fourth rule: a row whose header column does not declare its class. The compiler never reads
# that column, so nothing else would notice.
rawpage_add_gate_test(NAME gates.mutation_lint.row_without_class
    COMMAND ${ML} --root ${ML_FIX}/row-without-class
    EXPECT_EXIT 1
    EXPECT_MATCH "row-without-class: FixtureShearCommand2 \\(\"fixture.shear2\"\\)"
    EXPECT_NOT_MATCH "unregistered-command|no-serialize-test|no-revert-test")

# A gate that could not read its inputs has not run.
rawpage_add_gate_test(NAME gates.mutation_lint.unreadable_row_is_exit_2
    COMMAND ${ML} --root ${ML_FIX}/unreadable-row
    EXPECT_EXIT 2 EXPECT_MATCH "cannot read this row")

rawpage_add_gate_test(NAME gates.mutation_lint.missing_table_is_exit_2
    COMMAND ${ML} --root ${ML_FIX}/no-table
    EXPECT_EXIT 2 EXPECT_MATCH "no command table")

# --explain states what the gate cannot see, so a reader need not open the tool to learn it.
rawpage_add_gate_test(NAME gates.mutation_lint.explain_names_blind_spots
    COMMAND ${ML} --root ${CMAKE_SOURCE_DIR} --explain
    EXPECT_EXIT 0 EXPECT_MATCH "intermediate base")

# --------------------------------------------------------------------------------------
# rawpage_assert_headless() -- guide case A-03 (Stage 2 T1, decision D2-2).
#
# THE CASE THAT PROTECTS THE WHOLE PORTABLE STORY. tests/unit must link rawpage_core and rawpage_cos
# and nothing else; if a PDF engine or a widget library enters that closure, core/ is no longer
# testable without one and portable-asan stops being able to build it on Linux. The assertion is
# the only thing standing there, and until this fixture existed nothing had ever proved it fires.
#
# The fixture INCLUDES cmake/RawPageHeadless.cmake rather than carrying a copy, which is the whole
# reason that function was moved out of tests/unit/CMakeLists.txt: a fixture with its own copy
# proves the copy fires (convention 7, and Stage 0's I-15).
#
# NOT guarded by if(WIN32): the assertion is pure CMake over a LANGUAGES NONE project, so it runs on
# the bare Linux gates runner too -- which is exactly the configuration whose portability it exists
# to defend.
# --------------------------------------------------------------------------------------
set(HA_PROJECT "${FIXTURES}/headless-assert/project")

function(rawpage_add_headless_test)
    cmake_parse_arguments(HA "" "NAME;EXPECT_EXIT;EXPECT_MATCH;EXTRA" "" ${ARGN})
    rawpage_add_gate_test(NAME ${HA_NAME}
        COMMAND ${CMAKE_COMMAND} --fresh
                -S ${HA_PROJECT}
                -B ${CMAKE_BINARY_DIR}/gate-fixtures/${HA_NAME}
                -G Ninja
                -DRAWPAGE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                ${HA_EXTRA}
        EXPECT_EXIT ${HA_EXPECT_EXIT} EXPECT_MATCH "${HA_EXPECT_MATCH}")
endfunction()

# THE CONTROL, first: a clean closure must configure. Without it every case below is equally
# satisfied by an assertion that refuses everything, which is the failure mode this whole file
# exists to catch.
rawpage_add_headless_test(NAME gates.headless_assert.clean_closure_is_allowed
    EXTRA "-DFIXTURE_LEAK="
    EXPECT_EXIT 0 EXPECT_MATCH "closure is clean")

# A direct edge -- somebody adds one line to tests/unit/CMakeLists.txt to get a raster test to link.
# The pattern is a SINGLE WORD from the END of the diagnostic: message(FATAL_ERROR) hard-wraps, so a
# phrase can be split across a line break and fail for a reason unrelated to the message (I-09).
rawpage_add_headless_test(NAME gates.headless_assert.direct_pdfium_edge
    EXTRA "-DFIXTURE_LEAK=direct|-DFIXTURE_DEFINE_PDFIUM=ON"
    EXPECT_EXIT 1 EXPECT_MATCH "resist")

# THE ONE THAT ACTUALLY HAPPENS: nobody edits the suite's own CMakeLists at all. rawpage_cos grows a
# dependency on the engine, and the leak arrives transitively -- which is why the assertion walks the
# resolved closure rather than reading one link line.
rawpage_add_headless_test(NAME gates.headless_assert.transitive_pdfium_edge
    EXTRA "-DFIXTURE_LEAK=transitive|-DFIXTURE_DEFINE_PDFIUM=ON"
    EXPECT_EXIT 1 EXPECT_MATCH "resist")

# The name is rejected even where nothing defines it. This is the portable-preset case: rawpage_pdfium
# is Windows-only and will never be a target on Linux, and a check that skipped unresolvable names
# would be silent precisely there -- handing the developer CMake's own "target not found" from the
# generate step instead of a diagnostic that says what rule was broken.
rawpage_add_headless_test(NAME gates.headless_assert.pdfium_name_without_target
    EXTRA "-DFIXTURE_LEAK=direct"
    EXPECT_EXIT 1 EXPECT_MATCH "resist")

# And Stage 1's two entries still fire after the function moved out of tests/unit. A refactor that
# quietly narrowed the forbidden list to the newest member would pass every case above.
# Stage 3 T3, guide case A-03 (D3-3): rawpage_tests now links rawpage_ops for real. The edge itself is
# allowed -- a control, because an assertion that began refusing it would pass the case after it --
# and an engine arriving THROUGH it is caught, with the engine a real target as it is on Windows.
rawpage_add_headless_test(NAME gates.headless_assert.ops_edge_is_allowed
    EXTRA "-DFIXTURE_LEAK=ops"
    EXPECT_EXIT 0 EXPECT_MATCH "closure is clean")

rawpage_add_headless_test(NAME gates.headless_assert.ops_carrying_pdfium_is_caught
    EXTRA "-DFIXTURE_LEAK=ops_pdfium|-DFIXTURE_DEFINE_PDFIUM=ON"
    EXPECT_EXIT 1 EXPECT_MATCH "resist")

rawpage_add_headless_test(NAME gates.headless_assert.ui_edge_still_caught
    EXTRA "-DFIXTURE_LEAK=ui"
    EXPECT_EXIT 1 EXPECT_MATCH "rawpage_ui")

# --------------------------------------------------------------------------------------
# rawpage_assert_fuzz_target_count() -- Stage 3 guide case A-83(a), and Stage 2's A-27 proven to fire.
#
# "Ten minutes with no crashes" is passed by a target that does not exist, so tests/fuzz fails configure
# on the wrong count. The assertion moved to cmake/RawPageFuzzTargets.cmake at Stage 3 T13 so that these
# cases -- which run on every preset, the bare Linux gates runner included -- drive the real function;
# before, it could only run inside the portable-fuzz configure, and had never been seen to refuse.
# --------------------------------------------------------------------------------------
set(FC_PROJECT "${FIXTURES}/fuzz-count/project")

function(rawpage_add_fuzz_count_test)
    cmake_parse_arguments(FC "" "NAME;EXPECT_EXIT;EXPECT_MATCH;EXTRA" "" ${ARGN})
    rawpage_add_gate_test(NAME ${FC_NAME}
        COMMAND ${CMAKE_COMMAND} --fresh
                -S ${FC_PROJECT}
                -B ${CMAKE_BINARY_DIR}/gate-fixtures/${FC_NAME}
                -G Ninja
                -DRAWPAGE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                ${FC_EXTRA}
        EXPECT_EXIT ${FC_EXPECT_EXIT} EXPECT_MATCH "${FC_EXPECT_MATCH}")
endfunction()

# THE CONTROL: the count Stage 3 ships, as it ships it -- three targets and a helper beside them.
rawpage_add_fuzz_count_test(NAME gates.fuzz_count.three_of_three_with_a_helper
    EXTRA "-DFIXTURE_TARGETS=3|-DFIXTURE_EXPECTED=3|-DFIXTURE_HELPER=ON"
    EXPECT_EXIT 0 EXPECT_MATCH "as expected")

# A-83(a) in its own words: a graph declaring two targets where three are expected.
rawpage_add_fuzz_count_test(NAME gates.fuzz_count.two_of_three_fails
    EXTRA "-DFIXTURE_TARGETS=2|-DFIXTURE_EXPECTED=3"
    EXPECT_EXIT 1 EXPECT_MATCH "miscounted")

# Zero is a failure, not a pass -- A-27's sentence, and the case it was written for.
rawpage_add_fuzz_count_test(NAME gates.fuzz_count.zero_fails
    EXTRA "-DFIXTURE_TARGETS=0|-DFIXTURE_EXPECTED=3"
    EXPECT_EXIT 1 EXPECT_MATCH "miscounted")

# Stage 5 T4 (guide A-40): the count the stage ships, FOUR -- the control -- and three of four refused.
# The three-target cases above stay as they are: they prove the function, whatever the shipped count.
rawpage_add_fuzz_count_test(NAME gates.fuzz_count.four_of_four_with_a_helper
    EXTRA "-DFIXTURE_TARGETS=4|-DFIXTURE_EXPECTED=4|-DFIXTURE_HELPER=ON"
    EXPECT_EXIT 0 EXPECT_MATCH "as expected")
rawpage_add_fuzz_count_test(NAME gates.fuzz_count.three_of_four_fails
    EXTRA "-DFIXTURE_TARGETS=3|-DFIXTURE_EXPECTED=4"
    EXPECT_EXIT 1 EXPECT_MATCH "miscounted")

# Stage 6 T2 (guide A-23): the count the stage ships, FIVE -- the control -- and four of five refused.
rawpage_add_fuzz_count_test(NAME gates.fuzz_count.five_of_five_with_a_helper
    EXTRA "-DFIXTURE_TARGETS=5|-DFIXTURE_EXPECTED=5|-DFIXTURE_HELPER=ON"
    EXPECT_EXIT 0 EXPECT_MATCH "as expected")
rawpage_add_fuzz_count_test(NAME gates.fuzz_count.four_of_five_fails
    EXTRA "-DFIXTURE_TARGETS=4|-DFIXTURE_EXPECTED=5"
    EXPECT_EXIT 1 EXPECT_MATCH "miscounted")

# And the helper cannot stand in for a missing target: two targets plus a helper is still two.
rawpage_add_fuzz_count_test(NAME gates.fuzz_count.helper_is_not_a_target
    EXTRA "-DFIXTURE_TARGETS=2|-DFIXTURE_EXPECTED=3|-DFIXTURE_HELPER=ON"
    EXPECT_EXIT 1 EXPECT_MATCH "miscounted")

# --------------------------------------------------------------------------------------
# The PDFium pin (PA-1 .. PA-5) -- guide case A-05, Stage 2 T2, decision D2-3.
#
# A golden-image baseline is a claim about ONE rasterizer, so the engine's version is asserted at
# configure rather than assumed. These cases drive the assertion against small committed trees --
# four-line VERSION files and args.gn files -- rather than against this machine's third_party/, which
# is what lets them run on the bare Linux gates runner WITH NO PDFium FETCHED AT ALL. Testing the
# assertion and testing the developer's directory are different things, and only the first is worth
# a case.
#
# Not guarded by if(WIN32): a LANGUAGES NONE project needs no compiler and no engine.
# --------------------------------------------------------------------------------------
set(PP_PROJECT "${FIXTURES}/pdfium-pin/project")
set(PP_TREES "${FIXTURES}/pdfium-pin/trees")

function(rawpage_add_pdfium_pin_test)
    cmake_parse_arguments(PP "" "NAME;TREE;EXPECT_EXIT;EXPECT_MATCH;EXPECT_NOT_MATCH" "" ${ARGN})
    rawpage_add_gate_test(NAME ${PP_NAME}
        COMMAND ${CMAKE_COMMAND} --fresh
                -S ${PP_PROJECT}
                -B ${CMAKE_BINARY_DIR}/gate-fixtures/${PP_NAME}
                -G Ninja
                -DRAWPAGE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                -DFIXTURE_TREE=${PP_TREE}
        EXPECT_EXIT ${PP_EXPECT_EXIT} EXPECT_MATCH "${PP_EXPECT_MATCH}"
        EXPECT_NOT_MATCH "${PP_EXPECT_NOT_MATCH}")
endfunction()

# THE CONTROL, first and for the usual reason: a correct tree must configure, or every case below is
# satisfied by an assertion that refuses everything.
rawpage_add_pdfium_pin_test(NAME gates.pdfium_pin.correct_tree_is_accepted
    TREE ${PP_TREES}/good
    EXPECT_EXIT 0 EXPECT_MATCH "accepted 153.0.8009.0")

# PA-3 -- the case A-05 is written for. The pattern is the FOUND version, not the expected one: a
# diagnostic that says only "wrong version" cannot be acted on, and A-05 names that as its control.
rawpage_add_pdfium_pin_test(NAME gates.pdfium_pin.wrong_version_names_both
    TREE ${PP_TREES}/wrong-version
    EXPECT_EXIT 1 EXPECT_MATCH "153.0.7000.0")

# ...and the other half of "names both", asserted separately so a failure says which half broke.
rawpage_add_pdfium_pin_test(NAME gates.pdfium_pin.wrong_version_names_expected
    TREE ${PP_TREES}/wrong-version
    EXPECT_EXIT 1 EXPECT_MATCH "153.0.8009.0")

# PA-4 -- the -v8- archive. It is the wrong file rather than the wrong version, and it fails for a
# reason of its own: a JavaScript engine behind an entry point we never call is pure attack surface,
# and it renders differently, which makes it a baseline question too.
rawpage_add_pdfium_pin_test(NAME gates.pdfium_pin.v8_enabled_is_refused
    TREE ${PP_TREES}/v8-enabled
    EXPECT_EXIT 1 EXPECT_MATCH "pdf_enable_v8")

# PA-2 -- a VERSION file that does not state a version. Without this the parse would assemble "..."
# and PA-3 would report a nonsense `found` value, which is a diagnostic that sends its reader after
# the wrong problem. EXPECT_NOT_MATCH asserts exactly that: the failure must not arrive dressed as a
# version mismatch.
rawpage_add_pdfium_pin_test(NAME gates.pdfium_pin.unparseable_version_says_so
    TREE ${PP_TREES}/unparseable
    EXPECT_EXIT 1 EXPECT_MATCH "not parseable" EXPECT_NOT_MATCH "PA-3")

# PA-5 -- the DLL is there and the GNU import library is not, which is precisely the state of a tree
# unpacked by hand instead of through scripts/fetch-pdfium.ps1. Upstream ships only an MSVC-format
# import library, and without this check the failure is a wall of undefined FPDF_ references.
rawpage_add_pdfium_pin_test(NAME gates.pdfium_pin.missing_import_library
    TREE ${PP_TREES}/no-implib
    EXPECT_EXIT 1 EXPECT_MATCH "libpdfium.dll.a")

# PA-1 -- nothing fetched at all, which is what a fresh clone looks like. It gets its own message
# because the fix is completely different from every other failure here.
rawpage_add_pdfium_pin_test(NAME gates.pdfium_pin.unfetched_tree_says_run_the_script
    TREE ${PP_TREES}/does-not-exist
    EXPECT_EXIT 1 EXPECT_MATCH "fetch-pdfium.ps1")

# --------------------------------------------------------------------------------------
# The corpus (Stage 2 T4, decision D2-11).
#
# D2-11 makes every file in corpus/ generated by a script we own, and T4's exit criterion
# adds the clause that has teeth: every file regenerates BYTE-IDENTICALLY from a clean
# checkout, and that is ASSERTED rather than assumed. These three cases are the assertion.
#
# They belong in `gates` rather than in a unit suite because they check DATA and its
# documentation, need no Qt, no vcpkg and no PDFium, and CI runs this preset before it
# compiles anything -- so a corpus damaged by a hand edit or by a .gitattributes regression
# is reported before four jobs' worth of build time is spent on top of it.
#
# WHY A HARD FAILURE WHEN PYTHON IS ABSENT, rather than a skip. Stage 1's I-30 was a CI job
# that had never run because two systems disagreed about a variable name, and nothing
# noticed for the life of the stage. A conditionally-absent test has exactly that shape: the
# suite goes green and the property is unchecked. Python is not a build dependency of this
# project -- `cmake --build` never calls it -- so its absence costs a red TEST, never a
# broken build.
# --------------------------------------------------------------------------------------
find_package(Python3 3.9 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    set(CORPUS_DRIVER "${CMAKE_SOURCE_DIR}/corpus/make-corpus.py")

    # A-76. The whole corpus, regenerated in memory and compared byte for byte.
    rawpage_add_gate_test(NAME gates.corpus.reproducible
        COMMAND ${Python3_EXECUTABLE} ${CORPUS_DRIVER} --verify
        EXPECT_EXIT 0 EXPECT_MATCH "regenerate byte-identically")

    # A-77. Every file has a PROVENANCE.md row and every row has a file. The half that keeps
    # D2-11's "nothing is borrowed" claim checkable rather than merely stated.
    rawpage_add_gate_test(NAME gates.corpus.provenance
        COMMAND ${Python3_EXECUTABLE} ${CORPUS_DRIVER} --check-provenance
        EXPECT_EXIT 0 EXPECT_MATCH "one per file")

    # A-78, and the NEGATIVE CONTROL the two cases above cannot supply for themselves: a
    # checker that returns success without looking passes both of them. --self-test drives
    # each detection with an input that must trigger it, in memory, so an interrupted run
    # cannot leave the real corpus damaged: Stage 2 T4's five, Stage 5 T1's five, and Stage 6
    # T1's four (an /AP planted on annots-no-ap.pdf, /RawPageTestKey removed, an annotation
    # fixture with no label, an exchange file with no PROVENANCE.md row), and Stage 6 guide
    # I-27's and I-28's two (a <contents> put back in threads-acrobat26.xfdf, an /IRT reference
    # in threads-acrobat26.fdf), and Stage 7 T1's three (a wrong identity in an assembly label,
    # /Resources planted on an inheriting page of inherit-resources.pdf, an assembly fixture with no
    # label), each matched to the FILE it damaged so it cannot fire for another file's reason
    # (convention 7).
    rawpage_add_gate_test(NAME gates.corpus.self_test
        COMMAND ${Python3_EXECUTABLE} ${CORPUS_DRIVER} --self-test
        EXPECT_EXIT 0 EXPECT_MATCH "19 of 19 controls fired")

    # Stage 5 T1 (Stage5-TextSearch-Testing-Guide.md A-14): each text fixture carries the fault or
    # feature its PROVENANCE.md row claims, read from its bytes with every stream decoded -- a
    # /ToUnicode that is there, Hebrew emitted in visual order, a control word that is absent. Its
    # controls are two of --self-test's ten, above: a fixture stripped of its /ToUnicode, and the
    # control word planted, must each be reported.
    # Stage 6 T1 (Stage6-Annotations-Testing-Guide.md A-15) extends it to the annotation
    # fixtures, the exchange files and the stamp images, and holds every labels/annots-*.json
    # entry to the object it names. Stage 7 T1 (Stage7-Assembly-Testing-Guide.md A-15) adds the
    # assembly fixtures: every labels/assembly-*.json claim -- the page tree's order, the identity
    # each page shows, each reference's form and target -- held to the bytes.
    rawpage_add_gate_test(NAME gates.corpus.faults
        COMMAND ${Python3_EXECUTABLE} ${CORPUS_DRIVER} --check-faults
        EXPECT_EXIT 0 EXPECT_MATCH "every Stage 5, 6 and 7 fixture carries what its row claims")

    # Stage 5 T1 (A-15): every comparison record re-derives its revised document from its base, by
    # code that shares nothing with the edits, and each kind of pair keeps its promise about words.
    # Four controls damage a pair and must each be caught.
    rawpage_add_gate_test(NAME gates.corpus.compare
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/corpus/make-compare.py --self-test
        EXPECT_EXIT 0 EXPECT_MATCH "14 pairs checked, 4 of 4 controls fired")

    # Stage 6 T7 (Stage6-Annotations-Plan.md D6-7, D6-34): core/doc/CjkCoverage.inc -- the characters each CJK
    # collection can show in a typed comment -- is GENERATED, and is held to its generator the way the corpus is:
    # it regenerates byte-identically, and its rows read back as the codecs' sets less what the engine
    # measurement removed, with one changed bit as the control and Greek absent. Whether the ENGINE honours each
    # claim is tests/render/tst_typedtext.cpp's; these two only say the file is the generator's.
    set(CJK_COVERAGE "${CMAKE_SOURCE_DIR}/tools/cjk-coverage/make-cjk-coverage.py")
    rawpage_add_gate_test(NAME gates.cjk_coverage.reproducible
        COMMAND ${Python3_EXECUTABLE} ${CJK_COVERAGE} --verify
        EXPECT_EXIT 0 EXPECT_MATCH "4 collections regenerate byte-identically")
    rawpage_add_gate_test(NAME gates.cjk_coverage.self_test
        COMMAND ${Python3_EXECUTABLE} ${CJK_COVERAGE} --self-test
        EXPECT_EXIT 0 EXPECT_MATCH "1 of 1 control fired; Greek refused")

    # ----------------------------------------------------------------------------------
    # corpus/signed/ (Stage 3 T1, decision D3-2) -- the instrument A4's verdict will read.
    #
    # tests/gates/check-signatures.py reads each file as BYTES, never through pdf/cos/, and asks
    # OpenSSL. Its flipped-byte controls run FIRST and a control that verifies stops the case, so
    # a validator that is not checking the signed range can never report a signature valid
    # (Stage 2 I-41's lesson). The two cases after it are controls on the SCRIPT, not on OpenSSL:
    # `cmake -E true` accepts everything and must stop the run at its first control; `cmake -E
    # false` rejects everything and must fail the real check. Without them, "8 of 8 controls
    # rejected" is equally satisfied by a script that never consulted the validator (convention 7).
    # ----------------------------------------------------------------------------------
    set(CHECK_SIGNATURES "${CMAKE_CURRENT_SOURCE_DIR}/check-signatures.py")
    set(SIGNED_FILES
        "${CMAKE_SOURCE_DIR}/corpus/signed/approval.pdf"
        "${CMAKE_SOURCE_DIR}/corpus/signed/approval-xrefstream.pdf"
        "${CMAKE_SOURCE_DIR}/corpus/signed/certified-nochanges.pdf"
        "${CMAKE_SOURCE_DIR}/corpus/signed/approval-then-update.pdf")
    string(REPLACE ";" "@@" signed_files_joined "${SIGNED_FILES}")

    # A-15. Every generated signature verifies -- after its two flipped copies have been rejected.
    rawpage_add_gate_test(NAME gates.corpus.signed
        COMMAND ${CMAKE_COMMAND} -DPYTHON=${Python3_EXECUTABLE} -DSCRIPT=${CHECK_SIGNATURES}
                "-DFILES=${signed_files_joined}" -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckSignatures.cmake
        EXPECT_EXIT 0
        EXPECT_MATCH "validator: .*OpenSSL 3\\..*signature check ok: 8 of 8 controls rejected, 4 of 4 signatures verify")

    rawpage_add_gate_test(NAME gates.corpus.signed_accepting_validator_stops
        COMMAND ${Python3_EXECUTABLE} ${CHECK_SIGNATURES} --openssl ${CMAKE_COMMAND}
                --openssl-prefix=-E --openssl-prefix=true ${SIGNED_FILES}
        EXPECT_EXIT 1 EXPECT_MATCH "CONTROL VERIFIED" EXPECT_NOT_MATCH "verify    ")

    rawpage_add_gate_test(NAME gates.corpus.signed_rejecting_validator_fails
        COMMAND ${Python3_EXECUTABLE} ${CHECK_SIGNATURES} --openssl ${CMAKE_COMMAND}
                --openssl-prefix=-E --openssl-prefix=false ${SIGNED_FILES}
        EXPECT_EXIT 1 EXPECT_MATCH "all 8 flipped copies rejected.*4 of 4 signatures do NOT verify")

    # A-16. The shapes corpus/PROVENANCE.md claims -- one signature each, the right cross-reference
    # kind, /DocMDP /P 1 only on the certified file, the signed range ending at the FIRST %%EOF of
    # the two-revision file, no private key, and a certificate that names itself a test identity.
    # The self-test drives each of those seven detections with damage that must trigger it.
    rawpage_add_gate_test(NAME gates.corpus.signed_shapes
        COMMAND ${Python3_EXECUTABLE} ${CHECK_SIGNATURES} --shapes ${SIGNED_FILES}
        EXPECT_EXIT 0 EXPECT_MATCH "shape check ok: 4 files")
    rawpage_add_gate_test(NAME gates.corpus.signed_shapes_self_test
        COMMAND ${Python3_EXECUTABLE} ${CHECK_SIGNATURES}
                --shapes-self-test ${CMAKE_SOURCE_DIR}/corpus/signed
        EXPECT_EXIT 0 EXPECT_MATCH "7 of 7 detections fired")
else()
    foreach(case reproducible provenance self_test faults compare signed signed_accepting_validator_stops
                 signed_rejecting_validator_fails signed_shapes signed_shapes_self_test)
        add_test(NAME gates.corpus.${case}
            COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/RequirePython.cmake")
    endforeach()
    foreach(case reproducible self_test)
        add_test(NAME gates.cjk_coverage.${case}
            COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/RequirePython.cmake")
    endforeach()
endif()

# --------------------------------------------------------------------------------------
# No control byte in a tracked text file (S4 guide I-22, A-93).
#
# Four Windows paths in CLAUDE.md and the Stage 0 guide had their backslashes read as escapes on
# the way into the file -- BACKSPACE, CARRIAGE RETURN, BELL, VERTICAL TAB -- and a terminal draws
# all four invisibly, so the Commands section carried a broken path for four days. The checker's
# header owns the rule and its blind spot (an escape that produced a PRINTABLE byte). Here because
# it checks DATA, needs nothing but Python and git, and CI runs this preset first.
#
# THE CONTROL is --self-test, which drives each detection with the damage that must trigger it and
# the allowed bytes (TAB, LF, CRLF, mixed endings, UTF-8) with inputs that must pass -- in memory,
# because a damaged fixture in the tree would fail the tree case. The tree case itself refuses to
# report clean unless CLAUDE.md was among the files it read, so an empty listing cannot pass.
# Absent Python it FAILS, for the reason the corpus block above gives.
# --------------------------------------------------------------------------------------
if(Python3_Interpreter_FOUND)
    set(CHECK_TEXT_BYTES "${CMAKE_CURRENT_SOURCE_DIR}/check-text-bytes.py")
    rawpage_add_gate_test(NAME gates.text_bytes.tracked_tree
        COMMAND ${Python3_EXECUTABLE} ${CHECK_TEXT_BYTES} --root ${CMAKE_SOURCE_DIR}
        EXPECT_EXIT 0 EXPECT_MATCH "text bytes clean -- [0-9]+ tracked text files")
    rawpage_add_gate_test(NAME gates.text_bytes.self_test
        COMMAND ${Python3_EXECUTABLE} ${CHECK_TEXT_BYTES} --self-test
        EXPECT_EXIT 0 EXPECT_MATCH "9 of 9 detections fired, 5 of 5 clean inputs passed, positions exact")
else()
    foreach(case tracked_tree self_test)
        add_test(NAME gates.text_bytes.${case}
            COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/RequirePython.cmake")
    endforeach()
endif()

# --------------------------------------------------------------------------------------
# A4's automated signature validator (Stage 3 precondition P-2, guide case A-06).
#
# Decision D3-2 makes the OpenSSL command-line tool the integrity half of A4's verdict, and a
# missing validator must FAIL, never skip. ResolveOpenSSL.cmake owns the reasoning, including
# why it resolves at TEST time and why RAWPAGE_OPENSSL exists. Unconditional, and in `gates`,
# because both CI runners that will judge A4 run this directory.
#
# The two controls are what keep the first case from being satisfied by a script that always
# exits 0 (convention 7): each takes the validator away one way and asserts the failure NAMES
# the reason. What none of them covers: that the tool verifies a CMS signature -- that is the
# signed-corpus cases' job, with their flipped-byte control (Stage 3 T1).
# --------------------------------------------------------------------------------------
set(RESOLVE_OPENSSL "${CMAKE_CURRENT_SOURCE_DIR}/ResolveOpenSSL.cmake")

# A-06, first half. Passes only on OpenSSL 3, and prints the path, how it was found and the version.
rawpage_add_gate_test(NAME gates.validators.openssl
    COMMAND ${CMAKE_COMMAND} -P ${RESOLVE_OPENSSL}
    EXPECT_EXIT 0 EXPECT_MATCH "openssl validator: .*version: +OpenSSL 3\\.")

# A-06, second half: a PATH that holds no OpenSSL, and no override. The fixtures directory is a real
# directory with no executable in it; CMake itself is reached by absolute path, so it still starts.
rawpage_add_gate_test(NAME gates.validators.openssl_absent_fails
    COMMAND ${CMAKE_COMMAND} -P ${RESOLVE_OPENSSL}
    EXPECT_EXIT 1 EXPECT_MATCH "no OpenSSL command-line tool was found on PATH")
set_tests_properties(gates.validators.openssl_absent_fails PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=set:${FIXTURES};RAWPAGE_OPENSSL=unset:")

# The override names a file that is not there. It must fail rather than fall back to PATH, which
# would run a validator nobody chose -- so PATH is left as it is, where an OpenSSL may well be found.
rawpage_add_gate_test(NAME gates.validators.openssl_override_not_a_file_fails
    COMMAND ${CMAKE_COMMAND} -P ${RESOLVE_OPENSSL}
    EXPECT_EXIT 1 EXPECT_MATCH "RAWPAGE_OPENSSL names" EXPECT_NOT_MATCH "openssl validator:")
set_tests_properties(gates.validators.openssl_override_not_a_file_fails PROPERTIES
    ENVIRONMENT_MODIFICATION "RAWPAGE_OPENSSL=set:${FIXTURES}/no-such-openssl")

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
