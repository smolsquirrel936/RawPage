# RawPageWarnings.cmake -- the one place compiler and linker flags are decided.
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
function(_rawpage_assert_no_static_runtime)
    foreach(var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL
        CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
        if(${var} MATCHES "(^| )-static(-libstdc\\+\\+|-libgcc)?( |$)")
            message(FATAL_ERROR
                "${var} contains a -static* flag: '${${var}}'\n"
                "RawPage ships Qt and the GCC runtime as replaceable DLLs beside the "
                "executable. Static linking breaches the LGPL obligation in "
                "Tech-Stack-Plan.md 8.1 and is an ABI hazard against the dynamically "
                "linked Qt. Remove the flag.")
        endif()
    endforeach()
endfunction()

_rawpage_assert_no_static_runtime()

# --- The common flag set -----------------------------------------------------
# Applied to every RawPage target. Warnings are NOT errors here -- rawpage_strict() and
# rawpage_ui_grace() decide that per layer.
function(rawpage_common_flags target)
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

        # A build id on EVERY linked target, in every configuration -- decision D1-13 in
        # Stage1-AppShell-Plan.md 3. This toolchain emits DWARF and never a .pdb, so a field
        # minidump is symbolized against an archived unstripped binary rather than by a symbol
        # server, and the build id is what matches the two.
        #
        # --build-id is an ELF spelling and this is PE. GNU ld honours it anyway, by emitting a
        # CodeView RSDS record -- a 16-byte GUID plus an age -- into a `.buildid` section.
        # Measured on the pinned toolchain 2026-08-23: there is NO .note.gnu.build-id in the
        # output, and MiniDumpWriteDump copies that same RSDS record into the dump's module
        # list, so a dump identifies its own binary with no side table. scripts/symbolize.ps1
        # reads both ends.
        #
        # Inert on the static layer libraries, which are archived rather than linked. It is set
        # here rather than only on the executable so that the property holds for every future
        # linked target -- the satellite DLLs of risk R8 included -- without anyone remembering.
        -Wl,--build-id
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
function(rawpage_strict target)
    rawpage_common_flags(${target})
    target_compile_options(${target} PRIVATE -Werror)
endfunction()

# --- ui/ : -Werror with a grace list that may only shrink --------------------
# The list below is the entire negotiated exception surface for ui/. Every entry is
# a debt with a reason. Adding one is a decision that shows up in a diff; removing
# one is the goal. It must never grow.
set(RAWPAGE_UI_WARNING_GRACE
    # (empty -- ui/ has no code yet. Stage 1 adds entries here only with a reason.)
    CACHE STRING "Warnings tolerated in ui/. This list may only shrink.")
mark_as_advanced(RAWPAGE_UI_WARNING_GRACE)

function(rawpage_ui_grace target)
    rawpage_common_flags(${target})
    target_compile_options(${target} PRIVATE -Werror)
    foreach(w IN LISTS RAWPAGE_UI_WARNING_GRACE)
        target_compile_options(${target} PRIVATE ${w})
    endforeach()
endfunction()

# --- The application executable ---------------------------------------------
# -mwindows sets the Windows GUI subsystem, so no console window appears.
#
# -municode (the translation of /ENTRY:wWinMainCRTStartup) is deliberately NOT set,
# and that is now a MEASURED result rather than a deferral -- decision D1-3 in
# Stage1-AppShell-Plan.md 3, resolving Stage 0 deviation (a).
#
# Stage 0 recorded only that -mwindows -municode against a plain main() fails to link
# (undefined reference to `wWinMain'). Stage 1 asked the question that actually matters:
# does anything we consume lose a non-ASCII argument without it? Qt on Windows sources
# QCoreApplication::arguments() from GetCommandLineW(), not from argv, and
# `rawpage.exe --print-args` prints both lists side by side so the claim is checked
# rather than believed. See app/main.cpp and testing guide case A-05 for the figures.
#
# The fallback, had the measurement gone the other way, was Qt6::EntryPointPrivate --
# NOT a hand-rolled wWinMain stub written to satisfy a flag.
function(rawpage_app_flags target)
    rawpage_common_flags(${target})
    target_link_options(${target} PRIVATE -mwindows)

    # The flag's state, exported to the code that reports it (rawpage.exe --print-args).
    # Set HERE, beside the link option it describes, so the diagnostic cannot claim one thing
    # while the link does another. The first draft of that diagnostic probed _UNICODE instead
    # and read "yes" with -municode nowhere in the build -- Qt6::Core defines UNICODE and
    # _UNICODE in its INTERFACE compile definitions, so the probe was measuring Qt.
    target_compile_definitions(${target} PRIVATE RAWPAGE_MUNICODE=0)
endfunction()
