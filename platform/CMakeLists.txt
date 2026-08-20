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

add_library(rawpage_platform STATIC
    PlatformModule.cpp
)

target_include_directories(rawpage_platform PUBLIC "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_platform PUBLIC rawpage_core)

rawpage_common_flags(rawpage_platform)
