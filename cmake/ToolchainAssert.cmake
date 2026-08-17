# ToolchainAssert.cmake -- TA-1 .. TA-9, the assertions that keep risk A7 settled
# rather than merely documented.
#
# Included ONCE, from the top-level CMakeLists immediately after project(). It is
# separate from cmake/toolchain-mingw.cmake because that file is re-read for every
# try_compile(); see the header there.
#
# Every failure message must name the offending value AND the fix, because the person
# reading it is by definition someone whose environment differs from the pin -- most
# likely a developer whose MSYS2 install won a PATH race. A message that only says
# "wrong compiler" turns a five-second fix into an afternoon.

include_guard(GLOBAL)

function(_recto_pin_error label found expected fix)
    message(FATAL_ERROR
        "\n"
        "Toolchain pin violated: ${label}\n"
        "  found:    ${found}\n"
        "  expected: ${expected}\n"
        "\n"
        "${fix}\n"
        "\n"
        "Recto builds with Qt's own bundled GCC (Tech-Stack-Plan.md 3.3, risk A7). A "
        "different libstdc++ or C runtime against this Qt does not fail here -- it "
        "fails as an unexplained crash several stages later. That is the entire "
        "reason this check exists.\n")
endfunction()

set(_recto_expected_version "13.1.0")
set(_recto_expected_triple  "x86_64-w64-mingw32")

# --- TA-1 · Compiler is GCC --------------------------------------------------
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    _recto_pin_error("TA-1 compiler vendor" "${CMAKE_CXX_COMPILER_ID}" "GNU"
        "Configure with the pinned toolchain file:\n    cmake --preset x64-release")
endif()

# --- TA-2 · Exact GCC version ------------------------------------------------
# EXACT, not a minimum. A newer GCC is not "safer" here: it means a different
# libstdc++ from the one Qt 6.11.1's binaries were built against.
if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "${_recto_expected_version}")
    _recto_pin_error("TA-2 compiler version" "${CMAKE_CXX_COMPILER_VERSION}" "${_recto_expected_version}"
        "Use C:/Qt/Tools/mingw1310_64. If Qt was upgraded, the compiler moves with it "
        "in the same change -- update RECTO_MINGW_ROOT and this expected version together.")
endif()

# --- TA-3 · Target triple ----------------------------------------------------
execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -dumpmachine
    OUTPUT_VARIABLE _recto_triple
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _recto_rc)
if(NOT _recto_rc EQUAL 0)
    message(FATAL_ERROR "TA-3: could not run '${CMAKE_CXX_COMPILER} -dumpmachine' (exit ${_recto_rc}).")
endif()
if(NOT _recto_triple STREQUAL "${_recto_expected_triple}")
    _recto_pin_error("TA-3 target triple" "${_recto_triple}" "${_recto_expected_triple}"
        "A 32-bit or MSYS/Cygwin-targeted GCC cannot produce the binaries this project ships.")
endif()

# --- TA-4 · Exception model is SEH ------------------------------------------
# Checked by which unwinder DLL the toolchain ships: SEH and DWARF-2 exception models
# are not interchangeable, and Qt's binaries are built against the SEH one.
if(NOT EXISTS "${RECTO_MINGW_ROOT}/bin/libgcc_s_seh-1.dll")
    _recto_pin_error("TA-4 exception model" "no libgcc_s_seh-1.dll in ${RECTO_MINGW_ROOT}/bin" "SEH"
        "This looks like a DWARF-2 or SJLJ GCC build. Qt 6.11.1 mingw_64 requires SEH.")
endif()
if(EXISTS "${RECTO_MINGW_ROOT}/bin/libgcc_s_dw2-1.dll")
    _recto_pin_error("TA-4 exception model" "libgcc_s_dw2-1.dll is present" "SEH only"
        "A DWARF-2 unwinder alongside the SEH one means this is not the Qt Tools compiler.")
endif()

# --- TA-5 · Thread model is posix -------------------------------------------
# `g++ -v` writes its banner to stderr.
execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -v
    ERROR_VARIABLE _recto_banner
    OUTPUT_QUIET
    RESULT_VARIABLE _recto_rc)
if(NOT _recto_banner MATCHES "Thread model: posix")
    string(REGEX MATCH "Thread model: [a-z0-9-]+" _recto_found_model "${_recto_banner}")
    _recto_pin_error("TA-5 thread model" "${_recto_found_model}" "Thread model: posix"
        "A win32-threads GCC has a different std::thread and std::mutex implementation "
        "from the one Qt's binaries were built against.")
endif()

# --- TA-6 · C runtime is msvcrt, NOT UCRT -----------------------------------
# The single most valuable assertion in this file: an MSYS2 UCRT64 toolchain passes
# TA-1, TA-3 and TA-5, and can pass TA-4 -- this is the one that stops it.
#
# Note it requires __MSVCRT__ to be PRESENT as well as _UCRT to be ABSENT. Testing
# only for the absence of _UCRT would pass vacuously if the macro dump failed for any
# reason, and a check that passes when its own probe breaks is not a check.
#
# The probe is compiled WITH CMAKE_CXX_FLAGS, on purpose. An earlier version ran the
# bare compiler, and a test showed why that is not good enough: configuring with
# -DCMAKE_CXX_FLAGS=-D_UCRT passed cleanly and printed "msvcrt" while the whole build
# compiled as UCRT. The assertion has to interrogate the compilation the project
# actually performs -- flags can carry --sysroot, -target, or an -isystem pointing at
# UCRT headers, and any of those changes the answer.
set(_recto_empty "${CMAKE_BINARY_DIR}/CMakeFiles/recto-crt-probe.cpp")
file(WRITE "${_recto_empty}" "")
separate_arguments(_recto_cxx_flag_list NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")
execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" ${_recto_cxx_flag_list} -dM -E -x c++ "${_recto_empty}"
    OUTPUT_VARIABLE _recto_macros
    ERROR_QUIET
    RESULT_VARIABLE _recto_rc)
if(NOT _recto_rc EQUAL 0)
    message(FATAL_ERROR "TA-6: could not dump predefined macros from '${CMAKE_CXX_COMPILER}' (exit ${_recto_rc}).")
endif()
if(_recto_macros MATCHES "#define _UCRT")
    _recto_pin_error("TA-6 C runtime" "UCRT (_UCRT is defined)" "msvcrt"
        "This is a UCRT toolchain -- an MSYS2 ucrt64 install is the usual source. Qt "
        "6.11.1 mingw_64 links msvcrt.dll; mixing the two C runtimes in one process "
        "corrupts anything passed across the boundary as a FILE*, errno, or heap pointer.")
endif()
if(NOT _recto_macros MATCHES "#define __MSVCRT__")
    _recto_pin_error("TA-6 C runtime" "__MSVCRT__ is not defined" "msvcrt"
        "The compiler does not identify as an msvcrt-targeting mingw-w64 build.")
endif()

# --- TA-7 · The compiler actually used lives inside the pinned root ----------
# Guards a PATH-ordering accident: the pin says one thing, CMake found another.
get_filename_component(_recto_cxx_real  "${CMAKE_CXX_COMPILER}" REALPATH)
get_filename_component(_recto_root_real "${RECTO_MINGW_ROOT}"    REALPATH)
string(TOLOWER "${_recto_cxx_real}"  _recto_cxx_lc)
string(TOLOWER "${_recto_root_real}" _recto_root_lc)
string(FIND "${_recto_cxx_lc}" "${_recto_root_lc}" _recto_prefix_pos)
if(NOT _recto_prefix_pos EQUAL 0)
    _recto_pin_error("TA-7 compiler location" "${_recto_cxx_real}" "a path under ${_recto_root_real}"
        "CMake resolved a different compiler than the pin names. Do not 'fix' this by "
        "widening RECTO_MINGW_ROOT -- find out which install won and why.")
endif()

# --- TA-9 · No non-free GCC plugin ------------------------------------------
# The GCC Runtime Library Exception permits shipping proprietary binaries ONLY while
# the program is produced by an Eligible Compilation Process. Adding a non-free
# compiler plugin voids the exception and would retroactively relicense Recto's own
# source under GPL-3.0. One line of build config, unbounded downside -- so it is
# checked, and the result is written out for tools/license-audit to re-check
# independently (Tech-Stack-Plan.md 8.1).
set(_recto_plugins "")
foreach(_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
             CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL
             CMAKE_C_FLAGS)
    if(${_var} MATCHES "-fplugin")
        list(APPEND _recto_plugins "${_var}: ${${_var}}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=plugin
    OUTPUT_VARIABLE _recto_plugin_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
# When GCC cannot locate the directory it echoes the bare name back -- which is the
# case on this toolchain, and means there is no plugin directory at all.
if(IS_DIRECTORY "${_recto_plugin_dir}")
    file(GLOB _recto_plugin_files "${_recto_plugin_dir}/*.dll" "${_recto_plugin_dir}/*.so")
    foreach(_f IN LISTS _recto_plugin_files)
        list(APPEND _recto_plugins "installed: ${_f}")
    endforeach()
endif()

if(_recto_plugins)
    string(REPLACE ";" "\n  " _recto_plugin_report "${_recto_plugins}")
    message(FATAL_ERROR
        "\nTA-9: a GCC plugin is in play:\n  ${_recto_plugin_report}\n\n"
        "A non-free compiler plugin voids the GCC Runtime Library Exception, which is "
        "what permits shipping proprietary binaries linked against libstdc++. Losing it "
        "would relicense Recto's own source under GPL-3.0. See Tech-Stack-Plan.md 8.1.\n")
endif()

string(REPLACE ";" "\n" _recto_plugin_file_contents "${_recto_plugins}")
file(WRITE "${CMAKE_BINARY_DIR}/compiler-plugins.txt" "${_recto_plugin_file_contents}")

# --- TA-8 · Qt version and prefix -------------------------------------------
# A function, not inline: it can only run after find_package(Qt6), which the top-level
# CMakeLists does further down.
function(recto_assert_qt)
    if(NOT Qt6_VERSION VERSION_EQUAL "6.11.1")
        _recto_pin_error("TA-8 Qt version" "${Qt6_VERSION}" "6.11.1"
            "Recto pins Qt and its compiler together. Upgrading Qt means moving "
            "RECTO_MINGW_ROOT and the TA-2 expected version in the same change.")
    endif()

    get_filename_component(_qt_config_dir "${Qt6_DIR}" REALPATH)
    get_filename_component(_qt_expected   "${RECTO_QT_PREFIX}" REALPATH)
    string(TOLOWER "${_qt_config_dir}" _qt_config_lc)
    string(TOLOWER "${_qt_expected}"   _qt_expected_lc)
    string(FIND "${_qt_config_lc}" "${_qt_expected_lc}" _qt_pos)
    if(NOT _qt_pos EQUAL 0)
        _recto_pin_error("TA-8 Qt prefix" "${_qt_config_dir}" "a path under ${_qt_expected}"
            "A second Qt install won the search. Set RECTO_QT_PREFIX, or remove the "
            "stray Qt from CMAKE_PREFIX_PATH.")
    endif()
endfunction()

message(STATUS "Toolchain pin verified: GCC ${CMAKE_CXX_COMPILER_VERSION} ${_recto_triple}, SEH/posix/msvcrt, no GCC plugin")
