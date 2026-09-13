# tests/ui -- the widget suite. Decision D1-4.
#
# WHY IT IS A SEPARATE TARGET. tests/unit links rawpage_core and rawpage_cos and nothing else, and
# rawpage_assert_headless() enforces that against the RESOLVED link closure. That assertion is the
# standing proof that core/ is testable with no display and no engine, and portable-asan verifies it
# on a machine that has neither. The first widget test is precisely the pressure that would
# otherwise "just add rawpage_ui" to that target and destroy the property while every test still
# passed. tests/platform met the same pressure from the OS side at T7 and answered it the same way.
#
# WINDOWS ONLY, and under QT_QPA_PLATFORM=offscreen. None of it can judge appearance -- that is the
# guide's manual half, and a case here claiming to have checked a LOOK is a defect in the case.
#
# DEVIATION FROM THE PLAN, RECORDED RATHER THAN SLIPPED IN: Stage1-AppShell-Plan.md 4 schedules this
# target at T15. It arrives at T9 instead, with the first widgets, on the precedent T7 set for
# tests/platform: a stage's Definition of Done is code complete AND ctest green, and four widget
# files shipping with no suite until six tasks later would make that vacuous for all six. T15 still
# owns adding it to the CI `windows` job.

find_package(Catch2 3 REQUIRED)

add_executable(rawpage_ui_tests
    main.cpp
    tst_theme.cpp
    tst_shell.cpp
    tst_ribbonbuild.cpp
    tst_actionwiring.cpp
    tst_qat.cpp
    tst_toolfinder.cpp
    tst_documenttabs.cpp
    tst_context.cpp
    tst_preferences.cpp   # T14
    tst_logpane.cpp       # T14

    # S2 T13 -- the viewer. tst_viewport is PURE and needs no window: that is the whole reason
    # ViewportController and PageLayoutEngine are separate objects from PageView. tst_pageview is
    # the widget half, and every case in it asserts the geometry the layout GAVE the widget before
    # it asserts anything that depends on it (I-36).
    tst_viewport.cpp
    tst_pageview.cpp

    # S2 T14 -- the §8 Viewer commands, and the chrome that reports on the document. Every case in
    # tst_viewercommands drives the CHORD through ShortcutBinder rather than dispatching an id,
    # because "the command is implemented" and "the key reaches it" are different claims and I-42
    # is what happens when only the first is checked.
    tst_viewercommands.cpp
    tst_documentchrome.cpp

    # S2 T15 -- the two generated Preferences pages, and the sixteen keys reaching the viewer.
    # A-65 is deliberately split: the three flags that change PIXELS are measured in tests/render
    # over the real engine, and what is asserted here is that the preference reaches the request.
    tst_viewsettings.cpp
)

target_include_directories(rawpage_ui_tests PRIVATE "${CMAKE_SOURCE_DIR}")

# Catch2::Catch2 without WithMain -- main.cpp supplies one, because these tests need a QApplication
# on the offscreen platform before any of them runs. See the comment there.
target_link_libraries(rawpage_ui_tests PRIVATE
    rawpage_core
    rawpage_ui
    Catch2::Catch2
)

set_target_properties(rawpage_ui_tests PROPERTIES AUTOMOC ON)

# The suite installs rawpage_en.qm so the widgets are exercised against real text rather than
# against their own catalog keys. See the comment in main.cpp. It also needs res/ribbon.json beside
# the binary, for the same reason and with a second one on top: the ribbon cases assert that the
# SHIPPED resource is the one that loads, so depending on the validated copy is what makes them
# about the product rather than about a fixture.
add_dependencies(rawpage_ui_tests rawpage_translations rawpage_resources)

rawpage_common_flags(rawpage_ui_tests)

include(Catch)
catch_discover_tests(rawpage_ui_tests
    TEST_PREFIX "ui."
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin"
)

# A-84 -- and it drives `rawpage.exe` rather than linking anything, because the input it varies
# (`QT_SCALE_FACTOR`) is read when the `QGuiApplication` is built and is therefore a property of a
# process. It keeps the `ui.` prefix on purpose: a sixth CTest prefix would have to be remembered
# separately by CI's non-empty check, which is the same reasoning that put `render.golden.` inside
# `render.` at T17.
add_test(NAME ui.screen_metrics_invariant
    COMMAND ${CMAKE_COMMAND}
            -DRAWPAGE_EXE=$<TARGET_FILE:rawpage>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/ScreenMetricsInvariant.cmake")
set_tests_properties(ui.screen_metrics_invariant PROPERTIES
    # ENVIRONMENT_MODIFICATION, NEVER `ENVIRONMENT "PATH=<dir>;$ENV{PATH}"`. `ENVIRONMENT` is a CMake
    # LIST, so the semicolons of a Windows PATH split that one value into entries (113 on the CI runner) and the child
    # ran with `PATH=<mingw>/bin` and nothing else. It passed wherever the Qt DLLs had been copied
    # beside rawpage.exe and exited 0xC0000135 in every clean build -- CI runs 34724531551 and
    # 34772420519. It also froze the CONFIGURING shell's PATH into the test. The bench targets use
    # this form already (tests/bench/CMakeLists.txt).
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin")

# A-87 -- issue I-27, and like A-84 it drives `rawpage.exe` instead of linking anything, for the
# same class of reason: the condition it asserts is a property of a PROCESS. A diagnostic that
# composes and exits must print its answer while another process holds the single-instance bus, and
# a bus claim needs a claimant, so the case needs two processes and cannot be a Catch2 case.
#
# POWERSHELL RATHER THAN `cmake -P`, because CMake's `execute_process` has no way to start a process
# and keep it -- and the obvious workaround, `taskkill /IM rawpage.exe`, would kill the window of
# whoever is running the suite. `Start-Process -PassThru` gives a pid, and the script stops only
# what it started and adopts anything already running.
#
# It touches no settings file, which is what lets it be a CTest case where A-61's two-process half
# (`scripts/check-single-instance.ps1`) cannot be: that one has to edit settings.ini.
#
# 77 is SKIP: the case measures nothing on a machine whose `general/singleInstance` is false, and
# the script says so rather than passing. The skip is reached only through the CONTROL failing --
# a second GUI process that did not forward -- so a skip always means the bus was not claimed.
add_test(NAME ui.diagnostics_ignore_the_bus
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/DiagnosticsIgnoreTheBus.ps1"
            -Exe $<TARGET_FILE:rawpage>)
set_tests_properties(ui.diagnostics_ignore_the_bus PROPERTIES
    # The same trap as A-84's above, and here it did not even fail: a rawpage.exe that could not
    # load Qt never exited, the control read that as "the bus is not claimed", and the case SKIPPED.
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin"
    SKIP_RETURN_CODE 77
    RUN_SERIAL TRUE)
