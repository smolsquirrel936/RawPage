# tests/platform -- the OS layer's own suite.
#
# WHY THIS TARGET EXISTS SEPARATELY (Stage 1 T7, extending D1-4's reasoning to the other side).
#
# tests/unit/ links rawpage_core and rawpage_cos and NOTHING else, and rawpage_assert_headless()
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

    # S2 T12 -- the two interfaces the render policy layer was waiting on. Both suites drive the
    # real WNDPROC through the object's own notification HWND rather than calling a test hook: the
    # only synthetic part is where the message came from, and everything from DispatchMessage
    # inward is the shipped path. Each file states at the top what it therefore does NOT cover
    # (convention 19) -- a real scale change is manual case M-02's, and a real suspend is nobody's.
    tst_displaytopology.cpp
    tst_powersession.cpp

    # S2 T16 -- the fifth interface. This suite owns "is it installed here and what file backs it";
    # the ordered candidate list it walks is core/render/FontSubstitution's and is asserted by
    # tests/unit with no display at all, and the chain through PDFium is tests/render's.
    tst_fontresolver.cpp

    # S3 T7 -- IDurableFiles against real files in a temporary directory: the stream and creation time
    # a replace keeps, the refusals that leave both files, and the flush (guide cases A-47 ... A-49).
    tst_durablefiles.cpp

    # Stage 2 issue I-35: rawpage.exe's embedded manifest (the segment heap). Its subject is app/'s
    # build output, so the target below depends on `rawpage` and is handed its path.
    tst_manifest.cpp

    # S3 T9 -- TerminateProcess at random instants during commands and during saves (guide cases A-65's
    # child half, A-68, A-69). The child is rawpage_killprobe, below; the replay runs through
    # tests/unit/CosBackend.h, which is why this target now links rawpage_cos and rawpage_ops -- the
    # portable reader and writer, no engine.
    tst_recoverykill.cpp
)
target_compile_definitions(rawpage_platform_tests PRIVATE RAWPAGE_EXE_PATH="$<TARGET_FILE:rawpage>"
    RAWPAGE_KILLPROBE_PATH="$<TARGET_FILE:rawpage_killprobe>"
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus")
add_dependencies(rawpage_platform_tests rawpage rawpage_killprobe)

# rawpage_killprobe -- Stage 3 T9: a HELPER EXECUTABLE, not a test target (Stage3-Mutation-Plan.md §4). It
# edits or saves in a loop and reports its phase so tst_recoverykill can kill it at a known kind of instant.
add_executable(rawpage_killprobe killprobe/main.cpp)
target_include_directories(rawpage_killprobe PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(rawpage_killprobe PRIVATE rawpage_core rawpage_cos rawpage_ops rawpage_platform)
rawpage_link_qt(rawpage_killprobe Core)
rawpage_common_flags(rawpage_killprobe)
# Stage 7 T5: the page cycle's insert copies a corpus file (PageSteps.h).
target_compile_definitions(rawpage_killprobe PRIVATE RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus")

target_include_directories(rawpage_platform_tests PRIVATE "${CMAKE_SOURCE_DIR}")

# Catch2::Catch2 without WithMain -- main.cpp supplies one, because these tests need a
# QCoreApplication before any of them runs. See the comment there.
target_link_libraries(rawpage_platform_tests PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_ops
    rawpage_platform
    Catch2::Catch2
)

rawpage_common_flags(rawpage_platform_tests)

include(Catch)
catch_discover_tests(rawpage_platform_tests
    TEST_PREFIX "platform."
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin"
)
rawpage_serial_tests(rawpage_platform_tests PREFIX "platform." DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin")
