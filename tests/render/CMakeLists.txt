# tests/render -- the suite that links the ENGINE. Stage 2 T2, plan §5.1 and §5.6.
#
# WHY IT IS A SEPARATE TARGET, which is the same argument for the third time and the reason it is
# worth restating rather than cross-referencing. tests/unit links rawpage_core and rawpage_cos and
# nothing else, and rawpage_assert_headless() enforces that against the resolved closure -- the
# standing proof that core/ is testable with no display and no engine, which portable-asan verifies
# on a machine that has neither. The RASTER suite is precisely the pressure that would otherwise
# "just add rawpage_pdfium" to that target and destroy the property while every test still passed.
# tests/ui met the same pressure from the widget side at Stage 1 T9 and tests/platform from the OS
# side at T7; D2-2 named this one in advance, which is why it arrives as a target rather than as a
# link line.
#
# WINDOWS ONLY, because it rasterizes and PDFium is a Windows DLL here. It needs no display: nothing
# in this target is a widget, and case A-07's claim -- that initializing the engine does not require
# one -- is a thing this suite establishes rather than assumes, by running in a process that has no
# QGuiApplication at all.
#
# The `render.` prefix is new, and CI's T15-era assertion that a registered prefix is never EMPTY
# extends to it: a target that fails to build subtracts cases from a run still reporting
# "100% tests passed".

find_package(Catch2 3 REQUIRED)

add_executable(rawpage_render_tests
    tst_pdfiuminit.cpp            # Stage 2 T2 -- guide cases A-07's engine half, and A-45's first
    tst_pdfiumdoc.cpp             # Stage 2 T9 -- guide cases A-42 and A-43
    tst_rasterizer.cpp            # Stage 2 T9 -- guide cases A-44, A-45, A-46's second half,
                                  #               and A-47's engine half
    tst_renderpipeline.cpp        # Stage 2 T13 -- the REAL engine on the REAL scheduler, through
                                  #   app/BackendRasterizer. Every other suite fakes one end of
                                  #   this: tests/unit and tests/ui fake the rasterizer (they may
                                  #   not link an engine), and the other cases here call the
                                  #   rasterizer with no scheduler. The assembly the product
                                  #   actually runs had no case until this file.
)

# The corpus, by absolute source path -- the same mechanism tests/unit uses and for the same
# reason: a test that guessed a relative path would pass or fail on whatever working directory
# CTest happened to use.
target_compile_definitions(rawpage_render_tests PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus")

target_include_directories(rawpage_render_tests PRIVATE "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_render_tests PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_pdfium
    Catch2::Catch2WithMain
)

rawpage_common_flags(rawpage_render_tests)

include(Catch)
catch_discover_tests(rawpage_render_tests
    TEST_PREFIX "render."
    # Two directories, and the second is the new one: libstdc++/libgcc as everywhere else, plus the
    # output directory, because pdfium.dll is deployed there rather than being on any system path.
    # Without it every case in this suite fails to LAUNCH with 0xC0000135, which looks nothing like
    # a test failure.
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
