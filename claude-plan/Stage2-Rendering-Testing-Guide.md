# Stage 2 — PDF Engine Boundary & Tiled Rendering · Testing Guide

> **Authoritative verification document for Stage 2.** Written from
> [`Stage2-Rendering-Plan.md`](./Stage2-Rendering-Plan.md) and
> [`Implementation-Plan.md` § Stage 2](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering),
> **before any Stage 2 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** 🟡 **Written 2026-08-31; T1–T3's cases run 2026-09-01.** **75 automated**
> (A-01 … A-74, plus **A-75** added the same day — see I-02) and **10 manual** (M-01 … M-10), of which
> **A-01, A-03, A-05 … A-13 are green and A-46 is partly green** — every other box is still ☐,
> including **A-04**, which is a Linux job this machine does not run.
> **A2 has its first recorded verdict: `go`** (the probe; the product's is T18's — D2-1), in
> [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), with
> risk **R5** struck through in the same change. The
> issue log holds five entries. **I-01** (S3) — two
> `Architecture-FileMap.md` stage tags that disagreed with `Implementation-Plan.md`, **found by reading
> rather than by running**, confirmed with the user and **closed the same day**. **I-02** (S2, **open**)
> — found by *running* precondition **P-3**'s vcpkg dry-run: both features resolve, and the resolved
> closure pulls in four ports the licence gate structurally cannot see. Owned by T7 and T16.
> **I-03** (S3, closed at T1) — three `QImage` signatures in `Implementation-Plan.md`'s Stage 2
> component list that `core/` cannot name, found by writing the interface they describe.
> **I-04** (S3, open, T14) — no rule compares a key a source *references* against the catalogs, and
> eighteen referenced keys have no entry. **I-05** (S2, open, T11) — **PDFium cannot be called from
> two threads at once**, so D2-9's per-worker thread confinement is not sufficient and D10's worker
> pool cannot be eight PDFium threads. Found by the T3 probe on its first run, which is what that
> probe is for.
>
> **Two halves, per [`CLAUDE.md`](../CLAUDE.md): [§5 Automated](#5-automated-verification--i-run-this)**
> — I run it and report the actual output — and **[§6 Manual](#6-manual-verification--you-run-this)** —
> you walk it on real hardware, and **I never mark a manual case passed on your behalf.**
>
> **This stage's manual half is SMALLER than Stage 1's — ten cases against twelve — and that is a
> claim, not an accident.** Stage 1 was a window, and ribbon feel, key-tip navigation and whether a
> Traditional Chinese label fits its button were structurally out of an offscreen run's reach. Stage 2
> is mostly arithmetic: DPI resolution, tile geometry, page arrangement, xref chains and filter
> round-trips are all pure functions, and every one of them is automated below. What is left for a
> person is genuinely irreducible — **two monitors at different scale factors, perceived smoothness
> while scrolling, rasterization fidelity judged beside a reference, and a memory checker that is not
> installed on the CI runner.** Anything in §6 that could have been automated is a defect in this
> guide; say so and I will move it.
>
> **Label scoping.** `A-nn` and `M-nn` here are **this guide's**; Stage 0's and Stage 1's guides have
> their own `A-01`. `A2` (no hyphen) is the render-throughput **risk** in `Tech-Stack-Plan.md` §3.3.
> `T1`…`T18` are **Stage 2** tasks (`Stage2-Rendering-Plan.md` §6); `D2-n` are Stage 2 decisions from
> that file's §3. `I-nn` here are **Stage 2** issues — a citation of "I-34's lesson" means **Stage 1's**,
> and says so.

---

## 1. Scope

### In scope — must pass for sign-off

- A PDF **opens**: bytes are parsed, pages are counted, and a page is rasterized.
- The **engine boundary** holds — no PDFium type, handle, or exception outside `pdf/render/`.
- The **first-party COS reader** parses the corpus, survives `corpus/malformed/`, and reports what it
  had to reconstruct.
- **Tiled rendering at the right resolution**: `DpiPolicy`, `TileGrid`, the scheduler's cancellation
  path, the cache's `Scope`/`Revision` invalidation.
- **A page on screen**, scrollable and zoomable, in all four layout modes, at each monitor's physical
  DPI, surviving a drag between monitors at different scaling.
- The **§8 Viewer key bindings** listed in `Implementation-Plan.md` § Stage 2, and the first
  `KeyContext` declaration the product has ever had.
- **A2 has a recorded verdict with numbers**, and **O7** and **O10** are resolved.

### Out of scope — do NOT file these as bugs

These are planned absences, itemized in [`Stage2-Rendering-Plan.md` §11](./Stage2-Rendering-Plan.md).
Reporting one costs a triage cycle and, worse, invites someone to build it early.

| Not a bug this stage | Arrives at |
|---|---|
| **Nothing can be changed or saved.** `Ctrl+S`, `Ctrl+Z`, `Ctrl+Y`, `Ctrl+R` (rotate pages) do nothing and consume no key | Stage 3 |
| No text selection, no copy, no search, no `Ctrl+F` | Stage 5 |
| No annotations, forms, security editing, signatures, or export | Stages 6–11 |
| No split views, rulers, grid, guides, full screen, presentation, or session restore. `Ctrl+U`, `Ctrl+'`, `Ctrl+;`, `Ctrl+L` are unregistered | Stage 4 |
| No thumbnails, bookmarks or destinations pane — the sidebar still holds nothing document-shaped | Stage 4 |
| No Reflow view (`Ctrl+4`) | Stage 13 |
| **An encrypted file opens through PDFium and the COS reader declines it by name** (D2-6). That decline is correct behaviour, not a parse failure | Stage 10 |
| **No accelerated presentation.** `WinAcceleratedSurface` is declared and does nothing; tiles paint through Qt | Stage 4, if A2 says it is needed |
| Twenty-one of 26 Preferences groups are still absent; five ship after T15 | Stage 14 |
| `general/touchMode` still reads back and does nothing (Stage 1's I-38) | Stage 14 |
| **No icons anywhere.** Still true, still deliberate | when there are assets |
| Most of the 361 declared commands are still disabled and say so. Registration goes from 23 to roughly 45 | per feature stage |

### What this corpus cannot tell us

**Every file in `corpus/` is generated by us** (D2-11). That buys reproducibility — a PDF xref stores
absolute byte offsets, so a generated file is the only kind that survives editing — and it removes any
licence question from committed data, which is a gap **no gate in this project closes**, since
`license-audit` governs linked dependencies and says nothing about data.

It also bounds what a green run means, and the bound is stated here rather than discovered at Stage 5:

> **A generator emits only what its author already understood.** `tests/golden/` therefore proves we
> rasterize **our own output consistently**. It does **not** prove we rasterize **real-world PDFs
> correctly**. Real-producer constructs — a scanner's CCITT quirks, InDesign transparency, an unusual
> colour space, a broken-but-tolerated producer string — are **outside Stage 2's evidence entirely**.

Two consequences for how this guide is read. **A-48's green is a narrower claim than it looks**, and
the sign-off says so in those words (§9.7) rather than letting *"every corpus file renders correctly"*
stand unqualified. And **M-03 is not the real-world fidelity case it would be with a curated corpus** —
comparing two renderers over the same generated file still catches a wrong glyph weight or missing
content, which is worth walking, but it cannot catch what we never generated. The trigger for
revisiting all of this is named in D2-11's *Revisit* column: the first real-world rendering bug the
corpus cannot reproduce.

### Case anatomy

Every case is **ID · Goal · Pre · Steps · Expect · If it fails**, with a `☐` you change to ✅ or ❌.
A case either passes or yields a **specific** failure — "looks fine" is not a result.

### Two rules this guide is written under, both bought with Stage 1's issues

**Rule 1 — a case that reaches a behaviour through a back door must say so at the assertion, and name
what it therefore does NOT cover.** This is convention 19 in [`CLAUDE.md`](../CLAUDE.md), and Stage 1
paid for it six times — five of them S2: **I-34** (re-dock, asserted by calling the destination method,
unreachable by hand for six days), **I-42**, **I-43** (S3), **I-47**, **I-49** and **I-54** — every one
green in CI while a person could not perform the thing at all. The rule is *not* "test the gestures"; offscreen there is
no pointer and that limit is real. It is that the back door is **named at the assertion**, so the next
reader subtracts it instead of adding it.

**Rule 2 — a repaired case is re-walked from step 1, never signed off on the strength of the repair.**
Stage 1's **I-35**, **I-45** and **I-52** were each reachable *only after* an earlier fix, and I-45 was
a regression inside I-44's own fix, found by the user re-walking within the hour. A fix creates a code
path that has never been walked.

**And the one to fear most in a rendering stage — Stage 1's I-36.** Eleven green ribbon cases asked the
tab bar what it *contained*: page count, current index, which tabs the chevron had taken. All correct,
all answered from the widget's own members — while the **layout was giving that bar zero pixels** and
no tab had been on screen for six days. A page view has exactly this failure mode. So: **assert the
geometry the layout gave the widget, then assert the pixels it painted.** `QWidget::grab()` works
under `offscreen`; a case that can read pixels back must not settle for reading a queue.

### Severity

| Sev | Meaning |
|---|---|
| **S1** | A PDFium type or handle escapes `pdf/render/`; a malformed file crashes rather than reports; a stale tile is shown; **A2 cannot be judged**; the headless/portable guarantee is broken |
| **S2** | A documented behaviour is missing or wrong in a way a later stage will build on — a layout mode is wrong, cancellation does not cancel, a repaired file reports nothing, a Viewer chord resolves in the wrong context |
| **S3** | A diagnostic is unclear, rendering is visibly imperfect in a way a user would notice, a figure is reported wrongly |
| **S4** | Polish nobody outside the team would notice |

**An S1 here differs from Stage 1's.** There it meant the architecture's load-bearing claim was
already false. Here it means **the product shows the user something untrue about their document** —
a stale tile, a silently half-parsed file, a page that is not what the bytes say. That is the failure
class a PDF editor cannot ship with.

---

## 2. Build, launch, environment

`cmake`, `ninja` and `g++` are **not on this machine's `PATH`**. Every command below assumes
`scripts\env.ps1` has been dot-sourced first, and a case that works only *without* it is itself a
finding.

```powershell
. .\scripts\env.ps1
cmake --preset x64-release
cmake --build --preset x64-release --target checks     # THREE gates, still three
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure
.\build\x64-release\bin\rawpage.exe corpus\ordinary\text-a4.pdf   # a PAGE appears — this is new
```

Diagnostics the entry point answers, all usable under `QT_QPA_PLATFORM=offscreen`:

```powershell
.\build\x64-release\bin\rawpage.exe --version              # + the PDFium version, new at T2
.\build\x64-release\bin\rawpage.exe --print-dpi-awareness  # expect PerMonitorAwareV2
.\build\x64-release\bin\rawpage.exe --dump-ui-strings      # the headless smoke test
.\build\x64-release\bin\rawpage.exe --list-actions         # NEW at T18 — Stage 1's A-25, finally real
```

Two checks that are neither a `ctest` case nor a gate:

```powershell
.\build\x64-release\bin\rawpage_bench_render.exe    # A2's product figure, and O7's cache statistics
.\scripts\check-single-instance.ps1                  # unchanged from Stage 1
```

**Two notes on running `rawpage.exe` from PowerShell**, carried from Stage 1 because they still cost
time to rediscover. PowerShell does **not wait** on a GUI-subsystem binary, so `$LASTEXITCODE` after
`& .\rawpage.exe` reads the *previous* command's — use `Start-Process -Wait -PassThru` and read
`.ExitCode`. And an ordinary launch runs an event loop and does not return; `--dump-ui-strings` is the
switch that composes everything and exits.

### Settings keys that affect these cases

Sixteen keys arrive at T15 and every one of them changes what a case sees. Before reporting a
rendering difference, check these — a case walked against a non-default `pageDisplay/` is not a
finding, it is a configuration.

| Key | Default | Affects |
|---|---|---|
| `pageDisplay/layoutMode` | Single Continuous | M-05 |
| `pageDisplay/defaultZoom` | Fit Width | M-05, M-06 |
| `pageDisplay/showCoverInTwoUp` · `rtlBinding` | off · off | A-33, A-34, M-05 |
| `pageDisplay/antialiasText` · `antialiasImages` · `antialiasLineArt` | on · on · on | A-65, M-03 |
| `pageDisplay/lineWeight` | off | A-65, M-03 |
| `pageDisplay/transparencyGrid` | off | M-03 |
| `pageDisplay/maxRenderResolution` | *(to be set at T15)* | A-30, M-06 |
| `pageDisplay/wheelZooms` | off | M-04 |
| `performance/renderWorkers` | 8 | A-45, A-68, M-04 |
| `performance/tileCacheMB` · `diskCacheMB` | *(T15)* | A-40, A-70 |
| `performance/prefetchPages` | *(T15)* | A-31, M-04 |
| `performance/throttleOnBattery` | on | A-62 |

`settings.ini` lives at `%APPDATA%\RawPage\settings.ini`. Stage 1's convention 4 still applies: it is
read through our own codecs, never through `QVariant`'s conversions, and `[general]` is QSettings'
reserved fallback section.

### Test rig

| | |
|---|---|
| **Monitors** | **Two, at different scale factors, and they match A2's specification.** Measured 2026-08-31: a 4K panel — 3840×2160 physical, 2560×1440 logical, therefore **150 %** — plus a 2560×1440 at **100 %**. This is precondition **P-1** (`Stage2-Rendering-Plan.md` §7) and it is ✅ satisfied |
| **CPU** | 16 cores / 32 threads, so `performance/renderWorkers` = 8 is a real eight-worker pool rather than eight threads fighting over four |
| **The rig is asserted, not assumed** | **A-60** reads the topology back through the product's own `WinDisplayTopology` and fails if it does not see both monitors with those scale factors. The measurement above was taken with a query whose DPI awareness is unknown, and a DPI-unaware caller receives *virtualized* figures — it agrees with itself while being wrong. A rig that has only been measured by the thing that might be broken is not a rig |
| **Reference renderer** | **Its engine must be INDEPENDENT of PDFium** — precondition **P-4**, ✅ **satisfied 2026-08-31**, for M-03 and M-05. Primary: **Adobe Acrobat (64-bit) 26.001.21771** at `C:\Program Files\Adobe\Acrobat DC\` — Adobe's own engine, and the baseline real-world PDF producers target. Secondaries, both independent and both already present: **SumatraPDF 3.5.2** (MuPDF) and **Firefox 154** (pdf.js). ⛔ **Microsoft Edge and Google Chrome are DISQUALIFIED** — both render through PDFium, the library we link, so a comparison against either is our PDFium against their PDFium: it agrees perfectly and proves nothing. Three engines that share nothing with ours is what makes M-03 worth walking |
| **The `.pdf` association points at us** | `HKCU\...\FileExts\.pdf\UserChoice` is `Applications\rawpage.exe` — the hand-made association from Stage 1's M-07 walk (its **I-46**), and **re-checked after Acrobat was installed on 2026-08-31: the Adobe installer did not claim it.** Harmless, but it means double-clicking a corpus file opens **RawPage**, not a reference, so M-03 and M-05 must open the reference explicitly via right-click → *Open with* |
| **Memory checker** | Dr. Memory or Application Verifier, for M-09. **Not on the CI runner**, which is why that case is manual (`Stage2-Rendering-Plan.md` §9) |
| **Corpus** | `corpus/` after T4, and **every file in it is generated by a script we own** — nothing borrowed, nothing downloaded (D2-11, decided 2026-08-31; precondition **P-2** was withdrawn with that decision). ~20 ordinary files carrying DCT/JPX/CCITT image streams, shading patterns, transparency groups, hairline vector art, an embedded CJK subset and a form-heavy page; `corpus/malformed/` with each damage described exactly; encrypted, tagged, 900-page, and odd-count-plus-cover-plus-RTL files. Every file has a `corpus/PROVENANCE.md` row naming its generator and its purpose, and **regenerates byte-identically from a clean checkout**. **Read §1's *What this corpus cannot tell us* before judging any rendering case.** |

---

## 3. Verdicts this guide must produce

A stage that is green but has no verdicts is not signed off. Stage 2 owes three.

| Verdict | Assembled from | Recorded in |
|---|---|---|
| **A2** — is PDFium's raster throughput sufficient for smooth scrolling at the target DPI? | **The probe** at T3 (early, throwaway) and **A-68** at T18 (the shipped pipeline). Plus **M-04**, which is the only thing that can judge *perceived* smoothness — a p95 under budget and a scroll that stutters is a finding about the scheduler, not about PDFium | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified). **Both figures, the second labelled as the product's.** Risk **R5** in §10 struck through in the same change |
| **O7** — is the render cache's disk tier shared between documents or per-document? | **A-70** — hit rate, eviction rate and inter-document key overlap over the corpus | [`Implementation-Plan.md` §8](./Implementation-Plan.md#8-open-decisions-carried-forward), struck through and dated |
| **O10** — how is sanitizer coverage obtained? | **A-24** (ASan over the whole corpus parse), **A-25/A-26** (the first fuzz targets that have ever existed), and **M-09** — which is the half that says what the portable route demonstrably **cannot** reach | `Implementation-Plan.md` §8 |

**A2's verdict scale**, fixed here before the measurement so it is not graded against whatever was
achieved:

| Result | Means |
|---|---|
| **go** | p95 under 16.667 ms through the shipped pipeline at the target DPI. D10 stands unchanged |
| **go, with named custom work** | Over budget only in named, bounded cases (a specific filter, a specific page class), each with a named mitigation. D10 stands and the work is scoped |
| **no-go** | Over budget on ordinary text pages. **D10's tiling policy is reopened before anything is built on it** — the stage stops at T3 |

---

## 4. Traceability — exit criterion → case

Every exit criterion in
[`Implementation-Plan.md` § Stage 2](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering),
and the case that closes it.

| Exit criterion | Closed by |
|---|---|
| Every corpus file opens and renders correctly | A-42, A-44, A-48, **M-07** |
| `corpus/malformed/` included, with no crash | A-21, A-22, A-24, **M-07** |
| No ASan finding on the portable COS run | A-24 |
| No Dr. Memory finding on the raster run | **M-09** |
| Golden baselines committed | A-48, A-49, A-50 |
| Scrolling a large document stays responsive | A-36, A-37, A-68, **M-04** |
| Mixed-DPI dragging is correct | A-28, A-29, A-60, A-61, **M-01**, **M-02** |
| **A2 has a recorded verdict with numbers** | A-68, **M-04**, §3 |
| *(plan §13)* The engine boundary holds | A-03, A-10 … A-12, A-45 |
| *(plan §13)* O7 and O10 resolved | A-70; A-24 + A-25 + A-26 + **M-09** |

---

## 5. Automated verification — I run this

Every case names its exact command and its pass condition. I report the **actual** output; a failing
case is reported as failing, with that output, and is never declared fixed without a re-run.

> **Nothing below has been run.** Every box is ☐. When a case is ticked it carries the evidence it was
> ticked on — Stage 1's **I-32** was a guide whose summary claimed 73 green while 46 cases held no
> recorded result, and a guide whose summary is current and whose cases are stale is worse than one
> that is uniformly stale, because the summary is what people read.

### 5.1 Build, the engine boundary, environment

**A-01 · Configure and build, both presets**
✅ **at T1, 2026-09-01 — re-run at every task.** **Goal** Stage 2's sources compile under the pin with
`-Werror` in `core/` and `pdf/`.
**Steps** `cmake --preset x64-debug`, `--preset x64-release`, build both.
**Expect** Exit 0 on all four. No warning in `core/`, `pdf/`, `platform/`.
**Result at T1** Exit 0 on all four; no warning anywhere. `ctest` **367/367** on both presets (354 at
the end of Stage 1: +6 `unit.` backend cases, +5 `gates.headless_assert.*`, +2 `gates.layer_lint.*`),
and the bare `gates` preset **73/73**.
**If it fails** S1 — nothing else runs.

**A-02 · The `ui/` warning grace list is still empty**
☐ **Goal** D1-12 (`Stage1-AppShell-Plan.md` §3) survives the largest `ui/` addition since Stage 1 T10.
**Steps** Read `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake` after the build.
**Expect** Empty. `ui/viewer/` is new code with no legacy, so there is no honest reason for an entry.
**If it fails** S3, and worth an argument rather than a shrug — the list may only shrink.

**A-03 · `rawpage_tests` cannot link PDFium — the D2-2 assertion**
✅ **2026-09-01 (T1).** **Goal** The strengthened `rawpage_assert_headless()`. **This is the case that
protects the whole portable story**, and it must fail for the right reason (convention 7).
**Steps** With a fixture that adds `rawpage_pdfium` to `rawpage_tests`' link libraries, configure.
Then remove it and configure again.
**Expect** The first **fails configure** with the assertion's named message. The second succeeds.
**Result** Built as a CTest fixture rather than a hand edit, so it is re-run on every configure:
`tests/gates/fixtures/headless-assert/`, five cases, all green on `x64-release`, `x64-debug` **and the
bare `gates` preset** — it is a `LANGUAGES NONE` project, so it also runs on the Linux gates runner,
which is the closest that job gets to the portable presets it defends.

- `gates.headless_assert.clean_closure_is_allowed` — **the control, and it comes first**: without it
  every case below is equally satisfied by an assertion that refuses everything.
- `…direct_pdfium_edge` — one line added to the suite's own link list.
- `…transitive_pdfium_edge` — **the one that actually happens**: nobody edits `tests/unit` at all,
  `rawpage_cos` grows the dependency, and the leak arrives through the closure.
- `…pdfium_name_without_target` — the forbidden **name** with nothing defining it, which is the
  portable-preset shape.
- `…ui_edge_still_caught` — Stage 1's two entries still fire after the function moved out of
  `tests/unit/CMakeLists.txt`.

The fixture **includes `cmake/RawPageHeadless.cmake`** rather than carrying a copy — which is why the
function was moved there — and it earned that on its first run: the assertion was reading
`LINK_LIBRARIES` only, so an INTERFACE or IMPORTED target (`Qt6::Core` among them) kept its edges
where the walk never looked, and it reported a clean closure it had never finished traversing. Both
properties are read now.
**If it fails** S1 — the unit suite is linking a PDF engine while the assertion that exists to prevent
exactly that is passing.

**A-04 · `portable-asan` still configures and runs**
☐ **Goal** The split (D2-2) kept `pdf/` buildable on Linux, where no PDFium exists.
**Steps** `cmake --preset portable-asan` and build, on the Linux job.
**Expect** Configure succeeds; `rawpage_cos` builds; `rawpage_pdfium` is **not** in the build graph.
**State after T1** Still ☐, and deliberately so: this is a Linux job and **CI is what runs it**, not
this machine. What T1 can say is that the split leaves `pdf/` holding exactly one portable
translation unit (`BackendFactory.cpp`, which names no engine type), that `rawpage_pdfium` is
declared nowhere yet, and that the forbidden-name check does not depend on that target existing —
which is the half of the property a Windows run can establish. Reported as unrun rather than inferred
from it.
**If it fails** S1 — this is the failure mode D2-2 was written to prevent, and it is silent: a job that
stops configuring reads as infrastructure trouble rather than as a layering breach.

**A-05 · The PDFium version pin fails configure on a mismatch**
✅ **2026-09-01 (T2).** **Goal** D2-3. **Steps** Edit `third_party/pdfium/VERSION` to a wrong build
number; configure. Restore it; configure.
**Expect** The first fails with a named diagnostic naming both the expected and found versions. The
second succeeds. **A control:** the diagnostic must name the *found* version, or it cannot be acted on.
**Result** Walked by hand against the real tree — `BUILD=8009` → `9999` gave
`PDFium pin violated: PA-3 PDFium version / found: 153.0.9999.0 / expected: 153.0.8009.0` and the
restore configured clean. Then built as **seven CTest cases** so it is re-run rather than remembered:
`gates.pdfium_pin.*` over committed four-line fixture trees, which is what lets them run **on the
bare Linux `gates` runner with no PDFium fetched at all** — testing the assertion and testing this
developer's `third_party/` are different things, and only the first is worth a case.

- `correct_tree_is_accepted` — **the control, and it comes first.**
- `wrong_version_names_both` / `..._names_expected` — the two halves of the diagnostic, asserted
  separately so a failure says which half broke.
- `v8_enabled_is_refused` (PA-4) — the wrong *archive* rather than the wrong version.
- `unparseable_version_says_so` (PA-2) — with `EXPECT_NOT_MATCH "PA-3"`, because a `VERSION` file
  that states no version must not fail dressed as a version mismatch.
- `missing_import_library` (PA-5) — the state of a tree unpacked by hand: upstream ships only an
  MSVC-format import library, and without this the failure is a wall of undefined `FPDF_` references.
- `unfetched_tree_says_run_the_script` (PA-1) — what a fresh clone hits.

**If it fails** S2 — golden baselines become claims about an unknown rasterizer.

**A-06 · `--version` reports the PDFium version**
✅ **2026-09-01 (T2).** **Steps** `rawpage.exe --version` under `offscreen`.
**Expect** Exit 0; the RawPage name, build id, Qt version, **and PDFium 153.0.8009.0**.
**Result** Exit 0:

```
RawPage 0.1.0
build-id 14F8446940C19B6547E6B04AA77F844D1
Qt 6.11.1
PDFium 153.0.8009.0 (pinned at build)
```

The parenthesis is deliberate and is not padding: the `FPDF_` C API exposes **no version call**, so
this is the pin `cmake/PdfiumAssert.cmake` verified against `third_party/pdfium/VERSION` at configure
time. The one reader who cares about this line is usually the one wondering whether the `pdfium.dll`
beside the executable is still the one it was built against, and the line says what it can answer.
**If it fails** S3 — but it is the fastest way a person tells which engine a bug report came from.

**A-07 · The executable still starts and exits cleanly under `offscreen`**
✅ **2026-09-01 (T2).** **Goal** The property every later stage's headless smoke test depends on, now
with an engine linked.
**Steps** `$env:QT_QPA_PLATFORM="offscreen"; rawpage.exe --dump-ui-strings`
**Expect** Exit 0, every shell key and action label resolved, no window, no hang. **PDFium's
initialization must not require a display.**
**Result** Exit 0, 529 lines, no window, no hang. The engine-initialization half is asserted
separately and more strongly by `render.the engine initializes with no display and reports the
pinned version`: `rawpage_render_tests` creates **no `QGuiApplication` at all**, so a process with no
Qt GUI object in it starts PDFium and renders — which is a stronger statement than a `--dump-ui-strings`
run that happens not to touch the engine. **If it fails** S2.

**A-08 · Imports are still msvcrt, never ucrtbase — with `pdfium.dll` now in the picture**
✅ **2026-09-01 (T2).** **Steps** `objdump -p .\build\x64-release\bin\rawpage.exe | Select-String "DLL Name"`, and the same
for `pdfium.dll`.
**Expect** `msvcrt.dll` present, `ucrtbase.dll` **absent** from `rawpage.exe`. `pdfium.dll` present
beside the executable. **PDFium links no CRT at all** — that was Stage 0's A6 finding, and this case
re-checks it now that the DLL ships rather than sitting in a spike.
**Result** `rawpage.exe` imports `Qt6Core/Gui/Widgets`, `ADVAPI32`, `KERNEL32`, `SHELL32`, `USER32`,
`libgcc_s_seh-1`, `libstdc++-6`, `libwinpthread-1`, **`msvcrt.dll`** and **`pdfium.dll`** — and **no
`ucrtbase.dll`**. `pdfium.dll` imports `KERNEL32`, `ADVAPI32`, `GDI32`, `USER32` and **no CRT at
all**, which re-confirms Stage 0's A6 measurement now that the DLL ships beside the executable rather
than sitting beside a spike. `pdfium.dll` is deployed into the output directory by a build step, so a
fresh build tree runs without anything on `PATH`.
**If it fails** S1 — the Stage 0 toolchain pin has regressed at the moment the engine was added.

### 5.2 The gates — still three

**A-09 · `checks` clean, and it still says three**
✅ **at T1, 2026-09-01 — re-run at every task.** **Steps**
`cmake --build --preset x64-release --target checks`
**Expect** Exit 0. `layer-lint` clean over the grown tree; `license-audit` clean; `i18n-lint` clean.
The target reports **three** gates — `mutation-lint` arrives at Stage 3, and a `checks` that claims
four is itself the bug.
**Result at T1** Exit 0, three gates. `layer-lint` clean at **144 files / 999 includes** (141 / 972 at
the end of Stage 1) with the new `core-backend` layer entry live and six filesystem-write waivers
unchanged; `license-audit` clean at 17 components and 4 Qt modules, with its two standing
not-yet-built warnings; `i18n-lint` clean at **79 files / 2 catalogs / 0 waivers**, the catalogs now
**538 keys** each.
**Result at T2** Exit 0, three gates. `layer-lint` clean at **147 files / 1,024 includes** with the
narrowed `fpdf` rule and the new `fpdf-handle` rule live; `license-audit` unchanged; `i18n-lint` clean
at **90 files / 2 catalogs / 0 waivers** — the file count jumped because `pdf/` and `platform/` were
added to its scanned paths, which is half of **I-04**. **If it fails** S1.

**A-10 · `layer-lint`: `FPDF_` outside `pdf/render/` fails**
✅ **2026-09-01 (T2).** **Goal** D2-9's narrowing. **Steps** A fixture placing `FPDF_InitLibrary` in
`pdf/cos/Parser.cpp` — *inside* `pdf/`, which the old rule permitted.
**Expect** Gate fails, naming file, line and rule.
**Result** `gates.layer_lint.fpdf_in_cos` — exit 1, `fpdf`. The fixture also contains a legitimate
call in `pdf/render/`, so the failure is attributable to **which directory** named the engine rather
than to PDFium appearing anywhere in the tree. **If it fails** S1 — the first-party COS layer is
the half of D3 that must never learn about PDFium, and it lives in the directory the old rule allowed.

**A-11 · …and the control: a legal `FPDF_` in `pdf/render/` passes**
✅ **2026-09-01 (T2).** **Goal** Convention 7 — a rule that fails on everything is not a rule.
**Steps** The same call in `pdf/render/PdfiumInit.cpp`. **Expect** Gate clean.
**Result** `gates.layer_lint.fpdf_render_allowed` — exit 0, clean, over four call spellings. Worth
noting what this closes: `gates.layer_lint.fpdf_outside_pdf` has proved since Stage 0 that naming
PDFium **outside** `pdf/` fails, and until now **nothing had ever proved that naming it inside
succeeds** — so that case was equally satisfied by a gate rejecting every mention of PDFium in the
tree, which is not a stricter rule but a rule that forbids the product. **If it fails** S2.

**A-12 · `layer-lint`: a PDFium handle typedef outside `pdf/render/` fails, with its control**
✅ **2026-09-01 (T2).** **Steps** A fixture naming `FPDF_DOCUMENT` in `core/render/RenderRequest.h`;
then the same typedef used legally inside `pdf/render/`.
**Expect** Fails, then clean. **Vary the spelling** — `FPDF_DOCUMENT`, `FPDF_PAGE`, `FPDF_BITMAP` —
because Stage 0's I-15 was a gate that passed on real violations since every fixture used one spelling.
**Result** Three fixtures, exit 1 with kind `fpdf-handle` in each, and **no `FPDF_` call appears in
any of them** — which is the point: a type can hold the engine's object graph without ever calling
it. `fpdf_handle_outside_render` (three spellings in one `core/` struct), `fpdf_handle_in_cos` (a
fourth, one directory deeper), and `fpdf_scoper_in_core`.

**That third one is the case that makes the companion rule worth having, and writing it is what
found the gap.** The raw typedefs all begin with `FPDF_` and are therefore already caught by A-10's
rule — but PDFium ships `include/cpp/`, whose **`ScopedFPDFDocument`, `ScopedFPDFPage`,
`ScopedFPDFBitmap`** and the `FPDFDocumentDeleter` family contain **no underscore after `FPDF`**, so
a substring search for `FPDF_` does not see them at all. A `core/` struct holding a
`ScopedFPDFDocument` is this boundary's exact failure mode, spelled the one way the other rule
misses. **If it fails** S1.

**A-13 · `license-audit` sees PDFium as BSD-3 and stays clean**
✅ **2026-09-01 (T2).** **Steps** `license-audit.exe --root . --explain`
**Expect** PDFium listed, SPDX `BSD-3-Clause`, inside the allowlist. The GCC runtime still
`GPL-3.0-or-later WITH GCC-exception-3.1`. No `GPL` **substring** match — whole expressions only.
**Result** Clean — 17 components, 4 Qt modules, 2 standing not-yet-built warnings (openssl,
tesseract). PDFium is listed at 153.0.8009.0, SPDX `BSD-3-Clause`, inside the allowlist; the GCC
runtime is still `GPL-3.0-or-later WITH GCC-exception-3.1` and matched as a whole expression.
**Nothing in T2 changed this gate** — `spdx-map.json` has carried PDFium since Stage 0 T12, which is
the case passing for the right reason rather than a change of its own.
**If it fails** S1 — this is the gate that mechanically enforces D4.

**A-14 · `i18n-lint` clean with the new catalog families**
☐ **Steps** `i18n-lint.exe --root . --explain`
**Expect** Clean. The new `prefs.*` rows for sixteen settings keys and the `error.*` keys for the COS
reader's named failures present in **both** shipped locales. Waiver count still low and censused.
**If it fails** S2 — and a *literal* reaching a message bar is the likely shape, since `MessageBar` is
this stage's first new user-facing surface.

**A-75 · `license-audit` sees the RESOLVED closure, not just the declared manifest**
☐ **Goal** I-02. Added 2026-08-31, after the guide was written, when P-3's dry-run showed the
`codecs` and `text` features pull in four ports `vcpkg.json` never names.
**Steps** With both features active, list the resolved closure
(`vcpkg install --dry-run --x-feature=codecs --x-feature=text --triplet x64-mingw-dynamic`), then run
`license-audit.exe --root . --explain` and compare the two lists.
**Expect** Every port in the closure has a licence verdict. Today **four do not appear at all** —
`brotli` (MIT), `bzip2` (`bzip2-1.0.6`), `liblzma` (0BSD) and `libiconv` (**LGPL-2.1-or-later**) —
because `Inventory.cpp` builds its list from `vcpkg.json`'s `dependencies` and `features` arrays plus
the non-vcpkg overrides, and never enumerates what vcpkg actually installs.
**If it fails** S2. **Not because anything here is GPL** — none of the four is — but because
[`CLAUDE.md`](../CLAUDE.md) and `Tech-Stack-Plan.md` §8.2 both describe this gate as *what
mechanically enforces the no-GPL rule*, and a gate that audits what we **wrote down** rather than what
we **ship** cannot make that claim. Stage 10 (OpenSSL) and Stage 16 (tesseract, leptonica) add much
deeper closures on top of it, and Stage 15's release compliance gate is built directly on this one.

---

### 5.3 The COS reader

**A-15 · `tst_cos_lexer`** ☐ **Steps** `ctest -R unit.cos.lexer`
**Expect** Delimiters, literal and hex strings, numbers, names, comments; tolerant of the malformed
inputs the corpus generator produces on purpose. **If it fails** S2.

**A-16 · `tst_cos_parser`** ☐ **Steps** `ctest -R unit.cos.parser`
**Expect** Objects, indirect objects, streams, object streams. **Identity-preserving round-trip of
anything we do not interpret** — the property Stage 3's byte-identical save depends on. **If it fails** S1.

**A-17 · A `/Length` that lies is recovered — and the control proves the path was taken**
☐ **Goal** The single most common real-world malformation.
**Steps** A generated fixture whose stream `/Length` is 12 bytes short. Then one where it is correct.
**Expect** The first parses to the same content as the second, **and reports that it recovered**. The
control is the second: it must parse *without* the recovery path running, or the case proves nothing
about recovery. **If it fails** S2.

**A-18 · `tst_cos_filters` — each of the nine round-trips**
☐ **Steps** `ctest -R unit.cos.filters`
**Expect** `Flate`, `LZW`, `Ascii85`, `AsciiHex`, `RunLength`, `Dct`, `Jpx`, `Ccitt`, `Jbig2` — each
`encode`→`decode` returns the input exactly, and each `decode` of a corpus-sourced stream matches its
expected bytes. **If it fails** S2.

**A-19 · `CryptFilter` is absent, deliberately**
☐ **Goal** D2-4. **Steps** `ls pdf/cos/filters/`.
**Expect** Nine files, no `CryptFilter`. **If it fails** — that is, if it *exists* — S3 and a scope
finding: a crypt filter with no `StandardSecurityHandler` (Stage 10) has no key to decrypt with, so it
cannot have been tested and will be rewritten.

**A-20 · `tst_xreftable` walks a three-generation incremental file**
☐ **Steps** `ctest -R unit.cos.xref`
**Expect** Classic tables and xref streams both; the `/Prev` chain resolved across three incremental
updates, with the newest generation of each object winning; the trailer read.
**If it fails** S1 — Stage 3's incremental writer appends to exactly this structure.

**A-21 · Every `corpus/malformed/` file yields a report and never a crash — the exit criterion**
☐ **Steps** `ctest -R unit.cos.repair`, iterating the whole directory.
**Expect** For every file: a `Result` that is either success-with-a-`RepairReport` or a **named**
failure. No crash, no hang, no assertion, no unbounded allocation.
**If it fails** S1 — this is the criterion the malformed corpus exists for.

**A-22 · `RepairReport` says how much was reconstructed, not merely that it was**
☐ **Goal** `Stage2-Rendering-Plan.md` §5.4 — Stage 3 forces a repaired file's first save to Save-As,
and *"reconstructed 3 objects of 4,000"* and *"reconstructed the entire xref"* are different risks.
**Steps** Two malformed fixtures at those two extremes.
**Expect** Per-object entries with severity, and a summary a message bar can render in one line.
**If it fails** S2 — Stage 3 inherits a boolean and cannot make the decision the convention requires.

**A-23 · The COS reader declines an encrypted file by name**
☐ **Goal** D2-6. **Steps** Parse `corpus/encrypted/*.pdf` through `pdf/cos/Parser`.
**Expect** A named `Result` error meaning *encrypted, handler arrives at Stage 10* — **not** a parse
failure, not a partial parse, not a crash. **If it fails** S2: two parsers reaching different
conclusions about one protected file is a class of bug that is very hard to see.

**A-24 · `portable-asan` over the whole corpus parse — clean**
☐ **Goal** Half of O10. **Steps** On the Linux job, run the COS suites over every corpus file under
ASan+UBSan.
**Expect** No finding. **A finding here is reproduced in `x64-debug` before it counts as fixed**
(convention 11's corollary). **If it fails** S1.

### 5.4 Fuzzing — the preset's first real targets

**A-25 · `fuzz_cos_lexer` builds and runs 10 minutes clean**
☐ **Goal** `portable-fuzz` has been wired since Stage 0 with its preset description saying **"NO
TARGETS EXIST until Stage 2"**. **Steps** Build on `portable-fuzz`; run seeded from `corpus/malformed/`.
**Expect** Builds; 10 minutes with no crash, no leak, no timeout.
**If it fails** S1 if it crashes; **S2 if it fails to build** — a preset that has never built anything
has not been proven to build anything, which is Stage 1's I-30 in a different costume.

**A-26 · `fuzz_cos_parser` — the primary target — same**
☐ **Steps** As A-25. **Expect** As A-25. **If it fails** S1.

**A-27 · The fuzz targets actually exist in the build graph**
☐ **Goal** The negative that A-25 and A-26 cannot supply for themselves: a target that silently
builds nothing passes a "no crashes" check trivially.
**Steps** Enumerate the `portable-fuzz` build graph.
**Expect** Exactly two fuzz targets. **Zero is a failure, not a pass.**
**If it fails** S2 — and this case exists because Stage 1's I-30 was precisely a job that reported
success by doing nothing.

### 5.5 Render policy — pure, and therefore all headless

**A-28 · `tst_dpipolicy` — `resolve()`**
☐ **Steps** `ctest -R unit.dpipolicy`
**Expect** **Actual Size is 100 % at the monitor's physical DPI** ([`Features.md` §1.2](./Features.md#12-page-display)),
not 100 % of a logical pixel. The 150 % and 100 % rig values both resolve correctly, and so do 175 %
and 350 %, which the rig cannot produce on demand. **If it fails** S2 — this is the file most likely to
be the root cause of a blurry-text bug.

**A-29 · `requiresRerender()` across the threshold, in both directions**
☐ **Steps** One step above and one step below the threshold, increasing and decreasing.
**Expect** Re-render above, resample below, **and the answer is the same going up as coming down** —
hysteresis here means a monitor drag that re-rasterizes one way and not the other.
**If it fails** S2: wrong in one direction is blurry text on every monitor move, wrong in the other is a
stutter on every one.

**A-30 · The extremes: 8 % and 6400 %**
☐ **Expect** Both resolve to a sane device resolution, clamped by `pageDisplay/maxRenderResolution`; no
overflow, no zero, no allocation proportional to the square of the zoom.
**If it fails** S2 — 6400 % of an A4 page at 192 DPI is where an integer overflow lives.

**A-31 · `tst_tilegrid` — visible, prefetch, evict**
☐ **Expect** The three sets are disjoint; the visible set covers the viewport exactly with no gap and
no unnecessary tile; the prefetch set honours `performance/prefetchPages`; a tile that left the
viewport moves to evict. **If it fails** S2.

**A-32 · `tst_presentation` — all four layout modes** ☐
**Expect** Single, Single Continuous, Two-Up, Two-Up Continuous each produce page rects in document
order with correct gaps, and page-rect ↔ document-point mapping round-trips. **If it fails** S2.

**A-33 · Cover page shifts every subsequent pair's parity**
☐ **Steps** Two-Up with `showCoverInTwoUp` on, over an odd page count.
**Expect** Page 1 alone, then 2–3, 4–5. Off: 1–2, 3–4. **If it fails** S2 — this is where the
off-by-one lives.

**A-34 · RTL binding swaps the spread without changing page order**
☐ **Expect** With `rtlBinding` on, page 2 is on the **right** of the 2–3 spread and page 3 on the left;
the document order is unchanged. Combined with a cover page and an odd count, all three interact —
and the corpus has a file for exactly that. **If it fails** S2.

**A-35 · `RenderRequest` equality is the cache key, and it is a value type**
☐ **Goal** D2-12. **Steps** A static assertion that it holds no pointer and no handle; a runtime case
that two independently constructed equal requests compare equal and hash equal; and one that a
differing `Revision` makes them **unequal**.
**Expect** All three. **If it fails** S1 — a pointer member makes the key identity-based and every
cache lookup misses silently, and a defaulted `Revision` makes *showing a stale tile after a Stage 3
mutation* the default behaviour. Neither has any symptom a person would attribute to the cache.

### 5.6 Scheduler and cache

**A-36 · A cancelled request is never started**
☐ **Goal** The stage's headline behaviour. **Steps** Enqueue, cancel before a worker picks it up,
with the pool paused.
**Expect** The rasterizer is **never entered** for that request — asserted by a counting fake
`IPageRasterizer`, not by observing that no image arrived.
**Back door named:** this drives `RenderScheduler` directly and therefore proves the *destination*.
**It does not prove that scrolling reaches it** — that is **M-04**, and it is manual because
perceived smoothness has no offscreen proxy. (Rule 1, §1.)
**If it fails** S2.

**A-37 · Duplicate requests coalesce**
☐ **Expect** Two viewports over one tile produce **one** rasterization. **If it fails** S3 — twice the
work, correct output.

**A-38 · Delivery is on the UI thread, and a superseded delivery is dropped**
☐ **Expect** `ready` fires on the UI thread; a result arriving for a cancelled or `Revision`-superseded
request is dropped rather than cached. **If it fails** S1 — the second half is how a stale tile reaches
the screen.

**A-39 · A `PageView` destroyed with tiles in flight does not crash**
☐ **Goal** Convention 10 (`CLAUDE.md`) in its threaded form — Stage 1's I-12 was an S1 segfault of
exactly this shape, a lambda outliving the widget that installed it.
**Steps** Open, scroll to enqueue, close the tab while four tiles are in flight. Repeat 50 times.
**Expect** No crash, no use-after-free under `x64-debug`. **If it fails** S1.

**A-40 · `tst_rendercache` — LRU and the cap** ☐
**Expect** Eviction is least-recently-used; total held bytes stay under `performance/tileCacheMB`;
the disk tier honours `diskCacheMB`. **If it fails** S2.

**A-41 · `Scope`/`Revision` invalidation, and a stale tile is never served**
☐ **Expect** `invalidate(Scope, Revision)` drops exactly the tiles the scope names and no others; a
lookup for an older `Revision` **misses**. **If it fails** S1 — this is the mechanism that makes
Stage 3's mutations safe, and Stage 2 is where it must already be right.

### 5.7 The engine

**A-42 · Every corpus file opens** ☐ **Steps** `ctest -R render.pdfiumdoc`
**Expect** For each: a document handle and a page count matching `PROVENANCE.md`. The malformed set is
allowed to open *repaired* — but never to open silently wrong. **If it fails** S1.

**A-43 · The password callback** ☐
**Expect** `corpus/encrypted/*.pdf` opens with the right password, is refused with the wrong one, and
the refusal is a **named** `Result` rather than an empty document. **If it fails** S2.

**A-44 · `tst_rasterizer` — one page to a `QImage`** ☐
**Expect** A `RenderRequest` produces an image of exactly the requested pixel size, non-blank, with the
render flags honoured. **If it fails** S1.

**A-45 · Thread confinement — eight workers, concurrently**
☐ **Goal** D2-9. **Steps** Rasterize 200 tiles across 8 workers over 4 documents, repeatedly, under
`x64-debug`.
**Expect** No crash, no corrupted output, no data race. **`PdfiumInit` is entered once.**
**If it fails** S1 — PDFium's global state is the one thing this design must own exclusively.

**A-46 · The backend interface census** 🟡 **partly green at T1, 2026-09-01.** **Goal** D2-7.
**Expect** `core/backend.h` declares all seven; the backend names which it implements; **calling an
unimplemented one returns an explicit not-implemented `Result`, never a silent success or an empty
value.** After Stage 2 the implemented set is `IDocumentBackend` and `IPageRasterizer`.
**Result at T1** `tests/unit/tst_backend.cpp`, six cases in the **headless** suite (it names the
engine boundary and links `rawpage_cos`, so it runs on `portable-asan` too). All seven are declared
and named, with no duplicate and no empty entry; `createBackend()` reports **none** implemented and
`unimplementedBackendInterfaces()` names all seven in enum order; `open` and `close` return
`Unsupported` carrying `error.backend.notImplemented` and a context of `IDocumentBackend::open` /
`::close`; every sub-interface accessor is **null rather than an empty stub**; and the password
callback is asserted **not** to have run, so the case cannot pass against a backend that opened the
file and returned nothing. `notImplementedError` has its own case with a control for the no-method
form. **The last sentence of the Expect is not asserted and cannot be until T9** — that half is
`{IDocumentBackend, IPageRasterizer}` after the real backend exists, and the file says so at the top
rather than leaving a reader to add up what is covered (convention 19).
**If it fails** S1 — convention 6's argument: a stub that silently succeeds surfaces stages later as
data nobody produced.

**A-47 · `openFiles()` asks the backend, not the filename**
☐ **Goal** This replaces Stage 1's A-58, which asserted the opposite and was correct then (D1-2).
**Steps** A `.pdf` file that is not a PDF; a PDF with a `.dat` suffix.
**Expect** The first is **refused with a named reason**; the second **opens**. Deciding whether bytes
are a PDF is `IDocumentBackend`'s job. **If it fails** S2.

### 5.8 Golden images

**A-48 · Baselines at 96 / 144 / 192 DPI over the corpus** ☐ **Steps** `ctest -R render.golden`
**Expect** Every page within the perceptual threshold of its baseline. **If it fails** S2, and the
first question is whether the baseline or the renderer moved — A-05 is what makes that answerable.

**A-49 · A perturbed render fails, and writes a diff image**
☐ **Goal** D2-13, and convention 7 — a golden suite that cannot fail is decorative.
**Steps** Perturb one render flag; run.
**Expect** Failure naming the file and page, **with a diff image written beside the expected and
actual**. **If it fails** S2: "golden mismatch, 0.7 % of pixels" is not a bug report.

**A-50 · `golden-update` refuses without `--reason`** ☐ **Goal** D2-3.
**Expect** Refused, with a message saying why. With `--reason`, the baseline regenerates and the reason
is available to the commit. **If it fails** S2 — the mechanism `Implementation-Plan.md` §2 names has no
teeth without this.

**A-51 · A PDFium version change is visible to the golden suite** ☐
**Expect** With A-05's pin edited, the golden suite does not merely fail — configure never gets that
far. The two mechanisms are complementary and this case proves the ordering. **If it fails** S3.

### 5.9 The viewer — `rawpage_ui_tests`, offscreen

> Everything in this subsection is written against Stage 1's **I-36**: assert the geometry the layout
> gave the widget, **then** assert the consequence. A case that asks a viewer what it contains while
> the layout gives it zero pixels is green and worthless.

**A-52 · The layout gives `PageView` real pixels**
☐ **Steps** `show()` under `offscreen`, then `activate()` **every** layout level (convention 12), then
read `PageView::size()`.
**Expect** Non-zero, and matching the window's central area within the scrollbar allowance. **Assert
the size the widget actually got before asserting anything that depends on it** — `resize(240)`
returning 1044 px is a finding, not a failed assertion.
**If it fails** S1 — and it is the failure that hid for six days at Stage 1.

**A-53 · A page is actually painted**
☐ **Steps** `QWidget::grab()` after tiles have been delivered.
**Expect** The returned image is **not** uniform background: page-shaped content with a page border at
the expected rect. **This is the case that distinguishes "tiles were requested" from "a page is on
screen"**, and it is the whole reason §1's rule exists. **If it fails** S1.

**A-54 · `tst_viewport` — scroll and zoom** ☐
**Expect** Scroll offsets clamp at document bounds; zoom about the cursor keeps the point under the
cursor fixed; page-at-a-time paging snaps to boundaries. **If it fails** S2.

**A-55 · `KeyContext::Viewer` is declared and resolves — the first time in the product's history**
☐ **Goal** D2-8. **Steps** Focus inside `PageView`; ask `ShortcutBinder::activeContext()`.
**Expect** `Viewer`. Then a `Viewer`-bound chord dispatches its Viewer command.
**If it fails** S2 — and note that until T13 this answering `Global` was *correct*, so a regression
here looks exactly like the previous fifteen tasks' correct behaviour.

**A-56 · …and the negative: a Viewer chord does NOT fire from the tool finder's search box**
☐ **Goal** The case that actually tests context resolution. A chord that fires everywhere would pass
A-55 and be wrong.
**Steps** Focus the tool finder's search box; press a `Viewer`-only chord.
**Expect** The Viewer command does **not** run; the key reaches the line edit, through Qt's own
`ShortcutOverride` handshake (convention 15). **If it fails** S2 — and Stage 1's I-54 is the warning:
that defect existed because every case typed at `searchBox()`, the one address a real keystroke does
not reach.

**A-57 · The status bar shows real numbers** ☐
**Expect** Page *n* of *N* with the real total; the zoom label tracks the viewport. The em dash Stage 1
showed for a stub is gone **because there are pages now**, not because it was replaced by a guess.
**If it fails** S3.

**A-58 · The message bar surfaces a repair report** ☐
**Expect** Opening a `corpus/malformed/` file shows a message bar naming what was reconstructed, with
a route to the detail; opening a clean file shows **no** bar. **If it fails** S2 — a repaired file that
says nothing is how an original gets destroyed at Stage 3.

**A-59 · A registered-but-unavailable command says WHY — Stage 1's I-53, finally**
☐ **Goal** D2-10. **Steps** With no document open, hover a **registered** command that is refused
(needs a document). Then hover a **declared-but-unregistered** one.
**Expect** Two *different* tooltips: *"not available for this document"* versus *"arrives in Stage N"*.
Stage 1 built only the second (`Stage1-AppShell-Plan.md` §5.1).
**Note the route:** Stage 1's M-12 found that **no ribbon control can show the first case** — all nine
registered ribbon ids are unconditionally enabled — so this case must reach it through the tool finder
or the toolbar. **If it fails** S3.

### 5.10 Platform

**A-60 · `WinDisplayTopology` sees the real rig — and this is what makes the rig trustworthy**
☐ **Steps** `ctest -R platform.displaytopology` on the P-1 machine.
**Expect** **Two** monitors: one 3840×2160 at **150 %**, one 2560×1440 at **100 %**. Physical pixels
and scale factor reported separately.
**If it fails** S2 — **and treat a disagreement with §2's rig table as a finding about the case, not
about the rig, until proven otherwise**: the table's figures came from a query whose DPI awareness is
unknown, and a DPI-unaware caller is handed virtualized numbers that are self-consistent and wrong.

**A-61 · The `changed` signal fires on a mid-session scale change** ☐
**Expect** Changing a monitor's scale in Windows Settings emits `changed` once, with the new topology.
**If it fails** S2 — this is what M-02 needs in order to be a product test rather than a Windows test.

**A-62 · `WinPowerSession` reports battery and throttles** ☐
**Expect** Battery state readable; with `performance/throttleOnBattery` set and the machine on battery,
the effective worker count drops. On a desktop, the case reports *not applicable* **explicitly** rather
than passing vacuously. **If it fails** S3.

### 5.11 Settings, fonts, glyphs

**A-63 · The sixteen keys round-trip** ☐
**Expect** Each `SettingsKeys.def` row gets/sets its declared type; defaults match §2's table; an
undeclared key still does not compile (D1-10, and the negative-compile fixture still passes).
**If it fails** S2.

**A-64 · Two new Preferences groups are generated, not hand-laid-out** ☐ **Goal** Convention 17.
**Expect** Page Display and Performance appear with one editor per key, built by `SettingsPage`
`#include`ing the `.def`. **Five of 26 groups now ship.** No widget code was added per key.
**If it fails** S2 — a hand-built page means the mechanism has been bypassed and every later stage pays.

**A-65 · Rendering flags visibly change the render** ☐
**Expect** Toggling `antialiasText`, `antialiasImages`, `antialiasLineArt`, `lineWeight` and
`transparencyGrid` each produce a **measurably different** raster of the same page — asserted against
golden variants, not by eye. **If it fails** S2: a setting that reads back and changes nothing is Stage
1's I-38 repeating, and that one was only caught by a person.

**A-66 · A missing embedded font substitutes through OUR policy** ☐
**Expect** A corpus file with a missing embedded font renders, and the case **names the substitute
chosen**, proving `FontResolverBridge` → `platform::IFontResolver` ran rather than PDFium's internal
default. **If it fails** S2 — substitution policy on the wrong side of the boundary is invisible until
a document looks wrong on a customer's machine.

**A-67 · `GlyphCache` hits across tiles and pages** ☐
**Expect** Rendering two adjacent tiles of one text page rasterizes each distinct glyph once.
**If it fails** S3.

### 5.12 The measurement, CI, and Stage 1's carry-overs

**A-68 · `bench_render` — A2's PRODUCT figure**
☐ **Goal** D2-1's second half, and the §3 verdict. **Steps** `rawpage_bench_render.exe` on the P-1
machine: a text-heavy A4 page at 200 % on the 150 %-scaled monitor, 8 workers, over the corpus.
**Expect** p50 and p95 per-tile milliseconds reported against the **16.667 ms** frame budget, in
phases, **and the figure is recorded beside T3's probe figure with the difference explained.** A
benchmark reports; its exit code is about whether the measurement was *possible*, never whether the
machine was fast. **If it fails to measure** S1 against A2.

**A-69 · The benchmark carries its own control**
☐ **Goal** Stage 1's **I-29** — a benchmark whose every printed figure was its own argument-evaluation
bug, and Stage 1 T15's rule that **a 0.000 ms reading has two explanations: a cheap operation, or a
dead instrument.**
**Expect** A control phase with a known non-trivial cost reports a non-zero figure. Without it, no
number in A-68 is evidence. **If it fails** S2 — and every A-68 figure is then unusable, not merely
suspect.

**A-70 · The cache statistics that answer O7** ☐
**Expect** Hit rate, eviction rate and **inter-document key overlap** over the corpus, reported by the
same run. The overlap figure is the one O7 turns on: near-zero overlap means per-document, meaningful
overlap means shared. **If it fails** S2 — O7 stays open for want of a number, which is exactly the
failure `Stage2-Rendering-Plan.md` D2-14 was written to prevent.

**A-71 · CI green, and no suite prefix is empty** ☐
**Expect** All jobs green; `unit.`, `ui.`, `platform.`, **`render.`**, `bench.` each non-empty; the
fuzz job reports two targets built. **A target that failed to build subtracts cases from a run still
reporting "100% tests passed"** — that assertion is why it is here. **If it fails** S2.

**A-72 · `--list-actions` exists — Stage 1's A-25, carried and closed** ☐ **Goal** D2-10.
**Steps** `rawpage.exe --list-actions` under `offscreen`.
**Expect** Exit 0; every declared `ActionId` with its string id, default context, registration state,
and chord. **361+ rows.** **If it fails** S3 — but note this case has been *unrunnable* since Stage 1
and six documents already describe the switch as though it exists.

**A-73 · The `RAWPAGE_ACTION_ALIAS` fixture — Stage 1's A-26, carried and closed** ☐
**Steps** A synthetic alias row for a renamed id; resolve the old string.
**Expect** The old string resolves to the new enumerator; a saved QAT customization and a saved
shortcut both survive the rename. **If it fails** S2 — this is live code with **zero rows and no
test**, and the first stage to rename an id will be trusting a mechanism nothing has ever executed.

**A-74 · `--dump-ui-strings` still exits 0 with every new key** ☐
**Expect** Exit 0; the new `prefs.*` and `error.*` families resolved in both `en` and `zh-TW`; no key
rendering as its own name. **If it fails** S2.

---

## 6. Manual verification — you run this

Ten cases. Every one of them needs a real screen, a real second monitor at a different scale, your
judgement beside a reference, or a tool the CI runner does not have. **Anything here that could have
been automated is a defect in this guide — say so and I will move it.**

**Walk M-01 and M-02 first**, before the fidelity cases: a DPI bug makes every subsequent judgement
about rendering quality unreliable, and you would be grading the wrong thing.

---

**M-01 · Mixed DPI — dragging between the 150 % and 100 % monitors** ☐
- **Goal** The case a single-monitor machine and an offscreen run both structurally cannot reach, and
  the one that decides whether `DpiPolicy` is right.
- **Pre** P-1. A text-heavy corpus file open. Confirm `--print-dpi-awareness` reports
  `PerMonitorAwareV2` **first** — otherwise you are testing Windows' bitmap scaler, not our renderer.
- **Steps**
  1. Open on the 100 % monitor at Actual Size. Read a paragraph of body text closely.
  2. Drag the window to the 150 % monitor. Read the same paragraph. Then drag back. Twice.
  3. Repeat at Fit Width and at 400 %.
  4. Straddle the window across both monitors and leave it there.
- **Expect** Text is **crisp on both monitors** — re-rasterized, not resampled up. No layout jump, no
  change in which page is centred, no change in the zoom percentage. The straddled window is drawn at
  one consistent resolution rather than tearing at the boundary.
- **If it fails** S2. Say **which** direction is wrong — 100→150 blurry means re-rasterization did not
  trigger; 150→100 stuttering means it triggered when a resample would have done. That distinction is
  `DpiPolicy::requiresRerender()` and it is the whole finding.

**M-02 · A scale change while the application is running** ☐
- **Goal** The mid-session change is where per-monitor DPI actually goes wrong, and A-61 can only prove
  the signal fired — not that the product did the right thing with it.
- **Pre** P-1, a document open on the second monitor.
- **Steps**
  1. **With the app running**, change the second monitor's scale in Windows Settings. Watch the window
     on it.
  2. Drag the window across, and back.
  3. Change the **primary** monitor's scale with the window on it.
  4. Log out and back in at a different primary scale; relaunch and reopen.
- **Expect** The page re-renders at the new resolution within a moment; no blurry intermediate state
  that persists; the ribbon, status bar and message bar all follow (Stage 1's R-6 covered the chrome —
  this case is about the chrome **and** the page agreeing).
- **If it fails** S2.

**M-03 · Rasterization fidelity, beside a reference** ☐
- **Goal** The golden suite proves we render **consistently**; a baseline captured from a wrong
  renderer is green forever, and only a person beside a reference can catch that. **Read the scope
  limit before walking this**: the corpus is entirely generated (§1, *What this corpus cannot tell
  us*), so this case compares two renderers over **our own files**. That still exposes a wrong glyph
  weight, a wrong colour, a missing hairline or dropped content — which is most of what it is for. It
  **cannot** expose a construct our generator never emits, so **a clean M-03 is not evidence that
  real-world PDFs render correctly**, and must not be reported as though it were.
- **Pre** P-4 — **Acrobat Reader**, same monitor, same scaling, same zoom, opened via right-click →
  *Open with* (the `.pdf` association points at RawPage). Have **SumatraPDF** and **Firefox** to hand as
  second and third opinions. **Do not use Edge or Chrome**: both are PDFium, so they agree with us by
  construction and a match means nothing.
- **Steps** For six corpus files chosen to differ (dense text; a DCT or CCITT image; vector line art; a
  transparency-heavy page; a CJK document; a form-heavy page), compare side by side at 100 % and at 400 %.
  Then toggle `antialiasText`, `antialiasLineArt` and `lineWeight` and compare again.
- **Expect** No systematic difference in glyph weight, spacing, or colour. Hairlines are visible at
  100 %. With Line Weight on they are drawn at true width. Anti-aliasing toggles do what they say.
- **If it fails** S3 unless text is **unreadable** or content is **missing**, which is S2. Name the file
  and the region, and say whether the difference survives a zoom change — one that vanishes at 400 % is
  a resampling artifact rather than a rasterization one. **Then open the same file in SumatraPDF and
  Firefox before filing**: three independent engines make the finding far sharper. If both agree with
  Acrobat and all three disagree with us, it is ours. If they disagree among themselves, the file is
  ambiguous and the *generator* may be what needs fixing — a T4 finding, not a renderer one.
- **If it passes**, record it as *"consistent with the reference over the generated corpus"* rather
  than as *"rendering is correct"*. The second is a claim this case cannot support, and it is the kind
  of overstatement that survives for stages because nobody re-reads a criterion that already passed.

**M-04 · Scrolling a 900-page document — the perceived-smoothness half of A2** ☐
- **Goal** **The case A-68's number cannot replace.** A p95 under budget and a scroll that stutters is
  a finding about the scheduler, and only a person can tell the difference. It is also the only test in
  this guide that drives cancellation through the gesture that is supposed to trigger it — A-36 drives
  the scheduler directly and says so.
- **Pre** The 900-page corpus file. `performance/renderWorkers` at 8.
- **Steps**
  1. Scroll continuously with the wheel from page 1 to page 200 without pausing. Watch for blank tiles
     that persist, tiles that arrive out of order, and a scroll that stops responding to the wheel.
  2. Drag the scrollbar thumb from top to bottom in one fast motion, then release.
  3. `Ctrl+End`, then `Ctrl+Home`.
  4. Scroll fast, then **immediately** zoom. Then scroll fast and immediately switch layout mode.
  5. Repeat step 1 with `renderWorkers` at 1, then at 16.
- **Expect** Tiles fill in behind the scroll without the window becoming unresponsive; blank regions
  resolve within a moment and do not persist; step 2 does **not** rasterize the 700 pages it passed
  over — cancellation is what step 2 is for; step 4 does not show a tile at the old zoom or the old
  layout. At 1 worker it is slow but **correct**.
- **If it fails** S2 against A2, and say **which** step. Step 2 failing is cancellation. Step 4 failing
  is `Revision`/`Scope` invalidation and is S1 — that is a stale tile, and a stale tile is the product
  telling the user something untrue about their document.

**M-05 · The four layout modes, the cover page, and RTL binding** ☐
- **Pre** The odd-page-count corpus file with a cover page, and an RTL document. **P-4 for comparison —
  Acrobat Reader, not Edge or Chrome.** Layout mode is UX rather than rasterization, so a PDFium-based
  viewer would not be circular here the way it is in M-03 — but its *pairing* behaviour is Chromium's
  rather than a PDF reader's, which makes it the wrong reference for a different reason.
- **Steps** For each of Single, Single Continuous, Two-Up, Two-Up Continuous: page through the whole
  document, use `Ctrl+Shift+H` and `Ctrl+Shift+T`, and check the first and last spread. Then toggle
  `showCoverInTwoUp`, then `rtlBinding`, then both.
- **Expect** The pairing matches the reference. With a cover page, page 1 stands alone and 2–3 pair.
  With RTL, page 2 is on the **right**. Switching modes preserves the current page rather than jumping
  to page 1.
- **If it fails** S2. A-32 through A-34 cover the geometry; if they are green and this is not, the
  finding is in the viewer's use of `PagePresentation`, not in `PagePresentation`.

**M-06 · Zoom extremes — 6400 % and 8 %** ☐
- **Steps** `Ctrl+M`, 6400 %. Scroll around. Then 8 %. Then `Ctrl+0`, `Ctrl+1`, `Ctrl+2`, `Ctrl+3`.
  Then zoom with `Ctrl+=`/`Ctrl+-` through the whole preset range in both directions.
- **Expect** Both extremes render without a hang, an out-of-memory, or a black page. At 8 % a
  continuous document shows many pages and is still legible as *layout*. `maxRenderResolution` clamps
  6400 % rather than allocating for it. Fit Page / Actual Size / Fit Width / Fit Visible each do what
  their name says, and **Fit Visible ignores white margins** ([`Features.md` §1.2](./Features.md#12-page-display)).
- **If it fails** S2 if it hangs or exhausts memory; S3 for a wrong fit.

**M-07 · Open every corpus file, malformed included** ☐
- **Goal** The exit criterion, walked by hand. A-21 proves the parser reports; this proves the
  **product** does something sensible with the report.
- **Steps** Open every file in `corpus/`, one at a time, including all of `corpus/malformed/`. For each:
  read the message bar, page to the end, and close.
- **Expect** Every file either opens, or is refused **with a reason naming the file and the problem**.
  A repaired file shows a message bar saying what was reconstructed. **No crash, no hang, no silent
  empty window.** A file that opens with visibly wrong content is worse than one that refuses — report
  it as such.
- **If it fails** S1 for a crash or a silent wrong render; S2 for an unhelpful message.

**M-08 · An encrypted document** ☐
- **Steps** Open `corpus/encrypted/`. Enter the wrong password; then the right one. Cancel the prompt.
  Open it a second time in the same session.
- **Expect** A password prompt appears; a wrong password says so and asks again; cancelling leaves no
  half-open tab; the right password opens the document and it renders. The second open **asks again** —
  Stage 2 caches no credential, and silently remembering one would be a security decision this stage has
  not made.
- **If it fails** S2.

**M-09 · Dr. Memory / Application Verifier over the raster path** ☐
- **Goal** The coverage ASan structurally cannot provide, because PDFium is in the path and the path
  cannot leave Windows (risk **R7**, `Tech-Stack-Plan.md` §10). **This case IS half of O10's answer** —
  what it finds, and what it costs to run, is the evidence for whether the portable route's gap matters.
- **Pre** Dr. Memory or Application Verifier installed. Not on the CI runner, which is why this is here.
- **Steps** Run `rawpage.exe` under the checker; open ten corpus files including two malformed and the
  encrypted one; scroll each; zoom; close each tab; exit cleanly.
- **Expect** No uninitialized read, no leak growing with each document, no handle leak, no invalid free
  across the PDFium boundary.
- **If it fails** S1 for anything at the boundary — that is exactly what D2-9's buffers-and-callbacks-only
  rule exists to prevent, and a finding there means the rule is being violated somewhere the gate cannot see.

**M-10 · The Viewer keyboard, by hand** ☐
- **Goal** The one thing no synthetic key event can establish: whether a chord is intercepted by the
  window manager, the IME or the platform **before Qt ever sees it**. Stage 1's M-12 was the same case
  for the Global table and it is the reason that table is trustworthy.
- **Steps**
  1. With focus in the page, press every chord `Implementation-Plan.md` § Stage 2 lists for the Viewer
     context — zoom, fit, navigation, `Ctrl+Shift+=`/`Ctrl+Shift+-`, `H`, `V`, `Z`, `Ctrl+Shift+H`,
     `Ctrl+Shift+T`, `Ctrl+Shift+N`, `Alt+←`/`Alt+→`.
  2. Press the Viewer chords that are **deliberately not implemented this stage** — `Ctrl+4`, `Ctrl+R`,
     `S`, `Ctrl+B`, `Ctrl+E`, `Ctrl+L`, `Ctrl+U`, `Ctrl+'`, `Ctrl+;`, `Ctrl+Alt+C`.
  3. Focus the tool finder's search box and press `H`, `V`, `Z` and `Ctrl+Shift+T`.
  4. Press `Alt` and navigate the ribbon by key tips; `Esc` out; check the page still has focus.
- **Expect** Step 1: every chord does what §8 says. Step 2: **nothing happens, and nothing is
  consumed** — an unimplemented command is `NotFound` and deliberately leaves the key for whoever else
  wants it. Step 3: the letters are **typed into the box**, and `Ctrl+Shift+T` does not switch layout
  mode. Step 4: key tips still work and focus returns.
- **If it fails** S2. Step 3 is the one that matters most and is the hardest to get right — it is
  context resolution working, and Stage 1's I-54 lived exactly there.

---

## 7. Known caveats — verify, do not assume broken

Behaviours that look like defects and are not. **A wrong row here is worse than a missing one**: it
tells the walker that a real finding is expected behaviour, which is precisely what Stage 1's **I-31**
was. If a row here turns out to be wrong, that is itself a finding.

| Looks broken | Is |
|---|---|
| Most ribbon buttons are still disabled | Correct. Registration goes from 23 to roughly 45 of 361 |
| `Ctrl+S`, `Ctrl+Z`, `Ctrl+Y` do nothing and **do not consume the key** | Correct — `NotFound` deliberately leaves the key unconsumed. Stage 3 |
| A repaired file opens rather than refusing | Correct, and the point of `XrefRepair`. Stage 3 forces its first save to Save-As |
| The COS reader **declines** an encrypted file while the viewer **opens** it | Correct — D2-6. Two different components with two different jobs |
| No text can be selected, and there is no text cursor | Stage 5 |
| The sidebar has no thumbnails or bookmarks pane | Stage 4 |
| `WinAcceleratedSurface` does nothing | Correct — declared, unimplemented, and the interface census (A-46) says so |
| Zoom at 6400 % is not sharper than at 1600 % | Likely correct — `pageDisplay/maxRenderResolution` clamps it. Check the setting before reporting |
| A benchmark's exit code is 0 on a slow machine | Correct. A benchmark **reports**; its exit code is about whether the measurement was possible |
| **Edge and Chrome render our corpus identically to us** | **Expected, and not evidence of anything.** Both use PDFium, the library we link — a match is two copies of one engine agreeing. Use Acrobat Reader, SumatraPDF or Firefox (P-4) |
| **Double-clicking a PDF opens RawPage rather than a viewer** | Correct — the `.pdf` association was hand-made during Stage 1's M-07 walk (its I-46). Open a reference with right-click → *Open with* |
| **Every corpus file is synthetic — there is not one real-world PDF in the tree** | Correct and deliberate (D2-11). It bounds what the golden suite proves; see §1, *What this corpus cannot tell us*. **Not a gap to be filed** — the trigger for revisiting is the first real-world rendering bug the corpus cannot reproduce |
| **`portable-fuzz` has never run before this stage** | Correct — its preset description has said "NO TARGETS EXIST until Stage 2" since Stage 0. Its **first** green run is a finding worth recording, not a formality (A-27) |

---

## 8. Issue log

Numbered `I-nn`, **this stage's own namespace** — a reference to "I-34's lesson" or "I-36" in this
document means **Stage 1's**, and says so where it appears.

| # | Sev | Title | State |
|---|---|---|---|
| **I-01** | S3 | **`Architecture-FileMap.md` disagreed with `Implementation-Plan.md` about which stage owns the `pdf/cos/` reader, and about three `core/doc/` vocabulary types.** All six COS reader rows (fifteen files) were tagged ○ (S3) while Stage 2's component list claims them and Stage 3's does not; `Revision.h`, `Scope` and `Page` were tagged ○ (S3) while their **own** *Interacts with* column names Stage 2 consumers (`RenderCache`, `TileGrid`). Found while writing this guide's §4 manifest, by checking each path against the map rather than by running anything — which is the method Stage 1's I-31 and I-32 also came from. `Implementation-Plan.md` owns stage ordering, so the FileMap is the document that was wrong. **Confirmed with the user 2026-08-31** and corrected in the same change as this guide, along with `PageTree`'s split tag `○ (S2 read, S3 edit)` following `Outline`'s existing precedent. **A third instance surfaced while verifying this guide's §4 manifest against the map**, and it is the same root cause once more: `tests/fuzz/` was tagged ○ (S3) **and listed among Stage 3's components**, though a fuzz target over `pdf/cos/Lexer` and `Parser` cannot exist before those files do — and `CMakePresets.json`'s `portable-fuzz` description has said *“NO TARGETS EXIST until Stage 2”* since Stage 0. **The build system was right and two documents were wrong.** Moved to Stage 2 T5/T6; cases **A-25 … A-27** are written against it. The related filter-count disagreement — ten in the FileMap, nine in the plan — is resolved by **D2-4**: `CryptFilter` goes to Stage 10 with the security handlers it needs a key from. **The lesson is the one Stage 1's I-31 and I-32 already taught: a document set this size drifts in the places nobody executes**, and the only thing that finds it is checking each claim against its owner rather than reading for sense. | ✅ **Closed 2026-08-31** |
| **I-02** | S2 | **`license-audit` inventories the DECLARED manifest, not the RESOLVED closure — so four libraries Stage 2 links are invisible to the licence gate.** Found 2026-08-31 by running precondition **P-3**'s dry-run, which was in the plan only because writing it raised the question of whether two features declared at Stage 0 and never built still resolve. They do — `codecs`, `text` and both together all exit 0 on `x64-mingw-dynamic`, against Qt's own pinned `x86_64-w64-mingw32-g++`. **The resolution was never the risk; the closure was.** `freetype` pulls `brotli` and `bzip2`, `tiff` pulls `liblzma`, and — **measured with `vcpkg depend-info` on 2026-08-31, after this entry first mis-attributed it to `icu`** — **`hunspell` pulls `libiconv`**, and is its only parent. `icu` depends on neither — and none of the four appears in `vcpkg.json`, so `Inventory.cpp`'s `addPort` is never called for them: it iterates `root["dependencies"]`, `root["features"][*]["dependencies"]` and the non-vcpkg `overrides` map, and that is the whole of its input. It *does* read each port's SBOM, but only for ports it already knows to ask about — which is precisely backwards for a closure. **Nothing found is GPL**, so there is no breach today: `brotli` is MIT (already allowlisted), `bzip2` is `bzip2-1.0.6` and `liblzma` is 0BSD — both permissive, neither on `Tech-Stack-Plan.md` §8.2's allowlist — and **`libiconv` is GNU libiconv, LGPL-2.1-or-later**, which D4 permits but which carries an election and a **source-offer obligation** of the same shape as Qt's and Hunspell's, and nobody has recorded one. S2 rather than S3 because the gate's documented claim is broader than its behaviour and three later stages build on that claim: Stage 10 adds OpenSSL, Stage 16 adds tesseract and leptonica, and Stage 15's release compliance gate *is* this tool. **The allowlist rows are the cheap half; the inventory change is the real one.** Owned by **T7** (the map entries arriving with `codecs`) and **T16** (`text`, and `libiconv`'s source offer), with **A-75** as the case. Whether `libiconv` reaches the shipped DLL set is confirmed at T16 with `objdump -p` — the dry-run proves it enters the tree, which is not quite the same claim. **Licences verified 2026-08-31**, against each port's manifest, its `portfile.cmake` copyright block, and upstream licence text — not from memory. **`brotli` 1.2.0 = MIT** (upstream `LICENSE`, standard MIT text; already on the allowlist). **`bzip2` 1.0.8 = `bzip2-1.0.6`** (BSD-style permissive, no copyleft, no source-disclosure obligation; retain the notice, no endorsement). **`liblzma` 5.8.3 = `0BSD`** for the library itself — XZ Utils' own `COPYING` at v5.8.3 says *“liblzma is under the BSD Zero Clause License”*, while the `xzgrep`/`xzdiff` **scripts** are GPLv2+ and parts of the build system are GPLv2+/GPLv3+; the port ships the library, so none of that is linked. Both identifiers are **absent from `Tech-Stack-Plan.md` §8.2's allowlist** and must be added. ⚠️ **`libiconv` 1.19 is the one that is not simply a missing row — and `depend-info` moved it out of Stage 2's reach.** Its **only** parent is `hunspell`, the spell checker, which nothing before **Stage 13** links; `vcpkg.json`'s `text` feature is a group spanning *“Stages 2, 5, 13”* by its own description, so T16 turning it on **builds** hunspell, icu and libiconv while Stage 2 links only freetype. **Built is not linked**, so Stage 2 most likely incurs no LGPL obligation at all here — which T16's `objdump -p` settles either way, and is exactly why that check is in the task rather than assumed. The licence facts below stand regardless, and become live at Stage 13. Its vcpkg manifest declares `license: null` — vcpkg's marker for *not expressible as one SPDX expression* — and the portfile says why, verbatim: *“The libiconv and libcharset libraries and their header files are under **LGPL**, see COPYING.LIB… The **iconv program** and the documentation are under **GPL**, see COPYING.”* So the package is **split**: the part we link is **LGPL-2.1-or-later**, which D4 permits and which needs the Qt/Hunspell treatment — an election, dynamic linking, a user-replaceable binary and a **corresponding-source offer**, none of which exists. The part we do **not** link is **GPL**, and the portfile's `vcpkg_make_install()` plus `vcpkg_copy_tool_dependencies(…/tools/libiconv/bin)` means **a GPL `iconv.exe` is installed into the vcpkg tree**. **That is not a breach and Stage 2 does not package anything** — it is a build-tree artifact, and D4 forbids a GPL *dependency*, not a GPL binary sitting in a toolchain directory. **It becomes one the moment anything sweeps `installed/<triplet>/tools/` into an installer, which is Stage 15's MSI/MSIX work.** Recorded here rather than deferred silently, because the packaging stage will not rediscover it. | 🟡 **Open** — raised 2026-08-31, licences verified the same day, owned by T7/T16 (the map entries and the inventory fix) with the GPL-tool exclusion flagged forward to **Stage 15** |
| **I-03** | S3 | **Three signatures this document set had written for Stage 2 name `QImage`, and none of them could have been built as written.** `Implementation-Plan.md` § Stage 2's component list gives `PdfiumRasterizer` `Result<QImage> raster(const RenderRequest&)`, `RenderCache` `std::optional<QImage> lookup(...)` and `RenderScheduler` `Signal<RenderRequest, QImage> ready` — but `IPageRasterizer` is declared in `core/backend.h`, `RenderCache` and `RenderScheduler` are `core/render/`, and **`core/` may not name a QtGui type**: it links `Qt6::Core` only, `layer-lint`'s `core` layer has listed `QImage` in `denyExternal` since Stage 0, and CLAUDE.md convention 3 is the standing rule (it is why `core/cmd/Chord` is first-party rather than a `QKeySequence`). Found at **T1**, by writing the interface the list describes. **Not a conflict anyone had to adjudicate**: plan **D2-9** already says the tile pipeline carries *buffers and callbacks only*, so the component sketch was the loose statement of a settled rule rather than a rival decision. Fixed in `core/backend.h` with `core::RasterImage` — width, height, **stride**, pixel format and bytes, with the stride carried rather than computed because a rasterizer may align its rows and a consumer assuming `width * 4` on a padded buffer draws diagonal garbage. `ui/` wraps those bytes in a `QImage` at the far side, where QtGui is allowed, and the wrap is free: both formats are the little-endian BGRA layout `QImage::Format_ARGB32` and `Format_RGB32` consume. `Implementation-Plan.md`'s three rows were corrected in the same change. **T10 and T11 inherit the correction**, and **A-44 is unaffected** — the raster suite links `rawpage_pdfium` and may hold a `QImage` of its own. S3 because it was a documentation slip that the gate would have caught the moment anyone typed it. | ✅ **Closed 2026-09-01** at T1, in the header that declares the interface | 

| **I-04** | S3 | **Nothing compares a catalog key a source REFERENCES against the catalogs, and eighteen referenced keys have no entry.** Found at T2 the moment the engine layer named its first key: `error.engine.initFailed` was reported as an `orphan-key` — a key nothing references — while being referenced by the file that had just been written, because `pdf/` was not in `i18n-lint`'s `scannedPaths`. Neither was `platform/`, which names **eleven** keys. **Fixing the paths was the easy half and it is done** (`pdf/` and `platform/` added; the gate went from 79 files to 90 and the false note is gone). **The real half is that no rule asks the question at all**: `missing-key` compares the shipped catalogs *against each other*, `action-label` covers `action.*` from `ActionId.def`, and `orphan-key` is the INVERSE — a catalog key nothing references — so a key a source names and no catalog defines is invisible from both directions. Measured 2026-09-01 by diffing every quoted `error.*`/`shell.*`/`ribbon.*`/`prefs.*` literal in `core/`, `ui/`, `app/`, `pdf/` and `platform/` against `rawpage_en.ts`: **eighteen** are missing — `error.action.{unknown,unavailable,alreadyRegistered,noHandler}` and `error.shortcut.{unknownAction,invalidChord,widgetNative}` from **`core/`, which has been scanned since T6**, plus `error.shell.*` (3) and `error.singleInstance.*` (8) from `platform/`. **Latent today rather than visible**: only `error.open.*` and `error.customization.*` currently reach a surface, and both exist — an unresolved key would render as its own key text, which is what makes this S3 now and **S2 the moment `MessageBar` starts showing engine errors**. Owned by **T14**, which builds that surface, with **A-14** as its case; the fix is an `unknown-key` rule keyed on the declared catalog families (so `settings.ini` and `pdfium.dll` are not mistaken for keys) plus the eighteen rows. **Not fixed at T2 on purpose**: writing eighteen error messages in two locales is a design act that belongs with the surface that shows them, and T2 is the engine boundary. | 🟡 **Open** — the scanned-path half fixed 2026-09-01; the rule and the eighteen keys owned by **T14** |
| **I-05** | S2 | **PDFium cannot be called from two threads at once, and per-document confinement does not help — so D2-9's “thread-confined per worker” is false and D10's “worker pool” cannot be eight PDFium threads.** Found at **T3**, by the A2 probe, on its first run — which is the entire argument for measuring early rather than after T13. Eight workers, **each with its own `FPDF_DOCUMENT` and its own `FPDF_PAGE`**, crash. Measured by walking the worker count rather than asserted: **1 worker exits 0; 2, 4 and 8 all die with `STATUS_HEAP_CORRUPTION` (0xC0000374)** inside the render call, and before the pages were preloaded they died inside `FPDF_LoadPage` (stack captured under `gdb`). What PDFium shares between threads is **process-global** — its font mapper and glyph caches — not per-document, so confining a document to a worker confines the wrong thing. Reproducible on demand: `rawpage_bench_render.exe 8 --unsafe-parallel`, because a finding that needs a source edit to reproduce is one that gets re-litigated. **A2 IS UNAFFECTED AND IS `go`**: the constraint is a throughput ceiling, not a latency one — per-tile p95 is 0.532 ms against a 16.667 ms budget, and eight workers behind one engine lock retire **1.05×** the tiles per second of one worker. The engine is fast; it is simply not parallel. **What D10 actually promises is untouched** — *the UI thread never parses or rasterizes* — and two arrangements keep it: **one render thread with a queue**, or **N workers serialized on one engine lock**. Neither is chosen here, deliberately: that is `RenderScheduler`'s shape and it belongs to **T11**, with **A-45** as its case and **D2-9**'s wording to be corrected in the same change. Also flagged forward: **T16**'s font work and **Stage 5**'s text extraction inherit the same constraint, since both call the engine from the same global state. | 🟡 **Open** — raised 2026-09-01 at T3, owned by **T11**, which must choose the arrangement and correct D2-9 |

**Two S2 open — I-02 (T7/T16) and I-05 (T11) — plus one S3 open, I-04 (T14). Two S3 closed.**

**I-05 is the one to read before touching T10 or T11.** It is not a defect in code anybody wrote: it is a measurement that contradicts a decision two documents had already made, found by the probe D2-1 put at T3 precisely so that it would be found before `RenderScheduler` was built on top of it. Stage 1's lesson was that a spike's figure says nothing about the product; this stage's first is that a spike can also say something the product's *design* had assumed away.

---

## 9. Sign-off criteria

Stage 2 is done when **all** of these hold. This is
[`Stage2-Rendering-Plan.md` §13](./Stage2-Rendering-Plan.md) restated as a checklist.

1. ☐ A-01 … A-74 **run**, with actual output recorded against each case — not a summary claiming they
   were. Stage 1's **I-32** was exactly this gap.
2. ☐ M-01 … M-10 **walked and signed off by you.** I do not mark a manual case.
3. ☐ `checks` clean — three gates, with the narrowed `FPDF_` rule **and its control**.
4. ☐ `ctest` green across all five Windows targets, every prior stage included.
5. ☐ `portable-asan` clean over the whole corpus parse; `portable-fuzz` green for 10 minutes on **two**
   targets that demonstrably exist.
6. ☐ Golden baselines committed; `tests/roundtrip/` **not applicable** and recorded as such.
7. ☐ **The corpus limitation is written into the sign-off, not merely known.** `Implementation-Plan.md`'s
   Stage 2 status block states in those words that the golden suite's green is **consistency against our
   own generated output**, not **fidelity to real-world PDFs** (plan §13.6). An unqualified *"every
   corpus file renders correctly"* overstates the evidence.
8. 🟡 **A2 has a recorded verdict with numbers** — the probe's and the product's — in
   `Tech-Stack-Plan.md` §3.3, with risk **R5** struck through in the same change. **Half done
   2026-09-01:** the probe's figure is recorded (`go` — p95 **0.532 ms** against 16.667 ms, 8 workers
   behind an engine lock; 1,849 tiles/s against one worker's 1,769) and R5 is struck through. The
   product's figure is T18's and is still owed. Recorded with it: the probe's own limits — one page,
   one non-embedded font, no images or transparency — and **I-05**.
9. ☐ **O7 and O10 resolved** in `Implementation-Plan.md` §8.
10. ☐ No open S1 or S2 in §8. **I-02 (T7/T16) and I-05 (T11) are open now** and both must be closed before sign-off; **I-04** is S3 and is owned by T14.
11. ☐ `Architecture-FileMap.md` rows flipped and the plan's §10 debt paid;
    `Implementation-Plan.md` Stage 2 status block updated to reality, deviations included.
