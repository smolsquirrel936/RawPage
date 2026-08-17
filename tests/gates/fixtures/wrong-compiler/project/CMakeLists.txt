# A minimal project that exercises ONLY the toolchain pin.
#
# WHY NOT CONFIGURE THE REAL PROJECT: each wrong-compiler case is a full CMake configure,
# and the real one pulls in vcpkg manifest mode and Qt -- roughly 200 seconds apiece. Six
# cases would put twenty minutes into a suite that otherwise runs in half a second, and a
# test suite people stop running is worse than one that tests slightly less.
#
# This project includes exactly what is under test: cmake/ToolchainAssert.cmake, plus
# RectoWarnings for the -static guard. TA-8 (Qt) is a function called only by the real
# root, so it is out of scope here and stays covered by the ordinary build.

cmake_minimum_required(VERSION 3.25)
project(RectoWrongCompilerFixture LANGUAGES CXX)

if(NOT DEFINED RECTO_SOURCE_DIR)
    message(FATAL_ERROR "fixture requires -DRECTO_SOURCE_DIR=<repo root>")
endif()

list(PREPEND CMAKE_MODULE_PATH "${RECTO_SOURCE_DIR}/cmake")

include(ToolchainAssert)
include(RectoWarnings)

message(STATUS "wrong-compiler fixture: all toolchain assertions passed")
