# RawPage -- root build definition.
#
# Layers are separate static libraries linked by one executable, so the LINKER itself
# catches a class of layering violation before tools/layer-lint even runs
# (Tech-Stack-Plan.md 6.3). Dependencies point inward only; see 4.2 for the full rule.
#
# Stage 0 builds an empty rawpage.exe. That is the point: the target graph, the flag set,
# the toolchain pin, and the gates all exist before there is any product code to
# violate them.

cmake_minimum_required(VERSION 3.25)

# --- Preflight, BEFORE project() ---------------------------------------------
# The pin's cheap checks run here rather than only from the toolchain file, because a
# FATAL_ERROR raised inside a toolchain file lands in the middle of CMake's compiler
# detection: CMake carries on and prints "unable to find a build program corresponding to
# Ninja" and "CMAKE_CXX_COMPILER not set" after the real message (I-12). Here it aborts
# cleanly. Same reason the -fplugin scan is duplicated -- see toolchain-mingw.cmake.
#
# Gated on the host, not on WIN32: WIN32 does not exist before project(). RAWPAGE_GATES_ONLY
# and RAWPAGE_PORTABLE_SUBSET arrive from the presets as cache variables, so they are readable
# here, and neither configuration uses the pinned toolchain at all.
if(CMAKE_HOST_WIN32 AND NOT RAWPAGE_GATES_ONLY AND NOT RAWPAGE_PORTABLE_SUBSET)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ToolchainPreflight.cmake")
    rawpage_preflight(QT VCPKG)
endif()

project(RawPage
    VERSION 0.1.0
    DESCRIPTION "Windows-first desktop PDF editor"
    HOMEPAGE_URL "https://github.com/smolsquirrel936/RawPage"
    LANGUAGES CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Portable subset ---------------------------------------------------------
# Declared before the toolchain pin because it decides whether the pin applies at all.
#
# The portable-asan / portable-fuzz presets build ONLY the layers that are portable by
# rule: core/ (no display, no PDF engine) and pdf/cos/ (first-party C++). This is what
# recovers the sanitizer and fuzzing gates MinGW GCC cannot provide on Windows
# (Tech-Stack-Plan.md 6.3, risk R7) -- and it mechanically proves the portability the
# layer contract already claims.
option(RAWPAGE_PORTABLE_SUBSET
    "Build only core/ and pdf/cos/ -- the layers that must build with no display and no PDF engine"
    OFF)

# --- Gates-only -------------------------------------------------------------
# Builds tools/ and tests/gates/ and NOTHING else: no Qt, no vcpkg, no layers.
#
# This is what makes decision D0-1 true rather than aspirational. The CI `gates` job runs on
# a bare runner with no Qt and no vcpkg installed, so a layering or licence violation fails
# in under a minute instead of behind a full toolchain provisioning step. Without this
# option the root would still call find_package(Qt6 REQUIRED) and the job could not exist --
# the claim would have been false the first time anyone tested it.
option(RAWPAGE_GATES_ONLY
    "Build only the enforcement gates -- no Qt, no vcpkg, no layer libraries"
    OFF)

option(RAWPAGE_BUILD_FUZZERS
    "Build the libFuzzer targets (portable-fuzz preset): fuzz_cos_lexer, fuzz_cos_parser, fuzz_cos_roundtrip."
    OFF)

set(RAWPAGE_TARGET_ARCH "x64" CACHE STRING "Target architecture: x64, or arm64 (unresolved -- see O6)")

# --- arm64 ------------------------------------------------------------------
# The preset exists so the target is not forgotten, but it cannot configure yet and
# saying so plainly beats failing three assertions down with a confusing message.
if(RAWPAGE_TARGET_ARCH STREQUAL "arm64")
    message(FATAL_ERROR
        "\narm64 has no toolchain yet -- this preset is a placeholder.\n\n"
        "The pinned compiler is x86_64 GCC 13.1.0 (Qt's own), and MinGW-w64 GCC's aarch64 "
        "support is immature where LLVM-MinGW's is not. Whether arm64 means switching that "
        "target to LLVM-MinGW is open decision O6, owned by Stage 15, with risk R8 behind it.\n\n"
        "This is a known, recorded gap -- not a broken build. Use --preset x64-release.\n")
endif()

# --- The toolchain pin -------------------------------------------------------
# Included immediately after project(), once, and NOT from the toolchain file itself:
# CMake re-reads the toolchain file for every try_compile(), so compiler probes placed
# there run repeatedly and can fail a probe for reasons that have nothing to do with
# the probe. See Stage0-Toolchain-Plan.md 5.1.
#
# The pin describes the SHIPPING build. The portable presets deliberately compile the
# same sources with a different compiler on a different OS -- that is the entire point
# of them -- so applying an "exactly GCC 13.1.0, SEH, msvcrt" assertion there would
# reject the configuration we are specifically trying to prove works.
if(RAWPAGE_PORTABLE_SUBSET OR RAWPAGE_GATES_ONLY)
    message(STATUS "Gate build: toolchain pin NOT applied -- this is not a shipping build")
else()
    include(ToolchainAssert)
endif()

include(RawPageWarnings)
include(RawPageQt)
include(RawPageChecks)

# --- The PDFium pin (Stage 2 T2, decision D2-3) ------------------------------
# The second pinned dependency, asserted rather than assumed for the same reason as the first: a
# golden-image baseline is a claim about ONE rasterizer, and an unnoticed engine bump reclassifies
# every rendering regression as a baseline update. PA-1 .. PA-5 are in cmake/PdfiumAssert.cmake.
#
# Skipped for the same two configurations the toolchain pin is: the `gates` build has no layers at
# all, and the portable subset is core/ + pdf/cos/ on Linux, where there is no PDFium to pin -- which
# is exactly the property D2-2's library split exists to keep true.
if(RAWPAGE_PORTABLE_SUBSET OR RAWPAGE_GATES_ONLY)
    message(STATUS "Gate build: PDFium pin NOT applied -- no engine is linked here")
else()
    include(PdfiumAssert)
    rawpage_assert_pdfium("${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium")
endif()

enable_testing()

# --- Build layout ------------------------------------------------------------
# Everything runnable lands in one directory so the Qt and GCC runtime DLLs sit beside
# the executable, which is both how windeployqt expects to work (Stage 15) and what
# makes the LGPL replaceability check meaningful.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Position-independent code everywhere, so a layer can later become a shared library
# (the satellite-DLL escape hatch in risk R8) without a rebuild of everything.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Qt ----------------------------------------------------------------------
# Not built by vcpkg: it comes from Qt's official MinGW distribution, both because
# vcpkg's Qt-on-MinGW path is fragile and because D8 requires shipping a Qt the user
# can rebuild and drop in. The exact-version assertion belongs to ToolchainAssert (TA-8).
if(RAWPAGE_GATES_ONLY)
    message(STATUS "Gates only: Qt is NOT required")
elseif(RAWPAGE_PORTABLE_SUBSET)
    find_package(Qt6 REQUIRED COMPONENTS Core)
else()
    # LinguistTools supplies `lrelease`, which i18n/ runs to compile the .ts catalogs. It is a
    # build tool rather than a linked module, so it is deliberately NOT passed through
    # rawpage_link_qt(): qt-modules.txt is the LINK closure tools/license-audit audits, and adding
    # a tool we only execute would make that list mean something else.
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg LinguistTools)
    # TA-8 can only run once Qt6_VERSION and Qt6_DIR exist. Skipped for the gate builds for
    # the same reason the rest of the pin is: a Linux gate build uses whatever QtCore the
    # distro ships, and must.
    rawpage_assert_qt()
endif()

# --- Gates -------------------------------------------------------------------
# Before the layers, and independent of them: the gate executables link nothing but the
# standard library, so `--target checks` compiles four small programs rather than the
# whole project. That ordering is the guarantee, not a convenience.
add_subdirectory(tools)

# --- Layers ------------------------------------------------------------------
if(NOT RAWPAGE_GATES_ONLY)
    add_subdirectory(core)
    add_subdirectory(pdf)

    if(NOT RAWPAGE_PORTABLE_SUBSET)
        add_subdirectory(platform)
        add_subdirectory(ui)
        add_subdirectory(app)

        # Data, not code -- but data the running program loads by path, so both are built like
        # anything else and land beside the executable. See i18n/CMakeLists.txt, and res/'s own file
        # for why the ribbon resource is VALIDATED before it is copied (D1-8).
        add_subdirectory(i18n)
        add_subdirectory(res)
    endif()
endif()

# --- Tests -------------------------------------------------------------------
add_subdirectory(tests/gates)

if(NOT RAWPAGE_GATES_ONLY)
    add_subdirectory(tests/unit)   # needs Catch2 from vcpkg, and core/ to test

    # The fuzz targets (Stage 2 T5). ONLY the portable-fuzz preset sets this: MinGW GCC has no
    # libFuzzer, so there is nothing to build on Windows and a guard here is honest rather than
    # defensive. tests/fuzz/CMakeLists.txt asserts its own target COUNT at configure time, which is
    # guide case A-27 -- "zero fuzz targets is a failure, not a pass".
    if(RAWPAGE_BUILD_FUZZERS)
        add_subdirectory(tests/fuzz)
    endif()

    # The seed writer for fuzz_cos_roundtrip (Stage 3 T13, D3-17) is NOT behind that guard: it is a
    # plain executable over the portable libraries, so it builds and its case runs on Windows and under
    # portable-asan too. A subdirectory of tests/fuzz only on disk -- see tests/fuzz/seeds/CMakeLists.txt.
    add_subdirectory(tests/fuzz/seeds)

    # The platform suite is Windows-only and deliberately a SEPARATE target: tests/unit must keep
    # linking core/ and pdf/ and nothing else, which is what portable-asan proves on a machine with
    # no display and no engine. See tests/platform/CMakeLists.txt.
    if(NOT RAWPAGE_PORTABLE_SUBSET)
        add_subdirectory(tests/platform)

        # The widget suite, for the reason D1-4 gives and tests/platform repeats from the other
        # side: tests/unit links core/ and pdf/ and nothing else, and the first widget test is
        # exactly the pressure that would otherwise weaken that. Windows only, offscreen.
        add_subdirectory(tests/ui)

        # The suite that links the ENGINE (Stage 2 T2). Separate from tests/unit for the reason
        # D2-2 and tests/render/CMakeLists.txt give: the raster suite is the pressure that would
        # otherwise put PDFium into the headless closure. Windows only, and it needs no display.
        add_subdirectory(tests/render)

        # The golden-image suite (Stage 2 T17). A fifth target rather than three more files in
        # tests/render, because it needs QtGui for the PNG codec and tests/render's separateness
        # rests on it linking no Qt GUI library at all -- see tests/golden/CMakeLists.txt. Its
        # CTest cases stay inside the `render.` prefix.
        add_subdirectory(tests/golden)

        # The measurements. One benchmark today -- A3's criterion R-9, the only one of the eight
        # that is a number rather than a judgement -- and Stage 2's per-tile render latency joins
        # it. Windows only, because it links rawpage_ui; and a bench under ASan would be a
        # measurement of ASan.
        add_subdirectory(tests/bench)

        # The product's own composition (Stage 3 T11): CompositionRoot with the real engine and OS bundle and a
        # MainWindow on it, in one binary -- which every target above exists to keep apart, so it is a sixth.
        # Its cases sit inside the `ui.` prefix. See tests/composition/CMakeLists.txt.
        add_subdirectory(tests/composition)
    endif()
endif()

# --- Gate inputs -------------------------------------------------------------
# Written last, after every target exists: a global property read earlier would see a
# partial module list and let tools/license-audit pass by accident.
rawpage_write_qt_module_list()
rawpage_add_checks_target()

# --- Summary -----------------------------------------------------------------
message(STATUS "")
message(STATUS "RawPage ${PROJECT_VERSION}")
message(STATUS "  compiler        ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Qt              ${Qt6_VERSION}")
message(STATUS "  PDFium          ${RAWPAGE_PDFIUM_VERSION}")
message(STATUS "  portable subset ${RAWPAGE_PORTABLE_SUBSET}")
message(STATUS "  output          ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
if(RAWPAGE_BUILD_FUZZERS)
    # From Stage 0 to Stage 2 T5 this said "NO FUZZ TARGETS EXIST YET", deliberately: a fuzz preset
    # that silently builds nothing is indistinguishable from one that is broken. T5 gave it its
    # first real target, so the line now names what is actually in the graph -- and tests/fuzz
    # FAILS CONFIGURE if that count is ever wrong, which is guide case A-27's "zero is a failure,
    # not a pass" made structural rather than remembered.
    message(STATUS "  fuzzers         ENABLED")
endif()
message(STATUS "")
