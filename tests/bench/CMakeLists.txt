# tests/bench -- the measurements that gate a decision rather than pass or fail.
#
# Architecture-FileMap.md 3.17 describes this directory as Stage 2's per-tile render latency, the
# figure that settles risk A2. It arrives at Stage 1 T15 instead, one benchmark early, because A3's
# criterion R-9 is also a measurement (Stage1-AppShell-Plan.md 5.3 names `tests/bench` as how it is
# judged) and Stage 1 cannot record its A3 verdict without it. Stage 2 adds bench_render here beside
# it; the directory does not need to be invented twice.
#
# WHAT MAKES A BENCH TARGET DIFFERENT FROM A TEST TARGET. A test asserts and fails. A benchmark
# reports, and its exit code is about whether the measurement was POSSIBLE -- so a CI runner under
# load cannot turn a green build red for a reason that has nothing to do with the change. The
# threshold verdict is printed and recorded in the testing guide by a person.
#
# WINDOWS ONLY, and for the same reason tests/ui is (D1-4): it links rawpage_ui. It never runs on
# portable-asan -- the portable subset is core/ + pdf/cos/ by rule, and a sanitized build's timings
# would be a measurement of ASan anyway.

add_executable(rawpage_bench_ribbon
    bench_ribbon.cpp
)

target_include_directories(rawpage_bench_ribbon PRIVATE "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_bench_ribbon PRIVATE
    rawpage_core
    rawpage_ui
)

set_target_properties(rawpage_bench_ribbon PROPERTIES AUTOMOC ON)

# The same two data dependencies tests/ui declares, and for the same reasons: the shipped ribbon
# resource, because R-9 is a claim about the product's 9-tab ribbon rather than about a fixture, and
# the en catalog, because untranslated tab titles resolve to their own keys and lay out at different
# widths.
add_dependencies(rawpage_bench_ribbon rawpage_translations rawpage_resources)

rawpage_common_flags(rawpage_bench_ribbon)

# Registered with CTest so it is built and run on every Windows CI run. A benchmark nobody runs is a
# benchmark whose number is stale by the time it matters -- and worse, one that has usually stopped
# compiling. Not registered through catch_discover_tests: this is not a Catch2 binary and has no
# cases to discover.
add_test(NAME bench.ribbon.tab_switch COMMAND rawpage_bench_ribbon)
set_tests_properties(bench.ribbon.tab_switch PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    # Same reason tests/unit gives: the binary is dynamically linked against libstdc++ and libgcc,
    # and without the toolchain bin directory reachable it fails to LAUNCH with 0xC0000135 -- which
    # looks nothing like a benchmark failing.
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin"
)

# =======================================================================================
# rawpage_bench_render -- risk A2's probe. Stage 2 T3, decision D2-1 (first half).
# =======================================================================================
# The go/no-go for D3's render half, and the reason T3 comes before every line of tiling, scheduling
# or viewer code: a no-go reshapes T10 and T11 completely, and learning it after T13 is a rewrite.
#
# It links the ENGINE and no Qt GUI at all -- there is no QApplication in it, which is worth noticing
# rather than passing over: rasterizing a page needs no display, and a benchmark that created a
# window would be measuring one. rawpage_cos is linked because plan §5.6 names this target's closure
# as core + cos + pdfium; nothing in the probe calls into it yet, and T18's version -- which drives
# the shipped scheduler over the whole corpus -- will.
add_executable(rawpage_bench_render
    bench_render.cpp
)

target_include_directories(rawpage_bench_render PRIVATE "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_bench_render PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_pdfium
    # AND THE ENGINE DIRECTLY, which is a statement rather than a workaround. rawpage_pdfium links
    # PDFium PRIVATE on purpose: the facade owns the boundary, and a consumer of the facade has no
    # business seeing fpdfview.h -- if that dependency were PUBLIC, app/ would silently acquire the
    # PDFium headers through the composition root. This probe is the one program in the tree that is
    # deliberately RAW PDFium, below the facade rather than behind it, and it says so here.
    #
    # It is not a hole in the layer rule: tools/layer-lint's fpdf and fpdf-handle rules confine those
    # names to pdf/render/ within the LAYERS, and tests/ is not a layer. T18 rewrites this file to
    # drive the shipped scheduler, and this line goes with it.
    pdfium
)

# The default page, as a compile definition rather than a runtime search. A probe that scanned a
# directory would produce a figure whose meaning changed when the directory did -- and A2's question
# names ONE page shape (Tech-Stack-Plan.md §3.3). The same call tests/unit makes for Features.md.
target_compile_definitions(rawpage_bench_render PRIVATE
    RAWPAGE_A2_PROBE_PAGE="${CMAKE_SOURCE_DIR}/corpus/a2-text-page.pdf")

rawpage_common_flags(rawpage_bench_render)

# Registered with CTest for the same reason bench_ribbon is: a benchmark nobody runs is one that has
# stopped compiling by the time its number matters. Its exit code is about whether the MEASUREMENT
# WAS POSSIBLE -- the engine started, the file opened, the calls produced pixels -- and never about
# whether the machine was fast.
add_test(NAME bench.render.a2_probe COMMAND rawpage_bench_render)
set_tests_properties(bench.render.a2_probe PROPERTIES
    # No QT_QPA_PLATFORM here, deliberately: this binary creates no Qt GUI object, and setting a
    # platform plugin for a program that never asks for one would suggest it did.
    #
    # Two directories on PATH: libstdc++/libgcc as everywhere, and the output directory, because
    # pdfium.dll is deployed there rather than being on any system path.
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin;PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# =========================================================================================
# rawpage_bench_pipeline -- A2's PRODUCT figure, and O7's and D2-14's numbers. Stage 2 T18.
# =========================================================================================
#
# A SECOND BINARY RATHER THAN A REWRITE OF bench_render, and the comment above on that target said
# the opposite: *"T18 rewrites this file to drive the shipped scheduler, and this line goes with
# it."* Rewriting it would have destroyed two things worth more than the tidiness.
#
#   1. **The probe stops being reproducible.** D2-1 says BOTH figures stand in Tech-Stack-Plan.md
#      §3.3 -- the probe's answers *is this physically possible*, this one answers *is the product
#      fast*, and a document citing a number nobody can re-take is a document citing folklore.
#   2. **`--unsafe-parallel` is I-05's reproducer.** bench_render's own header records the rule that
#      a finding needing a source edit to reproduce is one that gets re-litigated. Deleting the
#      program that reproduces the heap corruption would make I-05 exactly that.
#
# So bench_render keeps its subject and its raw `pdfium` link line, and this target takes the other
# question.
#
# IT DOES NOT LINK RAW PDFIUM, WHICH IS THE STRUCTURAL HALF OF WHAT IT MEASURES. bench_render is
# deliberately below the facade; this one is behind it, so it cannot name an `FPDF_` symbol even by
# accident. A "measurement of the product's render path" that could reach past the boundary would be
# a measurement of something the product does not do.
add_executable(rawpage_bench_pipeline bench_pipeline.cpp)

target_include_directories(rawpage_bench_pipeline PRIVATE "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_bench_pipeline PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_pdfium
)

# QtCore only: this binary creates no GUI object, which is how "the render pipeline needs no
# display" stays a thing the build establishes rather than a thing a comment asserts.
rawpage_link_qt(rawpage_bench_pipeline Core)

target_compile_definitions(rawpage_bench_pipeline PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus")

rawpage_common_flags(rawpage_bench_pipeline)

# Registered with CTest on the same terms as the other two: it must not rot, and its exit code is
# about whether the MEASUREMENT WAS POSSIBLE -- the engine started, documents opened, tiles arrived
# -- never about whether the machine was fast.
add_test(NAME bench.render.a2_product COMMAND rawpage_bench_pipeline)
set_tests_properties(bench.render.a2_product PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin;PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# Stage 4 T2 (D4-16). The SAME binary in its `--stage4` mode, registered for the same reason the
# line above is: a measurement nothing runs is a measurement that rots, and this one carries a
# VERDICT -- A2 at Stage 4, recorded in Tech-Stack-Plan.md 3.3 -- which T14 re-reads through the
# shipped panes. Its exit code is still only about whether the measurement was possible.
add_test(NAME bench.render.a2_stage4 COMMAND rawpage_bench_pipeline --stage4)
set_tests_properties(bench.render.a2_stage4 PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin;PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# Stage 5 T3 (D5-22). `--stage5`: the viewport while text extraction shares the engine thread --
# an index build, a folder search over a scratch copy of the corpus, a comparison. It carries A2 AT
# STAGE 5 (Tech-Stack-Plan.md 3.3), which T14 re-reads through the shipped surfaces. About 40 s on
# the reference machine; the exit code is still only whether the measurement was possible.
add_test(NAME bench.render.a2_stage5 COMMAND rawpage_bench_pipeline --stage5)
set_tests_properties(bench.render.a2_stage5 PROPERTIES
    TIMEOUT 600
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin;PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# Stage 6 T4 (D6-24). `--stage6`: the viewport while annotation commands make the engine reload the
# composed revision (D6-2), and an edit's time to the pixels. The GRADED run is 30 s a file, by hand
# (Stage6-Annotations-Testing-Guide.md A-38); CTest runs it SHORT (`--seconds 4`) so the mode cannot rot
# without costing every suite run two minutes. Its exit code is only whether the measurement was possible.
add_test(NAME bench.render.a2_stage6 COMMAND rawpage_bench_pipeline --stage6 --seconds 4)
set_tests_properties(bench.render.a2_stage6 PROPERTIES
    TIMEOUT 600
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin;PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# rawpage_bench_openclose -- Stage 2 issue I-35's reproducer: private bytes and handles per round of
# ten documents opened, rendered and closed with no window. NOT registered with CTest: it reports a
# trend over dozens of rounds, and no threshold for that trend has been decided. It links the
# platform library only for `--bridge`, which installs the font hook the way app/CompositionRoot does.
add_executable(rawpage_bench_openclose bench_openclose.cpp HeapCensus.cpp AllocationTracker.cpp)
target_include_directories(rawpage_bench_openclose PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(rawpage_bench_openclose PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_pdfium
    rawpage_platform
    psapi
)
rawpage_link_qt(rawpage_bench_openclose Core)
target_compile_definitions(rawpage_bench_openclose PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus")
rawpage_common_flags(rawpage_bench_openclose)

# rawpage_bench_openclose_ui -- I-35 with the window in the loop: the product's own composition root
# and MainWindow, offscreen, opened and closed in rounds. The app/ sources are compiled in because
# app/ is an executable rather than a library.
add_executable(rawpage_bench_openclose_ui
    bench_openclose_ui.cpp
    AllocationTracker.cpp
    HeapCensus.cpp
    ${CMAKE_SOURCE_DIR}/app/CompositionRoot.cpp
    ${CMAKE_SOURCE_DIR}/app/LogFile.cpp
    ${CMAKE_SOURCE_DIR}/app/CrashHandler.cpp
    ${CMAKE_SOURCE_DIR}/app/SingleInstance.cpp
)
target_include_directories(rawpage_bench_openclose_ui PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(rawpage_bench_openclose_ui PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_pdfium
    rawpage_platform
    rawpage_ui
    psapi
)
set_target_properties(rawpage_bench_openclose_ui PROPERTIES AUTOMOC ON)
target_compile_definitions(rawpage_bench_openclose_ui PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus"
    RAWPAGE_VERSION="${PROJECT_VERSION}")
add_dependencies(rawpage_bench_openclose_ui rawpage_translations rawpage_resources)
rawpage_common_flags(rawpage_bench_openclose_ui)

# rawpage_bench_surface -- A2 at Stage 4, RE-READ THROUGH THE SHIPPED PANES (S4 T14, D4-16, guide
# A-72). It composes the product as bench_openclose_ui does -- CompositionRoot and MainWindow, the
# app/ sources compiled in because app/ is an executable -- and measures the VIEWPORT's p95 queued
# latency while the real ThumbnailsPane, SplitViewContainer and TilePresenter issue the requests.
#
# A SEPARATE TARGET FROM bench_pipeline, DELIBERATELY. bench_pipeline --stage4 is T2's figure: a
# synthetic request pattern issued before any pane existed, and it is quoted as such. A flag that
# could print either number under one name would make available again the exact confusion D2-1's
# two measurements exist to prevent.
#
# Stage 5 T14 (D5-22, guide A-112) added A2 AT STAGE 5's columns -- the find bar, the search pane and a
# comparison, each started through the shipped surface over text-900.pdf -- and `app/Application.cpp`,
# whose `loadTranslators` gives the widgets their English labels: with catalog KEYS for labels the find
# bar was 2,966 px wide at its minimum and the viewport it left was not A2's.
add_executable(rawpage_bench_surface
    bench_surface.cpp
    ${CMAKE_SOURCE_DIR}/app/Application.cpp
    ${CMAKE_SOURCE_DIR}/app/CompositionRoot.cpp
    ${CMAKE_SOURCE_DIR}/app/LogFile.cpp
    ${CMAKE_SOURCE_DIR}/app/CrashHandler.cpp
)
target_include_directories(rawpage_bench_surface PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(rawpage_bench_surface PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_pdfium
    rawpage_platform
    rawpage_ui
    shell32
)
set_target_properties(rawpage_bench_surface PROPERTIES AUTOMOC ON)
target_compile_definitions(rawpage_bench_surface PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus"
    RAWPAGE_VERSION="${PROJECT_VERSION}")
add_dependencies(rawpage_bench_surface rawpage_translations rawpage_resources)
rawpage_common_flags(rawpage_bench_surface)

# Stage 6 T15 (D6-24, guide A-104) added A2 AT STAGE 6's columns -- a split window, the shipped pencil drawing a
# stroke every 100 ms in one pane while the other scrolls -- and the ONE CTest registration this program has: the
# Stage 6 columns SHORT (`--seconds 4`), for `bench.render.a2_stage6`'s reason, so the pencil column cannot rot
# unnoticed. The graded run is 30 s, by hand. The exit code is only whether the measurement was possible: a
# pencil that made no command, an edit no tile showed, a pane that delivered nothing.
add_test(NAME bench.surface.a2_stage6 COMMAND rawpage_bench_surface --stage6 --seconds 4)
set_tests_properties(bench.surface.a2_stage6 PROPERTIES
    TIMEOUT 600
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin;PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# THE PRODUCT'S MANIFEST, EMBEDDED IN THE BENCHES THAT STAND IN FOR THE PRODUCT (Stage 2 issue I-35).
# app/rawpage.manifest opts rawpage.exe into the segment heap, which changes both how much memory a
# long session keeps and how fast a tile renders -- so a figure taken without it describes a different
# allocator. bench_render is deliberately left out: it is the RAW-PDFium probe, below the product.
target_sources(rawpage_bench_pipeline PRIVATE ${CMAKE_SOURCE_DIR}/app/rawpage.rc)
target_sources(rawpage_bench_openclose PRIVATE ${CMAKE_SOURCE_DIR}/app/rawpage.rc)
target_sources(rawpage_bench_openclose_ui PRIVATE ${CMAKE_SOURCE_DIR}/app/rawpage.rc)
target_sources(rawpage_bench_surface PRIVATE ${CMAKE_SOURCE_DIR}/app/rawpage.rc)
# The manifest is a dependency windres does not declare; a source-file property is directory-scoped,
# so app/'s own setting does not reach these targets. Without it an edited manifest ships stale here.
set_source_files_properties(${CMAKE_SOURCE_DIR}/app/rawpage.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/app/rawpage.manifest")

# rawpage_bench_openclose_ui_classicheap -- THE CONTROL: the same harness WITHOUT the manifest, so the
# classic heap's fragmentation stays reproducible without editing a build file. Through
# `--native --bus`, its msvcrt heap kept 60 -> 167 MB of committed-but-free memory over 30 rounds of
# ten documents, where the manifest build held private bytes flat.
get_target_property(_openclose_ui_sources rawpage_bench_openclose_ui SOURCES)
list(FILTER _openclose_ui_sources EXCLUDE REGEX "rawpage\\.rc$")
add_executable(rawpage_bench_openclose_ui_classicheap ${_openclose_ui_sources})
target_include_directories(rawpage_bench_openclose_ui_classicheap PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(rawpage_bench_openclose_ui_classicheap PRIVATE
    rawpage_core rawpage_cos rawpage_pdfium rawpage_platform rawpage_ui psapi)
set_target_properties(rawpage_bench_openclose_ui_classicheap PROPERTIES AUTOMOC ON)
target_compile_definitions(rawpage_bench_openclose_ui_classicheap PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus"
    RAWPAGE_VERSION="${PROJECT_VERSION}")
add_dependencies(rawpage_bench_openclose_ui_classicheap rawpage_translations rawpage_resources)
rawpage_common_flags(rawpage_bench_openclose_ui_classicheap)
# rawpage_bench_save -- Stage 3 T8, guide case A-61: what D3-4's whole-file save and D3-16's whole-file
# hash cost, through the shipped backend, SaveCoordinator and the real WinDurableFiles, on
# pages-900.pdf and on a generated 200 MB file. Registered with CTest like the A2 benches, so the figure
# is re-taken on every Windows run; its exit code is about whether the measurement was POSSIBLE.
add_executable(rawpage_bench_save bench_save.cpp)
target_include_directories(rawpage_bench_save PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(rawpage_bench_save PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_ops
    rawpage_pdfium
    rawpage_platform
)
rawpage_link_qt(rawpage_bench_save Core)
target_compile_definitions(rawpage_bench_save PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus")
target_sources(rawpage_bench_save PRIVATE ${CMAKE_SOURCE_DIR}/app/rawpage.rc)
rawpage_common_flags(rawpage_bench_save)

add_test(NAME bench.save.cost COMMAND rawpage_bench_save)
set_tests_properties(bench.save.cost PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin;PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
