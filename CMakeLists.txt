# Recto -- root build definition.
#
# Layers are separate static libraries linked by one executable, so the LINKER itself
# catches a class of layering violation before tools/layer-lint even runs
# (Tech-Stack-Plan.md 6.3). Dependencies point inward only; see 4.2 for the full rule.
#
# Stage 0 builds an empty recto.exe. That is the point: the target graph, the flag set,
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
# Gated on the host, not on WIN32: WIN32 does not exist before project(). RECTO_GATES_ONLY
# and RECTO_PORTABLE_SUBSET arrive from the presets as cache variables, so they are readable
# here, and neither configuration uses the pinned toolchain at all.
if(CMAKE_HOST_WIN32 AND NOT RECTO_GATES_ONLY AND NOT RECTO_PORTABLE_SUBSET)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ToolchainPreflight.cmake")
    recto_preflight(QT)
endif()

project(Recto
    VERSION 0.1.0
    DESCRIPTION "Windows-first desktop PDF editor"
    HOMEPAGE_URL "https://github.com/smolsquirrel936/Recto"
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
option(RECTO_PORTABLE_SUBSET
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
option(RECTO_GATES_ONLY
    "Build only the enforcement gates -- no Qt, no vcpkg, no layer libraries"
    OFF)

option(RECTO_BUILD_FUZZERS
    "Build the libFuzzer targets (portable-fuzz preset). No targets exist until Stage 2."
    OFF)

set(RECTO_TARGET_ARCH "x64" CACHE STRING "Target architecture: x64, or arm64 (unresolved -- see O6)")

# --- arm64 ------------------------------------------------------------------
# The preset exists so the target is not forgotten, but it cannot configure yet and
# saying so plainly beats failing three assertions down with a confusing message.
if(RECTO_TARGET_ARCH STREQUAL "arm64")
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
if(RECTO_PORTABLE_SUBSET OR RECTO_GATES_ONLY)
    message(STATUS "Gate build: toolchain pin NOT applied -- this is not a shipping build")
else()
    include(ToolchainAssert)
endif()

include(RectoWarnings)
include(RectoQt)
include(RectoChecks)

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
if(RECTO_GATES_ONLY)
    message(STATUS "Gates only: Qt is NOT required")
elseif(RECTO_PORTABLE_SUBSET)
    find_package(Qt6 REQUIRED COMPONENTS Core)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
    # TA-8 can only run once Qt6_VERSION and Qt6_DIR exist. Skipped for the gate builds for
    # the same reason the rest of the pin is: a Linux gate build uses whatever QtCore the
    # distro ships, and must.
    recto_assert_qt()
endif()

# --- Gates -------------------------------------------------------------------
# Before the layers, and independent of them: the gate executables link nothing but the
# standard library, so `--target checks` compiles two small programs rather than the
# whole project. That ordering is the guarantee, not a convenience.
add_subdirectory(tools)

# --- Layers ------------------------------------------------------------------
if(NOT RECTO_GATES_ONLY)
    add_subdirectory(core)
    add_subdirectory(pdf)

    if(NOT RECTO_PORTABLE_SUBSET)
        add_subdirectory(platform)
        add_subdirectory(ui)
        add_subdirectory(app)
    endif()
endif()

# --- Tests -------------------------------------------------------------------
add_subdirectory(tests/gates)

if(NOT RECTO_GATES_ONLY)
    add_subdirectory(tests/unit)   # needs Catch2 from vcpkg, and core/ to test
endif()

# --- Gate inputs -------------------------------------------------------------
# Written last, after every target exists: a global property read earlier would see a
# partial module list and let tools/license-audit pass by accident.
recto_write_qt_module_list()
recto_add_checks_target()

# --- Summary -----------------------------------------------------------------
message(STATUS "")
message(STATUS "Recto ${PROJECT_VERSION}")
message(STATUS "  compiler        ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Qt              ${Qt6_VERSION}")
message(STATUS "  portable subset ${RECTO_PORTABLE_SUBSET}")
message(STATUS "  output          ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
if(RECTO_BUILD_FUZZERS)
    # Said out loud on purpose: a fuzz preset that silently builds nothing is
    # indistinguishable from one that is broken.
    message(STATUS "  fuzzers         ENABLED, but NO FUZZ TARGETS EXIST YET -- they arrive")
    message(STATUS "                  with pdf/cos/Lexer and Parser at Stage 2")
endif()
message(STATUS "")
