# toolchain-mingw.cmake -- the pinned toolchain.
#
# THE PIN, in one sentence: we build with the compiler Qt itself ships, so "does our
# toolchain match Qt's" is true by construction rather than by discipline.
#
# Qt 6.11.1's MinGW build is GCC 13.1.0, x86_64-w64-mingw32, SEH, posix threads, and
# **msvcrt -- not UCRT**. That last row is the one that bites: an MSYS2 UCRT64
# toolchain looks modern and correct and is a different C runtime and a different
# libstdc++ against this Qt. It links. It crashes somewhere unrelated, weeks later.
# See Tech-Stack-Plan.md 3.3 (A7) for the measurements this pin is built on.
#
# ---------------------------------------------------------------------------------
# THIS FILE IS READ REPEATEDLY. CMake re-reads the toolchain file for EVERY
# try_compile(), so anything expensive or fallible here runs many times over and can
# fail a compiler probe for a reason that has nothing to do with the probe.
#
# So this file does only cheap, idempotent setup. Every assertion lives in
# cmake/ToolchainAssert.cmake, which the top-level CMakeLists includes exactly once
# after project(). See Stage0-Toolchain-Plan.md 5.1.
# ---------------------------------------------------------------------------------

# Cache variables, not hard-coded strings: a machine with Qt installed elsewhere
# overrides these, and the assertions still hold it to the same GCC.
set(RECTO_MINGW_ROOT "C:/Qt/Tools/mingw1310_64"
    CACHE PATH "Root of the pinned MinGW-w64 GCC 13.1.0 -- Qt's own bundled compiler")
set(RECTO_QT_PREFIX "C:/Qt/6.11.1/mingw_64"
    CACHE PATH "Qt 6.11.1 MinGW prefix")

# try_compile() runs in a fresh cache, so without this the values above revert to
# their defaults inside every probe -- and an overridden root would be silently
# ignored exactly where it matters.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RECTO_MINGW_ROOT RECTO_QT_PREFIX)

# NOTE: CMAKE_SYSTEM_NAME is deliberately NOT set here.
# We build Windows binaries on Windows. Setting CMAKE_SYSTEM_NAME in a toolchain file
# switches CMake into cross-compiling mode, which disables try_run(), changes how
# find_package() searches, and makes several Qt CMake paths behave differently -- all
# for no benefit on a native build. The plan sketched it; the implementation drops it
# on purpose.

if(NOT EXISTS "${RECTO_MINGW_ROOT}/bin/g++.exe")
    message(FATAL_ERROR
        "No g++.exe under RECTO_MINGW_ROOT='${RECTO_MINGW_ROOT}'.\n"
        "Recto builds with Qt's own bundled compiler. Install the 'MinGW 13.1.0 64-bit' "
        "tool package with the Qt Maintenance Tool, or point RECTO_MINGW_ROOT at an "
        "existing Qt Tools MinGW install:\n"
        "    cmake --preset x64-release -DRECTO_MINGW_ROOT=<path>")
endif()

# A -fplugin flag is checked HERE as well as in TA-9, and the duplication is deliberate.
# TA-9 runs after project(), which is too late for one case: a plugin that fails to load
# breaks GCC's own compiler probe first, and CMake then reports "the C++ compiler is not
# able to compile a simple test program" -- true, useless, and nowhere near the actual
# reason. This scan is a cheap deterministic string match, so it is safe in a file that
# is re-read for every try_compile(), and it puts the licence consequence in front of
# whoever added the flag. TA-9 still owns the case that matters most: a plugin that DOES
# load, and so never trips the probe at all.
foreach(_flag_var CMAKE_CXX_FLAGS CMAKE_C_FLAGS)
    if(DEFINED ${_flag_var} AND "${${_flag_var}}" MATCHES "-fplugin")
        message(FATAL_ERROR
            "\n${_flag_var} loads a GCC plugin: '${${_flag_var}}'\n\n"
            "A non-free compiler plugin voids the GCC Runtime Library Exception, which is "
            "the only thing permitting Recto to ship proprietary binaries linked against "
            "libstdc++. Losing it would relicense Recto's own source under GPL-3.0. "
            "See Tech-Stack-Plan.md 8.1.\n")
    endif()
endforeach()

set(CMAKE_C_COMPILER   "${RECTO_MINGW_ROOT}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${RECTO_MINGW_ROOT}/bin/g++.exe")
set(CMAKE_RC_COMPILER  "${RECTO_MINGW_ROOT}/bin/windres.exe")

# Ninja ships with the Qt install too (Tech-Stack-Plan.md 6.3: the Qt installation IS
# the build environment). Naming it here means a clean machine does not additionally
# need it on PATH -- which is one fewer undocumented step for the M-01 bootstrap case.
if(EXISTS "C:/Qt/Tools/Ninja/ninja.exe" AND NOT DEFINED CMAKE_MAKE_PROGRAM)
    set(CMAKE_MAKE_PROGRAM "C:/Qt/Tools/Ninja/ninja.exe" CACHE FILEPATH "")
endif()

list(APPEND CMAKE_PREFIX_PATH "${RECTO_QT_PREFIX}")
