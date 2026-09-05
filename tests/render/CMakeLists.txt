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
# THE `render.` PREFIX IS NOT YET ASSERTED BY CI, and this comment claimed the opposite until it was
# checked at T16. `.github/workflows/ci.yml`'s non-empty-prefix loop names `unit`, `ui`, `platform`
# and `bench`; `render` was never added when this target arrived at T2, so a target that failed to
# build would subtract 40 cases from a run still reporting "100% tests passed" -- which is the exact
# failure that loop exists to catch, in the one suite that links the engine.
#
# It is a one-word fix and it belongs to **T18**, which owns this stage's CI work (see
# `Stage2-Rendering-Plan.md` 10, where the same gap is recorded against the summary job's prefix
# list). Written here as well as there because this is the file whose absence would be missed.

find_package(Catch2 3 REQUIRED)

add_executable(rawpage_render_tests
    tst_pdfiuminit.cpp            # Stage 2 T2 -- guide cases A-07's engine half, and A-45's first
    tst_pdfiumdoc.cpp             # Stage 2 T9 -- guide cases A-42 and A-43
    tst_rasterizer.cpp            # Stage 2 T9 -- guide cases A-44, A-45, A-46's second half,
                                  #               and A-47's engine half
    tst_fontbridge.cpp            # Stage 2 T16 -- guide cases A-66 and A-67. The ONLY suite that
                                  #   drives PDFium -> FontResolverBridge -> WinFontResolver ->
                                  #   core/render/FontSubstitution end to end, for the reason
                                  #   below about rawpage_platform.
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

# rawpage_platform JOINED AT T16, and it is worth saying why rather than letting a link line grow.
#
# The font chain has three links in three libraries -- PDFium, pdf/render/FontResolverBridge and
# platform/win/WinFontResolver -- and no existing target could hold more than one of them:
# tests/unit may link neither the engine nor the OS layer (rawpage_assert_headless(), D2-2),
# tests/platform may not link the engine, and this target had no OS layer. So the arrangement the
# product actually runs was, once again, the one arrangement no suite could reach. That is issue
# I-09 exactly -- the finding that made app/BackendRasterizer a header at T13 -- and the answer is
# the same one: give the suite the real assembly rather than a copy of it.
#
# It does NOT weaken what this target's separateness protects. The property being protected is that
# tests/unit links no engine and no display; adding the OS layer to a Windows-only, engine-linking
# target costs nothing of that. Both of these libraries are already in the shipped executable.
target_link_libraries(rawpage_render_tests PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_pdfium
    rawpage_platform
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
