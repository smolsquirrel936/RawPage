add_executable(layer-lint
    main.cpp
    LayerLint.cpp
    IncludeGraph.cpp
    RuleSet.cpp
)

target_include_directories(layer-lint PRIVATE "${CMAKE_SOURCE_DIR}")

# No Qt, no vcpkg, nothing but the standard library -- decision D0-1. This is what lets
# `checks` build and run in seconds on a bare runner, which is what makes "CI runs checks
# BEFORE compiling" real. If a link line ever appears here, that guarantee is gone.
recto_strict(layer-lint)
