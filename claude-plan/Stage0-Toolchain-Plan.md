# Stage 0 — Toolchain, Skeleton, and the Four Gates · Execution Plan

> **What this document owns.** The **execution order** of Stage 0 and the **Stage-0-local design
> choices** that no other document decides. It owns no product facts. Every stack, license, layering,
> and verification fact is cross-referenced to its owner and is never restated as a new decision:
>
> | For… | Read |
> |---|---|
> | The Stage 0 objectives, components, testing summary, and exit criteria | [`Implementation-Plan.md` § Stage 0](./Implementation-Plan.md#stage-0--toolchain-skeleton-and-the-four-gates) |
> | The locked toolchain, flags, triplet, license allowlist, and verification commands | [`Tech-Stack-Plan.md` §6, §8, §9](./Tech-Stack-Plan.md#6-dependencies--build) |
> | What each file is for | [`Architecture-FileMap.md` §3.8, §3.17](./Architecture-FileMap.md#38-corebatch-coresettings-and-shared-headers) |
> | How Stage 0 is verified | [`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md) — written from this plan, **before** the code |
>
> **Status:** ⚪ Planned. No task below has started. This file is a plan, not a progress log — Stage 0's
> live status stays in `Implementation-Plan.md`'s Stage 0 status block.

---

## 1. What Stage 0 is actually for

Stage 0 ships **no product capability**. Its output is a repository in which it is *mechanically
impossible* to commit the classes of mistake that this architecture cannot absorb later:

1. **An outward include, an `FPDF_` leak, or a stray file write** — cheap to prevent on day 0, a
   month of untangling on day 300. → `layer-lint`.
2. **A GPL dependency, or a Qt module whose only open-source license is GPL-3.0** — Qt Charts is
   *installed in our prefix*, one `find_package` line away ([`Tech-Stack-Plan.md` §6.1](./Tech-Stack-Plan.md#61-qt-6111-modules)).
   → `license-audit`.
3. **A compiler that is not Qt's own bundled GCC 13.1.0/msvcrt** — a libstdc++ or CRT skew surfaces
   as an unexplained crash in Stage 6, not an error in Stage 0. → `toolchain-mingw.cmake`.
4. **Two unverified toolchain assumptions, A5 and A6**, each of which can cost a later stage a week.
   → the vcpkg resolve and the PDFium DLL spike.

Everything else in this plan exists to serve those four.

---

## 2. Environment ledger — measured on this machine, 2026-08-17

Recorded so the tasks below are concrete. Toolchain facts are **owned by**
[`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified) and are
referenced, not re-established. The rows below are the *new* measurements this plan adds.

| Fact | Value | Consequence for the tasks |
|---|---|---|
| `C:\Qt\6.11.1\mingw_64` | present, the only Qt version installed | `RECTO_QT_PREFIX` default |
| `C:\Qt\Tools\` | `mingw1310_64`, `CMake_64`, `Ninja`, `QtCreator`, `QtDesignStudio`, `sdktool` | the build environment is complete without any separate install |
| `cmake`, `ninja`, `g++` on `PATH` | **absent** | every documented command must either use absolute paths or a `scripts/env.ps1` that prepends `C:\Qt\Tools\*\bin`. **T1 owes that script** — "it works on my machine because my PATH is unusual" is precisely the clean-machine failure Stage 0 exists to catch |
| `vcpkg` | **installed and verified 2026-08-17** at `C:\Users\squirrel\vcpkg` — tool `2026-07-27-98d7cb0…`, baseline `94a5411977…` (2026-08-15), community triplet `x64-mingw-dynamic` present. `VCPKG_ROOT` set at user scope; empty at machine scope; **stale in the running session's process scope** | **T5 unblocked.** The process-scope gap is V-5/V-6's residue, removed permanently once `scripts/env.ps1` (T1) sets the variable itself — see the **§7.1 checklist** |
| `git` | `C:\Program Files\Git\cmd\git.exe` | available |
| `gh` (GitHub CLI) | `C:\Program Files\GitHub CLI\gh.exe` | the CI route in §9 is workable |
| The project directory | **not a git repository**; contains only `CLAUDE.md` and `claude-plan/` | T1 initializes it |
| Python | `C:\ProgramData\miniconda3\python.exe` | *not* used by the build — see D0-2 |

---

## 3. Stage-0-local decisions

Choices this plan makes because nothing upstream decides them. Each is scoped to Stage 0 and is
listed here so a later stage can find and revisit it rather than rediscovering it.

| # | Decision | Why | Revisit |
|---|---|---|---|
| **D0-1** | **`layer-lint` and `license-audit` link nothing but the C++ standard library — no Qt, no vcpkg dependency.** | This is what lets `checks` build and run in seconds on a bare Linux runner with no Qt installed, which is what makes "CI runs `checks` *before* compiling" ([`Tech-Stack-Plan.md` §9](./Tech-Stack-Plan.md#9-verification)) real rather than aspirational. A gate that needs the whole build environment to run is a gate that runs last. | Only if a gate genuinely needs Qt — none does |
| **D0-2** | **The gates are C++ built by CMake, not Python scripts.** | The signatures are already specified (`int run(const IncludeGraph&, const RuleSet&)`, `Report audit(const VcpkgManifest&, const QtModuleList&)`) in [`Implementation-Plan.md` § Stage 0](./Implementation-Plan.md#stage-0--toolchain-skeleton-and-the-four-gates); and a gate written in the project's own language is a gate the project's own developers will maintain. Python exists on this machine but is not a build dependency. | Never, without changing those signatures |
| **D0-3** | **`vcpkg.json` declares *every* §6.2 dependency, but later-stage ones sit behind vcpkg *features*** (`ocr`, `office`, `media`), off by default. | A5 asks whether the whole §6.2 list resolves on `x64-mingw-dynamic`. Putting Tesseract in the default set would block Stage 0's build on a Stage 16 dependency; leaving it out entirely would defer the A5 finding to Stage 16, which is exactly the "Stage 6 surprise" A5 exists to prevent. Features let the resolve be *tested* now and *required* later. | Per stage, as each feature is switched on |
| **D0-4** | **The bad-fixture tree lives at `tests/gates/fixtures/` and is excluded from the real gate run by an explicit deny-path in the rule set, not by a filename convention.** | The fixtures are, by construction, violations. If the production `layer-lint` run scans them, `checks` fails on the real tree forever; if they are excluded by a fragile convention (`*_bad.cpp`), the first fixture that forgets the suffix silently stops being tested. An explicit path in `RuleSet` is greppable and appears in `--explain` output. | Never |
| **D0-5** | **The Qt module list that `license-audit` inspects is generated at configure time from the actual link closure**, via a `recto_link_qt()` wrapper that records modules into a global property. | The requirement is "**a build that links `Qt6::Charts`** fails". A hand-maintained list cannot detect that; only the real link closure can. The `objdump -p` half of the check is the release gate's ([`Tech-Stack-Plan.md` §8.3](./Tech-Stack-Plan.md#83-release-gate-must-all-be-ticked)) and is Stage 15's, not Stage 0's. | Stage 15 adds the post-link half |
| **D0-6** | **PDFium arrives as the pinned prebuilt `bblanchon/pdfium-binaries` Windows-x64 DLL release**, hash recorded, import library generated locally with `gendef` + `dlltool`. | Per your answer. It is the fastest route to an A6 verdict, and A6 is only a *linkability* question — [`Tech-Stack-Plan.md` §3.3 A6](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified) already names build-from-source as the fallback if a DLL cannot be obtained. | Stage 2, when the facade replaces the spike; source-build fallback if the A6 spike fails |
| **D0-7** | **CI is GitHub Actions**, three jobs: `gates` (ubuntu, no Qt), `windows` (Qt + mingw + vcpkg), `portable-asan` (ubuntu). | Per your answer, and it is the only route the installed tooling (`gh`) already supports. | O6/arm64 at Stage 15 |
| **D0-8** | **Stage 0's `recto.exe` is an empty `int main()` that returns 0**, linking all four layer libraries so the linker exercises the target graph. | The exit criterion is "an empty `recto.exe` builds", and an executable that links all four libraries proves the target graph is wired; one that links none proves nothing. | Stage 1 replaces `main()` |

**Not decided here, deliberately:** **O10** (which sanitizer route we settle on) stays open. Stage 0
wires `portable-asan` and `portable-fuzz` per the plan of record and records what the Linux job costs
and covers; the *choice* is settled on Stage 2's corpus evidence, per
[`Implementation-Plan.md` §8](./Implementation-Plan.md#8-open-decisions-carried-forward).

---

## 4. File manifest

Every file Stage 0 creates, with the [`Architecture-FileMap.md`](./Architecture-FileMap.md) row it
must flip **○ → ✅ in the same change**.

| File | FileMap row | Task |
|---|---|---|
| `CMakeLists.txt` | ⚠️ **no row — see §10** | T2 |
| `CMakePresets.json` | ⚠️ **no row** | T4 |
| `vcpkg.json`, `vcpkg-configuration.json` | ⚠️ **no row** | T6 |
| `cmake/toolchain-mingw.cmake` | §3.17 ✅ has a row | T3 |
| `cmake/ToolchainAssert.cmake` | ⚠️ **no row** | T3 |
| `cmake/RectoWarnings.cmake`, `cmake/RectoQt.cmake`, `cmake/RectoChecks.cmake` | ⚠️ **no row** | T2, T7, T9 |
| `app/main.cpp` | §3.1, tagged **(S1)** — Stage 0 creates the ⚪ placeholder only; the row flips at Stage 1 | T2 |
| `core/Result.h` | §3.8 (S0) | T10 |
| `core/Log.h` | §3.8 (S0) | T10 |
| `core/Geometry.h`, `core/Units.h` | §3.8 (S0) | T10 |
| `tools/layer-lint/` | §3.17 (S0) | T7 |
| `tools/license-audit/` | §3.17 (S0) | T8 |
| `tools/spike-pdfium/` | ⚠️ **no row — throwaway**, see T12 | T12 |
| `tests/unit/` | §3.17 (S0) | T11 |
| `tests/gates/` | ⚠️ **no row** | T9 |
| `.github/workflows/ci.yml` | ⚠️ **no row** | T13 |
| `scripts/env.ps1`, `scripts/bootstrap.ps1` | ⚠️ **no row** | T1 |
| `THIRD-PARTY.md` (generated) | ⚠️ **no row** | T8 (output) |

---

## 5. Component design

Enough detail that implementation is transcription, not invention. Nothing here contradicts an
owning document; where a fact comes from one, it is cited.

### 5.1 `cmake/toolchain-mingw.cmake` + `cmake/ToolchainAssert.cmake`

Split into two files for a reason that is easy to get wrong: **CMake re-reads the toolchain file for
every `try_compile`.** Compiler probes, `execute_process` calls, and `message(FATAL_ERROR)` placed in
the toolchain file therefore run many times and can fail a probe for the wrong reason. So:

- **`toolchain-mingw.cmake`** (read pre-`project()`, repeatedly) does only the cheap, idempotent work:
  ```
  set(RECTO_MINGW_ROOT "C:/Qt/Tools/mingw1310_64" CACHE PATH "...")   # cache variable, not a literal
  set(RECTO_QT_PREFIX  "C:/Qt/6.11.1/mingw_64"    CACHE PATH "...")
  CMAKE_C_COMPILER / CMAKE_CXX_COMPILER / CMAKE_RC_COMPILER / CMAKE_SYSTEM_NAME
  CMAKE_PREFIX_PATH += RECTO_QT_PREFIX
  ```
- **`ToolchainAssert.cmake`** (included **once**, immediately after `project()`) holds every assertion.
  Each failure message must name the offending value **and the fix**, because the person hitting it is
  by definition someone whose environment differs from the pin.

| # | Assertion | How | Fact owner |
|---|---|---|---|
| TA-1 | `CMAKE_CXX_COMPILER_ID` is `GNU` | CMake | [§6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management) |
| TA-2 | `CMAKE_CXX_COMPILER_VERSION` is exactly `13.1.0` | CMake | §3.3 A7 |
| TA-3 | `g++ -dumpmachine` is `x86_64-w64-mingw32` | `execute_process` | §3.3 A7 |
| TA-4 | Exception model is **SEH** | `libgcc_s_seh-1.dll` resolves in `${RECTO_MINGW_ROOT}/bin`; `libgcc_s_dw2-1.dll` does not | §3.3 A7 |
| TA-5 | Thread model is **posix** | `g++ -v` banner contains `Thread model: posix` | §3.3 A7 |
| TA-6 | CRT is **msvcrt, not UCRT** | `g++ -dM -E -` over an empty TU must **not** define `_UCRT`. This is the single most valuable assertion in the file — it is the one that catches an MSYS2 UCRT64 install, which passes TA-1..TA-3 and TA-5 | §3.3 A7 |
| TA-7 | The compiler is *inside* `RECTO_MINGW_ROOT` | `get_filename_component(... REALPATH)` prefix match — stops a `PATH`-ordering accident where the pin says one thing and CMake found another | this plan |
| TA-8 | `Qt6_VERSION` is `6.11.1` and its prefix is `RECTO_QT_PREFIX` | after `find_package(Qt6)` | §6.1 |
| TA-9 | **No non-free GCC plugin.** `CMAKE_CXX_FLAGS` and `CMAKE_CXX_FLAGS_<CONFIG>` contain no `-fplugin`; the directory from `g++ -print-file-name=plugin` contains no third-party module. The list is written to `${CMAKE_BINARY_DIR}/compiler-plugins.txt` for `license-audit` to re-check | [§8.1](./Tech-Stack-Plan.md#81-obligation--concrete-action) — the Runtime Library Exception rests on this |

Flags come from the translation table in [`Tech-Stack-Plan.md` §6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management)
and are applied through `cmake/RectoWarnings.cmake`, never pasted per target: `-std=c++20` (not
`gnu++20`), `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor`, `-fno-ms-extensions`,
`-finput-charset=UTF-8 -fexec-charset=UTF-8`, `-Wa,-mbig-obj` (debug), `-municode -mwindows` (the
executable only), `-ffunction-sections -fdata-sections -Wl,--gc-sections` (release), `-g` (DWARF).
`-Werror` on `recto_core` and `recto_pdf`; `recto_ui` carries a grace list that may only shrink —
implemented as an explicit `RECTO_UI_WARNING_GRACE` list in `RectoWarnings.cmake` so shrinking it is a
visible diff.

**Never** `-static`, `-static-libstdc++`, or `-static-libgcc` — [§8.1](./Tech-Stack-Plan.md#81-obligation--concrete-action)
makes replaceability an LGPL obligation and an ABI hazard at once. `RectoWarnings.cmake` asserts none
of the three appears in any flag variable.

### 5.2 Targets and the `checks` graph

Four static libraries + one executable, per [§6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management):

```
recto_core      →  QtCore only        (+ the pure-computation allowlist, none linked yet at S0)
recto_pdf       →  recto_core
recto_platform  →  recto_core
recto_ui        →  recto_core, QtWidgets/QtGui/QtSvg
recto           →  all four                                          (D0-8: empty main, returns 0)

layer-lint      →  (nothing)          ┐ D0-1: standard library only
license-audit   →  (nothing)          ┘
checks          →  runs layer-lint, then license-audit
```

`checks` is a custom target that **depends only on the two gate executables**, never on the layer
libraries. That is the whole point: `cmake --build --target checks` compiles two small programs and
runs them, so a layering violation fails in seconds ([§9](./Tech-Stack-Plan.md#9-verification)).

At Stage 0 `recto_core` is the only library with real content (§5.5); the other three carry a single
placeholder TU each so the target graph and the link order are exercised from the start.

### 5.3 `tools/layer-lint/`

`int run(const IncludeGraph&, const RuleSet&)`.

- **`IncludeGraph`** — built by scanning every `*.{h,cpp}` under the source root for `#include` lines
  (a line-oriented scan, not a preprocessor: this must run without a compile database, before
  compiling). Each node is a repo-relative path; each edge is an include with its line number, so a
  violation is reported as `file:line`.
- **`RuleSet`** — declares layer membership by path prefix, the allowed edge set from
  [`Tech-Stack-Plan.md` §4.2](./Tech-Stack-Plan.md#42-the-dependency-direction--inward-only-stated-once),
  the `core/` external allowlist (`QtCore` headers + ICU/HarfBuzz/Hunspell + `core/backend.h` +
  `platform/platform.h`), the token rules, and D0-4's excluded paths. It is data
  (`tools/layer-lint/rules.json`), so extending the allowlist is a reviewable diff rather than a code
  change.
- **Three rule families:**
  1. **Edge rules** — any include crossing a boundary outward. `ui/` → `pdf/` or `platform/win/`;
     `core/` → anything but its allowlist; `pdf/`/`platform/win/` → `ui/`; anything → `app/`.
  2. **`FPDF_` outside `pdf/`** — token scan.
  3. **Filesystem writes outside `pdf/` and `core/settings/`** — token scan over a documented list
     (`std::ofstream`, `fopen`/`_wfopen` with a write mode, `QFile::open` with `WriteOnly`,
     `QSaveFile`, `CreateFile*` with `GENERIC_WRITE`, `std::filesystem::{remove,rename,create_*}`).
     **This one is a heuristic**, so it needs a documented, greppable escape:
     `// layer-lint: allow-write(<reason>)` on the offending line, and the tool prints every active
     escape in `--explain` output so they cannot accumulate silently.
- **`--explain`** prints the include graph (indented text; `--explain=dot` for Graphviz) and the
  active rule set including every exclusion and escape.
- **Exit codes:** `0` clean · `1` violations found (one line per violation, `path:line: rule: detail`)
  · `2` usage or I/O error. CI distinguishes 1 from 2.

### 5.4 `tools/license-audit/`

`Report audit(const VcpkgManifest&, const QtModuleList&)`.

- **Inputs:** `vcpkg.json` + the resolved package list (`vcpkg install --dry-run` output, or the
  installed tree's `share/<pkg>/copyright` where present) + `${CMAKE_BINARY_DIR}/qt-modules.txt`
  (D0-5) + `${CMAKE_BINARY_DIR}/compiler-plugins.txt` (TA-9) + `tools/license-audit/spdx-map.json`
  for the entries vcpkg does not carry an SPDX identifier for (the GCC runtime, the mingw-w64
  runtime, Qt itself, WiX).
- **Matching is on the whole normalized SPDX expression, never a substring** —
  [`Tech-Stack-Plan.md` §8.2](./Tech-Stack-Plan.md#82-third-party-license-register) is emphatic about
  this and it is the single easiest thing to get wrong here. `GPL-3.0-or-later WITH GCC-exception-3.1`
  passes; bare `GPL-3.0-or-later` fails; `LGPL-3.0` passes on its own merits and must not be caught by
  any `GPL` test. The allowlist is copied verbatim from §8.2 and is the *only* place it appears in the
  tree.
- **Qt module allowlist:** the nine modules of [§6.1](./Tech-Stack-Plan.md#61-qt-6111-modules). Any
  module in `qt-modules.txt` outside it fails — which is how "a build that links `Qt6::Charts` fails"
  is realized. `Charts`, `DataVisualization`, and `VirtualKeyboard` additionally get a *named*
  diagnostic ("GPL-3.0-only under the open-source license; see D4"), because a generic "not on the
  allowlist" message invites someone to add it to the allowlist.
- **Emits `THIRD-PARTY.md`** with, per dependency: name · exact version · SPDX expression · upstream
  URL · source-offer URL where required · whether a `NOTICE` file exists (Apache-2.0 requires shipping
  it verbatim). FreeType's **FTL election** and Hunspell's **MPL-2.0 election** are recorded explicitly
  rather than inferred.
- **Exit codes** as `layer-lint`.

### 5.5 The `core/` vocabulary headers

Header-only, `QtCore`-only, no OS and no engine — they are the first proof that `core/` builds
headless, and they are what `portable-asan` compiles on Linux from day one.

- **`core/Result.h`** — `Result<T>` and `Result<void>`; `Error { ErrorCode code; QLatin1String
  messageKey; QString context; }`. `messageKey` is a **catalog key, not a message**: `core/` must not
  hold user-facing English, and Stage 1's `i18n-lint` will start enforcing that. A `RECTO_TRY` macro
  for propagation. No `Result` constructor throws, and nothing in the header does — the whole point is
  that no exception crosses a layer ([§1.4](./Implementation-Plan.md#14-standing-policy-across-stages)).
- **`core/Log.h`** — levels `error|warning|info|debug|trace` and subsystems
  `render|parse|save|sign|ocr|batch|ui` exactly as [`Features.md` §9.5](./Features.md#95-logging-and-diagnostics)
  lists them. `trace` compiles out entirely in release. The rule *"never write document text or file
  paths into a log above the debug level"* is enforced **by the type system, not by discipline**: a
  `Redacted<T>` wrapper renders as `<redacted>` at info and above and as the real value at debug and
  below, and paths/document text are passed only through it. This is testable headlessly, which is why
  it is a type and not a code-review rule.
- **`core/Geometry.h`** — `PdfPoint`/`PdfRect` (y-up, origin bottom-left, unit = 1/72 in) and
  `DevicePoint`/`DeviceRect` (y-down, unit = physical pixel) as **distinct types with no implicit
  conversion, no shared base, and no common constructor**. Crossing between them requires an explicit
  `PageTransform`, which carries the zoom, the rotation, and the DPI. The friction is the feature:
  [`Implementation-Plan.md` § Stage 0](./Implementation-Plan.md#stage-0--toolchain-skeleton-and-the-four-gates)
  calls a silent PDF-point/device-pixel mix-up the most common bug class in this domain, and it
  manifests as "slightly wrong at 150% scaling".
- **`core/Units.h`** — a `Length` strong type stored in PDF points, with exact conversions to the five
  units [`Features.md` §1.3](./Features.md#13-rulers-grid-and-guides) names: points, inches, millimetres,
  centimetres, picas (1 in = 72 pt, 1 pica = 12 pt, 1 mm = 72/25.4 pt), plus parse and format for the
  Stage 4 rulers.

### 5.6 `tests/unit/` and `tests/gates/`

- Catch2 v3 from vcpkg, registered with CTest via `catch_discover_tests` so
  `ctest -R <name>` selects by test name, matching the commands in
  [`CLAUDE.md`](../CLAUDE.md) and [`Tech-Stack-Plan.md` §9](./Tech-Stack-Plan.md#9-verification).
- **The headless link proof is a build-time property, not a convention:** the unit test target links
  `recto_core` and `recto_pdf` and **must not** link `recto_ui` or `recto_platform`. A CMake assertion
  checks the resolved link closure and fails configure if either appears
  ([§9 *Platform boundary*](./Tech-Stack-Plan.md#9-verification)).
- Stage 0 suites: `tst_result`, `tst_units`, `tst_geometry`, `tst_log`, `tst_gates`.
  `tst_geometry`'s most important cases are **negative and compile-time**:
  `static_assert(!std::is_convertible_v<PdfPoint, DevicePoint>)` and its five siblings. A negative test
  that cannot fail to compile is worth more than a runtime test here.
- **`tests/gates/fixtures/`** (D0-4) — one directory per deliberate violation, each with a CTest case
  asserting the gate exits `1` **and** that its output contains a specific diagnostic, so a gate that
  fails for an unrelated reason does not count as passing:

  | Fixture | Gate | Must fail with |
  |---|---|---|
  | `outward-include/` | layer-lint | `core/` file including `pdf/cos/CosDict.h` |
  | `ui-includes-pdf/` | layer-lint | `ui/` file including `pdf/` |
  | `fpdf-outside-pdf/` | layer-lint | `FPDF_InitLibrary` in a `core/` file |
  | `write-outside-allowed/` | layer-lint | `std::ofstream` in a `ui/` file |
  | `manifest-gpl/` | license-audit | a Xapian-shaped `GPL-2.0` dependency |
  | `manifest-bare-gcc-gpl/` | license-audit | bare `GPL-3.0-or-later` — **proves the allowance is a whole-expression match, not a `GPL` substring hole** |
  | `manifest-gcc-exception/` | license-audit | *passes* — the same identifier `WITH GCC-exception-3.1`. The negative control; without it the previous row is satisfied by a tool that rejects everything |
  | `manifest-lgpl/` | license-audit | *passes* — `LGPL-3.0`, the substring false-positive control |
  | `qt-charts-link/` | license-audit | a target linking `Qt6::Charts` |
  | `plugin-loaded/` | license-audit | a non-empty `compiler-plugins.txt` |
  | `wrong-compiler/` | toolchain | a stub `g++` reporting 14.2.0 → **configure** fails, TA-2 named |

  The last one is worth calling out: it moves the roadmap's *manual* "point configure at a different
  GCC" case into the automated half, which is the direction every case should travel
  ([`CLAUDE.md`](../CLAUDE.md): if it can be automated, it belongs in the automated part). The manual
  case survives only for the *real* MSYS2 UCRT64 install, which a stub cannot simulate — a stub cannot
  reproduce TA-6.

---

## 6. Task breakdown

Dependencies are strict where shown. **T5 (vcpkg) gates T11**, because Catch2 comes from vcpkg.

```
T1 ─▶ T2 ─▶ T3 ─▶ T4 ─┬─▶ T5 ─▶ T6 ─────────────▶ T8 ─┐
                      ├─▶ T7 ──────────────────────────┼─▶ T9 ─▶ T13 ─▶ T14
                      └─▶ T10 ─▶ T11 ─▶ T12 ───────────┘         └─▶ T15
```

| # | Task | Depends on | Deliverable | Done when |
|---|---|---|---|---|
| **T1** | Repository skeleton | — | `git init`; `.gitignore` (build trees, vcpkg installed tree, `THIRD-PARTY.md`? **no — it is committed per release**, §8.2); `.gitattributes` (text=auto, `corpus/**` and `golden/**` binary, `*.ps1` CRLF); `scripts/env.ps1` prepending `C:\Qt\Tools\{CMake_64,Ninja,mingw1310_64}\bin`; `scripts/bootstrap.ps1` (vcpkg presence check + baseline pin); `README.md` build section | `git status` clean on a fresh clone; `scripts\env.ps1; cmake --version` reports 3.30.5 |
| **T2** | Root `CMakeLists.txt`, layer targets, `RectoWarnings.cmake` | T1 | Four static libs + `recto` (D0-8), each with a placeholder TU; the flag module | `cmake --build` produces `recto.exe` (once T3/T4 land) |
| **T3** | `toolchain-mingw.cmake` + `ToolchainAssert.cmake` | T2 | §5.1 in full, TA-1..TA-9 | every assertion fires on a deliberately wrong input; the real toolchain passes |
| **T4** | `CMakePresets.json` | T3 | `x64-debug`, `x64-release`, `arm64-release`, `portable-asan`, `portable-fuzz`. **Gotcha:** vcpkg's toolchain must be `CMAKE_TOOLCHAIN_FILE` with ours chainloaded via `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` — not the other way round; `VCPKG_TARGET_TRIPLET=x64-mingw-dynamic` | all five configure (arm64 configure-only, O6) |
| **T5** | **vcpkg bootstrap** — *blocking precondition, yours* | T1 | The **§7.1 checklist V-1 … V-6**: vcpkg at `C:\Users\squirrel\vcpkg`, `VCPKG_ROOT` visible to the running session, baseline commit pinned in `vcpkg-configuration.json` | **V-6 passes** — the process scope is non-empty and the directory exists; `vcpkg version` runs; the baseline is recorded in the repo |
| **T6** | `vcpkg.json` + **A5 sweep** | T5, T4 | Every §6.2 dependency declared, later-stage ones behind features (D0-3); `vcpkg install --dry-run --triplet x64-mingw-dynamic` for the default set **and** each feature | **A5 verdict recorded** (§8). Any port that resolves only under MSVC becomes a `tools/ports/` overlay or a named finding with an owning stage |
| **T7** | `tools/layer-lint/` | T2 | §5.3 | runs clean on the real tree; `--explain` prints the graph |
| **T8** | `tools/license-audit/` + `recto_link_qt()` | T6, T2 | §5.4, §D0-5; `THIRD-PARTY.md` generated | output lists Qt 6.11.1 and the GCC runtime with its exception |
| **T9** | `checks` target + `tests/gates/` | T7, T8 | §5.2, §5.6 fixture table | `--target checks` clean on the tree; every fixture fails with its named diagnostic; the two passing controls pass |
| **T10** | `core/` vocabulary headers | T2 | §5.5 | compile clean under `-Werror`; the `static_assert` negatives hold |
| **T11** | `tests/unit/` harness | T10, T6 | Catch2 wired, `catch_discover_tests`, the headless-link CMake assertion | `ctest --preset x64-debug` green; configure fails if `recto_ui` is added to the link closure |
| **T12** | **PDFium spike — A6** | T6 | Pinned prebuilt DLL (D0-6) with recorded SHA-256; `gendef` + `dlltool` → `libpdfium.dll.a`; `tools/spike-pdfium/main.cpp` doing `FPDF_InitLibrary` → `FPDF_LoadDocument` on one corpus file → `FPDF_GetPageCount` → `FPDF_DestroyLibrary` | **A6 verdict recorded**; `objdump -p spike.exe` imports `pdfium.dll` and `msvcrt.dll`, never `ucrtbase.dll`. The spike is **throwaway** — the durable output is the verdict plus a documented acquisition step in `cmake/FindPdfium.cmake`, which Stage 2 inherits |
| **T13** | CI — GitHub Actions | T9, T11, T12 | §9 | all three jobs green on a push |
| **T14** | `portable-asan` proof on Linux | T13, T10 | the Linux job builds `core/` (+ `pdf/cos/`, empty at S0) and runs the unit suite under ASan+UBSan | green, and it proves `core/` builds with **no display and no PDF engine** from day one |
| **T15** | Verdicts and doc lockstep | T14 | §8 | every row of §8 filled in and the owning documents edited |

---

## 7. Blocking preconditions — setup checklist

T5, T6, and T12 cannot start until these are done. **Owner** says who does each one; the checkboxes are
the live record, so tick them here rather than in a chat message.

### 7.1 vcpkg — owner: **you**

**Verified 2026-08-17.** vcpkg is installed and the path is settled. The one item still open is V-5 —
the *running session's* process scope, which no amount of registry setting reaches.

- [x] **V-0 · Path settled** — `C:\Users\squirrel\vcpkg`, taken from the registry value itself rather
      than from the earlier `vkpkg` typo; the plan, the guide, and `bootstrap.ps1` all use that spelling
- [x] **V-1 · Clone vcpkg** — present at `C:\Users\squirrel\vcpkg` with `.git/` and `ports/`
- [x] **V-2 · Bootstrap it** — `vcpkg.exe` present and reports
      **`2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8`**
- [x] **V-3 · Baseline commit recorded** — **`94a541197763a4f449a1b91478df48c0584a6256`**
      (`2026-08-15`, *[gmmlib] Update to 22.10.1 (#53413)*). **T5 pins exactly this value** in
      `vcpkg-configuration.json`; an unpinned baseline means the build is not reproducible, which
      defeats the purpose of manifest mode. A later `git pull` in that clone does **not** change the
      pin — only an edit to `vcpkg-configuration.json` does, which is the point
- [x] **V-3a · The A5 triplet exists** — `triplets/community/x64-mingw-dynamic.cmake` is present.
      Note **community**, not core: that is precisely why A5 is a question rather than an assumption
      ([`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified))
- [x] **V-4 · Set `VCPKG_ROOT` at user scope** — done; the user registry reads
      `C:\Users\squirrel\vcpkg`. (`setx VCPKG_ROOT "…"`, or System Properties → Environment Variables.)
      Note this was done **before** V-1, so the variable currently points at a directory that does not
      exist — harmless once V-1…V-3 land, and caught by V-6 either way
- [ ] **V-5 · Make it visible to this session.** `setx` and the GUI write to the registry and broadcast
      `WM_SETTINGCHANGE`, which only newly launched shells honor. **Every already-running process keeps
      the environment block it started with** — including Claude Code, whose child shells inherit that
      stale block, so a fresh tool call does *not* pick the value up. Pick one:
      - **Restart Claude Code** (or the terminal that launched it) — the durable fix
      - Per-shell, for one command:
        `$env:VCPKG_ROOT = [Environment]::GetEnvironmentVariable('VCPKG_ROOT','User')` — shell state does
        not persist between tool calls, so this must be repeated each time
      - **Preferred long-term:** don't depend on machine state at all. `scripts/env.ps1` (T1) sets
        `VCPKG_ROOT` alongside the `C:\Qt\Tools\*\bin` PATH entries, so the build works in any shell on
        any machine — which is also what makes the clean-machine case **M-01** in the testing guide pass
- [ ] **V-6 · Verify** — `"$env:VCPKG_ROOT"`, `[Environment]::GetEnvironmentVariable('VCPKG_ROOT','User')`,
      the `Machine` scope, and that the directory exists.
      **Partially passing as of 2026-08-17:** user scope `C:\Users\squirrel\vcpkg` ✅ · machine scope
      empty (fine — user scope is sufficient) · directory and `vcpkg.exe` ✅ · **process scope of the
      running session still empty** ❌. That last one is V-5's residue and is not a vcpkg problem;
      `scripts/env.ps1` (T1) removes it permanently by setting `VCPKG_ROOT` itself, at which point this
      case stops depending on machine state at all.
      *T5 may now proceed; T6's A5 sweep needs the process scope resolved, by restart or by `env.ps1`.*

### 7.2 Network and remote — owner: **you**

- [ ] **N-1 · Network access to `github.com/bblanchon/pdfium-binaries` releases** — T12's pinned DLL
      download (D0-6). Without it, A6 has no verdict
- [ ] **N-2 · A GitHub remote for the repository** — T13's three CI jobs. `gh` is installed, but
      creating or pushing to a remote is outward-facing, so **I will ask before doing it** rather than
      assuming. Tell me the account and visibility, or create it yourself and give me the URL
- [ ] **N-3 · Confirm the CI runner route** — GitHub-hosted `windows-latest` plus a Qt install action,
      or a self-hosted runner on this machine (§9, §12). This can wait until T13, but not past it

### 7.3 Corpus seed — owner: **me**

- [ ] **C-1 · One ordinary PDF in `corpus/`** for the T12 spike to open. Stage 2 owns the real corpus;
      Stage 0 needs exactly one file, and I will generate a trivial one rather than borrow a document of
      yours

---

## 8. Verdict ledger — what Stage 0 owes, and where each answer lands

A verdict is not recorded in this file. Each goes to its **owning document**, in the same change as
the work that produced it:

| Item | Question | Recorded in |
|---|---|---|
| **A5** | Does the §6.2 dependency set resolve on `x64-mingw-dynamic`? | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified) — struck through with the date and the measurement, the way A7 already is · plus Stage 0's status block in `Implementation-Plan.md` |
| **A6** | Does a PDFium DLL link from `g++` and open a file? | same two places |
| **A7** | Does it *stay* settled? | no new record — the assertion set TA-1..TA-9 and the `wrong-compiler` fixture are the record |
| **O10** | Which sanitizer route? | **stays open**; Stage 0 records what the portable job costs and covers in `Implementation-Plan.md` §8, and Stage 2 decides |
| Ports needing an overlay | which §6.2 entries lack a serviceable `x64-mingw-dynamic` port | `Tech-Stack-Plan.md` §6.3's vendored-port line, with the owning stage named |

---

## 9. CI design (D0-7)

Three jobs, and the ordering is the point: **`checks` runs before anything is compiled**
([§9](./Tech-Stack-Plan.md#9-verification)).

| Job | Runner | Does | Why it is shaped this way |
|---|---|---|---|
| `gates` | `ubuntu-latest` | builds `layer-lint` + `license-audit` and runs `--target checks`; runs `tst_gates` | D0-1's payoff: **no Qt, no vcpkg, no Windows** — under a minute, and it is the job that fails first. A layering violation must not wait behind a Qt install |
| `windows` | `windows-latest` + Qt install action, **or a self-hosted runner on this machine** | `cmake --preset x64-release` → `--target checks` → build → `ctest` | The A7 pin means CI must install *exactly* Qt 6.11.1 mingw_64 **and** `tools_mingw1310`. If the Qt install action cannot supply that tool package, the self-hosted route is the fallback — the pin is not negotiable to make CI easier |
| `portable-asan` | `ubuntu-latest` | configures `portable-asan`, builds `core/` + `pdf/cos/` + unit tests, runs `ctest` under ASan+UBSan | Recovers the gate MinGW cannot give us (R7), and mechanically proves the portability the architecture claims |

`portable-fuzz` is wired in the presets but has **no targets until Stage 2** gives it a lexer — a
documented Stage 0 deferral, not a gap. `THIRD-PARTY.md` is uploaded as a build artifact from the
`windows` job so a license change is visible in the PR without a local build.

---

## 10. Lockstep debt this stage creates

Nine of the files in §4 have **no row in `Architecture-FileMap.md`** — the build files, the CMake
modules, the gate fixtures, the CI workflow, and the scripts. `CLAUDE.md`'s rule is that a
*Components* entry resolves to a FileMap row and that rows flip in the same change as the code.

**T15 therefore includes adding those rows to `Architecture-FileMap.md` §3.17**, not just flipping the
existing ones. Flagging it here rather than discovering it at sign-off: the map is meant to answer
"what is this file for" for *every* file, and a build system that is invisible to it is the first
place the map starts drifting from the tree.

The other lockstep edits T15 owes:

- `Implementation-Plan.md` — Stage 0 status ⚪ → ✅ with deviations; §5's Stage 0 guide row; §8's O10 note.
- `Architecture-FileMap.md` — the S0 rows ○ → ✅, plus the new rows above.
- `README.md` §5 and *Current state* — both currently say no guide exists and nothing is implemented.
- `Tech-Stack-Plan.md` §3.3 — the A5 and A6 rows.

---

## 11. Explicitly out of scope

Per [`Implementation-Plan.md` § Stage 0 *Deferrals*](./Implementation-Plan.md#stage-0--toolchain-skeleton-and-the-four-gates):

- **`mutation-lint`** — needs a `Command` (Stage 3). **`i18n-lint`** — needs `tr()` sites (Stage 1).
  Two of the four gates therefore do not exist yet, and `checks` runs the two that do.
- **arm64** — configured, not validated on hardware; per R8 it may need LLVM-MinGW rather than GCC,
  which is **O6**'s problem at Stage 15.
- **`portable-fuzz` targets** — Stage 2.
- **Any product behaviour.** `recto.exe` opens no window and does nothing (D0-8). No settings key
  exists — `SettingsSchema` is Stage 1.
- **The real corpus, golden baselines, and benchmarks** — Stage 2.
- **Signing, packaging, the release gate** — Stage 15.

---

## 12. Risks inside Stage 0

| Risk | Signal | Response |
|---|---|---|
| A §6.2 port has no working `x64-mingw-dynamic` build (**A5 fires**) | T6 dry-run fails on a port | It is a **finding, not a blocker**: vendor an overlay port in `tools/ports/`, or record the gap against the stage that first needs it. ICU, Tesseract, and OpenJPEG are the likeliest. Do not switch triplet to make it pass — that would silently reintroduce the MSVC ABI question |
| The prebuilt PDFium DLL will not link or crashes on load (**A6 fires**) | T12 fails | Fallback is already named in [§3.3 A6](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified): build PDFium from source with its own clang toolchain and emit a DLL. **Not** switching engines, and **not** a static MSVC `pdfium.lib` |
| GitHub's Qt install action cannot supply `tools_mingw1310` | T13's `windows` job cannot be provisioned | Self-hosted runner on this machine. The pin outranks CI convenience |
| The write-detection lint (§5.3 rule 3) produces false positives | `checks` red on innocent code | The documented `// layer-lint: allow-write` escape, surfaced in `--explain`. If escapes exceed a handful, the rule is wrong and gets narrowed — an unusable gate gets disabled, which is worse than a narrow one |
| `-Werror` on GCC 13.1.0 flags something GCC 12 or MSVC would not | build red on day one in `core/` | Fix the code, not the flag. The toolchain pin exists precisely so this set is stable ([§6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management)) |
| Toolchain assertions run inside `try_compile` and break probing | configure fails for a confusing reason | The §5.1 two-file split. This is a known CMake trap, not a hypothetical |

---

## 13. Definition of Done for Stage 0

Stage 0 is done when [`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions)
is met, which for this stage reads:

1. Every task T1–T15 complete.
2. `cmake --build --preset x64-release --target checks` clean — the two live gates.
3. `ctest --preset x64-debug --output-on-failure` green: `tst_result`, `tst_units`, `tst_geometry`,
   `tst_log`, `tst_gates`. (No prior stage exists to re-run.)
4. Golden and round-trip suites: **not applicable** — they arrive at Stages 2 and 3.
5. [`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md) — written from this plan
   **before the code**, its automated half run green by me with actual output reported, its manual half
   walked and signed off **by you**. "Automated green, manual not walked" is a reportable state; it is
   not done.
6. No open S1/S2 issue in the guide's issue log.
7. `Architecture-FileMap.md` S0 rows ○ → ✅ **and the §10 missing rows added**, same change.
8. `Implementation-Plan.md` Stage 0 status updated to reality, deviations included; **A5 and A6 have
   recorded verdicts**; A7 remains settled and the `wrong-compiler` fixture proves it.
