# tools/ -- the enforcement gates.
#
# These build even for the portable subset: they depend on nothing but the C++ standard
# library (D0-1), and the CI `gates` job runs them on a bare Linux runner with no Qt and
# no vcpkg installed at all.

add_subdirectory(layer-lint)
add_subdirectory(license-audit)

# tools/i18n-lint      -> Stage 1 (needs tr() sites)
# tools/mutation-lint  -> Stage 3 (needs Command)
# tools/golden-update  -> Stage 2 (needs a rasterizer)
