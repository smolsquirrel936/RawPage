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
