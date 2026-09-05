# tools/ -- the enforcement gates.
#
# These build even for the portable subset: they depend on nothing but the C++ standard
# library (D0-1), and the CI `gates` job runs them on a bare Linux runner with no Qt and
# no vcpkg installed at all.

add_subdirectory(layer-lint)
add_subdirectory(license-audit)
add_subdirectory(i18n-lint)      # Stage 1 T6 -- the third gate

# The one target here that is not a gate and does link Qt: res/ribbon.json's build-time
# validation (D1-8, Stage 1 T10). Its own CMakeLists returns early for the gate configurations,
# so the "tools/ builds with no Qt" property above still holds.
#
# It is NOT a fourth gate. `checks` runs three and says three; this is a build step that ui/
# depends on, so a bad resource fails the build rather than the gate run. See its main.cpp.
add_subdirectory(ribbon-validate)

# The second non-gate target here, and it arrives on ribbon-validate's precedent (Stage 2 T17):
# golden/'s reference rasters are PRODUCED by a tool and ENFORCED by a CTest suite, so the producer
# links Qt, PDFium and the platform layer and its own CMakeLists returns early for the gate and
# portable configurations. `checks` still runs three gates and says three.
add_subdirectory(golden-update)

# tools/spike-ribbon -- the A3 go/no-go spike (Stage 1 T2) -- was DELETED at T10, exactly as D1-1
# said it would be. Its durable outputs were the verdict (`go, with named custom work`, recorded in
# Tech-Stack-Plan.md 3.3), the three named pieces of custom work, and issue I-05 -- all of which now
# live in ui/shell/Ribbon.{h,cpp} as production code with a test suite behind them.
#
# tools/spike-pdfium -- the A6 spike (Stage 0 T12) -- was DELETED at Stage 2 T2 on the same terms.
# It answered one question, "does a prebuilt PDFium DLL link from g++ and return sane answers", and
# its durable outputs were the A6 verdict, scripts/fetch-pdfium.ps1, and the two findings in
# third_party/pdfium/README.md about the static CRT and the packager's licence. pdf/render/PdfiumInit
# is the production form, behind the facade and with a suite behind it. Neither spike was ever wired
# into this file: both were built by hand, which is what a throwaway is.

# tools/mutation-lint  -> Stage 3 (needs Command)
# tools/golden-update  -> shipped at Stage 2 T17; the line that stood here said "needs a
#                         rasterizer", and T9 was where it got one.
