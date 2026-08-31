# RawPage — Implementation Roadmap (Staged Build Plan)

## Context

This is RawPage's **staged build plan** — the execution backbone and the living progress log. It is
derived from three upstream documents and adds only *ordering, verification, and status*:

- [`Features.md`](./Features.md) — **what** to build, and the authoritative key-binding tables that
  each stage below cross-references. No stage redefines a chord.
- [`Tech-Stack-Plan.md`](./Tech-Stack-Plan.md) — **how**: the locked stack, the layer contract, the
  mutation-through-commands design, and the risk-reducing first milestone that becomes Stages 1–3.
- [`Architecture-FileMap.md`](./Architecture-FileMap.md) — **where**: every *Components* entry below
  names a file that has a row in that map, with the same stage tag. *For per-file detail, read the
  map; this document does not repeat it.*

**Stack recap:** C++20 on **MinGW-w64 GCC 13.1.0, Qt's own bundled compiler** · Qt 6.11.1 Widgets,
dynamically linked (LGPL-3.0) · PDFium (BSD-3) as a C-API DLL for read/raster + a first-party COS
layer for writes · CMake 3.30.5 + Ninja 1.12.1 (both from the Qt install) + vcpkg
(`x64-mingw-dynamic`) · Catch2 v3 · MSI/MSIX.

**The promise.** Every stage carries a fixed template — *Status, Objectives, Components, Key
bindings handled, Data/settings keys, Cross-cutting additions, Algorithms & notes, Testing, Exit
criteria, Deferrals* — and **each stage is independently verifiable before the next begins**. A
stage is not done because it looks done; it is done when its exit criteria are met, its automated
suite is green, and its manual checklist has been walked.

---

## 1. Shared infrastructure & conventions

These apply to **every** stage. They are built in Stages 0–3 and then reused, never re-invented.

### 1.1 The action layer — one dispatch path for all input

Every user action is a named `ActionId`. The ribbon, the Quick Access Toolbar, context menus,
keyboard chords, the tool finder, the command line, and batch sequences **all** resolve to an
`ActionId` and call `ActionRegistry::dispatch(ActionId, KeyContext)`. Nothing is wired directly to a
widget slot.

This buys three things that would each otherwise be a project of its own: bindings are **remappable**
(the store is data, not code); the same chord **disambiguates by context** — which is the only way
`Ctrl+K` can be *Preferences* while reading and *Insert hyperlink* while writing, exactly as
[`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference) specifies; and every
feature is reachable **headlessly**, which is what makes Stage 14's batch runner and CLI possible
without a second implementation of anything.

`KeyContext` is computed from focus by `ui/shell/ShortcutBinder` and is one of: `Global`, `Viewer`,
`WriteMode`, `AnnotationTool`, `Thumbnails`, `Bookmarks`, `Comments`, `FormsDesign`, `FormsFill`,
`Search`, `FullScreen`.

### 1.2 One typed settings store

`core/settings/Settings` over `%APPDATA%\RawPage\settings.ini`, with `PolicyOverlay` layering
machine-wide administrative defaults and locks on top. Conventions:

- Every key is declared once in `SettingsSchema` with a type, default, range, group, and a
  policy-lockable flag. **An undeclared key is a compile-time error** — this is what stops settings
  sprawl across seventeen stages.
- Groups mirror the Preferences pages of
  [`Features.md` §9.1](./Features.md#91-preferences-groups): `general/`, `documents/`,
  `pageDisplay/`, `fullScreen/`, `commenting/`, `forms/`, `identity/`, `measuring/`, `reading/`,
  `search/`, `security/`, `signatures/`, `trust/`, `units/`, `spelling/`, `multimedia/`,
  `convertTo/`, `convertFrom/`, `batch/`, `javascript/`, `performance/`, `language/`, `shortcuts/`,
  `updates/`, `ai/`, `logging/`.
- Named-profile collections (stamps, watermarks, headers/footers, security schemes, signature
  appearances, conversion profiles, batch sequences) go through the one `ProfileStore`, never into
  ad-hoc files.

### 1.3 The cross-cutting gate — mutation-through-commands

Per [`Tech-Stack-Plan.md` §5](./Tech-Stack-Plan.md#5-the-headline-cross-cutting-concern-designed-end-to-end-mutation-through-commands):
**every document change is a `Command` run through `CommandJournal`, and no `ui/` or `core/` code
writes PDF bytes.** Four gates hold it, and every gate that exists runs in CI on every push.
**Three are live today** — `layer-lint`, `license-audit` and `i18n-lint` (Stage 1 T6);
`mutation-lint` arrives at Stage 3, with the stage that gives it something to check:

| Gate | Fails the build when… |
|---|---|
| `tools/layer-lint` | an include crosses a layer boundary outward; `FPDF_` appears outside `pdf/`; a filesystem write appears outside `pdf/` and `core/settings/` |
| `tools/mutation-lint` | a `Command` subclass is not in `CommandRegistry`, or lacks a serialize round-trip test, or lacks a revert test |
| `tools/i18n-lint` | a user-facing literal is not wrapped in `tr()`, or a shipped locale is missing a catalog key |
| `tools/license-audit` | any dependency's SPDX identifier is outside the allowlist (this is what mechanically enforces the no-GPL rule) |

`cmake --build --target checks` runs all four, and **CI runs `checks` before compiling**, so a
layering violation fails in seconds rather than after a full build.

### 1.4 Standing policy across stages

- **Strings out of code** (`tr()` + `.ts` catalogs) from Stage 1, with `zh-TW` loaded in CI so a
  hard-coded string is visible immediately, not at translation time.
- **`Result<T>` at boundaries** — no exception crosses a layer.
- **Warnings are errors** (`-Werror` over `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor`) in
  `core/` and `pdf/`; `ui/` has a grace list that must only shrink. The toolchain pin is what keeps
  this from breaking the build on an unrelated GCC upgrade.
- **A new `Command` ships with its inverse and its tests, or it does not ship.**
- **Every dialog collects parameters and constructs a `Command`.** A dialog containing document
  logic is a review rejection, not a style preference.
- **No stage adds a feature that flow K (headless batch) cannot reach.** If it is only reachable
  from a widget, it is not finished.

---

## 2. Cross-cutting testing strategy

Five layers, because a PDF editor fails in five different ways.

| Layer | Covers | Where |
|---|---|---|
| **Unit (headless)** | All pure logic: page tree, outline, annotation store, form model and calculation graph, permissions, text layout analysis, selection, search, line breaking, table formulas, change tracking, signature *decisions* (mock `ICryptoProvider`), COS lexer/parser/filters/crypt, every command's inverse. Linked **without** `rawpage_ui` and `rawpage_platform`. | `tests/unit/` |
| **Golden-image render** | Rasterization fidelity. Corpus pages rendered at 96/144/192 DPI and compared to `golden/` with a perceptual threshold; a diff image is written on failure. Baselines are regenerated only through `tools/golden-update` with a stated reason — a rendering regression must never be laundered into a new baseline. | `tests/golden/` |
| **Corpus round-trip** | Save fidelity. Open → run a command → incremental save → reopen: the mutation is present, untouched objects are **byte-identical**, and a pre-existing signature still validates. Run over the whole corpus including encrypted, signed, tagged, XFA, and malformed files. | `tests/roundtrip/` |
| **Fuzzing + sanitizers** | Parser robustness — a security property, not a quality one. libFuzzer over `pdf/cos/Lexer` and `pdf/cos/Parser`, seeded from `corpus/malformed/`, plus ASan+UBSan over the `core/` and `pdf/cos/` unit suites. 10 minutes clean before every release; nightly long runs. **Neither builds under MinGW GCC** (no sanitizer runtime, no libFuzzer), so both run on the `portable-asan` / `portable-fuzz` non-Windows presets over exactly the two layers that are portable by rule — see [`Tech-Stack-Plan.md` §6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management) and risk R7. Anything they find is reproduced in the Windows `x64-debug` build before it counts as fixed. | `tests/fuzz/` |
| **Per-stage testing guide** | One guide per stage, carrying **both** halves of that stage's verification: an **automated** section — the exact commands over the four layers above, with pass conditions, run by the implementer — and a **manual** section for what a person must see on a real machine: window behaviour, multi-monitor and mixed-DPI, input and tools, shell integration, printing, scanning, signing with a physical token. **Written immediately after the stage's plan and before the stage's code** — see §5. | `Stage<N>-<Area>-Testing-Guide.md` |

Plus **`tests/bench/`**, which is not pass/fail but gates a decision: per-tile render latency at
each DPI over the corpus, settling risk A2.

### Definition of Done — every stage, no exceptions

1. Code complete for the stage's Objectives.
2. `cmake --build --target checks` clean — every gate that exists at that stage. `checks` reports
   how many it ran; if it ever *claims* four before Stage 3, that is the bug.
3. `ctest` green: the stage's new unit tests **and** every prior stage's.
4. Golden-image and round-trip suites green (from Stage 2 and Stage 3 respectively).
5. The stage's `Stage<N>-<Area>-Testing-Guide.md` was **written from the stage plan before the code**
   (§5), its **automated** section runs green, and its **manual** section has been walked and signed
   off by a person. Automated-green with the manual pass outstanding is a reportable state, not a
   done one.
6. No open S1 or S2 issue against the stage.
7. `Architecture-FileMap.md` rows for the stage's files updated from ○ to ✅ **in the same change**.
8. This document's Status block updated to reality, including any deviation from the plan.

---

## 3. Stage dependency order

```
0 ─▶ 1 ─▶ 2 ─▶ 3 ─▶ 4 ─▶ 5 ─▶ ┬─▶ 6 ─┬─▶ 8 ─▶ ┬─▶  9 ─┬─▶ 11 ─▶ 12 ─▶ 13 ─▶ 14 ─▶ 15 ─▶ 16
                              └─▶ 7 ─┘         └─▶ 10 ─┘
```

- **0 → 1 → 2 → 3** is strictly sequential and is [the first milestone](./Tech-Stack-Plan.md#7-recommended-first-milestone-foundation-vertical-slice). Nothing parallelizes here; each proves a decision the next one builds on.
- **6 and 7** may run in parallel — annotations and page assembly touch disjoint parts of the model.
- **9 and 10** may run in parallel after 8.
- **11** needs 10 (encryption re-keying shares `pdf/cos` crypt code) and 8 (incremental save of a signature dictionary).
- **13** is the largest stage and depends on 5's `LayoutAnalyzer`; it may be split 13a/13b and its two halves parallelized after 13a's model lands.
- **16** is post-v1 and its items are independent of each other.

---

## 4. Stages

### Stage 0 — Toolchain, skeleton, and the four gates

**Status:** 🟢 **DONE, 2026-08-19.** All 15 tasks complete; **both halves of [`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md) are green** — A-01…A-73 run by me, M-01…M-07 walked and signed off by the user on 2026-08-18/19. CI run `32207889494` is green on all three jobs: `gates` 25, `windows` 66, `portable-asan` 52. Verdicts recorded: **A5** and **A6**.

> **What the manual pass cost, and bought.** Seven cases produced **22 issue-log entries** and took the suite from 50 tests to 66/52. Three were **S1**, and all three came from manual cases with none reachable by the automated half: **I-01** (Qt 6.11.1 is behind the installer's Archive filter, so the README led to the wrong Qt), **I-04** (a stock Windows account runs no scripts, and no script can diagnose that — the diagnosing script is itself blocked), and **I-15** (`layer-lint` missed outward includes spelled `../`, `<>`, or with different case, and Qt writes entirely). The first two made a clean-machine build unreproducible from the documentation; the third meant the gate the next sixteen stages lean on did not fail on real violations. One **S2** followed later: **I-22**, the licence register implying no attribution was owed for eleven BSD/MIT/zlib rows. None of these was visible from a green automated run.

Both planning artifacts were written first, in the §5 order:
[`Stage0-Toolchain-Plan.md`](./Stage0-Toolchain-Plan.md) (execution plan — tasks T1–T15, the
Stage-0-local decisions D0-1…D0-8, and the §7 precondition checklist) and
[`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md) (written **from** that plan,
**before** any code). **No guide case has been formally run. The A5 verdict is recorded (T6); A6 is still open.** Preconditions V-1…V-4 verified 2026-08-17 — vcpkg at `C:\Users\squirrel\vcpkg`, baseline
`94a5411977…`, community triplet `x64-mingw-dynamic` present.

| Task | State | Outcome and deviations |
|---|---|---|
| **T1** repository skeleton | ✅ 2026-08-17 | `.gitignore`, `.gitattributes`, `scripts/env.ps1`, `scripts/bootstrap.ps1`, root `README.md`. `env.ps1` yields cmake 3.30.5 · ninja 1.12.1 · g++ 13.1.0 (`x86_64-w64-mingw32`), resolves `g++` to the pin, and sets `VCPKG_ROOT` in-process — retiring the "setx does not reach a running process" problem. **Deviation:** `claude-plan/skills-specs-generation` had been committed as a gitlink with no `.gitmodules` — an accidental submodule that would have failed a fresh clone and so **case M-01**; untracked and ignored instead |
| **T2** target graph | ✅ 2026-08-17 | Root + five layer `CMakeLists.txt`, `RawPageWarnings.cmake`, `RawPageQt.cmake`, four placeholder TUs, `app/main.cpp`. Five TUs compile clean under `-Werror`; four static libs and `rawpage.exe` link; the exe **runs, exits 0**, subsystem **Windows GUI**, imports **`msvcrt.dll`, never `ucrtbase.dll`**. `rawpage_link_qt()` **fails configure on `Qt6::Charts`** with the GPL-3.0 diagnostic. **Deviations:** (a) `-municode` deferred to Stage 1 — measured, `-mwindows -municode` with a plain `main()` fails to link (*undefined reference to `wWinMain`*), and inventing a stub to satisfy a flag is worse than waiting for the real entry point; (b) `RawPageQt.cmake` pulled forward from T7, because the wrapper must exist before the *first* Qt link; (c) guide case **A-41** corrected — see the note there |
| **T3** toolchain pin | ✅ 2026-08-18 | `cmake/toolchain-mingw.cmake` + `cmake/ToolchainAssert.cmake`, TA-1…TA-9. Clean configure reports *"Toolchain pin verified: GCC 13.1.0 x86_64-w64-mingw32, SEH/posix/msvcrt, no GCC plugin"*; `compiler-plugins.txt` written empty. **Six negative tests pass** (`tests/gates/fixtures/wrong-compiler/`, WC-1…WC-6, pulled forward from T9 and run by hand until T9 wraps them in CTest): wrong version (**TA-2**), `_UCRT` defined (**TA-6**), `__MSVCRT__` undefined (**TA-6** cannot pass vacuously), bad `RAWPAGE_MINGW_ROOT`, injected `-fplugin`, and `-static-libstdc++` — each failing configure with its own diagnostic. **Deviations:** (a) `CMAKE_SYSTEM_NAME` is **not** set, against the plan's §5.1 sketch — setting it in a toolchain file switches CMake into cross-compiling mode, disabling `try_run()` and altering `find_package()`, for no benefit on a native build; (b) the `-fplugin` scan is **duplicated** into the toolchain file, because a plugin that fails to load breaks GCC's own compiler probe *before* `project()`, and CMake then reports "the compiler is broken" instead of the licence consequence; (c) **TA-6's probe now compiles with `CMAKE_CXX_FLAGS`** — writing WC-2 exposed that the original bare-compiler probe let `-DCMAKE_CXX_FLAGS=-D_UCRT` configure cleanly and report "msvcrt" while the whole build compiled as UCRT, i.e. the assertion was interrogating a compilation the project never performs. **Still untested:** TA-1, TA-3, TA-4, TA-5, TA-7 (each needs a differently *built* GCC) and TA-8 (a second Qt) — recorded in the fixture README, and covered by manual case **M-02** |
| **T4** presets | ✅ 2026-08-18 | `CMakePresets.json`, version 6: `x64-debug`, `x64-release`, `arm64-release`, `portable-asan`, `portable-fuzz`, plus build and test presets. vcpkg is outermost with the pin chainloaded via `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` — verified working before `vcpkg.json` exists (manifest mode simply stays off until T6). `hostSystemName` conditions hide the Linux presets on Windows and vice versa. **Windows presets fully verified**: configure, build, and `rawpage.exe` exit 0; `arm64-release` exits 1 with the O6 explanation. **Deviations:** (a) the toolchain pin and TA-8 are **skipped** when `RAWPAGE_PORTABLE_SUBSET=ON` — a Linux gate build compiles the same sources with a different compiler on a different OS, which is the entire point, so asserting "exactly GCC 13.1.0, SEH, msvcrt" there would reject the configuration we are trying to prove; (b) `arm64-release` **fails at configure** rather than configuring, and guide case **A-03** was tightened from "succeeds or fails explicitly" to one expected outcome. **Untested:** `portable-asan` and `portable-fuzz` are JSON-schema-valid (`--list-presets` parses the whole file) but **have never been run** — there is no Linux here. They are first exercised by CI at T13, and that is where a mistake in them will surface |
| **T5** vcpkg baseline | ✅ 2026-08-18 | `vcpkg-configuration.json` pins baseline `94a541197763a4f449a1b91478df48c0584a6256` and registers `tools/ports/` as an overlay directory (created, empty, with the rule written down: a port that builds only under MSVC is vendored there, **never** worked around by changing the triplet) |
| **T6** manifest + **A5** | ✅ 2026-08-18 | `vcpkg.json` declares every §6.2 vcpkg dependency across five features. **A5 verdict recorded** in [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified): all five groups **resolve** on `x64-mingw-dynamic`; no MSVC-only port; no overlay needed. The default set was **really built** — catch2 3.15.3 → `libCatch2.a`, installed and consumed through the preset. **Deviations:** (a) D0-3 is applied more broadly than written — *everything* except the test harness sits behind a feature, because making OpenSSL a default dependency would couple Stage 0's build to a Stage 10 need and let a Stage 10 port failure block Stage 1; (b) `VCPKG_HOST_TRIPLET=x64-mingw-dynamic` added to the presets, since the default `x64-windows` host triplet resolves MSVC — present here, absent by design in **M-01**. **Not closed:** resolution ≠ compilation. `codecs`, `text`, `crypto`, `ocr` have never been compiled; ICU and OpenSSL are the known MinGW risks, and each group closes when its stage switches the feature on |
| **T7** layer-lint | ✅ 2026-08-18 | `tools/layer-lint/` (RuleSet · IncludeGraph · LayerLint · main) plus `tools/common/Json.h`, all standard-library-only per D0-1. **Clean on the real tree** (13 files, 38 includes) and **six fixtures verified by hand** in `tests/gates/fixtures/layer-lint/`: `core/`→`pdf/`, `ui/`→`pdf/`, `FPDF_` outside `pdf/`, a write outside the allowed layers, `<QtWidgets/QWidget>` in `core/` — each exit 1 with `file:line: rule: detail` — plus **`write-escaped`, a negative control proving the inline waiver works** (exit 0). `--explain` prints the graph, the rules, the exclusions, and every live escape; a missing rules file exits **2**, not 1. **Deviations:** (a) external includes are checked by **deny-list, not allowlist** — an allowlist would have to enumerate the whole standard library and would fail closed on every unlisted header, which teaches people to edit the rules file to get work done; the linker enforces the remainder, since `core/` links only `Qt6::Core`; (b) `tools/common/Json.h` is a new shared file the plan did not name — the gates cannot use Qt's JSON (D0-1) and T8 must read `vcpkg.json`; (c) `core/settings/` is its own layer entry with longest-prefix-first matching, since it is the one part of `core/` permitted to write; (d) fixtures pulled forward from T9, which now inherits working cases. **`tools/layer-lint/` flipped ○ → ✅ in `Architecture-FileMap.md`** — the first row in the project to do so |
| **T8** license-audit | ✅ 2026-08-18 | `tools/license-audit/` (LicenseAudit · Inventory · main) + `spdx-map.json`. Reads **vcpkg's own SPDX SBOM** (`vcpkg.spdx.json`) for exact versions and concluded licences, falling back to the map; runs with **no installed tree at all**, because the CI gates job has no vcpkg. `THIRD-PARTY.md` generates. The allowlist appears in exactly one place. **Deviations:** (a) the plan's signature `audit(const VcpkgManifest&, const QtModuleList&)` is preserved, so the compiler-plugin re-check is a separate `checkCompilerPlugins()` called from `main`; (b) **findings are blocking or warning** — see the note below |
| **T9** checks + gate tests | ✅ 2026-08-18 | `cmake/RawPageChecks.cmake` and `tests/gates/`. `--target checks` runs both gates and **depends only on the two gate executables, never the layer libraries** — the property that makes "gates before compile" real. **16 CTest cases, all passing**, via `RunGateCase.cmake`, which asserts **both** exit code and diagnostic: CTest's `WILL_FAIL` cannot tell exit 1 from exit 2 (a correct finding from a crashed gate), and `PASS_REGULAR_EXPRESSION` ignores the exit code entirely. Includes four **negative controls** — the escaped write, `GPL-3.0-or-later WITH GCC-exception-3.1`, `LGPL-3.0`, and an allowed Qt module set — without which every positive case is equally satisfied by a gate that rejects everything. **Extended after a completeness audit: 23 cases**, and to **25** when I-08 added the two `THIRD-PARTY.md` emission cases with their `EXPECT_FILE`/`EXPECT_ABSENT` assertions. The six `wrong_compiler` cases (TA-2, TA-6 both directions, bad root, `-fplugin`, `-static-libstdc++`) had been written and passing by hand at T3 but never wired, while the testing guide already named `ctest -R gates.wrong_compiler` as their command. They now run against a **minimal fixture project** rather than the real one: each case is a full CMake configure, and the real project pulls in vcpkg and Qt at ~200 s apiece, which would have put twenty minutes into a suite that otherwise runs in 3.5 s |
| **T10** core vocabulary | ✅ 2026-08-18 | `core/Result.h`, `Log.h`, `Geometry.h`, `Units.h` — header-only, QtCore-only, no OS and no engine. `Result<T>`/`Result<void>` with `RAWPAGE_TRY`; `Error` carries a **catalog key, not a sentence**, since `core/` has no `tr()` context and must hold no user-facing English. `Log.h` enforces *"never write document text or file paths above debug level"* **by type**, via `Redacted<T>` — a code-review rule is enforced by whoever happens to review; a type is enforced by the compiler. `Geometry.h` keeps PDF space (y-up, points) and device space (y-down, pixels) as types with **no conversion between them at all** except through `PageTransform`, which must be handed zoom, rotation and physical DPI |
| **T11** unit harness | ✅ 2026-08-18 | `tests/unit/` on Catch2 3.15.3, registered with `catch_discover_tests`. **50 tests pass** (16 gate + 7 toolchain + 27 unit); **66** since I-08, I-10, I-11, I-13, I-15, I-17 and I-20 (26 gate + 13 toolchain + 27 unit); Linux runs 50, one fewer gate case than Windows minus the toolchain pin, because the case-mix spelling cannot occur on a case-sensitive filesystem (I-19). The **headless boundary is asserted at configure time, over the resolved link closure** — verified by adding `rawpage_ui` and watching configure refuse. `DL_PATHS` points CTest at the mingw runtime, or every test fails to *launch* with `0xC0000135`, which looks nothing like a test failure. **Deviation:** `rawpage_link_qt()` changed from `PRIVATE` to `PUBLIC` — `core/`'s public headers include `<QString>`, so Qt's include path is part of core's *interface*; with `PRIVATE`, every consumer fails to find QString and the tempting repair is to re-link Qt in each one, scattering the dependency the layer contract keeps in one place |
| **T12** PDFium spike — **A6** | ✅ 2026-08-18 | **A6 verdict recorded** in [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified). Pinned **PDFium 153.0.8009.0** (`chromium/8009`, non-V8, SHA-256 verified); `gendef` read 472 exports and `dlltool` produced the GNU import library upstream does not ship; the spike links from `g++`, opens `corpus/minimal.pdf`, and reports `pages=1`, `595.276 x 841.890 pt`. Imports are `msvcrt.dll`, **never `ucrtbase.dll`**. Durable outputs: `scripts/fetch-pdfium.ps1` (hash-pinned, idempotent) and `third_party/pdfium/README.md`; the spike source stays until Stage 2's facade replaces it. **C-1 done**: `corpus/minimal.pdf`, generated by `corpus/make-minimal.py` rather than hand-typed, because a PDF xref stores absolute byte offsets and a one-character edit invalidates every one of them. **Two findings:** (a) `pdfium.dll` imports **no C runtime at all** — it statically links its own — so there is no shared-CRT surface in either direction, a *stronger* result than A6 assumed; the consequence is that **memory allocated by PDFium must be freed by PDFium**, now a rule rather than a style note; (b) the prebuilt archive's `LICENSE` is the **packager's MIT**, not PDFium's BSD-3 text or its third-party notices, which §8.1 requires us to ship — carried to Stage 15 |
| **T13** CI | 🟡 2026-08-18 — **`gates` green; `windows` blocked on infrastructure** | `.github/workflows/ci.yml`, three jobs, `windows` and `portable-asan` both `needs: gates`. **First run: `gates` PASSED** — the enforcement gates run on a bare Linux runner with no Qt and no vcpkg, in seconds, before anything else starts. That validates D0-1 and the ordering guarantee. Writing the job also exposed a **false claim**: the root `CMakeLists.txt` called `find_package(Qt6 REQUIRED)` unconditionally, so the no-Qt job could not have existed; fixed with `RAWPAGE_GATES_ONLY` and a `gates` preset, verified locally with Qt and vcpkg deliberately hidden (3.8 s, 23/23). **`portable-asan` failed and is fixed**: `ubuntu-latest` exports vcpkg as `VCPKG_INSTALLATION_ROOT`, not `VCPKG_ROOT`, so the preset's `$env{VCPKG_ROOT}` expanded to empty — `windows-latest` sets both, which is why it only failed on Linux. **`windows` failed for a cause outside our control** — see below |
| **T14** portable-asan proof | ✅ 2026-08-18 | Ran in CI on `ubuntu-latest`: **43 tests clean under ASan+UBSan** (16 gate + 27 unit); **45** since I-08, pending the next CI run. This is the layer contract proven mechanically rather than asserted — `core/` compiled by a *different compiler* on a *different OS*, with no display and no PDF engine present. `-fno-sanitize-recover=all` is set, so a finding aborts rather than printing and continuing. The 43-vs-50 gap is the seven toolchain-pin cases, correctly absent where there is no toolchain to pin |
| **T15** verdicts and lockstep | ✅ 2026-08-18 | `Architecture-FileMap.md` gained **§3.18** (build system, scripts, CI) and every Stage 0 row is ✅ — the map previously described every planned `.cpp` and none of the machinery that compiles them. Testing guide reconciled against what actually ran. `Tech-Stack-Plan.md` carries the A5 and A6 verdicts, the corrected Hunspell licence, and the `-fanalyzer` finding |

> **⚠️ `aqtinstall` cannot install Qt 6.11.1 — CI's Windows job needs a self-hosted runner.**
> Investigated 2026-08-18 rather than assumed. Qt 6.11.1 *is* published and `tools_mingw1310`
> *is* listed; `Updates.xml` and its `.sha256` both return HTTP 200. With hash checking
> disabled, aqt reveals the real error: it requests `…/qt6_6111/qt6_6111/Updates.xml` → **404**,
> while Qt 6.11 splits its repository per architecture as `…/qt6_6111/qt6_6111_mingw/` → 200.
> aqtinstall **3.3.0 (latest) and 3.2.1 fail identically**, and a trusted-mirror config does not
> help — the "checksum" error was masking the 404.
>
> The choice was: downgrade the Qt pin to a version aqt can fetch, or run the job where the
> pinned toolchain already lives. **Downgrading was rejected** — TA-8 asserts 6.11.1, and
> testing against a different Qt than we ship is exactly the ABI question A7 exists to prevent.
> [`Stage0-Toolchain-Plan.md` §9](./Stage0-Toolchain-Plan.md#9-ci-design-d0-7) already named the
> self-hosted runner as the fallback and said the pin is not negotiable to make CI easier; that
> prediction came true on the first run.
>
> The job is now `runs-on: [self-hosted, windows]`, gated on repository variable
> `RAWPAGE_HAS_WINDOWS_RUNNER` so the workflow stays honest until a runner is registered — a
> queued-forever job, or one marked `continue-on-error`, would claim coverage that does not
> exist. **Until then, Windows build/test coverage is local only**, and Stage 0's Definition of
> Done cannot be met on that item.

> **Two bugs the gate found in itself on first run, both fixed.** `$`-prefixed comment keys in
> `spdx-map.json` were being read as components with no licence — JSON has no comment syntax, so the
> convention vcpkg uses was turned against us. And the audit initially **failed** on dependencies this
> build does not ship: `vcpkg.json` declares every future dependency so that A5 could be answered early
> (D0-3), which meant Stage 0 could not go green until every future licence was reconciled — and the
> tempting way to make that go away is to widen the allowlist, the precise failure this gate exists to
> prevent. Findings are now **blocking** for components in the active feature set and **warnings** for
> declared-but-unbuilt ones, with `--feature` to promote them. The warnings are printed on every run,
> so nothing is lost, only deferred to the stage that owns it.
>
> ✅ **Finding raised and resolved, 2026-08-18.** The gate reported Hunspell's `MPL-2.0` as not on the
> §8.2 allowlist. Checking upstream showed the owning document was wrong in a way nobody had noticed:
> Hunspell's grant is **`MPL-1.1 OR LGPL-2.1-or-later OR GPL-2.0-or-later`** — it does not offer
> MPL-2.0 at all, so §6.2 was electing a licence that was never granted. Resolution (yours): elect the
> **LGPL** branch, taken as **LGPL-3.0** since the grant reads *"or later"*. That identifier is already
> on the allowlist and is exactly Qt's licence, so **the allowlist did not have to be widened** — the
> gate was satisfied by fixing the fact, not by loosening the rule. `Tech-Stack-Plan.md` §6.2 corrected
> in the same change. Consequence carried forward: Hunspell now owes a **corresponding-source offer**
> like Qt's, currently a `TODO-BEFORE-RELEASE` in `spdx-map.json` and owned by Stage 15's release gate.
> The audit's LGPL check was generalized from the exact string `LGPL-3.0` to any LGPL expression, so a
> future `LGPL-2.1-or-later` component cannot ship without a source offer.

This stage exists because the gates in §1.3 are worthless if added
later — by then the violations are already in the tree.

**Objectives.** A repository that builds an empty executable on a clean machine, with all four
enforcement gates already failing correctly on deliberately bad input, every third-party dependency
resolved and license-audited, and **the two remaining toolchain unknowns (A5, A6) settled before
either can cost a later stage a week**. A7 is already settled — the build uses Qt's own bundled
GCC 13.1.0, so there is no second toolchain to reconcile
([`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified)).

**Components & responsibilities.**
- `CMakeLists.txt`, `CMakePresets.json` — four layer targets (`rawpage_core`, `rawpage_pdf`,
  `rawpage_platform`, `rawpage_ui`) plus the `rawpage` executable; presets `x64-debug`, `x64-release`,
  `arm64-release`, `portable-asan`, `portable-fuzz`; a `checks` target.
- `cmake/toolchain-mingw.cmake` — the **pinned** toolchain: `C:\Qt\Tools\mingw1310_64` (GCC 13.1.0,
  `x86_64-w64-mingw32`, SEH, posix, msvcrt) as a cache-variable default, plus the standard
  warning/charset/subsystem flags from
  [`Tech-Stack-Plan.md` §6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management). **Configure
  fails, loudly, on any other compiler** — keeping A7 settled rather than merely documented, since
  the next developer's MSYS2 install is the realistic way this regresses. Also asserts no non-free
  GCC plugin is loaded (the Runtime Library Exception condition, `Tech-Stack-Plan.md` §8.1).
- `vcpkg.json` — manifest with a pinned baseline, resolved on the **`x64-mingw-dynamic`** triplet;
  covers every dependency in
  [`Tech-Stack-Plan.md` §6.2](./Tech-Stack-Plan.md#62-third-party-libraries). Settles risk A5, whose
  answer is now triplet-specific: a port that builds only under MSVC is found here or not at all.
- A **PDFium link spike** — the smallest program that calls `FPDF_InitLibrary` and
  `FPDF_LoadDocument` against a PDFium **DLL** and opens one corpus file, with the `.dll.a` import
  library generated if upstream ships none. Settles A6. It is throwaway code; what it produces is a
  recorded answer and the documented acquisition step for `pdfium.dll`.
- `tools/layer-lint/` — `int run(const IncludeGraph&, const RuleSet&)`; `--explain` prints the graph.
- `tools/license-audit/` — `Report audit(const VcpkgManifest&, const QtModuleList&)`; emits
  `THIRD-PARTY.md`; fails on any SPDX identifier outside the allowlist.
- `core/Result.h`, `core/Log.h`, `core/Geometry.h`, `core/Units.h` — the shared vocabulary.
  `Geometry.h` gives PDF-space and device-space point/rect types that **do not implicitly convert**.
- `tests/unit/` — Catch2 harness, linked without `rawpage_ui` and `rawpage_platform`.
- CI pipeline — `checks` → build → `ctest`, on every push.

**Key bindings handled.** None.

**Data / settings keys.** None (the schema arrives in Stage 1).

**Cross-cutting additions.** None — the mutation gate turns on in Stage 3, but `layer-lint` and
`license-audit` are live from this stage.

**Algorithms & notes.** `Geometry.h`'s non-converting coordinate types are worth the friction: a
silent PDF-point/device-pixel mix-up is the single most common class of bug in this domain, and it
manifests as "slightly wrong at 150% scaling" — the hardest kind to find later.

**Testing.**
*Automated:* the gates are tested against fixtures — a file with an outward include, a file with a
stray `FPDF_` call, a manifest with a GPL dependency (**and one carrying bare `GPL-3.0-or-later`, to
prove the GCC-runtime allowance is an exact-expression match and not a `GPL` substring hole**) —
each must fail — **including a build that links `Qt6::Charts`, which is installed on this machine and
is GPL-3.0-only**. `Result` and unit conversions have unit tests. A clean-machine CI job proves the
build works from a bare checkout, and a second CI job configures `portable-asan` on Linux to prove
`core/` really does build without a display or a PDF engine from day one.
*Manual:* → **written**: [`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md)
(see §5), seven cases M-01…M-07 — a short manual section, because almost everything here is
automatable. In brief: clone into an empty directory on a machine with a stock `C:\Qt\6.11.1` install
and nothing else; run the documented bootstrap; confirm it builds without a single manual step. Then
deliberately point the configure at a different GCC — an MSYS2 UCRT64 one is the realistic case — and
confirm it refuses. **Refined by writing the guide:** the wrong-compiler check splits in two — a stub
compiler reporting the wrong *version* is automated (case A-04), while only a **real** UCRT64 install
exercises the CRT assertion, so M-02 is the residue a stub cannot fake.

**Exit criteria.** Clean checkout → documented command → empty `rawpage.exe` builds; `checks` passes
on the real tree and fails on each bad fixture, Qt Charts included; `THIRD-PARTY.md` is generated,
contains no non-allowlisted license, and lists Qt 6.11.1 and the GCC runtime with its exception;
**A5 and A6 have recorded verdicts** — the vcpkg manifest resolves on `x64-mingw-dynamic` and the
PDFium DLL spike opens a file — and **A7 stays settled**: a mismatched compiler fails configure.

**Deferrals.** `mutation-lint` (needs `Command`, Stage 3); `i18n-lint` (needs `tr()` sites, Stage 1 — **delivered at T6**);
arm64 is configured but not validated on hardware, and per R8 may need LLVM-MinGW rather than GCC —
that is O6's problem, not Stage 0's. `portable-fuzz` is wired but has no targets until Stage 2 gives
it a lexer.

---

### Stage 1 — App shell, action layer, settings, single instance

*Part 1 of the [first milestone](./Tech-Stack-Plan.md#7-recommended-first-milestone-foundation-vertical-slice).*

**Status:** 🟡 **T1–T15 all complete as of 2026-08-25, and the automated half is green in CI. The
stage is open on your manual walk and nothing else.** The A3 verdict is recorded, the §8 collision census is asserted, `checks` runs three gates, the OS boundary is declared in full, flow A runs end to end, and `rawpage.exe` opens a window whose ribbon — 9 tabs, 49 groups, 192 controls — is built entirely from `res/ribbon.json`, with a customizable Quick Access Toolbar, a tool finder, document tabs that open a stub per file, a keyboard in which every one of §8's Global chords resolves through `KeyBindingStore` and dispatches through the same `ActionRegistry` call the ribbon makes, and — since T14 — **a Preferences dialog whose every page is generated from `SettingsSchema`, a Shortcuts page that remaps a chord with nothing rebuilt, and a log pane over a rolling log file.** `ctest` is **334/334** on both presets and the bare `gates` preset is **66/66**. **Flow J is real**: changing the theme repaints the window behind the dialog.

**What T15 leaves open, precisely.** **Every automated guide case is green, A-80 included.** **CI is green on all four jobs** — run 32785493072, and **the `windows` job ran for the first time in the project's history**: 334/334 there, `gates` 52/52, `portable-asan` 196/196.
That job's long absence was never the missing runner every document had assumed: one had been online
since 2026-08-17, and the block was a variable **name** mismatch (**I-30**, S2). So three quarters of
the product's test surface — the widget suite, the OS layer, the toolchain pin, and the gates run
against the *real* configuration where `qt-modules.txt` and `compiler-plugins.txt` actually exist —
had never once been exercised by CI. It has now, and it was green on the first attempt. A fourth job,
`coverage-report`, states in every run summary which jobs ran and what went unexercised when one did
not, so a skipped job can never again be misread as coverage.

**The manual walk has started finding things the harness could not.** The user's M-03 walk on
2026-08-30 produced **I-33** (S3) and **I-34** (S2) — the stage's first defects **in the product**
rather than in a test, a gate, a document or the CI. I-34 is the one to remember: re-dock was
documented as shipped in four places, asserted green by a case that called `adoptDocument` directly,
and impossible for a person to perform for as long as it existed, because the gesture reported at the
40 px threshold crossing instead of at the release. Both were fixed the same day — three cases, three
negative controls, `ctest` **337/337** on both presets. Fixing I-34 then exposed **I-35** (S3) in the
same sitting — a window emptied by a re-dock stayed open, a state nothing could reach while re-dock
itself was unreachable — closed the same day with two more cases and two more controls, `ctest`
**339/339**. **M-03 was then re-walked in full and signed off by the user on 2026-08-30** — the
sign-off is on the repaired product rather than on the repair, which is the only reading of a fixed
case that means anything. Note the order: I-35 was unreachable until I-34 was fixed, so a case
interrupted by a defect is finished when it is walked again, not when the defect is closed.

**And then M-04 stopped at its own first step.** Its opening move is *View tab → Panes → Log*,
and on 2026-08-30 there was no View tab to click: the ribbon's `QTabBar` had been laid out **zero
pixels wide at every window size since T10** (**I-36**, S2), so the strip showed the tool finder
and nothing else, and only the page that happened to be current — Home — was reachable at all.
`QSizePolicy::Ignored` tells `QBoxLayout` that the widget's width preference is not a number, so a
**stretch factor** is the only thing that can state its share; beside a separate `addStretch(1)`
the spacer took the strip. Fixed the same day, `ctest` **340/340**, with the control convention 7
requires. The instructive part is why eleven green ribbon cases missed it: every one asked the tab
bar what it *contained* — page count, current index, which tabs the chevron had taken — and
`tabRect()` measures the bar's own contents, which stay perfectly correct inside a widget the
layout gave no width. Nothing asked what the **layout handed the widget**, and that is now part of
shared convention 12 in `CLAUDE.md`.

**M-04 was then re-walked from step 1 and signed off by the user on 2026-08-30**, with two more
results neither of which was a broken pane. **I-37** (S3): an auto-hidden sidebar answered a click
on its edge strip and not a hover, where the case's *Expect* asks for a hover — both gestures are
defensible, so this was closed by a **decision** rather than by default, and the user chose to move
the product to the guide; the strip now peeks on `QEvent::Enter` and still answers a click.
**I-38** is not a defect at all: `general/touchMode` is declared, stored, offered on the General
page and **read by nothing**, and the user's call was **planned absence, Stage 14** — it is now a
row in `Stage1-AppShell-Plan.md` §11 and in the guide's §1, and M-04's steps no longer ask for it.
**And re-walking that one gesture found I-39** (S3): the edge strip stayed on screen beside the pane
it had just summoned — behaviour unchanged since T9, and unseeable until I-37 made a peek something a
person could perform at all, since every earlier case reached `peek()` in a fixture where visibility
was not the subject. Fixed the same day and asserted in both directions inside I-37's own case.
Re-walked once more and **signed off by the user the same day**. `ctest` **341/341**.

**The same walk's tool-finder step then produced I-44 (S2), and it is the worst defect this stage
has found**: a query that matched nothing **froze the application**. The result list is a
`Qt::Popup` window and therefore holds the mouse grab for the whole process; the *nothing matched*
message is itself a row, so an empty result set still showed the list; and `Esc`, a click outside
and `Enter` on the unselectable row all failed to put it away. Every click anywhere went to a
popup that ignored it, and force-closing the process was the only way out. Found by making a
typo — which is how anyone would find it, and nobody had, since T11. Three exits added, each
independently missing, and the case asserts each one separately.

**And the fix for it shipped a regression, I-45 (S2), which the user found by re-walking the same
step within the hour.** Dismissing the list on any `FocusOut` closed it in the same turn it opened,
because **showing a `Qt::Popup` is itself a focus change** — the search box receives a focus-out
with reason `PopupFocusReason` exactly when the results appear — so no search showed anything at
all. The case added with I-44 could not see it: offscreen there is no such focus change, so the
assertion passed while the desktop did the opposite. Losing focus **to the popup** is not losing
focus; the rule is now asserted in both directions with the bug as its control. Two fixes for one
defect in one afternoon is the cost of a `Qt::Popup`, and it is written into the file map row.

**And answering a question about the QAT's context menu during that same sitting found I-43 (S3)**:
reordering the toolbar could not be done by a person. `moveCommand` had been public since T11 and
the order persisted since T11; `QuickAccessToolbar.h`, `CLAUDE.md` and `Architecture-FileMap.md`
all called reordering shipped; and there was no menu item, no drag and no `ActionId` — the only
caller in the tree was a test calling the method. **That is I-34's shape and the third instance in
this stage**, which is why the new case asserts the ROUTE as well as the destination: it raises
the real context menu and reads what it offers, and taking the items out of `buildMenu` fails it —
a dispatch-only case would have passed against the build that shipped. `ui.qatMoveLeft` and
`ui.qatMoveRight` are **the only ids Stage 1 has added to the plan's 358**, which now stands at
360 declared and 22 registered. Logged against **M-11**, whose steps say *customize the toolbar*.

**M-06 was signed off on 2026-08-31 after producing four defects, and it opened with I-42 (S2)**: pressing `Alt` did nothing,
in every locale. The ribbon has *drawn* key tips since T10 — two levels, `Esc` backs out — and
nothing ever asked it to: `Alt` is bound to `AppRibbonKeyTips`, `ShortcutBinder` forms the chord
and a case asserts that it does, and the id was never registered, so `dispatch` answered
`NotFound` and the key fell through. Every link was tested; the chain was not. And
`Tech-Stack-Plan.md` §3.3 had recorded **R-5 as met since 2026-08-23** — judged in M-01 against
the T2 spike, whose handler the shipped ribbon never inherited, which is I-36's shape exactly.
Registered as the **twentieth** of 358 ids, and the tip letters moved out of the tab titles into a
`ribbon.keytip.*` catalog family: the first-character scheme gave `zh-TW` 常 建 編 註 組 表 保 檢,
none of them typable, with 表 claimed by both 表單 and 表格工具. The new case asserts the letters
are ASCII and distinct **in both shipped catalogs**, because the half that was broken was broken
only in Chinese and M-01 walked English. `ctest` **345/345**. The A3 verdict stands — R-5 asked
whether Qt Widgets *can* host key tips and the spike answered it — but the row describing the
product was false for eleven days, and is now corrected the way T15 corrected R-9.

**M-05 then found I-40 (S3) at its first step**: switching Windows to dark left most of the shell's
text unreadable — not the theme failing to apply, but the **disabled** colour, which is what most
of this ribbon is drawn in because 358 commands are declared and 19 registered. Measured, it was
3.05 against the window and 2.62 against a pane; the light theme, signed off in two earlier passes,
measured **2.54** and was the worse of the two — M-01 and M-02 had walked light only, because until
M-05 nothing had a reason to switch the OS. Both raised, and the new case measures every foreground
token against the background it is actually painted over, in both themes, against a floor rather
than a target — and asserts in the same breath that disabled text stays under half the enabled
ratio, since a floor raised until the two match would trade the defect for a worse one. `ctest`
**342/342**. It is not an accessibility criterion: that left the product on 2026-08-23.

**And the I-40 fix did not work, which is how I-41 (S2) was found on 2026-08-31.** With the palette
corrected the text was still unreadable, and that is the fact that pointed away from the palette:
Qt's native `windows11` style paints control text from the colour scheme **it** derives from
Windows and ignores the application palette entirely. Rendering a widget and reading the pixels
back settled it — on a light-mode OS with the dark palette installed, an enabled `QToolButton`'s
glyph was `#1f1f1f` on a `#1f1f1f` background and a disabled one `#181b1f`, while a `QLabel` in the
same window honoured the palette exactly. So the theme reached every label and no button: the
ribbon, the toolbar, the tool finder. `apply()` now calls `QStyleHints::setColorScheme()` as well
as installing the palette, with **`Unknown` for follow-system** so the OS keeps the decision — and
that call is ordered before `systemTheme()` reads the scheme back, or "follow system" would read
our own last answer and report it as the system's. `ctest` **343/343**. The lesson is I-40's, one
level up: measuring the colour table confirmed the colour table and said nothing about what
reached the screen. **M-05 was then re-walked on the repaired build — both halves, follow-system
across a live OS flip and an explicit Dark held against a light-mode OS — and signed off by the
user on 2026-08-31.**

**M-04 took three passes and produced four findings, and that sequence is its real result**: a tab
strip nobody could click hid a pane gesture nobody could perform, which hid a strip nobody could
watch disappear. Not one of the three defects was a regression — two had shipped at T9 and T10 and
were green in CI throughout. **Four manual cases are now signed off**; **M-11 and M-12 are walkable and not yet walked**.

**The remaining Definition-of-Done item is that walk, and it is the user's alone**: M-01 and
M-02 are signed off — **M-02 re-walked in full on 2026-08-30**, all four steps against the shipped
shell rather than T2's spike, with no findings, which retires the last scoping note A3's R-6 carried;
M-03 and **M-04** are signed off too, both on 2026-08-30, and **M-05 and M-06 on 2026-08-31** —
M-05 a case this guide had listed as unwalkable, wrongly, since both halves of it had existed since
T14, and M-06 the longest walk of the stage: every surface in `zh-TW` plus the editorial review of
the 29 unfinished catalog entries, which no automated case can do. **M-11 and M-12 are walkable and
not yet walked**; M-07, M-09 and M-10 test code that does not exist yet.
Risk **A3** —
can Qt Widgets host a real ribbon, tear-off tabs, and auto-hiding panes without a commercial control
library — is **settled: `go, with named custom work`**. The fallback (a first-party ribbon widget,
*not* a change of toolkit) is not needed.

Both planning artifacts were written first, in the §5 order:
[`Stage1-AppShell-Plan.md`](./Stage1-AppShell-Plan.md) (tasks T1–T15, decisions D1-1…D1-16, the §7
precondition checklist P-1…P-4) and
[`Stage1-AppShell-Testing-Guide.md`](./Stage1-AppShell-Testing-Guide.md) (written **from** that plan,
**before** any code). Preconditions: **P-3 and P-4 satisfied**; **P-1 satisfied** when M-02 was walked
on 2026-08-23, and **exercised to its full extent on 2026-08-30** — the mid-session scale change and
the re-login at a different primary scale, which are the parts of the rig a static two-monitor setup
does not provide. **P-2 (Narrator) was withdrawn** with the accessibility removal — see
[`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23).

| Task | State | Outcome and deviations |
|---|---|---|
| **T1** entry point, DPI, crash handler, `-municode` verdict | ✅ 2026-08-23 | `app/main.cpp` rewritten; `app/CrashHandler.{h,cpp}`; `-Wl,--build-id` on every target via `rawpage_common_flags`; `scripts/symbolize.ps1`. Both presets build clean, `ctest` **66/66 on `x64-release` and `x64-debug`**, `checks` clean at **29 files / 109 includes / 3 escapes**. Guide cases run: **A-03 ✅** (`--version` under `offscreen`, exit 0, no window), **A-04 ✅** (`msvcrt.dll` present, `ucrtbase.dll` absent), **A-05 ✅** (see the verdict below), **A-06 ✅** (`PerMonitorAwareV2`, set before the first Qt object), **A-07 ✅ with a correction** (see I-02), **A-08 ✅**, **A-65 ✅** (forced crash → dump → `symbolize.ps1` resolved `rawpage::app::CrashHandler::forceCrashForTesting()` at `app/CrashHandler.cpp:290`, with the build ids of dump and binary agreeing), **A-66 ✅** (the last-rites hook is invoked and its no-op state is written to the dump sidecar *and* logged at install). **Deviations:** (a) three `layer-lint` `filesystem-write` waivers were added in `app/CrashHandler.cpp` — the crash directory, the minidump, and the sidecar — each with a named reason and visible in `--explain`; this is the first use of the escape hatch in product code and takes the standing census from 0 to 3. (b) The `--print-args` diagnostic originally probed `_UNICODE` and reported `municode=yes` with `-municode` nowhere in the build: Qt6::Core defines `UNICODE`/`_UNICODE` in its INTERFACE compile definitions, so the probe was measuring Qt. It now reads `RAWPAGE_MUNICODE`, set in `cmake/RawPageWarnings.cmake` beside the link option it describes, so the diagnostic cannot drift from the flag. (c) The last-rites hook is deliberately **not** wrapped in `__try`/`__except` — GCC on Windows x64 does not implement SEH in C++, so the wrapper would have been a comment pretending to be a guard. |
| **T2** A3 ribbon spike | ✅ 2026-08-23 — **A3 verdict recorded: `go, with named custom work`** | `tools/spike-ribbon/` (throwaway, deleted at T10): a ribbon of plain `QWidget` composition plus **one custom `QLayout`**, with a `--self-test` mode. **Measured: R-2 ✅, R-4 ✅ (height), R-7 ✅, R-9 ✅ at p95 0.29 ms against a 16 ms budget.** **Walked and signed off by the user: M-01 ✅ and M-02 🟡** *(M-02 ✅ since its 2026-08-30 re-walk — see deviation (b))*, carrying R-1, R-3, R-4's flicker, R-5, R-6 and R-7. **R-8 was withdrawn** the same day when accessibility left the product, so the verdict rests on **eight criteria, not nine** — recorded plainly in `Tech-Stack-Plan.md` §3.3 rather than smoothed over. **Named custom work:** a collapse layout (~60 lines), a chevron overflow (~30), and a tab-order pass (~15). **Your M-01 walk produced I-05 and I-06:** `Tab` visited the ribbon groups in collapse-priority order rather than visual order, because collapse is implemented by re-parenting and re-parenting appends to the end of the focus chain — a durable constraint on T10, and retained as **keyboard operability** after the accessibility removal. **Deviations:** (a) the plan's 3-working-day timebox is a calendar budget that does not map onto how this was executed; the criteria, not the clock, bounded it. (b) M-02 was 🟡: its non-ribbon half names a sidebar, a floating pane and Preferences, none of which exist before T9/T14, so it was re-walked then — **done 2026-08-30, in full and with no findings, and M-02 is now ✅**. The deviation is kept rather than deleted because it is the record of a debt that was named at the time it was incurred and paid seven days later. |
| **T3** `ActionId.def`, `ActionRegistry`, `KeyContext` | ✅ 2026-08-23 | `core/cmd/{ActionId.def,ActionId.h,KeyContext.h,ActionRegistry.h,ActionRegistry.cpp}` and `tests/unit/tst_actionregistry.cpp`. **`ctest` 88/88 on both presets** (was 66); `checks` clean at **37 files / 171 includes**. All three §5.1 properties asserted: an unregistered id returns `NotFound`, a disabled one returns the **distinct** `Unavailable`, and a second registration is an error with the first handler surviving. **Two negative controls run, both inside the table region after the first pair mistakenly landed after the `#undef` and proved nothing:** a duplicate enumerator fails the build with *`ActionId.def:52: error: redeclaration of 'FileOpen'`* — the typo's own line, which is D1-6's whole claim — and a catalog key that disagrees with its string id fails `tst_actionregistry`. **Deviations:** (a) **327 ids declared, not the plan's ~200.** The id space was derived section by section from `Features.md` §1–§7 and §9 rather than estimated; under-declaring would be worse, since every string id is a permanent public name that owes an alias row if it ever changes. Guide case **A-25**'s "~200" should read ~327. (b) **`ErrorCode::Unavailable` was added to `core/Result.h`** — a Stage 0 vocabulary header. A-22 requires unregistered and disabled to be *distinct codes*, and none of the existing nine expressed "exists, but not applicable right now": `PermissionDenied` means a document or OS refusal, which a disabled action usually is not. (c) A test name containing `§` failed — see the deviation note below. |
| **T4** `KeyBindingStore` + §8 defaults + census | ✅ 2026-08-23 | `core/cmd/{Chord.h,Chord.cpp,KeyBindingDefaults.def,KeyBindingStore.h,KeyBindingStore.cpp}` and `tests/unit/tst_keybindings.cpp`. **`ctest` 110/110 on both presets** (was 88); `checks` clean at **42 files / 199 includes**. **All three census numbers assert separately and all three hold: 12 Write-mode (8 over Viewer + 4 over Global + 0 over Common), 6 class 1, 9 class 2** — computed from the live document before a line of C++ was written, and unchanged by the accessibility removal. `shadows()` reports class 1 only. **D1-9 verified by negative control in both directions:** adding a §8 row the store lacks fails with *"context Global, chord Ctrl+Alt+Y, documented as: Some new command nobody implemented"*, and deleting a row the store has fails with *"stored binding Global / Ctrl+Shift+O is not in the document"* — each naming the row, which is what A-30 demands so that "the store is wrong" and "the parser is wrong" can be told apart. **Deviations:** (a) **`Chord` is a first-party type, not `QKeySequence`.** This document's Stage 1 component list sketches `lookup(QKeySequence, KeyContext)`, but `QKeySequence` lives in **QtGui**, which `core/` may not include — `layer-lint` denies it and `core/` links only `Qt6::Core`. `ui/shell/ShortcutBinder` will convert at the boundary. (b) **A second binding kind, `WidgetNative`.** Fifteen §8 rows (`↑ ↓ ← →` in the item panes, `Tab` in the Forms contexts) move focus or selection inside the focused widget; giving them `ActionId`s would mint permanent public names for behaviours nothing dispatches. They are *recorded* rather than dropped, so "the store matches §8 row for row" stays true of every row and not just the convenient ones. Remapping **onto** such a chord is the one refusal the store makes. (c) **31 further `ActionId`s** were needed to cover §8 — presentation controls, form-fill verbs, find toggles — taking the declared total from 327 to **358**. |
| **T5** `SettingsSchema`, `Settings`, `RecentFiles` | ✅ 2026-08-23 | `core/settings/{SettingsKeys.def,SettingsSchema.h,SettingsSchema.cpp,Settings.h,Settings.cpp,RecentFiles.h,RecentFiles.cpp}`, `tests/unit/{tst_settings.cpp,tst_recentfiles.cpp}`, and four compile fixtures under `tests/unit/compile/`. **`ctest` 159/159 on both presets** (was 110); `checks` clean at 54 files / 265 includes, waiver census still **3**. All eleven scalar Stage 1 keys plus the `shortcuts/` family; D1-16 live in both directions. **A-41 is asserted three ways plus a control** (`tests/unit/RunCompileCase.cmake`): a misspelled key, a key named by string, and a right key with a wrong-typed value each fail to compile, and `declared-key.cpp` must still compile — without that control the case is equally satisfied by a header nobody can include. **Deviations:** (a) **`SettingsKeys.def`, an X-macro table, was added** — the plan's §4 manifest lists only `SettingsSchema.{h,cpp}`, but the constexpr `Key<T>` objects and the runtime descriptor table a Preferences page iterates are two lists that must agree forever, and D1-6's lesson is one table. (b) **Two keys beyond the plan's twelve are declared, both in a new `Internal` group**: `meta/schemaVersion` (which the guide already treats as internal state) and `documents/recentList`. The MRU list has to live somewhere, and "an undeclared key does not compile" is only true if there is no undeclared key — declaring it is what keeps the guarantee rather than what weakens it. (c) **`RecentFiles` is keyed on the path, not `DocumentId`** — that type arrives with `core/doc/` at Stage 2, and a recent list is a list of paths by nature. (d) **`changed` is a subscription list, not a `Signal<Key>`** — there is no `Signal` type in `core/` and inventing a core-wide one for a single user would be speculative infrastructure. (e) **Two QSettings dialect defects were found by running the tests, not by reading** — see I-08 and I-09; both are fixed, and each has a test that fails if the fix is removed. |
| **T6** `tools/i18n-lint` + catalogs + the third gate | ✅ 2026-08-23 | `tools/i18n-lint/{main.cpp,I18nLint.{h,cpp},I18nRules.{h,cpp},Catalog.{h,cpp},SourceScan.{h,cpp},rules.json}`, `tools/common/LineScan.h`, `i18n/{locales.json,rawpage_en.ts,rawpage_zh_TW.ts}`, and 14 gate cases over 10 fixture trees. **`checks` runs three gates and says three.** **`ctest` 173/173 on both presets** (was 159); the bare `gates` preset — no Qt, no vcpkg — is **53/53**, which is A-11. Guide cases run: **A-10 ✅**, **A-11 ✅**, **A-13 ✅** (five spellings in one tree, and the case asserts the *count*, so finding four of five would not pass), **A-14 ✅**, **A-15 ✅**, **A-16 ✅**, **A-17 ✅** (the applied waiver waives; a quoted marker, an unknown kind, and a marker on a codeless line each waive nothing), **A-70 ✅**, **A-71 ✅**. **A-72 is not yet runnable** — it needs `--lang` and a UI, which arrive at T8 and T9. **Deviations:** (a) **The catalog key is the `<source>` and `en` is a real catalog, not the identity.** Qt's convention makes the English sentence the key; `core/Result.h` forbids user-facing English in `core/` and `ActionId.def` already carries one `action.*` key per action, so there is no English in the sources for `lupdate` to extract. The consequence is that a missing key in `en` is a detectable defect rather than a button rendering `action.file.open`. (b) **The `unwrapped-literal` rule is sink-driven, not literal-driven.** "Every literal" would fire on object names, settings paths, catalog keys and format specifiers, and this stage's own risk register says a gate people waive everywhere is a gate that gets switched off. The sink list is data in `rules.json` and grows with `ui/`; the honest cost is a false negative for a literal handed to a user through a helper it does not name. (c) **`shell.*`, `ribbon.*` and `prefs.*` are NOT seeded.** The plan lists them as Stage 1 catalog groups; they arrive with the code that references them at T9 and T14, because adding them now would be a hundred `orphan-key` notes and a catalog nobody could review. The three `error.*` keys `core/settings/` actually references *are* present. (d) **28 of the 361 zh-TW entries are marked `type="unfinished"`** — the domain-heavy terms (Bates numbering, imposition, redaction, tag artifacts, rights management) where the Taiwanese convention is genuinely uncertain. `--explain` counts them, `missing-key` does not fail on them, and **M-06 is the review** (precondition P-3). Reporting them as finished would be claiming a review that has not happened. (e) **`tools/common/LineScan.h` was extracted** so `layer-lint` and `i18n-lint` share one copy of the code/comment split and the applied-versus-quoted waiver test — both of Stage 0's gate findings, I-15 and I-20, live in that logic, and a second gate re-deriving it would re-derive them. `layer-lint` keeps its own copy for now; the extraction is the shared home, not yet the single one. |
| **T7** `platform/platform.h` + Windows factory | ✅ 2026-08-23 | `platform/platform.h`, `platform/win/{PlatformFactory,WinSingleInstanceBus,WinShellIntegration}.{h,cpp}`, and a new `tests/platform/` target. **`ctest` 194/194 on both presets** (was 173); `checks` clean at **72 files / 363 includes** (the Stage 0 placeholder TU `platform/PlatformModule.cpp` was deleted, as its own comment instructed once the layer had real content); the bare `gates` preset 55/55. **All seventeen interfaces declared**, two implemented, and `unimplementedInterfaces()` names the other fifteen rather than leaving a caller to dereference null. **Deviations:** (a) **The interface count is seventeen, not the plan's fifteen** — see I-10. The figure was a count of §4.3's table ROWS, one of which declares two interfaces, and `IPrinting` had never been written down at all even though `platform/win/WinPrinting` has had a FileMap row since the map was written. `Tech-Stack-Plan.md` §4.3 owns the corrected figure and now carries the `IPrinting` row; `tests/platform` asserts the count against the header. (b) **A third test target, `tests/platform/`.** `tests/unit`'s `rawpage_assert_headless()` forbids linking `rawpage_platform`, deliberately — the first platform test is exactly the pressure D1-4 met from `ui/` by adding a target rather than weakening the existing one. It is Windows-only and never runs on `portable-asan`. The handoff round trip runs **single-process**, which is a real handoff: `CreateMutexW` returns `ERROR_ALREADY_EXISTS` for a name the same process holds, so the second bus takes the genuine secondary branch. What it cannot cover is cross-process foreground activation — that stays manual case M-07. (c) **`platform.h`'s freedom from OS headers is now a gate rule, not a convention** — a `layer-lint` layer entry for that one file, with a fixture proving it fires and a control proving `platform/win/` may still include the OS. A header comment saying "no Win32 here" is review discipline; this is the gate. (d) **The pipe name carries the session id**, because `Local\` scopes mutexes and NOT pipes — the two halves of D1-11 look symmetric and are not. (e) **The fourth `filesystem-write` waiver**, on the named-pipe `CreateFileW` in the bus, taking the standing census from 3 to 4. Waived rather than narrowing the pattern: `CreateFileW` really is how a file gets written on Windows. (f) **A real race was found and fixed by the tests** — see I-11. **A-60, A-61 and M-07 remain outstanding**: they are end-to-end through `rawpage.exe`, which needs T8. |
| **T8** `app/` composition root | ✅ 2026-08-23 — **I-04 closed** | `app/{Services.h,CommandLine,CompositionRoot,Application,SingleInstance}.{h,cpp}`, `app/main.cpp` rewritten to flow A, `i18n/CMakeLists.txt` (the `.ts` → `.qm` step), `tests/unit/tst_commandline.cpp`, and two negative-compile fixtures. **`ctest` 226/226 on both presets** (was 194); the bare `gates` preset **59/59** (was 55); `checks` clean at **100 files / 509 includes**, i18n-lint **45 files / 2 catalogs / 0 waivers**. Flow A verified end to end against the real executable: a second launch forwarded `測試-📄.pdf` to the first over the named pipe and the primary logged the path **byte for byte**, which is I-04's rule proven across three process boundaries rather than asserted. Guide cases run: **A-60 ✅**, **A-63 ✅**, **A-64 ✅**, **A-72 ✅** (380 strings dumped per locale, 377 differ, **0 render a raw key**; the 3 identical are the product name twice and the em dash). **I-04 is closed by three mechanisms, not one:** there is no `argc/argv` overload to call and a compile fixture proves it; `tools/layer-lint` gained a **non-escapable `argv-path-read` rule** allowed only in `app/main.cpp`, with a fixture and an entry-point control; and `tst_commandline` asserts an emoji path survives the parser byte for byte. **Deviations:** (a) **`SingleInstance::claim()` runs AFTER `CompositionRoot::build()`, not before it as flow A draws.** The claim is governed by `general/singleInstance`, so `Settings` has to exist first. A secondary therefore constructs `Settings` before exiting, which reads the file and never writes it. (b) **Two `layer-lint` gaps were found by needing them** — see I-15 and I-16. `ui/` had no permitted path to `platform/platform.h`, whose own header comment has always said it is included by `core/`, `ui/` and `app/`; and no gate scanned `.def` files, though four X-macro tables are `#include`d straight into layers that have deny lists. Both fixed with fixtures and controls. (c) **`app/Services.h` is now `ui/`'s second permitted look outward**, with its own layer entry constraining it to `core/` plus `platform/platform.h` — the `platform.h` treatment, for the reason that a `pdf/` include added there at Stage 2 would otherwise reach every widget. (d) **Two real defects found by running the flow** rather than by reading it: settings were never written to disk (I-14), and they were being written to `%APPDATA%\RawPage\RawPage\` (I-13). (e) The `.qm` catalogs are built beside the executable rather than into a Qt resource, so Stage 15 can ship them replaceable; `LinguistTools` is used as a build tool and deliberately not registered with `rawpage_link_qt`, because `qt-modules.txt` is the LINK closure `license-audit` audits. |
| **T9** `MainWindow`, `StatusBar`, `SidebarDock`, `ThemeManager` | ✅ 2026-08-23 | `ui/shell/{Catalog,ThemeManager,StatusBar,SidebarDock,MainWindow}` plus `ui/shell/ShellKeys.def`, 22 new `shell.*` catalog entries in both shipped locales, and a new `tests/ui/` target. **`rawpage.exe` opens a window.** `ctest` 226/226 on both presets; `checks` clean; **`--dump-ui-strings` under `offscreen` composes every service, loads both catalogs and exits 0 without a window**, which is what keeps a headless smoke test meaningful now that an ordinary launch runs an event loop. **Deviations:** (a) **`tests/ui/` arrives here rather than at the plan's T15**, on the precedent T7 set with `tests/platform`: four widget files shipping with no suite until six tasks later would make "code complete AND `ctest` green" vacuous for all six. T15 still owns adding it to the CI `windows` job. (b) **Auto-hide is first-party.** §4.3 lists docking among what Qt gives us and is right — dock, float and re-dock are `QDockWidget` and are not reimplemented — but Qt has no notion of a pane collapsing to an edge strip, so that part is ours. It is a second piece of named custom work in the same family as A3's collapse layout. (c) **A crash found by `tests/ui` on its first run** (I-12), of exactly the shape T7's pipe race had: a lambda bound to a child's signal outliving the derived object. Fixed, and **verified by negative control** — with the `disconnect` commented out, that one case segfaults and no other. (d) **Chord input is not wired**, by plan: `ShortcutBinder` is T13. The three registered actions are reachable through the registry — and therefore through a test and the batch runner — and through the pane chrome, but not yet from the keyboard. (e) **Three actions registered, not the plan's 60–80.** That figure is Stage 1's total across T9–T14; registering a stub handler for an id this window cannot service would make every one of them look available, which is precisely the state §5.1 says to avoid. |
| **T10** `Ribbon`, `RibbonTabBuilder`, `ActionFactory`, `res/ribbon.json` | ✅ 2026-08-24 | `ui/shell/{RibbonModel,RawPageAction,ActionFactory,Ribbon,RibbonTabBuilder}`, `res/ribbon.json` with `res/CMakeLists.txt`, the new `tools/ribbon-validate/`, `layer-lint`'s non-escapable `bare-qaction` rule with **six** gate cases, `tests/ui/{tst_ribbonbuild,tst_actionwiring}.cpp`, 61 new `ribbon.*` catalog entries in both shipped locales, and **`tools/spike-ribbon/` deleted** exactly as D1-1 said it would be. `ctest` **250/250** on both presets (was 226); the bare `gates` preset **65/65** (was 59); `checks` clean at **109 files / 569 includes**, i18n-lint **55 files / 0 waivers**. The ribbon is 9 tabs, 49 groups and **192 controls**, all of it from the resource: `RibbonTabBuilder` makes no decision the JSON could have expressed, and `tst_ribbonbuild` asserts both directions — everything declared is present **and no control exists that was not declared**, which is the half that actually protects D1-8. Guide cases run: **A-12 ✅**, **A-50 ✅**, **A-51 ✅**, **A-54 ✅**. A-51 was verified by planting `"file.saevAs"` and building: the build stops with `res/ribbon.json:44: unknown action id` and the resource is **not** copied, so no stale valid file can mask it. **Deviations:** (a) **`ui/shell/RibbonModel.{h,cpp}` is a file the plan's §4 manifest does not list.** D1-8 needs the resource validated before the link and built at startup, which is two consumers of one schema — and a validator with its own parser can certify a file the runtime reads differently, which is worse than no validator. So the parser is widget-free and `tools/ribbon-validate` **compiles that translation unit in**, the technique `tests/unit` already uses for `app/CommandLine.cpp`. (b) **`tools/ribbon-validate/` is a new tool and is deliberately NOT a fourth gate.** `checks` runs three and says three; this is a build step `rawpage_ui` depends on. Wiring it into `checks` would have been the smaller CMake diff, would have made the gate count a lie, and would have put a Qt dependency into the job whose entire value is not having one. (c) **No icons, and no `res/rawpage.qrc`.** The manifest lists both at T10. There are no icon assets and inventing 358 of them is not work this task can do honestly, so a large button is a tall text button and `ActionRegistry`'s `iconName` field stays unread. Recorded rather than faked. (d) **`res/ribbon.json` ships beside the executable rather than in a Qt resource**, the same call T6 made for the `.qm` catalogs and for the same reason — Stage 15 may ship it replaceable. The build-time validation is what makes that safe, and the runtime parse is the same parser, so an edited copy fails with the same diagnostics rather than yielding a quietly thinner ribbon. (e) **One extra construction site inside the allowed file.** A `QMenu`'s items are `QAction`s and Qt gives no choice; the overflow chevron needs some. `ActionFactory::addNonCommandMenuItem` builds them where every other action is built and stamps them, rather than carving `Ribbon.cpp` out of the lint — the rule keeps exactly one allowed path and stays non-escapable. (f) **A fourth registered action**, `view.toggleRibbon`, so R-7's behaviour is reachable from the ribbon, from a test and from the Stage 14 batch runner before T13 wires `F9`. (g) **Three defects found by the new suite's first run against a real, shown window** — I-18, I-19 and I-20 — and **two of the three were the check being wrong rather than the product**. I-19 is the one that mattered: the ribbon was holding the whole main window open at 1044 px, and behind that, the chevron overflow the A3 verdict counts as named custom work was **unreachable in the shipped configuration** while its code looked correct. |
| **T11** `QuickAccessToolbar`, `UiCustomization`, `ToolFinder` | ✅ 2026-08-24 — **I-19 closed** | `ui/shell/{UiCustomization,QuickAccessToolbar,ToolFinder}.{h,cpp}`, two new settings keys (`ui/qatCommands`, `ui/qatBelowRibbon`), 6 new `shell.*` keys and one `error.*` key in both shipped locales, and `tests/ui/{tst_qat,tst_toolfinder}.cpp`. `ctest` **264/264** on both presets (was 250); the bare `gates` preset unchanged at 65/65; `checks` clean at **117 files / 659 includes**, i18n-lint 61 files / 0 waivers. Guide cases run: **A-55 ✅**, **A-56 ✅**. **I-19's open half is paid rather than carried again:** the ribbon body sits in a `QScrollArea`, so `RibbonPageLayout::minimumSize()` returns the honest content minimum — the sum of the collapsed groups — without that number reaching the main window. Collapse is still exhausted first; the row scrolls only below the width every group collapsed needs. **Deviations:** (a) **The QAT and the tool finder are hosted by a header COLUMN in the menu-widget slot**, not by the ribbon. The toolbar has to be above or below the ribbon and inside the one slot `QMainWindow` puts above the dock areas, or a docked pane can land between them; `Ribbon` gained only a corner-widget slot and a `controlContextMenuRequested` signal, and knows nothing about either. (b) **`ui.qatAdd` and `ui.qatRemove` are the product's first parameterized commands, and `ActionRegistry::dispatch` has no argument.** The Stage 1 answer is `QuickAccessToolbar::pendingTarget()` — the surface that raised the menu records what was under the cursor and the handler reads it, which keeps both reachable from a test and from the batch runner. It is not a general answer; recorded as **O11** in §8. (c) **`ActionFactory` had never called `setCheckable`**, so its header's promise that "a toggle is checkable and only a toggle is" had gone untested for want of a toggle. The QAT's placement item is the first one; fixed, and `ActionRegistry` gained `isToggle()` and `tooltipKey()` because the factory needed both (**I-21**). (d) **`ActionFactory::made_` became `QPointer`s.** The toolbar rebuilds its buttons on every add, remove and reorder and the actions are parented to the buttons, so entries in that list are destroyed as a matter of routine — raw pointers made `refresh()` a use-after-free waiting for the first reorder. (e) **`Settings::isSet()` was added**: a toolbar the user emptied and a profile that never customized one both read back as an empty list, and only one of them means "restore the factory defaults". (f) **Two catalog additions were needed for one command**, not one: `ui.qatPlacement`'s label is a noun and cannot say which way the toggle points, so the check mark says it and `shell.qat.placementHint` says what the check mark means. (g) **A third kind of Qt-manufactured action** was found by the wiring self-check on its first run against the tool finder: `QLineEdit::setClearButtonEnabled(true)` builds one, and unlike `QMenu::menuAction()` it is NOT in `QWidget::actions()`, so it cannot be recognised by identity. `ActionFactory::adoptWidgetActions()` lets the widget's owner stamp it at the moment the property is turned on (**I-22**). |
| **T12** `DocumentTabBar`, stub `DocumentWindow` | ✅ 2026-08-24 — **A-58 closed** | `ui/shell/{DocumentWindow,DocumentTabBar}.{h,cpp}`, `MainWindow`'s central column rewritten around a tab strip and a two-page body, 6 more `shell.*` keys in both locales, and `tests/ui/tst_documenttabs.cpp`. `ctest` **275/275** on both presets (was 264); the bare `gates` preset 65/65; `checks` clean at **122 files / 697 includes**, i18n-lint 65 files / 0 waivers. Guide cases run: **A-57 ✅**, **A-58 ✅** (its second half was not runnable before this task). **Seventeen commands are now registered**, up from nine: `file.open` (a dialog), `file.close`, `file.closeAll`, `window.{nextTab,prevTab,next,prev,new}`. **Deviations:** (a) **The command handlers stopped capturing `this`, and registration became once-per-registry.** Tearing a tab out makes a second `MainWindow` while `ActionRegistry` stays one shared object whose registration is deliberately not idempotent — so a second window would have collected one duplicate-registration error per command, and the first window's handlers would have gone on acting for every window in the application, including after that window was destroyed. Each handler now resolves `MainWindow::activeShell()` at dispatch time. This is a structural change T12 forced rather than a refactor T12 chose. (b) **`raiseToForeground()` sets the active shell directly** rather than waiting for the `ActivationChange` that `activateWindow()` normally causes: measured under `offscreen`, that event never arrives, and a Global command dispatched into the window nobody had raised. (c) **`file.open` is registered with an enablement predicate that is false where there is no user.** A modal file picker under `offscreen` is shown to nobody and never returns — a ten-minute CI hang the first time a test dispatched it (**I-23**). As a disabled command it is refused with `Unavailable`, which a headless caller can tell apart from `NotFound`; the parameterized form of "open this file" is `openFiles()`, which flow B and the CLI already use. (d) **The document strip uses `QTabBar`'s own overflow** where the ribbon replaced it with a chevron: a ribbon has a small fixed tab set, a document strip has an unbounded user-driven one, and copying the ribbon here would be consistency with the wrong thing. (e) **`DocumentWindow` holds a path and nothing else**, and A-57's "no member Stage 2 would have to remove" is a claim about the header — the test asserts what it can (the meta-object gains no property or method, and everything reported is derived from the path) and says so rather than implying more. **(f) Two defects shipped in this task and were found six days later by the first person to walk M-03 — `I-33` and `I-34`, fixed 2026-08-30.** Recorded against T12 rather than against the fix date, because that is where they were introduced. I-34 is the instructive one: the drop-onto-another-window branch was correct, and `tst_documenttabs.cpp` asserted the destination by calling `adoptDocument` directly while a comment recorded the gesture as manual-only — so a branch no test and no person had ever driven read as covered. It could not be reached by hand at all. The lesson is narrower than *"test the gestures"*: **a case that reaches a behaviour through a back door should say what it therefore does not cover**, because the next reader counts it as coverage. |
| **T13** `ShortcutBinder` + full chord wiring | ✅ 2026-08-24 — **A-53 closed** | `ui/shell/ShortcutBinder.{h,cpp}` and `tests/ui/tst_context.cpp`. `ctest` **297/297** on both presets (was 275); the bare `gates` preset 65/65; `checks` clean at **125 files / 735 includes**, i18n-lint 67 files / 0 waivers. **No new catalog keys and no new settings keys** — T13 adds a way to reach commands, not commands. Guide case **A-53 ✅**, and the T13 exit criterion is asserted over the table rather than over a handful of chords: every dispatched row in §8's Global table is registered with a recording handler, pressed, and checked to have reached its own action; a second sweep does the same for every context's own rows, which is the resolution ORDER asserted rather than described. **Deviations, and each is a decision the plan left open rather than a departure from it:** (a) **Chords are not `QAction` shortcuts, and that is the whole design.** Qt's shortcut system has no way to say *innermost context, then `Common`, then `Global`* — the order `KeyBindingStore`'s header calls the model — and it would have put the user's remap into 358 actions as a shadow copy of the store. So the binder filters key events and resolves through the store, which is also what makes a remap take effect with no widget rebuilt (asserted). (b) **The focused widget gets first refusal through Qt's own `ShortcutOverride` handshake** rather than through a list of widgets that edit text. `Ctrl+A` is `edit.selectAll` in §8's Common table AND what a `QLineEdit` does with its own text; a list would be wrong the first time somebody added a widget, and Qt already asks this question of every widget in its own shortcut path. (c) **Only two of five outcomes consume the key.** A dispatch and an `Unavailable` refusal consume; a `NotFound` does not, because Stage 1 registers seventeen of 358 ids and eating the other chords would make the shell swallow `Ctrl+S` for twelve stages. (d) **One binder per window, installed on `qApp` and answering only for its own window.** A key event reaches the focus widget first, so a filter on the window would never see a chord typed into a text field; but two windows filtering the application would dispatch one chord twice, and the fix is the window test rather than a single global binder, because the context must be computed per window. (e) **Nothing in the Stage 1 shell declares a `KeyContext`.** The Viewer surface is Stage 2, the panes Stage 4, Write mode Stage 13, so `activeContext()` answers `Global` throughout — the honest answer, and the one the T13 exit criterion is written against. Declaring `Viewer` on D1-2's stub to make the mechanism look exercised would have been a stub claiming to be a page surface. (f) **`unreachableBindings()` is a census the plan did not ask for**, and it is the case that could have found a dead table: a chord whose key the conversion cannot name looks identical to a working one everywhere else — the store holds it, the census counts it, the tool finder prints it, and only the keyboard disagrees. All 248 rows round-trip. (g) **A second self-check, in the shape of D1-7(b).** An action carrying its own key sequence, or a free-standing `QShortcut`, is a chord that bypassed the context stack and the remap on its way in; `MainWindow::showEvent` now runs that check beside the wiring one, asserting in debug and logging in release, and both kinds are planted in the suite. |
| **T14** `LogPane`, `PreferencesDialog` (shell + General + Shortcuts) | ✅ 2026-08-25 — **I-26, I-27, I-28** | `ui/dialogs/{PreferencesDialog,SettingsPage,ShortcutsPage}.{h,cpp}`, `ui/panes/LogPane.{h,cpp}`, `core/LogBuffer.{h,cpp}`, `app/LogFile.{h,cpp}`, 62 new `prefs.*` catalog entries in both shipped locales, and `tests/{unit/tst_logbuffer,ui/tst_preferences,ui/tst_logpane}.cpp`. `ctest` **328/328** on both presets (was 297); the bare `gates` preset **66/66** (was 65); `checks` clean at **140 files / 911 includes**, i18n-lint 79 files / 2 catalogs / 0 waivers, **6** live filesystem-write waivers (was 4). Guide cases run: **A-90 ✅**, **A-91 ✅**, **A-92 ✅**, **A-93 ✅**, **A-94 ✅** — a new §5.10 in the guide. `--dump-ui-strings` now emits **515** keys per locale (was 380), **0** of which render as their own key, and 510 differ between `en` and `zh-TW`. **Nineteen of 358 commands are registered** (was seventeen): `file.preferences` and `pane.log`. **Deviations, and each is a decision the plan left open rather than a departure from it:** **(a) The pages are generated by `#include`ing `SettingsKeys.def` into `SettingsPage.cpp`, not by walking `SettingsSchema::allKeys()`.** The runtime descriptor table would have been the obvious iteration and it cannot be: `core::Settings` has no string-keyed `get`/`set` by D1-10, and adding one for the Preferences dialog's convenience would make *“an undeclared key does not compile”* decorative for the whole product. So each row instantiates a template holding the real `constexpr Key<T>`. `SettingsSchema` still owns *which widget* and *what to call it*, through three artifacts added here — an editor kind, a choice set, and a label key derived from the path — all derived from the row's type and path rather than added as columns anybody maintains. **(b) A third page ships, Logging, and it reconciles two things this plan says.** The T14 row names two pages; §11 says *“three pages here”*. Logging is the third because T14 also ships the log pane, `logging/level` and `logging/subsystems` decide what that pane can ever show, and `logging/retainFiles` had been a declared key with no surface since T5. A generated page costs a row in `shippedPages()`. **(c) The dialog is MODELESS and applies immediately — Close and Restore Defaults, no OK/Cancel/Apply.** A `Cancel` that must un-apply needs a transaction `core::Settings` deliberately does not have (it loads once and writes on `sync()`, because D1-16's downgrade rule is not expressible through a live `QSettings`), and a `Cancel` that only stopped future writes while the theme already changed is a button that lies. Modeless is also what makes `file.preferences` need **no** enablement guard — a better answer than I-23's `canPromptTheUser`, available here only because the dialog had no reason to be modal. **(d) `core/LogBuffer` and `app/LogFile` are two files the §4 manifest does not list.** `core/Log.h` has ONE sink and `setLogSink` **replaces** it, so a pane that chained it would be dropped the next time `applyLoggingSettings()` ran — which is what T14's own Logging page does. `CompositionRoot` now installs one sink that **fans out** to the console, the file and the buffer. The file is `app/`'s because `core/` may not write to disk, and `main.cpp` has promised it to T14 since T1. **(e) `log.openFolder` is declared and NOT registered.** The folder belongs to `app/LogFile` and `ui/` looks outward to exactly two files; including a third to open a directory would widen that list for a convenience, and widening it is an architectural decision rather than a diff (I-15). **(f) Three defects, and all three were found by running something new against the product** — **I-26** (a remap *added* a chord instead of replacing it, found by the first caller that ever remapped for a user), **I-27** (`orphan-key` about to report forty generated keys nobody could act on) and **I-28** (a log file that could not name the binary that wrote it, found by reading the file the executable actually wrote). **(g) Convention 2 caught a new test name in this session**: a case named with a `§` registered with CTest and then matched nothing, exactly as I-07 says. Renamed; no new issue, because the convention did its job. A second self-inflicted one is recorded in A-92's result: a case asserted the shadow census as **6** and got **7**, because six is a count of *chords* and seven of *entries* — the same conflation as I-01, now asserted both ways. |
| **T15** test target, CI, verdicts, doc lockstep | ✅ 2026-08-25 — **I-29, I-30**; **A-80 closed, and every automated case with it** | `tests/bench/` (new, one benchmark), `tests/unit/tst_singleinstance.cpp`, `scripts/check-single-instance.ps1`, four `.github/workflows/ci.yml` changes, and the §4 manifest's last `Architecture-FileMap.md` rows. `ctest` **334/334** on both presets (was 328); the bare `gates` preset 66/66; `checks` clean at **141 files / 930 includes**, i18n-lint 78 files / 2 catalogs / 0 waivers. **No new catalog keys, no new settings keys and no new registered commands** — T15 builds no product surface, and that is what the task is. Guide cases run: **A-61 ✅**, **A-80 ✅**, **A-81 ✅**, **A-82 ✅**, **A-83 ✅**, **A-84 ✅**, **A-85 ✅** — **every automated case in the guide**. **CI is green on all four jobs** — run 32785493072, and **the `windows` job ran for the first time in the project's history**: 334/334 there, `gates` 52/52, `portable-asan` 196/196. A-85's figure on the runner (TOTAL p95 **0.476 ms**) is within 1 % of this machine's 0.471 ms, which is what a stable measurement across two boxes looks like. **`core/CoreModule.cpp` deleted**, as its own comment instructed and as §10 had carried since T7. **Deviations, and each is a decision the plan left open rather than a departure from it:** **(a) `tests/bench/` arrives a stage early, and A-85 is why.** `Architecture-FileMap.md` schedules the directory for Stage 2's per-tile render latency (risk A2), but R-9 — a tab switch inside one 60 Hz frame — is the only one of A3's eight criteria that is a number rather than a judgement, and `Stage1-AppShell-Plan.md` §5.3 names `tests/bench` as how it is judged. Stage 1 cannot record its A3 verdict complete without it. A bench target's exit code is deliberately about whether the measurement was *possible*, never about whether the machine was fast: a loaded runner must not turn a green build red for a reason unrelated to the change. **The figure is p95 = 0.471 ms against 16.667 ms**, reported in three phases because one opaque total cannot be acted on. **(b) A-61 is split, and the split was forced by a measurement.** Qt resolves `QStandardPaths::AppDataLocation` through `SHGetKnownFolderPath`, so overriding `APPDATA` for a child process does **not** redirect the settings file — verified rather than assumed — and `main.cpp` exposes no switch for `CompositionRoot::Options::settingsPath`. Adding a CLI verb at T15 would extend the vocabulary `Features.md` §9.6 owns for Stage 14. So the *policy* half is a CTest case and the *two-process* half is `scripts/check-single-instance.ps1`, which edits the real settings file and restores it in a `finally` — precisely why it must not be a suite anybody runs casually. Both halves ship with a control, because a `false` case with no `true` case passes against a build whose single-instance code was deleted outright. **(c) `app/SingleInstance.cpp` became the third `app/` translation unit compiled into the headless suite**, not linked. It names `platform/platform.h`, and the tempting repair was one `target_link_libraries` line — which is the exact pressure D1-4 exists to resist, applied for real rather than described. `platform.h` is an interface header that includes no OS header (its own `layer-lint` entry), so `rawpage_assert_headless()` still resolves to `core/` + `pdf/` and `portable-asan` runs the new cases where neither forbidden library exists. **(d) "`tests/ui/` into the CI `windows` job" turned out to be an assertion rather than a step.** `ctest --preset x64-release` already ran the widget suite; what no job could do was notice its **absence** — a target that failed to build subtracts cases from a run that still reports *"100% tests passed"*. All three jobs now count what they registered, in both directions. The first version of the absence check returned 0 against a listing containing 124 such cases and would have passed on a Linux runner with the whole suite leaked onto it; it was shown failing against real positive input before it was trusted (convention 7). **(e) Two findings, and both are the harness and its infrastructure rather than the product** — which is now the fourth consecutive task where that is true. **I-29**: every figure the new benchmark printed on its first run was 0.000 ms, because three percentile calls were arguments to one `printf` and argument evaluation order is unspecified. Caught by disbelieving the result, not by anything failing. **I-30** is the S2: **the `windows` CI job had never run, and it was never a missing runner.** One has been online since 2026-08-17. The repository variable was `RECTO_HAS_WINDOWS_RUNNER`; the workflow reads `RAWPAGE_HAS_WINDOWS_RUNNER`. Two artefacts in different systems had to agree and nothing checked that they did, so for a week three-quarters of the product's test surface had no CI while the run page said nothing was wrong. The variable was set with the user's authorization and the next push produced the first `windows` run the project has ever had — green, at 334/334. The structural fix is a fourth job, `coverage-report`, which states in every run summary which jobs ran and — when the Windows one did not — enumerates exactly what went unexercised. It does not fail, because a job that goes red for missing infrastructure teaches everyone to ignore a red build. |

> **A convention T9 adds to it: a Catch2 test name may not contain a comma either.** Catch2 splits a filter on commas, so a case named *"…round trip, and peeking…"* cannot be selected by hand even though CTest escapes it correctly when it runs the suite. The same family as I-07 and I-04 — a name crossing a narrow, punctuation-sensitive process boundary — and the same remedy: keep the name plain and put the detail in a comment.
>
> **A convention T3 established the hard way: Catch2 test names are ASCII.** The `KeyContext` case was
> first named *"…matches Features.md §8"* and failed — not in its body, but because
> `catch_discover_tests` registers the name with CTest, CTest passes it back as a command-line filter,
> and `§` does not survive that round trip (it returned as `Features.md ??8`). Catch2 then matched no
> test and CTest reported a failure that looked like a broken assertion. This is **I-04's root cause in
> a second place** — a narrow, code-page-bound process boundary — and the machine's ACP of 950 is again
> what made it visible. Section references belong in comments, not in test names.

#### Verdict: `-municode` — **not adopted** (resolves Stage 0 deviation (a), decision **D1-3**)

Measured 2026-08-23 with `rawpage.exe --print-args`, from PowerShell **and** `cmd.exe`, against a path
containing CJK characters and one containing an emoji. The machine's **system ANSI code page is 950**
(Big5), which is what makes the result decisive rather than vacuous — a UTF-8 ACP would have hidden it:

| List | CJK path | Emoji path |
|---|---|---|
| `argv` (narrow, from the CRT) | survives — Big5 can represent it | **`report-?????.pdf`** — lost, lossily and irreversibly |
| `QCoreApplication::arguments()` | byte-for-byte correct | **byte-for-byte correct** |

Both shells give the same answer. Qt sources `arguments()` from `GetCommandLineW()`, not from `argv`,
so the problem `-municode` exists to solve is already solved for our only consumer, and the flag is
**not adopted** — no `wWinMain`, no `Qt6::EntryPointPrivate`, a plain `int main(int, char**)`.

**The standing rule this creates, which is the real deliverable:** *nothing in RawPage may read a path
out of `argv`.* `main()` hands `argv` to `QApplication` (which ignores it for `arguments()`) and reads
nothing else from it. `app/CommandLine` at **T8** must parse `QCoreApplication::arguments()` and
nothing else — the emoji row above is what a violation costs, and it is silent.

#### Verdict: **A3** — ✅ `go, with named custom work`

**Recorded 2026-08-23.** Qt Widgets host a real ribbon. The middle rung of the
`Stage1-AppShell-Plan.md` §5.3 scale is a **pass**, and the fallback — a first-party ribbon widget —
is not in play. The named work is a collapse `QLayout` (~60 lines), a chevron tab overflow (~30), and
a tab-order pass (~15); all three are ordinary Qt.

**Stated plainly because it would be easy to hide:** the verdict rests on **eight of the nine
criteria**. R-8 was **withdrawn, not cleared**, when accessibility was removed from the product the
same day — so the ninth rung was deleted rather than met. Full per-criterion table in
[`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified).

**Objectives.** A window with a working ribbon, document tabs, a sidebar, and a status bar, where
every command already flows through the action layer, one typed settings store exists, the UI is
translatable, and a second launch hands its file to the first instance.

**Components & responsibilities.**
- `app/main.cpp` — DPI awareness before any window; crash handler; CLI parse; hand off.
- `app/CompositionRoot` — `Services build(Settings&, Platform&, IDocumentBackend&)`.
- `app/Application` — `void loadTranslators(const QLocale&)`, `void restoreSession()`.
- `app/CommandLine` — `Result<Intent> parse(QStringList)` (open-with-file only this stage).
- `app/CrashHandler` — `void install()`, `void writeMinidump(EXCEPTION_POINTERS*)`.
- `app/SingleInstance` — `bool claim()`, `void forwardTo(primary, QStringList files)`.
- `core/cmd/ActionId.h`, `core/cmd/ActionRegistry` — `void register(ActionId, Handler, Enable, Label)`,
  `bool dispatch(ActionId, KeyContext)`, `bool isEnabled(ActionId) const`.
- `core/cmd/KeyContext.h`, `core/cmd/KeyBindingStore` —
  `std::optional<ActionId> lookup(const Chord&, KeyContext) const`,
  `Result<void> rebind(const Chord&, KeyContext, ActionId)`, `std::vector<Shadow> shadows() const`.
  **Corrected at T4:** this list originally said `QKeySequence`, which lives in QtGui and `core/`
  may not include. `core/cmd/Chord` is the first-party equivalent; `ui/shell/ShortcutBinder`
  converts at the boundary.
- `core/settings/Settings`, `SettingsSchema` — `template<class T> T get(Key) const`,
  `void set(Key, T)`, `Signal<Key> changed`.
- `core/settings/RecentFiles` — `void note(DocumentId)`, `void pin(DocumentId)`, `void clear()`.
- `ui/shell/MainWindow`, `Ribbon`, `RibbonTabBuilder` (`void build(Ribbon&, const QJsonDocument&, ActionRegistry&)`),
  `QuickAccessToolbar`, `UiCustomization`, `ShortcutBinder` (`KeyContext activeContext() const`),
  `DocumentTabBar`, `DocumentWindow`, `SidebarDock`, `StatusBar`, `ToolFinder`, `ThemeManager`.
- `ui/panes/LogPane`, `ui/dialogs/PreferencesDialog` (shell + General + Shortcuts pages).
- `platform/platform.h` (all **seventeen** interfaces **declared** — the count was written as
  fifteen and corrected by measurement at T7), `platform/win/PlatformFactory`,
  `WinSingleInstanceBus`, `WinShellIntegration` (file association only).
- `tools/i18n-lint/`, `i18n/rawpage_en.ts`, `rawpage_zh_TW.ts`.

**Key bindings handled** — from [`Features.md` §8 → Global](./Features.md#global-active-in-every-context)
(`KeyContext::Global`): `Ctrl+O`, `Ctrl+W`, `Ctrl+Q`, `Ctrl+Tab` / `Ctrl+Shift+Tab`,
`Ctrl+F6` / `Ctrl+Shift+F6`, `F4`, `F9`, `Alt` (key tips), `Ctrl+Shift+U`, `Ctrl+K`, `F1`.
Also `Ctrl+Z` / `Ctrl+Y` from [Common](./Features.md#common-all-contexts-unless-a-context-above-overrides-the-chord)
— **registered and dispatching, but with an empty journal until Stage 3.**

**Data / settings keys.** `general/theme`, `general/uiFontScale`, `general/touchMode`,
`general/restoreSession`, `general/singleInstance`, `language/uiLocale`,
`shortcuts/<context>/<action>`, `logging/level`, `logging/subsystems`, `logging/retainFiles`,
`documents/recentCount`, `documents/recentPinned`.
**Four more are declared than this list names, and each is recorded where it was added:** `meta/schemaVersion`
and `documents/recentList` at T5 (deviation (b) — internal state, not preferences), and `ui/qatCommands`
and `ui/qatBelowRibbon` at T11. All four are in the `Internal` group and get no Preferences row: they
are state and customization rather than settings a user goes looking for in a dialog.

**Cross-cutting additions.** No commands yet. **i18n catalog groups introduced:** `shell.*`,
`ribbon.*`, `error.*`, `prefs.*`, `action.*` (one label per `ActionId`). **Live at T14: 536 keys per
shipped locale**, of which 62 are `prefs.*`. Three of those families are **generated rather than
written down** — `prefs.setting.<group>.<key>` from `SettingsKeys.def`, `prefs.choice.<enum>.<member>`
from the enum's own name table, and `prefs.context.<Name>` from `KeyContext` — so the whole key
appears in no source and only its namespace does. `tools/i18n-lint` learned to read a reference
ending in a dot as a namespace at T14 (**I-27**), which is what keeps forty unactionable `orphan-key`
notes off every run, and `Catalog::dumpStrings()` walks those tables so A-72 still covers all of them.

**Algorithms & notes.**
- `ShortcutBinder::activeContext()` is the keystone of the whole binding model: it walks the focus
  chain, takes the innermost widget that declares a context, and falls back to `Global`. It is a
  **pure function of the focus chain** and is unit-tested with synthetic chains — no window needed.
- `RibbonTabBuilder` reading a declarative resource means adding a command in a later stage is a
  registry entry plus a JSON line, never widget code. This is what keeps sixteen more stages from
  each growing their own toolbar plumbing.
- `KeyBindingStore` **reports** rather than forbids a remap that shadows a Global chord in one
  context, because [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference) already
  ships twelve deliberate collisions — forbidding shadowing would make the specified defaults
  illegal.

**Testing.**
*Automated:* `tst_actionregistry` (register, dispatch, enablement, unknown id);
`tst_keybindings` (§8 defaults load exactly; the twelve documented collisions each resolve to the
right `ActionId` per context; remap and persist; shadow detection);
`tst_settings` (typed round-trip, defaults, undeclared-key rejection, `changed` signal);
`tst_context` (`activeContext()` over synthetic focus chains); `tst_recentfiles`;
`tst_commandline`. `i18n-lint` green with `zh-TW` complete.
*Manual:* → queued **`Stage1-AppShell-Testing-Guide.md`**, written before this stage's code (§5). In brief:
ribbon renders and collapses; a QAT customization survives restart; tear a tab out and re-dock it;
panes dock, float, and auto-hide; the theme follows the system; `--lang zh-TW` translates the UI;
double-clicking a second PDF reuses the running instance; the tool finder finds and runs a command.

**Exit criteria.** The window opens with a functional ribbon; **every** ribbon button, menu item,
and chord dispatches through `ActionRegistry` (verified by a debug assertion that no `QAction`
carries a direct connection); the §8 Global defaults all resolve correctly; settings round-trip;
`--lang zh-TW` shows a translated UI; a second launch reuses the first instance; risk **A3** has a
recorded verdict.

**Deferrals.** No document can be opened yet — `Ctrl+O` shows a dialog and reports "not yet
implemented". Contextual ribbon tabs, per-document pane state, the full Preferences page set, and
`WinShellIntegration`'s thumbnail/preview/index handlers all come later.

---

### Stage 2 — PDF engine boundary and tiled rendering

*Part 2 of the first milestone.*

**Status:** ⚪ **Not started.** Carries risk **A2** as a hard **go/no-go**: p95 per-tile latency
must be under ~16 ms at the target DPI on the reference machine. If it is not, the tiling policy in
[`Tech-Stack-Plan.md` D10](./Tech-Stack-Plan.md#2-decisions-confirmed) is revisited **before**
anything is built on top of it.

**Objectives.** Open a PDF and look at it: scroll, zoom, all four page-layout modes, correct
rasterization at each monitor's physical DPI, and a window that survives being dragged between
monitors at different scaling.

**Components & responsibilities.**
- `core/backend.h` — `IDocumentBackend`, `IPageRasterizer`, `ITextExtractor`, `MutationSink`,
  `ISigner`, `IOcrEngine`, `IIndexer` all **declared** now, even where unimplemented, so later
  stages add no boundary.
- `core/render/DpiPolicy` — `Resolution resolve(Zoom, MonitorScale, PhysicalDpi) const`,
  `bool requiresRerender(Resolution, Resolution) const`.
- `core/render/TileGrid` — `TileSet tilesFor(const Page&, Resolution, QRectF viewport) const`.
- `core/render/RenderRequest.h` — the value type whose equality **is** the cache key.
- `core/render/RenderScheduler` — `Handle enqueue(RenderRequest, Priority)`,
  `void cancel(Handle)`, `Signal<RenderRequest, QImage> ready`.
- `core/render/RenderCache` — `std::optional<QImage> lookup(const RenderRequest&) const`,
  `void put(...)`, `void invalidate(Scope, Revision)`.
- `core/render/PagePresentation` — `QVector<QRectF> layout(const PageTree&, LayoutMode, Zoom) const`.
- `core/doc/RepairReport`.
- `pdf/cos/` first half — `CosObject`, `Lexer`, `Parser`, `XrefTable`, `XrefRepair`,
  `filters/{Flate,LZW,Ascii85,AsciiHex,RunLength,Dct,Jpx,Ccitt,Jbig2}`.
- `pdf/render/PdfiumInit`, `PdfiumDocument` (`Result<Handle> open(path, PasswordCb)`),
  `PdfiumRasterizer` (`Result<QImage> raster(const RenderRequest&)`), `GlyphCache`,
  `FontResolverBridge`.
- `pdf/DocumentBackendImpl`, `pdf/BackendFactory` (`std::unique_ptr<IDocumentBackend> createBackend()`).
- `ui/viewer/PageView`, `TilePresenter`, `PageLayoutEngine`, `ViewportController`.
- `ui/shell/MessageBar`.
- `platform/win/WinDisplayTopology` (`QVector<MonitorInfo> monitors() const`, `Signal<> changed`),
  `WinAcceleratedSurface`, `WinFontResolver`, `WinPowerSession`.
- `tests/golden/`, `tests/bench/`, `corpus/` (including `corpus/malformed/`), `tools/golden-update/`.

**Key bindings handled** — from [`Features.md` §8 → Viewer](./Features.md#viewer-review-mode)
(`KeyContext::Viewer`): `Ctrl+O` now functional; `Ctrl+=` / `Ctrl+-`, `Ctrl+0`, `Ctrl+1`, `Ctrl+2`,
`Ctrl+3`, `Ctrl+M`, `Ctrl+Shift+N`, arrow keys, `Page Down`/`Page Up`, `Space`/`Shift+Space`,
`Ctrl+Page Down`/`Ctrl+Page Up`, `Home`/`End`, `Alt+←`/`Alt+→`, `Ctrl+Shift+=`/`Ctrl+Shift+-`
(rotate **view**), `H`, `V`, `Z`, `Ctrl+Shift+H`, `Ctrl+Shift+T`.

**Data / settings keys.** `pageDisplay/layoutMode`, `pageDisplay/defaultZoom`,
`pageDisplay/showCoverInTwoUp`, `pageDisplay/rtlBinding`, `pageDisplay/lineWeight`,
`pageDisplay/antialiasText`, `pageDisplay/antialiasImages`, `pageDisplay/antialiasLineArt`,
`pageDisplay/transparencyGrid`, `pageDisplay/maxRenderResolution`, `pageDisplay/wheelZooms`,
`performance/renderWorkers`, `performance/tileCacheMB`, `performance/diskCacheMB`,
`performance/prefetchPages`, `performance/throttleOnBattery`.

**Cross-cutting additions.** None (no mutation yet — deliberately: read and write are proven
separately so a failure is unambiguous).

**Algorithms & notes.**
- **Tile invalidation is `Revision`-and-`Scope`-keyed, not time-keyed.** `RenderRequest` carries the
  `Revision`, so a stale tile can never be shown after a mutation lands in Stage 3.
- **Cancellation on scroll matters more than raw throughput.** A tile that scrolled out of view must
  be dropped from the queue, not rendered and discarded; the perceived-smoothness win is larger than
  any per-tile optimization.
- **`XrefRepair` runs a full-file object scan** when the xref is unusable, then rebuilds the table
  and reports every reconstruction. A file we repaired is flagged so Stage 3 can force its first
  save to Save-As — appending to a file whose structure we guessed is how originals get destroyed.
- **`DpiPolicy` distinguishes a scale change needing re-rasterization from one needing a resample.**
  Getting this wrong yields either blurry text or a stutter on every monitor move.
- PDFium is **thread-confined per worker**; `PdfiumInit` is the only place its global state is
  touched.

**Testing.**
*Automated:* `tst_dpipolicy`, `tst_tilegrid`, `tst_rendercache` (LRU, cap, scope invalidation),
`tst_presentation` (all four layout modes, cover page, RTL), `tst_cos_lexer`, `tst_cos_parser`,
`tst_cos_filters` (each filter round-trips), `tst_xrefrepair` (every `corpus/malformed/` file yields
a report, never a crash). `tests/golden/` establishes the baseline at three DPIs. `tests/bench/`
reports p50/p95 — **the A2 gate**. `portable-asan` runs the COS parse of the whole corpus clean
under ASan+UBSan; the raster path, which cannot leave Windows because PDFium is in it, is checked
with Dr. Memory / Application Verifier over the same corpus instead (R7).
*Manual:* → queued **`Stage2-Rendering-Testing-Guide.md`**, written before this stage's code (§5). In brief:
open every corpus file; scroll a 900-page document; all four layout modes; zoom to 6400% and to 8%;
drag the window between a 100 %- and a 150 %-scaled monitor and confirm crisp text with no layout
jump; change monitor scaling while the window is open; open an encrypted file and a repaired file.

**Exit criteria.** Every corpus file opens and renders correctly, `corpus/malformed/` included, with
no crash, no ASan finding on the portable COS run and no Dr. Memory finding on the raster run;
golden baselines committed; scrolling a large document stays
responsive; mixed-DPI dragging is correct; **A2 has a recorded verdict with numbers**.

**Deferrals.** No text selection, no search, no annotations, no editing, and no save — the document
is strictly read-only this stage. Split views, rulers, grid, guides, and full-screen come in Stage 4.

---

### Stage 3 — Document model, command journal, incremental save

*Part 3 of the first milestone — the stage that closes the risk slice.*

**Status:** ⚪ **Not started.** Carries risk **A4** as a **go/no-go**: can an incremental save
preserve the original bytes closely enough that a signature applied *before* an unrelated later edit
still validates? `pdf/cos/IncrementalWriter` is the riskiest first-party code in the project.

**Objectives.** Make one real change to a document, reversibly, durably, and recoverably: rotate a
page, undo it, redo it, save it incrementally, reopen it, and recover it after a kill.

**Components & responsibilities.**
- `core/doc/Document` — `Result<void> hydrate(IDocumentBackend&)`, `Revision revision() const`.
- `core/doc/DocumentId`, `Page`, `PageTree` (`void rotate(PageIndex, int)`, `void move(Range, PageIndex)`,
  `PageLabel labelFor(PageIndex) const`), `Outline` (read-only), `Metadata`, `Revision.h`, `Scope`.
- `core/cmd/Command.h` — the five-method contract.
- `core/cmd/CommandJournal` — `bool run(std::unique_ptr<Command>)`, `bool undo()`, `bool redo()`,
  `void beginGroup(QString)`, `void endGroup()`, `Revision revision() const`,
  `void markSaved(Revision)`, `Signal<Scope> mutated`, `Signal<bool> dirtyChanged`.
- `core/cmd/CommandRegistry` — `void add<T>()`, `std::unique_ptr<Command> deserialize(const QJsonObject&)`.
- `core/cmd/RecoveryJournal` — `void append(const QJsonObject&)`, `QVector<Found> discover()`,
  `ReplayResult replay(CommandJournal&)`, `void truncate()`.
- `core/cmd/commands/PageCommands` — `RotatePageCommand` only this stage.
- `pdf/cos/IncrementalWriter` — `Result<void> append(QIODevice&, const ObjectDelta&)`.
- `pdf/cos/ObjectAllocator`.
- `pdf/ops/MutationSinkImpl` — `void setPageRotation(PageIndex, int)` + the queue/flush pair.
- `pdf/ops/PageOps` — `void applyRotation(CosDict& pageDict, int angle)`.
- `app/RecoveryPrompt`, `ui/dialogs/RecoveryDialog`, `ui/dialogs/RepairReportDialog`.
- `tools/mutation-lint/`, `tests/roundtrip/`, `tests/fuzz/`.

**Key bindings handled** — `Ctrl+S` ([Global](./Features.md#global-active-in-every-context)),
`Ctrl+Z` / `Ctrl+Y` ([Common](./Features.md#common-all-contexts-unless-a-context-above-overrides-the-chord),
now backed by a real journal), and `Ctrl+R` in **both**
[`Viewer`](./Features.md#viewer-review-mode) and
[`Thumbnails`](./Features.md#page-thumbnails-pane) contexts — the first chord to demonstrate context
dispatch against a real mutation.

**Data / settings keys.** `documents/autosaveIntervalSec`, `documents/recoveryDir`,
`documents/saveMode` (incremental | full), `documents/warnOnRepairedSave`.

**Cross-cutting additions — the gate turns on here.** First registered command:
`RotatePageCommand`. From this stage forward `mutation-lint` blocks any command lacking registration,
a serialize round-trip test, and a revert test. i18n groups: `command.*` (undo-menu labels),
`recovery.*`.

**Algorithms & notes.**
- **Permission pre-flight happens before construction, not after.** `CommandJournal::run` asks
  `Permissions::may()` first; a refused operation must leave the document bit-identical.
- **Partial application is a bug, not a state.** `apply()` returning false must leave the document
  untouched; a debug build asserts it, and the reversibility property test proves it over the corpus.
- **The recovery journal is `fsync`ed per command.** Slower than buffering, and correct — the whole
  point is surviving a kill at an arbitrary instant.
- **Replay is per-command resumable.** A failure at command *k* keeps 1..*k-1* and reports *k*. An
  all-or-nothing recovery would throw away good work to avoid reporting a bad line.
- **A repaired document's first save is forced to Save-As.** Appending an incremental update to a
  file whose structure we reconstructed risks destroying the original.
- **Compound operations group.** "Insert 12 pages" is one `Ctrl+Z`, via `beginGroup`/`endGroup`.

**Testing.**
*Automated:* `tst_journal` (run/undo/redo/group, dirty tracking, revision monotonicity, permission
refusal leaves no trace); `tst_pagetree`; `tst_command_serialize` (round-trip per command);
`tst_reversibility` — **the property test**: for every registered command over the corpus,
`apply`+`revert` restores a structural hash exactly; `tst_roundtrip` — open → rotate → incremental
save → reopen: rotation present, untouched objects **byte-identical**, and `corpus/signed-*.pdf`
still validates (**the A4 gate**); `tst_recovery` (append/discover/replay, mid-file corruption stops
cleanly, read-only directory degrades to memory with one warning); `tests/fuzz` targets live on the
`portable-fuzz` preset.
*Manual:* → queued **`Stage3-Mutation-Testing-Guide.md`**, written before this stage's code (§5). In brief:
rotate from the Viewer and from Thumbnails; undo/redo repeatedly; save and reopen; compare the file
bytes before and after; kill the process mid-edit and recover; make the recovery directory read-only;
open a repaired file and confirm the forced Save-As.

**Exit criteria.** Rotate → undo → redo → save → reopen works end to end; the original bytes are
provably preserved; a pre-signed file's signature survives an unrelated incremental edit
(**A4 verdict recorded**); killing the process and relaunching offers and correctly replays the
change; `mutation-lint` is live and fails on a test command with no inverse.

**Deferrals.** Every other page operation (Stage 7). No annotations, forms, security, or export. No
full-rewrite Save-As yet — that is Stage 8; this stage's forced-Save-As path writes through the
incremental writer to a fresh file.

> **🏁 First milestone complete.** All five locked decisions are now proven against real code:
> the toolkit (D2), the render half of the engine (D3/D10), our own write half (D3), the mutation
> design (D5), and the platform boundary. Everything after this point is *work*, not *risk*.

---

### Stage 4 — Viewing and navigation surface

**Status:** ⚪ **Not started.**

**Objectives.** Everything in [`Features.md` §1](./Features.md#1-view--find-) that is not text or
search: the sidebar panes, split views, rulers/grid/guides, full-screen and presentation, window
arrangement, and session restore.

**Components & responsibilities.**
- `ui/panes/ThumbnailsPane` — `void setPageSize(int)`, `QVector<PageIndex> selection() const`.
- `ui/panes/BookmarksPane` (navigation only), `ui/panes/DestinationsPane` (read-only).
- `ui/viewer/SplitViewContainer` — `void split(Orientation)`, `void freeze(int row, int col)`.
- `ui/viewer/RulerWidget`, `ui/viewer/GridGuideLayer` — `QPointF snap(QPointF, SnapFlags) const`.
- `ui/viewer/FullScreenController` — `void enter(ScreenIndex)`, `void setTransition(Transition)`,
  `void setAutoAdvance(int sec)`, `void blank(BlankMode)`.
- `ui/viewer/ToolController` + `tools/{HandTool,SelectTool,MarqueeZoomTool,LoupeTool}`.
- `ui/shell/WindowArranger` — `void tile(Orientation)`, `void cascade()`, `void mergeWindows()`,
  `void syncScroll(bool)`.
- `core/settings/SessionState` — `void capture(const MainWindow&)`, `void restore(MainWindow&)`.
- `ui/dialogs/GoToPageDialog`, `ui/dialogs/DocumentPropertiesDialog`.

**Key bindings handled** — [Viewer](./Features.md#viewer-review-mode): `Ctrl+4`, `Ctrl+U`, `Ctrl+'`,
`Ctrl+;`, `Ctrl+L` (reading mode), `S` (snapshot). [Global](./Features.md#global-active-in-every-context):
`F11`, `Ctrl+Shift+P`, `Ctrl+Shift+B`. Full [Thumbnails](./Features.md#page-thumbnails-pane) context:
`Enter`, arrows, `+`/`-`. [Bookmarks](./Features.md#bookmarks-pane): `Enter`, `→`/`←`, `*`/`/`.
Full [Full Screen & Presentation](./Features.md#full-screen--presentation) table.

**Data / settings keys.** `pageDisplay/continuousScroll`, `pageDisplay/autoScrollSpeed`,
`units/rulerUnit`, `units/gridSpacing`, `units/gridSubdivisions`, `units/gridColour`,
`units/gridOffset`, `units/snapToGrid`, `units/snapToGuides`, `units/snapToObjects`,
`fullScreen/transition`, `fullScreen/autoAdvanceSec`, `fullScreen/loop`, `fullScreen/monitor`,
`fullScreen/showCursor`, `general/rememberPaneLayout`, `general/rememberWindowLayout`.

**Cross-cutting additions.** No new commands (this stage is all view state, no document mutation —
a useful check that view and document changes stay separate). i18n: `panes.*`, `fullscreen.*`,
`units.*`.

**Algorithms & notes.** Snapping resolves in a fixed precedence — object edges, then guides, then
grid — with one threshold in device pixels so it feels the same at every zoom. Session restore must
survive a monitor that no longer exists: geometry is stored per monitor-arrangement fingerprint and
clamped to the current desktop on restore. Thumbnails share the Stage 2 render pipeline at a low
resolution rather than owning a second one.

**Testing.**
*Automated:* `tst_snap` (precedence and threshold), `tst_sessionstate` (round-trip; restore onto a
smaller desktop clamps), `tst_presentation_modes`, `tst_thumbnail_selection`.
*Manual:* → queued **`Stage4-ViewingSurface-Testing-Guide.md`** (see §5). Emphasis: two monitors at
different scaling, full-screen on the secondary monitor, unplugging a monitor while a window is on
it, and spreadsheet-split freezing.

**Exit criteria.** All four layout modes plus split views work; rulers, grid, guides, and snapping
behave identically at 50% and 400% zoom; full-screen and presentation work on a chosen monitor;
window arrangement and session restore survive a monitor topology change.

**Deferrals.** Comparison view (Stage 5, needs text). Pane *editing* — bookmark edit is Stage 7,
comments Stage 6, fields Stage 9, layers/tags Stage 7. AI assistant (Stage 16).

---

### Stage 5 — Text layer: extraction, selection, find, search, comparison

**Status:** ⚪ **Not started.** `core/text/LayoutAnalyzer` is on the critical path for Stage 13's
reflow engine, so its quality bar here is higher than this stage alone needs.

**Objectives.** Text becomes first-class: extract it, select it, copy it, find it, search across
files, and diff two documents.

**Components & responsibilities.**
- `core/text/PageTextModel` — `Result<void> hydrate(ITextExtractor&, PageIndex)`.
- `core/text/LayoutAnalyzer` — `Blocks analyze(const PageTextModel&, const TagTree*) const`.
- `core/text/TextSelection` — `Range hitTest(QPointF) const`, `Range expandTo(Granularity) const`,
  `QString asPlainText() const`, `QString asFormatted() const`, `Table asTable() const`.
- `core/text/SearchEngine` — `Hits find(const Document&, const SearchQuery&)`,
  `Hits findAcross(const QStringList& paths, const SearchQuery&)`.
- `core/text/SearchQuery.h`, `core/text/DocumentIndex`.
- `core/text/DocumentComparer` — `DiffSet compare(const Document&, const Document&, CompareOptions)`.
- `pdf/render/PdfiumTextExtractor`, `pdf/cos/ContentStreamParser`.
- `ui/viewer/SelectionOverlay`, `ui/viewer/ComparisonView`, `ui/panes/SearchPane`,
  `ui/panes/DifferencesPane`, `ui/write/FindReplaceBar` (find half only),
  `ui/viewer/tools/SnapshotTool`, `ui/dialogs/CompareDialog`.
- `platform/win/WinRichClipboard`.

**Key bindings handled** — the whole [Find / Search](./Features.md#find--search) table
(`KeyContext::Search`): `Ctrl+F`, `F3`/`Shift+F3`, `Ctrl+G`/`Ctrl+Shift+G`, `Ctrl+Shift+F`, `Enter`,
`↑`/`↓`, `Alt+R`, `Alt+W`, `Alt+C`, `Esc`. [Viewer](./Features.md#viewer-review-mode):
`Ctrl+Alt+C` (compare), `Ctrl+]`/`Ctrl+[` (next/previous difference).
[Common](./Features.md#common-all-contexts-unless-a-context-above-overrides-the-chord): `Ctrl+C`,
`Ctrl+A` in the Viewer context.

**Data / settings keys.** `search/wholeWords`, `search/caseSensitive`, `search/ignoreDiacritics`,
`search/useRegex`, `search/includeComments`, `search/includeBookmarks`, `search/includeFields`,
`search/includeAttachments`, `search/maxResults`, `search/indexCacheMB`,
`documents/compareSensitivity`, `documents/compareIgnoreHeaderFooter`,
`documents/compareIgnoreWhitespace`.

**Cross-cutting additions.** No document mutation. i18n: `search.*`, `compare.*`.

**Algorithms & notes.**
- `LayoutAnalyzer` uses the tag tree where present and falls back to geometry: baseline clustering
  into lines, gap analysis into blocks, whitespace-column detection, and ruling-line-plus-alignment
  detection for tables. **Its accuracy is measured, not asserted** — a labelled corpus subset with a
  precision/recall target, so Stage 13 inherits a known quantity rather than a hope.
- The document index is built lazily on first search and reused, so a second search of a 900-page
  file is instant.
- Comparison aligns text with a Myers diff over normalized run tokens, then confirms
  page-appearance differences with a raster compare — text-only diffing misses moved images, and
  raster-only diffing reports every reflowed line as a change.

**Testing.**
*Automated:* `tst_selection` (hit-test, word/line/block expansion, column selection, RTL, vertical
text), `tst_layout` (precision/recall against the labelled subset), `tst_search` (all flags, regex,
each content class, CJK, combining marks), `tst_index`, `tst_compare` (known before/after pairs).
*Manual:* → queued **`Stage5-TextSearch-Testing-Guide.md`** (see §5), whose manual section is brief:
selection across columns and page boundaries; copy into Word and confirm formatting;
snapshot into an image editor; search a CJK document; compare two real revisions.

**Exit criteria.** Selection is accurate on multi-column, RTL, and CJK pages; every §1.8 search
toggle works and is honoured across all five content classes; cross-file search returns correct
snippets; comparison finds text, appearance, and page-level changes on real revision pairs.

**Deferrals.** Replace (needs Write mode, Stage 13). Catalog index (Stage 16). OCR'd-layer search
works automatically once Stage 16 adds OCR — no change needed here.

---

### Stage 6 — Annotations and review

**Status:** ⚪ **Not started.** May run in parallel with Stage 7.

**Objectives.** The full comment and markup toolset of
[`Features.md` §4](./Features.md#4-review--annotate-), the Comments pane with management, measurement
tools, and FDF/XFDF exchange.

**Components & responsibilities.**
- `core/doc/Annotation`, `AnnotationStore` — `void add(Annotation)`, `Query query(Filter) const`,
  `void reply(AnnotationId, Annotation)`, `void setStatus(AnnotationId, Status, QString author)`,
  `MergeResult importFdf(const Fdf&)`, `Fdf exportFdf(Filter) const`.
- `core/doc/Attachments` — `void add(EmbeddedFile)`, `Result<QByteArray> extract(FileId) const`.
- `core/cmd/commands/AnnotationCommands` — one subclass per operation.
- `core/cmd/commands/ContentCommands` — typewriter text only this stage.
- `core/settings/ProfileStore` (first user: stamps).
- `pdf/ops/AnnotationOps`, `pdf/ops/AppearanceStreamBuilder` —
  `CosStream build(const Annotation&, AppearanceState)`.
- `pdf/cos/ContentStreamBuilder`.
- `ui/viewer/AnnotationOverlay`, `ui/viewer/tools/{MarkupTool,NoteTool,TypewriterTool,DrawingTool,InkTool,StampTool,MeasureTool}`.
- `ui/panes/CommentsPane`, `ui/panes/StampPalettePane`, `ui/panes/AttachmentsPane`.
- `ui/dialogs/{CommentSummaryDialog,MeasurementScaleDialog,CustomStampDialog}`.

**Key bindings handled** — the whole
[Annotation tools](./Features.md#annotation-tools) table (`KeyContext::AnnotationTool`):
`Ctrl+Alt+{M,H,U,X,T,L,R,O,N,P,S,D,A,K}`, `K`, `Shift`+drag, `Alt`+drag, `Enter`, `Esc`, `Delete`,
`Ctrl+E`, `Ctrl+Alt+Enter`, arrows/`Ctrl+`arrows. The whole
[Comments pane](./Features.md#comments-pane) table (`KeyContext::Comments`): `↑`/`↓`, `Enter`, `F2`,
`Ctrl+Enter`, `Delete`, `Space` (checkmark — note the deliberate three-way collision with Viewer
scroll and Forms-Fill toggle), `Ctrl+Shift+K`, `Ctrl+F`, `Ctrl+.`/`Ctrl+,`. [Global](./Features.md#global-active-in-every-context):
`Ctrl+Shift+C`, `Ctrl+Shift+A`.

**Data / settings keys.** `identity/author`, `identity/email`, `identity/organization`,
`commenting/defaultColour.<tool>`, `commenting/defaultOpacity.<tool>`,
`commenting/defaultThickness.<tool>`, `commenting/stickyTool`, `commenting/popupBehaviour`,
`commenting/showPopupsOnHover`, `commenting/printComments`, `commenting/summaryLayout`,
`commenting/spellCheckComments`, `measuring/scaleRatio`, `measuring/displayUnit`,
`measuring/precision`, `measuring/snapToPaths`, `measuring/writeToComment`.

**Cross-cutting additions.** Commands registered: `AddAnnotation`, `EditAnnotation`, `MoveAnnotation`,
`DeleteAnnotation`, `ReplyToAnnotation`, `SetAnnotationStatus`, `SetAnnotationAppearance`,
`LockAnnotation`, `ImportAnnotations`, `AddAttachment`, `DeleteAttachment`, `AddTypewriterText` —
each with its inverse and both tests. i18n: `annot.*`, `comments.*`, `measure.*`, `stamps.*`.

**Algorithms & notes.**
- **We generate every `/AP` appearance stream ourselves** rather than relying on the viewer, so a
  RawPage highlight looks identical in Acrobat, Chrome, and a phone. This is the single biggest
  interoperability decision in the stage and it is why `AppearanceStreamBuilder` gets golden tests.
- Comment migration to a revised document matches on text content first, then position, and
  **reports** what it could not place — a comment silently moved to the wrong paragraph is worse than
  one reported as unplaceable.
- Ink smoothing fits a Catmull-Rom spline to sampled points, keeping the raw samples so the eraser
  can split a stroke.
- Measurement snapping reuses Stage 4's snap precedence, extended with path endpoints, midpoints,
  and intersections.

**Testing.**
*Automated:* `tst_annotation`, `tst_annotstore` (filter/sort/group, threading, status history),
`tst_fdf` (import/export round-trip, and against files produced by other readers),
`tst_appearance` (golden raster per annotation type — this is what protects interoperability),
`tst_measure` (scale, units, precision), plus reversibility and round-trip for all twelve commands.
*Manual:* → queued **`Stage6-Annotations-Testing-Guide.md`**. Every tool on a real page; open the
result in another PDF reader and confirm identical appearance; import comments exported by another
reader; record an audio comment; measure with a calibrated scale.

**Exit criteria.** Every §4.1 tool creates a correct, correctly-appearing annotation; the Comments
pane groups, filters, sorts, searches, replies, and sets status; FDF/XFDF round-trips with at least
one other reader; comment summaries generate in all four layouts; annotations undo cleanly.

**Deferrals.** Redaction *marks* exist; **applying** them is Stage 10. Shared review, review
tracking, and collaborative sessions are Stage 16. Audio recording needs `QtMultimedia` — if it
slips, it slips alone.

---

### Stage 7 — Document assembly

**Status:** ⚪ **Not started.** May run in parallel with Stage 6.

**Objectives.** All of [`Features.md` §5](./Features.md#5-assemble) except batch: page operations,
merge, bookmark editing, tags, layers, headers/footers/Bates, watermarks, imposition, and links.

**Components & responsibilities.**
- `core/doc/PageTree` (full mutation set), `Outline` (full editing + generators),
  `Destinations`, `LayerTree`, `TagTree`.
- `core/cmd/commands/PageCommands` (all: insert, delete, move, duplicate, extract, replace, crop,
  resize, renumber), `BookmarkCommands`, `StructureCommands`, `DecorationCommands`.
- `pdf/ops/PageOps` (full), `DecorationOps` — `void applyWatermark(const WatermarkSpec&, Range)`,
  `void applyHeaderFooter(const HfSpec&, Range)`, `void applyBates(const BatesSpec&, Range)`;
  `ImpositionOps` — `Result<Document> impose(const Document&, const ImpositionSpec&)`.
- `ui/panes/{BookmarksPane (editing),LayersPane,TagsPane,DestinationsPane}`.
- `ui/viewer/tools/{CropTool,LinkTool}`.
- `ui/dialogs/{PageOpsDialogs,MergeDialog,WatermarkDialog,HeaderFooterDialog,BatesDialog,ImpositionDialog,LinkPropertiesDialog}`.

**Key bindings handled** — the rest of [Page Thumbnails](./Features.md#page-thumbnails-pane):
`Delete`, `Ctrl+X`/`Ctrl+C`/`Ctrl+V`, `Ctrl+A`, `Alt+↑`/`Alt+↓`, `Ctrl+Shift+X`, `Ctrl+Shift+D`.
The rest of [Bookmarks](./Features.md#bookmarks-pane): `F2`, `Delete`, `Ctrl+B`, `Tab`/`Shift+Tab`,
`Alt+↑`/`Alt+↓`, `Ctrl+Shift+D`. [Viewer](./Features.md#viewer-review-mode): `Ctrl+B` (new bookmark).
[Global](./Features.md#global-active-in-every-context): `Ctrl+Shift+L`, `Ctrl+Shift+E`.

**Data / settings keys.** `documents/defaultPageSize`, `documents/cropRemoveWhiteMargins`,
`documents/splitMode`, `documents/splitValue`, `documents/mergeBookmarkPerFile`,
`documents/renameCollidingFields`, `documents/watermarkProfile`, `documents/headerFooterProfile`,
`documents/batesStart`, `documents/batesDigits`, `documents/batesPrefix`, `documents/batesSuffix`,
`documents/impositionProfile`, `documents/autoDetectUrls`, `documents/linkAppearance`.

**Cross-cutting additions.** Commands: `InsertPages`, `DeletePages`, `MovePages`, `DuplicatePages`,
`ExtractPages`, `ReplacePages`, `CropPages`, `ResizePages`, `RenumberPages`, `MergeDocuments`,
`CreateBookmark`, `RenameBookmark`, `DeleteBookmark`, `ReparentBookmark`, `SetBookmarkTarget`,
`GenerateBookmarks`, `CreateLayer`, `DeleteLayer`, `MergeLayers`, `FlattenLayers`, `SetLayerUsage`,
`RetagElement`, `AutoTag`, `ApplyWatermark`, `RemoveWatermark`, `ApplyHeaderFooter`,
`RemoveHeaderFooter`, `ApplyBates`, `RemoveBates`, `CreateLink`, `EditLink`, `DeleteLink`,
`CreateDestination`, `RenameDestination`, `DeleteDestination`. i18n: `pages.*`, `bookmarks.*`,
`layers.*`, `tags.*`, `decorate.*`, `links.*`.

**Algorithms & notes.**
- **Reordering pages must repair every reference to them:** links, bookmark destinations, named
  destinations, article threads, and structure-tree page references. A "move pages" that breaks
  bookmarks is the classic bug of this feature area, so `tst_reference_repair` runs after every page
  command, not only after `MovePages`.
- Watermarks, headers/footers, and Bates numbers are written into **marked content sections tagged
  with a RawPage key**, which is what makes "remove the watermark I added" possible without guessing.
- Split-by-size does a binary search on the page count per output, since compression makes size
  non-linear in pages.
- Bates numbering across a document set is one logical run: the allocator is shared and the report
  states the first and last number per document.
- `MergeDocuments` is the only page command that can hit form-field name collisions; the policy
  (rename versus deliberately merge) is a setting, and the result is reported either way.

**Testing.**
*Automated:* `tst_pagetree` (full mutation set), `tst_reference_repair` (links, bookmarks,
destinations, tags survive every reordering), `tst_outline` (hierarchy ops and all three generators),
`tst_layers`, `tst_tags`, `tst_decoration` (apply → remove → byte-compare against the original),
`tst_imposition` (n-up and booklet page order for 2/4/8/16 and odd counts), `tst_merge` (bookmarks,
links, fields, layers preserved; collisions reported); reversibility and round-trip for all 36
commands.
*Manual:* → queued **`Stage7-Assembly-Testing-Guide.md`**. Emphasis: drag pages between two open
documents; a 30-way merge; booklet imposition printed duplex on paper and physically folded.

**Exit criteria.** Every §5.1 page operation works and repairs all references; merge preserves
bookmarks/links/fields/layers; bookmark hierarchy editing works by drag and by chord; watermarks and
headers/footers apply, update, and remove cleanly; imposition output is correct on paper; the tag
tree can be viewed, retyped and reordered.

**Deferrals.** Batch (Stage 14). Scanner-sourced page insert (Stage 16). Flatten (Stage 8).
Portfolios (Stage 16).

---

### Stage 8 — Save, export, and optimize

**Status:** ⚪ **Not started.** This is the stage that makes the product's output *shippable* — up to
here every path has been incremental-save only.

**Objectives.** All of [`Features.md` §2.5](./Features.md#25-save-and-export): full-rewrite Save-As,
version and conformance targets, export to every listed format, content reuse, optimization, and
flattening.

**Components & responsibilities.**
- `pdf/cos/FullWriter` — `Result<void> write(QIODevice&, const Document&, WriteOptions)`.
- `pdf/cos/Linearizer`.
- `pdf/ops/OptimizeOps` — `Result<Savings> optimize(Document&, const OptimizeProfile&)`,
  `SpaceAudit audit(const Document&) const`.
- `pdf/ops/FlattenOps` — `void flatten(FlattenTargets, Range)`.
- `pdf/ops/ExportOps` — `Result<void> exportPages(const ExportSpec&)`,
  `Result<void> exportOffice(Format, const QString& path)`.
- `pdf/ops/ConformanceChecker` — `Report check(const Document&, Conformance) const`.
- `platform/win/WinPrinting`, `platform/win/WinOfficeBridge` (export path).
- `ui/dialogs/{ExportDialog,OptimizerDialog,ConformanceReportDialog,PrintDialog,FlattenDialog}`.

**Key bindings handled** — [Global](./Features.md#global-active-in-every-context): `Ctrl+Shift+S`,
`Ctrl+P`, `Ctrl+D`.

**Data / settings keys.** `convertFrom/exportFormat`, `convertFrom/exportDpi`,
`convertFrom/exportColourSpace`, `convertFrom/textLayoutPreserving`,
`convertFrom/officeEngine` (firstParty | officeBridge), `documents/optimizeProfile`,
`documents/targetPdfVersion`, `documents/conformanceTarget`, `documents/linearizeOnSave`,
`documents/flattenTargets`.

**Cross-cutting additions.** Commands: `OptimizeDocument`, `FlattenContent`,
`ConvertPdfVersion` — all document mutations and therefore commands. Export and print are **not**
commands (they do not change the document) — a distinction `mutation-lint` does not police, so it is
stated here. i18n: `export.*`, `optimize.*`, `conformance.*`, `print.*`.

**Algorithms & notes.**
- Save-As renumbers objects, drops unreachable ones, packs object streams, and is the **only** path
  that discards revision history — which is exactly why "Save" and "Save As" must never be silently
  interchanged.
- Office export quality is a product-defining bar and settles risk **R3** here on measured fidelity:
  a first-party writer versus round-tripping through `IOfficeBridge`, chosen per format, not
  globally. The decision and its measurements are recorded in this stage's status block.
- Conformance checking reports **per object with a page reference**; "this file is not PDF/A" without
  a location is not actionable.
- The space audit attributes every byte to a category, which is what makes size reduction
  predictable rather than a lottery.

**Testing.**
*Automated:* `tst_fullwriter` (renumber, compact, unreachable drop; reopen equals the source
semantically), `tst_linearize` (structure validated by an external checker), `tst_optimize` (each
profile reduces size without visual change — verified by golden compare), `tst_flatten` (flattened
output renders identically to the unflattened original), `tst_export_raster` (DPI and colour space),
`tst_conformance` (known-good and known-bad PDF/A and PDF/X files), `tst_export_office` (fidelity
score against a labelled corpus, tracked as a number over time).
*Manual:* → queued **`Stage8-SaveExport-Testing-Guide.md`**. Emphasis: export to Word and open in
real Word; print to a physical printer; validate PDF/A output in an external validator; optimize a
200 MB scanned file.

**Exit criteria.** Save-As produces a smaller, semantically identical file; every export format
opens correctly in its native application; PDF/A output validates externally; flattening changes
nothing visually; the space audit's numbers match reality; **R3 has a recorded decision with
fidelity measurements**.

**Deferrals.** PDF/X output profiles beyond X-1a and X-4. EPUB. Batch export (Stage 14).

---

### Stage 9 — Forms

**Status:** ⚪ **Not started.** May run in parallel with Stage 10.

**Objectives.** All of [`Features.md` §6](./Features.md#6-forms): field creation and properties for
all eight types, tab order, format/validate/calculate, filling, and data exchange.

**Components & responsibilities.**
- `core/doc/FormField`, `FormModel` — `Result<void> setValue(FieldName, Value)`,
  `CalcOrder calculationOrder() const`, `void setCalculationOrder(CalcOrder)`,
  `QVector<FieldName> missingRequired() const`, `TabOrder tabOrder(PageIndex) const`.
- `core/forms/FieldScript` — the sandboxed Acrobat-compatible JavaScript subset:
  `Result<Value> evaluate(const Script&, ScriptContext&)`.
- `core/cmd/commands/FormCommands`.
- `pdf/ops/FormFieldOps` — `void createField(const FieldSpec&)`, `void normalizeStaticXfa()`.
- `pdf/render/PdfiumFormFill`.
- `ui/viewer/FormOverlay`, `ui/viewer/tools/FieldTool`, `ui/panes/FieldsPane`.
- `ui/dialogs/{FieldPropertiesDialog,TabOrderDialog,CalcOrderDialog,FormDataDialog,DuplicateFieldDialog}`.

**Key bindings handled** — the whole [Forms — Design](./Features.md#forms--design) table
(`KeyContext::FormsDesign`): `Ctrl+Alt+F`, `T`/`K`/`B`/`D`/`L`/`U`/`G`/`X`, `Tab`/`Shift+Tab`,
`Enter`, `Delete`, `Ctrl+A`, `Ctrl+D`, arrows, `Ctrl+Shift+`arrows, `Ctrl+Shift+T`, `Ctrl+Alt+G`.
The whole [Forms — Fill](./Features.md#forms--fill) table (`KeyContext::FormsFill`): `Tab`/`Shift+Tab`,
`Space`, `↑`/`↓`, `Enter`, `Ctrl+Enter`, `Esc`, `F2`, `Ctrl+Alt+G`, `Ctrl+Shift+K`.
[Global](./Features.md#global-active-in-every-context): `Ctrl+Shift+I`.

**Data / settings keys.** `forms/highlightFields`, `forms/highlightColour`,
`forms/requiredFieldColour`, `forms/autoComplete`, `forms/autoCompleteHistory`,
`forms/showFieldTooltips`, `forms/warnCalcOrder`, `forms/showFocusRect`,
`javascript/enableFormScripts`, `javascript/warnBlockedScript`.

**Cross-cutting additions.** Commands: `CreateField`, `DeleteField`, `EditFieldProperties`,
`MoveField`, `ResizeField`, `DuplicateFieldAcrossPages`, `SetFieldValue`, `SetTabOrder`,
`SetCalculationOrder`, `ResetForm`, `ImportFormData`, `FlattenForm`, `NormalizeStaticXfa`.
i18n: `forms.*`, `fields.<type>.*`.

**Algorithms & notes.**
- Calculation order is a **topological sort** of the field dependency graph; a cycle is reported with
  the participating field names rather than silently iterated to a fixed point.
- `SetFieldValue` is a command like any other, so form filling is undoable and recoverable — which
  falls out of Stage 3 for free and is a differentiator many editors lack.
- The JavaScript subset is **sandboxed and enumerated**: no filesystem, no network, no `app.launchURL`
  without a trust check. A script calling outside the subset is blocked and *reported*, never
  silently ignored — a silently-failing form is worse than one that says why.
- Auto-size text fields resolve their font size by binary search on the fitted extent, cached per
  field geometry.

**Testing.**
*Automated:* `tst_formfield` (all eight types, every option flag), `tst_formmodel` (name resolution,
tab order by row/column/structure, required validation), `tst_calc` (sum/product/average/min/max,
simplified field notation, topological order, cycle reporting), `tst_fieldscript` (the subset;
blocked calls are reported), `tst_formdata` (FDF/XFDF/XML/CSV round-trip, and merging many filled
copies into one spreadsheet), `tst_xfa_normalize` (static normalizes; dynamic reports honestly);
`tst_appearance` extended to widget appearance states.
*Manual:* → queued **`Stage9-Forms-Testing-Guide.md`**. Emphasis: fill a real government form; a
calculated invoice with a deliberate cycle; tab through 200 fields; open a RawPage-authored form in
another reader and fill it there.

**Exit criteria.** All eight field types are creatable with full properties; auto-detection produces
usable fields on a static PDF; calculations and validation work with a reported order; tab order
works all three ways; data round-trips in all four formats; a RawPage-authored form fills correctly in
another reader; field edits undo.

**Deferrals.** Dynamic XFA beyond honest reporting (risk R4). Barcode *decoding* verification is
generation-only this stage. Reader-extension rights (a licensing question, not a technical one).

---

### Stage 10 — Security, permissions, redaction, sanitize

**Status:** ⚪ **Not started.** May run in parallel with Stage 9. `pdf/ops/RedactionOps` is the one
operation in the product whose correctness is a **security** property — a redaction that leaves the
text recoverable is a data breach, not a bug.

**Objectives.** [`Features.md` §7.1–§7.5 and §7.9](./Features.md#7-secure--sign-): passwords,
certificate encryption, security schemes, redaction, hidden-content removal, and the trust manager.

**Components & responsibilities.**
- `core/doc/Permissions` — `bool may(Operation) const`.
- `core/security/{EncryptionPolicy,SecurityScheme,SecuritySchemeStore,RedactionPlan,HiddenContentScanner,TrustPolicy}`.
- `core/cmd/commands/SecurityCommands`.
- `pdf/cos/{StandardSecurityHandler,PublicKeySecurityHandler}`.
- `pdf/ops/{EncryptionOps,RedactionOps,SanitizeOps}` —
  `RedactionOps::Result apply(const RedactionPlan&)` returns what it removed, per page.
- `platform/win/WinProcessSandbox`, `platform/win/WinCertificateStore` (recipient lookup).
- `ui/viewer/tools/RedactTool`, `ui/dialogs/{SecurityDialog,SanitizeDialog,RedactionSetupDialog,SecuritySchemeDialog,TrustManagerPage}`.

**Key bindings handled** — [Annotation tools](./Features.md#annotation-tools): `Ctrl+Alt+K` (mark
for redaction; the *apply* action is menu-only by design — it is irreversible in effect even though
it is undoable in session).

**Data / settings keys.** `security/defaultEncryption`, `security/minPasswordLength`,
`security/warnWeakPassword`, `security/encryptMetadata`, `security/defaultPermissions`,
`security/redactionOverlayColour`, `security/redactionCodeSet`, `security/redactAlsoMetadata`,
`trust/allowNetwork`, `trust/allowedHosts`, `trust/blockedHosts`,
`trust/blockedAttachmentTypes`, `trust/privilegedLocations`, `trust/protectedViewMode`,
`javascript/enableDocumentScripts`.

**Cross-cutting additions.** Commands: `ApplyPasswordSecurity`, `ApplyCertificateSecurity`,
`RemoveSecurity`, `ApplySecurityScheme`, `ApplyRedactions`, `SanitizeDocument`. i18n: `security.*`,
`redact.*`, `sanitize.*`, `trust.*`.

**Algorithms & notes.**
- `Permissions::may()` is asked by `CommandJournal::run` **before** any command's `apply()`, so
  permission enforcement is one predicate in one place rather than a check scattered through fifty
  commands. This was designed in Stage 3 precisely so this stage is small.
- **Redaction rewrites content streams.** Covered text-showing operators are removed and the
  surrounding graphics state is preserved; covered image regions are re-encoded with the area
  destroyed. Drawing a black rectangle over text — which some tools do — is not redaction, and
  `tst_redaction` asserts extraction returns nothing for redacted regions.
- Re-encryption must re-key **every** stream and string, including ones we never parsed. A missed
  string is a plaintext leak in an encrypted file.
- Sanitize **verifies after removing** and reports per category, because "we tried" is not a
  security guarantee.
- Applying redactions to a signed document warns first: it will break the signature, by design.

**Testing.**
*Automated:* `tst_crypt` (RC4 40/128, AESV2, AESV3 against published vectors), `tst_permissions`
(every operation × every permission combination), `tst_encryptionops` (apply → open with the password
→ remove → byte-compare; every string and stream re-keyed), `tst_redaction` — **the critical suite**:
after applying, text extraction over the region returns nothing, the content stream contains no
covered glyph, the image bytes changed, and the same holds after Save-As and after optimization;
`tst_sanitize` (each category found, removed, and verified gone), `tst_trust`.
*Manual:* → queued **`Stage10-Security-Testing-Guide.md`**. Emphasis: redact, save, then attack the
output with an independent extraction tool and with a hex editor; certificate encryption for a real
recipient; protected view on a hostile file.

**Exit criteria.** All four encryption algorithms round-trip; permissions are enforced through the
single predicate for every command; **redacted content is unrecoverable by an independent tool, in a
hex editor, and after Save-As and optimization**; sanitize finds, removes, and verifies every
category; the trust manager blocks what it claims to.

**Deferrals.** AIP/rights management (Stage 16). Electronic envelopes and secure delivery need
Stage 11's signing. The security audit log lands with Stage 14's logging work.

---

### Stage 11 — Digital IDs and signatures

**Status:** ⚪ **Not started.** Needs Stage 10 (shared `pdf/cos` crypt code) and Stage 8.

**Objectives.** [`Features.md` §7.5–§7.8](./Features.md#75-digital-ids-and-trust): digital ID
management, trusted identities, signing, appearances, long-term validation, validation, and secure
delivery.

**Components & responsibilities.**
- `core/security/{SignatureRequest,SignatureValidator,ValidationReport}` —
  `SignatureValidator::validate(const Signature&, ValidationOptions) const`.
- `pdf/ops/SignatureOps` — `Result<void> sign(const SignatureRequest&, ICryptoProvider&)`,
  `QVector<Signature> readSignatures(const Document&) const`.
- `platform/win/{WinCertificateStore,WinCryptoProvider,WinSecretStore}`.
- `ui/panes/SignaturesPane`, `ui/dialogs/{SignDialog,SignatureAppearanceEditor,DigitalIdManager,TrustedIdentitiesDialog,ValidationPreferencesDialog,CertificateViewer,EnvelopeDialog}`.

**Key bindings handled** — [Global](./Features.md#global-active-in-every-context): `Ctrl+Shift+G`
(Signatures pane). Signing itself is deliberately menu- and ribbon-only: a one-chord irreversible
cryptographic operation is a footgun.

**Data / settings keys.** `signatures/defaultId`, `signatures/idPasswordTimeoutMin`,
`signatures/defaultAppearance`, `signatures/timestampServer`, `signatures/timestampCredentials`,
`signatures/embedRevocation`, `signatures/padesProfile`, `signatures/verifyAsOf` (signing | now),
`signatures/requireRevocationCheck`, `signatures/allowExpiredTimestamps`,
`signatures/trustOsRootStore`, `signatures/ldapServer`.

**Cross-cutting additions.** Commands: `SignDocument`, `CertifyDocument`, `CreateSignatureField`,
`ClearSignature`. i18n: `sign.*`, `validate.*`, `digitalId.*`, `trustedId.*`.

**Algorithms & notes.**
- Signing reserves the `/Contents` byte range, hashes **exactly** the bytes outside it, then embeds
  the CMS blob without shifting a single byte. An off-by-one here produces a signature that validates
  locally and fails everywhere else — so `tst_signing` cross-validates with an independent verifier.
- Every crypto primitive comes from `ICryptoProvider`, so all validation *logic* is unit-tested
  headlessly against a mock: chain building, trust decisions, revocation, and the
  timestamp-versus-signer-clock distinction.
- The permitted-versus-actual change comparison after a certifying signature reuses Stage 5's
  `DocumentComparer` — the same code that diffs two files answers "was this modification allowed?"
- **Offline validation reports honestly.** "Cannot be validated without network access" is a
  distinct, non-alarming state; showing a red cross for an unreachable OCSP responder trains users
  to ignore real warnings.

**Testing.**
*Automated:* `tst_validation` (valid, tampered, expired, revoked, untrusted, offline — all against a
mock provider), `tst_signing_bytes` (byte ranges exact; the document is byte-identical outside
`/Contents`), `tst_pades` (B-B, B-T, B-LT, B-LTA structure), `tst_signed_version` (view-as-signed and
compare-versions), plus round-trip: sign → add an annotation incrementally → the signature still
validates and reports the later change as permitted or not.
*Manual:* → queued **`Stage11-Signatures-Testing-Guide.md`**. Emphasis: **a physical smart card or
USB token**; sign in RawPage and validate in Acrobat, and the reverse; a real timestamp authority; a
certifying signature followed by a permitted and a forbidden change.

**Exit criteria.** Signing works with a self-signed ID, a PKCS#12 file, an OS-store ID, and a
physical token; RawPage-signed documents validate in at least one other reader and vice versa;
timestamps and revocation embed and verify; view-signed-version and compare-versions are correct;
the offline state reports honestly.

**Deferrals.** AIP labels (Stage 16). Reader-extension rights. Batch signing lands with Stage 14.

---

### Stage 12 — Create and import

**Status:** ⚪ **Not started.**

**Objectives.** The v1 subset of [`Features.md` §2.1](./Features.md#21-create-a-pdf): blank PDF,
from one or many files, from the clipboard, from a screen-region capture, and from an open document's
page range.

**Components & responsibilities.**
- `core/create/DocumentBuilder` — `Result<Document> blank(const PageSpec&)`,
  `Result<Document> fromFiles(const QStringList&, const ConversionProfile&)`,
  `Result<Document> fromClipboard()`, `Result<Document> fromRegion(const QImage&)`.
- `core/create/ConversionProfile` + its `ProfileStore` collection.
- `pdf/ops/ContentImport` — image, text, HTML, and Markdown to page content.
- `platform/win/WinOfficeBridge` (import path), `platform/win/WinRichClipboard` (paste path).
- `ui/dialogs/{NewDocumentDialog,CreateFromFilesDialog,ConversionProfileDialog}`,
  `ui/viewer/tools/RegionCaptureTool`.

**Key bindings handled** — [Global](./Features.md#global-active-in-every-context): `Ctrl+N` (now
functional, opening in Write mode once Stage 13 lands; until then it creates a blank fixed page).
[Common](./Features.md#common-all-contexts-unless-a-context-above-overrides-the-chord): `Ctrl+V`
into the Thumbnails context to paste pages.

**Data / settings keys.** `convertTo/defaultProfile`, `convertTo/pageSize`,
`convertTo/orientation`, `convertTo/margins`, `convertTo/imageCompression`,
`convertTo/imageQuality`, `convertTo/fontEmbedding`, `convertTo/pdfVersion`,
`convertTo/conformance`, `convertTo/bookmarksFromHeadings`, `convertTo/preserveLinks`,
`convertTo/convertComments`, `convertTo/officeEngine`.

**Cross-cutting additions.** Commands: `InsertImportedPages`, `PastePages`. Creating a *new* document
is not a mutation of an existing one, so `DocumentBuilder` is a service, not a command — stated here
because the distinction is easy to get wrong. i18n: `create.*`, `convertTo.*`.

**Algorithms & notes.** Office import quality follows the Stage 8 / risk R3 decision — the same
engine choice applies in both directions, and the profile carries it. Markdown and HTML import go
through a first-party layout pass rather than a browser engine (`QtWebEngine` is excluded by D2's
module list). Image import chooses a page size from the image's physical DPI when it declares one,
and falls back to the configured default when it does not.

**Testing.**
*Automated:* `tst_builder` (blank pages at every preset size, correct boxes),
`tst_fromfiles` (each source format produces a valid, openable PDF), `tst_conversion_profile`
(every knob has a measurable effect), `tst_clipboard_import` (text, image, and vector flavours),
`tst_import_fidelity` (a labelled corpus scored as a tracked number).
*Manual:* → queued **`Stage12-Creation-Testing-Guide.md`** (see §5), whose manual section is brief:
create from a 200-page Word document with headings and cross-references, and check
bookmarks and links; drag twelve mixed files onto the window; paste a spreadsheet region.

**Exit criteria.** Blank creation works at every preset size; each listed source format converts to
a valid PDF; conversion profiles round-trip and every setting demonstrably matters; Word headings
become bookmarks and cross-references become links.

**Deferrals.** Scanner, OCR, web capture, the virtual printer, Office add-ins, and portfolios are
all Stage 16.

---

### Stage 13 — Write mode (Convert to Writable)

**Status:** ⚪ **Not started.** **The largest and least certain stage in the plan** — risk **R1**.
It is split so that failure is contained: **13a** delivers the reflow model, conversion, and text
and paragraph editing; **13b** adds tables, illustrations, document structure, and revision
tracking. 13b may parallelize once 13a's model is stable. **A spike precedes 13a** and its finding
is recorded here before the stage is committed to.

**Objectives.** [`Features.md` §3](./Features.md#3-edit-): the dual-mode model, *Convert to
Writable*, word-processor editing, and *Prevent Further Edits* — plus Review-mode object editing.

**Components & responsibilities.**
*13a:* `core/reflow/{ReflowDocument,StyleSheet,LineBreaker,Shaper,FixedToReflowConverter,ReflowToFixedComposer,FidelityReport,SpellChecker}`;
`core/cmd/commands/ReflowCommands`; `core/cmd/commands/ContentCommands` (full Review-mode object
editing); `pdf/cos/ContentStreamBuilder` (page composition); `ui/write/{WriteCanvas,StyleGallery,SpellCheckPane,FindReplaceBar}` (replace half);
`ui/viewer/tools/ObjectEditTool`; `ui/dialogs/{ParagraphDialog,TabStopsDialog,PageSetupDialog,ConversionFidelityDialog}`.
*13b:* `core/reflow/{TableModel,ChangeTracker}`; `ui/write/{TableTools,TrackChangesBar}`;
`ui/panes/DocumentMapPane`; `ui/dialogs/{TableDialog,TocDialog,CaptionDialog,FootnoteDialog,ChartDialog}`.

Key signatures: `FixedToReflowConverter::convert(const Document&) -> Result<ReflowDocument, FidelityReport>`;
`ReflowToFixedComposer::compose(const ReflowDocument&, MutationSink&)` — **emits both page content and a structure tree (O2)**;
`LineBreaker::breakParagraph(const Paragraph&, const Constraints&) -> Lines`;
`TableModel::evaluate(CellRef) -> Value`; `ChangeTracker::accept(RevisionId)`.

**Key bindings handled** — the **whole** [Write mode](./Features.md#write-mode) table
(`KeyContext::WriteMode`), which is where the twelve deliberate collisions become real and testable
— **eight overriding Viewer and four overriding Global**, per
[`Features.md` §8](./Features.md#what-the-twelve-deliberate-collisions-counts): `Ctrl+B`, `Ctrl+I`, `Ctrl+U`, `Ctrl+D`, `Ctrl+E`, `Ctrl+K`, `Ctrl+L`,
`Ctrl+R`, `Ctrl+J`, `Ctrl+=`, `Ctrl+Shift+A`, `Ctrl+Shift+L`, `Ctrl+Shift+N`, `Ctrl+Shift+>`/`<`,
`Ctrl+Shift+=`, `Ctrl+Space`, `Ctrl+Alt+1`/`2`/`3`, `Ctrl+Shift+7`, `Tab`/`Shift+Tab`, `Ctrl+H`,
`Ctrl+Enter`, `Ctrl+Shift+Enter`, `Ctrl+Shift+Space`, `Ctrl+Shift+8`, `Ctrl+Shift+R`, `Ctrl+Alt+M`,
`Alt+=`, `F7`, `Shift+F7`, `Ctrl+Alt+F`, `Ctrl+Shift+W`. [Global](./Features.md#global-active-in-every-context):
`Ctrl+Shift+O` (Document Map).

**Data / settings keys.** `spelling/checkAsYouType`, `spelling/language`,
`spelling/customDictionaries`, `spelling/ignoreUppercase`, `spelling/ignoreUrls`,
`documents/convertWarnOnFeatureLoss`, `documents/keepFixedRenditionUntilFinalized`,
`documents/trackChangesOnByDefault`, `documents/reviewerColour`, `documents/showFormattingMarks`,
`documents/defaultStyleSet`.

**Cross-cutting additions.** Commands: `EditTextRun`, `SetParagraphFormat`, `ApplyStyle`,
`InsertPageBreak`, `InsertTable`, `EditTable`, `InsertObject`, `SetTextWrap`, `InsertToc`,
`UpdateToc`, `InsertFootnote`, `InsertCrossReference`, `AcceptRevision`, `RejectRevision`,
`ConvertToWritable`, `PreventFurtherEdits`, plus the Review-mode set `EditTextInPlace`,
`ReplaceImage`, `MoveObject`, `ResizeObject`, `SetObjectZOrder`, `GroupObjects`, `EditArticleThread`.
Typing commands **coalesce**, so a burst of keystrokes is one undo step. i18n: `write.*`,
`styles.*`, `table.*`, `toc.*`, `revisions.*`, `spelling.*`.

**Algorithms & notes.**
- **Conversion is the whole stage's risk.** It consumes Stage 5's `LayoutAnalyzer` output plus the
  tag tree, and its accuracy on untagged documents is bounded by that analyzer's measured
  precision/recall — which is *why* Stage 5 measured it.
- **The fidelity report is a feature, not an apology.** Users tolerate an imperfect conversion they
  were warned about; they do not tolerate silent corruption. The report is shown *before* the user
  commits.
- **The original fixed rendition is retained** until *Prevent Further Edits*, so the conversion is
  reversible. That is why `Ctrl+Alt+F` confirms first — it is the one genuinely one-way action in
  the product.
- Line breaking is Knuth–Plass-style with penalties for widows, orphans, and keep-with-next.
- Table formulas share the Stage 9 dependency-graph and topological-sort code; there is no reason
  for two expression evaluators in one product.

**Testing.**
*Automated:* `tst_styles`, `tst_linebreak` (golden line-break positions per paragraph fixture),
`tst_table` (structure ops, sizing, sort, formulas), `tst_changetracking` (accept/reject one, all,
by reviewer), `tst_spell`, `tst_convert_fidelity` (a labelled corpus scored on paragraph, column, and
table detection — **tracked as a number over time, not a pass/fail**), `tst_roundtrip_reflow`
(fixed → reflow → fixed renders within the golden threshold of the original);
**`tst_reflow_convergence`** — **the measurement decision O2 rests on**: convert → compose → convert
*again with no edits in between*, and diff model₂ against model₁ across the corpus. Since the model
is **not** persisted, a converter that does not converge means a user's document silently restructures
every time they reopen it; and because compose now emits a structure tree, round 2 reads those tags
rather than re-inferring from glyphs, which is precisely what this case has to prove works. Plus
reversibility for all 23 commands including coalesced typing.
*Manual:* → queued **`Stage13-WriteMode-Testing-Guide.md`**. Emphasis: convert real-world documents
of every kind — a tagged Word export, an untagged scan-derived file, a multi-column journal article,
a spreadsheet-heavy report — and score each conversion by hand; the twelve chord collisions verified
by switching modes with the same key; IME input for CJK.

**Exit criteria.** Convert to Writable produces an editable document with an honest fidelity report;
**compose emits a structure tree derived from the reflow model, and `tst_reflow_convergence` is green
over the corpus (O2)**;
text, paragraph, style, and page-layout editing all work; tables including formulas work; TOC
generates and updates; change tracking records and accepts/rejects; Prevent Further Edits produces a
fixed PDF that renders within the golden threshold of the composed layout; **every one of the twelve
documented chord collisions resolves correctly on mode switch**; all Write-mode edits undo.

**Deferrals.** Convert to Writable Excel (Stage 16). Equations, SmartArt-style diagrams, and index
generation. Merging revisions from two writable documents.

---

### Stage 14 — Batch, CLI, and preferences completion

**Status:** ⚪ **Not started.**

**Objectives.** [`Features.md` §5.11, §9.5–§9.7](./Features.md#511-batch-processing): batch
sequences, the full command line, logging and diagnostics, and the complete Preferences set.

> ⤵ **Read Aloud and the accessibility pass were removed from this stage on 2026-08-23** — see
> [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23).
> **Full keyboard operability stays here**: it is retained product behaviour, and it is the one part
> of the old accessibility bullet that was never an accessibility feature.

**Components & responsibilities.**
- `core/batch/{Action,ActionSequence,SequenceStore,BatchRunner,BatchReport,WatchedFolder}` —
  `BatchRunner::run(const ActionSequence&, const Inputs&, ProgressSink&) -> BatchReport`.
- `app/CommandLine` (full verb set, JSON report, exit codes).
- `ui/dialogs/{BatchEditorDialog,BatchReportDialog,DiagnosticsDialog}`, all remaining
  Preferences pages, `ui/panes/LogPane` (complete).
- Keyboard pass across `ui/`: full keyboard operability and visible focus on every control.

**Key bindings handled** — none. **Every batch action reuses an existing `ActionId`**, which is the
point. *(`Ctrl+Shift+Y` for Read Aloud was removed 2026-08-23.)*

**Data / settings keys.** `batch/defaultSequence`, `batch/continueOnError`, `batch/outputPattern`,
`batch/collisionPolicy`, `batch/watchedFolders`, `batch/maxParallel`,
`reading/voice`, `reading/rate`, `reading/volume`, `reading/highlightSpoken`,
`reading/readingOrder` (tags | heuristic), `reading/reduceMotion`, `reading/highContrast`,
`logging/collectDiagnostics`, `general/safeMode`.

**Cross-cutting additions.** No new commands — **and that is the stage's most important property.**
If batch needed a new command for anything, it would mean a feature had been implemented in a widget
rather than behind an `ActionId`, and Stage 1's discipline had failed. `tst_batch_coverage` asserts
every mutating `ActionId` is reachable from a sequence. i18n: `batch.*`, `cli.*`, `reading.*`,
`diagnostics.*`.

**Algorithms & notes.** The batch runner drives the **same** `ActionRegistry` and `CommandJournal`
as the UI, with no window constructed — flow K in the file map. Watched folders debounce on file
settling (size stable across two polls) to avoid grabbing a half-copied file. Dry-run runs every
command's `apply` against a scratch copy and reports the diff without saving.

**Testing.**
*Automated:* `tst_sequence` (serialize/deserialize, parameter prompting), `tst_batchrunner`
(continue-on-error, dry-run, cancellation, parallelism), `tst_batch_coverage` (**every mutating
`ActionId` is reachable from a sequence — the standing proof that the action layer is real**),
`tst_cli` (every verb, exit codes, JSON schema), `tst_watchedfolder` (debounce), and a keyboard audit
test asserting every interactive widget is reachable by `Tab` in visual order.
*Manual:* → queued **`Stage14-BatchCli-Testing-Guide.md`**. Emphasis: a full keyboard-only pass
through the entire application; a 1,000-file batch run; a watched folder fed by a slow network copy.

**Exit criteria.** Sequences build, save, share, and run over files, folders, trees, watched folders,
and open documents; **every mutating action is reachable from both a sequence and the CLI**; the
whole application is operable by keyboard alone; the diagnostics bundle contains what support needs
and no document content.

**Deferrals.** Scheduled sequences (needs a service; Stage 15's installer work).

---

### Stage 15 — Packaging, licensing, deployment, release gate

**Status:** ⚪ **Not started.** Delivers **v1.0**.

**Objectives.** [`Features.md` §9.4, §9.8, §9.9](./Features.md#98-licensing-activation-and-updates):
installers, shell integration completion, activation, updates, administrative policy, telemetry, and
the [compliance release gate](./Tech-Stack-Plan.md#83-release-gate-must-all-be-ticked).

**Components & responsibilities.**
- `installer/` — WiX v4 MSI with per-feature selection, plus a per-user MSIX; `windeployqt
  --compiler-runtime` integration keeping the Qt **and** GCC runtime DLLs beside the executable and
  replaceable. WiX packages a file tree and is indifferent to which compiler produced it.
- The **symbol archive** for this release: DWARF is not PDB, so the unstripped binaries are archived
  by build ID and the crash reports symbolized against them
  ([`Tech-Stack-Plan.md` §6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management)).
- `platform/win/WinShellIntegration` (complete: thumbnail provider, preview handler, index filter,
  jump list, taskbar progress, context-menu verbs).
- `core/licensing/{Entitlement,ActivationClient,OfflineActivation}`.
- `core/update/{UpdateChecker,DeltaUpdater}`.
- `core/settings/PolicyOverlay` (complete) + an ADMX-style policy template.
- `core/telemetry/{EventSink,EventCatalog}` — opt-in, off by default.
- `ui/dialogs/{ActivationDialog,UpdateDialog,AboutDialog,ThirdPartyNoticesDialog}`.

**Key bindings handled** — none new.

**Data / settings keys.** `updates/channel`, `updates/checkIntervalHours`, `updates/autoDownload`,
`updates/disabled` (policy-lockable), `general/telemetryOptIn`, `general/portableMode`,
`licensing/entitlement`, `licensing/seat`.

**Cross-cutting additions.** No new commands. i18n: **every** catalog completed and reviewed for
`en`, `zh-TW`, `zh-CN`, `ja`; `i18n-lint` requires 100% coverage for a release build, which is when
D6 stops being a convention and becomes a ship blocker.

**Algorithms & notes.** Delta updates diff against the installed version and fall back to a full
download on mismatch. Offline activation is a signed request/response file pair with no network at
any point. Portable mode redirects every user-data path to a folder beside the executable, which is
tested by running from a read-only location and confirming a clear diagnosis rather than a crash.

**Testing.**
*Automated:* installer smoke tests (install, launch, uninstall leaves no orphan registration, and
per-feature selection works); `tst_activation` (online, offline, deactivate, expiry, trial);
`tst_update` (delta apply, fallback, signature verification); `tst_policy` (a locked key cannot be
changed and reports which policy locked it); `tst_telemetry` (**off by default; no document content,
file name, or path ever appears in a payload**).
*Manual:* → queued **`Stage15-Packaging-Testing-Guide.md`**. Emphasis: clean-VM install; upgrade
over a previous version; silent enterprise deployment with a policy template; **the compliance
release gate walked item by item, including physically replacing a Qt DLL with an independently
built one and confirming the app still launches**.

**Exit criteria.** MSI and MSIX install, upgrade, and uninstall cleanly on a clean VM; shell
integration works in Explorer; activation works online and offline; updates apply; the policy
template locks settings; telemetry is off by default and provably content-free; **every item in the
Tech-Stack-Plan §8.3 release gate is ticked**; all four locales are 100% translated.

**Deferrals.** Everything in Stage 16. macOS and Linux packaging (the `platform/` boundary makes
this a future project, not a rewrite).

---

### Stage 16 — Post-v1 subsystems

**Status:** ⚪ **Not started.** Post-v1. The items are **independent of each other** and can be
scheduled by demand rather than by dependency. Each is sized as its own mini-stage with its own
exit criteria; they are grouped here because none of them reshapes the core.

| Item | Feature source | Principal components | Notes |
|---|---|---|---|
| **OCR** | [§2.1](./Features.md#21-create-a-pdf) | `core/ocr/OcrPipeline`, Tesseract behind `IOcrEngine` | Searchable-image and editable-text modes. Search picks up the OCR layer with no change to Stage 5. |
| **Scanner** | [§2.1](./Features.md#21-create-a-pdf) | `platform/win/WinScanner`, `ui/dialogs/ScanDialog` | ADF, duplex, deskew, despeckle, blank-page drop. Needs physical hardware to verify. |
| **Virtual printer** | [§2.4](./Features.md#24-office-and-printer-integration) | `platform/win/WinPrinterDriverService`, a print monitor | The heaviest OS integration in the product; ships as a separate installer feature. **The likeliest place risk R8 bites**: the WDK's print-driver build rules are MSVC-shaped, so this component may have to be built with a second toolchain and talk to us across the platform layer's existing C/COM boundary. Decide when it is scheduled, on a spike, not now. |
| **Office add-ins** | [§2.4](./Features.md#24-office-and-printer-integration) | `platform/win/WinOfficeBridge`, five add-in projects | Word/Excel/PowerPoint/Visio/Outlook ribbons. COM automation without ATL/`#import` means hand-written interface plumbing under GCC — more code, no architectural change (R8). |
| **Browser extensions + web capture** | [§2.1](./Features.md#21-create-a-pdf) | `platform/win/WinBrowserBridge`, three extension projects | Native-messaging host plus link-depth crawling. |
| **PDF Portfolios** | [§2.2](./Features.md#22-pdf-portfolios) | `core/portfolio/*`, `ui/dialogs/PortfolioDialog` | A container format with its own navigation model. |
| **Catalog full-text index** | [§1.8](./Features.md#18-search-and-find-) | `core/text/CatalogIndex` implementing `IIndexer` | First-party inverted index — **risk R2**; Xapian is excluded by license. |
| **Multimedia & 3D** | [§3.4](./Features.md#34-rich-media-and-3d) | `core/media/*`, `core/three/*`, U3D/PRC loaders | The largest post-v1 item; 3D is effectively its own renderer. |
| **Shared review & collaboration** | [§4.3](./Features.md#43-sharing-and-collaboration) | `core/review/*`, a sync transport | Needs a server-side decision that does not exist yet. |
| **AIP / rights management** | [§7.8](./Features.md#78-secure-delivery) | `platform/win/WinRightsManagement` | Enterprise labels, tracking, revocation. |
| **AI assistant** | [§1.7](./Features.md#17-ai-assistant) | `core/ai/*` behind a provider interface | Must be disableable with a *provable* no-transmission guarantee and a per-document opt-out. |
| **Convert to Writable Excel** | [§3.1](./Features.md#31-the-dual-mode-model-) | `core/reflow/TableExtractor` | Depends on Stage 13's converter quality. |
| **PDF/X profiles, EPUB export** | various | — | Small, independent. |

**Testing.** Each item carries its own unit suite and, where it touches hardware or the OS, its own
manual guide. The scanner, virtual printer, and Office add-ins **cannot** be verified without real
hardware and real installed Office; that constraint is stated up front rather than discovered.

**Exit criteria.** Per item, defined when it is scheduled.

---

## 5. Per-stage testing guides

**Every stage gets one guide, written immediately after that stage's plan and before its first line
of code.** Sequence, per stage: plan (§4, optionally expanded into a `Stage<N>-<Area>-Plan.md` — see
below) → guide → implement → run the guide. Writing the guide from
the plan is what keeps verification independent of the implementation; written afterwards it only
ever confirms what was built, including the parts that were built wrong.

Each guide carries **two sections**, per §2:

- **Automated** — the exact commands, with pass conditions, that the implementer runs: `checks`, the
  stage's `ctest` suites plus all prior ones, golden and round-trip where they apply,
  `portable-asan` / `portable-fuzz`, and any `tests/bench` figure the stage owes.
- **Manual** — numbered cases (ID · Goal · Pre · Steps · Expect · If-it-fails) for what cannot run
  headless, walked by a person on real hardware and signed off. Anything automatable belongs in the
  section above; this one is the residue and should shrink over the project's life.

Stages whose surface is entirely headless still get a guide — its manual section is short or empty,
and that is a finding worth recording rather than a reason to skip the file. **Stages 0 and 1 have theirs**, each
written from its plan before its code; every other row below is a schedule, not a backlog of missing
documents.

**A stage's §4 entry may also be expanded into a `Stage<N>-<Area>-Plan.md` execution document** when
its task-level detail would swamp this file — [`Stage0-Toolchain-Plan.md`](./Stage0-Toolchain-Plan.md)
and [`Stage1-AppShell-Plan.md`](./Stage1-AppShell-Plan.md) are the two that exist. Such a document owns **only** that stage's
task ordering and its stage-local design choices; the objectives, components, and exit criteria stay
here, and the stage's live status stays in its §4 status block. It sits between plan and guide in the
sequence: **§4 entry → `Stage<N>-<Area>-Plan.md` (optional) → testing guide → implement → run the
guide.**

| Stage | Guide | State |
|---|---|---|
| 0 — Toolchain & gates | [`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md) | ✅ **Written 2026-08-17** from [`Stage0-Toolchain-Plan.md`](./Stage0-Toolchain-Plan.md), before any Stage 0 code · **not yet run** — no case has a result. 40 automated cases (A-01…A-73), 7 manual (M-01…M-07): the clean-machine bootstrap and the **real** UCRT64 wrong-compiler configure. Carries the **A5/A6** verdicts and keeps **A7** settled |
| 1 — App shell | [`Stage1-AppShell-Testing-Guide.md`](./Stage1-AppShell-Testing-Guide.md) | ✅ **Written 2026-08-19** from [`Stage1-AppShell-Plan.md`](./Stage1-AppShell-Plan.md), before any Stage 1 code · 🟡 **partly run — T1…T13 verified 2026-08-24**. 68 automated (A-01…A-85, plus **A-67** added at T7), 12 manual (M-01…M-12); the manual half is large by design, since A3 is judged on a real screen. Carries the **A3** ribbon go/no-go, **recorded**. Twenty-five issues, **none open** (I-04 closed at T8; I-12…I-16 at T8/T9; **I-17** by the first `portable-asan` run to reach Stage 1's code; **I-18…I-20** by T10's first run against a real, shown window; **I-24 and I-25 at T13, both of them the harness rather than the product** — **CI now green at `gates` 45/45 and `portable-asan` 177/177**, run 32649932954, with the `windows` job still skipped for want of a registered runner, which is why **A-80 stays open**); **fifteen of the twenty were found by running something rather than by reading it**, including both S1s |
| 2 — Rendering | `Stage2-Rendering-Testing-Guide.md` | ⚪ Queued — **needs two monitors at different scaling**; carries the **A2** verdict |
| 3 — Mutation & save | `Stage3-Mutation-Testing-Guide.md` | ⚪ Queued — **needs a hex editor and a way to kill the process mid-edit**; carries the **A4** verdict |
| 4 — Viewing surface | `Stage4-ViewingSurface-Testing-Guide.md` | ⚪ Queued |
| 5 — Text, selection, search | `Stage5-TextSearch-Testing-Guide.md` | ⚪ Queued — near-entirely automated; the manual section covers selection feel and RTL/CJK rendering only |
| 6 — Annotations | `Stage6-Annotations-Testing-Guide.md` | ⚪ Queued |
| 7 — Assembly | `Stage7-Assembly-Testing-Guide.md` | ⚪ Queued |
| 8 — Save & export | `Stage8-SaveExport-Testing-Guide.md` | ⚪ Queued |
| 9 — Forms | `Stage9-Forms-Testing-Guide.md` | ⚪ Queued |
| 10 — Security | `Stage10-Security-Testing-Guide.md` | ⚪ Queued — **needs an independent extraction tool and a hex editor in the rig** |
| 11 — Signatures | `Stage11-Signatures-Testing-Guide.md` | ⚪ Queued — **needs a physical smart card or USB token** |
| 12 — Creation & conversion | `Stage12-Creation-Testing-Guide.md` | ⚪ Queued — near-entirely automated; the manual section is fidelity spot-checks against source documents |
| 13 — Write mode | `Stage13-WriteMode-Testing-Guide.md` | ⚪ Queued — the largest guide; hand-scored conversion fidelity |
| 14 — Batch, CLI | `Stage14-BatchCli-Testing-Guide.md` | ⚪ Queued — a full keyboard-only pass over the whole application |
| 15 — Packaging | `Stage15-Packaging-Testing-Guide.md` | ⚪ Queued — **needs a clean VM**; walks the compliance gate |
| 16 | per item | One per item, written when that item is scheduled |

---

## 6. Milestone map

| Milestone | Stages | Outcome |
|---|---|---|
| **M0 — Foundation** | 0–3 | Shell, ribbon, action layer, settings, i18n; open and render a PDF at correct DPI; one reversible, journalled, incrementally-saved, crash-recoverable mutation. **Every locked decision proven.** |
| **M1 — Reader** | 4–5 | A genuinely usable PDF reader: all view modes, panes, split, full-screen, selection, copy, find, cross-file search, comparison. *Internal alpha.* |
| **M2 — Reviewer** | 6–8 | Annotate, assemble, and get output back out: full markup and comment management, all page and document assembly, Save-As, export, optimize. *Private beta.* |
| **M3 — Professional** | 9–11 | Forms, encryption, permissions, redaction, sanitize, digital signatures. *Feature-complete for the professional use case.* |
| **M4 — Author** | 12–13 | Create PDFs from other sources; dual-mode Write/Review editing. *The signature capability lands.* |
| **M5 — Release** | 14–15 | Batch, CLI, installers, activation, updates, policy, compliance gate. **→ v1.0** |
| **M6 — Beyond** | 16 | OCR, scanner, virtual printer, Office add-ins, browser extensions, portfolios, catalog index, multimedia and 3D, collaboration, AIP, AI assistant. |

---

## 7. Traceability — every catalog section to its stages

| `Features.md` section | Stages |
|---|---|
| §1.1 Workspace | 1 (ribbon, QAT, tabs, sidebar, tool finder, themes), 4 (panes, window arrangement, session restore) |
| §1.2 Page display | 2 (layout modes, zoom, fidelity controls), 4 (scroll modes, split, reading/full-screen/presentation) |
| §1.3 Rulers, grid, guides | 4 |
| §1.4 Navigation | 2 (page controls, zoom, view history), 4 (thumbnails, bookmarks, destinations), 5 (comment/field/result/difference navigation) |
| §1.5 Document comparison | 5 |
| ~~§1.6 Read Aloud~~ | ⤵ **withdrawn 2026-08-23** — see [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23) |
| §1.7 AI assistant | 16 |
| §1.8 Search and find | 5 (find, advanced search, per-document index), 16 (catalog index) |
| §2.1 Create a PDF | 12 (blank, files, clipboard, region), 16 (scanner, OCR, web capture, virtual printer) |
| §2.2 PDF Portfolios | 16 |
| §2.3 Batch conversion | 12 (profiles), 14 (batch engine, CLI, watched folders) |
| §2.4 Office & printer integration | 16 |
| §2.5 Save and export | 3 (incremental save), 8 (Save-As, export, optimize, flatten, conformance) |
| §3.1 Dual-mode model | 13 |
| §3.2 Review-mode editing | 6 (typewriter), 13 (text, objects, article threads), 3 (metadata) |
| §3.3 Write-mode editing | 13a (text, paragraph, styles, page layout, spelling), 13b (tables, illustrations, TOC, revisions) |
| §3.4 Rich media and 3D | 16 |
| §4.1 Annotation tools | 6 (all), 10 (redaction marks' apply path) |
| §4.2 Comment management | 6 |
| §4.3 Sharing and collaboration | 16 |
| §5.1–5.2 Pages & thumbnails | 3 (rotate), 4 (thumbnail navigation), 7 (everything else) |
| §5.3 Merge | 7 |
| §5.4 Bookmarks | 4 (navigate), 7 (edit, generate, TOC page) |
| §5.5 Tags & document structure | 7 (tag tree, autotag), 13 (Write mode's structure, decision O2). *Accessibility removed 2026-08-23; the structure it sat on is retained for Write mode.* |
| §5.6 Layers | 7 |
| §5.7 Headers, footers, Bates | 7 |
| §5.8 Watermarks | 7 |
| §5.9 Imposition | 7 |
| §5.10 Links and destinations | 7 |
| §5.11 Batch processing | 14 |
| §6 Forms | 9 |
| §7.1–7.4 Protection & schemes | 10 |
| §7.5 Digital IDs and trust | 11 |
| §7.6–7.7 Signing and validation | 11 |
| §7.8 Secure delivery | 11 (envelopes, password-and-email), 16 (AIP) |
| §7.9 Removing sensitive content | 10 |
| §8 Key bindings | 1 (Global, the binding store, context dispatch), then each stage wires its own context's table |
| §9.1 Preferences | 1 (shell + General + Shortcuts), each stage adds its page, 14 (completion) |
| §9.2 Persistence | 1 (settings, recent), 3 (recovery journal), 4 (session state), 6 (profile store) |
| §9.3 File formats | 2 (read + repair), 8 (write + conformance), 12 (import) |
| §9.4 OS integration | 1 (association, single instance), 8 (printing), 15 (shell handlers, jump list), 16 (printer driver, Office, browsers, scanner) |
| §9.5 Logging & diagnostics | 0 (`Log.h`), 1 (log pane), 14 (diagnostics bundle, safe mode) |
| §9.6 Command line | 1 (open-with-file), 14 (full verb set) |
| §9.7 Display & input | 2 (DPI, monitors), 4 (touch/pen), 14 (keyboard operability, focus visibility) |
| §9.8 Licensing & updates | 15 |
| §9.9 Telemetry | 15 |
| §10 Portability note | 1 (all **seventeen** interfaces declared — the figure was carried as fifteen until Stage 1 T7 measured it), then each stage implements the Windows backend it needs |

---

## 8. Open decisions carried forward

| # | Decision | Owner stage | Why it is not decided now |
|---|---|---|---|
| O1 | Office export/import engine: first-party writers, `IOfficeBridge` through the installed suite, or a commercial component for that subsystem only. (Risk R3.) | 8, confirmed in 12 | Should be decided on **measured** fidelity against a labelled corpus, not on preference. |
| ~~O2~~ | ~~Whether the Write-mode reflow model is persisted between sessions.~~ **Resolved: reconstruct on each *Convert to Writable*, and make `ReflowToFixedComposer` emit a structure tree** so the model's *structure* persists as **standard PDF tags** rather than as a private stream. | ✅ **Resolved 2026-08-20** | Persisting the model **inside** the PDF was rejected on security grounds: it would carry deleted text and reviewer identity in a container that Stage 10's sanitize/redaction path — which ships **three stages earlier** — knows nothing about, so *redact → save* would leave the redacted text recoverable. A **local cache** was rejected because the same file would then reconstruct differently depending on whether an invisible cache survived, and inconsistent results are harder to support than consistent slowness. Tag emission recovers most of the benefit interoperably: [flow H](./Architecture-FileMap.md#h-convert-to-writable--crossing-into-the-second-document-model) already consults `TagTree` for *"much better fidelity"*, so a second conversion reads our tags instead of re-guessing from glyphs. *(It also used to advance PDF/UA; that benefit lapsed with the 2026-08-23 accessibility removal, and the round-trip fidelity argument — always the primary one — is unaffected.)* **Accepted cost:** tracked changes have no PDF representation, so they cannot survive a close or be shared. If shareable Write-mode review is ever required, that is a **new** decision, not a reopening of O2. **13a still owes the convergence measurement** — see its status block. |
| O3 | Whether dynamic XFA gets more than honest reporting. (Risk R4.) | post-13 | Effectively a second document format; needs demand evidence before the investment. |
| O4 | AI assistant provider and whether an on-device model is offered. | 16 | Product and privacy decision; the interface keeps it open. |
| O5 | Collaboration transport (server product, third-party sync, or shared-folder only). | 16 | Needs a business decision that does not exist yet. |
| O6 | arm64 as a shipped target versus build-only. | 15 | Depends on PDFium and dependency availability, and on demand. **MinGW-w64 GCC's aarch64 support is immature (R8)**, so this is also a question of whether arm64 means switching that target to LLVM-MinGW. |
| O7 | Whether the render cache's disk tier is shared between documents or per-document. | 2, revisit at 5 | Wants real cache-hit data from the corpus. |
| O8 | Sequenced-run scheduling: an in-app timer versus an OS-level service. | 14/15 | A service changes the installer's privilege story. |
| ~~O9~~ | ~~Product name.~~ **Resolved: the product is RawPage.** | ✅ **Resolved 2026-08-20** | Settled by you, not deferred to Stage 15. The rename was carried out across the tree in the same change: **907 occurrences in 78 files**, three CMake modules renamed, `corpus/minimal.pdf` regenerated (its xref holds absolute byte offsets, so it could not be edited), and the GitHub repo and working folder renamed. Verified afterwards at 39/39 gate cases and **66/66 tests**, `rawpage.exe` exit 0, imports `msvcrt.dll` and never `ucrtbase.dll`. **The old estimate that a rename "touches only `res/`, `i18n/`, and the installer" was wrong** — it reached every layer, the build system, the scripts and CI. |
| O11 | **How a parameterized command reaches the action layer.** `ActionRegistry::dispatch(ActionId, KeyContext)` carries no argument, and T11 produced the first two commands that need one — `ui.qatAdd` and `ui.qatRemove`, which mean nothing without *which command*. Stage 1's answer is that the surface which raised the menu records the target (`QuickAccessToolbar::pendingTarget()`) and the handler reads it: enough to keep both reachable from a test and from a headless caller, and not enough for a batch script that never clicked anything. | **14** | Flow K is where it stops being avoidable — a batch step naming `pages.rotate` has to say *which pages, and by how much*, and there is no clicking involved. Deciding it now would be designing the batch runner's argument model a stage at a time from inside a toolbar. Related and deliberately not conflated: `file.open` raises a dialog and so is refused headlessly (I-23) — the parameterized form of that one already exists as `MainWindow::openFiles`. |
| O10 | How sanitizer coverage is obtained given that MinGW GCC has none: the `portable-asan`/`portable-fuzz` non-Windows jobs over `core/` + `pdf/cos/` (the plan of record), an `x64-clang-asan` preset over LLVM-MinGW, or both. | **2** | The portable route is cheap and also proves the layering claim, but it cannot reach `platform/win/` or `ui/`. Whether that gap is worth a second Windows toolchain should be settled on evidence from Stage 2's corpus runs, not upfront. **Stage 0 did its half** — both portable presets are wired and measured (`portable-asan` green at 52 tests, `portable-fuzz` configuring with zero targets) — and left the choice open by design, so the owner is now Stage 2. |

**A resolved decision keeps its row, struck through and dated, rather than being deleted** — the
same convention `Architecture-FileMap.md` uses for folded-away files. What was decided, when, and
what it actually cost is more useful to a later reader than a shorter table.
