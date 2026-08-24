# tests/ui -- the widget suite. Decision D1-4.
#
# WHY IT IS A SEPARATE TARGET. tests/unit links rawpage_core and rawpage_pdf and nothing else, and
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
