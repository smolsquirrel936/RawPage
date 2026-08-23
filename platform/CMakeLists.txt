# platform/ -- the OS abstraction.
#
# platform/platform.h declares the interfaces; platform/win/ is the only OS-specific
# code in the project and makes NO policy decisions. An interface exists here only
# where Qt does not already do the job cross-platform (Tech-Stack-Plan.md 4.3) -- the
# abstraction is derived from a real gap list, not from speculation.
#
# This isolation is what lets the MSVC-shaped corners of the Windows surface (the
# print driver, Office add-ins, shell extensions -- risk R8) become separately built
# satellite binaries talking across a C/COM boundary, rather than forcing a toolchain
# change on the whole project.

# PlatformModule.cpp -- the Stage 0 placeholder TU -- was DELETED at T7, which is what its own
# header comment instructed: "replaced by real content at Stage 1. Delete this file then -- a
# placeholder that outlives its purpose becomes a mystery." Nothing called its anchor any more
# either; T1's rewrite of main.cpp removed the last call.
add_library(rawpage_platform STATIC
    # The boundary itself (Stage 1 T7). Interfaces only -- and tools/layer-lint declares this ONE
    # FILE as its own layer with an OS-header deny list, because it is included by core/, ui/ and
    # app/, and a windows.h reaching it would give all three a platform-SDK dependency that only
    # fails on the Linux portable-asan job.
    platform.h

    # win/ -- the only OS-specific code in the project. Stage 1 implements two of the seventeen
    # interfaces; the rest are declared and null. See platform.h on why null beats a stub.
    win/PlatformFactory.h
    win/PlatformFactory.cpp
    win/WinSingleInstanceBus.h
    win/WinSingleInstanceBus.cpp
    win/WinShellIntegration.h
    win/WinShellIntegration.cpp
)

target_include_directories(rawpage_platform PUBLIC "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_platform PUBLIC rawpage_core)

# Named explicitly rather than relying on the MinGW default spec, for the same reason app/ names
# its own: the default is a property of the driver, not of this project. shell32 for
# ShellExecuteW/SHChangeNotify, advapi32 for the registry, user32 for AllowSetForegroundWindow.
target_link_libraries(rawpage_platform PRIVATE shell32 advapi32 user32)

rawpage_common_flags(rawpage_platform)
