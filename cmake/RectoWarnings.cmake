# RectoWarnings.cmake -- the one place compiler and linker flags are decided.
#
# Flags are applied through these functions and NEVER pasted into a target's own
# CMakeLists. A per-target flag list is how a codebase ends up with three different
# warning levels and a -Werror that only holds in half the tree.
#
# The intent-by-intent translation from the MSVC formulation this project previously
# carried is tabulated in Tech-Stack-Plan.md 6.3; this file is its implementation and
# must not diverge from it. If a flag needs to change, change it there first.

include_guard(GLOBAL)

# --- Guard: the runtimes are never linked statically -------------------------
# Tech-Stack-Plan.md 8.1 makes replaceability an LGPL obligation for Qt and the same
# for the GCC runtime under its Runtime Library Exception. It is also an ABI hazard:
# a statically linked libstdc++ beside a dynamically linked Qt built against the
# shared one is a crash waiting for a release build. Checked here rather than trusted.
function(_recto_assert_no_static_runtime)
    foreach(var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL
        CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
        if(${var} MATCHES "(^| )-static(-libstdc\\+\\+|-libgcc)?( |$)")
            message(FATAL_ERROR
                "${var} contains a -static* flag: '${${var}}'\n"
                "Recto ships Qt and the GCC runtime as replaceable DLLs beside the "
                "executable. Static linking breaches the LGPL obligation in "
                "Tech-Stack-Plan.md 8.1 and is an ABI hazard against the dynamically "
                "linked Qt. Remove the flag.")
        endif()
    endforeach()
endfunction()

_recto_assert_no_static_runtime()

# --- The common flag set -----------------------------------------------------
# Applied to every Recto target. Warnings are NOT errors here -- recto_strict() and
# recto_ui_grace() decide that per layer.
function(recto_common_flags target)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)          # -std=c++20, deliberately NOT gnu++20

    target_compile_options(${target} PRIVATE
        # Warning level (was /W4)
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor

        # Standards conformance (was /permissive-). Explicit so nobody re-enables
        # Microsoft extensions to make a Win32 header cooperate.
        -fno-ms-extensions

        # Source and execution charset (was /utf-8). Without these, a UTF-8 string
        # literal is reinterpreted in the active code page -- which on this machine
        # would silently mangle the zh-TW catalogs from Stage 1 onward.
        -finput-charset=UTF-8
        -fexec-charset=UTF-8

        # Dead-code stripping (was /OPT:REF); the linker half is applied below.
        -ffunction-sections
        -fdata-sections

        # Large object files (was /bigobj). Debug builds of template-heavy TUs
        # genuinely need this; Catch2 and the COS object model will hit it.
        $<$<CONFIG:Debug>:-Wa,-mbig-obj>

        # Debug information in EVERY configuration, release included (was /Zi).
        # This toolchain emits DWARF and never a .pdb, so a field minidump is symbolized
        # against an archived unstripped binary rather than by a symbol server
        # (Tech-Stack-Plan.md 6.3, risk R7). A release build with no DWARF makes that
        # story impossible, and the shipped artifact is stripped separately at packaging
        # time -- so the size cost is paid in the archive, not by the user.
        -g

        # Stack-smashing detection. Named in 6.3 as part of what stands in for the ASan
        # coverage MinGW GCC cannot give us on Windows-only code.
        -fstack-protector-strong
    )

    # Bounds and precondition checks inside libstdc++ itself -- the other half of the
    # sanitizer substitute. Debug only: it changes libstdc++ behaviour and costs time.
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)

    target_link_options(${target} PRIVATE
        $<$<CONFIG:Release,MinSizeRel>:-Wl,--gc-sections>
    )
endfunction()

# --- Strict layers: core/ and pdf/ ------------------------------------------
# Warnings are errors. Tech-Stack-Plan.md 6.3 notes the risk this carries -- a GCC
# upgrade can introduce new warnings where MSVC would not -- which is exactly what
# the toolchain pin in cmake/toolchain-mingw.cmake exists to prevent. The answer to
# a new warning is to fix the code, never to relax this.
#
# NOT APPLIED HERE: -fanalyzer.
#
# Tech-Stack-Plan.md 6.3's flag table lists it as the translation of MSVC's /analyze, on
# core/ and pdf/cos/. Measured on the pinned toolchain 2026-08-18, it is not usable on C++:
#
#     int main() { std::string s = "x"; return s.size(); }
#     warning: leak of 's.std::__cxx11::basic_string<char>::...::_M_p' [-Wanalyzer-malloc-leak]
#
# GCC's analyzer is built for C; on C++ it false-positives on the simplest possible use of
# the standard library. Combined with -Werror -- which is exactly where 6.3 puts it -- it
# would fail the build on any file that touches std::string. Enabling it and then papering
# over the noise with -Wno-analyzer-* would leave a flag that looks like static analysis and
# performs none.
#
# The same table row also names clang-tidy, which does work on C++ and is the real static
# analysis route. Recorded as a Stage 0 finding against the owning document.
function(recto_strict target)
    recto_common_flags(${target})
    target_compile_options(${target} PRIVATE -Werror)
endfunction()

# --- ui/ : -Werror with a grace list that may only shrink --------------------
# The list below is the entire negotiated exception surface for ui/. Every entry is
# a debt with a reason. Adding one is a decision that shows up in a diff; removing
# one is the goal. It must never grow.
set(RECTO_UI_WARNING_GRACE
    # (empty -- ui/ has no code yet. Stage 1 adds entries here only with a reason.)
    CACHE STRING "Warnings tolerated in ui/. This list may only shrink.")
mark_as_advanced(RECTO_UI_WARNING_GRACE)

function(recto_ui_grace target)
    recto_common_flags(${target})
    target_compile_options(${target} PRIVATE -Werror)
    foreach(w IN LISTS RECTO_UI_WARNING_GRACE)
        target_compile_options(${target} PRIVATE ${w})
    endforeach()
endfunction()

# --- The application executable ---------------------------------------------
# -mwindows sets the Windows GUI subsystem, so no console window appears.
#
# -municode (the translation of /ENTRY:wWinMainCRTStartup) is deliberately NOT set
# yet. Verified on the pinned toolchain 2026-08-17: with -mwindows -municode and a
# plain main(), the link fails with
#     undefined reference to `wWinMain'
# because -municode selects the wide entry point, which does not exist until there
# is a real GUI entry point. Stage 1 turns it on together with Qt's entry-point
# implementation. Turning it on now would mean inventing a wWinMain stub whose only
# purpose is to satisfy a flag -- which is how a codebase acquires code nobody can
# explain.
function(recto_app_flags target)
    recto_common_flags(${target})
    target_link_options(${target} PRIVATE -mwindows)
endfunction()
