# tests/platform -- the OS layer's own suite.
#
# WHY THIS TARGET EXISTS SEPARATELY (Stage 1 T7, extending D1-4's reasoning to the other side).
#
# tests/unit/ links rawpage_core and rawpage_pdf and NOTHING else, and rawpage_assert_headless()
# enforces that against the resolved link closure. That assertion is the standing proof that core/
# is testable with no display and no engine, and portable-asan verifies it on a machine that has
# neither. The first platform test is exactly the pressure that would otherwise "just add
# rawpage_platform" to that target and destroy the property while every test still passed.
#
# D1-4 met the same pressure from ui/ by adding a target rather than weakening the existing one.
# This is that decision applied again. WINDOWS ONLY, and never on portable-asan: the portable
# subset is core/ + pdf/cos/ by rule, and platform/win/ is Win32 by definition.

find_package(Catch2 3 REQUIRED)

add_executable(rawpage_platform_tests
    main.cpp
    tst_platform.cpp
)

target_include_directories(rawpage_platform_tests PRIVATE "${CMAKE_SOURCE_DIR}")

# Catch2::Catch2 without WithMain -- main.cpp supplies one, because these tests need a
# QCoreApplication before any of them runs. See the comment there.
target_link_libraries(rawpage_platform_tests PRIVATE
    rawpage_core
    rawpage_platform
    Catch2::Catch2
)

rawpage_common_flags(rawpage_platform_tests)

include(Catch)
catch_discover_tests(rawpage_platform_tests
    TEST_PREFIX "platform."
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin"
)
