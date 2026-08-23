# tools/ -- the enforcement gates.
#
# These build even for the portable subset: they depend on nothing but the C++ standard
# library (D0-1), and the CI `gates` job runs them on a bare Linux runner with no Qt and
# no vcpkg installed at all.

add_subdirectory(layer-lint)
add_subdirectory(license-audit)
add_subdirectory(i18n-lint)      # Stage 1 T6 -- the third gate

# The one target here that is not a gate and does link Qt: the A3 go/no-go spike (Stage 1 T2).
# Its own CMakeLists returns early for the gate configurations, so the "tools/ builds with no
# Qt" property above still holds. Deleted with the rest of the spike at T10.
add_subdirectory(spike-ribbon)

# tools/mutation-lint  -> Stage 3 (needs Command)
# tools/golden-update  -> Stage 2 (needs a rasterizer)
