# Stage 0 — Toolchain & Gates · Testing Guide

> **Authoritative verification document for Stage 0.** Written from
> [`Stage0-Toolchain-Plan.md`](./Stage0-Toolchain-Plan.md) and
> [`Implementation-Plan.md` § Stage 0](./Implementation-Plan.md#stage-0--toolchain-skeleton-and-the-four-gates),
> **before any Stage 0 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** 🟡 **Automated half green; manual half in progress.** Every automated case A-01…A-73 has
> been run and passed — on this machine and, since 2026-08-18, in CI (`gates`, `windows` on a
> self-hosted runner, `portable-asan` on Linux). The suite is **63 tests on Windows** (36 gate + 27 unit) after I-08,
> I-10, I-11, I-13 and I-15; the Linux figure is **50**, arithmetic until the next CI run confirms it - the
> toolchain-pin cases are inside the `WIN32` block, so only I-08's two and I-15's four reach Linux. **M-01…M-04 are walked and signed off (2026-08-19); M-05…M-07 are not**, and I
> do not mark those. Per
> [`CLAUDE.md`](../CLAUDE.md), *"automated green, manual not yet walked"* is a legitimate state to
> report and is **not** done. Failures are recorded in the [issue log](#8-issue-log).
>
> Two halves, per [`CLAUDE.md`](../CLAUDE.md): **[§5 Automated](#5-automated-verification--i-run-this)**
> — I run it and report the actual output — and **[§6 Manual](#6-manual-verification--you-run-this)** —
> you walk it on real hardware, and **I never mark a manual case passed on your behalf.**

---

## 1. Scope

### In scope — must pass for sign-off

The four things Stage 0 exists to guarantee ([plan §1](./Stage0-Toolchain-Plan.md#1-what-stage-0-is-actually-for)):
the toolchain pin holds, the two live gates fail correctly on bad input and pass on the real tree,
every dependency resolves and audits clean, and **A5 and A6 have recorded verdicts**.

### Out of scope — do NOT file these as bugs

These are planned absences, not defects. Filing them wastes a triage cycle and, worse, invites someone
to "fix" them early.

| Not present at Stage 0 | Arrives at |
|---|---|
| `recto.exe` does anything at all — it opens no window, prints nothing, returns 0 | Stage 1 |
| `mutation-lint` and `i18n-lint` — **only two of the four gates exist** | Stages 3 and 1 |
| Any settings key, any `tr()` string, any ribbon, any document | Stages 1–2 |
| Golden-image, round-trip, and bench suites | Stages 2–3 |
| `portable-fuzz` producing findings — it configures with **zero targets** | Stage 2 |
| arm64 running on hardware — it **configures only** (O6, R8) | Stage 15 |
| PDFium used for anything beyond opening one file in a throwaway spike | Stage 2 |
| Installers, signing, the release gate | Stage 15 |

### Case anatomy

Every case below is **ID · Goal · Pre · Steps · Expect · If it fails**, with a `☐` you change to ✅ or
❌. A case either passes or yields a **specific** failure — "looks fine" is not a result.

### Severity

| Sev | Meaning |
|---|---|
| **S1** | A gate does not fail on a violation, the compiler pin does not hold, or the build is unreproducible. Stage 0's whole purpose has failed — blocks sign-off outright |
| **S2** | A gate fails for the wrong reason, a verdict cannot be recorded, or CI cannot run the gates before compiling |
| **S3** | A diagnostic is unclear, a documented command is wrong, output is ugly |
| **S4** | Polish |

**An S1 in this stage is different from an S1 elsewhere:** everywhere else it means the product is
broken; here it means the *safety net for the next sixteen stages* is broken, and shipping it means
every later stage inherits an unenforced rule.

---

## 2. Build, launch, environment

`cmake`, `ninja`, and `g++` are **not on this machine's `PATH`** ([plan §2](./Stage0-Toolchain-Plan.md#2-environment-ledger--measured-on-this-machine-2026-08-17)).
Every command below assumes `scripts\env.ps1` has been dot-sourced first — and a case that works only
*without* it is itself a finding.

```powershell
. .\scripts\env.ps1                                          # prepends C:\Qt\Tools\{CMake_64,Ninja,mingw1310_64}\bin
cmake --preset x64-release
cmake --build --preset x64-release --target checks            # the gates — always first
cmake --build --preset x64-release
ctest --preset x64-debug --output-on-failure
```

**What "launched" looks like at this stage:** `recto.exe` is a GUI-subsystem binary that creates no
window and writes nothing. It has run correctly when it exits `0`. There is nothing to look at — that
is expected, not a failure.

### Environment variables and cache variables that affect these cases

Stage 0 has **no settings keys** (`SettingsSchema` is Stage 1), so this table replaces the usual
settings table.

| Variable | Where set | Default | Effect | Cases that must change it |
|---|---|---|---|---|
| `RECTO_MINGW_ROOT` | CMake cache | `C:/Qt/Tools/mingw1310_64` | The pinned compiler ([plan §5.1](./Stage0-Toolchain-Plan.md#51-cmaketoolchain-mingwcmake--cmaketoolchainassertcmake)) | A-04, M-02 |
| `RECTO_QT_PREFIX` | CMake cache | `C:/Qt/6.11.1/mingw_64` | Qt location; TA-8 | M-03 |
| `VCPKG_ROOT` | environment | `C:\Users\squirrel\vcpkg` | Dependency resolution | A-60, M-04 |
| `VCPKG_TARGET_TRIPLET` | preset | `x64-mingw-dynamic` | **The A5 question itself** — never change it to make a port resolve | A-60, A-61 |
| `RECTO_PORTABLE_SUBSET` | preset | `ON` for `portable-*` | Restricts the build to `core/` + `pdf/cos/` | A-70 |

### Test rig

Almost nothing here needs special hardware — which is why the manual section is short.

- **The primary machine** (Qt 6.11.1, `mingw1310_64`, vcpkg at `C:\Users\squirrel\vcpkg`) for everything.
- **A second, clean Windows machine or a fresh VM** with *only* a stock `C:\Qt\6.11.1` install and git —
  no MSYS2, no Visual Studio, no vcpkg on the `PATH`. This is the rig for **M-01**, and it is the only
  case that can catch "it builds because of something that happens to be installed here".
- **An MSYS2 UCRT64 install** (or any non-Qt MinGW GCC) for **M-02**. This is the one case a stub
  compiler cannot fake: only a real UCRT toolchain exercises TA-6, and TA-6 is the assertion that
  catches the realistic regression.
- **Network access** for the vcpkg resolve and the pinned PDFium download.

---

## 3. Verdicts this guide must produce

Sign-off is not just green cases. Three answers must exist in writing, in their owning documents
([plan §8](./Stage0-Toolchain-Plan.md#8-verdict-ledger--what-stage-0-owes-and-where-each-answer-lands)):

| Verdict | Produced by | Recorded in |
|---|---|---|
| **A5** — does the §6.2 set resolve on `x64-mingw-dynamic`? | A-60, A-61 | ✅ **recorded** in `Tech-Stack-Plan.md` §3.3 — resolves; only the default set is *built*, each feature closing at its own stage |
| **A6** — does a PDFium DLL link from `g++` and open a file? | A-62, A-63 | ✅ **recorded** in §3.3 — yes, and PDFium links no CRT at all, which is stronger than the assumption |
| **A7 stays settled** | A-04, A-07, A-08 + M-02 | ✅ assertions TA-1…TA-9 are the record; six negative cases run in CI on every push. **M-02 still outstanding** |

A stage that is green but has no verdicts is **not** signed off. The verdicts are the deliverable;
the green is the evidence.

---

## 4. Traceability — exit criterion → case

| [Exit criterion](./Implementation-Plan.md#stage-0--toolchain-skeleton-and-the-four-gates) | Cases |
|---|---|
| Clean checkout → documented command → empty `recto.exe` builds | M-01, A-01, A-02, A-40 |
| `checks` passes on the real tree | A-10, A-11, A-12 |
| `checks` fails on each bad fixture, **Qt Charts included** | A-20 … A-30 |
| `THIRD-PARTY.md` generated, no non-allowlisted license, lists Qt 6.11.1 and the GCC runtime with its exception | A-12, A-13, M-07 |
| **A5 verdict** — manifest resolves on `x64-mingw-dynamic` | A-60, A-61 |
| **A6 verdict** — the PDFium DLL spike opens a file | A-62, A-63 |
| **A7 stays settled** — a mismatched compiler fails configure | A-04, A-07, A-08, M-02 |
| `core/` builds with no display and no PDF engine | A-43, A-44, A-70 |

---

## 5. Automated verification — I run this

Every case names its exact command and its pass condition. I report the **actual** output; a failing
case is reported as failing, with that output, and is never declared fixed without a re-run.

### 5.1 Configure and the toolchain pin

**A-01 · Configure debug**
☐ **Goal** The pinned toolchain configures. **Pre** T3, T4 complete; `VCPKG_ROOT` set.
**Steps** `cmake --preset x64-debug`
**Expect** Exit 0. Log names GCC **13.1.0** at `C:/Qt/Tools/mingw1310_64`, Qt **6.11.1**, triplet
`x64-mingw-dynamic`, generator Ninja. **If it fails** S1 — nothing else in the stage can run.

**A-02 · Configure release** ☐ As A-01 with `--preset x64-release`. **If it fails** S1.

**A-03 · arm64 fails honestly**
☐ **Goal** The arm64 preset is real enough not to be forgotten, without pretending to be validated.
**Steps** `cmake --preset arm64-release`
**Expect** Exit 1, with a message naming **O6**, LLVM-MinGW, and "a known, recorded gap — not a broken
build". What must *not* happen is a confusing failure that looks like a bug, or a silent success that
implies arm64 works. **No build, no run.** **If it fails** S3 (S2 if the message misleads).
✅ **Passed 2026-08-18.**

> **Resolved during T4.** This case originally allowed *either* a successful configure or an explicit
> failure. That ambiguity was a placeholder for a fact nobody had checked: arm64 cannot configure,
> because the toolchain file pins an x86_64 triple and TA-3 rejects anything else. Rather than let it
> fail three assertions deep, the root `CMakeLists.txt` now checks `RECTO_TARGET_ARCH` first and says
> why. The case is no longer "either" — it has one expected outcome.

**A-04 · A mismatched compiler version fails configure — the A7 guard**
☐ **Goal** TA-2 fires. **Pre** `tests/gates/fixtures/wrong-compiler/` (case **WC-1**).
**Steps** `ctest --preset x64-debug -R gates.wrong_compiler` — until T9 wraps it, the fixture's
`README.md` gives the command by hand.
**Expect** Configure fails; the message names **TA-2**, the expected version `13.1.0`, the version
found, and the fix. **If it fails** **S1** — this is the assertion that stops a week-long crash hunt
in Stage 6.
✅ **Passed 2026-08-18.** Wired as `gates.wrong_compiler.wc1_wrong_version`; reported
`GNU 13.9.0` → `Toolchain pin violated: TA-2 compiler version · found: 13.9.0 · expected: 13.1.0`.

**A-05 · No static linking of the runtimes**
☐ **Goal** `-static`, `-static-libstdc++`, `-static-libgcc` cannot enter the flags — an LGPL
obligation *and* an ABI hazard ([`Tech-Stack-Plan.md` §8.1](./Tech-Stack-Plan.md#81-obligation--concrete-action)).
**Steps** Configure with `-DCMAKE_CXX_FLAGS=-static-libstdc++`.
**Expect** Configure fails naming the flag. **If it fails** S1 (license) — an obligation that is only
documented is not met.

**A-06 · The compiler plugin list is empty**
☐ **Goal** The Runtime Library Exception's condition (TA-9) is checked, not assumed.
**Steps** `cmake --preset x64-release` then read `build/x64-release/compiler-plugins.txt`.
**Expect** File exists and is empty. **If it fails** S1 (license) — a non-free plugin would relicense
our own source.

**A-07 · A UCRT runtime fails configure — the assertion that catches MSYS2**
☐ **Goal** TA-6 fires. **Pre** fixture case **WC-2**.
**Steps** configure with `-DCMAKE_CXX_FLAGS=-D_UCRT`.
**Expect** Configure fails naming **TA-6 C runtime**, `found: UCRT (_UCRT is defined)`, and MSYS2
ucrt64 as the usual source. **If it fails** **S1** — an MSYS2 UCRT64 toolchain passes TA-1, TA-3,
TA-4 and TA-5; this is the only assertion that stops it.
✅ **Passed 2026-08-18.** Wired as `gates.wrong_compiler.wc2_ucrt_runtime`.

> **This case changed the implementation.** TA-6's probe originally ran the bare compiler, so this
> exact command configured **cleanly** and reported "msvcrt" while the whole build compiled as UCRT —
> the assertion was interrogating a compilation the project never performs. The probe is now compiled
> with `CMAKE_CXX_FLAGS`, which is what makes `--sysroot`, `-target`, and a UCRT `-isystem` reachable
> by it too.

**A-08 · TA-6 cannot pass vacuously**
☐ **Goal** The probe's own failure is not mistaken for success. **Pre** fixture case **WC-3**.
**Steps** configure with `-DCMAKE_CXX_FLAGS=-U__MSVCRT__`.
**Expect** Configure fails with `found: __MSVCRT__ is not defined` — TA-6 requires the positive
marker present, not merely `_UCRT` absent. **If it fails** **S2** — a check that passes when its own
probe breaks is not a check.
✅ **Passed 2026-08-18.** Wired as `gates.wrong_compiler.wc3_probe_self_check`.

### 5.2 The gates on the real tree

**A-10 · `checks` is clean**
☐ **Steps** `cmake --build --preset x64-release --target checks`
**Expect** Exit 0; both gates report clean; **completes without compiling any layer library**
(the log shows only the two tool targets building). **If it fails** S1 if a real violation exists in
the tree; S2 if `checks` dragged the whole build in — that defeats "the gates run first".

**A-11 · The include graph has no outward edge**
☐ **Steps** `./layer-lint --explain > graph.txt`
**Expect** Non-empty graph; zero outward edges; the excluded-path list ([plan D0-4](./Stage0-Toolchain-Plan.md#3-stage-0-local-decisions))
and every active `allow-write` escape are printed. **If it fails** S2.

**A-12 · `THIRD-PARTY.md` is generated and correct**
☐ **Steps** `./license-audit --emit THIRD-PARTY.md`; read it.
**Expect** Exit 0. Contains **Qt 6.11.1 under LGPL-3.0** with its source-offer URL; the **GCC runtime
as `GPL-3.0-or-later WITH GCC-exception-3.1`**; FreeType's **FTL** election and Hunspell's **MPL-2.0**
election stated explicitly; a NOTICE column. No identifier outside the §8.2 allowlist.
**If it fails** S2 (S1 if a non-allowlisted license is present and passed).

**A-13 · The allowlist exists in exactly one place**
☐ **Goal** A second copy of the allowlist is how the two copies drift apart.
**Steps** grep the tree for `Unicode-DFS-2016`.
**Expect** One hit, in `tools/license-audit/`. **If it fails** S3.

### 5.3 The gates on deliberately bad input

The heart of the stage. Every case asserts the gate exits **1** *and* prints its **named diagnostic** —
a gate that fails for an unrelated reason has not been tested. Run as one suite:
`ctest --preset x64-debug -R tst_gates --output-on-failure`.

| ID | Fixture | Gate | Expect | Fail sev |
|---|---|---|---|---|
| ☐ **A-20** | `outward-include/` | layer-lint | Exit 1; `core/` → `pdf/cos/CosDict.h` reported with `file:line` | S1 |
| ☐ **A-21** | `ui-includes-pdf/` | layer-lint | Exit 1; `ui/` → `pdf/` reported | S1 |
| ☐ **A-22** | `fpdf-outside-pdf/` | layer-lint | Exit 1; `FPDF_InitLibrary` outside `pdf/` reported | S1 |
| ☐ **A-23** | `write-outside-allowed/` | layer-lint | Exit 1; the write site reported; adding `// layer-lint: allow-write(test)` makes it pass **and** the escape appears in `--explain` | S1 |
| ☐ **A-24** | `manifest-gpl/` | license-audit | Exit 1; the GPL-2.0 dependency named | S1 |
| ☐ **A-25** | `manifest-bare-gcc-gpl/` | license-audit | Exit 1 on **bare `GPL-3.0-or-later`** — proves the GCC allowance is a whole-expression match, not a `GPL` substring hole | **S1** |
| ☐ **A-26** | `manifest-gcc-exception/` | license-audit | **Exit 0** — the same identifier `WITH GCC-exception-3.1` passes. Without this control, A-25 is satisfied by a tool that rejects everything | **S1** |
| ☐ **A-27** | `manifest-lgpl/` | license-audit | **Exit 0** — `LGPL-3.0` must not be caught by any `GPL` test | **S1** |
| ☐ **A-28** | `qt-charts-link/` | license-audit | Exit 1; message names Qt Charts as **GPL-3.0-only** and cites D4 — not a generic "not on the allowlist", which invites someone to widen the allowlist | **S1** |
| ☐ **A-29** | `plugin-loaded/` | license-audit | Exit 1; the plugin named; the exception-void consequence stated | S1 |
| ☐ **A-30** | fixtures excluded from the real run | layer-lint | The production run over the tree ignores `tests/gates/fixtures/` **by the explicit deny-path**, and says so in `--explain` | S2 |

### 5.4 Build shape and the layer boundary

**A-40 · The empty executable builds and runs**
☐ **Steps** `cmake --build --preset x64-release`; run `recto.exe`; `echo $LASTEXITCODE`
**Expect** Builds with zero warnings in `core`/`pdf` (`-Werror`); exits **0**; no window, no output.
**If it fails** S1.

**A-41 · The CRT still matches Qt's**
☐ **Steps** `objdump -p build/x64-release/bin/recto.exe | Select-String "DLL Name"`
**Expect** `msvcrt.dll` present and **`ucrtbase.dll` absent** — the cheapest possible check that the
CRT matches Qt's ([`Tech-Stack-Plan.md` §9](./Tech-Stack-Plan.md#9-verification)). **If it fails** S1.

> **Corrected during T2, from the first real build.** This case originally also required
> `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, and `libwinpthread-1.dll` among the imports. At Stage 0
> that is wrong and the case would have failed for a legitimate reason: the placeholder translation
> units use no C++ runtime facility and no Qt symbol, so the linker records no import for those DLLs
> at all. Measured on the T2 build, `recto.exe` imports exactly `KERNEL32.dll` and `msvcrt.dll`.
> The runtime-DLL half of the check becomes meaningful once real code exists and moves to **Stage 1**;
> what survives here is the msvcrt/ucrtbase discrimination, which is the part that actually guards
> the A7 pin. An expectation that cannot hold yet is not a stricter test — it is a case that gets
> waved through once and then never believed again.

**A-42 · No Qt Charts anywhere**
☐ **Steps** Same output; also list the build tree.
**Expect** No `Qt6Charts.dll` or `Qt6DataVisualization.dll` among imports or in the tree.
**If it fails** S1 (license).

**A-43 · The unit test binary links no UI and no platform**
☐ **Steps** Inspect the resolved link closure of the `tests/unit` target.
**Expect** `recto_core` and `recto_pdf` only. **`recto_ui` and `recto_platform` absent.**
**If it fails** S1 — "if it cannot, the boundary has already leaked".

**A-44 · …and that is enforced, not observed**
☐ **Steps** Add `recto_ui` to the test target's `target_link_libraries` and configure.
**Expect** **Configure fails** with a message naming the boundary rule. **If it fails** S2 — an
unenforced invariant is one refactor from being false.

### 5.5 The `core/` vocabulary

Run: `ctest --preset x64-debug --output-on-failure`.

| ID | Suite | Expect | Fail sev |
|---|---|---|---|
| ☐ **A-50** | all | Every suite green: `tst_result`, `tst_units`, `tst_geometry`, `tst_log`, `tst_gates` | S1 |
| ☐ **A-51** | `tst_geometry` | The **compile-time negatives** hold: `static_assert(!std::is_convertible_v<PdfPoint, DevicePoint>)` and siblings, both directions, points and rects. Conversion is possible **only** through an explicit `PageTransform` | **S1** — this is the assertion that prevents the "slightly wrong at 150% scaling" bug class |
| ☐ **A-52** | `tst_log` | A `Redacted<QString>` renders as `<redacted>` at info/warning/error and as the real value at debug/trace; `trace` compiles out in release | S2 — [`Features.md` §9.5](./Features.md#95-logging-and-diagnostics) makes this a rule, and a rule enforced by a type is a rule |
| ☐ **A-53** | `tst_units` | Exact ratios: 1 in = 72 pt, 1 pica = 12 pt, 1 mm = 72/25.4 pt; round-trip through all five units within a stated tolerance; parse and format | S2 |
| ☐ **A-54** | `tst_result` | `Result<T>` and `Result<void>` carry code + catalog key + context; **nothing in the header throws**; `RECTO_TRY` propagates | S2 |

### 5.6 Dependencies and the two spikes

**A-60 · A5 — the manifest resolves on `x64-mingw-dynamic`**
☐ **Steps** `vcpkg install --dry-run --triplet x64-mingw-dynamic` over the default feature set.
**Expect** Every package resolves. **Any port that resolves only under MSVC is the A5 finding** — it is
recorded with an owning stage and an overlay port, **not** worked around by changing the triplet.
**If it fails** S2 and **record the verdict anyway** — a negative A5 is a successful Stage 0 outcome,
found now instead of in Stage 6.

**A-61 · A5 across the deferred features**
☐ **Steps** Repeat A-60 for each optional feature (`ocr`, `office`, `media` — [plan D0-3](./Stage0-Toolchain-Plan.md#3-stage-0-local-decisions)).
**Expect** Each resolves, or produces a named finding against its owning stage. **If it fails** S3 for a
Stage 16 feature, S2 for anything needed before Stage 8.

**A-62 · A6 — the PDFium DLL spike**
☐ **Pre** Pinned prebuilt DLL with recorded SHA-256; `libpdfium.dll.a` generated via `gendef` + `dlltool`;
one PDF in `corpus/`.
**Steps** Build and run `tools/spike-pdfium`.
**Expect** `FPDF_InitLibrary` → `FPDF_LoadDocument` → `FPDF_GetPageCount` > 0 → `FPDF_DestroyLibrary`,
exit 0. **If it fails** S1 for the stage's A6 objective — the documented fallback is building PDFium
from source with its own clang toolchain, **not** switching engines and **not** a static MSVC
`pdfium.lib`.

**A-63 · The spike's imports are sane**
☐ **Steps** `objdump -p spike.exe | Select-String "DLL Name"`
**Expect** `pdfium.dll` and `msvcrt.dll`; **no `ucrtbase.dll`**. **If it fails** S1 — a CRT split across
the PDFium boundary is exactly the ABI hazard A6 asks about.

### 5.7 Portable presets and CI

**A-70 · `portable-asan` on Linux**
☐ **Steps** Configure `portable-asan`; build; `ctest`.
**Expect** `core/` (+ `pdf/cos/`, empty at Stage 0) and the unit suites build **with no display and no
PDF engine linked**, and run clean under ASan+UBSan. **If it fails** S1 — this is the day-one proof of
the portability claim the whole layer contract rests on, and it is the only sanitizer coverage the
project has (R7).

**A-71 · `portable-fuzz` configures with zero targets**
☐ **Expect** Configures; builds nothing; **says** it has no targets until Stage 2. A silent
zero-target build is indistinguishable from a broken one. **If it fails** S3.

**A-72 · CI, all three jobs**
☐ **Steps** Push; watch `gh run watch`.
**Expect** `gates`, `windows`, `portable-asan` all green; `gates` finishes first and **runs `checks`
before anything is compiled**. **If it fails** S2.
✅ **Passed 2026-08-18** (run `32064437614`): all three green. `windows` reported *"Toolchain pin
verified: GCC 13.1.0 x86_64-w64-mingw32, SEH/posix/msvcrt, no GCC plugin"*, 50/50 tests, and imports
`msvcrt.dll` with no `ucrtbase.dll` and no `Qt6Charts`. `portable-asan` ran 43 tests clean.
**Re-run pending:** I-08 added two license-audit cases after that run, taking Windows to 52 and Linux
to 45. The next push re-confirms this case at the new counts.

> **The Windows job is self-hosted, and that was forced rather than chosen.** aqtinstall cannot
> install Qt 6.11.1: Qt 6.11 splits its repository per architecture (`qt6_6111_mingw/`) and aqt still
> requests the old single-repo path, returning 404 behind a misleading "checksum" error. 3.3.0 and
> 3.2.1 fail identically. Downgrading the Qt pin to suit CI was rejected — TA-8 asserts 6.11.1, and
> testing against a different Qt than we ship is the ABI question A7 exists to prevent. The
> consequence is an improvement: CI now builds with the **exact** toolchain bytes a release uses.

**A-73 · The `gates` job needs no Qt**
✅ **Passed 2026-08-18.** Verified twice: locally with `VCPKG_ROOT` cleared and `RECTO_QT_PREFIX`
pointed at a nonexistent path (3.8 s, 23/23), and in CI on `ubuntu-latest` with neither installed
(~40 s). **This case found a real defect:** the root `CMakeLists.txt` called `find_package(Qt6
REQUIRED)` unconditionally, so the job it describes could not have existed. `RECTO_GATES_ONLY` fixed it.

☐ **Goal** [Plan D0-1](./Stage0-Toolchain-Plan.md#3-stage-0-local-decisions)'s payoff is real.
**Steps** Confirm the `gates` job installs neither Qt nor vcpkg, and check its wall-clock time.
**Expect** No Qt, no vcpkg, under a couple of minutes. **If it fails** S2 — a gate that waits behind a
Qt install is a gate that runs last, and the ordering guarantee quietly disappears.

---

## 6. Manual verification — you run this

Short by design: almost everything above is automatable, and
[`CLAUDE.md`](../CLAUDE.md) is explicit that a manual case that could have been automated is a defect
in this guide. What is left is the residue that genuinely needs a second machine, a real second
toolchain, or human judgement.

**M-01 · Clean-machine bootstrap** ✅ **2026-08-18**
- **Goal** The build works from a bare checkout on a machine whose environment we did not curate.
- **Pre** A clean Windows box or fresh VM: stock `C:\Qt\6.11.1` + `C:\Qt\Tools`, git, **no** MSYS2, **no**
  Visual Studio, **no** vcpkg on `PATH`, nothing from this machine's `PATH`.
- **Steps** 1. Clone into an empty directory. 2. Follow `README.md`'s build section **literally** —
  no improvising, no "obviously you also need X". 3. `. .\scripts\env.ps1` 4. `scripts\bootstrap.ps1`
  5. `cmake --preset x64-release` 6. `--target checks` 7. build 8. `ctest`.
- **Expect** Every step succeeds with **no manual step that is not written down**. `recto.exe` exists
  and exits 0.
- **If it fails** **S1.** Note the *exact* step and the *exact* undocumented thing you had to do — that
  missing line is the finding, and this case is the only one that can find it.
- **Result** ✅ **Passed across two runs**, because no single run was both halves. Stated plainly so
  the split is on the record rather than implied:
  - **Clean Windows VM** (uncurated: Qt, vcpkg, execution policy all from scratch). Produced four
    findings — **I-01, I-02, I-04, I-05** — every one of them a missing line in `README.md`, which is
    exactly what this case exists to find. Its tree came from a GitHub **ZIP archive**, not a clone
    (**I-06**), so it proves the *machine* half only.
  - **Fresh `git clone` of `9c305ce`** on the primary machine. `git status --porcelain --ignored`
    empty; `corpus/minimal.pdf` byte-identical at `b0e6a706…a02b450`, 605 bytes, so `.gitattributes`'
    `corpus/** binary` held through a real checkout; `scripts/env.ps1` checked out CRLF and
    `README.md` LF, both per policy. Proves the *clone and tree* half.
  - **Both runs:** `bootstrap.ps1` clean · pin verified · `checks` at `[10/10]`, gate executables only
    · `layer-lint: clean -- 27 files, 93 includes` (identical on both trees) · **50/50 tests** ·
    `recto.exe` exit **0**.
  - **Residual gap:** a `git clone` on an uncurated machine was never run. Accepted — `.gitattributes`
    overrides any fresh machine's `autocrlf`, which the host checkout demonstrated directly.
  - Signed off by the user, 2026-08-18.

**M-02 · A real MSYS2 / UCRT64 GCC is refused** ✅ **2026-08-19**
- **Goal** TA-6 — the assertion a stub compiler cannot exercise, and the realistic way the pin regresses:
  the next developer's MSYS2 install.
- **Pre** A real MSYS2 UCRT64 (or any non-Qt MinGW) GCC installed.
- **Steps** `cmake --preset x64-release -DRECTO_MINGW_ROOT=C:/msys64/ucrt64` (adjust path).
- **Expect** **Configure fails**, the message names the pin as Qt's own bundled GCC 13.1.0/msvcrt, and
  it states the fix. It must not fail with a link error later, and it must not succeed.
  **Which assertion fires depends on the MSYS2 build, and TA-6 is usually not the one:**
  `_recto_pin_error` is `FATAL_ERROR`, so the assertions stop at the first violation in numbered
  order, and a stock MSYS2 GCC is never version 13.1.0 - **TA-2 fires first**. Refusal at TA-2 with a
  complete, actionable message satisfies this case's Goal. TA-6's own logic stays covered by
  `wc2_ucrt_runtime` and `wc3_probe_self_check`, which inject `-D_UCRT` and `-U__MSVCRT__`; this case
  covers the *scenario*, those cover the *logic*.
- **If it fails** **S1.** A pin that a real toolchain can slip past is not a pin. If it *fails* but with
  an unhelpful message, that is S3 — log it separately, do not merge the two.
- **Result** ✅ **Passed.** MSYS2 UCRT64 **GCC 15.2.0** at `C:/msys64/ucrt64`, driven through
  `toolchain-mingw.cmake` against the `wrong-compiler` fixture project. Configure **failed at
  `project()`-time assertion TA-2**: *found 15.2.0, expected 13.1.0*, followed by the whole fix
  sentence — *"Use C:/Qt/Tools/mingw1310_64. If Qt was upgraded, the compiler moves with it in the
  same change -- update RECTO_MINGW_ROOT and this expected version together."* Message judged clear
  enough by the user; **no separate S3 logged** for diagnostic quality.
  - This is the half the injected-macro cases cannot reach: `wc1`…`wc6` test each assertion's
    *logic*, this tests the *scenario* — a second real toolchain on a real machine.
  - **TA-2 fires, not TA-6**, because `_recto_pin_error` is `FATAL_ERROR` and no MSYS2 build is
    version 13.1.0. The Expect text was corrected to say so; TA-6's logic stays covered by
    `wc2_ucrt_runtime` and `wc3_probe_self_check`.
  - Two findings came out of the attempts rather than the result: **I-09** (six diagnostics were
    printing half a sentence — fixed, and this run is what confirmed the repair in the live path) and
    **I-10** (a toolchain whose own `bin` is off `PATH` fails as CMake's generic "compiler is broken"
    before the pin can speak — open).
  - Signed off by the user, 2026-08-19.

**M-03 · A moved Qt prefix fails clearly** ✅ **2026-08-19**
- **Goal** TA-8's diagnostic is usable by someone who does not know the codebase.
- **Steps** Configure with `-DRECTO_QT_PREFIX=C:/nope`.
- **Expect** Configure fails naming the missing prefix and the expected version 6.11.1. Not a CMake
  package-search wall of text.
- **If it fails** S3.
- **Result** ✅ **Passed after fix.** The first run failed the case: TA-8 reported *"A second Qt
  install won the search"* when there was no second install — `C:/nope` simply did not exist, so
  nothing constrained the search and the machine's only Qt was used. Logged as **I-11** and fixed;
  the re-run reports the missing prefix by name, says it is not the second-install case, states the
  pinned version, and gives the exact command. Signed off by the user, 2026-08-19.
- **Also fixed here:** **I-12** — a `FATAL_ERROR` raised inside the toolchain file lands mid-detection
  and CMake carries on to print *"unable to find a build program corresponding to Ninja"* and
  *"CMAKE_CXX_COMPILER not set"* after the real message. The checks now run from
  `cmake/ToolchainPreflight.cmake` **before `project()`**, and the abort is clean.

**M-04 · A missing vcpkg is diagnosed, not crashed into** ✅ **2026-08-19**
- **Goal** The one precondition this machine did not have ([plan §7.1 checklist](./Stage0-Toolchain-Plan.md#71-vcpkg--owner-you)).
- **Steps** Clearing `$env:VCPKG_ROOT` alone is **not enough** - both `bootstrap.ps1` and `env.ps1`
  fall back to the user registry and then to `%USERPROFILE%\vcpkg`, so with the clone present this
  case passes vacuously (issue **I-03**; the same shape as TA-6 at T3). Make vcpkg genuinely absent:
  in a **fresh shell**, without dot-sourcing `env.ps1`, rename `%USERPROFILE%\vcpkg` aside, set
  `$env:VCPKG_ROOT = ''`, run `scripts\bootstrap.ps1`, then `cmake --preset x64-release`. Rename it
  back afterwards. Leave the registry value alone - it then points at a missing directory, which is
  exactly the failure this case is about.
- **Expect** `bootstrap.ps1` says what is missing, where it should live
  (`C:\Users\squirrel\vcpkg`), and what to run. Configure fails with the same information rather than a
  toolchain-file-not-found.
- **If it fails** S3 (S2 if it configures anyway and fails much later).
- **Result** ✅ **Passed after fix**, scored as two halves.
  - **`bootstrap.ps1`: passed first time.** Three `[FAIL]` lines, each naming the path and the exact
    command — the clone, the bootstrap batch file, the triplet — then *"3 blocking problem(s)"* and
    exit 1. Nothing had to be guessed.
  - **Configure: failed the case**, exactly as its Expect warned — *"Could not find toolchain file:
    /scripts/buildsystems/vcpkg.cmake"*, a path assembled from an empty variable. Logged **I-13**,
    fixed in preflight, re-run: it now names `VCPKG_ROOT`, what was expected there, and the commands
    that repair it, aborting before `project()` with nothing after it.
  - **I-14** (S4) came out of the same run and is fixed.
  - Steps were corrected before this walk (**I-03**): clearing `$env:VCPKG_ROOT` alone tests nothing,
    because `bootstrap.ps1` and `env.ps1` both fall back to the registry and then to
    `%USERPROFILE%cpkg`. The clone must actually be absent.
  - Signed off by the user, 2026-08-19.

**M-05 · Fixture realism review** ☐
- **Goal** Human judgement, which is the reason this case cannot be automated: do the fixtures resemble
  violations a real developer would actually write?
- **Steps** Read each directory under `tests/gates/fixtures/`. Ask of each: *would a rushed developer
  write this?* An `#include` in an unusual place, a write hidden behind a helper, a `find_package` line
  added to get a chart into a dialog.
- **Expect** Each is plausible. A fixture that only fails because it was built to fail proves the gate
  catches nothing real.
- **If it fails** S2 — file the specific fixture and what a realistic version would look like.

**M-06 · `THIRD-PARTY.md` read as an auditor would** ☐
- **Goal** The document that answers a license audit is legible to someone outside the project.
- **Steps** Read it end to end. For three entries at random, check the version matches what is actually
  installed and the upstream URL resolves. Confirm the GCC runtime entry states the **exception**, and
  that Qt's **source-offer** URL is present.
- **Expect** No entry is vague, missing a version, or missing a URL that its license requires.
- **If it fails** S2 — the release gate ([`Tech-Stack-Plan.md` §8.3](./Tech-Stack-Plan.md#83-release-gate-must-all-be-ticked))
  depends on this file being right, and Stage 15 is a bad time to find out it is not.

**M-07 · The plan's own claims survive contact** ☐
- **Goal** Stage 0 sets conventions that sixteen stages inherit; this is the moment to reject one that
  is wrong, while it costs a day.
- **Steps** Skim [`Stage0-Toolchain-Plan.md` §3](./Stage0-Toolchain-Plan.md#3-stage-0-local-decisions)
  (D0-1 … D0-8) against what actually got built. Are the gates genuinely fast? Is the `allow-write`
  escape being used a handful of times or a hundred?
- **Expect** Each decision either held or is recorded as revisited, with the reason, in the plan.
- **If it fails** S3, but **record it** — an inherited convention that nobody believes in is worse than
  one that was changed.

---

## 7. Known caveats — verify, do not assume broken

- **`recto.exe` does nothing.** Correct at Stage 0 (D0-8). Verify it exits 0; do not file "app does not
  start".
- **Only two gates run.** `mutation-lint` and `i18n-lint` do not exist yet. If `checks` claims four
  gates ran, *that* is the bug.
- **`portable-fuzz` builds nothing.** Expected until Stage 2 — but it must **say** so (A-71).
- **arm64 is configure-only.** Not a regression; O6/R8 own it.
- **A negative A5 or A6 verdict is a successful Stage 0.** The stage exists to find these now. Record
  the verdict and the fallback; do not retry until it passes by changing the triplet or the linkage
  model, both of which would hide the finding rather than settle it.

---

## 8. Issue log

Everything found in §5 or §6 goes here. This table is the fix-pass backlog and the stage's progress
record.

| # | Case / area | What happens | Expected | Sev | Repro / notes | State |
|---|---|---|---|---|---|---|
| I-01 | M-01 | The Qt online installer offers only **6.11.2**; `README.md` said "selecting Qt 6.11.1" without saying 6.11.1 sits behind the **Archive** category filter. Following the README literally on a clean box yields the wrong Qt and a TA-8 configure failure. | The build section is followable with no undocumented step | **S1** | Found on the M-01 VM, 2026-08-18, before the case reached step 5 | **Closed** - fixed in `README.md`; re-walked and verified at `9c305ce` |
| I-02 | M-01 | `README.md` said "Exactly two installs... Nothing else is required", but `git clone` is step one of *Get the source* and `bootstrap.ps1` checks for git. | Every install the clean box needs is named | S3 | Same VM, same pass | **Closed** - fixed in `README.md`; verified at `9c305ce` |
| I-03 | M-04 (this guide) | The case said only "unset `VCPKG_ROOT`", but `bootstrap.ps1` and `env.ps1` both fall back registry then `%USERPROFILE%\vcpkg`. With the clone present the case passes without testing anything - the same vacuity TA-6 had at T3. | The case exercises a genuinely absent vcpkg | S2 | Read of `scripts/bootstrap.ps1` and `scripts/env.ps1` while preparing the rig; case not yet run | **Fixed in section 6** - M-04's Steps now require the directory to be absent |
| I-04 | M-01 | On a stock Windows install PowerShell's execution policy is `Restricted`, so `bootstrap.ps1` and `env.ps1` both refuse to run. `README.md` never said to change it, and no script can diagnose this - the diagnosing script is itself blocked. | The documented build sequence runs on a clean account | **S1** | M-01 VM, 2026-08-18: `. \scripts\env.ps1` -> *"running scripts is disabled on this system"*, `UnauthorizedAccess` | **Closed** - `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` is now the first line of the Build block; verified at `9c305ce` |
| I-05 | M-01 | `README.md`'s Build block ran `ctest --preset x64-debug` after configuring and building only `x64-release`. Each preset owns `build/<preset>`, so the debug tree does not exist and CTest fails. | The documented sequence ends in a green test run | S3 | M-01 VM, 2026-08-18. `CLAUDE.md`'s command list already used `x64-release`, so the README was the outlier | **Closed** - the block now uses `x64-release`, with a note that `x64-debug` needs its own configure and build; both trees re-run green |
| I-06 | M-01 (rig) | The first M-01 attempt used a **folder copy** of the primary machine's working tree, not `git clone`. It carried a gitignored `build/x64-debug` whose CMake cache held the primary machine's absolute paths; `x64-debug` failed to configure while `x64-release`, which had no counterpart directory, succeeded. | M-01 runs from a clone into an empty directory | n/a - rig error, not a product defect | Recorded so the walk is not mistaken for a valid one. I-01, I-02, I-04 and I-05 concern installs and documentation, not the tree, and remain valid | **Closed** - re-walked from a real clone of `9c305ce`; M-01's Result records the two-run split |
| I-07 | M-01 / A-10 | `layer-lint`'s exclude list omitted `third_party/`, so the scanned set depended on whether `fetch-pdfium.ps1` had been run: **51 files / 135 includes** on a machine with PDFium fetched, **27 / 93** on a clean clone. Nothing was actually checked there - third-party files match no layer, and `LayerLint.cpp:35` skips unlayered files before any rule runs - but a gate whose own report varies with machine state undercuts the point of having gates. | The gate reports the same figures on any machine at a given commit | S3 | Found by comparing the M-01 VM run against the primary machine; A-10 asserts no count, so the guide was not wrong | **Fixed** - `third_party/` added to `tools/layer-lint/rules.json`; this machine now reports 27 / 93, matching the clean clone. 23/23 gate cases and 50/50 unit tests re-run green |
| I-08 | A-10 / A-12 | `checks` always passes `--emit THIRD-PARTY.md` into the **source tree** (`RectoChecks.cmake:26`), while `--qt-modules` and `--vcpkg-installed` are conditional on the configuration. Running `checks` from the `gates` preset therefore **rewrites a committed file with less information**: catch2's version became `*(not resolved)*` and the Qt module count dropped from 4 to 0. The emitted file also carries CRLF where the repository stores LF. | Running any gate configuration never degrades a committed artifact | S3 | Reproduced 2026-08-18 while verifying I-07: `cmake --build --preset gates --target checks` then `git diff THIRD-PARTY.md`. Restored with `git checkout`. Harmless in CI, where nothing commits the result, but silent on a developer machine | **Fixed** - `license-audit` now rewrites the file only with a real Qt link closure **and** an installed vcpkg tree, skipping with a named reason otherwise, and writes in binary mode so the file stays LF. Two new gate cases assert the file, not just the message; `RunGateCase.cmake` gained `EXPECT_FILE` / `EXPECT_ABSENT` for that. 25/25 gates, 52/52 on `x64-debug` |
| I-09 | M-02 | `_recto_pin_error` took a named `fix` parameter, but six of the nine call sites write that text as **several adjacent quoted literals** - separate arguments in CMake, not concatenated as in C. Only the first fragment was printed and the rest went to `ARGN` and vanished. TA-7 ended at *"Do not 'fix' this by"*, TA-6 at *"the usual source. Qt"* - in every case the half that says what to do. The file's own header says a message that only names the problem "turns a five-second fix into an afternoon". | Each diagnostic prints its whole fix sentence | S3 | Surfaced by the first M-02 attempt, whose TA-7 output was visibly cut off. The seven `wrong_compiler` cases could not catch it: every `EXPECT_MATCH` asserted the label only | **Fixed** - the helper joins `ARGN`; `wc1`/`wc2` now assert a word from the **tail** of the sentence. Those patterns are single words on purpose: `message(FATAL_ERROR)` hard-wraps, so a phrase can be split across a line break and fail for an unrelated reason - which it did on the first attempt. 25/25 gates, 52/52 on `x64-debug` |
| I-10 | M-02 | Pointing `RECTO_MINGW_ROOT` at a toolchain whose own `bin` is **not** on `PATH` fails at `project()` with CMake's generic *"the C++ compiler is not able to compile a simple test program"*, with no output from the compiler at all - the pin's diagnostic never runs. MSYS2's `g++` spawns `cc1plus.exe` from `lib/gcc/...`, which is not the driver's directory, so it needs `C:/msys64/ucrt64/bin` on `PATH` to find its own DLLs. | The pin explains itself before CMake's probe does | S3 | M-02, 2026-08-19. Adding `C:/msys64/ucrt64/bin` to `PATH` makes the same command reach **TA-2** correctly. Precedent: `toolchain-mingw.cmake` already duplicates the `-fplugin` scan for this exact reason, calling CMake's message *"true, useless, and nowhere near the actual reason"* | **Fixed** - `recto_preflight_compiler_runs()` in the new `cmake/ToolchainPreflight.cmake`, called from the top-level `CMakeLists` **before `project()`**, so it runs once per configure rather than once per `try_compile()` - which is what made the trade-off go away. The probe is `-fsyntax-only` on an empty file, **not** `-dumpversion`: the driver answers that one itself and reports success while the toolchain is unusable. Verified against the real MSYS2 install with its `bin` off `PATH`. Gate case `compiler_cannot_run` uses a `fake-root/bin/g++.exe` that is a text file |
| I-11 | M-03 | With `RECTO_QT_PREFIX` pointing at a path that does not exist, configure failed at **TA-8 Qt prefix** saying *"A second Qt install won the search"*. There was no second install: the prefix was absent, nothing constrained the search, and the machine's only Qt was used. The message sends the reader hunting a rival Qt and pruning a `CMAKE_PREFIX_PATH` that is fine. `toolchain-mingw.cmake` guards `RECTO_MINGW_ROOT` with an `EXISTS` check but had no equivalent for the Qt prefix, and the closing paragraph about libstdc++ and the C runtime was appended to a Qt **location** failure where it does not apply. | The diagnostic is usable by someone who does not know the codebase | S3 | M-03, 2026-08-19, `cmake --preset x64-debug -DRECTO_QT_PREFIX=C:/nope` | **Fixed** - four changes: an `EXISTS` guard for `RECTO_QT_PREFIX` in the toolchain file, firing before Qt is searched; TA-8's prefix branch split by cause; the closing rationale chosen per assertion; and the pinned Qt version now stated in both TA-8 branches from one variable. Three gate cases added, each asserting a word from the sentence tail. 28/28 gates, 55/55 on `x64-debug` |
| I-12 | M-03 | A `FATAL_ERROR` from inside the toolchain file is raised during CMake's compiler detection, and CMake does not stop there: it prints *"unable to find a build program corresponding to Ninja"* and *"CMAKE_CXX_COMPILER not set"* afterwards. The real message is first but two red herrings follow it. Pre-existing - the `RECTO_MINGW_ROOT` guard always behaved this way. | A failed pin check aborts with nothing after it | S4 | Seen on every M-03 run, and on WC-4 since T3 | **Fixed** - the cheap checks moved to `cmake/ToolchainPreflight.cmake`, called before `project()` by both the real root and the fixture project. The toolchain file still calls the same two functions as a fallback for anyone configuring with `-DCMAKE_TOOLCHAIN_FILE` and no preflight, so the wording has one home |
| I-13 | M-04 | With `VCPKG_ROOT` unset, `CMakePresets.json:37` expands `$env{VCPKG_ROOT}` to the **empty string** rather than erroring, so `CMAKE_TOOLCHAIN_FILE` became `/scripts/buildsystems/vcpkg.cmake` and CMake reported *"Could not find toolchain file"* for a path that never existed - naming neither the variable nor the fix, and followed by the usual two red herrings. This is the outcome M-04's Expect calls out by name. | Configure carries the same information `bootstrap.ps1` gives | S3 | M-04, 2026-08-19, with `%USERPROFILE%\vcpkg` renamed aside and the variable removed from the process | **Fixed** - `recto_preflight_vcpkg()`, opted into by the root with `recto_preflight(QT VCPKG)`, before `project()`. Two branches: unset (names `setx`'s already-open-shell trap, `env.ps1`, `bootstrap.ps1`, README) and set-but-no-clone (names the path and the clone/bootstrap commands). Two gate cases via `cmake -E env --unset=` |
| I-14 | M-04 | `bootstrap.ps1`'s baseline check ran even with no clone present, so `git rev-parse` returned nothing and it printed *"the clone is at ."* - an empty revision rendered as a bare full stop, dressed as a `[WARN]`, directly beneath three `[FAIL]`s that had already said the directory does not exist. | No warning about a comparison that cannot be made | S4 | Same run | **Fixed** - the baseline check is skipped with an `[INFO]` line when there is no clone to compare against |
| I-15 | M-05 | **`layer-lint` missed real violations.** Every fixture used one spelling, so only that spelling was ever tested. Three that compile and passed clean: `#include "../../pdf/cos/CosDict.h"` from `core/doc/` (the sibling form resolved to `core/doc/../../pdf/...` un-normalised, and the layer lookup matches on **prefix**, so an outward include was filed under `core/`); `#include <pdf/cos/CosDict.h>` (dismissed as external "by convention", though every layer target puts the source root on its include path, so it compiles like the quoted form); and `QFile` + `QIODevice::WriteOnly` in `ui/` (the write list had `QSaveFile` but no Qt open-mode at all). | The gate fails on a violation however it is spelled | **S1** | M-05, 2026-08-19. Probe tree of the three, plus the canonical form as a positive control in the same tree: canonical caught, all three missed. Re-run after the fix: **all three caught** | **Fixed** - `resolveInclude` normalises `..` segments and resolves angled includes that exist under the root; the write list gains `QIODevice::WriteOnly/Append/Truncate`, `QTemporaryFile`, `QFile::remove/rename/copy`. Matching is on the **open mode**, not on `QFile`, which is equally the idiomatic way to READ and would have buried the rule in waivers. A fourth spelling found on the second pass: `#include "Pdf/Cos/CosDict.h"` resolved on a case-insensitive filesystem and came back spelled as written, while the layer lookup's prefix test was case-sensitive - `startsWith` in `RuleSet.cpp` now folds case, which is safe because the tree's own paths are lowercase by convention. Five gate cases, one per spelling, plus a negative control that prose naming the forbidden things stays legal. `tests/gates/fixtures/layer-lint/README.md` added, listing what is covered **and what is not** - block comments, string literals holding `FPDF_`, computed includes, a write behind a helper - because the absence of that list is why three spellings went untested. Real tree still clean. 36/36 gates, 63/63 on `x64-debug` |

---

## 9. Sign-off criteria

Stage 0 is signed off when **all** of the following hold — this is
[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions)
made concrete for this stage:

- [ ] Every **automated** case A-01 … A-73 is ✅, run by me, with actual output reported.
- [ ] Every **manual** case M-01 … M-07 is ✅, walked by you on the rig in §2, and **signed off by you** —
      I do not mark these.
- [ ] Every caveat in §7 has a human verdict, not an assumption.
- [ ] No open **S1** or **S2** in the issue log; every ❌ is either fixed and re-run or triaged into an
      accepted, written deferral.
- [ ] **A5 and A6 verdicts are recorded** in `Tech-Stack-Plan.md` §3.3 and the Stage 0 status block —
      including a negative verdict with its fallback.
- [ ] `Architecture-FileMap.md` S0 rows flipped ○ → ✅, **and the rows missing for the build system,
      CMake modules, gate fixtures, CI workflow, and scripts added** ([plan §10](./Stage0-Toolchain-Plan.md#10-lockstep-debt-this-stage-creates)).
- [ ] `Implementation-Plan.md` Stage 0 status, §5's guide row, and `README.md`'s *Current state* updated
      to reality, deviations included.

**"Automated green, manual not yet walked" is a legitimate state to report. It is not done.**
