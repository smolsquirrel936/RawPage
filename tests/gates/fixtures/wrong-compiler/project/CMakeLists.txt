# A minimal project that exercises ONLY the toolchain pin.
#
# WHY NOT CONFIGURE THE REAL PROJECT: each wrong-compiler case is a full CMake configure,
# and the real one pulls in vcpkg manifest mode and Qt -- roughly 200 seconds apiece. Six
# cases would put twenty minutes into a suite that otherwise runs in half a second, and a
# test suite people stop running is worse than one that tests slightly less.
#
# This project includes exactly what is under test: cmake/ToolchainAssert.cmake, plus
# RectoWarnings for the -static guard.
#
# TA-8 (Qt) is a function the real root calls after find_package(Qt6). It is opt-in here,
# with Qt6_VERSION and Qt6_DIR injected by the caller: its two prefix branches -- "the path
# does not exist" versus "a different Qt won" -- cannot otherwise be tested without a second
# Qt installed, and I-09 showed what happens to message text no case asserts.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED RECTO_SOURCE_DIR)
    message(FATAL_ERROR "fixture requires -DRECTO_SOURCE_DIR=<repo root>")
endif()

# Preflight before project(), exactly as the real root does -- these cases exist to check the
# messages a person actually sees, and the pre-project ones abort without CMake's follow-on
# noise (I-12). The compiler-runs probe (I-10) is here too, and is why fake-root/ exists.
include("${RECTO_SOURCE_DIR}/cmake/ToolchainPreflight.cmake")

# The fixture does not use vcpkg, so that check is opt-in -- but it has to be reachable
# from here, because the only way to test an unset VCPKG_ROOT is to run a configure with
# the variable removed from the environment.
if(RECTO_FIXTURE_VCPKG)
    recto_preflight(QT VCPKG)
else()
    recto_preflight(QT)
endif()

project(RectoWrongCompilerFixture LANGUAGES CXX)

list(PREPEND CMAKE_MODULE_PATH "${RECTO_SOURCE_DIR}/cmake")

include(ToolchainAssert)
include(RectoWarnings)

# Opt-in, because every other case here must reach the end of the file without it.
if(RECTO_FIXTURE_ASSERT_QT)
    recto_assert_qt()
endif()

message(STATUS "wrong-compiler fixture: all toolchain assertions passed")
