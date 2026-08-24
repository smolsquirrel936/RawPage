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
