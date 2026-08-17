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

project(Recto
    VERSION 0.1.0
    DESCRIPTION "Windows-first desktop PDF editor"
    HOMEPAGE_URL "https://github.com/smolsquirrel936/Recto"
    LANGUAGES CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- The toolchain pin -------------------------------------------------------
# Included immediately after project(), once, and NOT from the toolchain file itself:
# CMake re-reads the toolchain file for every try_compile(), so compiler probes placed
# there run repeatedly and can fail a probe for reasons that have nothing to do with
# the probe. See Stage0-Toolchain-Plan.md 5.1.
include(ToolchainAssert)

include(RectoWarnings)
include(RectoQt)

# --- Build layout ------------------------------------------------------------
# Everything runnable lands in one directory so the Qt and GCC runtime DLLs sit beside
# the executable, which is both how windeployqt expects to work (Stage 15) and what
# makes the LGPL replaceability check meaningful.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Position-independent code everywhere, so a layer can later become a shared library
# (the satellite-DLL escape hatch in risk R8) without a rebuild of everything.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Portable subset ---------------------------------------------------------
# The portable-asan / portable-fuzz presets build ONLY the layers that are portable by
# rule: core/ (no display, no PDF engine) and pdf/cos/ (first-party C++). This is what
# recovers the sanitizer and fuzzing gates MinGW GCC cannot provide on Windows
# (Tech-Stack-Plan.md 6.3, risk R7) -- and it mechanically proves the portability the
# layer contract already claims.
option(RECTO_PORTABLE_SUBSET
    "Build only core/ and pdf/cos/ -- the layers that must build with no display and no PDF engine"
    OFF)

# --- Qt ----------------------------------------------------------------------
# Not built by vcpkg: it comes from Qt's official MinGW distribution, both because
# vcpkg's Qt-on-MinGW path is fragile and because D8 requires shipping a Qt the user
# can rebuild and drop in. The exact-version assertion belongs to ToolchainAssert (TA-8).
if(NOT RECTO_PORTABLE_SUBSET)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()

# TA-8. Deferred to here because it can only run once Qt6_VERSION and Qt6_DIR exist.
recto_assert_qt()

# --- Layers ------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(pdf)

if(NOT RECTO_PORTABLE_SUBSET)
    add_subdirectory(platform)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()

# --- Gate inputs -------------------------------------------------------------
# Written last, after every target exists: a global property read earlier would see a
# partial module list and let tools/license-audit pass by accident.
recto_write_qt_module_list()

# --- Summary -----------------------------------------------------------------
message(STATUS "")
message(STATUS "Recto ${PROJECT_VERSION}")
message(STATUS "  compiler        ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Qt              ${Qt6_VERSION}")
message(STATUS "  portable subset ${RECTO_PORTABLE_SUBSET}")
message(STATUS "  output          ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
