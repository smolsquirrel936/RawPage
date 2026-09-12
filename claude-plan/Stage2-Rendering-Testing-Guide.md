# Stage 2 — PDF Engine Boundary & Tiled Rendering · Testing Guide

> **Authoritative verification document for Stage 2.** Written from
> [`Stage2-Rendering-Plan.md`](./Stage2-Rendering-Plan.md) and
> [`Implementation-Plan.md` § Stage 2](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering),
> **before any Stage 2 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** 🟡 **Written 2026-08-31; T1–T3's cases run 2026-09-01, T4's and T5's on 2026-09-02,
> T6's on 2026-09-03, T7 … T15's on 2026-09-04, T16's on 2026-09-05, and T17's and T18's on
> 2026-09-06. THE AUTOMATED HALF IS COMPLETE; the manual half is untouched and is yours.**
> **85 automated** (A-01 … A-74, plus **A-75** added 2026-08-31 — see I-02, **A-76 … A-78**
> added at T4 because the plan's *“asserted rather than assumed”* named a check the guide had no
> case for, **A-79** at T13 — see I-09 — **A-80** at T14, for a view rotation this guide was
> written before anyone noticed the render path could not carry, **A-81** at T15, the
> measurement behind I-11, and **A-82** at T17 — I-13's tripwire, because this guide was written
> before the font bridge existed and nothing in it stopped that finding from being re-made
> silently, and **A-83** on 2026-09-06 — added not by a task but by the MANUAL WALK, which found
> **I-19** in M-01's first minutes and with it the back door every case in §5.6 had been reaching
> through, and **A-84** the same day, when M-03 found **I-20** — a join that could never match and a
> DIP dpi read as a physical one, and **A-85** the same day for **I-21**, when the form page in
> M-03's own six turned out to have been rendering as an empty page since T9) and **10 manual**
> (M-01 … M-10), of
> which **A-01, A-03, A-05 … A-13, A-15 … A-23, A-28 … A-60, A-63, A-64, A-66 and
> A-75 … A-85 are green** — that range absorbed A-48 … A-51 at T17 and is written as one span
> rather than two, because a list of ranges is a second copy of the case table — **A-27 and A-46 are CLOSED** (T6 took the expected fuzz-target count from 1 to 2; T9 gave
> A-46 the second half it had been waiting for since T1), and **A-61 was 🟡 until M-02 cleared it on 2026-09-06; A-62, A-65 and A-67 remain 🟡, each with the half it
> does not claim named and owned**: a real mid-session scale change is manual case **M-02's**;
> A-62's battery throttle is WIRED as of T15 and its effect is unobservable on a desktop with no
> battery, so that case still SKIPS by name; and A-65's five toggles are three measured green, one
> corrected (`transparencyGrid` needed a render flag, which it now has) and one the engine cannot
> express at all (`lineWeight` — issue **I-11**); and **A-67's** criterion names glyph rasterization,
> which happens on PDFium's side of the boundary and is not ours to count — what IS ours, the face
> being mapped once and read once across tiles, pages and documents, is measured green (T16).
> Every other box is still ☐. **Three are Linux jobs this machine cannot
> run and none should be read as merely pending: A-04**, and **A-25** and **A-26**, whose targets
> both exist but have never been linked or run because there is no clang here. **A-24 joins them at
> T8** — the ASan corpus run is the same Linux job.
> **The Stage 2 exit criterion (A-21) is met on Windows as of T8** and is owed its ASan half.
> **A2 IS SETTLED ON BOTH OF D2-1's MEASUREMENTS — `go`.** The probe (T3, raw PDFium): p95
> **0.532 ms** of service time. The product (T18, through the shipped scheduler and cache): p95
> **14.18 ms** of queued latency against 16.667 ms — **1.18× headroom where the probe reported 31×**,
> which is not a regression but the difference between timing the engine and timing the product, and
> is the entire reason D2-1 asked for two figures. Both are in
> [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), with
> risk **R5** struck through and now closed out. **O7, O10 and D2-14 are answered at T18 by
> measurement rather than by preference** — one shared cache, the portable sanitizer route, and no
> disk tier. The
> **None of the ten manual cases has been walked**, and as of T13 **the first four are walkable** —
> there is a page on screen to look at; `rawpage.exe <a corpus file>` opens it. **T14 makes them worth
> walking**: the §8 Viewer chords work, the status bar reports real numbers, and a repaired file says
> so, and **T16 makes M-03's typography half gradeable for the first time** — a missing font now
> substitutes through our policy and the log names the face. **NINE OF THE TEN ARE WALKABLE as of
> T17, and §6's own list of what blocks them was four tasks stale until this task re-read it**: it
> said M-05 … M-10 needed *"golden baselines (T17), fonts (T16), the settings keys (T15) or
> Dr. Memory"*, and not one of those six cases is about a golden baseline — M-05 and M-06 were
> waiting on T15's layout and zoom keys, M-07 and M-08 on T9's opening and T14's message bar and
> password prompt, and M-10 on T14's chords. All of those shipped. **Only M-09 is still blocked**,
> and only because Dr. Memory is not installed here.
> The issue log holds **eighteen** entries and **none of them is an open S2** — **I-14, I-15 and
> I-16** were raised and closed at T17, all three in the harness and its plumbing rather than in the
> product, and **I-17 and I-18** on 2026-09-06, both in the BUILD rather than in the product: the
> first CI run to execute Stage 2's code found that `portable-asan` and `portable-fuzz` had never
> configured since T2, and that the Windows job had never fetched PDFium at all. **I-16 is the one to read**: a defect *inside* T4's own fix, four days old, found only
> because T17 repeated the same two-word mistake in a new directory. **I-01** (S3) — two
> `Architecture-FileMap.md` stage tags that disagreed with `Implementation-Plan.md`, **found by reading
> rather than by running**, confirmed with the user and **closed the same day**. **I-02** (S2, **open**)
> — found by *running* precondition **P-3**'s vcpkg dry-run: both features resolve, and the resolved
> closure pulls in four ports the licence gate structurally cannot see. **Closed at T16**: the
> inventory change was T7's, and T16 measured with `objdump -p` that Stage 2 links none of the
> `text` ports at all, then split that feature into three so the GPL `iconv.exe` enters the tree at
> Stage 13 rather than here.
> **I-03** (S3, closed at T1) — three `QImage` signatures in `Implementation-Plan.md`'s Stage 2
> component list that `core/` cannot name, found by writing the interface they describe.
> **I-04** (S3, **closed at T16**) — no rule compared a key a source *references* against the
> catalogs, and eighteen referenced keys had no entry. The `unknown-key` rule found the same
> eighteen a hand-diff had named four tasks earlier, and they are now written in both locales. **I-05** (S2, **closed at T11**) — **PDFium cannot be
> called from two threads at once**, so D2-9's per-worker thread confinement was not sufficient and
> D10's worker pool could not be eight PDFium threads. Found by the T3 probe on its first run, which
> is what that probe is for, and closed on 2026-09-04 when T11 chose the replacement arrangement —
> **one render thread with a queue** — and corrected D2-9 in the same change. **I-06** (S3, open, added at T7) — a **scope finding rather than a defect**:
> `/JBIG2Decode` is declared and decodes nothing, because the only usable library is AGPL-3.0 (D4
> forbids it), the corpus has no JBIG2 file, and nothing in Stage 2 would call it — which is D2-4's
> own argument for `CryptFilter`. It refuses under its own catalog key rather than returning a blank
> image. `/CCITTFaxDecode` with `K > 0` is the smaller half: implemented, and with no fixture.
> **I-12** (S3, closed at T16) — T16's *Done when* asked for the `text` feature to be turned on and
> the measurement in the same clause said not to; the feature was SPLIT instead.
> **I-13** (**S2**, closed at T16, and the one to read) — the font bridge substituted **Segoe UI for
> Helvetica** on every page of every document, because PDFium asks its font hook about the base-14
> faces too and every file in the chain had said it would not. **Found by the control case, which
> nothing in the criterion called for.**
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
  `KeyContext` declaration the product has ever had. **Met at T14** — 23 of 361 registered ids
  became 48, and the seven chords later stages own stay `NotFound` and leave the key unconsumed,
  which a case walks rather than a comment describing.
- **A missing embedded font substitutes through OUR policy and the product can name the face**
  (T16). Not in the original list, and it is what A-66 is for.
- **A2 has a recorded verdict with numbers**, and **O7** and **O10** are resolved. **Neither is done:**
  A2's probe figure is recorded (`go`, p95 0.532 ms) and the PRODUCT's is T18's — D2-1 measures it
  twice on purpose — and both open decisions are owned by T18, which reads T11's cache
  instrumentation. As of T16 they are correctly still open in `Implementation-Plan.md` §8.

### Out of scope — do NOT file these as bugs

These are planned absences, itemized in [`Stage2-Rendering-Plan.md` §11](./Stage2-Rendering-Plan.md).
Reporting one costs a triage cycle and, worse, invites someone to build it early.

| Not a bug this stage | Arrives at |
|---|---|
| **Nothing can be changed or saved.** `Ctrl+S`, `Ctrl+Z`, `Ctrl+Y`, `Ctrl+R` (rotate pages) do nothing and consume no key | Stage 3 |
| No text selection, no copy, no search, no `Ctrl+F` — the **Select** tool arms and selects nothing, which is deliberate rather than broken (T14) | Stage 5 |
| No annotation **editing**, form **filling**, security editing, signatures, or export. **Since I-21 both annotations and form widgets DRAW**, and that is the whole of what Stage 2 claims about them: a `/AP` stream is page content and is rasterized | Stages 6–11 |
| **A form field is not interactive — clicking, typing and tabbing into one do nothing, and that is the scope line I-21 was fixed along.** The widget's appearance stream is drawn because it is page content under ISO 32000; focus, typing, validation, the calculation cascade and any change to a field's value are a different thing entirely and none of them is wired. **The form-fill environment exists but answers no callback**: `FPDF_FORMFILLINFO` is version 2 with every entry null, because a read-only rasterizer has no dialog to pop, no timer to set and no focus to move. Reported once during the M-03 walk, which is why it is now itemized here rather than left to be inferred from the row above | Stage 9 |
| No split views, rulers, grid, guides, full screen, presentation, or session restore. `Ctrl+U`, `Ctrl+'`, `Ctrl+;`, `Ctrl+L` are unregistered | Stage 4 |
| No thumbnails, bookmarks or destinations pane — the sidebar still holds nothing document-shaped | Stage 4 |
| No Reflow view (`Ctrl+4`) | Stage 13 |
| **An encrypted file opens through PDFium and the COS reader declines it by name** (D2-6). That decline is correct behaviour, not a parse failure | Stage 10 |
| **No accelerated presentation.** `WinAcceleratedSurface` is declared and does nothing; tiles paint through Qt. **T16 re-tagged its FileMap row ○ (S2) → ○ (S4)** rather than building it, on this row's own terms: A2's probe is p95 0.532 ms against a 16.667 ms budget, so nothing has yet said it is needed | Stage 4, if A2 says it is needed |
| Twenty-one of 26 Preferences groups are still absent; five ship after T15 | Stage 14 |
| `general/touchMode` still reads back and does nothing (Stage 1's I-38) | Stage 14 |
| **No icons anywhere.** Still true, still deliberate | when there are assets |
| **A substituted font is chosen and logged, and no surface says so.** Since T16 the substitution is ours and `FontResolverBridge` records the face; what does not exist is any way for a person to see it — no message-bar row, no font-substitution dialog. The page looks right or it does not, and only the log says why | Stage 14, with the rest of the document-properties surface |
| Most of the 361 declared commands are still disabled and say so — and since T14 they say **which kind** of disabled (Stage 1's I-53). Registration went from 23 to **48** at T14; *roughly 45* was this row's estimate before the count existed | per feature stage |

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

**T4 added three narrower limits, each stated where it is created rather than left to be inferred**
(`corpus/PROVENANCE.md` carries the same table, beside the files):

| Limit | Consequence |
|---|---|
| The JPEG 2000 stream carries **one coding pass** — `corpus/jpxenc.py` implements the MQ coder and the EBCOT *cleanup* pass, not the significance-propagation or magnitude-refinement passes | `ordinary/image-jpx.pdf` decodes to two tones: a 0/255 source comes back as roughly 0/128. A **truncated**, not a malformed, codestream. It proves the `/JPXDecode` path is reached and decoded; it proves nothing about JPEG 2000 fidelity |
| The JPEG streams are **baseline 4:4:4 only** — no progressive, no chroma subsampling, no arithmetic coding, no restart markers | Each of those is a different decode path, and none of them is in the corpus. A `/DCTDecode` bug in one of them is outside this stage's evidence |
| The embedded fonts are **sixteen rectilinear glyphs** — nine hanzi and seven capitals, all rectangles | They are real characters correctly drawn, and they exercise `/FontFile2`, CIDFontType2, Identity-H and `/W`. They say nothing about curves, hinting, kerning or composite glyphs |

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
.\build\x64-release\bin\rawpage.exe --print-screen-metrics # NEW 2026-09-06 -- both sides of the
                                                            #   QScreen-to-MonitorInfo join, and
                                                            #   which source won. THE ONLY WAY TO
                                                            #   SEE I-20
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
| `performance/renderWorkers` | **1** — range 1–16 (`core/settings/SettingsKeys.def`) | A-45, A-68, M-04 |
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
| *(plan T4)* Every file has a provenance row, and regenerates byte-identically | **A-76, A-77**, with **A-78** as the control |
| No ASan finding on the portable COS run | A-24 |
| No Dr. Memory finding on the raster run | **M-09** |
| Golden baselines committed | **A-48, A-49, A-50 — all ✅ 2026-09-06 (T17)**, plus **A-51**'s runtime half and **A-82**. 198 images over 56 corpus files at three resolutions; the tool refuses to regenerate without a stated reason; a perturbed render fails with an expected/actual/diff triple |
| Scrolling a large document stays responsive | A-36, A-37, A-68, **M-04** |
| Mixed-DPI dragging is correct | A-28, A-29, A-60, A-61, **M-01**, **M-02** |
| *(plan T13)* **A page is on screen** | A-52, A-53, **A-79**, **M-03** — and the three are complementary rather than redundant: A-52 asserts the geometry the layout gave the widget (without which A-53 is green and worthless), A-53 asserts real pixels through the real viewer over a FAKE rasterizer (`ui/` may not link an engine — D1-4), A-79 asserts real pixels from the REAL engine through the real scheduler with no widget, and **only M-03 sees the two halves meet on glass** |
| *(plan T14)* **Every §8 Viewer chord dispatches** | A-55's second half, **A-80** for the rotation, and `ui.every Stage 2 Viewer chord dispatches from the viewer`, which walks all 23 — with its companion asserting the other seven are still `NotImplemented` |
| *(plan T15)* The sixteen settings keys exist, round-trip, and change the render | **A-63**, **A-64**, **A-65** — and A-65 is 🟡 by CORRECTION rather than by failure: see **I-11** |
| *(plan T14, D2-10)* A disabled registered command says **why** | **A-59**, four cases; and **M-04** for a person actually reading it, which no offscreen run can do |
| *(plan T13, D2-8)* `KeyContext::Viewer` resolves, and only in the viewer | A-55, **A-56** — the negative is the one that tests context resolution; a chord that fired everywhere would pass A-55 |
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
**Result at T9 (2026-09-04), which is this case's CURRENT figure** — the T1 line above is kept as the
first measurement rather than overwritten, because the point of re-running at every task is the
progression: `ctest` **526/526** on both presets, the bare `gates` preset **90/90**, and no warning
in `core/`, `pdf/` or `platform/`. The per-task history is in `Implementation-Plan.md`'s Stage 2
block, which owns it; this case owns only the pass/fail.
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
added to its scanned paths, which is half of **I-04**. *(Recorded 2026-09-02, on re-measurement: T2
also took the catalogs from 538 keys to **539**, adding `error.engine.initFailed` with `PdfiumInit`,
and no running figure moved with it — see the T5 result below.)*

**Result at T4** Exit 0, three gates, all figures unchanged: T4 added **data and Python**, and
neither gate reads either.

**Result at T5** Exit 0, three gates. `layer-lint` clean at **153 files / 1,047 includes**, still
**six** filesystem-write waivers; `license-audit` clean at 17 components and 4 Qt modules with its two
standing not-yet-built warnings; `i18n-lint` clean at **92 files / 2 catalogs / 0 waivers**, the
catalogs at **539 keys** each (12 `error.*`), 29 of `zh_TW`'s marked unfinished. `pdf/cos/Lexer`
adds no catalog key at all, which is correct rather than an omission: it holds no user-facing text —
`core/Result.h` carries the key when a failure has to reach a person, and a tokenizer that never
fails has no such failure to report. **If it fails** S1.

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

**A-76 · The whole corpus regenerates byte-identically** ✅ **2026-09-02 (T4)**
☐→✅ **Goal** D2-11's load-bearing property, and Stage 2 T4's exit criterion in its own words:
*every file regenerates byte-identically from a clean checkout — that reproducibility is the property
the whole decision rests on, and it is **asserted rather than assumed**.*
**Steps** `ctest --preset gates -R gates.corpus.reproducible`, or by hand
`python corpus/make-corpus.py --verify`.
**Expect** Exit 0 and *“N files regenerate byte-identically”*, where N is the count in `PROVENANCE.md` — 60 at T4, **61 from T16**. The check regenerates every file in
memory and compares it against the tree, **and** flags an ORPHAN — a `.pdf` on disk that no generator
claims — because a corpus that can accumulate files from nowhere is one where D2-11's *“every file is
ours”* has stopped being checkable. It also builds twice in one process, which catches a generator
seeded per-run that a disk comparison alone would only catch on a machine that had already written
the first answer.
**Result 2026-09-02** Exit 0, 60 files. **Re-run 2026-09-05 (T16): exit 0, 61 files** — the thirtieth `ordinary/` file, `font-not-embedded.pdf`, added for A-66, with the other 60 regenerating byte-identically in the same run. **If it fails** S2 — and read the failure before assuming the
corpus is at fault: the three likeliest causes are a non-deterministic generator (a clock, a `set`, a
`zlib` that differs between this box and the Ubuntu runner), a corpus file **edited by hand** (a PDF
xref stores absolute byte offsets, so one character invalidates every offset below it), and
`.gitattributes` failing to keep `corpus/**` binary.

**A-77 · Every corpus file has a `PROVENANCE.md` row, and every row has a file** ✅ **2026-09-02 (T4)**
☐→✅ **Goal** The other half of D2-11: *“`corpus/PROVENANCE.md` still gives every file a row — which
generator produced it, what it is for, and what is deliberately wrong with it.”* This is the only
mechanism in the project that keeps *“nothing is borrowed”* checkable rather than merely stated —
**`license-audit` governs linked dependencies and says nothing about data**, which is the gap D2-11
names.
**Steps** `ctest --preset gates -R gates.corpus.provenance`, or
`python corpus/make-corpus.py --check-provenance`.
**Expect** Exit 0 and *“60 PROVENANCE.md rows, one per file”*. An UNLISTED file, a STALE row naming a
file nothing generates, and a duplicated row are each reported by name.
**Result 2026-09-02** Exit 0, 60 rows. **If it fails** S3 unless the missing row is for a
`malformed/` file, in which case **S2** — an undescribed damaged fixture is exactly the *“this file is
broken somehow”* mystery D2-11 exists to prevent.

**A-78 · The corpus checks can actually fail — the negative control** ✅ **2026-09-02 (T4)**
☐→✅ **Goal** The negative A-76 and A-77 cannot supply for themselves: **a checker that returns
success without looking passes both of them.** This is Stage 1's I-30 in its general form, and
CLAUDE.md convention 7 — *every negative control must actually exercise the thing*.
**Steps** `ctest --preset gates -R gates.corpus.self_test`, or
`python corpus/make-corpus.py --self-test`.
**Expect** Exit 0 and *“5 of 5 controls fired”*, with each detection named: an edited file reported
as DIFFERS, a deleted one as MISSING, an unclaimed one as ORPHAN, an undocumented one as UNLISTED,
and a row for no file as STALE. The stand-in tree and row list are built **in memory**, so an
interrupted run cannot leave the real corpus damaged.
**If it fails** S2 — a control that stops firing turns A-76 and A-77 into decoration, and neither of
them would go red to tell you.

**A-75 · `license-audit` sees the RESOLVED closure, not just the declared manifest**
✅ **T7's half 2026-09-04; T16's half 2026-09-05** **Goal** I-02. Added 2026-08-31, after the guide was written, when P-3's dry-run showed the
`codecs` and `text` features pull in four ports `vcpkg.json` never names.
**Steps** With both features active, list the resolved closure
(`vcpkg install --dry-run --x-feature=codecs --x-feature=fonts --x-feature=shaping --x-feature=spelling --triplet x64-mingw-dynamic` — `text` became those three at T16), then run
`license-audit.exe --root . --explain` and compare the two lists.
**Expect** Every port in the closure has a licence verdict. Today **four do not appear at all** —
`brotli` (MIT), `bzip2` (`bzip2-1.0.6`), `liblzma` (0BSD) and `libiconv` (**LGPL-2.1-or-later**) —
because `Inventory.cpp` builds its list from `vcpkg.json`'s `dependencies` and `features` arrays plus
the non-vcpkg overrides, and never enumerates what vcpkg actually installs.
**Result 2026-09-04 (T7)** **The inventory change is done, and it is the half I-02 called "the real
one".** `Inventory.cpp` now enumerates `share/*/vcpkg.spdx.json` in the installed tree and adds every
port the manifest never named, marked `(transitive)`. An installed tree **is** the closure — vcpkg
writes an SBOM for every package it actually installed — so the fix reads what we ship rather than
maintaining a second hand-kept list of what depends on what, which would be one more thing to drift.
Only directories carrying an SBOM count, which filters vcpkg's alias and documentation directories
(`jpeg`, `png`, `man`, `doc`) that would otherwise appear as four licence-less components.
**Measured:** `checks` went from **17 components to 20** — `liblzma` plus the two `vcpkg-cmake` host
ports, none of which any previous run had a verdict on. `liblzma` is the one that needed a decision:
vcpkg concludes `LicenseRef-vcpkg-null` for it, so the **map** supplies `0BSD`, verified against XZ
Utils 5.8.3's own `COPYING`. `0BSD` joined the allowlist and `Tech-Stack-Plan.md` §8.2 in the same
change, as that list's own comment requires.
**With NO installed tree the loop does nothing, and that is correct rather than a gap**: the CI
`gates` job has no vcpkg (D0-1) and still fails on a bad licence, from the map — which is why the map
carries a row for each known transitive port.
**Two new gate cases with a real control:** `gates.license_audit.undeclared_transitive_gpl` puts a
GPL-3.0-only port in a fixture tree that the manifest never names and requires exit 1;
`…_permissive` does the same with an MIT port and requires exit 0 **and a component count of 1**,
because a control that only checked exit 0 would pass against a tool that failed on any installed
tree at all. Before the change that count was 0.
**Result 2026-09-05 (T16), and the answer is a measurement rather than a source offer.** T16's own
*Done when* clause names `objdump -p` on the built executable as what decides whether these ports
reach us, and it says they do not: `rawpage.exe` imports **eighteen DLLs** — Qt's three, ten Windows
libraries (`DWrite.dll` is T16's addition and is an OS library), the two GCC runtimes, `pdfium.dll`,
and `libz` / `libjpeg-62` / `libopenjp2` from `codecs` — and **not one of them is freetype, brotli,
bzip2, icu, harfbuzz, hunspell or libiconv.** Nothing in Stage 2 links any of them, because PDFium
bundles its own FreeType and T16 reaches the OS through DirectWrite.
So **libiconv's LGPL source-offer obligation is Stage 13's**, and T16 made that structural rather
than remembered: `vcpkg.json`'s `text` feature is now **three** features — `fonts`, `shaping`,
`spelling` — and `fonts` + `shaping` together pull **neither hunspell nor libiconv**, measured with
a dry-run rather than reasoned from the dependency graph. The GPL `iconv.exe` the port installs now
enters the tree at Stage 13 and not before, which is eleven stages later than `text` would have put
it and is the whole point of the split. **Stage 2 turns on none of the three** — issue **I-12**.
**What is NOT closed by this and must not be read as if it were:** the three ports still have no
licence verdict in `license-audit`, because they are not in this configuration's installed tree.
That is the tool working as designed — it audits the closure it is given — and the day Stage 5 turns
`fonts` on, `brotli` and `bzip2` appear and want rows. `spdx-map.json` carries them already, so the
first run after that will be a pass rather than a discovery.
**If it fails** S2. **Not because anything here is GPL** — none of the four is — but because
[`CLAUDE.md`](../CLAUDE.md) and `Tech-Stack-Plan.md` §8.2 both describe this gate as *what
mechanically enforces the no-GPL rule*, and a gate that audits what we **wrote down** rather than what
we **ship** cannot make that claim. Stage 10 (OpenSSL) and Stage 16 (tesseract, leptonica) add much
deeper closures on top of it, and Stage 15's release compliance gate is built directly on this one.

---

### 5.3 The COS reader

**A-15 · `tst_cos_lexer`** ✅ **2026-09-02 (T5)** **Steps** `ctest -R "unit.cos lexer"`
**Expect** Delimiters, literal and hex strings, numbers, names, comments; tolerant of the malformed
inputs the corpus generator produces on purpose.
**Result 2026-09-02** **24 cases green** on both Windows presets, plus
`unit.cos.compile.fuzz_entrypoint`. The suite is in three parts and the third is the one that
matters most: the ordinary grammar; the **tolerances**, each a specified behaviour with a corpus
file or a named real-world producer behind it (`--5` reads as −5, `1.2.3` still yields a number,
`/A#GG` keeps its `#`, `<41A>` pads to `<41A0>`, an out-of-range integer **clamps rather than
wraps**); and the **contract the fuzzer depends on** — progress, bounds, `End` stability,
`peek`/`next` agreement — replayed over all **60** corpus files, **38** of which produce at least
one `LexNote`. **Two negative controls were run** rather than reasoned about: removing the `)`
advance produced *“no progress at 0 (token Junk)”* **and** *“no progress at 585”* from a real corpus
file, and removing the `<<` lookahead reddened two cases. **If it fails** S2.

**A-16 · `tst_cos_parser`** ✅ **2026-09-03 (T6)** **Steps** `ctest -R unit.cos.parser`
**Expect** Objects, indirect objects, streams, object streams. **Identity-preserving round-trip of
anything we do not interpret** — the property Stage 3's byte-identical save depends on.
**Result 2026-09-03** **30 cases green** on both Windows presets, plus
`unit.cos.compile.fuzz_entrypoint_parser`. The round-trip half is asserted the strong way rather
than over literals: **every object in every one of the 29 `corpus/ordinary/` files is re-parsed FROM
ITS OWN RECORDED BYTE RANGE and must compare equal** — 200-plus objects, 20-plus of them streams —
because the failure mode is a construct nobody thought to write down. `(a\053b)` versus `(a+b)` and
`4.` versus `4.0` are asserted directly beside it: they decode identically, are not the same bytes,
and both spellings come back verbatim.
**Two limits this case does NOT cover, named at the assertion** (convention 19). **Object streams
run against hand-built payloads**, not the corpus, because every `/ObjStm` in `corpus/` is
`/FlateDecode`d and Flate is **T7** — and that gap is *asserted* by its own case rather than left in
a comment, so it closes when T7 closes it. And nothing here resolves an object NUMBER to an offset;
that is `XrefTable`'s (T7), and the `/Length 9 0 R` cases use a hand-written resolver in its place.
**Negative control run:** truncating a stream's span reddened this case and the stream case, and
nothing else. **If it fails** S1.

**A-17 · A `/Length` that lies is recovered — and the control proves the path was taken**
✅ **2026-09-03 (T6)** **Goal** The single most common real-world malformation.
**Steps** A generated fixture whose stream `/Length` is 12 bytes short. Then one where it is correct.
**Expect** The first parses to the same content as the second, **and reports that it recovered**. The
control is the second: it must parse *without* the recovery path running, or the case proves nothing
about recovery.
**Result 2026-09-03** Both halves green over the pair T4 built for it —
`malformed/length-lies-short.pdf` (4497) and `ordinary/stream-length-control.pdf` (4509), three
bytes apart. The damaged file recovers to **4,509 bytes**, byte-identical to the control's stream,
reports `StreamLengthRecovered` **once** and keeps the declared 4,497 alongside it, because T8's
report has to say what it reconstructed rather than that it did. **The control reports zero.**
The rule the parser actually implements is **trust `/Length` only when it lands on `endstream`**,
and three further files establish why nothing weaker would do: `length-lies-long.pdf` is 400 bytes
too long and **still fits inside the file**, so a check of `declared > available` alone would accept
it silently; `huge-length-claim.pdf` declares 4,294,967,295 in a 10 KB file and allocates nothing,
because a stream is an offset and a length into the caller's buffer; and `truncated-mid-stream.pdf`
has no `endstream` at all, so the data runs to the end of the file under its own note.
**Negative control run:** accepting the declared length without checking for `endstream` reddened
exactly this case and its too-long twin, and nothing else. **If it fails** S2.

**A-18 · `tst_cos_filters` — each of the nine round-trips**
✅ **2026-09-04 (T7), with a correction to the case itself** **Steps** `ctest -R unit.cos.filters`
**Expect** `Flate`, `LZW`, `Ascii85`, `AsciiHex`, `RunLength`, `Dct`, `Jpx`, `Ccitt`, `Jbig2` — each
`encode`→`decode` returns the input exactly, and each `decode` of a corpus-sourced stream matches its
expected bytes.
**THE CASE AS WRITTEN ASKS FOR SOMETHING THREE OF THE NINE CANNOT DO, and that is a defect in this
guide rather than in the code.** `encode`→`decode` returning the input exactly is **not a property
JPEG or JPEG 2000 has** — both are lossy by construction — and CCITT and JBIG2 are decode-only this
stage (D2-4 makes `pdf/cos/` a READER). Written before the code, the criterion read as uniform; it
is not. Corrected here rather than quietly narrowed (`Stage2-Rendering-Plan.md` §12), and the
correction is **structural**: `isLossless(FilterKind)` lives in `pdf/cos/filters/Filter.h`, the
round-trip case iterates it rather than a list the test keeps in its own head, and a companion case
asserts that the other four **refuse to encode by name**. Adding a tenth filter cannot silently
escape either half.
**Result 2026-09-04** **13 cases green** on both Windows presets. The five lossless filters
round-trip over nine payload shapes plus 9,000 bytes of pseudo-noise chosen to push LZW past its
9-bit boundary. The corpus decodes are **exact, not structural**, wherever the generator's bitmap is
a closed-form expression — `image-lzw-ascii85.pdf`'s chained `[/ASCII85Decode /LZWDecode]` gradient,
its `/RunLengthDecode` twin, and `image-flate-predictor.pdf`'s PNG-Up photograph are all recomputed
here and compared byte for byte. **The two CCITT files are compared BOTH ways**: against each other
(the same bitmap through one-dimensional and two-dimensional coding — two independent decoder paths,
one expected answer) *and* against the reproduced bitmap, because the cross-check alone has a blind
spot that was measured rather than reasoned about — `/BlackIs1` is a polarity, and inverting it
inverts both files identically, so two decoders that agreed with each other would both have been
producing a photographic negative.
**Four negative controls were run:** breaking LZW's EarlyChange boundary reddened the corpus chain
and the round-trip; inverting `/BlackIs1` reddened the CCITT case **only after** the exact
comparison was added, which is what established the blind spot was real; truncating a stream span
and trusting `/Length` blindly reddened their own cases (T6's).
**If it fails** S2.

**A-19 · `CryptFilter` is absent, deliberately**
✅ **2026-09-04 (T7)** **Goal** D2-4. **Steps** `ls pdf/cos/filters/`.
**Expect** Nine files, no `CryptFilter`.
**Result 2026-09-04** Asserted in `tst_cos_filters` over the FILTER LIST rather than by counting
files, and the difference matters: `Filter.{h,cpp}` is the registry, the `DecodeParms` and the
predictor Flate and LZW share — it is not a tenth filter, and a file count would have to be told to
ignore it and would then ignore a real tenth filter too. The case checks all nine kinds resolve from
both their full names and their ISO 32000-1 table 6 abbreviations, that each has its own translation
unit, that `CryptFilter.{h,cpp}` do not exist, and that `/Crypt` is reported under **its own catalog
key** rather than as an unknown filter — "unknown filter" sends a reader looking for a typo, and
`/Crypt` should send them to Stage 10.
**If it fails** — that is, if it *exists* — S3 and a scope
finding: a crypt filter with no `StandardSecurityHandler` (Stage 10) has no key to decrypt with, so it
cannot have been tested and will be rewritten.

**A-20 · `tst_xreftable` walks a three-generation incremental file**
✅ **2026-09-04 (T7)** **Steps** `ctest -R unit.cos.xref`
**Expect** Classic tables and xref streams both; the `/Prev` chain resolved across three incremental
updates, with the newest generation of each object winning; the trailer read.
**Result 2026-09-04** **14 cases green** on both Windows presets. Over
`corpus/ordinary/incremental-3gen.pdf`: three sections newest-first, and the property that matters
asserted **by revision** rather than by counting sections — object 6 comes from revision 0, object 3
from revision 1, object 1 from revision 2, and every live object fetches to itself. Reversing the
rule reads a document as it was two saves ago **with no error anywhere**, which is why the case names
the numbers.
Cross-reference **streams** are covered by `objstm-xrefstream.pdf`, a file with no classic table
anywhere — and it is where **T6's deliberately open seam closes**: its `/Type /XRef` stream is
`/FlateDecode`d, so nothing could read it until T7's filters existed, and its type-2 entries are
objects that exist only inside a compressed container. The merged trailer takes each key from the
newest section carrying it, because a file whose last revision omits `/Root` must still resolve.
**Negative control run:** changing the merge from first-wins to last-wins (that is, letting the
OLDEST revision win) reddened exactly this case and nothing else.
**If it fails** S1 — Stage 3's incremental writer appends to exactly this structure.

**A-21 · Every `corpus/malformed/` file yields a report and never a crash — the exit criterion**
✅ **2026-09-04 (T8) on Windows; its ASan half is A-24 and is unrun**
**Steps** `ctest -R unit.cos.repair`, iterating the whole directory.
**Expect** For every file: a `Result` that is either success-with-a-`RepairReport` or a **named**
failure. No crash, no hang, no assertion, no unbounded allocation.
**Result 2026-09-04** **12 cases green.** All 24 malformed files go through `openWithRepair()` — the
single entry point T9 will call, deliberately the only one, so that "open a document" is one code
path whether or not the file is damaged. Every file yields a table and a report or an
`Unsupported`/`ParseFailure` carrying a catalog key; none crashes and none hangs.
The three forbidden outcomes are each asserted where they live rather than inferred from the suite
finishing: the **two hang fixtures** (`circular-prev.pdf`, `pages-loop.pdf`) are named individually
so a red run says which property broke; the **two resource fixtures**
(`huge-length-claim.pdf`'s `/Length 4294967295` in a 10 KB file, `deep-nesting.pdf`'s 4,000 levels)
are checked by asserting every recovered stream span fits inside the file. The first case in the
suite is the NEGATIVE one — every `ordinary/` file must come back **un-repaired** — because the
easiest way to satisfy this criterion by accident is to rebuild everything on open, which would also
make A-22's ratio 1.0 for every document ever opened.
**Caveat worth reading with a red run:** a hang would surface as a CTest **timeout**, not as a named
failure. There is no in-process watchdog.
**Negative control run:** forcing the repairer to skip its "the file's own structures work" fast path
reddened the un-repaired control immediately.
**If it fails** S1 — this is the criterion the malformed corpus exists for.

**A-22 · `RepairReport` says how much was reconstructed, not merely that it was**
✅ **2026-09-04 (T8)** **Goal** `Stage2-Rendering-Plan.md` §5.4 — Stage 3 forces a repaired file's
first save to Save-As, and *"reconstructed 3 objects of 4,000"* and *"reconstructed the entire xref"*
are different risks.
**Steps** Two malformed fixtures at those two extremes.
**Expect** Per-object entries with severity, and a summary a message bar can render in one line.
**Result 2026-09-04** The two extremes are `xref-missing.pdf` (nothing to start from — every object
found by scanning, `xrefRebuiltFromScratch()` true, summary key `error.repair.wholeXref`) and
`xref-offsets-short.pdf` (offsets twelve bytes short from object 4 onward, so the early entries are
fine — patched rather than rebuilt, `error.repair.objects` with **both** halves of the ratio). A
repairer that rebuilt everything would report the two identically, which is the outcome §5.4 exists
to prevent; the distinction is recorded as an explicit flag rather than inferred from the ratio,
because a two-object file rebuilt from nothing and a two-object file with two bad offsets both have
a ratio of 1.0.
**The summary is a CATALOG KEY plus arguments, never a sentence**, and that is asserted structurally
over every corpus file: `core/` holds no user-facing English and has no `tr()`, so an English summary
returned from this layer would be the single change that makes the product unshippable in `zh-TW`
while looking entirely reasonable in review. The case checks the key has no spaces, is one of the
four declared families, and carries exactly the arguments its `%1`/`%2` need.
**One finding, fixed in the same change:** `root-points-at-nothing.pdf` came back marked repaired
with an **empty report** — the trailer named a `/Root` that is not in the file, and the code asked
"is there a `/Root`?" rather than "does it resolve?". Repaired-with-nothing-to-show is the one
combination this case forbids, since it tells the user nothing.
**If it fails** S2 — Stage 3 inherits a boolean and cannot make the decision the convention requires.

**A-23 · The COS reader declines an encrypted file by name**
✅ **2026-09-04 (T8)** **Goal** D2-6. **Steps** Parse `corpus/encrypted/*.pdf` through the COS reader.
**Expect** A named `Result` error meaning *encrypted, handler arrives at Stage 10* — **not** a parse
failure, not a partial parse, not a crash.
**Result 2026-09-04** All five encrypted fixtures (revisions 2/3/4/6) are declined by
`openWithRepair()` with `error.pdf.encrypted` and — the half that matters as much as the key — the
code **`Unsupported`, not `ParseFailure`**. `Unsupported` says *this file is fine and this build
cannot read it*; `ParseFailure` would say *this file is damaged* and would send the repair path, and
a caller, down entirely the wrong road. The check sits **before** the repair path for that reason.
It is knowable early because `/Encrypt` is a trailer key and the cross-reference structure itself is
never encrypted, so `XrefTable::isEncrypted()` answers before any string is touched. **The control**
is an unencrypted file that must still open, without which the case is satisfied by a reader that
declines everything.
**Deviation from the steps as written:** they say *"through `pdf/cos/Parser`"*, and the check lives
one layer up, on the table. The parser sees objects, not trailers, so it is not where the question
can be asked — the wording predates the layering and the behaviour is the one D2-6 specifies.
**If it fails** S2: two parsers reaching different
conclusions about one protected file is a class of bug that is very hard to see.

**A-24 · `portable-asan` over the whole corpus parse — clean**
☐ **THE SUITES EXIST AS OF T8; THIS IS A LINUX JOB, like A-04, A-25 and A-26.**
**Goal** Half of O10. **Steps** On the Linux job, run the COS suites over every corpus file under
ASan+UBSan.
**Expect** No finding. **A finding here is reproduced in `x64-debug` before it counts as fixed**
(convention 11's corollary).
**State 2026-09-04** The suites this case runs — `unit.cos lexer`, `unit.cos parser`,
`unit.cos filters`, `unit.cos xref`, `unit.cos repair` — are all green on Windows over the whole
corpus. **That is not this case.** Running these files under `x64-debug` proves they terminate and
produce the right answers; it proves nothing about a read one byte past a buffer, which is exactly
what ASan exists to find and what MinGW GCC cannot provide (risk R7). **`pdf/cos/` links zlib,
libjpeg-turbo and openjpeg as of T7**, so the portable presets now pull the `codecs` feature on
Linux too — `VCPKG_MANIFEST_FEATURES` is set for `_portable` as well as `_windows`, and the first
`portable-asan` run after T7 is the first evidence that resolution works on `x64-linux`.
**If it fails** S1.

### 5.4 Fuzzing — the preset's first real targets

**A-25 · `fuzz_cos_lexer` builds and runs 10 minutes clean**
🟡 **WRITTEN AT T5. A CI JOB EXISTS AS OF T18 and the target now BUILDS AND RUNS on every
push — seeded, for 60 seconds, not for ten minutes. The build half is closed; the soak is not.**
**Goal** `portable-fuzz` has been wired since Stage 0 with its preset description saying **"NO
TARGETS EXIST until Stage 2"**. **Steps** Build on `portable-fuzz`; run seeded from `corpus/malformed/`:
`./bin/fuzz_cos_lexer corpus/malformed -max_total_time=600`.
**Expect** Builds; 10 minutes with no crash, no leak, no timeout.
**State 2026-09-02** The target **exists** (`tests/fuzz/fuzz_cos_lexer.cpp`), and this machine has no
clang and no libFuzzer, so neither the link nor the run has happened. **Two things were done instead,
and neither is a substitute:** the entry point is compiled `-fsyntax-only` by the Windows toolchain on
every run (`unit.cos.compile.fuzz_entrypoint`), so it cannot rot unnoticed between Linux runs; and its
body lives in `tests/fuzz/LexerFuzzBody.h`, which `tst_cos_lexer` calls over all 61 corpus files and a
table of adversarial inputs. **Replaying 61 known files is not fuzzing and finds nothing new by
construction** — say so rather than letting the green count imply otherwise.
**If it fails** S1 if it crashes; **S2 if it fails to build** — a preset that has never built anything
has not been proven to build anything, which is Stage 1's I-30 in a different costume.

**State 2026-09-06 (T18) — THE JOB EXISTS, AND THAT IS THE HALF THAT WAS MISSING.** From T5 to T17
this case was recorded as *"a Linux job, like A-04"* — honestly, and with a gap nobody had named:
**there was no Linux job.** `.github/workflows/ci.yml` had `gates`, `windows` and `portable-asan`,
and nothing that ran clang. A deferral whose owner is *"a job"* and whose job does not exist is a
deferral with no owner, which is I-30's shape exactly — a coverage claim resting on infrastructure
nobody had checked was there. T18 added the **`portable-fuzz`** job: it installs clang, configures
the preset (where **A-27**'s target-count assertion has teeth), builds both targets, **asserts each
binary was actually linked before running it** — because *"it did not crash"* is also what a missing
file produces if nobody looks — and runs each seeded from `corpus/malformed/`.
**The S2 half is closed: the preset builds, measurably, on every push.**
**🟡 THE TEN MINUTES IS NOT CLOSED, and the number here is 60 seconds per target on purpose.** A
ten-minute soak on every push costs twenty minutes of wall time to re-cover ground the previous push
already covered, and would make the slowest job in the pipeline the one that finds least. What the
short run establishes is that the driver starts, reads the seed corpus and executes the body — which
is what turns *"the target exists"* into *"the target runs"*. A real soak belongs on a schedule, and
that is **Stage 3's** to add along with `tests/roundtrip/`; recorded here rather than left implicit,
because the difference between a smoke test and a fuzz campaign is exactly the kind of thing a green
tick makes invisible.

**A-26 · `fuzz_cos_parser` — the primary target — same**
🟡 **WRITTEN AT T6. BUILDS AND RUNS SEEDED ON EVERY PUSH as of T18; the ten-minute soak is
still owed — see A-25, which carries the reasoning for both.**
**Steps** As A-25, against `./bin/fuzz_cos_parser`. **Expect** As A-25.
**State 2026-09-03** The target **exists** (`tests/fuzz/fuzz_cos_parser.cpp`) and this machine has
no clang, so neither the link nor the run has happened. The same two substitutes-that-are-not stand
as at T5: the entry point is compiled `-fsyntax-only` every Windows run
(`unit.cos.compile.fuzz_entrypoint_parser`), and its body lives in `tests/fuzz/ParserFuzzBody.h`,
which `tst_cos_parser` replays over all 61 corpus files and a table of adversarial inputs.
**Replaying 61 known files is not fuzzing** — say so rather than letting the green count imply
otherwise. What the body checks beyond what ASan would: **progress** (a parser that stops advancing
is a hang, the one failure a fuzzer reports badly), **the depth bound** — measured by an ITERATIVE
walk, because a recursive one would be the very bug it is looking for — and **span containment**,
which is not a crash here but becomes someone else's memory inside a saved PDF once Stage 3's
writer starts copying those bytes. **If it fails** S1.
**State 2026-09-06 (T18)** As A-25: the `portable-fuzz` CI job builds this target, asserts it was
linked, and runs it for 60 seconds seeded from `corpus/malformed/` on every push. Being the
**primary** target, it is the one whose soak matters most — and the soak is the half still owed.

**A-27 · The fuzz targets actually exist in the build graph**
✅ **closed at T6 — and moved from a manual enumeration to an assertion at T5**
**Goal** The negative that A-25 and A-26 cannot supply for themselves: a target that silently
builds nothing passes a "no crashes" check trivially.
**Steps** Enumerate the `portable-fuzz` build graph.
**Expect** Exactly two fuzz targets. **Zero is a failure, not a pass.**
**Change made at T5** Counting targets by hand once, at sign-off, is the shape of check that is
right on the day and silently wrong afterwards. `tests/fuzz/CMakeLists.txt` now **fails configure**
when its target count is not `RAWPAGE_EXPECTED_FUZZ_TARGETS`, so an empty fuzz graph stops the
`portable-fuzz` build instead of passing its run. **The number was 1 at T5 and became 2 at T6**, bumped in
the same commit as `fuzz_cos_parser` exactly as that comment said it would be. The root
`CMakeLists.txt` message that said *"NO FUZZ TARGETS EXIST YET"* since Stage 0 changed at T5.
**Closed 2026-09-03**: the graph holds the two targets this stage specifies, and the count is
asserted at configure time rather than counted by hand at sign-off — so it cannot be right on the
day and silently wrong afterwards, which is the failure mode this case was written against.
**If it fails** S2 — and this case exists because Stage 1's I-30 was precisely a job that reported
success by doing nothing.

### 5.5 Render policy — pure, and therefore all headless

**A-28 · `tst_dpipolicy` — `resolve()`** ✅ **2026-09-04 (T10)**
**Steps** `ctest -R "render dpipolicy"`
**Expect** **Actual Size is 100 % at the monitor's physical DPI** ([`Features.md` §1.2](./Features.md#12-page-display)),
not 100 % of a logical pixel. The 150 % and 100 % rig values both resolve correctly, and so do 175 %
and 350 %, which the rig cannot produce on demand.
**Result 2026-09-04** All four, plus the case the rig can never produce at all: **a 157-DPI panel that
Windows drives at 150 %**, where Actual Size must rasterize at 157 rather than 144. That is why `resolve()`
takes three inputs and not two — the OS scale factor is carried out separately as a device-pixel ratio and
**never reaches the raster DPI**. The commodity desktop has `physicalDpi == 96 * scale`, so the two are
indistinguishable there; **the control is two monitors with the same glass and different scale factors**,
which must produce identical pixels. Without it the case passes against an implementation that multiplies by
the scale as well.
**Negative control run:** letting the scale into the raster DPI reddens the control and six other lines.
**If it fails** S2 — this is the file most likely to be the root cause of a blurry-text bug.

**A-29 · `requiresRerender()` across the threshold, in both directions** ✅ **2026-09-04 (T10)**
**Steps** One step above and one step below the threshold, increasing and decreasing.
**Expect** Re-render above, resample below, **and the answer is the same going up as coming down** —
hysteresis here means a monitor drag that re-rasterizes one way and not the other.
**Result 2026-09-04** Symmetric **by construction rather than by luck**: the predicate divides the larger
DPI by the smaller, so the ratio is the same number whichever way the move went. A formulation such as
`(target - current) / current` is not symmetric, and the asymmetry it introduces is invisible in a
one-directional test. Tolerance 2 %, asserted at 1 % (resample) and 3 % (re-render) in both directions.
**A fourth clause fell out of the design and is asserted:** a change of PRESENTATION alone — same glass
DPI, different OS scale factor — is **not** a re-render, because the pixels are identical and only the
ratio they are painted at differs. Without that case the separation `resolve()` makes could be deleted with
everything still green.
**If it fails** S2: wrong in one direction is blurry text on every monitor move, wrong in the other is a
stutter on every one.

**A-30 · The extremes: 8 % and 6400 %** ✅ **2026-09-04 (T10)**
**Expect** Both resolve to a sane device resolution, clamped by `pageDisplay/maxRenderResolution`; no
overflow, no zero, no allocation proportional to the square of the zoom.
**Result 2026-09-04** Both, and the last clause is the one with teeth. **The cap carries a compensating
`upscale`**: 6400 % on a 192-DPI monitor asks for 12,288 DPI and is capped at 2,400, and `dpi * upscale`
must reproduce the request exactly — capping without it silently shrinks the page, which reads as a zoom
bug rather than as a cap. **No allocation proportional to the square of the zoom** is `TileGrid`'s half and
is asserted there (A-31): at that resolution an A4 page is 19,833 x 28,067 device pixels and the returned
set is bounded by the VIEWPORT, with a same-viewport-coarser-page comparison as the control. A separate
case feeds NaN, infinity, zero and negative values through every input and requires a usable resolution
out of all of them — this runs on every paint, from a scale factor the OS supplied and a zoom a settings
file supplied, and a zero here divides three files away.
**If it fails** S2 — 6400 % of an A4 page at 192 DPI is where an integer overflow lives.

**A-31 · `tst_tilegrid` — visible, prefetch, evict** ✅ **2026-09-04 (T10)**
**Expect** The three sets are disjoint; the visible set covers the viewport exactly with no gap and
no unnecessary tile; the prefetch set honours `performance/prefetchPages`; a tile that left the
viewport moves to evict.
**Result 2026-09-04** All four. *No gap and no overlap* is asserted as an **area identity** rather than by
eye — twelve rectangles that tile a page have exactly the page's area, and any gap or overlap changes the
sum — and no tile is allowed to hang off the page, which is invisible otherwise because a rasterizer
returns off-page pixels as white. Disjointness is checked by insertion into a set, so a duplicate WITHIN a
set fails too.
**ONE CLAUSE WAS CORRECTED RATHER THAN MET.** `performance/prefetchPages` counts **pages**, and `TileGrid`
sees one page: deciding *which* pages to ask about belongs to `ui/viewer/PageLayoutEngine` at T13. Within a
page the knob is a ring of tiles, `TileGrid::Config::prefetchRings`, and that is what this case asserts.
Both halves are real and they are not the same number; naming only the first would have left the tile ring
undecided and unmeasured.
**AND THE CASE ITSELF WAS WRONG FIRST TIME, which the control found.** Replacing floor division with
truncation — the actual defect the code guards against — reddened **nothing**: a viewport starting above a
page clamps its first row to 0 either way, so only the LAST index tells the two apart. Two assertions were
added for a viewport entirely off the page on each axis, and with truncation restored those are the only
lines in the suite that redden. Convention 7 catching a case rather than a defect.
**If it fails** S2.

**A-32 · `tst_presentation` — all four layout modes** ✅ **2026-09-04 (T10)**
**Expect** Single, Single Continuous, Two-Up, Two-Up Continuous each produce page rects in document
order with correct gaps, and page-rect ↔ document-point mapping round-trips.
**Result 2026-09-04** Four modes that behave as four, which took a signature change: `layout()` takes a
**current page**, because Single and Two-Up show ONE page or ONE spread and without it they are
indistinguishable from their continuous twins. The round trip is asserted over three probe points per page,
**none of them the centre** — a dropped y-flip or a dropped CropBox offset is exact at the centre of a
symmetric page and wrong everywhere else — across a plain page, a page whose CropBox origin is not (0,0),
and a page rotated 90°. A point in a gap belongs to **no** page rather than to the nearest one.
**If it fails** S2.

**A-33 · Cover page shifts every subsequent pair's parity** ✅ **2026-09-04 (T10)**
**Steps** Two-Up with `showCoverInTwoUp` on, over an odd page count.
**Expect** Page 1 alone, then 2–3, 4–5. Off: 1–2, 3–4.
**Result 2026-09-04** Asserted **twice over**, as page numbers in spreads and again through the rectangles,
because a layout whose parity is wrong still produces plausible rectangles and comparing coordinates alone
would let it through. The cover is alone **and it is a recto** — the left column of the first spread stays
empty rather than the page sliding across — which is the entire reason a person turns the option on; the
same rule puts a lone final page in the left column with the right one blank. **With the control**: with the
cover off, page 1 shares its row with page 2, without which the case is satisfied by a layout that always
puts page 1 alone.
**Negative controls run:** removing the parity shift reddens the spread assertions; putting the cover in the
verso column reddens the geometry half. **If it fails** S2 — this is where the off-by-one lives.

**A-34 · RTL binding swaps the spread without changing page order** ✅ **2026-09-04 (T10)**
**Expect** With `rtlBinding` on, page 2 is on the **right** of the 2–3 spread and page 3 on the left;
the document order is unchanged. Combined with a cover page and an odd count, all three interact —
and the corpus has a file for exactly that.
**Result 2026-09-04** All three at once, over five pages with the cover on. RTL maps column 0 to the right
and changes **nothing else**: `pageSlots()` still walks pages 0–4 in order and the vertical stacking is
still 1 | 2–3 | 4–5, so the document order is asserted rather than assumed. The mirrored cover — a recto
in LTR, so the left column in RTL — is the one place the column swap is visible on a spread holding one page.
**And the case asserts what RTL does NOT do**: in the single modes there is one column and nothing to swap,
so a layout that mirrored them (the tempting wrong reading, that RTL does something everywhere) would push
every page against the right edge for no reason.
**Negative control run:** removing the column swap reddens four assertions here and nothing else in the
suite. **If it fails** S2.

**A-35 · `RenderRequest` equality is the cache key, and it is a value type**
☐ **Goal** D2-12. **Steps** A static assertion that it holds no pointer and no handle; a runtime case
that two independently constructed equal requests compare equal and hash equal; and one that a
differing `Revision` makes them **unequal**.
**Expect** All three. **If it fails** S1 — a pointer member makes the key identity-based and every
cache lookup misses silently, and a defaulted `Revision` makes *showing a stale tile after a Stage 3
mutation* the default behaviour. Neither has any symptom a person would attribute to the cache.

### 5.6 Scheduler and cache

**A-36 · A cancelled request is never started** ✅ **2026-09-04 (T11)**
**Goal** The stage's headline behaviour. **Steps** Enqueue, cancel before a worker picks it up,
with the pool paused.
**Expect** The rasterizer is **never entered** for that request — asserted by a counting fake
`IPageRasterizer`, not by observing that no image arrived.
**Result 2026-09-04** The pool is PAUSED, so the cancel provably beats the worker rather than racing it — a
case that enqueued and cancelled quickly would pass on a fast machine and fail on a loaded runner for a
reason nobody could act on. The rasterizer's entry count is **0**, and `cancelledBeforeStart` is 1. **With
the control**: an uncancelled request IS started, without which the case is satisfied by a scheduler whose
workers never run at all.
**Back door named:** this drives `RenderScheduler` directly and therefore proves the *destination*.
**It does not prove that scrolling reaches it** — that is **M-04**, and it is manual because
perceived smoothness has no offscreen proxy. (Rule 1, §1.)
**Negative control run:** making `cancel` mark the job instead of dequeuing it reddens this case and A-37.
**If it fails** S2.

**A-37 · Duplicate requests coalesce** ✅ **2026-09-04 (T11)**
**Expect** Two viewports over one tile produce **one** rasterization.
**Result 2026-09-04** One job, **two tickets**, and that split is the point: cancelling one holder must not
take the other's pixels, and the job goes only when no ticket is left on it. Both directions are asserted.
**One behaviour beyond the case**: a VISIBLE request landing on a queued prefetch **raises** the job's
priority, or a tile that scrolled into view waits behind every prefetch already queued — nothing looks
broken and the page simply fills in from the wrong end. **If it fails** S3 — twice the work, correct output.

**A-38 · Delivery is on the UI thread, and a superseded delivery is dropped** ✅ **2026-09-04 (T11)**
**Expect** `ready` fires on the UI thread; a result arriving for a cancelled or `Revision`-superseded
request is dropped rather than cached.
**Result 2026-09-04** The first half is asserted **more strongly than it is written**, because offscreen
there is no UI thread: **no subscriber is ever entered from a worker**, checked by recording the thread id
at every callback, with the data-free wake handler as the control proving a worker thread existed at all.
That is also what lets `RenderCache` be lock-free — only the pumping thread touches it.
**THE SECOND HALF FOUND A HOLE IN THIS SUITE RATHER THAN IN THE CODE.** The first pass covered a request
superseded while QUEUED and one superseded after it had FINISHED, and a control that kept superseded results
passed — because a job cancelled or superseded **while running** takes a third path that no case could
reach: everything else had either not started or already finished. A latch in the fake rasterizer now holds
a tile inside the engine so a case can act mid-flight, and both branches have a case. With them, the control
reddens.
**Negative controls run:** delivering from the worker thread reddens four cases; keeping a superseded result
and keeping a cancelled one each redden this one. **If it fails** S1 — the second half is how a stale tile
reaches the screen.
**WHAT THIS CASE AND ITS NEIGHBOURS DID NOT COVER, added 2026-09-06 after M-01 found it (I-19)** and named
here rather than left for the next reader to infer: **every case in this suite drains the completion queue
by calling `deliverReady()` itself.** That is a back door — no running product calls it, the wake handler
does — so the whole suite proved the DESTINATION and none of it proved the product ever arrives there.
A cache hit woke nothing for the whole of Stage 2 and every case here stayed green. **A-83 is the missing
half**, and convention 19 is why it is written at this assertion instead of only in the issue log.

**A-83 · A tile served from the CACHE wakes the pump — the half A-38 reached through a back door** ✅
**2026-09-06** **Goal** I-19, and convention 19 (`Implementation-Plan.md` §1.5) paid off at the assertion
that owed it. `RenderScheduler` has two producers of a completion and only one of them woke anything.
**Steps** `ctest -R "render scheduler WAKES"`. Enqueue a tile (a MISS), drain it, then enqueue **the same
tile again** with nothing else in flight, and count wake-handler calls across the second `enqueue()`.
**Expect** The second, cache-served `enqueue()` calls the wake handler. **Its MISS leg is a `REQUIRE`,
not a `CHECK`** — that is the control, and without it the case would pass identically against a scheduler
whose wake handler never fired at all (convention 7).
**Result 2026-09-06** Green on the fixed scheduler. **Run first against the unfixed one, which is what
makes it evidence**: `CHECK( wakes.load() > before )` expanding to `1 > 1` — the miss woke, the hit did
not. **What it does NOT cover:** it counts wakes, not repaints. That the wake reaches a widget and the page
comes back is **M-01**, and M-01 is where this defect was found in the first place.
**If it fails** S2 — a completion nothing drains is a blank page, and the user cannot tell it from a
rendering failure.

**A-39 · A `PageView` destroyed with tiles in flight does not crash** ✅ **`core/` half 2026-09-04
(T11); widget half 2026-09-04 (T13)**
**Goal** Convention 10 (`CLAUDE.md`) in its threaded form — Stage 1's I-12 was an S1 segfault of
exactly this shape, a lambda outliving the widget that installed it.
**Steps** Open, scroll to enqueue, close the tab while four tiles are in flight. Repeat 50 times.
**Expect** No crash, no use-after-free under `x64-debug`.
**Result 2026-09-04, and what it does NOT cover.** There is no `PageView` until T13, so what is provable now
is that the **scheduler** survives being destroyed mid-flight: 50 rounds of construct, enqueue eight tiles,
destroy without waiting. The destructor stops and JOINS before any member the workers hold is destroyed.
**The widget half — the tab closed while its tiles are in the queue — is T13's and is not covered here**,
and neither is a data race: this toolchain has no TSan (risk R7), so a race that happens not to fire in
fifty rounds would pass.
**Result 2026-09-04 (T13), the widget half.** `ui.a page view destroyed with tiles in flight does not
crash` opens a 20-page document into a real `MainWindow` with the scheduler **PAUSED**, so the jobs are
queued and never start — which makes *in flight* a state the case HOLDS rather than a race it hopes to
hit. The window is then destroyed. `TilePresenter`'s destructor unsubscribes **by token** and cancels
every ticket, and the case asserts `pendingCount() == 0` afterwards and then, on resume,
`cancelledBeforeStart > 0` with `started == 0` — the rasterizer was never entered.
A second case covers the ordinary close: `ui.closing a tab closes its handle and drops its tiles`
asserts all three steps of `DocumentWindow`'s protocol, because a viewer that only cancelled would keep a
closed document's megabytes for the life of the process.
**Negative control run:** removing `renderCache.dropDocument()` and the `backend.close()` from that
destructor reddens both close cases. The TSan caveat above still stands. **If it fails** S1.

**A-40 · `tst_rendercache` — LRU and the cap** ✅ **2026-09-04 (T11)**
**Expect** Eviction is least-recently-used; total held bytes stay under `performance/tileCacheMB`;
the disk tier honours `diskCacheMB`.
**Result 2026-09-04** All three. The LRU case **touches an entry before the eviction**, without which it
would pass equally against a first-in-first-out cache — a different policy that agrees on an untouched
sequence. The cap is in **bytes**, not entries: a cache that counted tiles would hold ten 4-megapixel tiles
as happily as ten small ones. A tile larger than the whole cap is **refused rather than admitted**, because
admitting it evicts everything to store one tile that the next insertion evicts — a cache that holds nothing
and reports 0 %, indistinguishable from one that is merely cold.
**The disk tier is real and is OFF by default** (D2-14: whether it earns its place is T18's measurement).
Its case runs against a `QTemporaryDir`, checks that an evicted tile's **file** goes with its index entry —
a tier that forgot one and left the other would grow the directory without bound while reporting a cap it
was honouring — and compares the recovered **payload**, which is what would catch a store serving the wrong
file. A promoted disk hit is lifted back into memory. **What it does not claim**: the tier does not survive
a restart; its index is in memory and the directory is purged on construction.
**O7's instrumentation is built with the cache rather than at T18** (D2-14), and the number that answers O7
— `crossDocumentEvictions` — has a case **and its control**: a document evicting its own tiles is not
cross-document contention and must not be counted as such, or the number only measures how full the cache
is. **If it fails** S2.

**A-41 · `Scope`/`Revision` invalidation, and a stale tile is never served** ✅ **2026-09-04 (T11)**
**Expect** `invalidate(Scope, Revision)` drops exactly the tiles the scope names and no others; a
lookup for an older `Revision` **misses**.
**Result 2026-09-04, and the half that is not obvious is the one that had to be built.** A `RenderRequest`
carries a `Revision` and its equality IS the key (D2-12) — so a cache that only DROPPED would lose every
tile the moment a revision moved, and `Scope` would buy nothing at all: rotating page 3 of a 900-page
document would re-render all 900, with nothing wrong on screen and no test failing. So invalidation does two
things: it drops what the scope touches and **re-keys the rest to the new revision**. The Expect's second
clause then holds structurally rather than by a second check — after a re-key there is no entry at the old
revision to find.
**`invalidate()` takes a `DocumentHandle` the signature above omits**, and that is a correction rather than
an addition: a `Scope` names pages and objects and says nothing about which document they belong to, while
**O7** leaves open whether one cache serves several — `RenderRequest` carries a handle precisely so two
documents can share one. Without it, invalidating this document's page 3 drops page 3 of every open
document, which the case asserts from the other side.
An **object** scope drops conservatively (every page of its document), which is `core/doc/Scope`'s widening
rule exercised from its intended caller: a wasted repaint is the correct failure direction, a stale tile is
not.
**Negative controls run:** removing the re-key reddens this case; removing the document filter reddens it
too — the first attempt at that control was aimed at a line that was not the filter and passed, which is
worth recording, because a control that does not fail for the right reason proves nothing.
**If it fails** S1 — this is the mechanism that makes
Stage 3's mutations safe, and Stage 2 is where it must already be right.

### 5.7 The engine

**A-42 · Every corpus file opens** ✅ **2026-09-04 (T9)** **Steps** `ctest -R "^render\."`
**Expect** For each: a document handle and a page count matching `PROVENANCE.md`. The malformed set is
allowed to open *repaired* — but never to open silently wrong.
**Result 2026-09-04** All 29 `ordinary/` files open, and the check is a page TREE rather than a
count: every page must have a usable display size and every box must resolve inside its MediaBox.
The malformed set opens or is refused with a named key, and — the assertion that matters —
**at least one file PDFium opened happily is reported as damaged by our own reader**. PDFium repairs
silently and reports nothing, so without the first-party `RepairReport` run over the same bytes,
*"it opened"* is all anyone would ever learn and Stage 3's forced Save-As would have nothing to
trigger on.
**ONE FINDING, and it is the reason `pages-count-wrong.pdf` exists.** `FPDF_GetPageCount` returns
the `/Pages` dictionary's `/Count` field verbatim, so that file — which declares **`/Count 97` over
three real kids** — opened through PDFium claiming **97 pages**. `PROVENANCE.md` says in as many
words that the answer has to be the tree, and the page count reaches the status bar and the
scrollbar before anything walks the document: believing it gives a three-page file a 97-page
scrollbar. `PdfiumDocument` now drops the TRAILING run of pages that will not load, and only the
trailing run — a page that fails in the middle keeps its placeholder, because removing it would
renumber every page after it and a link pointing at page 40 would land on 39.
**Negative control run:** restoring the trust in `/Count` reddens exactly this case.
**If it fails** S1.

**A-43 · The password callback** ✅ **2026-09-04 (T9)**
**Expect** `corpus/encrypted/*.pdf` opens with the right password, is refused with the wrong one, and
the refusal is a **named** `Result` rather than an empty document.
**Result 2026-09-04** Four cases. All four enciphered fixtures open with `user`; the wrong password
is refused as **`PermissionDenied` + `error.open.badPassword`**, and the attempt loop is bounded, so
a scripted caller returning the same wrong string stops rather than spinning. **Giving up is
`Cancelled`, not a failure** — a cancelled prompt is not a damaged file, and a message bar saying so
would be lying to the person who just pressed Escape. `owner-empty-user.pdf` opens with **no prompt
at all**, which is what its `PROVENANCE.md` row exists to check from the other side. And a callback
that is absent entirely — a batch run, a headless test — is a refusal rather than a parse failure.
Every case asserts whether the callback WAS ASKED, which is what separates an encrypted file that
opened from an unencrypted one; without it the suite would pass against a corpus of plain documents.
**If it fails** S2.

**A-44 · `tst_rasterizer` — one page to a `QImage`** ✅ **2026-09-04 (T9)**
**Expect** A `RenderRequest` produces an image of exactly the requested pixel size, non-blank, with the
render flags honoured.
**Result 2026-09-04** **This is where a page is drawn for the first time.** Exactly the requested
pixels, the right stride, non-blank measured as distinct byte values rather than "some pixel is not
white" (one stray dot would satisfy that), and five invalid requests refused rather than returned as
correctly-sized emptiness — an empty image is indistinguishable from a blank page once it is in a
cache. The result is a `core::RasterImage` of BGRA bytes, **not a `QImage`**: `core/` may not name a
QtGui type (I-03), and `ui/` wraps the bytes at the far side without a copy.
**The tile arithmetic has a case of its own**, because it is the thing most likely to be wrong:
`FPDF_RenderPageBitmap` has no region parameter, so a tile comes out by passing the WHOLE scaled
page's size with a NEGATIVE origin. Passing the tile's size instead scales the entire page into
every tile — a bug that reads as a zoom problem. Asserted by comparing two tiles of one page and by
rendering the same tile twice.
**THE FIXTURE FOR THE FLAGS CASE WAS WRONG FIRST TIME, and the failure was the code being right.**
It rendered `text-a4.pdf`, which is black body copy on white paper — content that is ALREADY
greyscale, so a `Grayscale` render of it is byte-identical by construction. Moved to
`image-dct-rgb.pdf`, and the case now first REQUIREs that the colour render actually contains
colour, so the comparison cannot pass vacuously.
**Negative control run:** replacing the negative origin with `(0,0)` and the page size with the
tile's reddens the region case and the scale case, and nothing else.
**If it fails** S1.

**A-85 · A widget annotation is drawn, and the annotations flag governs it** ✅ **2026-09-06**
**Goal** **I-21**, and the question the golden suite structurally cannot ask. A baseline compares
our render with our own earlier render, so a page that never drew its widgets has a reference
saying a form is an empty page — which is what `golden/ordinary/forms-acroform/` held, green, for
four days.
**Steps** `ctest -R "annotation is drawn"`. Render `ordinary/forms-acroform.pdf` twice, differing
only in `RenderFlags::Annotations`, and compare the two rasters byte for byte.
**Expect** They DIFFER. **No reference image appears in this case at all**, which is the point: the
assertion is a property of the renderer rather than a comparison with a capture. Both renders must
also be non-blank — the heading and the labels are page content and appear either way, so a case
that only checked non-blankness would have passed against the defect.
**Result 2026-09-06** Green. **The control ran in both directions and it discriminates**: with
`FPDF_FFLDraw` removed the case fails at `CHECK( differing > 1000 )` with **`differing bytes: 0`** —
annotations on or off changed nothing on that page — while its companion below stayed green.
**Its companion is the other half of PDFium's split.** `ordinary/annots-mixed.pdf` — nine NON-widget
annotations with `/AP`, the same flag — passed before this fix and after it. Keeping both is what
makes a future regression say WHICH path broke: widgets alone, or `FPDF_ANNOT` entirely.
**If it fails** S2 — content missing, by M-03's own severity rule.

**A-45 · Thread confinement — eight workers, concurrently**
☐ **Goal** D2-9. **Steps** Rasterize 200 tiles across 8 workers over 4 documents, repeatedly, under
`x64-debug`.
**Expect** No crash, no corrupted output, no data race. **`PdfiumInit` is entered once.**
**Result ✅ 2026-09-04 (T9)** Eight threads, 25 tiles each, over four documents — and the workers
SHARE documents deliberately, because two threads on one document is precisely the case per-worker
confinement was supposed to make safe and did not. 200 of 200 tiles rendered, none failed, and
`initializationCount()` is 1.
**What made this passable at all is the engine lock**, which lives in `PdfiumInit` because that file
is the one place PDFium's global state is touched. D2-9's *"thread-confined per worker"* was
measured FALSE at T3 (**I-05**); the lock is the conservative half of the choice **T11** still has
to make, and is correct under either outcome.
**WHAT IT DOES NOT PROVE, named at the assertion** (convention 19): it is **not a data-race
detector**. This toolchain has no TSan (risk R7), so a race that happens not to fire in one run
would pass. What makes it worth having is that the UNGUARDED version of this exact shape crashed
reliably at T3.
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
form. **Result at T9 — the case is now CLOSED.** The last sentence of the Expect is asserted in
`tests/render/tst_rasterizer.cpp`: the shipped backend reports exactly `{IDocumentBackend,
IPageRasterizer}`, and `unimplementedBackendInterfaces()` names the other five in enum order. The
five accessors are **null rather than empty stubs**, and a handle is never reused — a token held
across a close MISSES rather than starting to name a different document.
**The file had to SPLIT to get there, and the reason is worth recording.** `tst_backend.cpp` drove
`pdf::createBackend()` directly, which was fine while that factory returned a backend implementing
nothing and needing no engine. **A backend that opens documents IS the engine**, so the factory moved
into `rawpage_pdfium` — which `rawpage_assert_headless()` forbids the headless suite from linking by
name (D2-2). What stays in `tests/unit` is asserted over a **test backend defined in that file**: the
interface declarations, `notImplementedError`, the census helper and `RasterImage`'s stride
validation, all still running under `portable-asan`. **What is lost is stated rather than absorbed**:
the SHIPPED backend's census is no longer asserted under ASan.
**If it fails** S1 — convention 6's argument: a stub that silently succeeds surfaces stages later as
data nobody produced.

**A-47 · `openFiles()` asks the backend, not the filename**
✅ **2026-09-04 (T9)** **Goal** This replaces Stage 1's A-58, which asserted the opposite and was
correct then (D1-2).
**Steps** A `.pdf` file that is not a PDF; a PDF with a `.dat` suffix.
**Expect** The first is **refused with a named reason**; the second **opens**. Deciding whether bytes
are a PDF is `IDocumentBackend`'s job.
**Result 2026-09-04** Asserted from BOTH sides, and the split matters. The **engine half** is in
`tests/render/`, against real bytes: a copy of `minimal.pdf` named `.dat` opens, and a `.pdf`
containing a sentence of English is refused with `error.open.notPdf`. The **UI half** is in
`tests/ui/`, against a fake backend, because `ui/` may not link the engine (D1-4) *and* because the
claim under test is about the shell's POLICY — a fake is what lets a case decide whether the shell
consulted the backend **at all**. "The file was refused" and "the backend was never asked" look
identical from outside, and only the second is the bug this case exists to catch, so the fake records
every path it was asked about.
**Existence stays the shell's own check**: *"there is no such file"* is a different message from
*"that is not a PDF"*, and asking the engine about a path that is not there would turn the first into
the second. The recent list is now noted only AFTER the backend accepts — Stage 1 noted every path it
was handed because it could not tell which were openable.
**Two Stage 1 cases were rewritten rather than deleted**, because they asserted the OLD contract and
were correct under it. **`Services` gained `core::IDocumentBackend&`** at the same time, which is the
consumer `app/Services.h` has been saying since T1 that it was waiting for.
**Negative control run:** ignoring the backend's refusal reddens both halves of the UI case.
**If it fails** S2.

**A-79 · The REAL engine on the REAL scheduler — the assembly nothing was testing**
✅ **2026-09-04 (T13)** — **a case this guide did not list**, added with T13 for the reason below.
**Goal** Three suites touch the render path and each fakes one end of it: `tests/unit` drives the
scheduler and the cache over a fake rasterizer (it may not link an engine — D2-2); `tests/ui` drives
`PageView` and `TilePresenter` over a fake, for D1-4's reason; and the rest of `tests/render` calls
`PdfiumRasterizer` directly with no scheduler. **So the one arrangement the shipped product actually
runs — the real PDFium rasterizer, reached through the real backend by `app::BackendRasterizer`, on a
worker thread the real `RenderScheduler` owns — was the arrangement nothing exercised.** That is the shape
Stage 1 kept finding under other names (I-34, I-42, I-43, I-46): every piece tested, the assembly not.
**Steps** `ctest -R render.pipeline`.
**Expect** `ordinary/text-a4.pdf` opens through `pdf::createBackend()`; `TileGrid` produces the page's
visible tiles; every one is enqueued on a scheduler built with the **shipped factory** (`workerCount`
defaulting to 1, which is I-05's answer and not a test's convenience); all of them come back, none fails,
the cache holds them, and at least one tile carries **more than one colour** — a text page has ink on it,
and a blank buffer would satisfy every other assertion.
**And the adapter's other branch:** a request for a document that has been closed is refused by name
(`error.render.documentClosed`, `Unavailable`) rather than dereferencing a null rasterizer. In the product
that branch is reached only if `DocumentWindow`'s close protocol is not followed; building the failure
deliberately is what makes the recovery real rather than assumed.
**It drives `app::BackendRasterizer` itself, not a copy** — which is why that class became a header in
`app/` rather than staying private to `CompositionRoot.cpp` (issue **I-09**).
**What it still does not cover:** there is no widget here. *A page is on screen* is A-53's claim over a
fake, and the two halves meeting on glass is the manual pass. **If it fails** S1.

### 5.8 Golden images

> **Read `golden/README.md` beside the images before reading these five cases.** It owns what a
> green run claims (*the product rasterizes our own corpus the same way it did when the baselines
> were captured*), what it does not (*correctness*, and *anything about real-world PDFs*), the split
> between the hand-authored `manifest.json` and the tool-written `captured.json`, and the two
> reproducibility hazards that would otherwise be discovered rather than known — the OS text
> renderer, and a substituted face. The cases below record results; that file records meaning.

**A-48 · Baselines at 96 / 144 / 192 DPI over the corpus** ✅ **2026-09-06 (T17)**
**Steps** `ctest -R render.golden`
**Expect** Every page within the perceptual threshold of its baseline. **If it fails** S2, and the
first question is whether the baseline or the renderer moved — A-05 is what makes that answerable.
**Result 2026-09-06.** Three cases, one per resolution — `render.golden.golden baselines match at
96 / 144 / 192 dpi` — over **198 baselines: 56 corpus files, 66 pages, three resolutions**. All
green, and the margin is the number worth recording: a re-render of the unchanged corpus differs
from its baselines in **zero pixels**, so the whole of both thresholds is headroom rather than a
budget already spent. Each case additionally asserts `compared > 0`, because *"0 compared, 0
failed"* is a passing run over nothing.
**What the green means, in the words §9.7 requires:** *consistent with our own generated corpus*,
never *rendering is correct*. Asserted here rather than assumed as well: **every corpus file is a
baseline case or an excluded row with a stated reason** (61 = 56 + 5), checked in both directions,
so a corpus file added at a later stage cannot silently have no baseline — `corpus/make-corpus.py
--check-provenance`'s rule in a second subject.

**A-49 · A perturbed render fails, and writes a diff image**
✅ **2026-09-06 (T17)** **Goal** D2-13, and convention 7 — a golden suite that cannot fail is
decorative.
**Steps** Perturb one render flag; run.
**Expect** Failure naming the file and page, **with a diff image written beside the expected and
actual**. **If it fails** S2: "golden mismatch, 0.7 % of pixels" is not a bug report.
**Result 2026-09-06, and the first version of this case PASSED WHEN IT SHOULD HAVE FAILED.** The
perturbation is `RenderFlags::Grayscale`, and the page it was first aimed at was
`ordinary/text-a4.pdf` — black ink on white paper, where greying the render is the **identity**.
171 of the 198 baselines are grey in every pixel, so the comparison was working perfectly and the
control was measuring nothing. Repointed at `ordinary/image-dct-rgb.pdf`, one of the 27 that carry
colour: the comparison now reports **165,413 differing pixels of 891,662**, the failure sentence
names the worst channel delta and the coordinates of the first bad pixel, and
`<build>/golden-failures/` receives `-expected.png`, `-actual.png` and `-diff.png` with the changed
regions in red over a ghost of the render. **A second case is its control in the other direction** —
the same file, page and DPI without the flag — because a comparison that answered *different* to
everything would have made this one green too. That pair is convention 7's own requirement, met by a
case that first failed to meet it: T11's finding a second time.

**A-50 · `golden-update` refuses without `--reason`** ✅ **2026-09-06 (T17)** **Goal** D2-3.
**Expect** Refused, with a message saying why. With `--reason`, the baseline regenerates and the reason
is available to the commit. **If it fails** S2 — the mechanism `Implementation-Plan.md` §2 names has no
teeth without this.
**Result 2026-09-06.** `render.golden.golden-update refuses to regenerate a baseline without a
stated reason` drives the executable as a child process. A bare invocation exits **2** with a
diagnostic naming `--reason` and citing D2-3; a second invocation carrying only `--filter` is
refused identically **and leaves `captured.json`'s timestamp untouched**, because a refusal that had
already rewritten half the tree before deciding would satisfy every other assertion here. **The
control supplies a reason** and a filter matching nothing: the run gets *past* the D2-3 check and
stops with a different diagnostic and **exit 1** — 2 means *I will not*, 1 means *I tried and could
not* — which is what makes it a control rather than a second copy of the assertion above. The
reason itself lands in `golden/captured.json`, in an append-only `golden/UPDATES.md`, and in a
ready-to-paste commit block: a **reviewed file** rather than only a commit message, because a commit
message can be amended and a squash can lose it.

**A-51 · A PDFium version change is visible to the golden suite** ✅ **2026-09-06 (T17)**
**Expect** With A-05's pin edited, the golden suite does not merely fail — configure never gets that
far. The two mechanisms are complementary and this case proves the ordering. **If it fails** S3.
**Result 2026-09-06.** The configure-time half is A-05's and is unchanged
(`cmake/PdfiumAssert.cmake`, PA-1 … PA-5). What T17 added is the half configure **cannot** see:
`golden/captured.json` records the engine the baselines were made against, and `render.golden.the
baselines were captured against the PDFium this build is pinned to` compares it with
`pdf::render::version()`. In the ordinary case it can never fire, and that is the point — reaching
it means the `pdfium.dll` beside the executable was replaced *after* the build, which is exactly the
reader `pdf/render/PdfiumInit.h`'s `version()` comment is written for, and which would otherwise
show up as pixel drift across all 198 baselines at once and be diagnosed as a rendering regression.
Verified by setting the recorded version to `999.0.0.0` and watching the case fail by name.

**A-82 · No baseline depends on an installed face unless it says so** ✅ **2026-09-06 (T17)**
**Goal** Convention 22, and **I-13**'s tripwire. *(Added at T17 — the eighty-second case. This guide
was written before the font bridge existed, so nothing in it kept I-13's finding from being re-made
silently.)*
**Steps** `ctest -R "render.golden.only the declared"`.
**Expect** Every substitution `golden/captured.json` recorded is `standard-declined` or
`none-available` — both engine-internal and identical on every machine — **except** on the cases
`golden/manifest.json` declares `machineDependent`.
**Result 2026-09-06, and it earned its place on the first capture run.** The expected
machine-dependent case is `ordinary/font-not-embedded.pdf`. The capture found a **second**:
`malformed/bad-name-hex-escape.pdf` resolves `Helve -> Segoe UI [substituted]`, because the broken
`#`-escape the file is named for truncates `/Helvetica` to `/Helve`, which is not a standard-14 name
— so the bridge matches a real installed face instead of declining to PDFium's own copy. Nothing
predicted it; the manifest says so because the capture said so first. Both cases are compared only
where this machine resolves the same faces the capture did, and **skipped by name** elsewhere
(A-62's rule, second subject) rather than reported as a rendering regression.
**Why this is a numbered case rather than a comment:** if the base-14 decline is ever removed, all
198 baselines move at once and the failure reads as a renderer regression. This names the cause in
one line. **If it fails** S2.

### 5.9 The viewer — `rawpage_ui_tests`, offscreen

> Everything in this subsection is written against Stage 1's **I-36**: assert the geometry the layout
> gave the widget, **then** assert the consequence. A case that asks a viewer what it contains while
> the layout gives it zero pixels is green and worthless.

**A-52 · The layout gives `PageView` real pixels** ✅ **2026-09-04 (T13)**
**Steps** `show()` under `offscreen`, then `activate()` **every** layout level (convention 12), then
read `PageView::size()`.
**Expect** Non-zero, and matching the window's central area within the scrollbar allowance. **Assert
the size the widget actually got before asserting anything that depends on it** — `resize(240)`
returning 1044 px is a finding, not a failed assertion.
**Result 2026-09-04.** `ui.the layout gives the page view real pixels`: a 1200x900 `MainWindow` with one
open document, `show()`n offscreen and every layout level `activate()`d, gives the viewer **more than
400x300** and never more than the window. The controller is then asserted to have been told the SAME size
in device pixels — `viewport()->width() * devicePixelRatioF()` — because the two spaces are equal only at
100 % and an implementation that confused them is correct on this machine and two-thirds size on the P-1
rig's 4K panel. Two further cases assert what depends on it: the content size grows with the page count
and (to within the non-scaling margins) doubles with the zoom, and the scrollbars are ranged in LOGICAL
pixels and reach the end of the document.
**One of those is weaker here than it looks:** the scrollbar case cannot fail on a 100 % monitor, because
the device-pixel ratio the offscreen platform reports is 1.0 and the two spaces coincide. It is a real
assertion on a scaled monitor and a tautology here; manual case M-01 is where it means something.
**If it fails** S1 — and it is the failure that hid for six days at Stage 1.

**A-53 · A page is actually painted** ✅ **2026-09-04 (T13)**
**Steps** `QWidget::grab()` after tiles have been delivered.
**Expect** The returned image is **not** uniform background: page-shaped content with a page border at
the expected rect. **This is the case that distinguishes "tiles were requested" from "a page is on
screen"**, and it is the whole reason §1's rule exists.
**Result 2026-09-04, and read the fake's colour choice as part of the case.** `tests/ui` may not link the
engine (D1-4), so the rasterizer is a fake — and a fake that returned WHITE would make the two claims
indistinguishable, because `TilePresenter` already paints a white sheet under every page before any tile
arrives. The fake fills a flat blue nothing else in the viewer draws, so the grabbed image containing it
can only be true if the request was built, queued, run, delivered, converted to a `QImage` and painted at
the right rectangle. The case additionally asserts the grab is **not uniform** (the sheet and its border
are there too), and that `lastPaint()` reports pages and tiles drawn with **zero placeholders**.
The fake also pads its rows — `bytesPerLine > width * 4` — because PDFium happens to return tight rows, so
the stride assumption `core::RasterImage` exists to prevent would otherwise never be exercised anywhere.
**Its negative control is a case of its own**: a `PageView` with no document must NOT contain that colour.
**Negative control run:** commenting out `painter.drawImage` reddens this case and nothing else.
**What is still owed and by whom:** the REAL engine reaching a real widget. `tests/render` now drives the
real PDFium rasterizer through the real `RenderScheduler` and the shipped `app::BackendRasterizer`
(`render.the shipped rasterizer factory renders real tiles on a real worker`, added at T13 because that
assembly had no case at all), and `tests/ui` drives the real widget over a fake. **The two halves meeting
on glass is the manual pass**, M-03. **If it fails** S1.

**A-54 · `tst_viewport` — scroll and zoom** ✅ **2026-09-04 (T13)**
**Expect** Scroll offsets clamp at document bounds; zoom about the cursor keeps the point under the
cursor fixed; page-at-a-time paging snaps to boundaries.
**Result 2026-09-04.** Thirteen cases, **every one of them with no window at all** — which is the whole
reason `ViewportController` and `PageLayoutEngine` are separate objects from `PageView`. The clamp is
asserted at both ends, for a document smaller than the viewport (maximum **zero**, not negative — a
negative maximum lets the page drift off the top edge, which reads as content that cannot be reached) and
for a document that just got shorter. Zoom-about-a-point is asserted as an **equality rather than a
tolerance**, because the content space scales uniformly and the answer is exact; its negative control is
an anchor at the origin, where a correct implementation moves nothing and one that used a position where
it wanted a ratio moves something.
**Two things A-54 does not name and that are here anyway:** the fit arithmetic — including that it is
computed against the **physical** DPI, so the P-1 rig's 163-DPI panel and the nominal 144 differ by
exactly 163/144 — and the view history, which records deliberate navigation and **not** scrolling, a
distinction asserted rather than described because a history that captured every wheel notch is a Back
button that goes back one notch.
**Page-at-a-time paging is NOT covered and is T14's**: `NavNextScreen` and `NavPrevScreen` are §8 Viewer
commands and nothing registers them yet. Recorded rather than left to look covered.
**Negative control run:** dropping the ratio from `zoomAbout` reddens the zoom case. **If it fails** S2.

**A-55 · `KeyContext::Viewer` is declared and resolves — the first time in the product's history**
✅ **2026-09-04 (T13)** **Goal** D2-8. **Steps** Focus inside `PageView`; ask
`ShortcutBinder::activeContext()`.
**Expect** `Viewer`. Then a `Viewer`-bound chord dispatches its Viewer command.
**Result 2026-09-04.** `ui.focus inside the page view resolves to Viewer, and nowhere else does`, over a
real `MainWindow` with a real open document. It asserts the OUTSIDE as well: focus on the ribbon still
answers `Global`, which is what makes the inside mean something. And a child widget created inside the
view inherits `Viewer` without declaring anything — the declaration is on the view itself, so Stage 4's
overlays, rulers and tool widgets get it for free.
**Completed at T14, and the case was rewritten rather than extended.** At T13 it registered `ToolHand`
itself and said so at the assertion (convention 19), because nothing did; T14 registers it for real, so
the case now ASSERTS the registration and observes `PageView::tool()` — an effect rather than a counter,
which is what distinguishes *the command ran* from *the command did something*. Re-registering would
have been refused (property 3) and would have gone on watching the case's own lambda.
**And the whole subset is covered at T14 by `ui.every Stage 2 Viewer chord dispatches from the viewer`**,
which walks all 23 chords `Implementation-Plan.md` § Stage 2 lists and requires each to be `Dispatched`
or `Unavailable` — never `NotImplemented` — with a companion case asserting the other seven are still
`NotImplemented` and leave the key unconsumed.
**Negative control run:** removing the `declareContext` call from `PageView`'s constructor reddens this
case and A-56.
**If it fails** S2 — and note that until T13 this answering `Global` was *correct*, so a regression
here looks exactly like the previous fifteen tasks' correct behaviour.

**A-56 · …and the negative: a Viewer chord does NOT fire from the tool finder's search box**
✅ **2026-09-04 (T13)** **Goal** The case that actually tests context resolution. A chord that fires everywhere would pass
A-55 and be wrong.
**Steps** Focus the tool finder's search box; press a `Viewer`-only chord.
**Expect** The Viewer command does **not** run; the key reaches the line edit, through Qt's own
`ShortcutOverride` handshake (convention 15).
**Result 2026-09-04.** The chord is **`H`** — `tool.hand` in §8's Viewer table and a perfectly ordinary
letter in a text box, which is exactly the collision the twelve deliberate ones in `Features.md` §8 are
made of. In the viewer it dispatches; in the search box the resolution finds nothing (`H` has no Global
and no Common row), the outcome is `NoBinding`, `consumes()` is false, and the dispatch counter has not
moved. **The last third of the case sends the key event to the WINDOW rather than to `searchBox()`** —
Stage 1's I-54 is the reason, and it is quoted at the assertion: nine green `[toolfinder]` cases missed a
frozen application because every one typed at the one address a real keystroke does not reach.
**If it fails** S2.

**A-57 · The status bar shows real numbers** ✅ **2026-09-04 (T14)**
**Expect** Page *n* of *N* with the real total; the zoom label tracks the viewport. The em dash Stage 1
showed for a stub is gone **because there are pages now**, not because it was replaced by a guess.
**Result 2026-09-04.** `ui.the status bar shows the real page total and the real zoom` asserts the em
dash BEFORE the document and the count after, against the document's own page count rather than a
literal — which is what "because there are pages now" means — and asserts **the dash comes back when
the last tab closes**, rather than the previous document's figure staying on screen over an empty
window. A second case covers the page box, which has been an input with no consumer since T9: a number
inside the document navigates, and one outside it **puts the current page back** rather than leaving the
box holding a value nothing acted on, because an empty box or one still reading "4000" leaves the user
unsure whether it worked.
**One defect of this task's own, found here rather than by a person:** nothing told the chrome when the
VIEW moved. The numbers were correct exactly once, when the tab was adopted, and then stayed there while
the document scrolled under them — a confident wrong answer, which is worse than the em dash.
`PageView::viewChanged` is now connected in `adoptDocument`, and **not with `Qt::UniqueConnection`**:
Qt refuses a unique connection to a lambda and refuses it with an `ASSERT` that fail-fasts the process,
so the mistake read as 42 unrelated suites dying at once rather than as one wrong argument.
**Negative control run:** dropping that connection reddens both status-bar cases. **If it fails** S3.

**A-58 · The message bar surfaces a repair report** ✅ **2026-09-04 (T14)**
**Expect** Opening a `corpus/malformed/` file shows a message bar naming what was reconstructed, with
a route to the detail; opening a clean file shows **no** bar.
**Result 2026-09-04.** Both halves, and the second is the one that would be lost by a bar saying *no
repairs were needed*: a sentence nobody needs, which would make the real banner unremarkable. The fake
backend returns an **empty** report for a clean file, which is the state *this needed nothing* and is
not the same as no report at all — so `ui.a clean file shows no message bar at all` is asserting the
distinction rather than the absence of a backend.
The banner's text is the report's OWN summary, resolved from `RepairReport::summaryKey()` and its
arguments — never a sentence invented in `ui/`, and never one invented in `core/`, which holds no
user-facing English. The case checks the resolved text contains the document's name and does **not**
contain the raw key.
**The route to the detail, and the trade made there.** `RepairKind`'s names are declared ASCII, stable
and **non-localized** by their own header: they are diagnostic codes, they are what the log holds, and
translating thirteen xref-internals codes would put a different string in front of every bug reporter.
So the detail view localizes the FRAME — a title and a sentence saying what the list is — and prints the
codes as codes beside the object number and offset. Convention 5 is satisfied because nothing here is an
English literal in a source file: the codes come from `repairKindName()`, which is a function.
**A third case covers the half that is not about repair at all:** the banner is cleared on every tab
change, because the condition belongs to the tab and a *this was repaired* banner left over the document
that replaced it is a false statement about the file in front of the user — and Stage 3 forces a Save-As
on exactly that statement. What that case does NOT cover is a per-document report re-appearing on a tab
switch back, because the fake cannot answer differently per handle; that is the real backend's and is
manual case M-03's.
**Negative control run:** raising the banner for an empty report reddens two cases.
**If it fails** S2 — a repaired file that says nothing is how an original gets destroyed at Stage 3.

**A-59 · A registered-but-unavailable command says WHY — Stage 1's I-53, finally**
✅ **2026-09-04 (T14)** **Goal** D2-10. **Steps** With no document open, hover a **registered** command that is refused
(needs a document). Then hover a **declared-but-unregistered** one.
**Expect** Two *different* tooltips: *"not available for this document"* versus *"arrives in Stage N"*.
Stage 1 built only the second (`Stage1-AppShell-Plan.md` §5.1).
**Note the route:** Stage 1's M-12 found that **no ribbon control can show the first case** — all nine
registered ribbon ids are unconditionally enabled — so this case must reach it through the tool finder
or the toolbar.
**Result 2026-09-04, and the route is a third one the case did not anticipate.** T14 does not put the
twenty-five Viewer commands on the ribbon either, so the automated half reaches the distinction through
`ActionFactory::tooltipFor()` — the one place that decides — and through an action the factory built.
That is a back door and the suite names it: **what is NOT covered is a person hovering a real control
and reading the sentence**, which is manual case M-04's, and which is reachable now in a way it was not
at Stage 1 because opening and closing a document changes twenty-five commands' enablement while the
window is up.
Three answers are asserted and, crucially, asserted to DIFFER: *declared but not implemented* (about
this build), *not available for this document* (about this document), and the plain label when the
command is available. A fourth case covers the specific reason: **"not available for this document" is
true of Next Page on the last page and is not the useful thing to say**, so the two page steps carry
`shell.tooltip.atFirstPage` / `atLastPage` and the pair swaps over as the current page moves. And a
fifth asserts the tooltip is **re-asked** when a document opens — one computed at construction would be
a sentence about a state that has passed.
**One finding, `I-10`.** Stage 1's I-53 note said the mechanism already existed as
`Registration::tooltipKey`. It did not: that field is the tooltip an **enabled** control shows, and
reusing it for the refusal reason put *needs a document to be open* on every enabled Viewer button. The
two are shown in mutually exclusive states and say opposite things, so `unavailableKey` is a second
field.
**Negative control run:** collapsing the two kinds back into one — Stage 1's actual behaviour — reddens
three cases. **If it fails** S3.

**A-80 · Rotating the VIEW turns the layout, the tiles and their keys**
✅ **2026-09-04 (T14)** — **a case this guide did not list**, added with T14.
**Goal** `Ctrl+Shift+=` / `Ctrl+Shift+-` are in `Implementation-Plan.md` § Stage 2's own binding list,
and this guide was written before anyone noticed that **nothing in the render path could carry a view
rotation**: `RenderRequest` had no field for one, `TileGrid` no way to swap a page's bounds,
`PagePresentation` no parameter, and `PdfiumRasterizer` a hard-coded `rotate=0`. A command that needed
four layers extended deserves a case rather than a line in a status block.
**Read the distinction first:** the page's own `/Rotate` belongs to the DOCUMENT and survives a save;
the view rotation belongs to this SESSION and never reaches the file. Rotating pages destructively is
`page.rotate`, is Stage 3's, and stays unregistered — which the Viewer-subset case asserts.
**Steps** `ctest -R "rotat"`, plus `ui.rotating the view rotates the layout and re-keys the tiles`.
**Expect**, in four layers:

- **The arithmetic.** `rotateBy` is correct for NEGATIVE turns — C++'s `%` yields a negative remainder,
  so a bare `% 4` makes one press of Rotate Counter-Clockwise fall through to `None` and the command
  silently does nothing in one direction only. `combine` adds two rotations without producing 450
  degrees, and is commutative, which it must be: a landscape page in a rotated view and a rotated view
  over a landscape page are the same thing on screen.
- **The geometry.** `TileGrid::pageBounds` swaps at 90 and 270 and **not at 180** — a `!= None` test
  passes the first and fails the second — and `PagePresentation` swaps every page, so the content
  extent, the column width and the scrollbars all follow. Each `PageSlot::rotation` comes out as the
  COMBINED rotation, because `PageLayout::toDocument()` builds its `PageTransform` from it and a
  mapping using only `/Rotate` puts a click on a rotated view at the wrong point of the page — at
  Stage 5 that is text selection picking the wrong words.
- **The key.** `viewRotation` is part of `RenderRequest`'s equality **and its hash**, so two rotations
  of one tile are two cache entries. Without it the rotated view is served the upright pixels straight
  out of the cache: a command that appears not to work, with no error and no failing test (D2-12).
- **The engine.** `FPDF_RenderPageBitmap`'s `rotate` argument carries the VIEW rotation **and nothing
  else** — PDFium applies `/Rotate` itself, so passing the combined value would rotate a landscape page
  twice, which reads as *rotate view is broken for some documents and fine for others*, the worst shape
  of rendering bug because it depends on the file.

**Result 2026-09-04.** Green in all four. **Negative control run, and one of the three refused to
fire:** removing the `rotate` argument reddens the engine case, and dropping `viewRotation` from
`RenderRequest`'s equality reddens the cache case — but making `TilePresenter` stop putting the rotation
in its requests changed nothing, because a rotated page has a different shape and therefore different
tile rectangles, so new tiles were requested either way and the case was counting requests rather than
reading keys. `TilePresenter::heldRequests()` exists because of that, and the case now asserts every
held key carries the rotation.
**If it fails** S2 — and the first question is which of the four layers, because three of them fail
invisibly.

### 5.10 Platform

**A-60 · `WinDisplayTopology` sees the real rig — and this is what makes the rig trustworthy**
✅ **2026-09-04 (T12)** **Steps** `ctest -R platform.displaytopology` on the P-1 machine.
**Expect** **Two** monitors: one 3840×2160 at **150 %**, one 2560×1440 at **100 %**. Physical pixels
and scale factor reported separately.
**Result 2026-09-04 — measured, and it AGREES with §2's rig table for the first time through the
product's own query:**

| id | pixels | scale | physical DPI |
|---|---|---|---|
| `\\.\DISPLAY1` | 3840×2160 at (0,0) | **150 %** | **163** |
| `\\.\DISPLAY2` | 2560×1440 at (3840,353) | **100 %** | **108** |

**The physical DPI column is the point of the whole task.** `96 * scale` says 144 and 96; the glass says
163 and 108. Every caller of `DpiPolicy` passed the nominal figure before T12, which is a 13 % error on
the 4K panel — and `DpiPolicy` takes three inputs precisely because *Actual Size means 100 % at the
physical DPI*.
**The precondition is asserted rather than assumed**, which is what this case's own warning demanded:
`processIsPerMonitorAware()` is a `REQUIRE` at the top, and `tests/platform/main.cpp` sets per-monitor-v2
awareness before `QCoreApplication` exists — without it Windows would have handed this suite 2560×1440 at
100 % for the 4K panel, self-consistently and wrongly.
**The rig half SKIPS BY NAME on any other machine**, printing what it did see, so a one-monitor CI runner
reports *not applicable* rather than passing vacuously; the structural half (every monitor has an id,
positive pixels, a plausible scale and a believable physical DPI, and exactly one is primary) runs
everywhere. **A raw DPI outside 48…1200 is not believed** and falls back to the effective one — EDID is
user-supplied data in the sense that matters, and a physical DPI of 4 propagates into `DpiPolicy` as a
rasterization resolution.
**One finding, I-07:** `platform/platform.h` documented `MonitorInfo`'s geometry as *device-independent
pixels* and it is physical pixels — the DIP reading is what a DPI-UNAWARE process is handed. Corrected in
the same change. **If it fails** S2 — **and treat a disagreement with §2's rig table as a finding about
the case, not about the rig, until proven otherwise**.

**WHAT A-60 DOES NOT COVER, added 2026-09-06 after M-03 found it (I-20)** and named at the
assertion rather than left to be inferred (convention 19): this case reads the topology **with no
`QScreen` beside it**. It proves the platform layer measures the glass correctly — 163 and 108 —
and says nothing about whether the product can find the row describing the monitor a widget is
actually on. That join lived in `ui/viewer/`, matched two identifiers that can never be equal, and
**failed on every real desktop for the whole of Stage 2** while this case stayed green. **A-84 and
`--print-screen-metrics` are the missing half.**

**A-84 · The resolved physical DPI does not move with the OS scale factor** ✅ **2026-09-06**
**Goal** **I-20.** *Actual Size means 100 % at the physical DPI* and the scale factor must not enter
it (`core/render/DpiPolicy`'s header). The defect collapsed the two into one, so the assertion is the
INVARIANT rather than a number: vary only the scale factor, and the dots per inch of glass must not
budge.
**Steps** `ctest -R screen_metrics_invariant`. Two processes — a scale factor is read when the
`QGuiApplication` is built and cannot be varied inside one — differing only in `QT_SCALE_FACTOR`,
compared on `--print-screen-metrics`' resolved `physDpi`.
**Expect** The two agree within a whole percent. **And the control, which runs first**: the second
process must actually report a different device pixel ratio, or the case passed by failing to vary
its own input — Stage 1 T3's two useless probes, and convention 7.
**Result 2026-09-06** Green. **Run against the unfixed build first, which is what makes it
evidence**: *"the resolved physical DPI moved with the scale factor: 100 dpi at dpr 1, 66 dpi at
dpr 1.5 — 51 % apart"*.
**What it does NOT cover:** it exercises the **fallback**, because offscreen has no topology to join
to — so it catches fault (2) of I-20 and not fault (1). **Nothing headless can catch fault (1)**;
that is what `--print-screen-metrics` is for, and why the join is checked by hand on the rig the way
A-60 checks the rig itself. Recorded so the next reader subtracts it rather than adding it.
**If it fails** S2 — every zoom in the product is computed from this number.

**A-61 · The `changed` signal fires on a mid-session scale change** ✅ **automated half
2026-09-04 (T12); the real scale change WALKED AS M-02 ON 2026-09-06 — this case's missing half is
no longer missing**
**Expect** Changing a monitor's scale in Windows Settings emits `changed` once, with the new topology.
**Result 2026-09-04, and what is and is not claimed.** No automated run can change a monitor's scale
factor, so **the half that says "emits `changed` once on a real scale change" is M-02's and is not
claimed here.** What is claimed is the two halves that make that observation meaningful and that no
manual pass can check precisely:

1. **A notification carrying no actual change is SUPPRESSED.** `WM_DISPLAYCHANGE` and `WM_SETTINGCHANGE`
   are sent to the object's real `HWND` — `SendMessageW` runs the shipped `WNDPROC` synchronously, so the
   only synthetic part is where the message came from — and `changed` does not fire. That is what makes
   A-61's *"once"* achievable at all: one scale change in the Settings app produces several of these
   messages, and `WM_SETTINGCHANGE` is broadcast for a great many things that are not displays. The case
   asserts the **suppression counter moved by exactly two**, which is the control proving the messages
   arrived rather than the window never having existed (convention 7).
2. **`sameTopology()` — the predicate the suppression is built on — answers false for every field a real
   change moves**, the scale factor first among them: a monitor whose pixels and position did not move but
   whose scale did is precisely M-02's gesture, and an implementation comparing only geometry would
   answer *same* and never fire however correct the message plumbing was.

**Also asserted:** `subscribe()` ADDS rather than replaces. The interface's signature — one handler, no
token — invites the other implementation, and Stage 4's session-state consumer would then silently unhook
the render policy.
**`subscribe()` has no product caller yet, and that is a decision rather than an oversight.**
`ui/viewer/PageView` re-reads the topology on Qt's own `ScreenChangeInternal` /
`DevicePixelRatioChange`, because a widget is told by Qt when its own monitor moves, and
`IDisplayTopology` has **no `unsubscribe`** — so a per-window subscription would dangle when a torn-off
window is closed (convention 10). The subscription's real consumer is something that cares about the whole
desktop: `core/settings/SessionState` at Stage 4.
**THE DEFERRED HALF, SUPPLIED 2026-09-06.** M-02 was walked and signed off — all four steps, the real
scale change included — so the sentence above that reads *"is M-02's and is not claimed here"* now has
its other end. **The 🟡 is cleared by a MANUAL result, which is the only kind that could clear it**, and
this case is left saying exactly what each half proves rather than merging them: T12 proves the
suppression and the predicate, M-02 proves a person changing a scale in the Settings app sees the page
and the chrome follow together. **If it fails** S2.

**A-62 · `WinPowerSession` reports battery and throttles** 🟡 **the interface 2026-09-04 (T12);
the throttle is T15's**
**Expect** Battery state readable; with `performance/throttleOnBattery` set and the machine on battery,
the effective worker count drops. On a desktop, the case reports *not applicable* **explicitly** rather
than passing vacuously.
**Result 2026-09-04.** The P-1 machine is a desktop with **no battery**, and the case says so and
**SKIPS** — `platform.powersession reports the machine's power source` is the one skipped test in the
whole run, which is this criterion's own wording honoured rather than a green tick over an untested
policy. It still asserts what a desktop CAN answer: `hasBattery()` is false and `isOnBattery()` is false
with it, because *no battery* is not *running on battery*, and an implementation reading `ACLineStatus`
alone would report a UPS-backed desktop as unplugged during a blip.
The event path is driven for real through the object's own `HWND`: suspend, resume, AC, DC, **a UPS
(source 2), which reports as MAINS** — throttling the renderer for the length of a power blip is a visible
stutter bought for nothing — and session lock and unlock, seven events in order. Its negative control is
its own case: a power-setting broadcast for a GUID this object did not register for, a truncated payload
and a null payload each deliver nothing, and then the same message correctly addressed delivers one.
Presentation mode is asserted idempotent and released on destruction.
**THE THROTTLE ITSELF IS NOT CLAIMED AND IS T15's.** This criterion's second clause names
`performance/throttleOnBattery`, a settings key that does not exist until T15, and
`RenderScheduler::setActiveWorkerLimit()` clamps to `[1, workerCount]` where `workerCount` is **1** — so
wiring it now would build a code path no configuration could make behave differently. The interface the
wire is made of is what T12 owes, and it is here. **If it fails** S3.

### 5.11 Settings, fonts, glyphs

**A-63 · The sixteen keys round-trip** ✅ **2026-09-04 (T15)**
**Expect** Each `SettingsKeys.def` row gets/sets its declared type; defaults match §2's table; an
undeclared key still does not compile (D1-10, and the negative-compile fixture still passes).
**Result 2026-09-04.** `tests/unit/tst_stage2settings.cpp`, six cases, and it is a file of its own
rather than sixteen more in `tst_settings.cpp` because those two test different things:
`tst_settings` tests the MECHANISM — typed get and set, clamping, migration, the codecs — and every
one of its cases would pass over an empty table. This one tests the TABLE.
Every value written in the round-trip case is DIFFERENT from its default, so a `get` that silently
ignored the file would read the default back and fail — which is the failure A-63 exists for.
**Three things beyond the round trip:**
- **The four defaults that changed owner are asserted to still agree with the structs they came
  from** — `DpiPolicy::Limits::maxRenderDpi`, `RenderCache::Config::memoryCapBytes` and
  `diskCapBytes`, `RenderScheduler::Config::workerCount`. Each of those had said in its own comment
  that the row would take over; this is what makes that a check rather than a promise.
- **The two enums are stored as their own NAMES, and the case reads the INI's bytes.** An
  enumerator's value changes the day somebody inserts a mode in the middle, which would silently
  reinterpret every saved preference — `SettingsKeys.def` rule 1 is that the file is read back by
  later versions.
- **A nonsense value falls back rather than being invented.** `fitModeFromName` answers `nullopt`
  for a name it does not know precisely so a typo cannot silently mean *actual size*, which is a
  real value and therefore indistinguishable from a deliberate choice. And the ranged rows CLAMP
  rather than refuse (rule 3): a hand-edited INI must not prevent startup.
**Negative control run:** writing the enums as integers reddens two cases. **If it fails** S2.

**A-64 · Two new Preferences groups are generated, not hand-laid-out** ✅ **2026-09-04 (T15)**
**Goal** Convention 17.
**Expect** Page Display and Performance appear with one editor per key, built by `SettingsPage`
`#include`ing the `.def`. **Five of 26 groups now ship.** No widget code was added per key.
**Result 2026-09-04, and the diff is the evidence:** two entries in
`PreferencesDialog::shippedPages()` and sixteen rows in `SettingsKeys.def`. Not one line of widget
code was written for any of the sixteen.
The case counts editors **from the table** rather than against a literal — a case asserting "11"
would be a second copy of the row count and would go stale the day a twelfth `pageDisplay/` key is
declared, which is convention 17's own argument turned on the test. It also asserts the two enum
rows became COMBO BOXES and the ranged rows became SPIN BOXES: that follows from
`SettingsEditorTraits<T>`, which is a property of the TYPE, and a page built from runtime
descriptors could not have known it.
**If it fails** S2 — a hand-built page means the mechanism has been bypassed and every later stage pays.

**A-65 · Rendering flags visibly change the render** 🟡 **2026-09-04 (T15) — three of the five
measured green, and the criterion is CORRECTED for the other two rather than narrowed**
**Expect** Toggling `antialiasText`, `antialiasImages`, `antialiasLineArt`, `lineWeight` and
`transparencyGrid` each produce a **measurably different** raster of the same page — asserted against
golden variants, not by eye.
**Result 2026-09-04. The criterion turned out to be two different claims wearing one sentence**, and
correcting it is the same call T7 made about A-18, when *encode then decode returns the input* turned
out not to be a property JPEG has:

| toggle | changes the raster? | measured |
|---|---|---|
| `antialiasText` | yes | **51,279 pixels** on `ordinary/text-a4.pdf` |
| `antialiasLineArt` | yes | **9,048 pixels** on `ordinary/vector-hairlines.pdf` |
| `antialiasImages` | yes | **23,928 pixels**, swept across every image fixture at three scales |
| `transparencyGrid` | **yes — and finding that out is the story below** | the raster changes format and the page comes back mostly transparent |
| `lineWeight` | **no, and it cannot** | PDFium has no such control — issue **I-11** |

**Golden variants are T17's**, so the comparison is between the two rasters directly rather than
against a baseline — which is available now and is a stronger claim than a baseline would be, since
it cannot be satisfied by a baseline that was captured wrong.
**The image measurement is the one to read.** Two straightforward ways of writing it measured ZERO
and would have been reported as a finding about PDFium: `image-dct-rgb.pdf` at scale 1.0 over a
400×400 tile at the page origin (the tile contains no part of that file's image), and the same file
over the whole page at 1.0 and at 4.0. Image smoothing only bites where a source pixel is resampled
hard, and **which fixture at which scale does that is a property of the corpus rather than of the
flag** — so the case sweeps every image fixture at three scales rather than picking one and
asserting a floor. A case that had picked one and asserted zero would have declared a working flag
broken.
**AND `transparencyGrid` WAS WRONG WHEN THIS RESULT WAS FIRST WRITTEN.** It was implemented as a
viewer-side paint — a checkerboard drawn under the page, no render flag, and the note here said it
"changes no pixel of any raster, and correctly". Writing the case that asserts the grid APPEARS is
what showed otherwise: the rasterizer fills its bitmap with white before drawing, so a transparent
region of the page comes back opaque white and covers the checkerboard completely. **A feature that
reads back as enabled and displays nothing is Stage 1's I-38 exactly**, and it was one edit away
from shipping as one.
So `RenderFlags::TransparentBackground` exists; `PdfiumRasterizer` switches the bitmap to `BGRA` and
fills with `0x00000000`; and the flag is part of the cache key, which is also right — a page
rendered onto transparency is genuinely different pixels. Measured over the real engine: a tile of
`ordinary/text-a4.pdf` comes back **more than half fully transparent**, which is the region the grid
is meant to show through. The viewer half is asserted in `tests/ui`, where the painted result
differs and every held request carries the flag.
**If it fails** S2: a setting that reads back and changes nothing is Stage 1's I-38 repeating, and
that one was only caught by a person.

**A-66 · A missing embedded font substitutes through OUR policy** ✅ **2026-09-05 (T16)**
**Expect** A corpus file with a missing embedded font renders, and the case **names the substitute
chosen**, proving `FontResolverBridge` → `platform::IFontResolver` ran rather than PDFium's internal
default. **If it fails** S2 — substitution policy on the wrong side of the boundary is invisible until
a document looks wrong on a customer's machine.

**Result 2026-09-05, and the substitutes are named:** `corpus/ordinary/font-not-embedded.pdf` —
**a thirtieth `ordinary/` file, added by this task** — resolves `RawPageAbsentSerif` to **Times New
Roman** and `RawPageAbsentSans` to **Segoe UI**. Four things are worth reading before the next task
touches this path.

- **NONE OF THE OTHER 29 CORPUS FILES COULD SERVE THIS CASE**, and finding that out is what added the
  thirtieth. Every text fixture in the corpus uses either a base-14 face — which every conforming
  reader is obliged to have — or an embedded `/FontFile2`, where the outlines are in the file. A font
  a document NAMES and does not embed is a third thing and the only one that reaches a resolver. The
  new file carries two of them, differing in exactly one `/Flags` bit, so *the descriptor was read* is
  a check rather than a hope: a resolver that ignored it answers the same family twice.
- **THE FIXTURE'S `/Widths` HAD TO BE NON-UNIFORM, and the first version was not.** With every width
  equal, PDFium concludes the font is monospaced and sets `FXFONT_FF_FIXEDPITCH` in the `pitch_family`
  it hands the hook — so both fonts resolved down the fixed-pitch branch and both came back as
  Consolas, and the serif bit became unobservable. The engine is right to infer it; the fixture was
  wrong to invite it.
- **THE CHAIN IS FOUR FILES IN THREE LIBRARIES, and each layer's own suite owns one link.**
  `tests/unit/tst_fontsubstitution.cpp` asserts the POLICY with no display, no Windows and no engine —
  which is the whole reason `core/render/FontSubstitution` exists rather than a table inside
  `WinFontResolver`. `tests/platform/tst_fontresolver.cpp` asks the machine. `tests/render/
  tst_fontbridge.cpp` drives all of it through PDFium, and `rawpage_render_tests` gained
  `rawpage_platform` for it — **issue I-09's shape a second time**, and answered the same way.
- **THE CASE THAT MATTERED MOST IS NOT THIS ONE.** A-66 asks whether the bridge RUNS. The control
  asking whether it runs when it should NOT is what found **I-13**, and nothing in A-66's wording
  called for it (convention 7). **Re-verified by planting it back**: making `isStandardFontName()`
  answer false reddens three cases — two in `tests/unit` and the base-14 control in `tests/render` —
  so the guard against I-13 recurring is a check rather than a comment.

**A-67 · `GlyphCache` hits across tiles and pages** 🟡 **2026-09-05 (T16) — the measurable half is
green and the criterion is CORRECTED for the other half rather than narrowed**
**Expect** Rendering two adjacent tiles of one text page rasterizes each distinct glyph once.
**If it fails** S3.

**Result 2026-09-05. The criterion names something on the far side of the engine boundary**, which is
the same call T7 made about A-18 and T15 made about A-65. PDFium rasterizes glyphs and PDFium caches
them, in the process-global state D2-9 confines to `PdfiumInit`; nothing in `pdf/render/` ever sees a
glyph bitmap, so a `GlyphCache` that claimed to count them would be reporting a number it invented.

What this layer owns is the step before, and both halves of it are measured:

| claim | measured |
|---|---|
| A second tile of the same page **maps no font** | `mapFontCalls()` is unchanged across the second tile — PDFium resolves a font when it LOADS it, not when it draws |
| A second tile of the same page **reads no face from disk** | `GlyphCache` misses are unchanged |
| A second DOCUMENT naming the same missing font **re-maps** it | it does, and the case asserts the RISE — font mapping is per document, and a case that hoped otherwise would be asserting a bug |
| …and still **reads no face from disk** | the cache's misses are unchanged and its hits rise. For a CJK collection that is 18 MB of file I/O per document that does not happen on the render thread |

**The name `GlyphCache` is the plan's and is kept**, with its header saying in its first paragraph
what it is not — renaming a file the plan's §4 manifest lists would trade a misleading name for a
missing row.

**Two negative controls were planted and run**, and the second answered a question the cases could
not answer by inspection. Disabling `GlyphCache`'s hit path reddens the **document** case and
**not** the second-tile one — because a second tile performs no font mapping and therefore no
`GetFontData` either, so the cache is never asked. That is correct, and it means the second-tile
case's cache assertion is *implied by* its mapping assertion rather than independent of it. Recorded
at the case rather than left for the next reader to add up (convention 7 and 19).

**A-81 · NO PDFium flag can draw a hairline at its true width — the measurement behind I-11**
✅ **2026-09-04 (T15)** — **a case this guide did not list**, added with T15.
**Goal** `Features.md` §1.2 defines Line Weight as *"draw hairlines at their true width instead of a
minimum of one pixel"*, and `pageDisplay/lineWeight` is in `Implementation-Plan.md`'s Stage 2 key
list. **PDFium always enforces the one-pixel minimum and has no `FPDF_*` control over it**, so the
setting's ON state is a thing this engine cannot be asked for — and D3 says the `FPDF_*` C API is
the only way we reach it.
**This is a case rather than a paragraph because "I read the header" is weaker evidence than "I
tried them"**, which is the same standard T4 applied to the corpus and T3 to the A2 probe.
**Steps** `ctest -R "NO PDFium flag"`.
**Expect** Over `corpus/ordinary/vector-hairlines.pdf`, every flag `core::render::RenderFlags` can
express is applied in turn and the **solid ink coverage** does not fall.
**The threshold is the point.** Counting every non-white pixel counts the anti-aliasing FRINGE, and
`NoSmoothPath` reduces that by a third — 12,598 to 8,528 — which looks like a thinner line and is
not one. Counting only pixels darker than `0x80` counts the line's CORE, and that moves from 7,013
to 6,926: **1.2 %**. The other two flags move it by nothing at all. A flag that drew hairlines at
their true width would drive it toward zero, because a sub-pixel line has no fully dark pixel.
**Result 2026-09-04: green, and the finding stands.** `pageDisplay/lineWeight` is declared and
inert — issue **I-11**, the same shape as **I-06**, where `/JBIG2Decode` is declared and decodes
nothing because the only usable library is AGPL-3.0. `ViewSettings::lineWeight` carries the value so
that the day an engine can honour it there is a value to honour, and `tests/ui` asserts that it
round-trips, reaches the view and changes no render flag.
**If it fails** S3 — and a failure here would be GOOD NEWS: it would mean a flag does thin a line,
and I-11 should be closed rather than the case fixed.

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

**WALKABLE AS OF 2026-09-06 (T17): NINE OF THE TEN — M-01 … M-08 and M-10.** **WALKED AND SIGNED OFF AS OF 2026-09-12: FOUR — M-01, M-02, M-03 and M-04.** There is a page on
screen — open one with `.\build\x64-release\bin\rawpage.exe corpus\ordinary\text-a4.pdf`, or any
other corpus file — the §8 Viewer chords move it, the status bar counts it, the four layout modes
and the zoom presets are reachable, a repaired file raises a banner, an encrypted one prompts, and a
missing font substitutes through our own policy.

**This block said something different until T17 re-read it, and the correction is worth keeping.**
From T13 to T16 it read *“WALKABLE AS OF 2026-09-04 (T13): M-01, M-02, M-03 and M-04”*, followed by
two “does not exist yet” notes about the status bar and the anti-aliasing toggles and the line
*“M-05 … M-10 remain unwalkable: they need golden baselines (T17), fonts (T16), the settings keys
(T15) or Dr. Memory.”* Every clause of that was true when it was written and three of the four
blockers were cleared within two days — and **none of the six cases was ever about a golden
baseline at all**. What they were actually waiting for:

| Case | Was waiting for | Shipped |
|---|---|---|
| M-05 — the four layout modes, cover page, RTL | `pageDisplay/layoutMode` and `Ctrl+Shift+H`/`Ctrl+Shift+T` | T14, T15 |
| M-06 — zoom extremes | the zoom box, the four fits, `maxRenderDpi` as a settings row | T14, T15 |
| M-07 — open every corpus file | documents that open, and a message bar that reports the repair | T9, T14 |
| M-08 — an encrypted document | the password prompt | T14 |
| M-10 — the Viewer keyboard by hand | the 23 §8 Viewer chords | T14 |
| M-09 — Dr. Memory over the raster path | **nothing this project builds.** Still blocked |

The lesson is the one Stage 1 kept re-learning and CLAUDE.md's T16 note states outright: **a
deferral's note about what will unblock it is a guess by whoever had not yet built the thing**, and
a stale one reads as a plan for four tasks because nobody re-derives a sentence that already looks
settled. It was found by asking what T17 had actually unblocked, and finding the honest answer was
*nothing, and also five other tasks had*.

Two things about the state you will be walking, so a planned absence is not reported as a defect:

- **Seven §8 Viewer chords deliberately do nothing** — `Ctrl+4`, `Ctrl+R`, `S`, `Ctrl+B`, `Ctrl+E`,
  `Ctrl+L`, `Ctrl+U` — and they leave the key **unconsumed**, which is T14's decision and is walked
  by a case rather than described by a comment. They belong to Stages 3 to 13.
- **Line Weight is a checkbox that does nothing**, and that is issue **I-11** rather than a defect:
  PDFium always draws a hairline at a minimum of one pixel and exposes no flag over it. The other
  four `pageDisplay/` toggles M-03 asks you to flip do change pixels.

**M-09 remains unwalkable** — it needs Dr. Memory or Application Verifier, which is not on this
machine and is the whole reason that case is manual.

---

**M-01 · Mixed DPI — dragging between the 150 % and 100 % monitors** ✅ **WALKED AND SIGNED OFF BY THE USER 2026-09-06**, on the second walk, against the build carrying I-19's fix
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
- **FIRST WALK 2026-09-06 — FAILED, and NEITHER direction above is what it found.** The user's words:
  *“When I drag the window between 100% and 150% monitor with Ctrl + 1/2/M, the text in PDF will
  disappear. Sometime can make it visible again with mouse scrolling, sometimes cannot, which can only
  drag window to another window to make text visible.”* The page did not go blurry or stutter — it went
  **blank**. Diagnosed as **I-19** and fixed the same day: the defect is in `RenderScheduler`'s pump and
  `DpiPolicy` is innocent, so the *If it fails* text above led away from the mechanism rather than to it.
  **Kept as written even so** — it correctly names the two failures this case was designed to catch, and
  a third one existing is not an argument for deleting the first two. **The case is NOT walked**: it is
  re-walked from step 1, by the user, against the fixed build.
- **SECOND WALK 2026-09-06 — PASSED, and signed off by the user.** All four steps against the fixed
  build: Actual Size on the 100 % monitor, both drag directions twice, Fit Width, 400 %, and the window
  left straddling the boundary. **The first Stage 2 manual case to be signed off, and it took a product
  fix to get there** — which is the whole argument for the manual half stated as a fact rather than as a
  principle. **What the sign-off covers is this rig**: the P-1 pair, 150 % and 100 %, asserted by A-60 in
  the same session. It says nothing about a third scale factor, a 200 % panel, or a laptop's internal
  display, none of which exist here to be walked.

**M-02 · A scale change while the application is running** ✅ **WALKED AND SIGNED OFF BY THE USER 2026-09-06**, all four steps
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
- **WALKED 2026-09-06 — PASSED, all four steps, signed off by the user.** Reported as *“It looks
  fine.”* **Step 4 was walked** — logged out, back in at a different primary scale, relaunched and
  reopened — which is the one step of this case that is a COLD START rather than a live change, and
  the only place the settings file and the topology are read fresh against a scale nobody has seen.
  **Walked immediately after M-01 on the same window**, which is deliberate: the two share a rig and
  M-01's fixed build is what this one is judging.
  **What the sign-off covers and what it does not.** It covers the four scale factors this rig can be
  put into and the transitions between them; it does **not** cover a monitor being hot-plugged or
  removed mid-session, which no step here performs and which `IDisplayTopology` reports through the
  same `changed` signal — that is a Stage 4 concern with no case in this guide, named here rather
  than left to be discovered as a gap. **The chrome-and-page agreement is the part that was actually
  at risk** and it is the part reported fine: A-61's automated half proves only that the signal fired.

**M-03 · Rasterization fidelity, beside a reference** ✅ **WALKED AND SIGNED OFF BY THE USER 2026-09-06**, on the third walk, after two product fixes
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
- **FIRST WALK 2026-09-06 — ABANDONED AT THE FIRST FILE, and the reason was worth more than the
  case.** Before any fidelity judgement could be made, `text-a4.pdf` at `Ctrl+1` beside Acrobat was
  **visibly smaller** — measured off the two screenshots at ~469 px against ~712 px for the same A4
  page on the same monitor, a ratio of **1.52**. That is **I-20**: the page was rasterized at
  108.89 dpi where the glass measures 163, so *Actual Size* was wrong by the scale factor on every
  scaled monitor. Fixed the same day.
  **This is why the guide puts M-01 and M-02 first and it is not why they are first** — the ordering
  note says a DPI bug makes every later judgement unreliable, and it was right for a reason it did
  not anticipate: M-01 and M-02 both PASSED, because both judge crispness and continuity, and the
  render was crisp and continuous at the wrong size. **A size error is invisible to a case that
  compares the product with itself**, and this one was found in the first minute of the first case
  that put it beside something else. **The case is NOT walked**: it is walked from the start against
  the fixed build.
- **SECOND WALK 2026-09-06 — one finding, `forms-acroform.pdf`: the form rendered as its labels and
  eleven blank spaces.** That is **I-21**, fixed the same day: PDFium draws every annotation except
  `/Subtype /Widget` from `FPDF_RenderPageBitmap`, and the facade never created the form-fill
  environment a widget's `/AP` needs. **The corpus contained the control the whole time** —
  `annots-mixed.pdf`'s nine non-widget annotations rendered correctly under the identical flag —
  and nothing compared the two files until a person did.
- **THIRD WALK 2026-09-06 — PASSED, and signed off by the user.** All six files against Acrobat at
  **100 % and 400 %**, with the `antialiasText` and `antialiasLineArt` toggles exercised. No
  systematic difference in glyph weight, spacing or colour; hairlines visible at 100 %; the two
  live toggles do what they say.
  **RECORDED IN THE WORDING THIS CASE REQUIRES: *consistent with the reference over the generated
  corpus*, NOT *rendering is correct*.** The corpus is entirely ours (§1, D2-11), so this compared
  two renderers over files our own generator emitted. It says nothing about a construct the
  generator never produces — a scanner's CCITT quirks, InDesign transparency, an unusual colour
  space — and a clean M-03 is **not** evidence that real-world PDFs render correctly. The trigger
  for revisiting that is D2-11's own *Revisit* column: the first real-world rendering bug the
  corpus cannot reproduce.
  **What the pass does NOT cover, beyond the corpus bound.** `pageDisplay/lineWeight` was toggled
  and does nothing — **I-11**, open by decision, not a finding of this walk. The CJK file's glyphs
  are sixteen rectilinear shapes by construction, so *correct* there means the rectangles agree,
  not that curves, hinting or kerning were judged. And **no form field is interactive**, which is
  Stage 9's and is now itemized in §1's out-of-scope table because this walk raised it.
  **THE CASE COST TWO PRODUCT FIXES TO REACH A PASS** — **I-20** at its first file and **I-21** at
  its sixth — which is the argument for an independent reference renderer stated as a fact rather
  than as a principle: both defects were invisible to a green suite, and both were found within
  minutes of putting the product beside something that was not itself.


**M-04 · Scrolling a 900-page document — the perceived-smoothness half of A2** ✅ **WALKED AND SIGNED OFF BY THE USER 2026-09-12**, all five steps, at all three worker counts, after two product fixes
- **Goal** **The case A-68's number cannot replace.** A p95 under budget and a scroll that stutters is
  a finding about the scheduler, and only a person can tell the difference. It is also the only test in
  this guide that drives cancellation through the gesture that is supposed to trigger it — A-36 drives
  the scheduler directly and says so.
- **Pre** The 900-page corpus file, `corpus/ordinary/pages-900.pdf`. **Set `performance/renderWorkers`
  to 8 and relaunch** — the shipped default is **1** (**I-05**'s answer, §8), and the key takes effect
  only at the next launch (§7).
- **Steps**
  1. Scroll continuously with the wheel from page 1 to page 200 without pausing. Watch for blank tiles
     that persist, tiles that arrive out of order, and a scroll that stops responding to the wheel.
  2. Drag the scrollbar thumb from top to bottom in one fast motion, then release.
  3. `End`, then `Home` — **the bare keys**. [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference)
     binds those to first/last page; `Ctrl+End` and `Ctrl+Home` are bound to nothing and will do
     nothing.
  4. Scroll fast, then **immediately** zoom. Then scroll fast and immediately switch layout mode.
  5. Repeat step 1 with `renderWorkers` at 1, then at 16, **relaunching after each change** (§7).
- **Expect** Tiles fill in behind the scroll without the window becoming unresponsive; blank regions
  resolve within a moment and do not persist; step 2 does **not** rasterize the 700 pages it passed
  over — cancellation is what step 2 is for; step 4 does not show a tile at the old zoom or the old
  layout. At 1 worker it is slow but **correct**.
- **If it fails** S2 against A2, and say **which** step. Step 2 failing is cancellation. Step 4 failing
  is `Revision`/`Scope` invalidation and is S1 — that is a stale tile, and a stale tile is the product
  telling the user something untrue about their document.
- **WALKED 2026-09-12 — PASSED, all five steps, signed off by the user.** Step 5 was walked **in
  full** — `renderWorkers` at 8, then 1, then 16, relaunching between each, since the key takes
  effect only at the next launch. At 1 worker it was slow and **correct**, which is the half of that
  step that matters: the shipped default is 1 (**I-05**), so the one-worker walk is the walk of what
  the product actually ships and the other two are the sweep around it.
  **A2 NOW HAS ITS THIRD ANSWER, and it is the one the other two cannot give.** The probe (T3, 0.532
  ms of engine service time) and the product figure (**A-68**, 14.18 ms p95 of queued latency against
  a 16.667 ms budget) are both numbers; this is a person reporting that the scroll does not stutter.
  A p95 under budget with a scroll that judders would have been a finding about the **scheduler**,
  and that is the reading this case exists to make possible. It did not happen.
  **THE CASE COST TWO PRODUCT FIXES TO REACH A PASS**, both found by scrolling rather than by
  measuring, and both in the shell rather than in the engine: **I-23** (`Ctrl+Shift+T` could enter
  two-up and never leave) and **I-24** (a paged mode trapped the reader on the page — nothing that
  scrolled could cross a page boundary). Neither is about tiles, latency or PDFium. **A2's verdict is
  untouched by both**, which is worth stating so the pass is not read as broader than it is: this
  case's contribution to A2 is *the scroll feels smooth*, and what it found on the way was that two
  of the gestures around the scroll did not work at all.
  **What this pass does NOT cover.** It is one rig — 16 cores, the P-1 monitor pair — and one
  document, the generated 900-page file, **whose pages are deliberately light** —
  `corpus/make-ordinary.py`'s `pages_900` gives each page a heading plus one of **ten shared**
  Helvetica text blocks and no image, no shading and no transparency, because the file exists to
  test the page TREE rather than the raster. So nothing here says what scrolling costs on 900 pages
  of scanned CCITT or heavy transparency, and the corpus contains no such document to say it with. **Perceived smoothness on a slower machine is not measured by
  anything in this guide**, and the benchmarks report a figure rather than judging the machine. And
  step 2 was judged behaviourally — responsiveness returning, no long CPU tail — because
  `RenderScheduler::Stats::cancelledBeforeStart` is surfaced nowhere in the product; **A-36** is what
  proves cancellation mechanically, and this step proves the gesture reaches it.

**M-05 · The four layout modes, the cover page, and RTL binding** ☐
- **Pre** The odd-page-count corpus file with a cover page, and an RTL document. **P-4 for comparison —
  Acrobat Reader, not Edge or Chrome.** Layout mode is UX rather than rasterization, so a PDFium-based
  viewer would not be circular here the way it is in M-03 — but its *pairing* behaviour is Chromium's
  rather than a PDF reader's, which makes it the wrong reference for a different reason.
- **Steps** For each of Single, Single Continuous, Two-Up, Two-Up Continuous: page through the whole
  document, use `Ctrl+Shift+H` and `Ctrl+Shift+T` **twice each** (I-23 — one press cannot see a
  toggle that does not toggle), and check the first and last spread. Then toggle
  `showCoverInTwoUp`, then `rtlBinding`, then both.
  Then, **in the two PAGED modes only**, leave the page by SCROLLING rather than by a command: a
  real wheel flick, a trackpad if you have one, and `Down`/`Up` held at the page's edge. **This is
  here rather than in §5 because a synthetic wheel event has no momentum and no high-resolution
  delta** — `tests/ui/tst_pageview.cpp` presses twice at the edge and proves the crossing happens,
  and it structurally cannot tell you what one flick of a real wheel does.
- **Expect** The pairing matches the reference. With a cover page, page 1 stands alone and 2–3 pair.
  With RTL, page 2 is on the **right**. Switching modes preserves the current page rather than jumping
  to page 1. Scrolling past a page's edge **turns the page** and lands at the near edge of the new one
  — its top going forward, its bottom coming back (I-24) — and **one flick should cross one
  boundary, not several**; at the first and last page the gesture does nothing rather than wrapping.
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
| Most ribbon buttons are still disabled | Correct. Registration went from **23 to 48 of 361** at T14, and **none of the 25 new ones is on the ribbon** — they are chords and a status-bar box. The ribbon's own share grows at Stages 3 to 13 |
| `Ctrl+S`, `Ctrl+Z`, `Ctrl+Y` do nothing and **do not consume the key** | Correct — `NotFound` deliberately leaves the key unconsumed. Stage 3 |
| A repaired file opens rather than refusing | Correct, and the point of `XrefRepair`. Stage 3 forces its first save to Save-As |
| The COS reader **declines** an encrypted file while the viewer **opens** it | Correct — D2-6. Two different components with two different jobs |
| No text can be selected, and there is no text cursor | Stage 5 |
| The sidebar has no thumbnails or bookmarks pane | Stage 4 |
| `WinAcceleratedSurface` does nothing | Correct — declared, unimplemented, and the interface census (A-46) says so |
| Zoom at 6400 % is not sharper than at 1600 % | Likely correct — `pageDisplay/maxRenderResolution` clamps it. Check the setting before reporting |
| ~~The status bar's page total is still an em dash~~ | **Fixed at T14** — real numbers, and the dash now means *there is no document*. A dash WITH a document open is a finding |
| ~~Every §8 Viewer chord does nothing~~ | **Fixed at T14** for the 23 chords Stage 2 owns. The other seven — `Ctrl+4`, `Ctrl+R`, `S`, `Ctrl+B`, `Ctrl+E`, `Ctrl+L`, `Ctrl+U` — are still `NotFound` and still leave the key unconsumed, which is correct and is asserted |
| **`H`, `V` and `Z` arm a tool but `Select` selects nothing** | Correct. Stage 2 owns the three BINDINGS; the tool state machine and its seventeen tool classes are `ui/viewer/ToolController` at **Stage 4**, and text to select arrives at **Stage 5** |
| **Rotate View turns the page and Save would still write it upright** | Correct, and the distinction is deliberate: the view rotation belongs to this session and never reaches the file. Rotating pages destructively is `page.rotate`, is **Stage 3's**, and is unregistered |
| **The repair detail dialog shows codes like `ObjectOffsetWrong` rather than sentences** | Correct — those are diagnostic codes, are what the log holds, and are what a bug report should quote. The frame around them IS translated |
| **A protected document opens read-only and the banner says so** | Correct at Stage 2. Permissions are `core/doc/Permissions` at **Stage 10**; today the banner reports only that a password was asked for |
| ~~The status bar's page total is still an em dash, with a page on screen~~ | Correct at T13 — the status bar gets real numbers at **T14**. "1 of 1 at 100 %" over a document nothing has counted is the lie T9 refused to tell, and T13 did not start telling it |
| ~~Every §8 Viewer chord does nothing~~ *(T13's row, kept for a walker who read it)* | Was correct at T13. `KeyContext::Viewer` resolved and nothing was registered; **T14 registered 25 commands** and the row above replaces this one |
| ~~Anti-aliasing, line weight and the resolution cap cannot be changed~~ | **Fixed at T15** for fifteen of the sixteen keys, and the sixteenth is **I-11**: PDFium always draws a hairline at a minimum of one pixel and has no flag over it, so Line Weight is a checkbox that does nothing. Measured, not assumed — case A-81 |
| **The Preferences dialog has five pages and `Features.md` describes 26** | Correct. A group gets a page when its keys exist; the other 21 arrive with the stages that declare their settings |
| **`performance/renderWorkers` changes nothing until the application is restarted** | Correct, and said on the row itself: a worker pool's threads are made once. The two cache SIZES are live |
| A page appears a moment after the tab does, rather than with it | Correct, and it is D10's whole promise: the UI thread never rasterizes. The sheet and its border are drawn immediately and the tiles land on top |
| ~~**A missing embedded font renders in something else, silently**~~ | **No longer true as of T16.** Substitution goes through `core/render/FontSubstitution` → `WinFontResolver`, every mapping is logged with the face it chose, and the base-14 set is deliberately handed back to PDFium's own metric-compatible copies (issue **I-13**). **M-03's typography half is gradeable from T16 on.** What is still absent: the substitution is not reported in the UI — no message bar row, no font-substitution dialog — so a person sees the page and the log sees the reason |
| A benchmark's exit code is 0 on a slow machine | Correct. A benchmark **reports**; its exit code is about whether the measurement was possible |
| **Edge and Chrome render our corpus identically to us** | **Expected, and not evidence of anything.** Both use PDFium, the library we link — a match is two copies of one engine agreeing. Use Acrobat Reader, SumatraPDF or Firefox (P-4) |
| **Double-clicking a PDF opens RawPage rather than a viewer** | Correct — the `.pdf` association was hand-made during Stage 1's M-07 walk (its I-46). Open a reference with right-click → *Open with* |
| **Every corpus file is synthetic — there is not one real-world PDF in the tree** | Correct and deliberate (D2-11). It bounds what the golden suite proves; see §1, *What this corpus cannot tell us*. **Not a gap to be filed** — the trigger for revisiting is the first real-world rendering bug the corpus cannot reproduce |
| **`portable-fuzz` has never run before this stage** | Correct — its preset description has said "NO TARGETS EXIST until Stage 2" since Stage 0. Its **first** green run is a finding worth recording, not a formality (A-27) |
| **`ordinary/image-jpx.pdf` renders as black-on-mid-grey rather than black-on-white** | Correct, and written down at both ends — `corpus/jpxenc.py`'s header and `corpus/PROVENANCE.md`'s limitations table. The codestream carries **one coding pass** (the EBCOT cleanup pass of the MSB plane), so a 0/255 source reconstructs to roughly 0/128. It is a **truncated** codestream, not a malformed one, and it exercises the `/JPXDecode` path rather than JPEG 2000 fidelity |
| **The embedded fonts have nine hanzi and seven capitals, all made of rectangles** | Correct (`corpus/fontenc.py`). 一 二 三 十 口 日 田 中 目 and E F H I L T are real characters correctly drawn — they genuinely are straight strokes. What is exercised is the embedding path (`/FontFile2`, CIDFontType2, Identity-H, `/W`), not typography |
| **19 of the 24 `malformed/` files open in PDFium rather than being refused** | Correct and intended (measured at T4). They are *damaged*, not *destroyed* — most are recoverable by design, which is what makes them a test of `XrefRepair` rather than of error handling. The five PDFium refuses — `missing-endobj`, `no-root`, `objstm-corrupt`, `root-points-at-nothing`, `truncated-mid-stream` — are the interesting targets, not the failures |

---

## 8. Issue log

Numbered `I-nn`, **this stage's own namespace** — a reference to "I-34's lesson" or "I-36" in this
document means **Stage 1's**, and says so where it appears.

| # | Sev | Title | State |
|---|---|---|---|
| **I-01** | S3 | **`Architecture-FileMap.md` disagreed with `Implementation-Plan.md` about which stage owns the `pdf/cos/` reader, and about three `core/doc/` vocabulary types.** All six COS reader rows (fifteen files) were tagged ○ (S3) while Stage 2's component list claims them and Stage 3's does not; `Revision.h`, `Scope` and `Page` were tagged ○ (S3) while their **own** *Interacts with* column names Stage 2 consumers (`RenderCache`, `TileGrid`). Found while writing this guide's §4 manifest, by checking each path against the map rather than by running anything — which is the method Stage 1's I-31 and I-32 also came from. `Implementation-Plan.md` owns stage ordering, so the FileMap is the document that was wrong. **Confirmed with the user 2026-08-31** and corrected in the same change as this guide, along with `PageTree`'s split tag `○ (S2 read, S3 edit)` following `Outline`'s existing precedent. **A third instance surfaced while verifying this guide's §4 manifest against the map**, and it is the same root cause once more: `tests/fuzz/` was tagged ○ (S3) **and listed among Stage 3's components**, though a fuzz target over `pdf/cos/Lexer` and `Parser` cannot exist before those files do — and `CMakePresets.json`'s `portable-fuzz` description has said *“NO TARGETS EXIST until Stage 2”* since Stage 0. **The build system was right and two documents were wrong.** Moved to Stage 2 T5/T6; cases **A-25 … A-27** are written against it. The related filter-count disagreement — ten in the FileMap, nine in the plan — is resolved by **D2-4**: `CryptFilter` goes to Stage 10 with the security handlers it needs a key from. **The lesson is the one Stage 1's I-31 and I-32 already taught: a document set this size drifts in the places nobody executes**, and the only thing that finds it is checking each claim against its owner rather than reading for sense. | ✅ **Closed 2026-08-31** |
| **I-02** | S2 | **`license-audit` inventories the DECLARED manifest, not the RESOLVED closure — so four libraries Stage 2 links are invisible to the licence gate.** Found 2026-08-31 by running precondition **P-3**'s dry-run, which was in the plan only because writing it raised the question of whether two features declared at Stage 0 and never built still resolve. They do — `codecs`, `text` and both together all exit 0 on `x64-mingw-dynamic`, against Qt's own pinned `x86_64-w64-mingw32-g++`. **The resolution was never the risk; the closure was.** `freetype` pulls `brotli` and `bzip2`, `tiff` pulls `liblzma`, and — **measured with `vcpkg depend-info` on 2026-08-31, after this entry first mis-attributed it to `icu`** — **`hunspell` pulls `libiconv`**, and is its only parent. `icu` depends on neither — and none of the four appears in `vcpkg.json`, so `Inventory.cpp`'s `addPort` is never called for them: it iterates `root["dependencies"]`, `root["features"][*]["dependencies"]` and the non-vcpkg `overrides` map, and that is the whole of its input. It *does* read each port's SBOM, but only for ports it already knows to ask about — which is precisely backwards for a closure. **Nothing found is GPL**, so there is no breach today: `brotli` is MIT (already allowlisted), `bzip2` is `bzip2-1.0.6` and `liblzma` is 0BSD — both permissive, neither on `Tech-Stack-Plan.md` §8.2's allowlist — and **`libiconv` is GNU libiconv, LGPL-2.1-or-later**, which D4 permits but which carries an election and a **source-offer obligation** of the same shape as Qt's and Hunspell's, and nobody has recorded one. S2 rather than S3 because the gate's documented claim is broader than its behaviour and three later stages build on that claim: Stage 10 adds OpenSSL, Stage 16 adds tesseract and leptonica, and Stage 15's release compliance gate *is* this tool. **The allowlist rows are the cheap half; the inventory change is the real one.** Owned by **T7** (the map entries arriving with `codecs`, **and the inventory change itself**) and **T16** (`text`, and `libiconv`'s source offer), with **A-75** as the case. **T7 did the inventory change on 2026-09-04**: the audit now enumerates the installed tree's SBOMs and the component count went 17 → 20, picking up `liblzma` (mapped to `0BSD`, since vcpkg concludes `LicenseRef-vcpkg-null`) and the two `vcpkg-cmake` host ports. Two gate cases with a control prove the enumeration fires and does not simply fail on every tree. Whether `libiconv` reaches the shipped DLL set is confirmed at T16 with `objdump -p` — the dry-run proves it enters the tree, which is not quite the same claim. **Licences verified 2026-08-31**, against each port's manifest, its `portfile.cmake` copyright block, and upstream licence text — not from memory. **`brotli` 1.2.0 = MIT** (upstream `LICENSE`, standard MIT text; already on the allowlist). **`bzip2` 1.0.8 = `bzip2-1.0.6`** (BSD-style permissive, no copyleft, no source-disclosure obligation; retain the notice, no endorsement). **`liblzma` 5.8.3 = `0BSD`** for the library itself — XZ Utils' own `COPYING` at v5.8.3 says *“liblzma is under the BSD Zero Clause License”*, while the `xzgrep`/`xzdiff` **scripts** are GPLv2+ and parts of the build system are GPLv2+/GPLv3+; the port ships the library, so none of that is linked. Both identifiers are **absent from `Tech-Stack-Plan.md` §8.2's allowlist** and must be added. ⚠️ **`libiconv` 1.19 is the one that is not simply a missing row — and `depend-info` moved it out of Stage 2's reach.** Its **only** parent is `hunspell`, the spell checker, which nothing before **Stage 13** links; `vcpkg.json`'s `text` feature is a group spanning *“Stages 2, 5, 13”* by its own description, so T16 turning it on **builds** hunspell, icu and libiconv while Stage 2 links only freetype. **Built is not linked**, so Stage 2 most likely incurs no LGPL obligation at all here — which T16's `objdump -p` settles either way, and is exactly why that check is in the task rather than assumed. The licence facts below stand regardless, and become live at Stage 13. Its vcpkg manifest declares `license: null` — vcpkg's marker for *not expressible as one SPDX expression* — and the portfile says why, verbatim: *“The libiconv and libcharset libraries and their header files are under **LGPL**, see COPYING.LIB… The **iconv program** and the documentation are under **GPL**, see COPYING.”* So the package is **split**: the part we link is **LGPL-2.1-or-later**, which D4 permits and which needs the Qt/Hunspell treatment — an election, dynamic linking, a user-replaceable binary and a **corresponding-source offer**, none of which exists. The part we do **not** link is **GPL**, and the portfile's `vcpkg_make_install()` plus `vcpkg_copy_tool_dependencies(…/tools/libiconv/bin)` means **a GPL `iconv.exe` is installed into the vcpkg tree**. **That is not a breach and Stage 2 does not package anything** — it is a build-tree artifact, and D4 forbids a GPL *dependency*, not a GPL binary sitting in a toolchain directory. **It becomes one the moment anything sweeps `installed/<triplet>/tools/` into an installer, which is Stage 15's MSI/MSIX work.** Recorded here rather than deferred silently, because the packaging stage will not rediscover it. **T16 CLOSED IT ON 2026-09-05, and the answer to the `libiconv` half is a measurement rather than a source offer.** `objdump -p` on the built `rawpage.exe` names **eighteen** DLLs — the seventeen from before plus `DWrite.dll`, an OS library — and **not one of them is freetype, brotli, bzip2, icu, harfbuzz, hunspell or libiconv**: Stage 2 links none of the `text` ports, because PDFium bundles its own FreeType and the substitution bridge reaches the OS through DirectWrite and hands the engine BYTES. So the LGPL obligation is Stage 13's, and T16 made that **structural rather than remembered** by splitting `vcpkg.json`'s `text` feature into `fonts`, `shaping` and `spelling` — measured: `fonts` + `shaping` pull neither hunspell nor libiconv, so the GPL `iconv.exe` now enters the tree at Stage 13 instead of eleven stages early. The three map rows and the `bzip2-1.0.6` allowlist entry landed in the same change, **even though nothing links them yet**, so the first run after Stage 5 turns `fonts` on is a pass rather than a discovery. **The GPL-tool exclusion stays flagged forward to Stage 15** — it is a packaging decision and no measurement here can close it. | ✅ **Closed 2026-09-05 at T16** — raised 2026-08-31, licences verified the same day; the inventory change at T7, the closure measurement and the feature split at T16. The `iconv.exe` packaging exclusion is flagged forward to **Stage 15**, which is a carry rather than an open issue |
| **I-03** | S3 | **Three signatures this document set had written for Stage 2 name `QImage`, and none of them could have been built as written.** `Implementation-Plan.md` § Stage 2's component list gives `PdfiumRasterizer` `Result<QImage> raster(const RenderRequest&)`, `RenderCache` `std::optional<QImage> lookup(...)` and `RenderScheduler` `Signal<RenderRequest, QImage> ready` — but `IPageRasterizer` is declared in `core/backend.h`, `RenderCache` and `RenderScheduler` are `core/render/`, and **`core/` may not name a QtGui type**: it links `Qt6::Core` only, `layer-lint`'s `core` layer has listed `QImage` in `denyExternal` since Stage 0, and CLAUDE.md convention 3 is the standing rule (it is why `core/cmd/Chord` is first-party rather than a `QKeySequence`). Found at **T1**, by writing the interface the list describes. **Not a conflict anyone had to adjudicate**: plan **D2-9** already says the tile pipeline carries *buffers and callbacks only*, so the component sketch was the loose statement of a settled rule rather than a rival decision. Fixed in `core/backend.h` with `core::RasterImage` — width, height, **stride**, pixel format and bytes, with the stride carried rather than computed because a rasterizer may align its rows and a consumer assuming `width * 4` on a padded buffer draws diagonal garbage. `ui/` wraps those bytes in a `QImage` at the far side, where QtGui is allowed, and the wrap is free: both formats are the little-endian BGRA layout `QImage::Format_ARGB32` and `Format_RGB32` consume. `Implementation-Plan.md`'s three rows were corrected in the same change. **T10 and T11 inherit the correction**, and **A-44 is unaffected** — the raster suite links `rawpage_pdfium` and may hold a `QImage` of its own. S3 because it was a documentation slip that the gate would have caught the moment anyone typed it. | ✅ **Closed 2026-09-01** at T1, in the header that declares the interface | 

| **I-04** | S3 | **Nothing compares a catalog key a source REFERENCES against the catalogs, and eighteen referenced keys have no entry.** Found at T2 the moment the engine layer named its first key: `error.engine.initFailed` was reported as an `orphan-key` — a key nothing references — while being referenced by the file that had just been written, because `pdf/` was not in `i18n-lint`'s `scannedPaths`. Neither was `platform/`, which names **eleven** keys. **Fixing the paths was the easy half and it is done** (`pdf/` and `platform/` added; the gate went from 79 files to 90 and the false note is gone). **The real half is that no rule asks the question at all**: `missing-key` compares the shipped catalogs *against each other*, `action-label` covers `action.*` from `ActionId.def`, and `orphan-key` is the INVERSE — a catalog key nothing references — so a key a source names and no catalog defines is invisible from both directions. Measured 2026-09-01 by diffing every quoted `error.*`/`shell.*`/`ribbon.*`/`prefs.*` literal in `core/`, `ui/`, `app/`, `pdf/` and `platform/` against `rawpage_en.ts`: **eighteen** are missing — `error.action.{unknown,unavailable,alreadyRegistered,noHandler}` and `error.shortcut.{unknownAction,invalidChord,widgetNative}` from **`core/`, which has been scanned since T6**, plus `error.shell.*` (3) and `error.singleInstance.*` (8) from `platform/`. **Latent today rather than visible**: only `error.open.*` and `error.customization.*` currently reach a surface, and both exist — an unresolved key would render as its own key text, which is what makes this S3 now and **S2 the moment `MessageBar` starts showing engine errors**. Owned by **T14**, which builds that surface, with **A-14** as its case; the fix is an `unknown-key` rule keyed on the declared catalog families (so `settings.ini` and `pdfium.dll` are not mistaken for keys) plus the eighteen rows. **Not fixed at T2 on purpose**: writing eighteen error messages in two locales is a design act that belongs with the surface that shows them, and T2 is the engine boundary. **RE-TAGGED T14 → T16 at T14**, because T14 had no reason to touch `i18n-lint` and a task tag that quietly passes is how an item becomes permanent. **CLOSED AT T16 on 2026-09-05, and the count was exactly eighteen** — which is worth saying, because the measurement that produced that number was a hand-diff on 2026-09-01 and the rule reproduced it four days and four tasks later, naming the same eighteen keys in the same four files. `keyFamilies` in `tools/i18n-lint/rules.json` is the whole of the rule's input and the reason it can BLOCK where `orphan-key` only reports: the scanner's notion of a catalog key is deliberately generous, because an over-broad reference set makes orphan-key quieter, and the same generosity in a blocking rule fails the build on `settings.ini` and `pdfium.dll`. Anchoring on the five declared families — `action`, `shell`, `ribbon`, `prefs`, `error` — is what separates a missing translation from a file name. The eighteen keys were written in both shipped locales in the same change, and `--explain` now prints the family list, because a rule's blind spot has to be visible without reading the tool. **Two gate cases over one fixture**, which carries four references of which exactly one is the defect: a key that IS defined, a namespace reference ending in a dot, and a dotted literal that is a file name all have to stay silent, and those three are what the rule's precision consists of. | ✅ **Closed 2026-09-05 at T16** — the scanned-path half 2026-09-01, the rule and the eighteen keys at T16 |
| **I-05** | S2 | **PDFium cannot be called from two threads at once, and per-document confinement does not help — so D2-9's “thread-confined per worker” is false and D10's “worker pool” cannot be eight PDFium threads.** Found at **T3**, by the A2 probe, on its first run — which is the entire argument for measuring early rather than after T13. Eight workers, **each with its own `FPDF_DOCUMENT` and its own `FPDF_PAGE`**, crash. Measured by walking the worker count rather than asserted: **1 worker exits 0; 2, 4 and 8 all die with `STATUS_HEAP_CORRUPTION` (0xC0000374)** inside the render call, and before the pages were preloaded they died inside `FPDF_LoadPage` (stack captured under `gdb`). What PDFium shares between threads is **process-global** — its font mapper and glyph caches — not per-document, so confining a document to a worker confines the wrong thing. Reproducible on demand: `rawpage_bench_render.exe 8 --unsafe-parallel`, because a finding that needs a source edit to reproduce is one that gets re-litigated. **A2 IS UNAFFECTED AND IS `go`**: the constraint is a throughput ceiling, not a latency one — per-tile p95 is 0.532 ms against a 16.667 ms budget, and eight workers behind one engine lock retire **1.05×** the tiles per second of one worker. The engine is fast; it is simply not parallel. **What D10 actually promises is untouched** — *the UI thread never parses or rasterizes* — and two arrangements keep it: **one render thread with a queue**, or **N workers serialized on one engine lock**. Neither was chosen there, deliberately: that is `RenderScheduler`'s shape and it belonged to **T11**. **T11 CHOSE ONE RENDER THREAD WITH A QUEUE, on 2026-09-04**, and corrected D2-9's wording in the same change. The choice was made on the measurement rather than on taste: the 5 % the second arrangement buys is not worth a concurrency bug when the engine is already 30x inside the frame budget, and one consumer makes cancellation — which plan §5.3 calls *the feature* — provable rather than probable, and makes PDFium's single-threadedness structural rather than a discipline every future caller has to remember. `RenderScheduler::Config::workerCount` remains and defaults to **1**, each worker taking its **own** `IPageRasterizer` from a factory, because T18 re-measures A2 through this pipeline and must be able to walk the count without a rebuild, and because a future non-PDFium rasterizer is not bound by this finding. The engine lock in `PdfiumInit` stays: it is correct under either arrangement and it is what **A-45** passes on. Also flagged forward: **T16**'s font work and **Stage 5**'s text extraction inherit the same constraint, since both call the engine from the same global state. | ✅ **Closed 2026-09-04 at T11** — raised 2026-09-01 at T3 |
| **I-06** | S3 | **`/JBIG2Decode` is DECLARED AND DECODES NOTHING, and `/CCITTFaxDecode` with `K > 0` is implemented against no fixture.** Two gaps in T7's nine filters, raised by building them rather than by running anything. **JBIG2 is the substantive one, and three facts forced it, each established rather than assumed.** (1) **There is no permissible library**: the one usable open-source JBIG2 decoder is Ghostscript's `jbig2dec`, which is **AGPL-3.0** — `Tech-Stack-Plan.md` **D4** forbids any GPL or AGPL dependency outright and `license-audit` enforces it mechanically, so linking it is a build failure rather than a trade-off to weigh. PDFium has its own decoder and is behind the `FPDF_` boundary `layer-lint` forbids `pdf/cos/` from crossing (D2-9), which is half of D3. (2) **There is no corpus fixture** — checked against `corpus/PROVENANCE.md`, not remembered: `corpus/` has DCT, JPX, CCITT G3, CCITT G4, Flate, LZW and RunLength images and **no JBIG2 file at all**, and **D2-11** says in as many words that *a construct the corpus never contains is a construct nothing in this project ever renders*. (3) **Nothing in Stage 2 would call it** — rendering goes through PDFium, which decodes JBIG2 itself; the first real consumer is Stage 8's image export. Together those are exactly the argument **D2-4** already accepted for `CryptFilter`: *building it here produces a file that cannot be tested and will be rewritten*. **What was built instead** is a declared filter that returns `Unsupported` under its own catalog key `error.pdf.filterJbig2` — never an empty buffer, which would reach the image pipeline as a blank page indistinguishable from a page that is genuinely blank (convention 6's argument about silent stubs, one layer down from `platform/`). The refusal is asserted by `tst_cos_filters`, through the ordinary filter chain as well as directly, so a registry that never dispatched to it would not pass. **The cost of finishing it, recorded so the estimate is not rediscovered:** an MQ arithmetic decoder, segment headers, and generic region decoding for templates 0–3 with TPGDON — roughly 450 lines — **plus** a first-party `corpus/jbig2enc.py`, since every corpus file is generated by us (D2-11). Symbol dictionaries and text regions are more again. **The honest sequencing is corpus first, decoder second**, because the decoder cannot be believed without the fixture. **The CCITT half is smaller**: `K > 0` (mixed one- and two-dimensional Group 3) is implemented — it is the same machinery as the other two plus one flag bit — and has no corpus file, so it is exercised only for *does not crash on nonsense*. Named at the assertion rather than left for a reader to assume covered (convention 19). **S3 rather than S2** because nothing in Stage 2 through Stage 7 calls either path and both refuse audibly; it becomes S2 at **Stage 8**, when image export needs to read what it is exporting. **Raised at T7 by the author of the filters**, and recorded rather than absorbed because `Stage2-Rendering-Plan.md` §12 says an overrun is said out loud rather than quietly narrowing the corpus. | 🟡 **Open** — a scope finding, not a defect. Owner unassigned; the natural home is Stage 8 |
| **I-07** | S3 | **`platform/platform.h` described `MonitorInfo`'s geometry as *device-independent pixels*, and it is PHYSICAL pixels.** Found at **T12** by the first implementation of `IDisplayTopology`, measuring the P-1 rig: Windows lays the virtual desktop out in physical pixels, so the 4K panel at 150 % occupies a 3840×2160 rectangle and not a 2560×1440 one. **The DIP reading is exactly what a DPI-UNAWARE process is handed** — virtualized figures that are self-consistent and wrong — so the field comment described the failure mode as if it were the specification, and guide case A-60 expects the physical figures. A header comment is not a rule, but it is what the next implementer reads: this one would have produced a `WinDisplayTopology` that divided by the scale factor and agreed with itself. Corrected in the same change as the file, with `processIsPerMonitorAware()` added so a caller can say which kind of answer it is getting. | ✅ **Closed 2026-09-04 at T12** |
| **I-08** | S2 | **`IDocumentBackend::pages()` was declared on the CONCRETE backend, where `ui/` could never reach it.** T9 added `pages()` and `repairReport()` to `pdf::DocumentBackendImpl` with a comment saying `ui/` would reach them *"through the backend it was handed, which is still a `core::` type"*. That is not achievable and could not have been: `ui/` is handed a `core::IDocumentBackend&`, `ui/` may never name `pdf/` (the linker enforces it — `rawpage_ui` links neither `rawpage_cos` nor `rawpage_pdfium`), and a `dynamic_cast` to the concrete type would be the layering violation the whole boundary exists to prevent. It was invisible at T9 because **nothing in `ui/` needed a page tree until there was a viewer**: `openFiles()` asked the backend whether the bytes were a PDF, threw the handle away, and never asked what was in the file. T13 is the first task that needs it, and it is where the gap became reachable rather than theoretical. **Fixed by moving the declaration onto `core::IDocumentBackend`**: opening a document and saying what is in it are the same responsibility, and `open()` already returns a handle that means nothing without it. `repairReport()` deliberately stays on the concrete type until **T14**, its first consumer — a service handed to every caller ahead of the first that reads it is the shape Stage 1 found five times (I-34, I-42, I-43, I-46, I-47). | ✅ **Closed 2026-09-04 at T13** |
| **I-09** | S3 | **The one render arrangement the product actually runs was the one arrangement nothing tested.** Raised at **T13** while writing the composition root: the adapter that turns a `RenderRequest`'s `DocumentHandle` back into an engine was a class private to `CompositionRoot.cpp`, and every suite that could have driven it fakes one end of the path — `tests/unit` and `tests/ui` may not link an engine at all (D2-2, D1-4), and `tests/render`'s existing cases call `PdfiumRasterizer` with no scheduler. Not a defect in any of them; a gap between them, and the same shape as Stage 1's built-but-unreachable findings. **Fixed by making it `app/BackendRasterizer.h`**, a header naming only `core/` types, so `tests/render/tst_renderpipeline.cpp` (case **A-79**) drives the shipped class rather than a second copy that could agree with a mistake in it. | ✅ **Closed 2026-09-04 at T13** |
| **I-10** | S3 | **Stage 1's I-53 named a mechanism that does not do what the note said it did.** I-53 recorded that the *registered-but-currently-refused* tooltip was unbuilt and that "the mechanism to fill it in exists and is unused — a `tooltipKey` on the registration". Building it at **T14** found that `Registration::tooltipKey` is the tooltip an **ENABLED** control shows: reusing it for the refusal reason put *needs a document to be open* on every enabled Viewer button, which is a worse sentence than the one I-53 was about. The two are shown in mutually exclusive states and say opposite things — *what this does* against *why it cannot* — so one field cannot serve both. **Fixed with a second field**, `Registration::unavailableKey`, read only while the action is disabled. The finding is worth the entry not for its size but for its shape: **a deferral's note about how it will be fixed is a guess made by someone who did not build it**, and this one was carried through a stage boundary and two documents as though it were a design. | ✅ **Closed 2026-09-04 at T14** |
| **I-11** | S3 | **`pageDisplay/lineWeight` is DECLARED AND INERT: PDFium cannot draw a hairline at its true width.** `Features.md` §1.2 defines the option as *"draw hairlines at their true width instead of a minimum of one pixel"* and `Implementation-Plan.md` lists the key among Stage 2's sixteen. Building them at **T15** found that PDFium always enforces the one-pixel minimum and exposes no `FPDF_*` flag over it — and D3 says that C API is the only way this project reaches the engine, so this is a CAPABILITY GAP rather than a defect. **Measured rather than asserted** (case **A-81**): every flag `RenderFlags` can express, applied to `corpus/ordinary/vector-hairlines.pdf`, moves the solid ink coverage by at most 1.2 % — `NoSmoothPath` reduces the total ink by a third, but that is the anti-aliasing fringe rather than the line. **The row is declared rather than left out** because the setting is real, the plan lists it, and an absent row is indistinguishable from one nobody thought of; `ViewSettings::lineWeight` carries the value so the day an engine can honour it there is a value to honour. Same shape as **I-06**. **A-65's criterion is corrected rather than narrowed**, as A-18's was at T7. | 🟡 **OPEN by decision** — raised 2026-09-04 at T15. Closed by an engine that can express it, or by the user deciding the option should be withdrawn from `Features.md` |
| **I-12** | S3 | **T16's *Done when* asked for vcpkg's `text` feature to be turned on, and the measurement in the same clause says not to.** The clause reads *“vcpkg's `text` feature turned on, and the LGPL source-offer row for `libiconv`”* — and then, in its own *Done when* column, *“`objdump -p` confirms which of the transitive DLLs actually ship”*. Building T16 ran that check and it answers the first half: **`rawpage.exe` links nothing from `text`.** PDFium statically bundles its own FreeType, and the substitution bridge reaches the OS through **DirectWrite** and hands the engine the face's BYTES, so freetype has no caller; harfbuzz, icu and hunspell belong to Stages 5 and 13 by that feature's own description. Turning it on would have built four ports, downloaded ICU, and put a **GPL `iconv.exe`** into `installed/<triplet>/tools/` eleven stages before anything links the library — to satisfy a clause whose evidence says the opposite. **`Tech-Stack-Plan.md` §6.2 already said “Stage 5 for `text`”**, and that document owns the stack. **So T16 did the OTHER thing the plan's §10 offered**: it split `text` into `fonts`, `shaping` and `spelling`, one per owning stage, verified all three resolve, and measured that `fonts` + `shaping` pull neither hunspell nor libiconv. **Recorded as an issue rather than absorbed** because a task that quietly does less than its plan says is indistinguishable from a task that forgot, and because the *reason* — build-is-not-link, and a licence hazard that travels with the build rather than with the link — is the part a later stage needs. | ✅ **Closed 2026-09-05 at T16** — a scope correction, recorded in `vcpkg.json`, `Tech-Stack-Plan.md` §6.2 and the Stage 2 plan's §10 in the same change |
| **I-16** | S3 | **`corpus/PROVENANCE.md` AND `corpus/*.json` HAVE BEEN COMMITTING AS BINARY SINCE T4, INSIDE THE FIX WRITTEN TO STOP EXACTLY THAT.** `.gitattributes` marks `corpus/**` and `golden/**` `binary`, which is correct for the `.pdf` and `.png` data — a PDF xref stores absolute byte offsets and CRLF injection shifts every one of them. T4 put the GENERATORS in `corpus/` too, noticed that the blanket rule was marking several thousand lines of Python and Markdown binary, and pulled them back out with `corpus/*.py text eol=lf diff=python merge` and `corpus/*.md` / `corpus/*.json text eol=lf merge`. **`binary` is a MACRO for `-text -diff -merge`, so coming back out of it takes all three words, and the `.md` and `.json` lines supply two.** The result is that the half that mattered most — line endings — was fixed, and `git diff` went on printing `Bin 23443 -> 24421 bytes` for the provenance record. Only `corpus/*.py` escaped, and by accident: `diff=python` sets the attribute as a side effect of choosing a driver. **So the file whose entire job is to be read in review was the one nobody could read in review**, for four days and five commits, and T4's own `.gitattributes` comment says in the present tense that it fixed this. **Found at T17** — not by looking for it, but because `golden/manifest.json` was written with the same two words, committed, and landed as `Bin` in the commit that had just described at length why that must not happen. Fixed by adding `diff` to all four lines; the attribute is applied when a diff is RENDERED rather than when a blob is stored, so **every past commit became readable too** — T16's `PROVENANCE.md` change now shows as `1 insertion(+)` where it showed `Bin`. **The lesson is not about git.** It is that a fix is verified by observing the thing it was supposed to change, and this one was verified by reading its own diff of `.gitattributes`. | ✅ **Closed 2026-09-06 at T17** |
| **I-14** | S3 | **THE GOLDEN CAPTURE RECORDED NO FONT SUBSTITUTIONS FOR THE ONE CORPUS FILE WHOSE PURPOSE IS TO HAVE THEM — and nothing failed.** T17's `golden::renderPage` marks the font-bridge log, renders, and keeps the rows that appeared in between, so that each baseline records which faces it resolved and the suite can skip a `machineDependent` case on a machine that resolves differently. The first version took the mark **after** `PdfiumDocument::open()` returned. **PDFium resolves some fonts while opening**, and `ordinary/font-not-embedded.pdf`'s two absent families are among them, so every row it produced fell outside the window and its capture record came back empty. **The symptom was silence in both directions**: the baseline images were correct, the comparison passed, and the machine-dependence guard those rows feed was simply never armed — so the first CI run on a box with different fonts would have reported a rendering regression, which is the exact failure `CLAUDE.md`'s own T17 note warned about by name. Found by **reading the data the tool produced** rather than by a failing assertion: 61 files' worth of capture records, and the one file expected to carry substitutions carried none. Fixed by sampling before the open, which also revealed the finding A-82 now guards — `malformed/bad-name-hex-escape.pdf` substitutes too. **Same shape as Stage 1's I-29**: a measurement whose every figure was its own bug, caught by disbelieving a result rather than by anything going red. | ✅ **Closed 2026-09-06 at T17**, in the same change as the code that found it |
| **I-15** | S3 | **THE GOLDEN SUITE'S PERTURBATION CONTROL PASSED WHILE MEASURING NOTHING.** Guide case **A-49** requires a deliberately perturbed render to FAIL the comparison, which is convention 7 applied to the golden machinery itself — a suite that cannot fail is decorative. The perturbation is `RenderFlags::Grayscale` and the page it was first aimed at was `ordinary/text-a4.pdf`, which is black ink on white paper: **greying a grey page is the identity**, so the control reported zero differing pixels and CHECK_FALSE(passes) failed. It failed LOUDLY, which is the only reason this is an S3 and not the shape of I-14 — had the assertion been written the other way round it would have been a control that agreed with everything forever. The underlying fact is a property of this corpus that nobody had counted: **171 of the 198 baselines have R == G == B in every pixel**, which is also why they are stored as eight-bit grey. Repointed at `ordinary/image-dct-rgb.pdf`, one of the 27 that carry colour: 165,413 differing pixels of 891,662. **A second case is now its control in the other direction** — same file, same page, same DPI, no flag — because a comparison that answered *different* to everything would have made the first one green too. | ✅ **Closed 2026-09-06 at T17** |
| **I-13** | **S2** | **THE FONT BRIDGE SUBSTITUTED A SYSTEM FACE FOR THE BASE-14 FONTS, changing how every page of every existing document renders.** Found at **T16 by the control case, not by the case the task was written around.** A-66 asks whether the substitution bridge RUNS; the control beside it asks whether it runs when it should NOT, and that one measured `mapFontCalls() == 2` on `corpus/ordinary/text-a4.pdf` with **Helvetica resolved to Segoe UI**. Every file in this project that touched the subject — the bridge's header, the policy's header, this guide's own A-66 wording — had said that Helvetica, Times, Courier, Symbol and ZapfDingbats are required of every conforming reader, that PDFium carries its own metric-compatible Foxit copies, and that a font hook would therefore never be asked about them. **PDFium asks about them.** `FPDF_SetSystemFontInfo` is consulted for EVERY unembedded font and the engine uses its own faces only when the hook declines. **The consequence is out of proportion to the fix.** T16's subject is documents that render *wrongly today* — a font named and not embedded — and without the decline it would have changed the appearance of every page of every document that names a standard face, which is most of them, in a task nobody would think to look at for that. It would also have invalidated `tests/golden/`'s baselines at **T17** before they were captured, and the substitution is not neutral: Segoe UI is not metric-compatible with Helvetica, whereas PDFium's copy is and is identical on every machine. **Fixed with `core::render::isStandardFontName()`**, checked against the NORMALIZED family so that `Times-BoldItalic` reduces to `Times`, and deliberately NOT listing `Arial`, `TimesNewRoman` or `Courier New` — those are real installed families that a resolver should match exactly, and declining them would refuse a font we can serve perfectly. **The lesson is convention 7's, in the form it takes when the thing being verified is a NEW mechanism**: a case that proves a mechanism runs is half of proving it runs when it should, and nothing in the criterion this task was given called for the other half. | ✅ **Closed 2026-09-05 at T16**, in the same change as the code that found it — raised and closed by the same run |
| **I-17** | **S2** | **`portable-asan` AND `portable-fuzz` COULD NOT CONFIGURE FOR THE WHOLE OF STAGE 2, AND THE COMMENT ABOVE THE DEFECT ASSERTED THE GUARD THAT WAS MISSING.** `pdf/CMakeLists.txt` defines `rawpage_cos` (portable by rule) and then `rawpage_pdfium` (the engine facade, Windows only). The root `CMakeLists.txt` correctly skips `rawpage_assert_pdfium()` for the portable subset and the gate build, so `RAWPAGE_PDFIUM_VERSION` is unset there **by design** — and `pdf/CMakeLists.txt` then walked straight into the `FATAL_ERROR` that exists to catch a *reordered* assert, reporting *“rawpage_pdfium cannot be built: RAWPAGE_PDFIUM_VERSION is not set”* on both Linux jobs. **The comment sitting directly above that error said the guard was elsewhere**: *“It is not guarded by `if(WIN32)` here: the root CMakeLists only descends into `pdf/` with `RAWPAGE_PORTABLE_SUBSET` off.”* The root has never done that and must not — the portable subset **needs** `pdf/cos/`, so `add_subdirectory(pdf)` is deliberately outside that guard. The claim was written at **T2** (2026-09-01) alongside the target it describes, and the file it named was never checked. **Live for eighteen tasks and invisible**, because no CI run happened between Stage 1's sign-off (2026-08-31) and T18 (2026-09-06): the sanitizer and fuzz jobs had never once met Stage 2's code. Fixed with an explicit `if(RAWPAGE_PORTABLE_SUBSET) return()` **in the file the reader is looking at**, printing what it did rather than passing silently. **The lesson is I-10's and convention 8's together**: a comment that asserts a guard lives in another file is a claim nothing re-derives, and it reads as a design decision for as long as nobody runs the configuration it describes. | ✅ **Closed 2026-09-06**, with the negative control run in both directions — the original file reproduces the exact CI error locally, the fixed one configures and prints its branch |
| **I-18** | **S2** | **THE WINDOWS CI JOB NEVER FETCHED PDFIUM, SO IT COULD NOT CONFIGURE EITHER — the engine had been in the tree since T2 and the workflow had never mentioned it.** PDFium is not a vcpkg port and nothing under `third_party/pdfium/` is committed except its README (`.gitignore`), so a fresh checkout has no engine and `cmake --preset x64-release` fails in `rawpage_assert_pdfium()` (**PA-1**) with *“found: no third_party/pdfium/VERSION”*. `scripts/fetch-pdfium.ps1` has existed since T2 to do exactly this — verify the pinned SHA-256, generate the GNU import library upstream does not ship — and **no CI step ever called it**. The assertion was working; what was absent was the step. Found in the same run as **I-17** and for the same meta-reason: Stage 2's build met CI for the first time at T18, five days and eighteen tasks after the engine landed. Fixed by adding a *Fetch the pinned PDFium* step to the `windows` job, placed **after** the toolchain is on `PATH` because the script needs `gendef` and `dlltool` from `C:\Qt\Tools`, and **before** Configure. The script is idempotent, so on the self-hosted runner it is a no-op after the first run. **This is I-30's shape from Stage 1** — a step everything assumed was someone else's — and the countermeasure is the same: the job that proves it is the job that runs it. | ✅ **Closed 2026-09-06** |
| **I-19** | **S2** | **A CACHE HIT NEVER WOKE THE PUMP, SO A MIXED-DPI DRAG BLANKED THE PAGE AND LEFT IT BLANK.** Found by the user on 2026-09-06 in the **first minutes of M-01's walk** — the first Stage 2 defect found by a human rather than by a suite. Dragging the window between the 150 % and 100 % monitors, at Actual Size, Fit Width or an arbitrary zoom, made the page's text **disappear**; scrolling sometimes brought it back and sometimes did not, and when it did not, only dragging to the other monitor again did. **`DpiPolicy` is innocent** — the whole of M-01's *If it fails* text is about re-rasterization direction, and the mechanism is in neither direction. `RenderScheduler` has **two producers of a completion**: the worker loop, which calls the wake handler when a job finishes, and `enqueue()`'s cache-hit branch, which pushed the finished tile onto `completed_` and returned **without waking anything**. `completed_` is drained only by `deliverReady()`, and the only thing that posts `deliverReady()` in the running product is that handler (`app/CompositionRoot.cpp`) — so **a batch of requests that are ALL cache hits starts no worker, wakes nobody, and strands its tiles in the queue.** The mixed-DPI drag is the arrangement that produces such a batch: `TilePresenter::requestTiles` re-keys every request on `deviceScale`, drops every tile held at the old one — the page blanks **correctly** at that instant — and dragging BACK to a monitor whose tiles are still in the 256 MiB cache finds every one of them a hit. **Both halves of the *sometimes* fall out of the same gap**: a scroll that reaches an uncached tile is a MISS, which starts a worker, which wakes the pump, and `deliverReady()` then drains the *whole* queue including the stranded hits; a scroll that stays inside cached tiles is all hits and changes nothing. **WHY 696 GREEN TESTS MISSED IT, which is the part worth keeping.** A-38's neighbour — *“render scheduler serves a cached tile without rasterizing it”* — calls `scheduler.deliverReady()` **by hand**. That is a back door no running product has, and it was not named at the assertion, so the case proved the cache **serves** and nothing anywhere proved the product ever **asks**. **Convention 19, in Stage 1 I-34's exact shape**: green in CI and unreachable by hand. It is also **convention 22** from the other side — the arrangement that fires was tested, the arrangement in which it must fire was not. Fixed by copying `wake_` under the lock and calling it after unlocking, precisely as the worker loop does; waking is not delivering, so `enqueue()`'s standing rule that no subscriber is entered from inside it still holds — the handler posts, and the queued call runs after the caller's `requestTiles()` has finished asking. | ✅ **Closed 2026-09-06**, with the control in both directions: `unit.render scheduler WAKES for a tile it served from the cache` fails on the unfixed scheduler at `CHECK( wakes.load() > before )` expanding to `1 > 1`, and its own MISS leg is a `REQUIRE` so the case cannot pass against a handler that never fires at all. **Confirmed on the P-1 rig by the user the same day** — the page no longer blanks on a mixed-DPI drag, which is the half a unit test counting wakes cannot claim. **M-01 is NOT thereby walked** — it is re-walked from step 1 by the user |
| **I-20** | **S2** | **ACTUAL SIZE WAS WRONG BY THE SCALE FACTOR ON EVERY SCALED MONITOR, BECAUSE THE TOPOLOGY JOIN NEVER MATCHED AND THE FALLBACK IT FELL THROUGH TO WAS A DIP FIGURE WEARING A PHYSICAL ONE'S NAME.** Found by the user 2026-09-06 in the first minutes of **M-03**, by the only method that could find it: holding the product beside Acrobat. At `Ctrl+1` on the 150 % panel our A4 page measured ~469 px against Acrobat's ~712 — **1.52x**, which is the scale factor. **Two faults, and each alone was survivable.** **(1) The join could never succeed.** `ui/viewer/PageLayoutEngine::metricsFor()` matched `QScreen::name()` against `MonitorInfo::id`. `id` is `GetMonitorInfoW`'s `szDevice` — `\\.\DISPLAY1` — and Qt 6 on Windows names a screen with the display's MODEL, `MAG 272URDF`. Those two strings can never be equal, so the `Topology` branch **never once executed in the shipped product on any machine**, and T12's whole deliverable — the raw DPI measured off the glass, 163 where `96 x scale` says 144 — reached no pixel. **(2) The fallback is device-independent.** `QScreen::physicalDotsPerInch()` divides the panel's millimetres into `QScreen::geometry()`, and Qt's geometry is in DIP: 2560 for a 3840-wide panel. It answers **108.89** for glass that measures **163** — off by exactly the scale factor, which is why the error was invisible on the 100 % monitor and on every unscaled development desktop. Feeding it to `DpiPolicy` as `PhysicalDpi` violates that class's own header, which says at length that Actual Size is the physical DPI and that the OS scale factor *does not enter it*. **This is I-07 at one remove** — there a rectangle documented as device-independent was physical; here a dpi that genuinely is device-independent was read as physical. **WHY 697 GREEN TESTS MISSED IT.** The join is reachable only on a real desktop and **nothing tests it there**: `tests/ui` runs under `offscreen`, where there is one fake screen, no topology, and a fixture whose two sides agree by construction; **A-60** measures the topology with no screen beside it. The single case that touches the resolved source — `tests/ui/tst_pageview.cpp` — asserts `source != Topology` (correct, its bundle is null) beneath a comment reading *“On the P-1 rig the shipped product takes the topology path instead, and `platform.displaytopology` is where that is measured.”* **Both clauses were false**, and it is a present-tense claim nothing re-derives — CLAUDE.md rule 4, in **I-17**'s exact shape. **Fixed in three parts, because two would have left the third invisible:** `MonitorInfo` gains `friendlyName` from `DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME` and the join accepts it (`id` still accepted, since a platform reporting one and not the other must still match); the Qt fallback is multiplied by the device pixel ratio, giving 163.3 against the topology's 163 and 108.8 against its 108; and **`--print-screen-metrics`** prints both sides and the resolved source, because a diagnostic that printed only the answer would have looked right in precisely the broken case. | ✅ **Closed 2026-09-06.** Verified on the rig: the join now reports `source topology`, `physDpi 163` and `108` — **and confirmed on the glass by the user the same day, beside Acrobat at `Ctrl+1`, which is the measurement that actually closes this one.** The diagnostic and A-84 both say the number is now right; only a person holding the two renderers side by side can say the page is, and that is how the defect was found in the first place. Regression case **A-84** is headless and carries its control in both directions — it fails on the unfixed build with *“the resolved physical DPI moved with the scale factor: 100 dpi at dpr 1, 66 dpi at dpr 1.5 — 51 % apart”*, and it refuses to pass if `QT_SCALE_FACTOR` did not actually change the ratio it varies. **M-03 is NOT thereby walked** — it was blocked by this and is walked from the start against the fixed build |
| **I-21** | **S2** | **WIDGET ANNOTATIONS WERE NEVER DRAWN, SO EVERY FORM RENDERED AS ITS LABELS AND NOTHING ELSE — AND THE GOLDEN BASELINE SAID THAT WAS CORRECT.** Found by the user 2026-09-06 in the **M-03** walk, on `corpus/ordinary/forms-acroform.pdf` beside Acrobat: eleven widgets with real `/AP` streams, none of them on the page. **The mechanism is a split in PDFium that the corpus already contained the experiment for.** `FPDF_RenderPageBitmap` with `FPDF_ANNOT` draws every annotation **except** `/Subtype /Widget`; a widget's appearance stream is reachable only through `FPDF_FFLDraw`, which needs a handle from `FPDFDOC_InitFormFillEnvironment`. The facade never made one. `corpus/ordinary/annots-mixed.pdf` — nine NON-widget annotations with `/AP`, same flag — rendered all nine correctly the whole time, so the two files are the two halves of one experiment that had been sitting in the tree since T4 with nothing comparing them. **WHY THE GOLDEN SUITE WAS GREEN OVER IT, which is the part worth keeping.** `golden/ordinary/forms-acroform/` held three baselines of a page with no widgets on it — captured at T17 from the same renderer that could not draw them. **A comparison with oneself cannot see an absence**, and `golden/README.md` says exactly that in general terms; this is the concrete instance. It is **I-14's shape a second time**: the golden capture recording *nothing* for the one corpus file whose stated purpose is to have *something* — there font substitutions, here widgets. Both were caught by writing a case that asked a question the baseline could not. **Scope: settled with the user 2026-09-06 rather than decided inline.** `Architecture-FileMap.md`'s `pdf/render/PdfiumFormFill` row is Stage 9 and describes *live field appearances and widget states* — focus, typing, validation, the calculation cascade — and **none of that is in this fix**. What is here is the minimum that makes an ALREADY-COMPOSED `/AP` part of the page, which is what it is under ISO 32000 and what every conforming viewer draws. Stage 2's exit criterion is *every corpus file renders correctly* and M-03 grades missing content **S2**, so this is a Stage 2 defect rather than a Stage 9 deferral. **Fixed** by creating a read-only form-fill environment with the document (`FPDF_FORMFILLINFO` version 2, every callback null — a rasterizer answers none of them) and calling `FPDF_FFLDraw` after the page render, gated on the same `Annotations` flag because a widget IS an annotation. The `FORMFILLINFO` is a member rather than a temporary: PDFium keeps the pointer instead of copying, so a stack struct would leave it reading a dead frame. The handle is torn down **before** `FPDF_CloseDocument`, since the environment holds a reference to the document. | ✅ **Closed 2026-09-06.** All eleven widgets render — text, multiline, password, two check boxes, a three-button radio group, combo box, list box with its selection, and the push button. **The three baselines were regenerated with a stated reason** (D2-3's refusal is what makes that a record rather than a laundering); the golden suite failed on exactly those three and on nothing else — 266 of 267 assertions unmoved — which is itself the evidence that only this page's render changed. Regression case **A-85** and its control; `checks` clean; ctest **700/700** |
| **I-22** | S3 | **M-04's SETUP NAMED A DEFAULT THE PRODUCT NEVER SHIPPED AND A CHORD NOTHING BINDS — both written before the code that would have contradicted them.** Found 2026-09-12 while preparing the M-04 walk, by reading the two `.def` tables rather than the prose. **(a) The worker count.** §4's settings table gave `performance/renderWorkers` a default of **8** and M-04's *Pre* said "at 8" as though that were the state a walker would find. `core/settings/SettingsKeys.def` declares it **1**, range 1–16 — which is **I-05**'s answer arriving after this guide was written: the 8 was D10's pre-measurement *worker pool*, and T11 chose one render thread with a queue on 2026-09-04. A walker trusting the table would have walked every step at 1 worker while believing it was 8, and reported slowness that was a configuration. **(b) The chord.** Step 3 pressed `Ctrl+End` and `Ctrl+Home`. `core/cmd/KeyBindingDefaults.def:94-95` binds the **bare** `Home` and `End` to `NavFirstPage` / `NavLastPage` in the Viewer context (and again at 301–302 for FullScreen), matching [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference), which is the authority. An unbound chord is `NotFound` and **deliberately leaves the key unconsumed** (`ui/shell/ShortcutBinder.cpp:368`), so the walker would have pressed twice, seen nothing, and had a navigation failure to report that was not one. **WHY NOTHING CAUGHT EITHER, which is the part worth keeping.** A manual step is executed by no harness, so nothing re-derives a chord in prose against `KeyBindingDefaults.def` or a default in prose against `SettingsKeys.def` — and this guide is written **before** the stage's code by design, which means every value it predicts is a claim awaiting a re-check that the workflow does not schedule. It is §7's rule one section over: a wrong step tells the walker a non-finding is a finding, exactly as a wrong caveat row tells them a finding is expected. The other 24 modifier chords named in §6 were swept against the Viewer table in the same pass and all exist. | ✅ **Closed 2026-09-12** — both corrected in place (rule 8), not appended beneath: the table row now reads **1**, range 1–16, citing the `.def` that owns it; M-04's *Pre* says to set 8 and **relaunch**, since the key takes effect at the next launch only; step 3 reads `End`, then `Home`, with the authority cited; step 5 says to relaunch between counts. **M-04 itself is still ☐ not walked** |
| **I-23** | **S2** | **`Ctrl+Shift+T` COULD ENTER TWO-UP AND NEVER LEAVE IT — a select wearing a toggle's check mark, and the case that covered it pressed the chord once.** Found by the user 2026-09-12, by pressing it twice. [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference) calls the chord **Toggle Two-Up** and that table is the authority; `ViewTwoUp` was registered through `MainWindow.cpp`'s `registerMode` helper, whose handler is `setLayoutMode(mode)` with a **fixed** mode — so every press set `TwoUpContinuous`, and the second press set it again. **The second half of the same defect**: from paged `Single`, one press gave `TwoUpContinuous` rather than `TwoUp`, silently turning scrolling on as well — the chord was writing both axes of `LayoutMode` when it owns one. **WHY IT LOOKED LIKE A TOGGLE TO EVERYTHING THAT ASKED.** `registerMode` sets `checked`, so `isToggle` was true, `ActionFactory` made the ribbon control checkable, and the check mark tracked the mode correctly the whole time. **Reporting state is not the same as toggling**, and nothing in the product distinguished them — the comment above the helper called all three registrations toggles, which is how the wrong helper got used and stayed used for eight days (T14, 2026-09-04). **WHY THE SUITE WAS GREEN: convention 22, in its plainest form.** `tests/ui/tst_viewercommands.cpp`'s *"the layout chords select and toggle, and report which is in force"* pressed `Ctrl+Shift+T` **once**, asserted the mode, and moved on; the only chord it pressed twice was `Ctrl+Shift+H`, which was correct. The case title claimed both halves and the body tested one, and T14's exit criterion — *every §8 Viewer chord dispatches* — asked only for the half that was written. **A chord that fires is not a chord that toggles.** | ✅ **Closed 2026-09-12.** `ViewTwoUp` now has its own registration, the mirror of `ViewTogglePageMode`: it flips the two-up axis and **preserves the continuity**, so each chord owns one axis. `checked` asks `isTwoUp` rather than equality, because the control means *two-up is on* and both two-up modes are on. Two new cases in `tests/ui/tst_viewercommands.cpp` — one pressing the chord **twice from each of the four modes** (the round trip is the assertion), one checking the ribbon state in both two-up modes **with its negative control** in the two single ones. **Both were run against the unfixed handler first and fail there**, 7 assertions, every mode landing on `TwoUpContinuous` — convention 7, since a regression case that passes either way records nothing. `checks` clean; ctest **702/702**, one skipped. Nothing added to §6: this is automated now, and a manual case that could have been a test is a defect in the guide |
| **I-24** | **S2** | **A PAGED MODE TRAPPED THE READER ON THE PAGE: NOTHING THAT SCROLLED COULD CROSS A PAGE BOUNDARY.** Found by the user 2026-09-12, during the M-04 walk, by scrolling in `Single` and not arriving anywhere. `PagePresentation.cpp` lays out ONE page in `Single` and one spread in `Two-Up`, so the scroll range **is** that page; `PageView::wheelEvent` forwarded to `QAbstractScrollArea`, which reached the bottom of that range and stopped. There was no *past the bottom* to fall off. **The arrow keys were the same defect with a specification behind it**: [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference) gives them *"Scroll the page (paging at page boundaries, per S3)"* — two behaviours — and `KeyBindingDefaults.def` classified them `RAWPAGE_BINDING_NATIVE` with the reason *"scrolls the page surface; QScrollArea already does it"*, which is true of the first behaviour and silently false of the second. The row was classified on its first three words and the parenthetical was dropped. **That is I-08's shape again — a comment asserting a route is not the route existing** — and here the comment was half-right, which is why it read as settled. **WHY NOTHING CAUGHT IT.** `Page Down`, `Space` and `Ctrl+Page Down` all worked the whole time: `PageView::nextScreen` explicitly becomes a page step in a paged mode and has a case asserting exactly that. The suite drove the COMMANDS and never drove the GESTURE, so the one input path a reader actually uses in a paged mode was the one path nothing pressed. The wheel had no specification at all — `Features.md` §1.2 listed the four layouts and said nothing about how a reader leaves a page — so there was no document to contradict either. | ✅ **Closed 2026-09-12.** `PageView::scrollOrTurnPage` scrolls, and if the scroll was already at the limit **and the mode is paged**, turns the page and lands at the near edge of the new one — the far edge coming backwards, since `Up` at the top of page 5 landing on the top of page 4 would read as a jump back past everything just passed. The limit is detected by the scroll NOT MOVING, because `ViewportController::setScroll` clamps; computing a maximum instead would give the layout, the zoom and the device pixel ratio each a vote. The arrows stay `RAWPAGE_BINDING_NATIVE` — minting four permanent `ActionId`s for a scroll gesture is what D1-16 warns against, and `NavNextPage` is already the dispatchable form — but their reason strings now say what the widget does. **`Features.md` §1.2 gains the capability line**, since the wheel half was specified nowhere and a fix to an unwritten rule is just a preference. Two cases in `tests/ui/tst_pageview.cpp` press **twice at the edge** — once to arrive, once to cross — for the wheel and for the arrows, **plus a continuous-mode control** proving a notch at the END OF THE DOCUMENT still does nothing, which is what stops the fix from double-stepping every continuous document. Both paged cases were run against the unfixed viewer first and fail there (5 assertions); the control passes either way, which is its job. `checks` clean; ctest **705/705**, one skipped |

**NO S2 IS OPEN — I-17 and I-18 were raised and closed on 2026-09-06, in the CI run that first
executed Stage 2's build, and I-23 and I-24 on 2026-09-12 in the changes that found them. Two S3
remain — I-06 (JBIG2) and I-11 (line weight) — and both are open BY DECISION rather than pending.
**Twenty-two closed, re-counted from the table on 2026-09-12**: I-01 and I-03 before any code, I-05
at T11, I-07 at T12, I-08 and I-09 at T13, I-10 at T14, I-02, I-04, I-12 and I-13 at T16, **I-14,
I-15 and I-16 at T17**, **I-17 through I-21 all on 2026-09-06** — the CI run and the first three
manual walks — and **I-22, I-23 and I-24 on 2026-09-12**, one preparing M-04's walk and two during
it.**

**I-21 through I-24 are four consecutive findings that a green suite could not have made**, and they
failed in four different ways: I-21's baseline agreed with a renderer that drew nothing, I-22's prose
predicted values the code later contradicted, I-23's case asserted the half of a behaviour its own
title promised twice, and I-24's suite drove the COMMAND where a reader uses the GESTURE. Only the
third had a convention already written for it (**22**), which is the argument for walking a stage
rather than measuring it. **I-23 and I-24 are the same morning's walk**, which is worth stating
plainly: the two were found within an hour of a person using the viewer the way a reader would, and
neither is subtle once seen.

**I-14 and I-15 are both T17's own harness, raised and closed in the same change**, which is the
fourth task in this stage where the finding was the verification rather than the product. They are
worth reading as a pair because they failed in opposite directions: **I-15 went red on its first
run** — a control that could not fail, doing exactly what convention 7 says a control must do —
while **I-14 stayed green and recorded nothing**, and was found only by reading the data the tool had
produced and noticing that the one file expected to carry font substitutions carried none. The
second is the dangerous kind, and the only defence against it is looking at the output rather than
at the exit code. **I-16 is that same defence applied one level further out**: T17's own commit was
inspected, `golden/manifest.json` showed as `Bin`, and the trail led back to a four-day-old defect
inside the fix that was written to prevent it. A change is verified by observing the thing it was
meant to change — not by re-reading the change.

**I-06 and I-11 are the same finding twice, and the pattern is worth naming.** Both are a feature
this document set specified, that the engine or the licence cannot deliver, found by BUILDING it
rather than by reading about it — and both were kept as a declared-and-inert surface rather than
deleted, so that the day the constraint lifts there is a row to fill in and a case that says what
"filled in" would look like. Neither is a defect and neither should be triaged as one; what would be
a defect is either of them quietly doing something that looks like the feature.
**I-04 moved from T14 to T16 and was closed there** — the `unknown-key` rule and the eighteen keys it
found, which are the same eighteen a hand-diff had named four days and four tasks earlier. The re-tag
is the part worth keeping: T14 had no reason to touch `i18n-lint` and did not, and a task tag that
quietly passes is how an item becomes permanent.

**I-13 is the one to read before T17.** It is this stage's only S2 found in the PRODUCT rather than in
a document or a harness, it was found by a control nobody had asked for, and it would have moved every
golden baseline the next task is about to capture. Its shape — *the thing every file in the chain said
could not happen* — is I-10's from T14 one level down: a claim written by whoever had not yet built the
mechanism, repeated across four files until something executed it.

**I-08 is the one to read before T14.** It is not interesting as a bug — a declaration in the wrong
place, moved — but as a demonstration that **a boundary comment claiming a route is not the same as the
route existing**, which is convention 19's rule applied to a header instead of to a test. The comment was
written at T9 by someone who had checked that `PageTree` is a `core::` type and had not checked that
`ui/` is handed the interface rather than the class. It stayed true-looking for nine days because nothing
in `ui/` had any reason to ask. **T14 has the same shape waiting in it:** `repairReport()` is in exactly
the position `pages()` was, and the message bar is the consumer that will find out.

**I-05 was the one to read before T10 and T11, and it is now closed by them.** It was never a defect in code anybody wrote: it was a measurement that contradicted a decision two documents had already made, found by the probe D2-1 put at T3 precisely so that it would be found before `RenderScheduler` was built on top of it. Stage 1's lesson was that a spike's figure says nothing about the product; this stage's is that a spike can also say something the product's *design* had assumed away — **and that the design then has to be changed rather than worked around**, which is what T11 did.

---

## 9. Sign-off criteria

Stage 2 is done when **all** of these hold. This is
[`Stage2-Rendering-Plan.md` §13](./Stage2-Rendering-Plan.md) restated as a checklist.

1. ☐ A-01 … A-85 **run**, with actual output recorded against each case — not a summary claiming they
   were. Stage 1's **I-32** was exactly this gap.
2. 🟡 M-01 … M-10 **walked and signed off by you.** I do not mark a manual case. **Four of ten as of 2026-09-12: M-01, M-02, M-03 and M-04.** M-05 … M-08 and M-10 are walkable and unwalked; **M-09 is blocked** on a memory checker this machine does not have. Re-read the case headings rather than this line — each carries its own ✅ or ☐ and its own walk record.
3. ☐ `checks` clean — three gates, with the narrowed `FPDF_` rule **and its control**.
4. ☐ `ctest` green across all five Windows targets, every prior stage included.
5. 🟡 `portable-asan` clean over the whole corpus parse; `portable-fuzz` green on **two** targets
   that demonstrably exist. **Half done 2026-09-06 at T18**, and the half is the one that was
   missing: **a Linux fuzz job now exists**. Until T18, A-25 and A-26 were recorded as *"Linux
   jobs"* and there was no Linux fuzz job in `ci.yml` at all — a deferral whose owner did not
   exist, which is I-30's shape. Both targets now build, are asserted to have LINKED before being
   run, and run seeded from `corpus/malformed/` on every push. **The 10 minutes is deliberately not
   run per-push** (60 s each): a soak belongs on a schedule, and is Stage 3's to add. `portable-asan`
   is unchanged and still runs the corpus parse.
6. ✅ **Golden baselines committed 2026-09-06 (T17)** — 198 images, 56 corpus files, 66 pages, at
   96 / 144 / 192 DPI, with `golden/manifest.json` declaring what is captured and why, and
   `golden/captured.json` recording the engine and the fonts it was captured against.
   `tests/roundtrip/` **not applicable** and recorded as such: nothing in Stage 2 writes a byte.
7. ☐ **The corpus limitation is written into the sign-off, not merely known.** `Implementation-Plan.md`'s
   Stage 2 status block states in those words that the golden suite's green is **consistency against our
   own generated output**, not **fidelity to real-world PDFs** (plan §13.6). An unqualified *"every
   corpus file renders correctly"* overstates the evidence.
8. ✅ **A2 has a recorded verdict with numbers** — the probe's and the product's — in
   `Tech-Stack-Plan.md` §3.3, with risk **R5** struck through in the same change. **Probe
   2026-09-01:** `go` — p95 **0.532 ms** against 16.667 ms, 8 workers behind an engine lock;
   1,849 tiles/s against one worker's 1,769. Recorded with it: the probe's own limits — one page,
   one non-embedded font, no images or transparency — and **I-05**.
   **Product 2026-09-06 at T18: `go`, p95 14.18 ms against 16.667 ms — 1.18× headroom**, over a
   1920×1080 viewport at A2's own 288 dpi, through the shipped scheduler and cache, at a per-tile
   **cost** of 0.356 ms. **The two are different quantities and the gap between 31× and 1.18× is
   the entire reason D2-1 asked for both**: the probe times service inside the engine lock, the
   product figure times queued latency from `enqueue()` to delivery. The comparable pair is the
   probe's 0.532 ms and this run's 0.356 ms per-tile cost, and those agree — the engine did not get
   slower, the queue got counted. Also recorded there: a whole page at 288 dpi is **43.9 ms**, three
   frames, which is why tiles stream rather than pages and why prefetch is a Stage 4 subject.
9. ✅ **O7 and O10 resolved** in `Implementation-Plan.md` §8, both on 2026-09-06 at T18 and both
   by evidence rather than preference.
   **O7 — one SHARED cache.** Eight documents open at once, two passes each: at the shipped 256 MiB
   cap, **50 % hit rate and zero cross-document evictions**; at 24 MiB, **0 % hits and 297 of 297
   evictions cross-document**. Sharing is free until the cap is too small and catastrophic after,
   so **the lever is the cap and not the topology** — and the cap is already a settings row.
   `RenderCache::crossDocumentEvictions()` was built with the cache at T11 for exactly this, which
   is the only reason the question could be answered with a number.
   **O10 — the portable route, both presets, and NO second Windows toolchain.** What settled it is
   that the route stopped being a plan: `portable-fuzz` is a CI job as of this task. An
   `x64-clang-asan` preset would buy `platform/win/` and `ui/` coverage at the price of a second
   toolchain pin, which is what TA-1…TA-9 exist to prevent. The residual gap is **manual case
   M-09**, and it is named rather than closed.
10. ✅ **D2-14 answered 2026-09-06 at T18: the disk tier does NOT ship.** *(Added to this checklist
    at T18. The plan made the measurement a T18 deliverable and §13's checklist never listed it —
    a decision with an owning task and no sign-off box is one that can be quietly skipped.)* Same
    workload twice, differing only in whether a `DiskTileStore` is attached: **memory only, 0 %
    hits, 0.118 ms/tile; with disk, 50 % hits, 0.254 ms/tile.** The tier does exactly what a cache
    is supposed to do and makes the work **more than twice as slow** — because a tile costs about a
    quarter of a millisecond to re-render, and no disk round trip beats that. A second tier that
    raises the hit rate and the wall clock together is not a cache. `performance/diskCacheMB` stays
    at **0**, by evidence rather than by inertia, and the row, the code and the range all stay:
    a machine where rendering is far more expensive may still want it, and Stage 5 changes the
    workload.
10. ☐ **No open S1 or S2 in §8 — MET as of T16, 2026-09-05.** I-05 closed at T11, **I-02 at T16** (the closure measurement and the `text` split), and **I-13** — this stage's only S2 found in the product — raised and closed within the same run. **I-04** and **I-12** are S3 and closed at T16 too. The two S3 that remain, **I-06** (JBIG2) and **I-11** (line weight), are open BY DECISION rather than pending: each is a feature the engine or the licence cannot deliver, kept as a declared-and-inert surface so the day the constraint lifts there is a row to fill in.
11. ☐ `Architecture-FileMap.md` rows flipped and the plan's §10 debt paid;
    `Implementation-Plan.md` Stage 2 status block updated to reality, deviations included.
