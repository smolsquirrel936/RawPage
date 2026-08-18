# ToolchainPreflight.cmake -- the pin checks that must run BEFORE project().
#
# WHY BEFORE project(): a FATAL_ERROR raised inside the toolchain file lands in the middle of
# CMake's compiler detection. CMake does not stop there -- it carries on and prints "unable to
# find a build program corresponding to Ninja" and "CMAKE_CXX_COMPILER not set", two red
# herrings after the real message (I-12). Raised from the top-level CMakeLists before
# project(), the same message aborts cleanly with nothing after it.
#
# cmake/toolchain-mingw.cmake includes this module as well, so the wording exists in exactly
# one place and still fires for anyone who configures with -DCMAKE_TOOLCHAIN_FILE and no
# preflight. Only the EXISTS checks are safe there: that file is re-read for EVERY
# try_compile(). recto_preflight_compiler_runs() starts a process and is preflight-only.

include_guard(GLOBAL)

# The pin's two roots, defined here because preflight runs before the toolchain file has been
# read and both need the same defaults. CACHE set is idempotent, so the toolchain file's own
# include of this module is a no-op.
set(RECTO_MINGW_ROOT "C:/Qt/Tools/mingw1310_64"
    CACHE PATH "Root of the pinned MinGW-w64 GCC 13.1.0 -- Qt's own bundled compiler")
set(RECTO_QT_PREFIX "C:/Qt/6.11.1/mingw_64"
    CACHE PATH "Qt 6.11.1 MinGW prefix")

function(recto_preflight_mingw_root)
    if(NOT EXISTS "${RECTO_MINGW_ROOT}/bin/g++.exe")
        message(FATAL_ERROR
            "\nNo g++.exe under RECTO_MINGW_ROOT='${RECTO_MINGW_ROOT}'.\n\n"
            "Recto builds with Qt's own bundled compiler. Install the 'MinGW 13.1.0 64-bit' "
            "tool package with the Qt Maintenance Tool, or point RECTO_MINGW_ROOT at an "
            "existing Qt Tools MinGW install:\n"
            "    cmake --preset x64-release -DRECTO_MINGW_ROOT=<path>\n")
    endif()
endfunction()

function(recto_preflight_qt_prefix)
    if(NOT EXISTS "${RECTO_QT_PREFIX}")
        message(FATAL_ERROR
            "\nRECTO_QT_PREFIX='${RECTO_QT_PREFIX}' does not exist.\n\n"
            "Nothing was found there, so this is NOT the 'a second Qt won the search' case "
            "that TA-8 reports -- the path is simply not present. Recto links Qt 6.11.1 from "
            "Qt's official MinGW install; point RECTO_QT_PREFIX at it:\n"
            "    cmake --preset x64-release -DRECTO_QT_PREFIX=C:/Qt/6.11.1/mingw_64\n")
    endif()
endfunction()

# I-10. A compiler that is present but cannot START fails inside CMake's own probe, which
# reports "the C++ compiler is not able to compile a simple test program" with no output from
# the compiler at all -- true, and nowhere near the reason. The usual cause on Windows is a
# mingw g++ whose own bin directory is off PATH: the driver spawns cc1plus.exe out of
# lib/gcc/<triple>/<version>/, which is NOT the driver's directory, so the DLLs sitting beside
# g++.exe are never found. Costs one process per configure, and runs once because preflight
# is called from the top-level CMakeLists rather than from the toolchain file.
function(recto_preflight_compiler_runs)
    # -fsyntax-only on an empty translation unit, NOT -dumpversion. The driver answers
    # -dumpversion by itself and reports success even when the toolchain is unusable; only a
    # real compile spawns cc1plus, which is the process that cannot find its DLLs. Measured:
    # a real MSYS2 g++ with its bin off PATH passes -dumpversion and fails this.
    set(_probe "${CMAKE_CURRENT_BINARY_DIR}/recto-preflight-probe.cpp")
    file(WRITE "${_probe}" "int main() { return 0; }\n")

    execute_process(
        COMMAND "${RECTO_MINGW_ROOT}/bin/g++.exe" -x c++ -fsyntax-only "${_probe}"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE  _err)

    if(NOT _rc STREQUAL "0")
        message(FATAL_ERROR
            "\nThe pinned compiler is present but cannot compile:\n"
            "    ${RECTO_MINGW_ROOT}/bin/g++.exe -x c++ -fsyntax-only <empty file>\n"
            "  exit:   ${_rc}\n"
            "  stderr: ${_err}\n\n"
            "On Windows this is almost always PATH. A mingw g++ spawns cc1plus.exe from "
            "lib/gcc/<triple>/<version>/, which is NOT the driver's own directory, so the "
            "DLLs sitting beside g++.exe are never found -- the driver starts, the compiler "
            "proper does not, and it fails with no output at all. Put "
            "${RECTO_MINGW_ROOT}/bin on PATH; for the pinned toolchain that is what "
            "scripts/env.ps1 does:
"
            "    . ./scripts/env.ps1\n")
    endif()
endfunction()

# I-13. vcpkg is the OUTER toolchain: CMakePresets sets
# CMAKE_TOOLCHAIN_FILE to "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake", and an
# undefined $env{...} expands to the empty string rather than erroring. CMake then reports
# "Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake" -- a path that never
# existed, naming neither the variable nor the fix. Checked here because preflight runs
# before project(), which is where CMake would otherwise resolve it.
function(recto_preflight_vcpkg)
    # TO_CMAKE_PATH so the message does not come out as
    # "C:\Users\squirrel\vcpkg/bootstrap-vcpkg.bat", half one separator and half the other.
    file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _root)

    if(_root STREQUAL "")
        message(FATAL_ERROR
            "\nVCPKG_ROOT is not set in this process.\n\n"
            "The presets build CMAKE_TOOLCHAIN_FILE out of it, so an unset value silently "
            "becomes the path '/scripts/buildsystems/vcpkg.cmake', which has never existed "
            "anywhere. Note that setx writes the registry and does NOT reach a shell that "
            "is already open. Either dot-source the build environment, which sets it:\n"
            "    . ./scripts/env.ps1\n"
            "or run scripts/bootstrap.ps1, which reports what is missing and how to fix it. "
            "See README.md, Prerequisites 2.\n")
    endif()

    if(NOT EXISTS "${_root}/scripts/buildsystems/vcpkg.cmake")
        message(FATAL_ERROR
            "\nVCPKG_ROOT='${_root}' has no vcpkg toolchain file.\n\n"
            "Expected: ${_root}/scripts/buildsystems/vcpkg.cmake\n\n"
            "The variable points somewhere, but not at a vcpkg clone -- a moved or deleted "
            "checkout is the usual cause. Clone and bootstrap it:\n"
            "    git clone https://github.com/microsoft/vcpkg ${_root}\n"
            "    ${_root}/bootstrap-vcpkg.bat\n"
            "scripts/bootstrap.ps1 checks this and prints the same commands.\n")
    endif()
endfunction()

# Everything the pin can check before a compiler exists. Callers decide whether Qt applies:
# the gate and portable configurations build without it.
macro(recto_preflight)
    cmake_parse_arguments(_RECTO_PF "QT;VCPKG" "" "" ${ARGN})
    recto_preflight_mingw_root()
    recto_preflight_compiler_runs()
    if(_RECTO_PF_QT)
        recto_preflight_qt_prefix()
    endif()
    if(_RECTO_PF_VCPKG)
        recto_preflight_vcpkg()
    endif()
endmacro()
