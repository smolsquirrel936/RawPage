# tests/golden -- the golden-image suite. Stage 2 T17, guide cases A-48 … A-51.
#
# ---------------------------------------------------------------------------------------------
# WHY IT IS A FIFTH TEST TARGET AND NOT THREE MORE FILES IN tests/render.
#
# tests/render links rawpage_core, rawpage_cos, rawpage_pdfium and rawpage_platform. This suite
# needs all four AND QtGui, for the PNG codec that reads and writes the baselines -- and QtGui is
# the one dependency tests/render has deliberately never had, because "the engine needs no display"
# is a property that target establishes by running in a process with no Qt GUI library at all.
# Folding a PNG codec into it would quietly retire that.
#
# It is also the target that shares code with a TOOL: tools/golden-update compiles GoldenSuite.cpp
# in so that the producer and the consumer of golden/ cannot disagree about what a render is, how
# it is encoded, or what "the same" means.
#
# THE CTEST PREFIX IS `render.golden.`, which is not cosmetic. Guide case A-48's own command is
# `ctest -R render.golden`, and keeping these cases inside the `render.` namespace means CI's
# non-empty-prefix loop covers them the moment T18 adds `render` to that list -- rather than
# creating a sixth prefix that would need remembering separately.
#
# WINDOWS ONLY, for tests/render's reason: it rasterizes, and PDFium is a Windows DLL here. It needs
# no display -- QImage is a raster class and Qt's PNG handler is built into QtGui rather than being
# a plugin, so nothing here creates a QGuiApplication.

find_package(Catch2 3 REQUIRED)

add_executable(rawpage_golden_tests
    GoldenSuite.cpp
    tst_golden.cpp
    # Stage 6 T8 (D6-6, guide A-64): the appearance baselines -- a declarative spec of pages our builder draws,
    # compared with golden/appearance/. AppearanceCases.cpp is compiled into golden-update and
    # rawpage_render_tests too, so the three read one spec.
    AppearanceCases.cpp
    tst_appearancegolden.cpp
)

target_include_directories(rawpage_golden_tests PRIVATE "${CMAKE_SOURCE_DIR}")

# Absolute source paths, the same mechanism tests/unit and tests/render use and for the same
# reason: a test that guessed a relative path would pass or fail on whatever working directory
# CTest happened to use.
#
# RAWPAGE_GOLDEN_FAILURE_DIR is in the BUILD tree, never the source tree. A failing case writes an
# expected/actual/diff triple there, and writing those beside the committed baselines would leave
# the next `git status` full of files that look like new references.
target_compile_definitions(rawpage_golden_tests PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus"
    RAWPAGE_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/golden"
    RAWPAGE_GOLDEN_FAILURE_DIR="${CMAKE_BINARY_DIR}/golden-failures"
    RAWPAGE_GOLDEN_UPDATE_EXE="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/golden-update.exe")

target_link_libraries(rawpage_golden_tests PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_ops
    rawpage_pdfium
    rawpage_platform
    Catch2::Catch2WithMain
)

# Core for everything else, Gui for the PNG codec. Already in the project's Qt link closure through
# rawpage_ui, so tools/license-audit sees no new component. Through the wrapper, never Qt6::X
# directly (D0-5).
rawpage_link_qt(rawpage_golden_tests Core Gui)

rawpage_common_flags(rawpage_golden_tests)

# The A-50 case drives golden-update as a child process, so the tool has to exist before this suite
# runs. A dependency rather than a CTest fixture: the executable is a build artifact, and a missing
# one should fail the BUILD rather than produce a test that skips itself.
add_dependencies(rawpage_golden_tests golden-update)

include(Catch)
catch_discover_tests(rawpage_golden_tests
    TEST_PREFIX "render.golden."
    # libstdc++/libgcc as everywhere else, plus the output directory: pdfium.dll is deployed there
    # rather than being on any system path, and without it every case fails to LAUNCH with
    # 0xC0000135, which looks nothing like a test failure.
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
rawpage_serial_tests(rawpage_golden_tests PREFIX "render.golden."
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
