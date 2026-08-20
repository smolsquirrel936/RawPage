# tests/unit -- the headless suite.
#
# THE STANDING PROOF: this target links rawpage_core and rawpage_pdf and NOTHING ELSE. If the
# full domain suite cannot pass without a display and without a PDF engine, the layer
# boundary has already leaked -- and it will have leaked quietly, months before anyone
# notices. Tech-Stack-Plan.md 9 names this as a verification step in its own right.
#
# It is asserted below rather than left as a convention, because a convention survives
# exactly until someone needs one widget class in a test.

find_package(Catch2 3 REQUIRED)

add_executable(rawpage_tests
    tst_result.cpp
    tst_units.cpp
    tst_geometry.cpp
    tst_log.cpp
)

target_include_directories(rawpage_tests PRIVATE "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_tests PRIVATE
    rawpage_core
    rawpage_pdf
    Catch2::Catch2WithMain
)

rawpage_common_flags(rawpage_tests)

# --- The boundary assertion ---------------------------------------------------------
# Checks the RESOLVED link closure, not just what this file writes: a transitive edge
# (rawpage_pdf gaining a dependency on rawpage_platform, say) is exactly the way this leaks
# without anybody editing this file.
function(rawpage_assert_headless target)
    set(forbidden rawpage_ui rawpage_platform)
    set(pending ${target})
    set(seen "")

    while(pending)
        list(POP_FRONT pending current)
        if(current IN_LIST seen)
            continue()
        endif()
        list(APPEND seen ${current})

        if(NOT TARGET ${current})
            continue()
        endif()

        foreach(bad IN LISTS forbidden)
            if(current STREQUAL bad)
                message(FATAL_ERROR
                    "\n${target} links ${bad}, directly or transitively.\n\n"
                    "The unit suite must build and pass with NO DISPLAY and NO PDF ENGINE. "
                    "That is not a preference: core/ is required to be testable in isolation "
                    "(Tech-Stack-Plan.md 4.2, 9), and the portable-asan preset compiles the "
                    "same sources on Linux where neither library exists at all.\n\n"
                    "If a test needs a widget or an OS handle, the thing it is testing "
                    "belongs behind an interface, not in this suite.\n")
            endif()
        endforeach()

        get_target_property(deps ${current} LINK_LIBRARIES)
        if(deps)
            foreach(dep IN LISTS deps)
                if(TARGET ${dep})
                    list(APPEND pending ${dep})
                endif()
            endforeach()
        endif()
    endwhile()
endfunction()

rawpage_assert_headless(rawpage_tests)

# catch_discover_tests registers each TEST_CASE with CTest by name, so `ctest -R tst_journal`
# selects a suite exactly as CLAUDE.md and Tech-Stack-Plan.md 9 document it.
include(Catch)
catch_discover_tests(rawpage_tests
    TEST_PREFIX "unit."
    # The built binaries are dynamically linked against libstdc++ and libgcc, so the
    # toolchain bin directory has to be reachable or every test fails to LAUNCH with
    # 0xC0000135 -- a failure that looks nothing like a test failure.
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin"
)
