# tests/composition -- cases that need THE PRODUCT'S OWN COMPOSITION: app::CompositionRoot with the real
# engine and the real Windows bundle, a ui::MainWindow on it, and app/ code between them. Stage 3 T11.
#
# WHY A SIXTH TEST BINARY (Implementation-Plan.md §2.1's rule: a suite is a separate target when linking
# something would destroy a property another suite proves). Guide case A-74 replays a journal that
# rawpage_killprobe left behind, through the composition that ships. That needs the engine AND the widgets
# AND the OS bundle in one process -- and every existing target exists to keep one of those out:
#   * rawpage_ui_tests proves MainWindow needs no engine (FakeBackend);
#   * rawpage_render_tests and rawpage_golden_tests prove the engine needs no widgets and no display;
#   * rawpage_platform_tests is the OS layer on its own, with a QCoreApplication rather than a QApplication;
#   * rawpage_tests is headless by an assertion that fails configure.
# Folding A-74 into any of them would retire that target's property with every case still green. The
# benches that compose the product (tests/bench/bench_openclose_ui.cpp) report and never assert, so they are
# no home for a pass/fail case either.
#
# app/ IS AN EXECUTABLE, so its translation units are COMPILED IN, as the UI bench compiles them. The rule
# that only app/ names pdf::createBackend() and platform::createPlatform() still holds: the call sites are
# app/CompositionRoot.cpp, whichever binary it is compiled into.
#
# ITS CTEST PREFIX IS `ui.composition.`, INSIDE `ui.` ON PURPOSE -- the call rawpage_golden_tests made inside
# `render.`: CI's non-empty check reads prefixes, and a new one would have to be remembered there separately.
#
# WINDOWS ONLY, offscreen, and it never runs on portable-asan.

find_package(Catch2 3 REQUIRED)

add_executable(rawpage_composition_tests
    main.cpp

    # Stage 3 T11 -- guide case A-74: a journal written by rawpage_killprobe and killed with TerminateProcess,
    # offered and replayed by the product's composition.
    tst_recoveryproduct.cpp

    # Stage 4 T13 -- guide cases A-75 ... A-79: quit, relaunch, and get the session back, through the same
    # composition. It needs `app/Application.cpp` compiled in for `app::SessionShell`; the `Application`
    # class beside it is a QApplication subclass this binary never instantiates, and compiling it costs one
    # translation unit rather than a second file to keep the session half in.
    tst_restore.cpp

    # Stage 5 T5 -- guide cases A-42 ... A-48: a selection made with the mouse, graded against the
    # generator's labels for its text and against the RENDERED INK for its highlight (rule 6). Here and
    # not in rawpage_ui_tests because the ink needs the engine, which that target links none of.
    tst_selectionink.cpp

    # Stage 5 T10 -- guide cases A-90 and A-114's pixel halves: a snapshot against a FRESH RENDER of the page
    # this file asks for itself, within golden/manifest.json's thresholds; the overlays absent, the rotation
    # present, and 300 dpi a re-rasterization rather than an enlargement. Here for tst_selectionink's reason.
    tst_snapshotink.cpp

    # Stage 5 T11 -- guide cases A-92 and A-93 over a REAL page: Reflow view over columns-two.pdf against the
    # generator's labelled blocks, and the place both ways at a block mid-page. Here for tst_selectionink's reason.
    tst_reflowink.cpp

    ${CMAKE_SOURCE_DIR}/app/Application.cpp
    ${CMAKE_SOURCE_DIR}/app/CompositionRoot.cpp
    ${CMAKE_SOURCE_DIR}/app/CrashHandler.cpp
    ${CMAKE_SOURCE_DIR}/app/LogFile.cpp
    ${CMAKE_SOURCE_DIR}/app/RecoveryPrompt.cpp
)

target_include_directories(rawpage_composition_tests PRIVATE "${CMAKE_SOURCE_DIR}")

target_compile_definitions(rawpage_composition_tests PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus"
    RAWPAGE_GOLDEN_MANIFEST="${CMAKE_SOURCE_DIR}/golden/manifest.json"
    RAWPAGE_KILLPROBE_PATH="$<TARGET_FILE:rawpage_killprobe>"
    RAWPAGE_VERSION="${PROJECT_VERSION}")

target_link_libraries(rawpage_composition_tests PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_ops
    rawpage_pdfium
    rawpage_platform
    rawpage_ui
    Catch2::Catch2
    shell32     # app/CrashHandler.cpp: SHGetFolderPathW
)

set_target_properties(rawpage_composition_tests PROPERTIES AUTOMOC ON)

# The catalogs and the ribbon beside the binary, as rawpage.exe has them; the probe, because A-74 runs it.
add_dependencies(rawpage_composition_tests rawpage_translations rawpage_resources rawpage_killprobe)

rawpage_common_flags(rawpage_composition_tests)

include(Catch)
catch_discover_tests(rawpage_composition_tests
    TEST_PREFIX "ui.composition."
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
