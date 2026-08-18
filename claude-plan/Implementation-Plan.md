# Recto — Implementation Roadmap (Staged Build Plan)

## Context

This is Recto's **staged build plan** — the execution backbone and the living progress log. It is
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

`core/settings/Settings` over `%APPDATA%\Recto\settings.ini`, with `PolicyOverlay` layering
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
writes PDF bytes.** Four gates hold it, and all four run in CI on every push:

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
| **Unit (headless)** | All pure logic: page tree, outline, annotation store, form model and calculation graph, permissions, text layout analysis, selection, search, line breaking, table formulas, change tracking, signature *decisions* (mock `ICryptoProvider`), COS lexer/parser/filters/crypt, every command's inverse. Linked **without** `recto_ui` and `recto_platform`. | `tests/unit/` |
| **Golden-image render** | Rasterization fidelity. Corpus pages rendered at 96/144/192 DPI and compared to `golden/` with a perceptual threshold; a diff image is written on failure. Baselines are regenerated only through `tools/golden-update` with a stated reason — a rendering regression must never be laundered into a new baseline. | `tests/golden/` |
| **Corpus round-trip** | Save fidelity. Open → run a command → incremental save → reopen: the mutation is present, untouched objects are **byte-identical**, and a pre-existing signature still validates. Run over the whole corpus including encrypted, signed, tagged, XFA, and malformed files. | `tests/roundtrip/` |
| **Fuzzing + sanitizers** | Parser robustness — a security property, not a quality one. libFuzzer over `pdf/cos/Lexer` and `pdf/cos/Parser`, seeded from `corpus/malformed/`, plus ASan+UBSan over the `core/` and `pdf/cos/` unit suites. 10 minutes clean before every release; nightly long runs. **Neither builds under MinGW GCC** (no sanitizer runtime, no libFuzzer), so both run on the `portable-asan` / `portable-fuzz` non-Windows presets over exactly the two layers that are portable by rule — see [`Tech-Stack-Plan.md` §6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management) and risk R7. Anything they find is reproduced in the Windows `x64-debug` build before it counts as fixed. | `tests/fuzz/` |
| **Per-stage testing guide** | One guide per stage, carrying **both** halves of that stage's verification: an **automated** section — the exact commands over the four layers above, with pass conditions, run by the implementer — and a **manual** section for what a person must see on a real machine: window behaviour, multi-monitor and mixed-DPI, input and tools, shell integration, printing, scanning, signing with a physical token. **Written immediately after the stage's plan and before the stage's code** — see §5. | `Stage<N>-<Area>-Testing-Guide.md` |

Plus **`tests/bench/`**, which is not pass/fail but gates a decision: per-tile render latency at
each DPI over the corpus, settling risk A2.

### Definition of Done — every stage, no exceptions

1. Code complete for the stage's Objectives.
2. `cmake --build --target checks` clean — all four gates.
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

**Status:** 🟡 **Automated half done — **all 15 tasks complete, automated verification green, awaiting the manual pass**. Both verdicts recorded: A5 and A6.**

Both planning artifacts were written first, in the §5 order:
[`Stage0-Toolchain-Plan.md`](./Stage0-Toolchain-Plan.md) (execution plan — tasks T1–T15, the
Stage-0-local decisions D0-1…D0-8, and the §7 precondition checklist) and
[`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md) (written **from** that plan,
**before** any code). **No guide case has been formally run. The A5 verdict is recorded (T6); A6 is still open.** Preconditions V-1…V-4 verified 2026-08-17 — vcpkg at `C:\Users\squirrel\vcpkg`, baseline
`94a5411977…`, community triplet `x64-mingw-dynamic` present.

| Task | State | Outcome and deviations |
|---|---|---|
| **T1** repository skeleton | ✅ 2026-08-17 | `.gitignore`, `.gitattributes`, `scripts/env.ps1`, `scripts/bootstrap.ps1`, root `README.md`. `env.ps1` yields cmake 3.30.5 · ninja 1.12.1 · g++ 13.1.0 (`x86_64-w64-mingw32`), resolves `g++` to the pin, and sets `VCPKG_ROOT` in-process — retiring the "setx does not reach a running process" problem. **Deviation:** `claude-plan/skills-specs-generation` had been committed as a gitlink with no `.gitmodules` — an accidental submodule that would have failed a fresh clone and so **case M-01**; untracked and ignored instead |
| **T2** target graph | ✅ 2026-08-17 | Root + five layer `CMakeLists.txt`, `RectoWarnings.cmake`, `RectoQt.cmake`, four placeholder TUs, `app/main.cpp`. Five TUs compile clean under `-Werror`; four static libs and `recto.exe` link; the exe **runs, exits 0**, subsystem **Windows GUI**, imports **`msvcrt.dll`, never `ucrtbase.dll`**. `recto_link_qt()` **fails configure on `Qt6::Charts`** with the GPL-3.0 diagnostic. **Deviations:** (a) `-municode` deferred to Stage 1 — measured, `-mwindows -municode` with a plain `main()` fails to link (*undefined reference to `wWinMain`*), and inventing a stub to satisfy a flag is worse than waiting for the real entry point; (b) `RectoQt.cmake` pulled forward from T7, because the wrapper must exist before the *first* Qt link; (c) guide case **A-41** corrected — see the note there |
| **T3** toolchain pin | ✅ 2026-08-18 | `cmake/toolchain-mingw.cmake` + `cmake/ToolchainAssert.cmake`, TA-1…TA-9. Clean configure reports *"Toolchain pin verified: GCC 13.1.0 x86_64-w64-mingw32, SEH/posix/msvcrt, no GCC plugin"*; `compiler-plugins.txt` written empty. **Six negative tests pass** (`tests/gates/fixtures/wrong-compiler/`, WC-1…WC-6, pulled forward from T9 and run by hand until T9 wraps them in CTest): wrong version (**TA-2**), `_UCRT` defined (**TA-6**), `__MSVCRT__` undefined (**TA-6** cannot pass vacuously), bad `RECTO_MINGW_ROOT`, injected `-fplugin`, and `-static-libstdc++` — each failing configure with its own diagnostic. **Deviations:** (a) `CMAKE_SYSTEM_NAME` is **not** set, against the plan's §5.1 sketch — setting it in a toolchain file switches CMake into cross-compiling mode, disabling `try_run()` and altering `find_package()`, for no benefit on a native build; (b) the `-fplugin` scan is **duplicated** into the toolchain file, because a plugin that fails to load breaks GCC's own compiler probe *before* `project()`, and CMake then reports "the compiler is broken" instead of the licence consequence; (c) **TA-6's probe now compiles with `CMAKE_CXX_FLAGS`** — writing WC-2 exposed that the original bare-compiler probe let `-DCMAKE_CXX_FLAGS=-D_UCRT` configure cleanly and report "msvcrt" while the whole build compiled as UCRT, i.e. the assertion was interrogating a compilation the project never performs. **Still untested:** TA-1, TA-3, TA-4, TA-5, TA-7 (each needs a differently *built* GCC) and TA-8 (a second Qt) — recorded in the fixture README, and covered by manual case **M-02** |
| **T4** presets | ✅ 2026-08-18 | `CMakePresets.json`, version 6: `x64-debug`, `x64-release`, `arm64-release`, `portable-asan`, `portable-fuzz`, plus build and test presets. vcpkg is outermost with the pin chainloaded via `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` — verified working before `vcpkg.json` exists (manifest mode simply stays off until T6). `hostSystemName` conditions hide the Linux presets on Windows and vice versa. **Windows presets fully verified**: configure, build, and `recto.exe` exit 0; `arm64-release` exits 1 with the O6 explanation. **Deviations:** (a) the toolchain pin and TA-8 are **skipped** when `RECTO_PORTABLE_SUBSET=ON` — a Linux gate build compiles the same sources with a different compiler on a different OS, which is the entire point, so asserting "exactly GCC 13.1.0, SEH, msvcrt" there would reject the configuration we are trying to prove; (b) `arm64-release` **fails at configure** rather than configuring, and guide case **A-03** was tightened from "succeeds or fails explicitly" to one expected outcome. **Untested:** `portable-asan` and `portable-fuzz` are JSON-schema-valid (`--list-presets` parses the whole file) but **have never been run** — there is no Linux here. They are first exercised by CI at T13, and that is where a mistake in them will surface |
| **T5** vcpkg baseline | ✅ 2026-08-18 | `vcpkg-configuration.json` pins baseline `94a541197763a4f449a1b91478df48c0584a6256` and registers `tools/ports/` as an overlay directory (created, empty, with the rule written down: a port that builds only under MSVC is vendored there, **never** worked around by changing the triplet) |
| **T6** manifest + **A5** | ✅ 2026-08-18 | `vcpkg.json` declares every §6.2 vcpkg dependency across five features. **A5 verdict recorded** in [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified): all five groups **resolve** on `x64-mingw-dynamic`; no MSVC-only port; no overlay needed. The default set was **really built** — catch2 3.15.3 → `libCatch2.a`, installed and consumed through the preset. **Deviations:** (a) D0-3 is applied more broadly than written — *everything* except the test harness sits behind a feature, because making OpenSSL a default dependency would couple Stage 0's build to a Stage 10 need and let a Stage 10 port failure block Stage 1; (b) `VCPKG_HOST_TRIPLET=x64-mingw-dynamic` added to the presets, since the default `x64-windows` host triplet resolves MSVC — present here, absent by design in **M-01**. **Not closed:** resolution ≠ compilation. `codecs`, `text`, `crypto`, `ocr` have never been compiled; ICU and OpenSSL are the known MinGW risks, and each group closes when its stage switches the feature on |
| **T7** layer-lint | ✅ 2026-08-18 | `tools/layer-lint/` (RuleSet · IncludeGraph · LayerLint · main) plus `tools/common/Json.h`, all standard-library-only per D0-1. **Clean on the real tree** (13 files, 38 includes) and **six fixtures verified by hand** in `tests/gates/fixtures/layer-lint/`: `core/`→`pdf/`, `ui/`→`pdf/`, `FPDF_` outside `pdf/`, a write outside the allowed layers, `<QtWidgets/QWidget>` in `core/` — each exit 1 with `file:line: rule: detail` — plus **`write-escaped`, a negative control proving the inline waiver works** (exit 0). `--explain` prints the graph, the rules, the exclusions, and every live escape; a missing rules file exits **2**, not 1. **Deviations:** (a) external includes are checked by **deny-list, not allowlist** — an allowlist would have to enumerate the whole standard library and would fail closed on every unlisted header, which teaches people to edit the rules file to get work done; the linker enforces the remainder, since `core/` links only `Qt6::Core`; (b) `tools/common/Json.h` is a new shared file the plan did not name — the gates cannot use Qt's JSON (D0-1) and T8 must read `vcpkg.json`; (c) `core/settings/` is its own layer entry with longest-prefix-first matching, since it is the one part of `core/` permitted to write; (d) fixtures pulled forward from T9, which now inherits working cases. **`tools/layer-lint/` flipped ○ → ✅ in `Architecture-FileMap.md`** — the first row in the project to do so |
| **T8** license-audit | ✅ 2026-08-18 | `tools/license-audit/` (LicenseAudit · Inventory · main) + `spdx-map.json`. Reads **vcpkg's own SPDX SBOM** (`vcpkg.spdx.json`) for exact versions and concluded licences, falling back to the map; runs with **no installed tree at all**, because the CI gates job has no vcpkg. `THIRD-PARTY.md` generates. The allowlist appears in exactly one place. **Deviations:** (a) the plan's signature `audit(const VcpkgManifest&, const QtModuleList&)` is preserved, so the compiler-plugin re-check is a separate `checkCompilerPlugins()` called from `main`; (b) **findings are blocking or warning** — see the note below |
| **T9** checks + gate tests | ✅ 2026-08-18 | `cmake/RectoChecks.cmake` and `tests/gates/`. `--target checks` runs both gates and **depends only on the two gate executables, never the layer libraries** — the property that makes "gates before compile" real. **16 CTest cases, all passing**, via `RunGateCase.cmake`, which asserts **both** exit code and diagnostic: CTest's `WILL_FAIL` cannot tell exit 1 from exit 2 (a correct finding from a crashed gate), and `PASS_REGULAR_EXPRESSION` ignores the exit code entirely. Includes four **negative controls** — the escaped write, `GPL-3.0-or-later WITH GCC-exception-3.1`, `LGPL-3.0`, and an allowed Qt module set — without which every positive case is equally satisfied by a gate that rejects everything. **Extended after a completeness audit: 23 cases**, and to **25** when I-08 added the two `THIRD-PARTY.md` emission cases with their `EXPECT_FILE`/`EXPECT_ABSENT` assertions. The six `wrong_compiler` cases (TA-2, TA-6 both directions, bad root, `-fplugin`, `-static-libstdc++`) had been written and passing by hand at T3 but never wired, while the testing guide already named `ctest -R gates.wrong_compiler` as their command. They now run against a **minimal fixture project** rather than the real one: each case is a full CMake configure, and the real project pulls in vcpkg and Qt at ~200 s apiece, which would have put twenty minutes into a suite that otherwise runs in 3.5 s |
| **T10** core vocabulary | ✅ 2026-08-18 | `core/Result.h`, `Log.h`, `Geometry.h`, `Units.h` — header-only, QtCore-only, no OS and no engine. `Result<T>`/`Result<void>` with `RECTO_TRY`; `Error` carries a **catalog key, not a sentence**, since `core/` has no `tr()` context and must hold no user-facing English. `Log.h` enforces *"never write document text or file paths above debug level"* **by type**, via `Redacted<T>` — a code-review rule is enforced by whoever happens to review; a type is enforced by the compiler. `Geometry.h` keeps PDF space (y-up, points) and device space (y-down, pixels) as types with **no conversion between them at all** except through `PageTransform`, which must be handed zoom, rotation and physical DPI |
| **T11** unit harness | ✅ 2026-08-18 | `tests/unit/` on Catch2 3.15.3, registered with `catch_discover_tests`. **50 tests pass** (16 gate + 7 toolchain + 27 unit); **58** since I-08, I-10, I-11 and I-13 (18 gate + 13 toolchain + 27 unit). The **headless boundary is asserted at configure time, over the resolved link closure** — verified by adding `recto_ui` and watching configure refuse. `DL_PATHS` points CTest at the mingw runtime, or every test fails to *launch* with `0xC0000135`, which looks nothing like a test failure. **Deviation:** `recto_link_qt()` changed from `PRIVATE` to `PUBLIC` — `core/`'s public headers include `<QString>`, so Qt's include path is part of core's *interface*; with `PRIVATE`, every consumer fails to find QString and the tempting repair is to re-link Qt in each one, scattering the dependency the layer contract keeps in one place |
| **T12** PDFium spike — **A6** | ✅ 2026-08-18 | **A6 verdict recorded** in [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified). Pinned **PDFium 153.0.8009.0** (`chromium/8009`, non-V8, SHA-256 verified); `gendef` read 472 exports and `dlltool` produced the GNU import library upstream does not ship; the spike links from `g++`, opens `corpus/minimal.pdf`, and reports `pages=1`, `595.276 x 841.890 pt`. Imports are `msvcrt.dll`, **never `ucrtbase.dll`**. Durable outputs: `scripts/fetch-pdfium.ps1` (hash-pinned, idempotent) and `third_party/pdfium/README.md`; the spike source stays until Stage 2's facade replaces it. **C-1 done**: `corpus/minimal.pdf`, generated by `corpus/make-minimal.py` rather than hand-typed, because a PDF xref stores absolute byte offsets and a one-character edit invalidates every one of them. **Two findings:** (a) `pdfium.dll` imports **no C runtime at all** — it statically links its own — so there is no shared-CRT surface in either direction, a *stronger* result than A6 assumed; the consequence is that **memory allocated by PDFium must be freed by PDFium**, now a rule rather than a style note; (b) the prebuilt archive's `LICENSE` is the **packager's MIT**, not PDFium's BSD-3 text or its third-party notices, which §8.1 requires us to ship — carried to Stage 15 |
| **T13** CI | 🟡 2026-08-18 — **`gates` green; `windows` blocked on infrastructure** | `.github/workflows/ci.yml`, three jobs, `windows` and `portable-asan` both `needs: gates`. **First run: `gates` PASSED** — the enforcement gates run on a bare Linux runner with no Qt and no vcpkg, in seconds, before anything else starts. That validates D0-1 and the ordering guarantee. Writing the job also exposed a **false claim**: the root `CMakeLists.txt` called `find_package(Qt6 REQUIRED)` unconditionally, so the no-Qt job could not have existed; fixed with `RECTO_GATES_ONLY` and a `gates` preset, verified locally with Qt and vcpkg deliberately hidden (3.8 s, 23/23). **`portable-asan` failed and is fixed**: `ubuntu-latest` exports vcpkg as `VCPKG_INSTALLATION_ROOT`, not `VCPKG_ROOT`, so the preset's `$env{VCPKG_ROOT}` expanded to empty — `windows-latest` sets both, which is why it only failed on Linux. **`windows` failed for a cause outside our control** — see below |
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
> `RECTO_HAS_WINDOWS_RUNNER` so the workflow stays honest until a runner is registered — a
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
- `CMakeLists.txt`, `CMakePresets.json` — four layer targets (`recto_core`, `recto_pdf`,
  `recto_platform`, `recto_ui`) plus the `recto` executable; presets `x64-debug`, `x64-release`,
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
- `tests/unit/` — Catch2 harness, linked without `recto_ui` and `recto_platform`.
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

**Exit criteria.** Clean checkout → documented command → empty `recto.exe` builds; `checks` passes
on the real tree and fails on each bad fixture, Qt Charts included; `THIRD-PARTY.md` is generated,
contains no non-allowlisted license, and lists Qt 6.11.1 and the GCC runtime with its exception;
**A5 and A6 have recorded verdicts** — the vcpkg manifest resolves on `x64-mingw-dynamic` and the
PDFium DLL spike opens a file — and **A7 stays settled**: a mismatched compiler fails configure.

**Deferrals.** `mutation-lint` (needs `Command`, Stage 3); `i18n-lint` (needs `tr()` sites, Stage 1);
arm64 is configured but not validated on hardware, and per R8 may need LLVM-MinGW rather than GCC —
that is O6's problem, not Stage 0's. `portable-fuzz` is wired but has no targets until Stage 2 gives
it a lexer.

---

### Stage 1 — App shell, action layer, settings, single instance

*Part 1 of the [first milestone](./Tech-Stack-Plan.md#7-recommended-first-milestone-foundation-vertical-slice).*

**Status:** ⚪ **Not started.** Carries risk **A3** (can Qt Widgets host a real ribbon, tear-off
tabs, and auto-hiding panes without a commercial control library?) as an explicit **go/no-go**. If
it cannot, the fallback is a first-party ribbon widget — *not* a change of toolkit.

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
  `std::optional<ActionId> lookup(QKeySequence, KeyContext) const`,
  `void rebind(QKeySequence, KeyContext, ActionId)`, `QList<Shadow> shadows() const`.
- `core/settings/Settings`, `SettingsSchema` — `template<class T> T get(Key) const`,
  `void set(Key, T)`, `Signal<Key> changed`.
- `core/settings/RecentFiles` — `void note(DocumentId)`, `void pin(DocumentId)`, `void clear()`.
- `ui/shell/MainWindow`, `Ribbon`, `RibbonTabBuilder` (`void build(Ribbon&, const QJsonDocument&, ActionRegistry&)`),
  `QuickAccessToolbar`, `UiCustomization`, `ShortcutBinder` (`KeyContext activeContext() const`),
  `DocumentTabBar`, `DocumentWindow`, `SidebarDock`, `StatusBar`, `ToolFinder`, `ThemeManager`.
- `ui/panes/LogPane`, `ui/dialogs/PreferencesDialog` (shell + General + Shortcuts pages).
- `platform/platform.h` (all sixteen interfaces **declared**), `platform/win/PlatformFactory`,
  `WinSingleInstanceBus`, `WinShellIntegration` (file association only).
- `tools/i18n-lint/`, `i18n/recto_en.ts`, `recto_zh_TW.ts`.

**Key bindings handled** — from [`Features.md` §8 → Global](./Features.md#global-active-in-every-context)
(`KeyContext::Global`): `Ctrl+O`, `Ctrl+W`, `Ctrl+Q`, `Ctrl+Tab` / `Ctrl+Shift+Tab`,
`Ctrl+F6` / `Ctrl+Shift+F6`, `F4`, `F9`, `Alt` (key tips), `Ctrl+Shift+U`, `Ctrl+K`, `F1`.
Also `Ctrl+Z` / `Ctrl+Y` from [Common](./Features.md#common-all-contexts-unless-a-context-above-overrides-the-chord)
— **registered and dispatching, but with an empty journal until Stage 3.**

**Data / settings keys.** `general/theme`, `general/uiFontScale`, `general/touchMode`,
`general/restoreSession`, `general/singleInstance`, `language/uiLocale`,
`shortcuts/<context>/<action>`, `logging/level`, `logging/subsystems`, `logging/retainFiles`,
`documents/recentCount`, `documents/recentPinned`.

**Cross-cutting additions.** No commands yet. **i18n catalog groups introduced:** `shell.*`,
`ribbon.*`, `prefs.general.*`, `prefs.shortcuts.*`, `action.*` (one label per `ActionId`).

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
comments Stage 6, fields Stage 9, layers/tags Stage 7. AI assistant (Stage 16). Read Aloud (Stage 14).

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
  Recto highlight looks identical in Acrobat, Chrome, and a phone. This is the single biggest
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
- `ui/dialogs/{PageOpsDialogs,MergeDialog,WatermarkDialog,HeaderFooterDialog,BatesDialog,ImpositionDialog,LinkPropertiesDialog,AccessibilityReportDialog}`.

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
`RetagElement`, `SetAltText`, `AutoTag`, `ApplyWatermark`, `RemoveWatermark`, `ApplyHeaderFooter`,
`RemoveHeaderFooter`, `ApplyBates`, `RemoveBates`, `CreateLink`, `EditLink`, `DeleteLink`,
`CreateDestination`, `RenameDestination`, `DeleteDestination`. i18n: `pages.*`, `bookmarks.*`,
`layers.*`, `tags.*`, `decorate.*`, `links.*`.

**Algorithms & notes.**
- **Reordering pages must repair every reference to them:** links, bookmark destinations, named
  destinations, article threads, and structure-tree page references. A "move pages" that breaks
  bookmarks is the classic bug of this feature area, so `tst_reference_repair` runs after every page
  command, not only after `MovePages`.
- Watermarks, headers/footers, and Bates numbers are written into **marked content sections tagged
  with a Recto key**, which is what makes "remove the watermark I added" possible without guessing.
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
headers/footers apply, update, and remove cleanly; imposition output is correct on paper;
accessibility check reports and fixes real issues.

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
`tst_conformance` (known-good and known-bad PDF/A and PDF/UA files), `tst_export_office` (fidelity
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
calculated invoice with a deliberate cycle; tab through 200 fields; open a Recto-authored form in
another reader and fill it there.

**Exit criteria.** All eight field types are creatable with full properties; auto-detection produces
usable fields on a static PDF; calculations and validation work with a reported order; tab order
works all three ways; data round-trips in all four formats; a Recto-authored form fills correctly in
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
USB token**; sign in Recto and validate in Acrobat, and the reverse; a real timestamp authority; a
certifying signature followed by a permitted and a forbidden change.

**Exit criteria.** Signing works with a self-signed ID, a PKCS#12 file, an OS-store ID, and a
physical token; Recto-signed documents validate in at least one other reader and vice versa;
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
`ReflowToFixedComposer::compose(const ReflowDocument&, MutationSink&)`;
`LineBreaker::breakParagraph(const Paragraph&, const Constraints&) -> Lines`;
`TableModel::evaluate(CellRef) -> Value`; `ChangeTracker::accept(RevisionId)`.

**Key bindings handled** — the **whole** [Write mode](./Features.md#write-mode) table
(`KeyContext::WriteMode`), which is where the twelve deliberate collisions with the Viewer context
become real and testable: `Ctrl+B`, `Ctrl+I`, `Ctrl+U`, `Ctrl+D`, `Ctrl+E`, `Ctrl+K`, `Ctrl+L`,
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
(fixed → reflow → fixed renders within the golden threshold of the original), plus reversibility for
all 23 commands including coalesced typing.
*Manual:* → queued **`Stage13-WriteMode-Testing-Guide.md`**. Emphasis: convert real-world documents
of every kind — a tagged Word export, an untagged scan-derived file, a multi-column journal article,
a spreadsheet-heavy report — and score each conversion by hand; the twelve chord collisions verified
by switching modes with the same key; IME input for CJK.

**Exit criteria.** Convert to Writable produces an editable document with an honest fidelity report;
text, paragraph, style, and page-layout editing all work; tables including formulas work; TOC
generates and updates; change tracking records and accepts/rejects; Prevent Further Edits produces a
fixed PDF that renders within the golden threshold of the composed layout; **every one of the twelve
documented chord collisions resolves correctly on mode switch**; all Write-mode edits undo.

**Deferrals.** Convert to Writable Excel (Stage 16). Equations, SmartArt-style diagrams, and index
generation. Merging revisions from two writable documents.

---

### Stage 14 — Batch, CLI, accessibility, and preferences completion

**Status:** ⚪ **Not started.**

**Objectives.** [`Features.md` §5.11, §9.5–§9.7](./Features.md#511-batch-processing): batch
sequences, the full command line, Read Aloud, accessibility completion, logging and diagnostics, and
the complete Preferences set.

**Components & responsibilities.**
- `core/batch/{Action,ActionSequence,SequenceStore,BatchRunner,BatchReport,WatchedFolder}` —
  `BatchRunner::run(const ActionSequence&, const Inputs&, ProgressSink&) -> BatchReport`.
- `app/CommandLine` (full verb set, JSON report, exit codes).
- `platform/win/WinSpeech`, `ui/shell/ReadAloudBar`.
- `ui/dialogs/{BatchEditorDialog,BatchReportDialog,DiagnosticsDialog}`, all remaining
  Preferences pages, `ui/panes/LogPane` (complete).
- Accessibility pass across `ui/`: UI Automation names, roles, and values on every control; full
  keyboard operability; focus visibility; high-contrast; reduced motion.

**Key bindings handled** — [Viewer](./Features.md#viewer-review-mode): `Ctrl+Shift+Y` (Read Aloud).
No new chords otherwise: **every batch action reuses an existing `ActionId`**, which is the point.

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
command's `apply` against a scratch copy and reports the diff without saving. The reading order for
Read Aloud comes from the tag tree when present and from `LayoutAnalyzer` when not — the same
fallback as everywhere else.

**Testing.**
*Automated:* `tst_sequence` (serialize/deserialize, parameter prompting), `tst_batchrunner`
(continue-on-error, dry-run, cancellation, parallelism), `tst_batch_coverage` (**every mutating
`ActionId` is reachable from a sequence — the standing proof that the action layer is real**),
`tst_cli` (every verb, exit codes, JSON schema), `tst_watchedfolder` (debounce), and an accessibility
audit test asserting every interactive widget has an automation name.
*Manual:* → queued **`Stage14-BatchCli-Testing-Guide.md`**. Emphasis: a full keyboard-only pass
through the entire application; a screen reader reading a tagged and an untagged document; a
1,000-file batch run; a watched folder fed by a slow network copy.

**Exit criteria.** Sequences build, save, share, and run over files, folders, trees, watched folders,
and open documents; **every mutating action is reachable from both a sequence and the CLI**; the
whole application is operable by keyboard alone; a screen reader reads documents in the correct
order; the diagnostics bundle contains what support needs and no document content.

**Deferrals.** Scheduled sequences (needs a service; Stage 15's installer work). PDF/UA authoring
guidance beyond the checker.

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
| **Read Aloud enhancements, PDF/X profiles, EPUB export** | various | — | Small, independent. |

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
and that is a finding worth recording rather than a reason to skip the file. **Only Stage 0's exists**,
written from its plan before its code; every other row below is a schedule, not a backlog of missing
documents.

**A stage's §4 entry may also be expanded into a `Stage<N>-<Area>-Plan.md` execution document** when
its task-level detail would swamp this file — Stage 0's is
[`Stage0-Toolchain-Plan.md`](./Stage0-Toolchain-Plan.md). Such a document owns **only** that stage's
task ordering and its stage-local design choices; the objectives, components, and exit criteria stay
here, and the stage's live status stays in its §4 status block. It sits between plan and guide in the
sequence: **§4 entry → `Stage<N>-<Area>-Plan.md` (optional) → testing guide → implement → run the
guide.**

| Stage | Guide | State |
|---|---|---|
| 0 — Toolchain & gates | [`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md) | ✅ **Written 2026-08-17** from [`Stage0-Toolchain-Plan.md`](./Stage0-Toolchain-Plan.md), before any Stage 0 code · **not yet run** — no case has a result. 40 automated cases (A-01…A-73), 7 manual (M-01…M-07): the clean-machine bootstrap and the **real** UCRT64 wrong-compiler configure. Carries the **A5/A6** verdicts and keeps **A7** settled |
| 1 — App shell | `Stage1-AppShell-Testing-Guide.md` | ⚪ Queued — carries the **A3** ribbon go/no-go |
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
| 14 — Batch, CLI, a11y | `Stage14-BatchCli-Testing-Guide.md` | ⚪ Queued — **needs a screen reader** |
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
| **M5 — Release** | 14–15 | Batch, CLI, accessibility, installers, activation, updates, policy, compliance gate. **→ v1.0** |
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
| §1.6 Read Aloud | 14 |
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
| §5.5 Tags & accessibility | 7 (tag tree, autotag, checker), 14 (UI accessibility, reading order) |
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
| §9.7 Display, input, accessibility | 2 (DPI, monitors), 4 (touch/pen), 14 (keyboard operability, screen readers, high contrast) |
| §9.8 Licensing & updates | 15 |
| §9.9 Telemetry | 15 |
| §10 Portability note | 1 (all sixteen interfaces declared), then each stage implements the Windows backend it needs |

---

## 8. Open decisions carried forward

| # | Decision | Owner stage | Why it is not decided now |
|---|---|---|---|
| O1 | Office export/import engine: first-party writers, `IOfficeBridge` through the installed suite, or a commercial component for that subsystem only. (Risk R3.) | 8, confirmed in 12 | Should be decided on **measured** fidelity against a labelled corpus, not on preference. |
| O2 | Whether the Write-mode reflow model is persisted between sessions, or reconstructed on each *Convert to Writable*. (Risk R1.) | 13a | Depends on measured conversion cost and on how stable the model turns out to be. |
| O3 | Whether dynamic XFA gets more than honest reporting. (Risk R4.) | post-13 | Effectively a second document format; needs demand evidence before the investment. |
| O4 | AI assistant provider and whether an on-device model is offered. | 16 | Product and privacy decision; the interface keeps it open. |
| O5 | Collaboration transport (server product, third-party sync, or shared-folder only). | 16 | Needs a business decision that does not exist yet. |
| O6 | arm64 as a shipped target versus build-only. | 15 | Depends on PDFium and dependency availability, and on demand. **MinGW-w64 GCC's aarch64 support is immature (R8)**, so this is also a question of whether arm64 means switching that target to LLVM-MinGW. |
| O10 | How sanitizer coverage is obtained given that MinGW GCC has none: the `portable-asan`/`portable-fuzz` non-Windows jobs over `core/` + `pdf/cos/` (the plan of record), an `x64-clang-asan` preset over LLVM-MinGW, or both. | 0, revisit at 2 | The portable route is cheap and also proves the layering claim, but it cannot reach `platform/win/` or `ui/`. Whether that gap is worth a second Windows toolchain should be settled on evidence from Stage 2's corpus runs, not upfront. |
| O7 | Whether the render cache's disk tier is shared between documents or per-document. | 2, revisit at 5 | Wants real cache-hit data from the corpus. |
| O8 | Sequenced-run scheduling: an in-app timer versus an OS-level service. | 14/15 | A service changes the installer's privilege story. |
| O9 | Product name. "Recto" is a working codename throughout this document set. | before 15 | Trademark and marketing input needed; a rename touches only `res/`, `i18n/`, and the installer. |
