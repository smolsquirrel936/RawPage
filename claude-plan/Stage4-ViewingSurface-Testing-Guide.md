# Stage 4 — Viewing and Navigation Surface · Testing Guide

> **Authoritative verification document for Stage 4.** Written from
> [`Stage4-ViewingSurface-Plan.md`](./Stage4-ViewingSurface-Plan.md) and
> [`Implementation-Plan.md` § Stage 4](./Implementation-Plan.md#stage-4--viewing-and-navigation-surface),
> **before any Stage 4 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** 🟡 **Written 2026-09-18, the same day as the plan and the day Stage 3 was signed off;
> partly run.** **83 automated** (A-01 … A-82, plus **A-66(a)**; A-66(a) came with T11's A2 named
> work — a lane no case in this file claimed) and **12 manual** (M-01 … M-12); **all 83 carry
> a result** as of 2026-09-26 (re-derived from this file: 83 case headers, none still boxed — the
> 81st is **A-80**, added 2026-09-24 with I-10's fix, and the 82nd and 83rd are **A-81** and
> **A-82**, added 2026-09-26 with the fixes for I-11 and I-12) — T1's
> corpus cases, T2's four A2 cases, T3's seven session cases, T4's four reader cases, T5's seven
> pane cases, T6's two plus the two re-reads T5 owed, **T7's five tool cases**, **T8's eight ruler,
> grid, guide and snap cases** (one of them, A-52, ✅ for its inversion half and 🟡 for the
> substitution it names — **I-07**), **T9's four split cases**, **T10's seven full-screen and
> presentation cases**, **T11's four arranger cases**, **T12's six** — A-67 … A-70 plus **A-11
> and A-29, both of which had been recorded amber or grey because the observable they were written
> against did not exist until T12 built it** — and **T13's five session-restore cases, A-75 … A-79**.
> and **T14's thirteen — the stage-wide sweeps A-01 … A-09 and the four censuses and re-measurements
> A-71 … A-74. The LAST to be filled in was A-04**, a `portable-asan` job this machine cannot run:
> MinGW GCC has no ASan runtime, so its result was **read from Linux CI's log on 2026-09-24** rather
> than produced here — run 35879213301, 609/609 with all 39 Stage 4 `core/` cases by name and no
> sanitizer line.
> **Two manual cases are walked.** **M-11**, by the user on 2026-09-24, passed and raised **I-10**.
> **M-01**, by the user on 2026-09-26, **failed on its first walk** — it raised **I-11 (S2)**, Fit
> Width drawing the landscape page at twice the window's width, and **I-12 (S3)**, a split with no
> visible divider — both were fixed the same day, and **the re-walk on the fixed build PASSED, the
> same day**. The other ten
> are the stage's largest remaining gap. §8 holds **twelve** issues and **only I-07 (S4) is open**;
> I-02, I-03, I-04, I-05, I-06, I-08, **I-01 and I-09 at T13**, **I-10 on 2026-09-24** and **I-11 and
> I-12 on 2026-09-26** are closed — **I-01 where this file said its owner would settle it, I-09 found
> by `x64-debug` after `x64-release` had reported green, and I-10, I-11 and I-12 each found by a
> person walking a case over a green automated half**.
>
> **Two halves, per [`CLAUDE.md`](../CLAUDE.md): [§5 Automated](#5-automated-verification--i-run-this)**
> — I run it and report the actual output — and **[§6 Manual](#6-manual-verification--you-run-this)** —
> you walk it on real hardware, and **I never mark a manual case passed on your behalf.**
>
> **The manual half is twelve cases, the largest of any stage so far, and that is a fact about the
> subject rather than a failure of the harness.** Stage 3 was bytes and bookkeeping and automated
> almost completely. Stage 4 is *what a person sees and reaches*: two monitors at different scaling,
> a presentation on the second one, a monitor unplugged mid-slide, a session restored onto a desktop
> that changed shape overnight, a 900-page thumbnails pane judged for smoothness, a guide dragged out
> of a ruler by hand. Every one of those is either hardware the harness does not have or a judgement
> no assertion makes. **Anything in §6 that could have been automated is a defect in this guide — say
> so and I will move it**, and the direction of travel is §6 shrinking into §5, as Stage 3's kill
> harness and byte comparison did.
>
> **Label scoping.** `A-nn`, `M-nn` and `I-nn` here are **this guide's**; Stages 0, 1, 2 and 3 have
> their own, and this guide cites theirs with the stage named — *"Stage 2's I-28"*. `A2` (no hyphen)
> is the rendering-performance **risk** in `Tech-Stack-Plan.md` §3.3. `T1`…`T14` are **Stage 4**
> tasks and `D4-n` Stage 4 decisions, both in `Stage4-ViewingSurface-Plan.md` (§6 and §3).

---

## 1. Scope

### In scope — must pass for sign-off

- **The document is navigable by every surface §1.4 promises for this stage**: a thumbnail, a
  bookmark, a named destination, a page number, a page *label*, and the view history.
- **The window is workable**: panes that dock, float and auto-hide with real content in them; splits
  including the spreadsheet freeze; rulers, grid, guides and snapping; reading mode; tile, cascade
  and merge.
- **Full Screen and Presentation on a chosen monitor**, with transitions, auto-advance, blank and the
  laser pointer — and the screen saver suppressed while presenting and released afterwards.
- **Session restore survives a monitor topology change**, and never resurrects an edit.
- **Nothing in this stage changes a document.** `mutation-lint` reports **one** command on the last
  day of the stage as it did on the first (D4-1).
- **Fit Visible fits the content** — Stage 2's **I-28**, closed here, asserted on the **zoom**.
- **The A2-at-Stage-4 verdict is recorded**, and `platform::IAcceleratedSurface` is answered rather
  than deferred again.
- **No regression** in any Stage 0, 1, 2 or 3 case.

### Out of scope — do NOT file these as bugs

Planned absences, itemized in [`Stage4-ViewingSurface-Plan.md` §11](./Stage4-ViewingSurface-Plan.md#11-explicitly-out-of-scope).

| Not a bug this stage | Arrives at |
|---|---|
| **A thumbnail cannot be dragged to reorder pages**, and `Delete`, `Ctrl+X`/`C`/`V`, `Alt+↑`/`↓`, `Ctrl+Shift+X` and `Ctrl+Shift+D` in the pane do nothing and leave the key alone (D4-1) | Stage 7 |
| **`Ctrl+R` in the pane rotates the CURRENT page or ALL pages** — the pane's selection is not yet a page set | Stage 7 |
| **A bookmark cannot be renamed, moved or deleted**; `F2`, `Delete`, `Ctrl+B`, `Tab`/`Shift+Tab`, `Alt+↑`/`↓` and `Ctrl+Shift+D` in the Bookmarks pane are unregistered (D4-2) | Stage 7 |
| **Dragging selects nothing** with the Selection tool | Stage 5 |
| **`Ctrl+4` (Reflow) and `S` (Snapshot) do nothing** and leave the key unconsumed (D4-15), and so does **`Ctrl+E`** (the contextual properties bar), which Stage 4 never listed | Stage 5, Stage 5, Stage 6 |
| **Nothing snaps to an object edge**, and there is no `units/snapToObjects` setting (D4-5) | Stage 6 |
| **Document Properties shows the Info dictionary and cannot edit anything; no XMP is shown** (D4-4) | Stage 8 |
| **Seven pane chords — `Ctrl+Shift+C`, `Ctrl+Shift+I`, `Ctrl+Shift+L`, `Ctrl+Shift+E`, `Ctrl+Shift+A`, `Ctrl+Shift+G` and `Ctrl+Shift+O` — do nothing**, and the sidebar lists no pane for them (D4-2) | per pane's stage |
| **Hairlines still ignore `pageDisplay/lineWeight`** — Stage 2's **I-11**, open by decision, a PDFium capability gap and not a Stage 4 defect | when an engine can honour it |
| **Ribbon key tips still stop at the tab level** — Stage 2's **I-34**, untouched by this stage | Stage 14's keyboard pass |
| Most of the **363** declared commands are still unregistered — 297 of them, re-derived 2026-09-21 (the catalog was 361 until T7 named the Loupe and auto-scroll) | per feature stage |

### What this evidence cannot tell us

- **`ui/` is outside every sanitizer this project has** (**O10**, `Implementation-Plan.md` §8), and
  Stage 4 is more `ui/` than any stage since Stage 1. `portable-asan` covers `SessionState`, the snap
  resolver and the three readers — the `core/` half — and nothing about the panes, the split or the
  presentation. Stage 2's manual **M-09** (Application Verifier over the raster path) is the shape of
  coverage that reaches `ui/`, and this guide's **M-06** re-uses it for the pane.
- **An offscreen widget run is not a screen.** Every `ui.` case runs under
  `QT_QPA_PLATFORM=offscreen`, where there is one virtual screen at one scale factor. Multi-monitor,
  mixed DPI and the fingerprint's real inputs are **structurally** unreachable there, which is why
  M-01, M-02, M-04 and M-05 exist and why `tst_sessionstate` tests the fingerprint over *synthetic*
  `MonitorInfo` values and says so at its assertions (convention 19).
- **Smoothness is not a number this project measures.** T2 measures p95 queued latency, which is the
  quantity A2 is graded on; whether a 900-page thumbnails pane *feels* smooth while scrolling is
  M-06's, and the two answers can disagree.

### Case anatomy

Every case is **ID · Goal · Steps · Expect · If it fails**, with a `☐` changed to ✅ or ❌. A case
either passes or yields a **specific** failure — "looks fine" is not a result.

### The rules this guide is written under

**Rule 1 — assert the EFFECT, not the intent** (Stage 2's **I-28**, convention 19). A fit mode is not
a zoom, a snap flag is not a snapped point, a selected row is not a scrolled view, and a computed
fingerprint is not a restored window. Where a case can only reach the intent, it says so at the
assertion and names what it therefore does not cover.

**Rule 2 — a repaired case is re-walked from step 1** (Stage 1's I-45).

**Rule 3 — a green tick is not a clean run** (Stage 2's I-38). Linux-only halves are read from the
CI job's **log**, never from its tick.

**Rule 4 — a widget case drives layout itself, and asserts a non-zero visible count first**
(convention 12). A pane that was never laid out has no visible rows, and every assertion about what
is visible passes vacuously.

**Rule 5 — a new mechanism gets a control written from what must NOT happen** (convention 22).
Snapping must not snap outside its threshold; restore must not resurrect a discarded edit; a
presentation must not leave the screen saver suppressed; a pane must not enqueue a document.

### Severity

| Sev | Meaning |
|---|---|
| **S1** | **A Stage 4 surface changes the document** — any byte on disk, any journal entry, any `Commands.def` row. The product shows a page that is not the document's page. A window restores where it cannot be reached, or a session restore loses or resurrects documents. A presentation leaves the machine unable to sleep or lock |
| **S2** | A documented behaviour is missing or wrong in a way a later stage will build on — a pane's selection and the view disagree, a snap resolves in the wrong order, a fit is the wrong zoom, a chord resolves in the wrong context, a pane starves the viewport |
| **S3** | A message is unclear, a figure is misreported, a layout is ugly at one scale factor, a diagnostic names the wrong thing |
| **S4** | Polish nobody outside the team would notice |

**An S1 here differs from Stage 3's.** There it meant the product changed the document in a way the
user did not ask for. Here the document is not supposed to change **at all**, so any change is the
S1 — and the second S1 class is new: **state the user cannot get back to**, a window off-screen or a
session that lost its documents.

---

## 2. Build, launch, environment

`cmake`, `ninja` and `g++` are **not on this machine's `PATH`**. Every command assumes
`scripts\env.ps1` has been dot-sourced first.

```powershell
. .\scripts\env.ps1
cmake --preset x64-release
cmake --build --preset x64-release --target checks     # four gates; mutation-lint must say ONE command
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure
ctest --preset x64-release -R "^ui\."                   # the widget suites, offscreen
ctest --preset x64-release -R "sessionstate|snap|outline|metadata"
.\build\x64-release\bin\rawpage.exe corpus\ordinary\outline-labels.pdf
```

**Three notes on running `rawpage.exe` from PowerShell**, carried from `CLAUDE.md` because they still
cost time: PowerShell does not wait on a GUI binary (use `Start-Process -Wait -PassThru`); an ordinary
launch runs an event loop; and `--dump-ui-strings`, `--list-actions` and `--print-screen-metrics` do
not claim the single-instance bus.

**Where things are written**, so a walker can look:

| What | Where |
|---|---|
| The session | `%APPDATA%\RawPage\session.json` — **unless a composition sets its own root**, which every test composition must (plan §5.6) |
| Settings | `%APPDATA%\RawPage\settings.ini` |
| Logs | `%LOCALAPPDATA%\RawPage\logs\` |
| Recovery journals | `%LOCALAPPDATA%\RawPage\recovery\` (Stage 3; **restore runs after the recovery prompt**, D4-9) |
| A manual walk's own files | `tests\manual\<case>\`, and nowhere else (`CLAUDE.md`) |

### Settings keys that affect these cases

**Defaults are owned by `core/settings/SettingsKeys.def`** — **re-read the `.def` rather than this
table** (Stage 2's I-26 was this table disagreeing with that file in four rows). The fifteen Stage 4
rows are listed in the plan's §4; these are the ones a case turns:

| Key | Affects |
|---|---|
| `general/restoreSession` (Stage 1, first read here) | A-75 … A-79, M-05 |
| `general/rememberPaneLayout`, `general/rememberWindowLayout` | A-78, M-10 |
| `units/rulerUnit`, `units/gridSpacing`, `units/gridSubdivisions`, `units/gridOffset`, `units/snapToGrid`, `units/snapToGuides` | A-45 … A-52, M-07 |
| `fullScreen/transition`, `fullScreen/autoAdvanceSec`, `fullScreen/loop`, `fullScreen/monitor`, `fullScreen/showCursor` | A-57 … A-62, M-02, M-03 |
| `pageDisplay/autoScrollSpeed` | A-44, M-12 |
| `pageDisplay/layoutMode` | A-53, M-01 — a split and a pane both read the layout |
| `performance/tileCacheMB` | A-14 … A-17 — T2's small-cap column |

### Test rig

| | |
|---|---|
| **Monitors** | **P-1** (✅): a 4K panel at **150 %** and a 2560×1440 at **100 %**, the rig A2 was specified on |
| **Removing a monitor** | **P-2** (✅ **satisfied 2026-09-19** — plan §7): **`Win`+`P` → *PC screen only***. The cable stays connected, so Windows keeps the display's identity — the monitor leaves `monitors()` and the fingerprint changes by its **set**, and the restored arrangement comes back with the same `id` and `scaleFactor`. **Not reached by this method:** a display whose identity itself changed (another port, another panel), which moves a monitor's `id` rather than the set; no case in §6 claims it |
| **Input** | **P-4** (answered 2026-09-19): a wheel mouse (✅), and **no pen or touch screen on this rig** — M-12's pen/touch step is therefore recorded *not run for want of hardware*, never as passed |
| **Independent readers** | Not needed this stage — nothing is written. ⛔ If a case in §6 makes you open another PDF reader, it is in the wrong guide |
| **Corpus** | **70 files since T1 (2026-09-19)**: the 67 Stage 3 files plus `ordinary/cropbox-margins.pdf`, `ordinary/dests-named.pdf` and `ordinary/transitions.pdf`, every one generated by a script we own with a `PROVENANCE.md` row. **65 golden cases and 5 excluded with a reason**, 231 baseline images |

---

## 3. Verdicts this guide must produce

A stage that is green but has no verdict is not signed off.

| Verdict | Assembled from | Recorded in |
|---|---|---|
| **A2 at Stage 4** — does the shipped software path hold the frame budget once a thumbnails pane, a split view and a presentation share one scheduler, one worker and one cache? Therefore: **is `platform::IAcceleratedSurface` needed?** | **Bench (T2):** A-14 … A-17, before the panes exist. **Product (T14):** A-72, through the shipped panes. **Judgement (M-06):** whether it is smooth to a person | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), beside A2's existing figures and **labelled as Stage 4's workload**; and `Architecture-FileMap.md`'s `WinAcceleratedSurface` row |

**The scale, fixed here before the measurement** so it is not graded against whatever is achieved.
The quantity is the **viewport's p95 queued latency** — the same quantity as Stage 2 T18's product
figure (13.03–13.20 ms against a 16.667 ms budget, ~1.27× headroom), so the two numbers are
comparable; the thumbnails' own latency is reported and is **not** what is graded, because a
thumbnail arriving late is a thumbnail, and a tile arriving late is a stutter.

| Result | Means |
|---|---|
| **go** | With a thumbnails pane streaming at `Speculative` and a second viewport live, the viewport's p95 stays **inside the 16.667 ms frame budget** at the shipped cache cap, and the cache's cross-document eviction count stays at **0** for one document's panes. `IAcceleratedSurface` is **not built**, and its row records this measurement as the reason |
| **go, with named custom work** | The p95 leaves the budget only under a named, reachable condition — a small cache cap, a 900-page pane, a split at 400 % — and a change in `ui/` or `core/render/` brings it back: a per-pane request cap, a coarser thumbnail scale, a pane that yields while the viewport is busy. **The work is named in the verdict and done in the stage**, and `IAcceleratedSurface` still is not built |
| **no-go** | The p95 leaves the budget under an ordinary workload and no scheduling change recovers it. **`platform/win/WinAcceleratedSurface` enters Stage 4's scope**, its row flips ○ → 🟡, and the panes are designed against it — which is why this is measured at T2 and not at T14 |

---

## 4. Traceability — exit criterion → case

| Exit criterion ([`Implementation-Plan.md` § Stage 4](./Implementation-Plan.md#stage-4--viewing-and-navigation-surface)) | Closed by |
|---|---|
| All four layout modes plus split views work | A-53 … A-56 for the splits; the four layout modes are Stage 2's suites, re-run by name in A-74; **M-01**, **M-08** |
| Rulers, grid, guides and snapping behave identically at 50 % and 400 % | A-45 … A-52, **M-07** |
| Full Screen and presentation work on a chosen monitor | A-57 … A-63, **M-02**, **M-03** |
| Window arrangement and session restore survive a monitor topology change | A-18 … A-24 (the fingerprint and the clamp), A-64 … A-66 (arrangement), A-75 … A-79 (restore), A-60 (a presentation's monitor disappearing), **M-04**, **M-05**, **M-09** |
| *(Plan D4-1)* The viewing surface changes no document | A-06, A-07, A-31, A-35, A-48, A-63, A-69 |
| *(Plan D4-10)* The pane does not starve the viewport | A-14 … A-17, A-32, A-72, **M-06** |
| *(Plan D4-14)* Fit Visible fits the content — Stage 2's **I-28** | A-11, A-67, A-68, **M-11** |
| *(I-10)* Fit Page fits the page a reader is SHOWN, not the sheet | **A-80**, and **M-11**, which found it |
| *(I-11)* A fit measures the page it was CHOSEN on, and a zoom command keeps the reader's place | **A-81**, and **M-01**, which found it |
| *(I-12)* A split's divider is visible against the desk, in both themes | **A-82**, and **M-01**, which found it |
| *(Plan D4-9)* Restore never resurrects an edit, and never doubles with recovery | A-76, A-77, **M-05** |
| *(Plan D4-12)* A presentation releases what it suppressed | A-58, **M-03** |
| *(§1.4)* Navigation by thumbnail, bookmark, destination, number and **label** | A-26, A-29, A-33, A-35, A-36, **M-01** |

---

## 5. Automated verification — I run this

Every case names its command and its pass condition. I report the **actual** output; a failing case is
reported as failing, with that output, and is never declared fixed without a re-run. **When a case is
ticked it carries the evidence it was ticked on** — Stage 1's I-32 was a guide whose summary claimed a
green its cases did not record.

### 5.1 Build, boundary, environment

**A-01 · Configure and build, both presets, from clean** ✅
**2026-09-23, T14.** `--clean-first` on both presets: **0 `warning:` and 0 `error:` lines in either
log**, `ui/` included, over **302 objects release and 291 debug** (re-counted from the built trees;
they were 232 each at Stage 3's sign-off, and the growth is this stage's). Clean rather than
incremental, because a built tree compiles nothing and prints *"ninja: no work to do"* — its warning
census would be inherited rather than measured, which is Stage 3 A-01's lesson and is why this case
takes twenty minutes instead of two.
**Goal** Stage 4's sources compile under the pin with `-Werror` in `core/`, `pdf/` and `ui/`.
**Steps** `cmake --preset x64-debug`, `--preset x64-release`; `cmake --build --preset <p> --clean-first` for each.
**Expect** Exit 0 on all four; **0 `warning:` and 0 `error:` lines in either log**, `ui/` included. **`--clean-first` is required** — an incremental build compiles nothing and its warning census is inherited rather than measured (Stage 3 A-01). **If it fails** S1.

**A-02 · The `ui/` warning grace list is still empty** ✅
**2026-09-23, T14.** `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake` is empty, and its
comment still says *"(empty — `ui/` has no code yet)"* — which is now false about the code and true
about the list. Stage 4 is the largest `ui/` stage since Stage 1 and added no entry.
**Steps** Read `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake`.
**Expect** Empty. Stage 4 is the largest `ui/` stage since Stage 1 and adds no legacy. **If it fails** S3 — and the entry's reason is recorded in the status block.

**A-03 · The headless suite is still headless** ✅
**2026-09-23, T14.** `gates.headless_assert.*` **7/7**, including the two negative controls — a
`ui/` edge and an `ops/` edge carrying PDFium are both still caught. `SessionState` and `core/Snap`
went into `core/` this stage and the assertion that `rawpage_tests` links no display and no engine
still fails configure if they had brought one.
**Goal** `SessionState` and the snap resolver are `core/`, and `core/` still links no display.
**Steps** `ctest -R "gates.headless_assert"`.
**Expect** Every case as before; `rawpage_tests` links no Qt GUI module and no engine. **If it fails** S1 — the portable half is where this stage's sanitizer coverage lives.

**A-04 · `portable-asan` builds and runs the Stage 4 `core/` suites** ✅
**2026-09-24, read from the LOG of CI run 35879213301's `portable-asan` job** (commit `a01192b`, the
T14 push of 2026-09-23; rule 3 — not the tick): *“100% tests passed, 0 tests failed out of 609”*, and
**all 39 cases of the five Stage 4 `core/` translation units present by name among its `Test #n:`
lines** — `tst_sessionstate` (12), `tst_fingerprint` (11), `tst_snap` (4), `tst_outline` (7) and
`tst_metadata` (5), each name taken from the source and matched against the log rather than
eyeballed. **0 `runtime error:`, 0 `ERROR: AddressSanitizer` and 0 `LeakSanitizer` lines** anywhere
in the job.
**The suite FILE names appear in that log as BUILD lines and not as run lines, and that is not a
shortfall** — the Expect below was written as though `tst_*` would appear in the run list, and it
cannot: Catch2 registers each case with CTest under its own NAME, so the log shows
*“[81/127] Building CXX object … tst_sessionstate.cpp.o”* and then the cases themselves. Matching all
39 names is the stronger reading of the same claim, and is what was done. **The Expect is corrected
in place** rather than left to be re-satisfied loosely by the next reader.
**Beside it, for §9 item 5's other half, read from the same run and from the nightly soak:** the
`portable-fuzz` job ran **three** targets — `fuzz_cos_lexer`, `fuzz_cos_parser`, `fuzz_cos_roundtrip`
— 61 s each, *“Done … runs”*, no crash; and `fuzz-soak.yml` run **35837723167** ran the same three
for 601–602 s each, no crash. The soak's head is `3f9cb01`, two commits before this one, and
`git diff --name-only 3f9cb01..a01192b` names **no file under `pdf/` or `tests/fuzz`**, so its claim
still holds over the tree this stage ends on — stated because a soak that predates the last two
commits would otherwise be evidence about a different tree.
**Goal** O10's coverage, applied to what this stage puts in `core/`.
**Steps** The `portable-asan` CI job. **Read the log, not the tick** (rule 3):
`gh run view <run> --log --job <the ASan job's id>`.
**Expect** The log names `tst_sessionstate`, `tst_fingerprint`, `tst_snap`, `tst_outline` and `tst_metadata` as run — **as their CASE names, which is the only form CTest gives them**; **0 `runtime error:`, 0 `ERROR: AddressSanitizer`, 0 `LeakSanitizer` lines**. **What it does not cover, and the case says so:** every pane, the split, the presentation and the arranger, which are `ui/`. **If it fails** S1 for a sanitizer finding; S2 if the suites are absent from the log.

**A-05 · The product still starts, and the startup census is unchanged where it should be** ✅
**2026-09-23, T14.** `--dump-ui-strings` exits 0 offscreen in both locales, **694 keys each, and
none resolves to itself** in either — the failure mode that would show a button reading
`action.file.open`. The censuses are where they should be: the platform bundle still **6 of 18**
(Stage 4 adds no OS interface — D4-17 settled that the window arranger needs none) and the backend
still **3 of 7** (the outline, destination and metadata readers are `IDocumentBackend` MEMBERS, not
new interfaces — D4-3), with the ribbon at **9 tabs and 200 controls**.
**Read it through `cmd` redirection, not PowerShell's pipeline**: `rawpage.exe` is a GUI-subsystem
binary, and `$out = & .\rawpage.exe` captures nothing. The first attempt at this case reported
*"694 keys, 0 self-resolving"* as *"0 keys, 0 self-resolving"* and would have passed a check for
zero. `CLAUDE.md` → *Commands* carries the trap; this is it met a second time.
**Steps** `$env:QT_QPA_PLATFORM="offscreen"; rawpage.exe --dump-ui-strings`; `--list-actions`.
**Expect** Exit 0; every `panes.*` and `shell.*` key resolved in `en` and `zh-TW` with **no key resolving to itself**; the platform census still **6 of 18** (Stage 4 adds no interface, D4-17) and the backend census still **3 of 7** (the three readers are `IDocumentBackend` members, not interfaces). **If it fails** S2.

### 5.2 The gates — and the one number that must not move

**A-06 · `checks` clean, and `mutation-lint` still says ONE command** ✅
**2026-09-23, T14.** Four gates, exit 0, and *"mutation-lint: clean -- 1 command(s):
RotatePageCommand; 1 table row(s); 132 test file(s) scanned"* — **the same one command the stage's
first day reported (T1, 2026-09-19)**, which is what makes this a measurement rather than a hope.
**D4-1 held for fourteen tasks**: three document panes, four tools, rulers, grid, guides, snap, a
split, a presentation, a window arranger, a fit, a properties dialog and session restore, and not
one of them constructed a `Command`.
**Goal** **D4-1's mechanical proof that the viewing surface mutates nothing.**
**Steps** `cmake --build --preset x64-release --target checks`.
**Expect** Exit 0; four gates; *"mutation-lint: clean -- 1 command(s): RotatePageCommand"*. **If it fails** S1 if the count is above one and a Stage 4 file is the cause — a view gesture became a document change; S2 otherwise.

**A-07 · `core/cmd/Commands.def` is byte-unchanged across the stage** ✅
**2026-09-23, T14.** `git diff d877992..HEAD -- core/cmd/Commands.def core/cmd/commands/` is
**empty** — `d877992` is Stage 3's sign-off commit, so the range is the whole of Stage 4. This is
the stronger form of A-06: A-06 says the gate counted one, and this says the table and the command
directory were never touched at all.
**Steps** `git diff <Stage 3 sign-off commit>..HEAD -- core/cmd/Commands.def core/cmd/commands/`.
**Expect** Empty. **The control:** a planted row makes A-06 report two commands, which is what proves A-06 reads what it claims. **If it fails** S1.

**A-08 · The layer and waiver census** ✅
**2026-09-23, T14.** `layer-lint --explain`: clean, **423 files, 3,354 includes, 14 escapes** — the
same fourteen filesystem-write waivers Stage 3 T9 counted, and no fifteenth. Stage 4 writes one new
file (`session.json`) and needs no waiver for it, because `core/settings/` already carries the
privilege. Four rules remain non-escapable (`fpdf`, `fpdf-handle`, `argv-path-read`,
`bare-qaction`).
**Steps** `layer-lint.exe --root . --explain`.
**Expect** Clean; **fourteen** active escapes, unchanged — Stage 4 writes no file of its own except through `core/settings/`, which is already allowed. A fifteenth is named and justified in the status block or removed. **If it fails** S2 for an unnamed waiver; S3 for a count that disagrees with `CLAUDE.md`, which is then corrected.

**A-09 · `i18n-lint` knows the new family, and no key is missing** ✅
**2026-09-23, T14.** `i18n-lint --explain` lists the families as
`action.* shell.* ribbon.* prefs.* error.* command.* panes.*` — **`panes.*` is there**, added for a
NAMESPACE rather than for a feature, which is Stage 3's **I-16** applied rather than repeated: the
full-screen and units strings shipped inside `shell.*` and `prefs.*` and got no family of their own.
The gate is clean over **263 files and 2 catalogs with 0 waivers**.
**Steps** `i18n-lint.exe --root . --explain`; a fixture referencing `panes.noSuchKey`.
**Expect** `keyFamilies` lists `panes`; the fixture fails `unknown-key` naming **that** key; every key Stage 4 references exists in `en` and `zh-TW`. **Do not add a family for a feature — add one for a namespace** (Stage 3's I-16, where a `recovery` family was required by a case and never existed because the strings shipped as `shell.recovery.*`). **If it fails** S2.

### 5.3 The corpus this stage is judged on

**A-10 · The three new files regenerate byte-identically and say what they claim**
✅ **2026-09-19, T1.** `--verify` → *"corpus check ok: 70 files regenerate byte-identically"*; `--check-provenance` → *"corpus check ok: 70 PROVENANCE.md rows, one per file"*; `--self-test` → *"5 of 5 controls fired"*, each naming the input that fired it. `git status corpus/` lists only the three new files and `make-ordinary.py`: **no existing corpus file moved a byte**, which is the half `--verify` cannot claim on its own — it compares the tree against the generators, not against what was there yesterday. Each new row says what its file carries, including the two deliberate faults (an empty page, a destination naming object 9999).
**Steps** `python corpus/make-corpus.py --verify`, `--check-provenance`, `--self-test`.
**Expect** All three exit 0; **70 files**, 70 `PROVENANCE.md` rows; every existing file byte-unchanged (`git status`). Each new file's row says what fault or feature it carries. **If it fails** S2.

**A-11 · `cropbox-margins.pdf` separates the three rectangles**
✅ **Closed 2026-09-23 at T12**, when the observable it was written against finally existed. `tests/render/tst_contentbox.cpp` reads all three rectangles through `PdfiumDocument::pages()` and `PdfiumDocument::contentBox(0)` and asserts each **strictly inside the last by at least 36 pt**, which the fixture's 72 pt clears twice over. The amber note below is kept rather than replaced, because *why a case could not run* is the part that goes stale silently.
🟡 **2026-09-19, T1 — the fixture is built and measured; the CASE is owed to T12 and this box stays unticked.** From the bytes: `/MediaBox [ 0 0 595.276 841.89 ]`, `/CropBox [ 72 72 523.276 769.89 ]`, ink confined to `144 160 451.276 681.89` — **72 pt between each rectangle and the next on every side, twice the 36 pt this case requires**. That the engine honours the crop is measured rather than assumed: `golden-update --survey` reports the page as **602×931 px at 96 dpi**, which is the CropBox, not the 794×1123 sheet. **Why it is not run:** these Steps name a `render.` case reading a **content box**, and `core/backend.h` has no box accessor of any kind until **T12** builds one (D4-14) — so the observable this case is written against does not exist yet, and a tick here would be the intent-for-effect substitution §1 forbids. *(The ink claim was checked against the RENDERED baseline, not the generator: the first capture showed body text spilling across the whole sheet — see the status block's T1 deviation (b).)*
**Goal** The fixture Stage 2's **I-28** said did not exist.
**Steps** A `render.` case reads the page's media box, crop box and content box.
**Expect** **Three different rectangles**, each strictly inside the last, with at least 36 pt between the content and the CropBox and between the CropBox and the MediaBox. **If it fails** S2 — a fixture that does not carry its fault tests nothing (Stage 3's A-17).

**A-12 · `dests-named.pdf` carries both destination forms, and one that dangles**
✅ **Closed 2026-09-19 at T4.** Raised amber at T1, when the fixture existed and the reader did not: the bytes carried a `/Names /Dests` root with `/Kids`, two kid nodes with `/Limits` and `/Names` in byte order, the old-style catalog `/Dests` beside it, `/XYZ` + `/Fit` + `/FitH`, and `9999 0 R` exactly once. **T4 built the resolver these Steps call and ran it** — `/XYZ` to page 0 at (72, 720), `/Fit` to page 1, `/FitH` to page 2 at top 640, and `ghost-page` present with `pageIndex` −1 and `unresolvedCount()` 1, the reader having carried on past it. The case's own record and its control live at **A-26**, which is where the reader is the subject; this entry is about the FIXTURE and is now met on both halves. **What neither half proves**, at the assertion: which of the two PDF tables a given name came from — PDFium merges them, so that claim rests on the bytes and `PROVENANCE.md`.
**Steps** A `unit.`/`render.` case over the file.
**Expect** A `/Names /Dests` name tree **and** an old-style `/Dests` dictionary; `/XYZ`, `/Fit` and `/FitH` destinations resolving to a page index and a point; **one destination naming a page that does not exist**, reported and not resolved — the control. **If it fails** S2.

**A-13 · `transitions.pdf` carries per-page transitions and a document-level presentation hint**
✅ **2026-09-19, T1.** Both halves are readable without a reader that does not exist yet. Bytes: `/Trans << /S /Wipe /D 1 /Di 0 >>`, `/Trans << /S /Dissolve /D 0.5 >>` and `/Trans << /S /Split /D 1.5 /Dm /V /M /O >>` on pages 1, 2 and 3; `/Dur 3` and `/Dur 5` on pages 1 and 3; `/PageMode /FullScreen` once in the catalog; **page 4 carries neither key**, which is the control this case's own subject needs. Manifest: `golden-update --check-manifest` → *"70 corpus files, 65 with baselines and 5 excluded with a reason"*, with this file a case at page 0.
**Expect** Three `/Trans` kinds on different pages, a `/Dur`, and `/PageMode /FullScreen`; the golden manifest lists the file as a case or an excluded row with a reason. **If it fails** S3.

### 5.4 A2 at Stage 4 — the measurement, before the panes (T2, D4-16)

**A-14 · The viewport's p95 under a thumbnail stream**
✅ **2026-09-19, T2.** `rawpage_bench_pipeline.exe --stage4`, three runs, shipped 256 MiB cap, the viewport's p95: **`viewer` 9.78–9.82 ms → `+ split` 9.91–9.99 → `+ pane` 10.59–10.74 ms** against the 16.667 ms budget, about **1.55× headroom**; per-tile cost 0.33–0.45 ms. Cache: 618 lookups, **29.6 % hits, 0 evictions, 0 CROSS-DOCUMENT** at the shipped cap; at 24 MiB the same workload is **9.98 ms** with 394 evictions and 109 cross-document. The thumbnails' own p95 (35–46 ms) is reported and **not** graded, per §3. The bench's phase 0 latency control passed first, so these are readings rather than a dead instrument's arithmetic.
**Goal** §3's verdict. The quantity is the **viewport's** queued latency, comparable with Stage 2 T18's.
**Steps** `rawpage_bench_pipeline.exe --stage4` over `pages-900.pdf` and `mixed-page-sizes.pdf`, at the shipped `performance/tileCacheMB` and at a small cap.
**Expect** A reported p95 per column, with the cache's hit rate, evictions and cross-document evictions. **The bench never fails on speed** — it reports, and §3's scale grades. **If it fails** S2 only if the measurement could not be made.

**A-15 · A second viewport is in the mix**
✅ **2026-09-19, T2 — and the first instrument was wrong about what a split IS.** Both viewports enqueue at `Visible` (the bench prints `enqueued`/`started`/`delivered`, and the thumbnails are the only `Speculative` requests), and the reported p95 covers both. **The correction:** the first version gave each pane a full 1920×1080 viewport and called the result the split's cost — but `SplitViewContainer` divides ONE window between its panes (D4-13), so two panes are 1920×540 each and the visible pixel count does not change. Modelled correctly the split costs **+0.09…+0.19 ms**; modelled as two full viewports it costs **+8.25…+8.60 ms**, which is a real workload — two WINDOWS on two monitors (D4-17) — and is kept as its own column rather than deleted. The verdict is graded on the first and bounded by the second.
**Expect** The split's second viewport's requests are issued at `Visible`, not `Speculative`, and the reported p95 covers both. **If it fails** S2 — a measurement of one viewport is not the workload this stage creates.

**A-16 · The thumbnail stream is `Speculative`, and strict priority holds**
✅ **2026-09-19, T2.** Every thumbnail request is enqueued at `Speculative` and every viewport tile at `Visible`, with the pane's 30 slots enqueued FIRST each round so the scheduler actually has to choose. Inversions — a speculative tile delivered while a visible tile is still outstanding — are **4 to 7 against a bound of 8**, one per document per round, which is the one job that can already be inside the rasterizer when the viewport's tiles arrive (workerCount 1). **Cache-served speculative tiles are excluded**, measured exactly through `servedFromCache` either side of each enqueue, because a tile that never reached a worker stole nothing; 46–90 were excluded that way. `droppedOverflow` **0** in every column, `priorityRaised` 0, `undelivered` 0. **What this does not cover** (convention 19): reordering among cached deliveries, which costs no worker time and which no case here claims to catch.
**Steps** The bench prints the scheduler's own counters (`RenderScheduler::Stats`).
**Expect** Every thumbnail request at `Speculative`; **no visible request completes after a speculative one it was enqueued before**; `droppedOverflow` is reported. **If it fails** S1 for a priority inversion — the pane would starve the page.

**A-17 · The verdict is graded on §3's scale and recorded**
✅ **2026-09-19, T2 — `go, with named custom work`.** Recorded in [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified) as *A2 at Stage 4*, labelled as this stage's workload, and `WinAcceleratedSurface`'s `Architecture-FileMap.md` row **stops deferring and says what was measured: not built**. Against §3's scale, fixed before the measurement: the `go` row's two conditions are met for the workload D4-16 names (p95 inside the budget at the shipped cap, cross-document evictions 0), and the middle row is what the two-window column earns — it leaves the budget under a named, reachable condition, and the named work is **T11's**: *a window that does not have focus enqueues its viewport at `Prefetch`, not `Visible`*. **`IAcceleratedSurface` is not built either way, and the reason is structural** — a surface presents finished pixels and does not rasterize them, while every millisecond measured is PDFium behind I-05's engine lock: re-measured the same day, 1/2/4/8 workers give p95 13.199/14.078/14.093/13.808 ms, so more threads buy nothing. **T5's design is unchanged by this verdict**, which is what measuring at T2 was for.
**Expect** A written verdict — `go`, `go with named custom work`, or `no-go` — in `Tech-Stack-Plan.md` §3.3 beside A2's figures, labelled **Stage 4's workload**, and `WinAcceleratedSurface`'s FileMap row answered. **A no-go changes T5's design before T5 is written.** **If it fails** S1 — *A2 at Stage 4 cannot be judged* is not the same as a pass.

### 5.5 `SessionState` — headless, and the fingerprint (T3)

**A-18 · A session round-trips**
✅ **2026-09-19, T3.** `unit.` — three documents in one window through capture → `save()` → `load()` → `restore()`, asserted **field by field** rather than by count: tab order by filename in order, active tab, geometry, maximized, pane layout, and per document the layout mode, the fit intent, the zoom, the page index, both scroll offsets, all four `DocumentId` fields, and **both guides with their orientation, position, page and locked flag**. A document with no guides comes back with none. The enums round-trip as their NAMES (`layoutModeName`, `fitModeName`), not as numbers, so inserting a value into either enum cannot silently reinterpret an old file.
**Expect** Three documents, tab order, active tab, per-window geometry, per-document layout mode, zoom intent, scroll position and guides survive capture → write → read → restore, field by field. **If it fails** S2.

**A-19 · The fingerprint is stable, and sensitive to what it must be**
✅ **2026-09-19, T3.** `unit.` over synthetic `MonitorInfo` values — and the case says so at the top of the file, naming what it therefore does **not** cover: that `WinDisplayTopology` reports what Windows believes, which is M-04's and M-05's on the P-1 rig. Identical arrangements hash identically **whatever order they were enumerated in**; a moved monitor, a changed resolution, 150 %→125 %, the primary flag moving, a monitor going away (P-2's `Win`+`P`) and a different device at the same position each change the hash; **`friendlyName` and `physicalDpi` do not**. An empty topology hashes stably rather than crashing. **Controls run red:** including `friendlyName` in the hash, and removing the sort, each turn the matching case red.
**Goal** D4-8, over **synthetic** `MonitorInfo` values — the case says so, and names what it therefore does not cover: a real topology, which is M-05's.
**Expect** Identical arrangements hash identically whatever order `monitors()` returned them in; a changed `scaleFactor`, position, pixel size, `primary` flag or monitor count changes the hash; **`friendlyName` does not** (it may be empty and is not unique — `platform.h`). **If it fails** S2.

**A-20 · An unknown arrangement falls back to the most recent**
✅ **2026-09-19, T3.** Geometry saved under *desk*, restored under a fingerprint never seen, comes from *desk* with `usedFallbackArrangement` set. **And the control the criterion does not ask for** (convention 22): with TWO arrangements stored, restoring a KNOWN one uses that one and **not** the most recent — without which "falls back" and "always uses the newest" are the same code. Both controls run red: deleting the fallback branch, and making the lookup never match.
**Expect** Geometry saved under arrangement A, restored under unknown arrangement B, comes from A rather than from nothing — and is then clamped (A-21). **If it fails** S2.

**A-21 · Every restored window is clamped onto a desktop that exists**
✅ **2026-09-19, T3.** A session captured across a 5760×2160 desktop — one window at x = 4200, another at x = 2000 — restored onto a synthetic 1920×1080: **every** window ends wholly inside the one screen, keeping its size where it fits, and a window larger than the desktop is shrunk onto it. The title-bar cases are asserted directly: a window whose title bar is above the desktop is brought down, and one with a 10 px sliver on screen is pulled back — `kMinimumVisibleWidth`, because *intersects at all* is not something a person can grab. **The control:** a window already on screen is not moved by a single pixel, and one **straddling two adjacent screens** is left alone. **What the control does not test, measured rather than assumed:** planting a bug in the clamp's early return leaves the already-on-screen case GREEN — for a contained window the fallback arithmetic is a no-op, so that assertion is about behaviour two paths satisfy, and the straddling case is the one that holds the branch up. Both facts are recorded at the assertions.
**Goal** The S1 this stage most plausibly ships: a window nobody can reach.
**Steps** Restore a session captured on a 5760×2160 virtual desktop onto a synthetic 1920×1080 one.
**Expect** Every window's rectangle **intersects** an available screen by at least a stated minimum, and its title bar is on-screen. **The control:** a window already fully on-screen is **not moved by a single pixel**. **If it fails** S1.

**A-22 · The arrangement store is bounded**
✅ **2026-09-19, T3.** The case reads `SessionState::kMaxArrangements` **from the code** rather than repeating a number, captures four more arrangements than that, and asserts the count is the bound, the newest is at the front, and the oldest is gone. Seeing an arrangement again **moves** it to the front with its new geometry rather than duplicating it. **Control run red:** removing the bound.
**Expect** Remembering N arrangements evicts the least recently used and never grows without limit; the bound is read from the code, not from this guide. **If it fails** S3.

**A-23 · A document that no longer exists is dropped with one report**
✅ **2026-09-19, T3.** With the middle document's file absent, restore reports that path **once**, returns the other two in order, and the next capture of what restore produced does not carry it. The active-tab index pointed AT the dropped document and is answered in `SessionState` rather than left to the shell. **The control:** with every file present nothing is dropped and the active tab is unchanged. **Control run red:** dropping the path silently. Existence is a predicate parameter, so the case needs no disk — the host answers a question of fact and the domain rule stays in `core/` (convention 11).
**Expect** Restore reports the missing path **once**, opens nothing for it, and the next capture does not carry it. **If it fails** S3; S2 if it is reported on every launch.

**A-24 · A corrupt or future session file is ignored, not fatal**
✅ **2026-09-19, T3.** Five inputs, each its own section: no file at all (the first launch — **not** reported, because nothing was wrong), truncated JSON, an empty file, `"format":99` from a newer build, and a directory where the file should be. Each loads nothing, reports once, and leaves the object usable; restoring from a state that was never loaded returns an empty session rather than crashing. A further case writes a good session over an unusable file and reads it back, which is the *"replaced on the next clean quit"* half. A future format is **ignored rather than guessed at**: a newer build's fields are not knowable from here.
**Steps** Truncated JSON, a schema version above ours, an empty file, a directory where the file should be.
**Expect** The product starts with no session restored and one log line each; the file is replaced on the next clean quit. **If it fails** S1 for a failure to start.

### 5.6 The read-only models (T4)

**A-25 · The outline is read with its structure and its open state**
✅ **2026-09-19, T4.** `render.` over `ordinary/outline-labels.pdf`: two roots, **three nodes over two levels**, and the parent/child relationship asserted by name — an outline read as a flat list of three would satisfy the count and fail a user. Each node's destination resolves to a page INDEX (3, 4 and 7) with its fit kind, and the nested section's `/XYZ null 780 null` keeps `hasY` true with `hasX` and `hasZoom` **false**, because null is not zero: conflating them scrolls every such link to the page's bottom-left corner. The open flag is what the FILE asks for (ISO 32000-1 Table 153, a positive `/Count`). **Controls run red:** forcing `openByDefault` false, and never walking the children.
**Steps** `tst_outline` over `ordinary/outline-labels.pdf`.
**Expect** Two levels, the parent/child relationships, the open/closed flag, and each node's destination resolved to a page index. **If it fails** S2.

**A-26 · Destinations of three kinds resolve, and a dangling one is reported**
✅ **2026-09-19, T4 — and this is T1's A-12 paid off.** `render.` over T1's `ordinary/dests-named.pdf`: `/XYZ` gives page 0 with the point (72, 720), `/Fit` gives page 1 and nothing else, `/FitH` gives page 2 with top 640. **`ghost-page` is present in the table and unresolved** — `pageIndex` −1, `unresolvedCount()` 1 — and the reader did not stop at it, which the entries read after it prove. **Control run red:** dropping unresolved entries silently. **What this does not prove**, at the assertion: WHICH of the two PDF tables each name came from — PDFium merges the name tree and the flat `/Dests` dictionary into one enumeration and the facade keeps that merge, so a file carrying only the tree would satisfy every assertion here. That `dests-named.pdf` carries both forms is asserted over its BYTES, by T1's corpus check and `PROVENANCE.md`.
**Steps** `tst_outline`/`tst_metadata` over `dests-named.pdf`.
**Expect** `/XYZ` gives a page and a point, `/Fit` a page, `/FitH` a page and a top; **the dangling destination is reported and resolves to nothing**, and the reader does not stop at it. **If it fails** S2.

**A-27 · A document with no outline and no destinations answers empty**
✅ **2026-09-19, T4.** `render.` over `ordinary/text-a4.pdf`: `outline()` and `destinations()` are **empty, not null and not a failure**. `core/backend.h` makes the distinction explicit — null means *not a handle of mine*, empty means *this document has none* — so a pane can show its empty state without an error to distinguish. The pane half is T6's.
**Expect** `outline()` and `destinations()` on `text-a4.pdf` return an empty model, not a failure, and the panes show their empty state rather than an error. **If it fails** S3.

**A-28 · Info is read; XMP is not claimed**
✅ **2026-09-19, T4**, over all three inputs the case asks for. **With `/Info`:** `ordinary/text-a4.pdf` — title, producer and both dates read, the dates **raw as stored** (`D:20260831000000Z`, the corpus's one fixed instant), and the PDF version **1.7** from the file header rather than from any Info field. **Without:** `corpus/minimal.pdf`, Stage 0's seed, carries no `/Info` at all — `isEmpty()` is true and the version still reads, since every PDF has a header. **With a malformed date:** no corpus file has one and T1's corpus is closed, so the document is built in BYTES in the case and opened through `openBytes` — `(Tuesday afternoon)` comes back **exactly as written**, and `Metadata::parseDate` refuses it rather than inventing a date. The `unit.` half exercises the parser over the forms producers write and the ones they mangle, with a control that it is not merely lenient. **No XMP is read, and the case says so** (D4-4): there is no API on `core::Metadata` that could answer an XMP question, which is the mechanical form of the same claim. **Control run red:** the title never read from the file.
**Steps** `tst_metadata` over a file with `/Info`, one without, and one whose `/Info` holds a malformed date.
**Expect** Every Info field read as text; a missing `/Info` is empty rather than an error; a malformed date is shown as it was stored rather than invented. **The case states that no XMP is read** (D4-4). **If it fails** S3; S2 for a field invented.

**A-29 · Page labels, and the page box that reads them**
✅ **2026-09-23, T12**, in `tests/ui/tst_properties.cpp` over a fake document shaped like `outline-labels.pdf` — roman front matter then arabic body from index 3, so **the fourth page is called "1"**. The status bar's box shows the **label**, and the total carries the physical index beside it (`of 8 (page 4)`) — and does not when the label IS the physical number, where *"of 8 (page 8)"* would be noise. *Go to Page* resolves `iii` to index 2 and `A-2` to index 7, neither of which an integer parse could ever reach. **`1` goes to index 3**: the label wins over the physical number, which is a decision with a cost stated at `StatusBar`'s declaration — the box DISPLAYS the label, so typing what was just read must land where it was read, and the physical page whose number collides with a label is then reachable only by its label. **What the case asserts that this guide's wording did not anticipate:** `iv` and `A-12` **are not in `outline-labels.pdf`** — its front matter stops at `iii` and it carries no prefixed label — so what is proved is the half that matters, that a label-shaped input is ACCEPTED as a label query and REFUSED without scrolling rather than parsed as a number or silently ignored. The control is a document with no labels at all, where typing `5` still goes to physical page 5, which is every document before this task and most after it.
⚪ **Not T4's, and not owed by it — recorded here so its ☐ is not read as a gap.** Page labels are already read into `core/doc/PageTree` (Stage 2), and everything this case asserts is a SURFACE: the status bar showing the label beside the physical index, and *Go to Page* accepting `iv` and `A-12`. The plan's §6 gives both to **T12**, which is where this case will be run.
**Goal** §1.4's *jump by logical page label*, on the file `PROVENANCE.md` says exists for it.
**Expect** Over `outline-labels.pdf`, index 3 is labelled **"1"**; the status bar shows the label and the physical index; *Go to Page* accepts `iv` and `A-12` as well as a number, and an unknown label is refused without moving the view. **If it fails** S2.

**A-30 · The readers are hydrated once, at open**
✅ **2026-09-19 at T4 by construction; the pane half run 2026-09-21 at T6.** Each reader is called exactly once, in `PdfiumDocument::open`, and the models are plain `core/` values held on the document; `IDocumentBackend::{outline,destinations,metadata}` return a pointer to what was already read and touch no engine. **A pane cannot re-read on show even if it tried**: there is no API that would let it — the `FPDF_DOCUMENT` never leaves `pdf/render/`. **The pane half, run at T6:** with the Bookmarks and Destinations panes built, showing and re-showing them five times each reads **neither model again** — the fake backend counts every `outline()` and `destinations()` call, and both counts are unchanged from the moment the document opened. The models are fetched when the DOCUMENT IN FRONT changes (`MainWindow::updateDocumentPanes()`), and selecting a pane is not that. The tree is also asserted still to hold its five nodes, because a pane that had quietly rebuilt itself from nothing would pass the counter and fail that.
**Expect** Opening a document calls each reader once; opening the Bookmarks pane on an already-open document reads nothing from the engine. **If it fails** S3 — a pane that re-reads on every show is a cost nobody measured.

### 5.7 The panes (T5, T6)

**A-31 · The Thumbnails pane declares its context, and its chords resolve from a KEY EVENT**
✅ **2026-09-20, T5.** `ui.` — `activeContext()` answers `PageThumbnails` through the WINDOW'S OWN binder with the focus in the pane, and every chord below is a real `QKeyEvent` sent to the focus widget, not an `activate()` call on a chord. **`Enter` is asserted on the EFFECT** (rule 1): the VIEW moved to the selected page, not that `page.goToSelected` dispatched. The four arrows move the selection and **leave the view where it was** — the control that separates *move the selection* from *navigate*. **`Ctrl+R` is consumed by Stage 3's `page.rotate`** in this context and the pane constructs nothing; what that does NOT cover is stated at the assertion — the dialog is refused offscreen (convention 16), so the rotation itself stays `tst_mutationcommands`' A-75. **The control found I-04** (§8): two of the nine Stage 7 rows were consumed on its first run.
**Goal** `KeyContext::PageThumbnails` has been reachable in tests only since Stage 3 T10 (D3-13).
**Expect** With focus in the pane, `activeContext()` is `PageThumbnails`; `Enter`, `+`, `-` and the arrows do what §8 says, sent as key events to a shown, laid-out widget (rule 4); **`Ctrl+R` dispatches Stage 3's `page.rotate`**, and the pane itself constructs nothing (D4-1). **If it fails** S2.

**A-32 · The pane is a viewport, not a document**
✅ **2026-09-20, T5.** `ui.` over a 900-page document, laid out in a 1200×900 window: the pane enqueues the **visible slots plus one screen of margin each way** and the census is bounded by 3× the visible count and by 60 absolutely — never 900. Every visible slot IS among them, so the bound was not met by asking for less than is on screen. **The control is a second document**: twelve pages and nine hundred cost the same, which is the assertion a pane that asked for the document would fail. A long scroll drops the pages that left (the first page before it is gone from the set afterwards) and the bound still holds after the scroll. **What it does not cover, stated at the case:** the priority as the SCHEDULER recorded it — `RenderTicket` carries none back — which is T2's bench (A-16), instrumented at the enqueue over the same scheduler.
**Goal** D4-10's bound.
**Steps** Open `pages-900.pdf`, show the pane, record every enqueued request.
**Expect** Only the visible thumbnail slots plus the stated margin are enqueued, **all at `Speculative`**; scrolling the pane cancels what left and enqueues what arrived; **the control** — the count does not grow with the document's page count. **If it fails** S1 for 900 requests; S2 for a wrong priority.

**A-33 · Clicking a thumbnail moves the view, and the view moves the selection — without a loop**
✅ **2026-09-20, T5.** `ui.` — a counter on the pane's gesture signal asserts **exactly one round per gesture**, in both directions: activating a thumbnail moves the view to that page and the pane's selection follows, with the count still 1; and `goToPage` from the view moves the pane's selection with the count still **0**. The termination is structural rather than guarded — `activated()` is emitted only by a gesture and `setCurrentPage()` emits nothing — so there is no window in which a flag could be missed.
**Expect** A click scrolls the view to that page and records a view-history entry; scrolling the view updates the pane's selection; **neither triggers the other a second time** (a counter asserts exactly one round per gesture). **If it fails** S2 — a feedback loop here is a scroll that fights the user.

**A-34 · Thumbnail size, and its bounds**
✅ **2026-09-20, T5.** `ui.` — `+` and `-` step the size from a key event and return to where they started. The bounds are **walked** rather than checked at one step (a clamp in `setThumbnailWidth` and forgotten in `larger()` would pass a single step): twenty steps land exactly on the maximum, a twenty-first does not move it, and the command reports itself **unavailable** at the bound rather than refusing after the press (convention 16). The size survives closing and reopening the pane **in one session**; surviving a restart is `SessionState`'s at T13, and D4-5 declares no key for it. A third case asserts that a new size **re-keys** every request — `deviceScale` and the tile width both grow — rather than scaling what is held, which would be a column that never sharpens (D2-12).
**Expect** `+`/`-` step the size between a minimum and a maximum; the size survives closing and reopening the pane in one session; a size at the bound does not step past it. **If it fails** S3.

**A-35 · The Bookmarks pane navigates, and edits nothing**
✅ **2026-09-21, T6.** `ui.` over a two-level tree with the file's own open state — Chapter One open, Chapter Two closed, so **four rows are drawn of five nodes**, which is the assertion that catches a pane expanding everything and throwing away what the author said about their own structure. `Enter` is asserted on the EFFECT (rule 1): the view moved to the selected bookmark's page, not that `bookmark.goTo` dispatched. `→`/`←` expand and collapse the selected node from a key event and the visible row count moves by one; `*`/`/` take it to five and back to two. **The three behaviours of one table are all exercised**: `→`/`←` are registered commands here (unlike the thumbnails pane, where the same physical keys are `WidgetNative`), and `↑`/`↓` appear in no Bookmarks row at all, resolve to nothing, and reach the tree, which walks the selection natively. **The control** presses all eight editing rows — `F2`, `Delete`, `Ctrl+B`, `Tab`, `Shift+Tab`, `Alt+↑`, `Alt+↓`, `Ctrl+Shift+D` — requires each unregistered and each key **unconsumed**, and then asserts the tree is unchanged and the document clean. **It found I-04 a second time** (§8): `Alt+↓` moved the selection, because `QAbstractItemView` walks on an arrow without looking at the modifiers.
**Expect** `Enter` and a click go to the destination; `→`/`←` expand and collapse; `*`/`/` expand and collapse all. **The control:** `F2`, `Delete`, `Ctrl+B`, `Tab`, `Shift+Tab`, `Alt+↑`, `Alt+↓` and `Ctrl+Shift+D` are **`NotImplemented` and leave the key unconsumed**, and the document is byte-unchanged after the whole case. **If it fails** S1 for an edit; S2 for a navigation failure.

**A-36 · The Destinations pane lists and navigates**
✅ **2026-09-21, T6.** `ui.` over the four shapes `dests-named.pdf` carries — `/XYZ`, `/Fit`, `/FitH`, and **one naming a page that does not exist**. All four are listed: the dangling one is shown and marked, never dropped, which the case reads off the **page column itself** and not only off the model behind it. Sortable by name and by page, and **the page column sorts as a NUMBER**: page 10 after page 5, which a lexicographic compare on the column's text would get backwards on any document past nine pages, with the unresolvable row sorting last rather than as page 0. Activating a destination moves the view to its page; **the control** — activating the unresolvable one does not move the view, and in particular does not land on page 1, which a naive `goToPage(pageIndex)` with a clamped −1 would do. **Two planted-bug controls run, both red.** The first run of the display bug left this case GREEN, which is the finding recorded at the case: `unresolvedRowCount()` reads the MODEL and *the reader can see which one goes nowhere* is a claim about the SURFACE (convention 22). The case now reads both. **Raised I-05**: a destination's fit is carried and not yet applied.
**Expect** Over `dests-named.pdf`: every resolvable destination listed with its name and page, sortable; the dangling one shown as unresolvable rather than omitted silently; activating one moves the view. **If it fails** S2.

**A-37 · The sidebar lists only panes that exist**
✅ **2026-09-20 at T5 for one pane; re-read 2026-09-21 at T6 against all three.** `ui.` — the registry holds exactly **one** entry, `pane.thumbnails`, and the chooser built from it holds one row and is hidden entirely; there is no greyed row and no placeholder row, and `paneWidget(PaneBookmarks)` is null rather than a stub. Each of the seven unbuilt `Ctrl+Shift+*` chords is unregistered, **leaves its key unconsumed**, and adds nothing to the list. `Ctrl+Shift+P` from the Viewer shows the sidebar, selects the pane and **moves the focus into it**, which is asserted through `activeContext()` becoming `PageThumbnails`. **At T6 the case is met as written**: the registry holds **exactly Thumbnails, Bookmarks and Destinations, in that order**, the chooser holds three rows and is now visible (it is hidden below two, because a chooser with one entry is furniture), and the seven panes whose models do not exist are still absent from both — asserted where the list has GROWN, because *only panes that exist* is a claim about the whole set. T5's two cases were corrected in place rather than left to contradict this one (convention 8).
**Goal** D4-2.
**Expect** The pane list holds exactly Thumbnails, Bookmarks and Destinations (plus the bottom log dock, which is a view of the application, not of the document); the seven remaining `Ctrl+Shift+*` chords are `NotImplemented` and leave the key alone; **no greyed or placeholder row appears**. **If it fails** S2 — Stage 2's I-11 shape.

**A-38 · Dock, float, auto-hide and peek still work with real content in them**
✅ **2026-09-20, T5, offscreen.** `ui.` — Stage 1's behaviours re-asserted with a pane rather than a label: width memory survives an auto-hide, `peek()` reveals **without pinning** (`isAutoHidden()` stays true through it, which is what I-39's strip logic asks about), `unpeek()` puts it back, and docking right then left leaves the pane laid out with a **non-zero** visible count each time — the assertion that would catch a pane that came back 0 pixels wide. **What this does not cover** (convention 19): the pointer ARRIVING on the edge strip (I-37), which is a hover no synthetic run reproduces, and floating the dock into its own top-level window, where the binder's `window() == scope_` test means a chord resolves differently. Both are manual **M-07**'s.
**Expect** Stage 1's `SidebarDock` behaviours, re-asserted with a pane rather than a placeholder: dock left and right, float, auto-hide, the strip peeking on pointer arrival (Stage 1's I-37) and the strip disappearing while the pane is out (I-39). **If it fails** S2.

**A-39 · Two documents, two pane states**
✅ **2026-09-20, T5.** `ui.` — two documents with **different page counts**, so *which document the pane is showing* is a question its own state can answer; two identical documents would make a pane that never switched indistinguishable from one that did. Switching tabs gives the new document's page count and handle, and the pane is **the same object** — not a rebuild, which is what keeps a tab switch from discarding thumbnails the one worker had just produced. Every request afterwards names the new handle, so no ticket into the previous document survives. A second case closes the last document and asserts the pane empties and holds **no ticket into a handle its window has closed** — the half that would be a use-after-free in the backend rather than a cosmetic error.
**Expect** Switching tabs shows the new document's thumbnails, bookmarks and destinations — not the previous document's, and not a rebuild of an identical pane. **If it fails** S2.

### 5.8 Tools (T7)

**A-40 · `H`, `V`, `Z` arm their tool from a key event, and the cursor follows**
✅ **2026-09-21, T7.** `ui.` — each chord is a real `QKeyEvent` to the focused viewer, and the cursor is read back off the **viewport widget** as well as off the controller: a controller reporting the right shape while the widget still showed an arrow would pass every other line. Select is the default and shows an I-beam, Hand an open hand, Marquee a cross. **The case states that `SelectTool` selects nothing** (convention 19) and asserts it — a full drag with `V` armed moves no scroll and no zoom — because text selection needs `ITextExtractor`, which the backend census names as missing at every launch and which is **Stage 5's**. The **Loupe is armed by dispatch**, not by a chord: `tool.loupe` is one of this stage's two new ids and §8 gives it no binding.
**Expect** Each chord dispatches and the controller reports the armed tool and its cursor. **The case states that `SelectTool` selects nothing** (Stage 5) — convention 19. **If it fails** S2.

**A-41 · The temporary hand modifier pans without disarming the tool**
✅ **2026-09-21, T7.** `ui.` — the modifier is **`Shift`**, and the choice is forced rather than preferred: §8 gives `Space` to `nav.nextScreen`, a bare `Alt` to `app.ribbonKeyTips` and `Ctrl` to the wheel, and this stage does not touch §8 (plan §10). `ToolController.h` records all three, and records what `Shift` collides with. Holding it makes `effective()` the Hand while `armed()` stays MarqueeZoom; the drag pans, **and the case asserts the DIRECTION** — dragging up and left grows the scroll offset — because the classic hand-tool defect is a sign error that is unmistakable to a person and invisible to a case checking only that something moved. Releasing restores the tool and its cursor. **The off-window release** is asserted on the EFFECT: no release event ever arrives, the next press re-derives, and the pan the stale modifier would have caused does not happen. **What it does not cover, at the assertion**: a HOVER does not correct the state, because with a non-tracking tool armed Qt delivers no move at all — bounded and cosmetic, and written down rather than left to be found. A third case is the control: the modifier does **not** swap tools under a live drag, or a release would reach a tool that never saw the press.
**Expect** Holding the modifier pans; releasing it restores the previously armed tool and its cursor, including when the modifier is released outside the widget. **If it fails** S2.

**A-42 · `Esc` cancels a drag and leaves the view where it was**
✅ **2026-09-21, T7, and it moved a design decision.** `ui.` — both halves, because they fail differently: a cancelled MARQUEE has nothing to restore (it changes nothing until release, so the case asserts the zoom and scroll are *exactly* as before, and that the release still in the user's hand does not then complete the gesture), while a cancelled HAND pan has been moving the view all along and must put the scroll back. **The control** — `Esc` with no drag changes neither the tool nor the view, **and does not consume the key**. That second clause is what changed the design: registering `edit.cancel` made a disabled command answer `Unavailable`, which `ShortcutBinder::consumes` counts as consuming, and `Esc` stopped reaching the tool finder's popup — `tst_toolfinder`'s *the result list can always be put away* went red, and that popup holds a mouse grab (Stage 1's **I-44**). So `edit.cancel` is left UNREGISTERED and `Esc` is handled in `PageView::keyPressEvent`; the reasoning is at the id's absent registration and in §8 as **I-06**.
**Expect** A marquee-zoom drag cancelled with `Esc` leaves the zoom and scroll exactly as before; **the control** — `Esc` with no drag in progress changes neither the tool nor the view. **If it fails** S2.

**A-43 · Marquee zoom zooms to the rectangle, and the loupe magnifies without moving the view**
✅ **2026-09-21, T7.** `ui.` — a band a quarter of the viewport on both axes gives **about 4×**, asserted within a **stated 10 %** tolerance rather than by bare equality, because the band is integral pixels, the viewport is not exactly divisible and `Zoom::clamped()` may round; 10 % is far tighter than the failure the case exists to catch. **The control**: a drag below `MarqueeZoomTool::kMinDragPixels` is a CLICK and does not zoom — zooming to a two-pixel band lands at 6400 % with no obvious way back. The loupe moves **nothing**: scroll, zoom and history are unchanged, it tracks a hover with no button down (and the case asserts mouse tracking is on for it and for no other tool), and `Esc` puts the lens away and reports that it did. **What is not covered, at the case**: whether the magnified picture is the right region and readable — manual **M-12**'s. The lens magnifies the last painted FRAME rather than re-rasterizing, which `LoupeTool.h` argues from A2: a second rasterization would lag the pointer and compete with the viewport's own tiles.
**Expect** The zoom after a marquee matches the dragged rectangle within a stated tolerance; the loupe changes no scroll position and no zoom. **If it fails** S2.

**A-44 · Auto-scroll runs at the configured speed, pauses and reverses**
✅ **2026-09-21, T7.** `ui.` — 60 px/s moves **59–61 px in a stepped second**, and 240 px/s moves more than 3.5× as far over the same interval: the rate is asserted, not merely that it moved, which is the difference between a setting that is read and one that is read and ignored (`pageDisplay/lineWeight` is the standing example). The toggle IS the pause, and a paused auto-scroll advanced by the harness moves nothing. **Reverse is a negative `pageDisplay/autoScrollSpeed`** — which is why there is no third `ActionId` for it: the sign lives in a settings key and is therefore reachable from Preferences, the file and the batch runner. It **stops at the end of the document** rather than spinning against a clamped offset, it refuses to start at zero and the command reports itself unavailable there, and `Esc` stops it. A last case asserts the key reaches the CONTROLLER — `settings.set(autoScrollSpeed, 175)` and the controller says 175 — which is the half that would otherwise be a preference nothing reads, exactly as `performance/prefetchPages` was until S2 T15. **The harness is a back door** (`advanceAutoScrollForTesting`) and the suite header says what it therefore does not cover: that the timer fires.
**Expect** `pageDisplay/autoScrollSpeed` changes the rate measurably; a pause stops it; a reverse reverses it; it stops at the end of the document rather than spinning. **If it fails** S3.

### 5.9 Rulers, grid, guides, snapping (T8)

**A-45 · The snap precedence is object → guide → grid** ✅ **2026-09-21, T8.**
`unit.` — `tests/unit/tst_snap.cpp`, over synthetic geometry with no display. The three kinds are
placed **deliberately out of distance order** — the guide nearest, the object edge furthest — so a
resolver that took the nearest candidate regardless of kind would answer *guide* on every line and
the case would be about nothing. With all three in reach the object edge wins on both axes; with no
object the guide; with neither, the grid. A fourth section proves the precedence is **per axis**: a
guide on x and an object edge on y resolve independently, which is the half a whole-point rule
loses. The control is `SnapFlags::None` — nothing snaps and the point comes back untouched. **The
case states at its assertions that no product surface supplies an object edge** (convention 19):
`GridGuideLayer::activeFlags()` never sets `ObjectEdges`, the key that would is Stage 6's (D4-5),
and the branch is tested now because the precedence is what a later stage would otherwise have to
discover under a drag.
**Goal** D4-6, over synthetic geometry, exhaustively.
**Expect** With candidates of all three kinds inside the threshold, the object edge wins; with no object, the guide; with neither, the grid. **The case states that no product surface supplies object edges until Stage 6** (convention 19). **If it fails** S2.

**A-46 · One threshold, in device pixels, at every zoom** ✅ **2026-09-21, T8.**
`unit.` — two scales an eightfold apart, 0.5 and 4.0 device pixels per point. A point **7 device
pixels** from the candidate snaps at both; **9 device pixels** snaps at neither, which is the
control and the half the criterion does not ask for (convention 22). The case also asserts that the
tolerance in POINTS really did differ — 18 pt at one scale and 2.25 pt at the other — so the two
results are a statement about screen pixels rather than a coincidence. **A measurement changed the
case: the candidate is a lone GUIDE, not a grid line.** At 0.5 device pixels per point the declared
grid's 18 pt step is 9 screen pixels, so no point on the page is more than 4.5 pixels from some
line and an 8-pixel threshold catches every one of them — correct behaviour, and useless as a
control, because *one pixel further does not snap* cannot be said of a point that is always within
reach of the NEXT line. A second section covers a view with nothing presented in it: a non-positive
scale snaps nothing rather than dividing into an infinite tolerance that grabs the page.
**Goal** The stage's exit criterion, made a number.
**Expect** At 50 % and at 400 %, a point the same number of **screen** pixels from a grid line snaps, and a point one pixel further **does not** — the control, and the half the criterion does not ask for (convention 22). **If it fails** S2.

**A-47 · Guides are created, moved, locked, deleted and cleared** ✅ **2026-09-21, T8.**
`ui.` — `tests/ui/tst_rulers.cpp`, and every guide in it is **dragged off a real ruler by real
mouse events** rather than made by calling `dropGuide`: the gesture travels ruler → global
coordinates → `DocumentWindow` → page surface, which is the wiring, and calling the method would
have asserted the arithmetic twice and the wiring never. The top ruler makes a vertical guide and
the left ruler a horizontal one, each landing within a pixel's worth of points of the pointer's page
coordinate. A drag moves it; `Esc` mid-drag puts it back where the drag started; a **locked** guide
refuses the drag (the press falls through to the tool) and survives a clear-all, and the same clear
takes it once unlocked — which is what makes the first half a statement about the LOCK rather than
about a clear that does nothing. A zoom to 400 % and a view rotation leave the stored coordinate
untouched, because it is a page fact (D4-7). **The control**: a press on the ruler released on the
ruler makes no guide — that is how a person changes their mind. **This case found the wiring
defect** the task's own §4 notes: `updateRulerMappings()` asked `isVisible()` while running inside
`build()`, before the tab is in the window, so every mapping was skipped and a ruler could not be
dragged from at all.
**Expect** A guide dragged from a ruler lands at the pointer's page coordinate; a locked guide does not move; clear-all removes them; a guide's coordinate is unchanged by a zoom or a rotation of the view. **If it fails** S2.

**A-48 · Guides round-trip through `SessionState` and never through the file** ✅
**2026-09-21, T8 — with a stated bound.** `unit.` — a capture, a `save()`, a fresh `load()` and a
`restore()` bring back **every field**: orientation, the fractional position in PDF points, the page
index and the lock. Asserting all four matters — a round trip that dropped the lock or the page
index would leave a guide that reads back as unlocked on page 0 — and the fractional positions
(216.5 and 72.25 pt) are the tell that nothing rounded them into pixels on the way.
**WHAT THIS DOES NOT COVER, stated rather than implied (convention 19):** the case does **not**
SHA-256 a document before and after, because no Stage 4 surface writes one — the claim that nothing
about a guide reaches `pdf/` is structural here (`core/settings/SessionState` names no PDF type,
`ui/` may not mutate a `Document`, and **`mutation-lint` reports one command on this stage's sixth
task as on its first**, which is D4-1's whole mechanism). A byte comparison over a real save belongs
with the first Stage 4 surface that saves one, and **T13** is where session capture meets a
document. **I-01 is the open issue underneath this case** and T8 is what created the guides it is
about: the `DocumentId` a guide is keyed by changes on the first save.
**Goal** D4-7.
**Expect** Guides survive a close and a reopen in the same session and across a restore; **the document's bytes on disk are identical before and after** (SHA-256, read back from the path). **If it fails** S1 for a changed byte.

**A-49 · The rulers read out the cursor's page coordinates in the chosen unit** ✅
**2026-09-21, T8.** `ui.` — the reading at the page's drawn edge is 0, and 144 points along it reads
144 points, which `core/Units.h` converts to exactly 2 inches (through `Length`, not through a
number spelled in the case). The same page point reads the same at 50 % and at 400 %. **The tick
STEP, however, changes, and the case asserts that it does**: a ruler that kept its step across an
eightfold zoom would draw ticks a pixel apart at one end of it — the reading is a page fact, the
tick spacing is a legibility choice. Changing `units/rulerUnit` to millimetres moves both rulers and
the corner box, and the step is then round in **millimetres** rather than in points. The cursor
marker follows a move over the surface and is **cleared when the pointer leaves**, which is caught
in `viewportEvent` because `QEvent::Leave` goes to the viewport and not to the scroll area.
**What it cannot judge is legibility** — whether the numbers read at `general/uiFontScale` 0.75 and
whether the ticks are distinguishable on a real panel are manual case **M-07**'s.
**Expect** `units/rulerUnit` changes the readout and the ruler's ticks; the readout at a known page point matches `core/Units.h`'s conversion; at 400 % the readout is the same page coordinate as at 50 %. **If it fails** S3.

**A-50 · The grid obeys spacing, subdivisions, colour and origin offset** ✅ **2026-09-21,
T8.** `ui.` — the viewport is rendered to a pixmap and hashed; turning the grid on changes the hash,
and so does each of `units/gridSpacing`, `units/gridSubdivisions`, `units/gridColour`,
`units/gridOriginX` and `units/gridOriginY` in turn. **The load-bearing assertion is a NEGATIVE:
`RenderScheduler::stats().enqueued` does not move, and the cache's entry count does not move**,
across every one of those changes — which is the whole of D4-11. A grid composed into the tile would
have put its spacing, colour and offset into the cache key, and each line above would have
re-rasterized the document while the picture looked right. A section covers an unreadable colour
(`units/gridColour=blueish`): still a grid, still no render, because `core/` holds the text and
`ui/` decides whether it is a colour. **The case asserts pixels CHANGED, not lines in the right
places**, and says so: counting drawn lines would be a copy of the painter.
**Expect** Each key changes the drawn grid; the offset moves the origin rather than the whole page; **the tiles are not re-rendered** when any of them changes (D4-11) — asserted on the scheduler's request count, which must not move. **If it fails** S2 for a re-render.

**A-51 · `Ctrl+U`, `Ctrl+'` and `Ctrl+;` dispatch from a key event and toggle what they name**
✅ **2026-09-21, T8.** `ui.` — each chord is a real `QKeyEvent` to the focused viewer; each flips its
`units/*` row, and pressing it again flips it back, so the case is a round trip rather than one
direction of it. **The command's `checked` state is read from the SETTING**, not from a copy beside
the toggle — a ribbon button disagreeing with the Preferences page would be two truths. The rulers
start OFF, as they ship, so the first `Ctrl+U` is what turns them on: a case that started with them
on could not tell a toggle from a no-op. A second `MainWindow` over the same settings store opens
with the rulers already on, which is how a headless case states *the choice survives a restart*
without restarting a process. A companion case covers the three T8 command ids: with no guide under
the pointer `view.lockGuide` and `view.deleteGuide` are **DISABLED** (convention 16, with a target
rather than a user as the missing thing) while `view.clearGuides` is enabled because a guide exists;
with one, lock checks, delete refuses a LOCKED guide, and clearing the last guide disables itself.
**Expect** Rulers, grid and snap-to-grid each toggle, and the setting they write round-trips. **If it fails** S2.

**A-52 · Colour inversion and a custom page background are applied over the tiles** 🟡
**2026-09-21, T8 — the inversion is built and asserted; the SUBSTITUTION is not built (I-07).**
`ui.` — dispatching `view.invertColors` changes the rendered viewport, **enqueues nothing** and
leaves the cache's entry count where it was, and dispatching it again restores the **exact same
bytes** — which is what `RasterOp_NotDestination` buys over a composition mode with an alpha. The
inversion is applied per page SHEET rather than over the viewport, so the desk around the page stays
itself. **The second half of this case's title is not built and the case asserts that instead of
implying it**: `Features.md` §1.2 offers *invert **or substitute*** page colours, and substitution
needs a colour pair that none of D4-5's declared keys holds — so `view.pageBackground` stays
declared and **unregistered**, which consumes nothing, and the case asserts it is unregistered.
Raised as **I-07** (§8).
**Goal** D4-11's second half.
**Expect** Toggling inversion changes what is painted and **enqueues no render request**; the cache's entry count is unchanged; toggling back is exact. **If it fails** S2 — an inversion in the cache key doubles the cache for a toggle.

### 5.10 Splits (T9)

**A-53 · A split is two views over one document** ✅
**2026-09-21, T9 — asserted as an IDENTITY, which is the only way this case can fail usefully.**
`ui.` — after `window.split` the container holds two `PageView`s, and the two are checked
`document() == document()`, both equal to the tab's handle, both at the same `revision()`, and that
revision equal to the one `CommandJournal` the tab holds. **Why an equality and not a count of
handles:** a split that opened a second handle would render both panes correctly and look right in
every screenshot — it is wrong only in the two places nothing on screen reports, the three-step
close protocol that exists once and the memory the document costs. Both panes also report the full
page count and a non-zero height, without which every assertion below would hold vacuously over a
pane that was never laid out. **And the horizontal split really divides horizontally**: the second
pane's `y` is below the first's and its `x` equal, which is the only assertion that catches
`Mode::Horizontal` built as a `QSplitter(Qt::Horizontal)` — the coin flip
`SplitViewContainer.h` warns about. The vertical split is its mirror in a second case, whose
**control** is that pressing the same mode twice leaves the same two pane OBJECTS rather than a
rebuilt pair.
**Expect** After a horizontal split: two `PageView`s, **one** `DocumentHandle`, one `CommandJournal`, one revision. **If it fails** S1 — a second handle is a second copy of the document and a second close protocol.

**A-54 · A mutation in one view appears in both** ✅
**2026-09-21, T9 — geometry before pixels, and the revision is the half that could rot silently.**
`ui.` — `page.rotate` through the registry over a two-pane horizontal split: the model's page 0
is at 90°, the journal's revision has moved, and **both** panes report the new revision. The
assertion on the second pane's LAYOUT is what makes the case about the rotation rather than about a
tile — its first page slot's width and height have swapped (Stage 1's **I-36**). **Why the
revision is asserted separately:** it is part of every tile's key (D2-12), so a pane that was not
told would go on showing the un-rotated page with no error, no crash and no failing test. A second
case covers the same fan-out for GUIDES, which the plan did not name: a guide added in one pane
appears in the other and a clear in one clears both, because a guide is a page fact in PDF points
(D4-7) and what is per-view is the drag.
**Expect** `Ctrl+R` in one pane of a split rotates the page in both, at the same revision, geometry before pixels (Stage 1's I-36). **If it fails** S2.

**A-55 · Synchronized scrolling, and the spreadsheet freeze** ✅
**2026-09-21, T9 — and the corner is the control the other three assertions need.**
`ui.` — synchronization is **on by default**, because `Features.md` §1.2 says *"two
synchronized panes"* and a split that opened unsynchronized would make that word mean nothing.
Scrolling either pane moves the other — asserted **in both directions**, since a one-way mirror
is a leader and a follower rather than a synchronization and would be invisible until someone
scrolled the second pane — and `window.syncScroll` frees them, after which the parked pane does
not move. The mirror is carried through **PDF points** rather than device pixels, so two panes at
different zooms stay on the same place in the document; at a shared zoom the ratio is exactly 1.0.
The spreadsheet split is taken with the body already scrolled, and its four assertions are the
freeze written out: the top pane follows the body in **x only**, the left pane in **y only**, and
**the corner moves in neither** — which is the control, because three panes that all followed
the body would satisfy two of the first four. The two rows of panes are asserted to be the same
widths, which is what makes the frozen column one column rather than two panes that happen to be on
the left. A third case asserts `window.syncScroll` is **refused** in a spreadsheet split: the
derivation is the mode, not an option on it (Stage 1's **I-53**).
**What this case does NOT cover** (convention 19): the splitter handles are real, and the sizes a
person drags them to are the widget's and die with the window; that dragging one row's handle moves
the other's is **M-09**'s, and so is whether the panes LOOK locked together while a wheel turns.
**Expect** Sync on: both views scroll together; off: independently. The frozen row and column stay put while the body scrolls, and the frozen regions scroll **only** in their own axis. **If it fails** S2.

**A-56 · Removing a split leaves one view and no leaked tickets** ✅
**2026-09-21, T9 — and the case asserts the TICKETS rather than the widget, on purpose.**
`ui.` — the pane about to be retired is first REQUIREd to hold at least one tile, or what
follows would hold vacuously over a pane that was never drawing. After `window.removeSplit`:
`viewRetired` named that pane, one view remains and it is the primary, the tab reports it as
`view()`, and the retired pane holds **no ticket, no tile and no valid handle** — with the
scheduler's own pending count at zero once deferred deletes are flushed. **Why the claim is about
tickets and not existence:** the command runs from inside `ShortcutBinder`'s application event
filter while a key event is still being delivered to the very pane it destroys, so the widget must
go through `deleteLater` — but that alone would leave its tickets in flight until the event loop
got round to it, so the pane is handed an EMPTY document first, which cancels them now. The
surviving pane is asserted untouched, because a removal that cleared the wrong view would pass
everything above. **The control** is a second case: *Remove Split* is **refused** on an unsplit
window, enabled once split, and refused again after the removal — a control that silently does
nothing cannot be told from one that is broken (Stage 1's **I-53**). A third case asserts the active
pane follows FOCUS and that `view.zoomIn` — registered at Stage 2 T14 and untouched by this task
— lands on it and not on the other pane, which is the whole claim that a split needed no change
to any of the twenty-five §8 Viewer commands.
**Expect** One view remains, the scheduler reports no outstanding tickets for the removed view, and no tile of the removed view is left in the cache keyed to a dead viewport. **If it fails** S2.

**A-82 · The split's divider is VISIBLE against the desk, in both themes — I-12, closed** ✅
✅ **2026-09-26** (`tests/ui/tst_split.cpp`, two cases tagged `[I-12]`). Measured on **painted pixels**
— the handle's `grab()` against a margin pixel of the primary view's own `grab()` — through a real
`ui::ThemeManager` applied in each theme, with the application palette restored afterwards, and with
`uitest::deltaE76` and its floor rather than `!=` (Stage 2's **I-29**). **Measured:** light, desk
`#f3f3f3`, band `#d0d0d0` **ΔE 12.4**, centre line `#5e5e5e` **ΔE 55.9**; dark, desk `#1f1f1f`, band
`#3d3d3d` **ΔE 14.0**, line `#9a9a9a` **ΔE 51.8**. The band must clear the distinct-surface floor (10)
and the line three times it. Under the pointer the handle is the accent (`#1a5fb4` / `#4c9be8`),
distinct from both band and desk, and it returns to the line when the pointer leaves. The second
case asserts **all three** splitters of a spreadsheet split paint the same divider.
**THE PLANTED CONTROL** — `createHandle` returning Qt's stock handle — reddens both cases: the band
measures **ΔE 0** against the desk in both themes, which is M-01's invisible boundary as a number.
**What it does NOT cover** (convention 19): the offscreen platform runs **Fusion**, whose stock handle
draws a faint grip (ΔE 19 at its centre, read off the control) where the product's `windows11` style
drew nothing M-01 could see — so the control fails here on the band and on the line floor, not on
"nothing"; the pointer is an `Enter` event sent by hand; and whether the divider reads as a boundary
at a glance on a 150 % panel is **M-01**'s and **M-09**'s.
**Steps** `ctest -R "divider"`.
**Expect** Band ΔE ≥ 10 and line ΔE ≥ 30 against the desk in both themes, on every splitter of every
split mode; the pointer lights the handle. **If it fails** S3 — I-12 is back.

### 5.11 Full Screen and presentation (T10)

**A-57 · Every §8 Full Screen chord dispatches in the `FullScreen` context** ✅
**2026-09-21, T10 — and the context is declared on the PAGE SURFACE, not on the window.**
`ui.` — with a presentation active, `activeContext()` is `FullScreen` and all seven rows are
registered and enabled. **Two things had to be right for that, and each was wrong first.**
`activeContext()` takes the INNERMOST declared context, and `PageView`'s constructor declares
`Viewer` on every instance — so a declaration on the presentation WINDOW is shadowed by the view
inside it and every chord resolves as a Viewer one, which is **I-06's shape in a new place**: the
key is consumed by the wrong command rather than left alone. And `ShortcutBinder`'s scope test was
`widget->window() == scope_`, which no separate top-level can pass — so before it was widened to
*a window this one OWNS*, `activeContext()` answered `Global` with a presentation on screen. The
case caught the second on its first run. **`Ctrl+Shift+M` is REFUSED with one monitor** rather than
enabled and doing nothing, which is what the offscreen platform reports and also the commonest real
machine. **The control** is a second case: with nothing presenting all seven are registered and
DISABLED — not unregistered, because an unregistered id leaves its key unconsumed, which is right
for a feature that does not exist and wrong for one that exists and does not apply (Stage 1's I-53).
**Expect** With the controller active, `activeContext()` is `FullScreen`; `Esc`, `F11`, the paging chords, `B`, `W`, `P`, `Ctrl+L`, `Ctrl+Shift+H` and `Ctrl+Shift+M` each dispatch; the Viewer's chords do **not** resolve there unless §8 lists them. **If it fails** S2.

**A-58 · `setPresentationModeActive` is called true on entry and FALSE on every exit** ✅
**2026-09-21, T10 — asserted as a CALL SEQUENCE, because a final state cannot say it.**
`ui.` over `ShellFixture`'s new `FakePowerSession`, which COUNTS calls in order rather than
recording a flag: a controller that asked `true` twice and `false` once ends inactive and is still
leaking a request, and a final-state check passes it. Five exit paths, each its own section, each
asserting `{true, false}` exactly: the `present.exit` command, `F11` out of a presentation (which
leaves entirely rather than dropping to level 1), **the window being closed by the OS** — the path
no chord reaches — a topology change, and **the controller being destroyed while presenting**,
which is the one that matters most: a machine left unable to sleep by a process that no longer
exists. **The pairing is structural rather than disciplined**: `applyPowerState()` has ONE caller,
`setLevel()`, which is the only writer of the level and which applies the power state BEFORE any
work that could throw — so there is no path that changes the level without pairing, and an
exception on the way out cannot skip it. **Level 1 calls it too** (D4-12), which a separate case
asserts. A sixth case covers the OS REFUSING: both calls are still made, the refusal is logged and
not acted on, and the presentation runs — a screen saver we could not disable is a worse
presentation, not a broken one. **`ShellFixture` gained two platform fakes for this**, because a
null `platform::Platform` member would have made the whole case vacuous rather than green.
**Goal** D4-12, and the S1 a presentation most plausibly ships.
**Steps** A `platform.`/`ui.` case over a fake `IPowerSession`: normal exit, `Esc`, `F11`, window close, a topology change, and an exception on the way out.
**Expect** Exactly one `true` per entry and exactly one `false` per exit, **on every path**; the fake's final state is inactive. **If it fails** S1 — a machine that cannot lock.

**A-59 · The monitor comes from `IDisplayTopology`, and the join is by `friendlyName`** ✅
**2026-09-21, T10 — green on the fallback; the JOIN itself is manual.**
`ui.` — `fullScreen/monitor` naming a display nobody has still presents, on the primary, with one
log line rather than silently. The key stores a **`friendlyName`** and the join uses `QScreen::name()`,
never `MonitorInfo::id`: `id` is the OS device name and a `QScreen` is named with the display's
model, so those two can never match — Stage 2's **I-20**, where the comparison was made for a
whole stage and never once succeeded, costing nothing visible because the fallback answered
plausibly and wrongly.
**What this case does NOT cover** (convention 19): the offscreen platform reports exactly one
screen, so no assertion here is about a join SUCCEEDING against a real second monitor. That is
**M-04**, which precondition P-2 exists for.
**Expect** `fullScreen/monitor` selects a monitor by a stable key; the `QScreen` join uses `friendlyName` and never `id` (Stage 2's **I-20**); an unknown or absent monitor falls back to the primary with one log line. **If it fails** S2.

**A-60 · A topology change moves the presentation rather than losing it** ✅
**2026-09-21, T10.**
`ui.` — `FakeTopology::notify()` drives the subscribed handler as `WinDisplayTopology` does when
Windows reports a change; the presentation is still presenting afterwards, **at the same page**, and
its surface still visible. Moving rather than exiting is the decision and it is not the obvious
one — exiting is also defensible and loses the presenter's place mid-sentence, whereas a
presentation on a monitor that no longer exists is unreachable: the person cannot even press `Esc`,
because they cannot see what has focus. The same path is one of A-58's five sections, asserting
that a handler which tore down without pairing would be that S1 arriving through the one door no
chord opens.
**Steps** Drive `IDisplayTopology::subscribe`'s handler with a fake arrangement in which the presenting monitor is gone.
**Expect** The presentation reappears on the primary monitor, still presenting, at the same page. **If it fails** S1 — a presentation on a monitor that does not exist is unreachable.

**A-61 · Transitions, auto-advance, loop and blank** ✅
**2026-09-21, T10 — and the reader is the first in this project PDFium could not supply.**
Two halves. **`render.`** reads `corpus/ordinary/transitions.pdf` through the backend: page 1's
`/S /Wipe /D 1 /Di 0`, page 2's `/S /Dissolve /D 0.5`, page 3's **`/S /Split`** — which this
build does not draw, so it reads `None` **with the file's own word kept beside it** and
`unsupportedCount()` of 1, rather than being approximated with a wipe the author did not ask for —
the `/Dur 3` and `/Dur 5` on pages 1 and 3, and `/PageMode /FullScreen` in the catalog. **Page 4
carries neither key and is the control the override needs**: absent is not `None`, and a model that
stored `None` for both would make the override unimplementable. A second `render.` case is the
whole-reader control — an ordinary file reads EMPTY, which is the answer for almost every
document. **`ui.`** asserts the resolution: a page's own `/Trans` beats `fullScreen/transition` and
a page without one falls through to it; the same for `/Dur` against `fullScreen/autoAdvanceSec`;
auto-advance steps, **loop off stops at the last page AND stops the timer** — a presentation still
waking every few seconds at the end would be a laptop that never idles — and loop on restarts at
page 1.
**What this case does NOT cover** (convention 19): what a transition LOOKS like. The animation's
appearance and smoothness are **M-10**'s, and the auto-advance interval is asserted as the resolved
NUMBER — the timer firing is M-10's too, which is why `advanceForTesting` is named as a back door
on the method itself.
**Expect** Over `transitions.pdf`: a per-page `/Trans` overrides `fullScreen/transition`; auto-advance steps at `fullScreen/autoAdvanceSec` (measured, not assumed); `loop` restarts and off stops at the last page; `B` and `W` blank and restore the exact page. **If it fails** S2; S3 for a transition that is merely ugly.

**A-62 · The cursor and the laser pointer obey their settings** ✅
**2026-09-21, T10.**
`ui.` — `fullScreen/showCursor` is read when the presentation STARTS (a case sets it false and
checks the controller opens hidden), `Ctrl+L` toggles it at runtime, and `P` toggles the laser and
changes no page. `B` and `W` blank and a second press restores **the exact page**, which the case
asserts on the page index rather than on pixels: blanking is not navigation. Leaving a presentation
clears the blank and the laser, because a presentation re-entered with the screen still black would
look broken and that state belongs to the presentation rather than to the window.
**What this case does NOT cover**: that the projector actually went black. The blank is asserted as
the overlay's state and the page beneath it as unchanged — M-10 is where a person sees it.
**Expect** `fullScreen/showCursor` hides and shows it; `Ctrl+L` toggles at runtime; `P` toggles the laser and leaves the page unchanged. **If it fails** S3.

**A-63 · A presentation changes no document** ✅
**2026-09-21, T10.**
`ui.` — a full pass over a five-page document: page turns from both ends, `B`, `W`, the laser, the
cursor toggle, an auto-advance step, and the exit. Afterwards the file's SHA-256 read back from
disk is unchanged, the journal cannot undo, and the document is not dirty. `ui/`'s entire mutation
vocabulary is *construct a `Command`* and this controller constructs none — which `mutation-lint`
also reports from the other side, still one command on the stage's eighth task as on its first.
**Expect** After a full presentation pass over `transitions.pdf` including blanks and the laser, the file's SHA-256 read back from disk is unchanged and the journal is empty. **If it fails** S1.

### 5.12 Window arrangement (T11)

**A-64 · Tile and cascade over synthetic screen geometry** ✅
✅ **2026-09-21, T11.** `ui.` over `WindowArranger::tileGeometry` and `cascadeGeometry`, which are pure functions of a work area and a count — the offscreen platform reports one screen of a size no case may depend on, so a tiling case written against it would have to be loosened until it asserted nothing. Over counts 1, 2, 3, 4, 5 and 7 in both orientations the cells **cover the work area exactly**, pairwise never intersect, and none is below `kMinTileWidth`/`kMinTileHeight`. The ORIENTATION is asserted rather than assumed: `Horizontal` gives full-width strips one above another and `Vertical` gives columns side by side, which is the coin flip a swapped implementation would pass every other check on. Eight windows on a 1040-pixel work area is 130 pixels a strip, so the strips stop at four and the surplus is laid across them — the **grid fallback**, with the cover and the minimum both still holding. Cascade steps by the step it was GIVEN (never by the constant, so a theme with taller title bars cannot redden it), keeps every window wholly inside the work area, and restarts the diagonal rather than marching off the corner. **The control, from both ends:** with one window `window.tileHorizontal`, `window.tileVertical`, `window.cascade` and `window.merge` are all refused with `Unavailable`, AND calling `tile()`/`cascade()` directly moves the geometry not at all — only the second says the operation is a no-op, and only the first says the button is not merely broken (Stage 1's I-53). `window.list` is deliberately not among the four. **Two planted-bug controls run red for the right reason** (convention 7): dropping the remainder in the size share reddens the exact-cover assertion, and merging with `adoptDocument(document, 0)` instead of appending reddens A-65's order. **What this case does NOT cover** (convention 19): a real taskbar — `availableGeometry()` offscreen is the whole screen — and windows on two monitors. Both are **M-09**'s.
**Expect** Tiling N windows fills the work area with no overlap and no window smaller than a stated minimum; cascade steps by the title-bar height and keeps every title bar on-screen. **The control:** arranging with one window changes nothing. **If it fails** S2.

**A-65 · Merge windows re-docks every detached tab, in order** ✅
✅ **2026-09-21, T11.** `ui.` — five documents in one window, two torn off with `detachDocument` into windows of their own, then `WindowArranger::mergeWindows()`: one window, five tabs, in their original order, each tab the SAME `DocumentWindow` object with the same `DocumentHandle`, the same `CommandJournal` and the scroll position it had, which is what distinguishes a MOVE from a close and a re-open. **The order is only a meaningful claim because the target is fixed**: the first window in `shellWindows()` rather than the active one, since merging into whichever window a person stood in would make the answer depend on where they clicked. **What no merge can restore**, said at the case: a detached window records nothing about which tab its document used to be, so the result is window creation order with each window's tabs in tab order — tearing `e` out before `d` puts them back as `e`, `d`, and nothing in the product claims otherwise.
**Expect** Three windows with five documents become one window with five tabs in their original order, each keeping its handle, journal and scroll position. **If it fails** S2.

**A-66 · The window list and `window.new`** ✅
✅ **2026-09-21, T11 — five cases.** The LIST names every window in creation order by its own title, `activateWindow` moves `activeShell()` (which is what every Global command resolves through, so that is the claim rather than anything about a menu), and an out-of-range index is a no-op, because the list a menu was built from can be stale by the time it is clicked. **`window.new` is a second view**: same path, **different `DocumentHandle`** — two windows are two `DocumentWindow`s and each runs the three-step close protocol, so a shared handle would be closed out from under the other, which is the exact opposite of why a split shares one (D4-13) — and **no second journal**, so the companion is not editable and never dirty. A rotation run in the leader's window moves both to the same revision and the second view's laid-out page has its width and height SWAPPED (geometry before pixels, Stage 1's I-36): it lays out from the leader's model tree while rasterizing through its own handle, and a companion reading its own engine tree would show the page upright with nothing on screen to say so. Closing the leader **detaches** the companion rather than taking it down, and it goes on drawing the file through the handle it always had. The two scroll together by default and `window.syncScroll` unlinks them, mirrored in PDF points and both ways, behind an echo guard — without which two windows each following the other is a loop with no fixed point.
**Expect** The list names every window and switching activates it; `window.new` opens a **second view of the same document** in a new window with its own handle (D4-13's exception), and both views stay correct after a rotation in one. **If it fails** S2.

**A-66(a) · A window that is not in front asks for its viewport at `Prefetch`** ✅
✅ **2026-09-21, T11 — a case this guide did not have, added with A2's named work.** [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified)'s *A2 at Stage 4* records the two-window column as leaving the 16.667 ms budget **under a named, reachable condition**, and names that condition as T11's: *a window that does not have focus enqueues its viewport at `Prefetch`, not `Visible`*. Nothing on screen can tell the two apart — the same tiles are asked for and they arrive — so `RenderScheduler::Stats` gained three per-lane counters and the case asserts on those. With two windows over one document, scrolling the BACKGROUND one moves `enqueuedPrefetch` and leaves `enqueuedVisible` exactly where it was; **the control convention 22 says the criterion will not ask for** is the same gesture in the same window once it is in front, which moves `enqueuedVisible`. Without it, *"the background window queues at Prefetch"* and *"this build queues everything at Prefetch"* would be the same passing case. **Planted-bug control run red:** ignoring the background flag in `TilePresenter` reddens the first half. **What it does not claim** (convention 19): that the change buys the milliseconds §3.3 predicts. That is `bench_pipeline --stage4`'s to measure, and **T14 re-reads it**.
**Expect** A window that is not `MainWindow::activeShell()` enqueues its viewport tiles at `RenderPriority::Prefetch`; the window in front enqueues at `Visible`. **If it fails** S3 — A2's named work is unmet and the two-window figure stands as measured.

### 5.13 Fit Visible, Document Properties, reading mode (T12)

**A-67 · The content box is the union of the page's objects, and it is cached**
✅ **2026-09-23, T12**, across `tests/render/tst_contentbox.cpp` and `tests/unit/tst_fitpolicy.cpp`. **Inside and TIGHT:** over `cropbox-margins.pdf` the box is strictly inside the CropBox by more than 36 pt on every side *and* occupies under 60 % of the sheet's area — both assertions, because a "content box" that had quietly come back as the CropBox would pass the first and fail the second. **The empty page:** page 2 of that same file is a well-formed page with a zero-length content stream (T1 built it for this clause), and it answers `ErrorCode::NotFound` — which is a value and not a failure — with the fit then falling back to the **CropBox and asserted NOT the MediaBox**, two rectangles that differ visibly on that file and on no other. `text-a4.pdf` is not used: its ink does not reach its edges either, so it would have proved nothing the first file does not. **Cached:** asking twice returns the identical rectangle, and an empty page asked twice answers `NotFound` twice rather than walking a page that has nothing to walk. **The back door is named** (convention 19): there is no counter on `FPDF_LoadPage` a test may read, because the handle never leaves `pdf/render/` — so *the engine was not called again* rests on the code, which consults the map before taking the engine lock, and **what this case therefore does NOT cover is a cache that is written and never read**; a per-page timing assertion would be a flake. An index outside the tree is `InvalidArgument` and is not cached; a handle the backend did not open is `InvalidArgument` too — a caller error, never an empty page. The unit half covers a content box the engine reported with **no area**, which is the same absence in another spelling.
**Steps** `tst_contentbox` over `cropbox-margins.pdf`, `text-a4.pdf` and an empty page.
**Expect** The content box is inside the CropBox and tight around the ink; a page with no objects falls back to the CropBox; asking twice calls the engine once. **If it fails** S2.

**A-68 · `Ctrl+3` produces the CONTENT's zoom — Stage 2's I-28, closed**
✅ **2026-09-23, T12 — and the first version of this case PASSED with the defect planted back in, which is the part worth reading.** Over `cropbox-margins.pdf` the three fits give three different zooms and **Fit Visible's is the content's**, asserted against a zoom computed independently from the engine's own content box. *(`tests/render/tst_contentbox.cpp`, with the arithmetic in `tests/unit/tst_fitpolicy.cpp` and the widget half — `Ctrl+3` fitting **and scrolling to** the content box — in `tests/ui/tst_properties.cpp`.)*

**The control, run first and run red only after the case was fixed.** With `fitRect` reverted to returning the CropBox for `FitMode::Visible` — I-28 exactly as it was — the case's original assertions (`visible != page`, `visible > page`) both **held**, because on this file the CropBox is smaller than the sheet, so a Fit Visible still fitting the crop is both different from Fit Page and larger than it. *Three different numbers is a necessary condition and not a sufficient one.* The assertion is now `visible == zoomToFit(content)` with `zoomToFit(crop)` asserted **different** beside it, so the equality cannot be satisfied by the defect it exists to catch; the planted bug then reddens it. **This is convention 7 catching the case rather than the product** — a control that does not fail for the right reason is worse than none — and it is I-28's own lesson arriving a second time, one layer up: the old case asserted `fitMode()` and was green for a month, and this one asserted three inequalities and would have been green too.

**The other control, as the case asks:** a page whose content fills its sheet makes Fit Visible and Fit Page agree. It is **built in the case rather than added to the corpus** — a second generated file would have to regenerate byte-identically forever to prove one equality — and the unit half proves the same over synthetic geometry.
**Goal** Rule 1, on the case that taught it.
**Expect** Over `cropbox-margins.pdf`, Fit Page, Fit Visible and Actual Size give **three different zoom factors**, and Fit Visible's is the one that makes the content box fill the viewport. **The assertion is on the zoom, never on `fitMode()`** — the old case asserted the mode and was green for a month. **The control:** on a page whose content fills its CropBox, Fit Visible and Fit Page agree, which is correct and is why the corpus file had to be built. **If it fails** S2, and I-28 stays open in Stage 2's guide.

**A-80 · Fit Page produces the DRAWN page's zoom — I-10, closed** ✅
✅ **2026-09-24**, across `tests/unit/tst_fitpolicy.cpp` (synthetic geometry) and
`tests/render/tst_contentbox.cpp` (the real file). **This case exists because I-10 says it had to**:
A-68 applied I-28's lesson — *a fit is a zoom* — to Fit **Visible** only, and nothing checked Fit
**Page**'s number against the rectangle the product lays out, so a Fit Page measuring the sheet had a
green suite over it from Stage 2 until a person walked M-11. The assertion is the same shape A-68
uses: the zoom is compared against one computed independently from the CropBox, with
`zoomToFit(media)` asserted **different** beside it — two rectangles that fitted to the same number
could not tell the defect from the fix. Over `cropbox-margins.pdf` the product-level half also
asserts the consequence a person actually saw: **the drawn page fills the viewport it was fitted to**
(> 99.9 % of the available height), where at the sheet's zoom it covered **under 90 %**.
**THE PLANTED CONTROL WAS RUN, AND WHAT STAYED GREEN IS THE POINT.** With `FitMode::Page` returning
`boxes.media` again, three cases go red — this one in both suites (0.691 against the crop box's
0.834 on synthetic geometry; 0.780 against 0.941 over the real file) and `fitRect measures the crop
box and the content box and never the sheet`. **Both *"three different zooms"* cases stayed green**,
the unit one and the one over the real file: they assert that the fits DIFFER, and a
wrong-but-different Page fit satisfies that. That is I-10's *"why no case caught it"* demonstrated
rather than argued, and it is the reason this case is an equality against a named rectangle.
**Steps** `ctest -R "Fit Page|fitRect"`.
**Expect** `fitRect(boxes, Page) == boxes.crop` and `!= boxes.media`; Fit Page's zoom equals the zoom
computed from the CropBox and differs from the sheet's; over `cropbox-margins.pdf` the drawn page
fills the viewport. **If it fails** S2 — I-10 is back, and `Ctrl+0` is not fitting the page.

**A-81 · A fit on a MIXED-size document fits the page it was chosen on — I-11, closed** ✅
✅ **2026-09-26** (`tests/ui/tst_properties.cpp`, four cases tagged `[I-11]`, over
`uitest::FakeBackend::pageBoxes` set to `mixed-page-sizes.pdf`'s four pages in its order). **This case
exists because M-01 found I-11** over a suite that had no document of mixed sizes in any widget case:
`FakeBackend` built every page A4 until this change, so *"a uniform document gives every page the same
zoom"* — the sentence T12 wrote to excuse Fit Width and Fit Page from the anchor — was true of every
document the widget suite could open. **The four cases:** from 400 % in the lower part of the
landscape page, **Fit Width** leaves the reader on that page and makes its slot the viewport's width
less the margins (± 3 px), and **Fit Page** leaves them on it and fits it inside the viewport, touching
on one axis; a fit set on the landscape page **keeps its zoom at every refresh** while the A5 page is
scrolled into view, and a NAVIGATION to the A5 page moves the anchor and fits the A5 page's width;
and on a uniform twelve-page document **the zoom box, Actual Size and `Ctrl+-`** leave the reader on
page 6. **TWO PLANTED CONTROLS, one per cause, each run red:** with the fit measuring the current page
again, Fit Width reddens at **1753.9 px against 877** — exactly M-01's 2× — and the scroll case at
**1.500 against 0.747** and on the page; with the place not restored, Fit Page lands on **page 4**
(`3 == 1`), Fit Width on page 4 as well, and the zoom case on **page 2** (`1 == 5`). **And the first
draft of the third case passed with the defect planted**: the unanchored fit does not settle on the
wrong zoom, it OSCILLATES between the two pages' zooms, and a check after an odd number of refreshes
read the right one by coincidence — so the zoom is now asserted after EVERY refresh (convention 7
catching the case rather than the product, as A-68's did).
**What it does NOT cover** (convention 19): the chord — the cases call `PageView::setFitMode`, which
is the whole of `registerFit`'s handler, and `tst_viewercommands.cpp` owns the chord reaching it; and
a device pixel ratio other than 1.0, which is M-01's rig.
**Steps** `ctest -R "percent fits the page|keeps its zoom while|keeps the reader on"`.
**Expect** The reader stays on the page they fitted or zoomed; Fit Width fits that page's width; a fit
keeps its zoom while the reader scrolls and moves only with a navigation. **If it fails** S2 — I-11
is back, and `Ctrl+2` can draw a page at twice the window's width.

**A-69 · Document Properties shows what it read, and edits nothing**
✅ **2026-09-23, T12** (`tests/ui/tst_properties.cpp`). Every Info field, the version, the page count, the current page's size **named by its LABEL** where the document has one, the security line, the repaired line, and the path and size — the last two from the identity taken at OPEN (D3-16) rather than a fresh `QFileInfo`, which would describe a file that may have changed since. A date `Metadata::parseDate` refuses is shown **verbatim** and one it accepts is formatted, asserted as two visibly different treatments rather than one lenient one. **Nothing is editable, and that is asserted mechanically**: the dialog contains **no `QLineEdit` at all**, and every value label is selectable and carries no `Qt::TextEditable` — a read-only edit box looks like a field somebody forgot to enable. An absent Info field reads as an em dash rather than as blank, because most generated files have no `/Info` and a blank row looks like a field that failed to load. `mutation-lint` reporting **one command** on this task's day is the *constructs no command* half (D4-1), which is a stronger claim than any assertion in the case. **What this case does NOT do:** it never `exec()`s the dialog — offscreen a modal is shown to nobody and never returns (Stage 1's I-23) — so the widget is constructed and read, and `Ctrl+D`'s reachability is asserted on the REGISTRATION instead, including that it is disabled where there is nobody to ask (convention 16). The *file hash unchanged* clause is met by D4-1's gate rather than by re-hashing: this stage constructs no command, so there is no path from this dialog to a byte.
**Expect** `Ctrl+D` opens the dialog; every Info field, the version, the page count, the current page's size, the file size and path, and whether the document was repaired or is encrypted; **no field is editable and the dialog constructs no command**; the file's hash is unchanged after opening and closing it. **If it fails** S1 for a change; S3 for a wrong figure.

**A-70 · Reading mode hides the chrome and restores it exactly**
✅ **2026-09-23, T12** (`tests/ui/tst_properties.cpp`, four cases). `Ctrl+L` hides the header — ribbon and Quick Access Toolbar together — and the docks, and **keeps the window chrome**: the window state is asserted NOT full screen, because reading mode is the chrome and `F11` is the window state (D4-12) and the two compose. **It restores the same window and not a default one:** the sidebar comes back at the width it had (read from the WIDGET, not from `rememberedWidth()` — a person who has just dragged the splitter has a width the dock has not written down yet) and on the edge it was docked to. **The control is the one that separates *restore what was there* from *restore everything*:** a sidebar that was CLOSED before entering reading mode comes back closed, which is the only case in which the two differ. The command toggles and is **enabled with no document open**, deliberately — a person who has just closed their last tab must still be able to get their ribbon back. **One deliberate departure from this case's wording:** the status bar STAYS. `Features.md` §1.2 calls the mode *maximum page area*, not *no information at all*, and somebody who has hidden the ribbon has no other way to find out which page they are on.
**Expect** `Ctrl+L` hides the ribbon and panes and keeps the window chrome; a second press restores **the same** panes, widths and dock sides. **If it fails** S3.

### 5.14 Settings, pages, and everything before

**A-71 · The nineteen keys round-trip, and the two pages ship** ✅
**2026-09-23, T14.** All **nineteen are declared in `core/settings/SettingsKeys.def`** and all
nineteen resolve a `prefs.setting.*` label in **both** `en` and `zh-TW` — counted from the key list
and the string dump rather than read off a page. **Eight of 26 groups ship**, re-derived rather than
quoted: the groups carrying at least one non-`Internal` key are General, Language, Logging,
Documents, PageDisplay, PerformanceAndCache and this stage's two, **Units & Guides** and **Full
Screen**, against 26 user-facing values in `SettingsGroup` (27 cases, less `Internal`, which rule 4
says gets no Preferences row). Both new pages are generated from the table with no widget code
(convention 17), and the settings suites are green.
**The control holds, and it is a compile-time one:** `pageDisplay/continuousScroll` and
`units/snapToObjects` are **not declared** — measured against the table, not assumed — so a case
that read either would not compile, which is what D4-5 chose over a key that reads back as set and
does nothing.
**The number is a CORRECTION, re-derived 2026-09-23 at T13** from `core/settings/SettingsKeys.def` rather than
carried forward: Stage 4 declares **nineteen** keys — one `pageDisplay/`, eleven `units/`, five `fullScreen/`
and the two `general/` T13 added — and *fifteen* was this case's figure from before T8 raised it, while
*eighteen* is D4-5's headline disagreeing with D4-5's own enumeration. Both are fixed in place.
**Expect** Each of the nineteen writes and reads back through `settings.ini` with its declared default and, where ranged, its clamp; **Preferences shows eight of 26 groups**, Units & Guides and Full Screen generated with no widget code (convention 17); `--lang zh-TW --dump-ui-strings` resolves both page names and every row label. **The control:** `pageDisplay/continuousScroll` and `units/snapToObjects` **do not exist** (D4-5), and a case that reads them fails to compile. **If it fails** S2.

**A-72 · The Stage 4 workload, re-measured through the shipped panes** ✅
**2026-09-23, T14** (`tests/bench/bench_surface`, four runs). **A2 at Stage 4 is now a plain `go`**,
and the verdict moved because of it: T2 had withheld the plain verdict for the two-window column
alone, and that column is **4.01–4.75 ms** where it was 18.91–19.33 ms, because T11's named work —
a window that is not in front enqueues at `Prefetch` — is built and is now measured rather than
merely asserted. D4-16's own workload, the split plus the thumbnails pane, is **7.03–8.56 ms against
the 16.667 ms budget**. The table, the reasoning and the bounds are in `Tech-Stack-Plan.md` §3.3,
which owns the figure; this case owns only whether it was taken properly.

**IT WAS NOT, TWICE, AND BOTH CORRECTIONS ARE THE CASE EARNING ITS KEEP.** The first run of the new
bench reported the viewport at **0.361 ms against T2's 9.78** — which is not a 27× product, it is a
viewport of five tiles being compared with one of several dozen, because the harness ran at the
`offscreen` default of 96 dpi and zoom 1.0 instead of A2's 288 dpi over 1920×1080. Recording that
beside T2's figure as *the product's* would have been **D2-1's exact error committed inside the case
written to prevent it**. The second run set `QT_SCALE_FACTOR=1.5` and zoom 200 % and reached
**199.9 dpi, still not 288** — `DpiPolicy` reads the monitor's PHYSICAL dpi, which offscreen reports
as about 100 because there is no `IDisplayTopology` to join against (**I-20**, met from the other
side). The bench now SOLVES for the zoom that puts `resolution().dpi` at 288, grows the window until
the viewport is 1920×1080 device pixels, and **prints what it actually reached beside what it asked
for** — so a third version of this mistake is visible in the output rather than in the conclusion.
**A third correction, found the same way:** a `MainWindow` is built with the sidebar open and
`SidebarDock::addPane` shows the first pane registered, so the `viewer` and `+ split` columns had a
live thumbnails pane in them and `+ pane` added nothing — three measurements of one workload. The
columns without a pane now hide the sidebar, and the pane's column scrolls the pane each round,
because a pane that streams once at open and then sits still is not the pane D4-10 is about.

**What the figure does not claim** is in §3.3 with it, and the short form is: offscreen, one rig, one
generated corpus, and nothing here draws to a 60 Hz display — **M-06 is still owed** and is the only
thing that can say whether a 900-page pane *feels* smooth.
**Goal** D4-16's second reading — the bench is a request pattern, the product is the product (D2-1's lesson, where the probe said 31× and the product said 1.18×).
**Expect** A viewport p95 beside T2's figure, and **the verdict either confirmed or corrected in `Tech-Stack-Plan.md` §3.3 in the product's words**. **If it fails** S2 — a bench figure quoted as the product's is the exact error D2-1 exists to prevent.

**A-73 · The two census cases, and `--list-actions`** ✅
**2026-09-23, T14.** `--list-actions` reports **373 declared / 94 registered / 279 unregistered**,
and the two derived censuses agree with it: `tst_viewercommands` asserts **30 Viewer rows registered
and 9 not**, `tst_context` walks the whole binding table, and both are green. **The thirteen Viewer
rows that were unregistered at Stage 3's sign-off lost exactly four** — `view.readingMode` (T12),
`view.toggleRulers`, `view.toggleGrid` and `view.toggleSnapToGrid` (T8) — read from the run, one id
at a time. **Every one of the nine that remain belongs to a later stage by name:** `view.reflow`,
`tool.snapshot`, `compare.documents`, `nav.nextDifference` and `nav.prevDifference` are **Stage 5's**
(the last four with D4-15's correction behind two of them); `view.togglePropertiesBar`,
`nav.nextComment` and `nav.prevComment` are **Stage 6's**; `bookmark.new` is **Stage 7's**. None is
an oversight.
**Expect** `rawpage.exe --list-actions` reports the new registered/unregistered split; `tst_viewercommands`' derived census and `tst_context`'s agree with it. **The thirteen Viewer rows that were unregistered at Stage 3's sign-off** — `view.readingMode`, `view.togglePropertiesBar`, `view.reflow`, `view.toggleRulers`, `view.toggleGrid`, `view.toggleSnapToGrid`, `nav.nextComment`, `nav.prevComment`, `nav.nextDifference`, `nav.prevDifference`, `tool.snapshot`, `compare.documents`, `bookmark.new` (read from `--list-actions`, 2026-09-18) — **lose the four this stage registers, and every one that remains belongs to Stage 5, 6 or 7 by name**. Both census numbers are read from the run rather than predicted here. **If it fails** S3, corrected in the same change.

**A-74 · Every prior stage's case is still present, by name, and green** ✅
**2026-09-23, T14** — **and re-run 2026-09-24 with I-10's fix at 1108/1108 on both presets**, the
two added being A-80's halves. T14's reading: **1106/1106 on `x64-release` and 1106/1106 on
`x64-debug`**, both from the CLEAN builds A-01 made, one explicitly skipped (the battery case, on a
desktop that has none); the `gates` preset **124/124** with no Qt. Stage 3 signed off at 923; the
183 added were this stage's, and **no PRIOR stage's case was renamed or removed** — which is this
case's claim, and it survives I-10's fix renaming one STAGE 4 case
(`fitRect measures three different rectangles`, whose old name asserted the defect).
**The golden clause, stated precisely because it is the one that could be read too kindly:** the
only changes under `golden/` since Stage 3's sign-off are **nine NEW baselines** for T1's three new
corpus files, plus their `manifest.json`, `captured.json`, `README.md` and `UPDATES.md` rows. **Every
pre-existing baseline is byte-unchanged** — `git diff` shows the nine as additions (`Bin 0 -> …`) and
not one modification. That is what A-74 asks for: Stage 4 changes no rasterization, so a MOVED
baseline would be a finding and a new file for a new corpus file is not one.
**Steps** `ctest --preset x64-release -N` against the Stage 3 sign-off list; then the full run on both presets and the `gates` preset.
**Expect** Every Stage 0–3 test name present or its rename named; all green, one known skip; the **golden suite green with its baselines byte-unchanged** — Stage 4 changes no rasterization, so a moved baseline is a finding (D2-3). **If it fails** S1 for a regression; S2 for a silently renamed case.

### 5.15 Session restore through the product's own composition (T13)

**A-75 · Quit and relaunch brings back the documents, the order and the layout** ✅
**2026-09-23, T13** (`tests/composition/tst_restore.cpp`). Three documents, the second split vertically, the
third in Two-Up Continuous at 150 % on page 3, the Thumbnails pane open and the second tab in front — captured
by `SessionShell::captureNow()`, which is what `aboutToQuit` calls, and read back by a SECOND
`CompositionRoot` and a SECOND `MainWindow` over the same scratch directory, the first pair destroyed before
the second is built. **The tab order is asserted as the whole list in one comparison**, not one document at a
time: a restore that brought back the right three in the wrong order satisfies every per-document check there
is. **What this case cannot see** (convention 19): `app/main.cpp` is not run — it makes the three calls in
D4-9's order itself, which proves the order works and not that main.cpp has it, and M-05 is where that is
seen.
**Goal** D4-9 end to end, through `CompositionRoot` — `tst_restore` in `rawpage_composition_tests`, the only target that links the engine, the widgets and the OS bundle together.
**Steps** Open three documents, split one, open a pane on another, arrange the windows, quit cleanly, relaunch.
**Expect** The same three documents, in the same tab order, with the same active tab, the same split and the same pane, and each at the scroll position and layout mode it had. **If it fails** S2; S1 if a document is lost.

**A-76 · A document closed with *Don't Save* comes back CLEAN** ✅
**2026-09-23, T13.** Rotate, close the WINDOW, answer *Don't Save* through the dialog's twin, quit, relaunch:
the document is back, `isDirty()` is false, page 0's rotation is 0, the journal's history is empty and **the
file's SHA-256 is the one taken before the rotation**. The recovery directory is gone with the discard
(D3-14), so nothing offers the edit back by the other route either.
**THE READING THIS CASE TAKES, because the wording admits two.** *Close with Don't Save → quit* is read as the
close that IS the quit — the window closing, the protocol asking, the person declining — and not as a tab
closed with `Ctrl+W` an hour earlier. The second reading would mean a closed tab comes back, which contradicts
A-75's *three documents were open and three came back*.
**And it is the case that shaped the design.** The close protocol REMOVES a tab before `aboutToQuit` is
emitted, so a session captured only at quit forgets the document entirely. **Its planted-bug control is
exactly that**: with `SessionShell::snapshot()` ignoring the close-time stash, this case reddens with
`restored.documents == 0` — the document is not resurrected wrongly, it is *lost*, which is the failure the
stash exists to prevent and the reason the stash is not an optimisation.
**Goal** D4-9's control — the clause that separates restore from recovery.
**Expect** After rotate → close with *Don't Save* → quit → relaunch: the document reopens, is **not dirty**, has no rotation, and its recovery directory is gone (Stage 3 D3-14 removed it on the Discard). **If it fails** **S1** — a resurrected edit is the product telling the user their decision did not count.

**A-77 · Restore runs after recovery, and never opens the same document twice** ✅
**2026-09-23, T13.** The kill probe plants a journal, `RecoveryPrompt` recovers into the first window, and
restore then finds that path open and skips it — one tab in one window holds the file, and the recovered tab
is still the recovered one (dirty, with its replay report) rather than a clean tab at the bytes on disk.
**THE SESSION HAS TWO WINDOWS, AND THAT IS THE DESIGN OF THE CASE RATHER THAN THOROUGHNESS.** In ONE window
the skip is guarded twice: `MainWindow::openFiles` already refuses a path that window holds and focuses the
existing tab instead (Stage 2 T13), so a restore that had forgotten to skip would still not make two tabs —
and the case would have gone green for the wrong reason. **That guard is per window.** A session whose second
window names the document recovery opened in the FIRST is the one arrangement where nothing below
`SessionShell` is watching, and the first draft of this case was the single-window shape until its control
said so. **The control, with the skip removed:** `2 tab(s) in 2 window(s) hold the recovered file` — two
`DocumentHandle`s and two `CommandJournal`s over one file, which is the S1 in the line below, reproduced. The
case's mechanism assertions are `CHECK` and not `REQUIRE` deliberately, so a planted bug shows the harm and
not only the mechanism.
**Steps** Plant a crashed session's journal for a document that is also in the saved session (the kill probe writes one — Stage 3 A-74); relaunch.
**Expect** `RecoveryPrompt` offers it first; whatever it opens, restore **skips that path**; exactly one window holds that document. **The control:** with no crashed journal, restore opens it as usual. **If it fails** S1 for two copies of one document, which is two journals over one file.

**A-78 · The three memory switches** ✅
**2026-09-23, T13**, three cases, each asserted on what came back. `restoreSession` off restores nothing **and
writes nothing** — the session file is byte-identical across a launch that opened a document and quit, because
a product that went on recording what was read after the person asked it to stop would be answering a
different question from the one on the checkbox. `rememberWindowLayout` off leaves the window at the rectangle
it was built with; `rememberPaneLayout` off leaves the sidebar CLOSED, which is a layout rather than an
absence — a window is built with the sidebar open, so "do nothing" would have made the switch look inert.
**Each has the ON case run first**, so the OFF assertion is a difference and not a coincidence (convention 7).
**One thing the geometry case had to learn**, and it is convention 7 catching the case rather than the
product: a `MainWindow` carries a ribbon and will not go below its minimum width, so the first draft's 820-wide
rectangle was 1222 wide on *both* sides of the relaunch and the case passed while proving nothing. It now
asserts against the rectangle the window actually took, component by component, since Catch cannot print a
`QRect` and `{?} == {?}` names none of the four numbers.
**Expect** `general/restoreSession` off restores nothing and captures nothing new; `general/rememberWindowLayout` off restores documents at default geometry; `general/rememberPaneLayout` off restores documents with no panes. Each is asserted **on what came back**, not on the setting. **If it fails** S2.

**A-79 · A test composition writes its own session file, never the user's** ✅
**2026-09-23, T13.** `%APPDATA%\RawPage\session.json` is read — existence, mtime and SHA-256 — before a full
capture-and-restore round trip and again after it, and the scratch directory's `session.json` is the one that
moved. The case names `SessionState::defaultFilePath()` rather than spelling the path, so it follows the
product if the location ever changes. **Every launch in this file points its settings file, its session file
AND its recovery root at the scratch directory**, which is the trap in all three of its known subjects.
**Goal** The trap Stage 3 T11 recorded for the recovery root, with a second subject.
**Expect** `tst_restore` points the session root at a scratch directory; after the suite, `%APPDATA%\RawPage\session.json` is untouched (mtime and hash read before and after). **If it fails** S1 — a suite that reopened the tester's documents, or replaced their session.

---

## 6. Manual verification — you run this

Twelve cases. Every one needs a second monitor, a scale factor the harness cannot have, a display
removed while the product runs, an input device, or a judgement about smoothness or legibility that
no assertion makes. **Anything here that could have been automated is a defect in this guide — say so
and I will move it.**

**Walk M-06 as soon as T5 lands** — it is the human half of the stage's verdict, and if the pane is
not smooth the answer matters before T9 builds a second viewport on top of it. The rest are walkable
from T13, except M-11 (T12) and M-07 (T8).

Three things about the state you will be walking, so a planned absence is not reported as a defect:

- **Nothing you do in Stage 4 changes the document.** If a file's *modified* timestamp moves, that is
  a finding, not a feature.
- **The Selection tool selects nothing**, and a thumbnail cannot be dragged to a new position.
- **Save As, Save and undo still behave exactly as Stage 3 left them** — this stage adds nothing to
  them and breaks nothing in them, and if it has, that is an S1 belonging to this guide.
- **Two things CHANGED at T12 and are not defects.** `Ctrl+3` no longer matches `Ctrl+0`: it fits the
  page's ink, so on `cropbox-margins.pdf` it is visibly tighter and scrolls to the content — that is
  Stage 2's **I-28** closed, and a `Ctrl+3` that still matches `Ctrl+0` on that file is now the
  finding. And a bookmark or named destination now lands at the destination's **point**, not its
  page's top. **`/FitR` is the exception** and frames nothing: `core::Destination` has no fields for
  its rectangle, which is Stage 7's.

---

**M-01 · Two monitors, two scale factors, one document** ✅ **signed off by the user, 2026-09-26, on
its RE-WALK** — the first walk that day FAILED and raised **I-11 (S2)** and **I-12 (S3)**; both were
fixed the same day, and the re-walk on the fixed build passed every step.
- **Re-walk result, 2026-09-26** On the rebuilt binary — relinked with the product window closed,
  both presets 1114/1114 — **all five steps passed**, including the two the first walk failed:
  step 5's Fit Width and Fit Page leave the reader on the landscape page and fit it, and step 3's
  divider is visible. No new finding.
- **First walk result, 2026-09-26** Walked on the P-1 rig to the end, all five steps. **Step 5:** `Ctrl+2` from 400 %
  on the landscape page came out at **250 %** with the page far wider than the window — **I-11**: the
  fit re-measured the A5 page (419.5 pt wide, half the landscape page's 841.9 pt, hence twice the
  zoom) once dropping from 400 % had scrolled the viewport onto it. **Step 3:** the vertical split
  showed **no visible boundary** between its panes — **I-12**: the stock handle is painted in the
  desk's own colour. No other finding was reported.
- **What the re-walk looked at specifically:** step 5 — `Ctrl+2` and `Ctrl+0` leave you on the
  landscape page, and it fits the window's width (roughly half the 250 % you saw) and then its whole
  page; and step 3 — a grey band with a darker line down its middle between the two panes, turning
  the accent blue under the pointer, **on both monitors and in both themes**.
- **Goal** The whole Stage 4 surface on the rig A2 was specified on, where every layout bug lives.
- **Pre** P-1. A copy of `corpus\ordinary\mixed-page-sizes.pdf` in `tests\manual\M-01\`.
- **Steps**
  1. Open the copy on the **150 %** monitor. Open the Thumbnails pane, then the Bookmarks pane.
  2. Drag the window to the **100 %** monitor, and back. Watch the pane, the ruler ticks and the page.
  3. Split the window vertically; scroll both panes; turn synchronized scrolling on and off.
  4. Half-drag the window so it straddles both monitors.
  5. Zoom to 400 % on a landscape page, then Fit Width, then Fit Page.
- **Expect** Thumbnails, ruler ticks, text and pane chrome are **crisp on both monitors** and resize
  at the boundary without a lasting blur; nothing overlaps; the split's divider lands where you
  dropped it; no page is drawn at the wrong size after a monitor change.
- **If it fails** S2; S1 if a page is shown at the wrong size or the window becomes unreachable.

**M-02 · Full Screen on the monitor you chose** ☐
- **Goal** `fullScreen/monitor` and the `QScreen` join (**I-20**), which no offscreen run can exercise.
- **Pre** P-1.
- **Steps** Set Full Screen to the secondary monitor in Preferences → Full Screen. Press `F11` with
  the window on the **primary**. Press `Ctrl+Shift+M`. Press `Esc`. Repeat with the setting on
  *the monitor the window is on*.
- **Expect** Full Screen opens on the monitor you named, not the one the window was on; the other
  monitor keeps showing what it showed; `Ctrl+Shift+M` moves it; `Esc` returns the window to exactly
  the position and size it had.
- **If it fails** S2; S1 if the window does not come back.

**M-03 · A presentation, start to finish** ☐
- **Goal** Transitions, auto-advance, blanking, the pointer, and the machine's sleep behaviour.
- **Pre** `corpus\ordinary\transitions.pdf` copied to `tests\manual\M-03\`. Note your screen-saver /
  sleep timeout before you start.
- **Steps**
  1. Enter presentation mode. Page through with `Space` and `←`; watch each transition.
  2. Turn on auto-advance (`Ctrl+Shift+H`) with a short interval and `loop` on; let it wrap.
  3. Press `B`, then `W`, then `B` again; return to the slide.
  4. Press `P` and move the pointer; press `Ctrl+L` twice.
  5. Leave the presentation running, untouched, past your screen-saver timeout. Then exit and leave
     the machine idle past it again.
- **Expect** Each page's own transition is used where it has one; auto-advance keeps the interval and
  loops; blanking is instant and returns to the same page; **the screen saver and sleep do not start
  during the presentation, and DO start after you exit** — that second half is the control.
- **If it fails** S3 for a transition; **S1 if the machine cannot sleep or lock after you exit.**

**M-04 · Take a monitor away** ☐
- **Goal** D4-8 and D4-12's topology clause, and **P-2**'s reason for existing.
- **Pre** P-2 — **`Win`+`P` → *PC screen only*** (chosen 2026-09-19). Windows keeps the display's
  identity under this method, so step 3 restores onto a fingerprint the product has not seen while
  step 1 returns to one it has. Say so if you use another method instead, because they differ in what
  Windows remembers.
- **Steps**
  1. Put a window on the secondary monitor. Remove that monitor. Restore it.
  2. Start a presentation on the secondary monitor. Remove it mid-slide.
  3. With both monitors back, quit cleanly; remove one; relaunch.
- **Expect** Step 1: the window reappears on a monitor you can see, at a usable size. Step 2: the
  presentation moves to the primary monitor, still presenting, at the same page. Step 3: every
  restored window is reachable — at least its title bar on a real screen.
- **If it fails** **S1** for anything unreachable; S2 for a window that survives but at a silly size.

**M-05 · Session restore, across a change of shape** ☐
- **Goal** D4-9, and the fingerprint on a real topology.
- **Pre** P-1, P-2. Three copies of corpus files in `tests\manual\M-05\`.
- **Steps**
  1. Open all three; split one; open the Thumbnails pane on another; arrange the windows; quit with
     `Ctrl+Q`.
  2. Relaunch. Read what came back.
  3. Rotate a page in one document and close its tab with **Don't Save**. Quit. Relaunch.
  4. Remove a monitor, relaunch, restore it, relaunch again.
  5. Turn `general/restoreSession` off, quit, relaunch.
- **Expect** Steps 1–2: the same documents, tab order, window positions, split and pane. Step 3: the
  document comes back **clean** — no unsaved marker, no rotation. Step 4: windows land on monitors
  that exist in both arrangements, and the two-monitor layout comes back when the monitor does.
  Step 5: nothing is restored.
- **If it fails** **S1** for a resurrected edit or a lost document; S2 for a lost layout.

**M-06 · A 900-page thumbnails pane, judged by a person** ☐
- **Goal** The human half of §3's verdict — A-14's p95 and this can disagree.
- **Pre** `corpus\ordinary\pages-900.pdf`.
- **Steps** Open it; open the Thumbnails pane; scroll the pane fast from top to bottom and back;
  while the pane is still filling, scroll the **page view** and page through with `Ctrl+Page Down`;
  then drag a thumbnail's scrollbar to the end and immediately click page 900.
- **Expect** The page view stays responsive **while the pane is working** — this is the property
  strict priority exists for; thumbnails fill in progressively and out-of-view work stops when you
  scroll away; nothing freezes for more than a moment; memory does not climb without bound while you
  scroll (watch Task Manager, and read `liveHeapMB` rather than private bytes if you use the bench).
- **If it fails** S2, **and it is evidence for §3's verdict** — record what you saw, not only that it
  was bad.

**M-07 · Rulers, grid, guides and snapping by hand, at 50 % and 400 %** ☐
- **Goal** The exit criterion's *"identically at 50 % and 400 %"*, which is about how it feels.
- **Pre** `corpus\ordinary\text-a4.pdf` copied to `tests\manual\M-07\`.
- **Steps** Show the rulers (`Ctrl+U`) and the grid (`Ctrl+'`). Change the unit through all five.
  Drag two guides out of each ruler. Turn on Snap to Grid (`Ctrl+;`) and drag a guide near a grid
  line, at 50 % and then at 400 %. Lock one guide and try to move it. Clear all guides. Quit and
  relaunch.
- **Expect** The readout matches the ruler; the snap **grabs from the same visual distance at both
  zooms**; a locked guide does not move; guides come back after the relaunch and the file's
  timestamp has not changed.
- **If it fails** S2 for the snap distance; **S1 if the document's timestamp moved.**

**M-08 · The spreadsheet split** ☐
- **Goal** The freeze, which is the split nobody gets right by accident.
- **Steps** On a wide document, take a spreadsheet split; scroll the body right and down; resize the
  window; change the zoom; remove the split.
- **Expect** The frozen row and column stay put and stay aligned with the body at every zoom and size;
  removing the split leaves one view showing the same place.
- **If it fails** S2.

**M-09 · Arranging windows** ☐
- **Steps** Open four documents; detach two into their own windows; tile horizontally, then
  vertically; cascade; use the window list; merge all windows. Then tile with windows on **both**
  monitors.
- **Expect** Tiles fill each monitor's work area without overlapping the taskbar; cascade keeps every
  title bar reachable; merge restores one window with every tab in order and every document where it
  was; a tile with windows on two monitors does something sensible rather than stacking them all on
  one.
- **If it fails** S2; S3 for the two-monitor tiling if it is merely unhelpful.

**M-10 · Panes, reading mode, and what is remembered** ☐
- **Steps** Open and resize the sidebar; auto-hide it and peek; float a pane and re-dock it; enter
  reading mode (`Ctrl+L`) and leave it; switch between two documents with different panes open; quit
  and relaunch with `general/rememberPaneLayout` on, then off.
- **Expect** Reading mode restores exactly what it hid; each document keeps its own open panes;
  widths come back with the setting on and do not with it off.
- **If it fails** S3; S2 if a pane comes back attached to the wrong document.

**M-11 · Fit Visible, beside Fit Page** ✅ signed off by the user, 2026-09-24 — **the first manual
case walked in this stage, and it raised I-10 (S2)**
- **Walk result** On `cropbox-margins.pdf`: `Ctrl+0` **89 %**, `Ctrl+1` **100 %**, `Ctrl+2` **331 %**,
  `Ctrl+3` **143 %** — three different zooms, and Fit Visible fills the window with the ink panel
  rather than the white margins, which is the thing this case exists to look at. On `text-a4.pdf`:
  **89 / 100 / 251 / 106 %** — `Ctrl+3` and `Ctrl+0` close, as the case predicts for a page whose ink
  nearly fills it. **Page 2, the empty one:** `Ctrl+1` 100 %, `Ctrl+3` **108 %** — the CropBox
  fallback (D4-14), and NOT the MediaBox's 89 %, which is the distinction only this file can show.
  **Both files' timestamps unmoved** before and after, so nothing this stage does reaches the
  document.
- **The eight readings are mutually consistent and pin the rig**, which is how a by-eye case became
  arithmetic somebody can re-check: they solve to a viewport of **1991 × 998 device pixels at ~96
  DPI**, and every one of them is that viewport divided by a named rectangle — 89 % = 998 ÷
  (841.89 × 1.3333), 143 % = 998 ÷ (521.89 × 1.3333), 331 % = 1991 ÷ (451.276 × 1.3333), and so on.
  A percentage that matched no rectangle would have been the finding.
- **Walked with two steps this case does not ask for**, both kept here because they are what made the
  result arithmetic rather than an impression: **`Ctrl+2`** on each file, whose two different answers
  (331 % and 251 %, the two CropBox widths) is what proves the two documents were measured
  separately — the first attempt reported one file's four numbers for both, and only Fit Width made
  that visible — and **page 2's fallback**. Whoever re-walks this should walk those too.
- **AND THE WALK FOUND WHAT THE GREEN AUTOMATED HALF HAD RATIFIED:** `Ctrl+0` fits the **MediaBox**
  while the product draws the **CropBox**, so on this file Fit Page leaves **17 % of the window
  empty** and page 2's top edge shows below the page. That is **I-10** (§8), S2, open. **M-11 itself
  passes**: it asserts three different zooms and an ink-filled window, and both are there.
- **Goal** Stage 2's **I-28** judged the way it was found — by looking.
- **Pre** `corpus\ordinary\cropbox-margins.pdf`.
- **Steps** Press `Ctrl+0`, then `Ctrl+3`, then `Ctrl+1`, on that file and then on `text-a4.pdf`.
- **Expect** On `cropbox-margins.pdf` the three are **visibly different**, and `Ctrl+3` fills the
  window with the ink rather than the white margins. On `text-a4.pdf`, whose content nearly fills its
  page, `Ctrl+3` and `Ctrl+0` are close — and that is correct, which is why the first file exists.
- **If it fails** S2, and **I-28 stays open in Stage 2's guide** rather than being closed here.

**M-12 · Input devices** ☐
- **Pre** P-4 — a wheel mouse, and **no pen or touch screen on this rig** (confirmed 2026-09-19), so
  the pen/touch step below is recorded *not run for want of hardware* rather than skipped silently.
- **Steps** Wheel-scroll, then wheel-zoom with the modifier, then swap them with
  `pageDisplay/wheelZooms`; hold the temporary-hand modifier and pan; run auto-scroll and change its
  speed, pause and reverse it; use the loupe; if you have a pen or touch screen, try the same
  gestures with it.
- **Expect** Each gesture does what §1.2 says; the temporary hand never leaves the previous tool
  disarmed; auto-scroll stops at the end rather than spinning. **Touch is not implemented this stage**
  (`general/touchMode` is Stage 1's inert key), so clumsy touch targets are **not** a defect — a
  crash or a gesture that moves the document is.
- **If it fails** S3 for feel; S2 for a gesture that does nothing; S1 for a document change.

---

## 7. Known caveats — verify, do not assume broken

Behaviours that look like defects and are not. **A wrong row here is worse than a missing one.**

| Looks broken | Is |
|---|---|
| **A thumbnail cannot be dragged to reorder pages** | Correct at Stage 4 (D4-1). Stage 7 |
| **`Ctrl+R` in the Thumbnails pane rotates the current page, not the selected ones** | Correct — the selection is not yet a page set (D4-1). Stage 7 |
| **A bookmark cannot be renamed or deleted** | Correct (D4-2). Stage 7 |
| **Dragging with the Selection tool selects nothing** | Correct since Stage 2 T14. Stage 5 |
| **`Ctrl+4`, `S` and `Ctrl+E` do nothing and the key falls through** | Correct and deliberate — `Ctrl+4` and `S` are Stage 5's (D4-15), `Ctrl+E` Stage 6's |
| **Seven pane chords do nothing and their panes are not listed** | Correct (D4-2) — a pane cannot precede its model |
| **Document Properties cannot be edited, and shows no XMP** | Correct (D4-4). Stage 8 |
| **Hairlines are still one pixel wide however `pageDisplay/lineWeight` is set** | Stage 2's **I-11**, open by decision — a PDFium capability gap, not this stage's |
| **The second level of ribbon key tips still does nothing** | Stage 2's **I-34**, untouched here |
| **Thumbnails appear progressively rather than all at once** | Correct and intended (D4-10) — they are speculative work behind the page you are reading |
| **A thumbnail scrolled away stops rendering** | Correct — its ticket is cancelled, as the page view's are |
| **`Ctrl+3` and `Ctrl+0` agree on most corpus documents** | Correct — their content fills their page. `cropbox-margins.pdf` is the file where they differ (A-68) |
| **Guides are invisible in every other PDF reader** | Correct — guides are view state and are never written to the file (D4-7) |
| **A restored window is not exactly where it was, after the monitors changed** | Correct — it was clamped onto a desktop that exists (D4-8). Unreachable is the defect; moved is the design |
| **A document closed with *Don't Save* comes back with no changes** | Correct (D4-9) — unsaved work is recovery's, and only after a crash |
| **The session does not come back after a crash** | Correct — a crash is the **recovery** prompt's (D3-11), and the session is captured on a clean quit |

---

## 8. Issue log

Numbered `I-nn`, **this stage's own namespace**. Each entry carries its mechanism, why the suite
missed it, and the control that was run red before the fix — the shape every earlier stage's log
uses.

| # | Sev | Title | State |
|---|---|---|---|
| **I-01** | **S3** | **Guides are keyed by `DocumentId`, and a save changes the `DocumentId`** | ✅ **Closed 2026-09-23 at T13**, where this row said it would be — by a capture-time rule, and D4-7's *reasoning* corrected rather than its key |
| **I-02** | **S4** | **Seven new Catch2 case names carried a COMMA, so none could be selected by name** | ✅ **Closed 2026-09-19 at T4**, by renaming them |
| **I-03** | **S3** | **A Qt 6.5+ API in `core/` broke both Linux jobs: the portable subset builds against Ubuntu's Qt 6.4** | ✅ **Closed 2026-09-19**, and written up as **convention 24** |
| **I-04** | **S2** | **A pane swallowed `Alt+↑`/`Alt+↓` and moved the selection — a Stage 7 chord doing something plausible and wrong** | ✅ **Closed 2026-09-20 at T5** (Thumbnails) **and again 2026-09-21 at T6** (Bookmarks): one mechanism, two panes, two layers of Qt |
| **I-05** | **S3** | **A destination's FIT is read, carried and not applied — a `/XYZ` point lands at its page's top** | ✅ **Closed 2026-09-23 at T12**, by the arithmetic D4-14 promised — and it found a second defect on the way in |
| **I-06** | **S2** | **Registering `edit.cancel` — §8's `Common` row — swallowed `Esc` application-wide, including the tool finder's mouse-grabbing popup** | ✅ **Closed 2026-09-21 at T7**: the id is left unregistered and `Esc` is handled where the drag is |
| **I-07** | **S4** | **A page colour can be INVERTED but not SUBSTITUTED: no declared settings key holds a colour pair, so `view.pageBackground` stays unregistered** | ⚪ **Open — a scope finding, owned by Stage 8's colour work; nothing is broken and nothing is inert** |
| **I-09** | **S2** | **`Qt::UniqueConnection` asked for with a LAMBDA slot: fatal in `x64-debug`, and silently NO CONNECTION AT ALL in `x64-release`** | ✅ **Closed 2026-09-23 at T13**, by guarding uniqueness with the container the connection is about |
| **I-08** | **S3** | **FOUR targets had been compiling at the compiler's DEFAULT language standard, not C++20 — `rawpage_fuzz_seeds` since Stage 3 T13 and the three `tests/fuzz` drivers since Stage 2 T5** | ✅ **Closed 2026-09-23 at T12**, in two rounds: the flags call each target lacked, and then `rawpage_assert_language_standard()`, which refuses to configure a target that omits it |
| **I-10** | **S2** | **Fit Page measures the SHEET while the product draws the CROP: `Ctrl+0` leaves 17 % of the window empty on a page whose CropBox is inset** | ✅ **Closed 2026-09-24, the day M-11 raised it** — `FitMode::Page` measures the CropBox, the assertion that pinned the sheet is inverted, and **A-80** asserts the zoom against the drawn page |
| **I-11** | **S2** | **On a document of mixed page sizes, `Ctrl+2` from 400 % drew the landscape page at TWICE the window's width: the fit re-measured whichever page the zoom change had scrolled onto** | ✅ **Closed 2026-09-26, the day M-01 raised it** — every fit is anchored to the page it was chosen on, a zoom command keeps the reader's place, and **A-81** asserts both over mixed sizes |
| **I-12** | **S3** | **A split had no visible divider: the stock `QSplitter` handle is painted in `QPalette::Window`, which is the desk's own colour** | ✅ **Closed 2026-09-26, the day M-01 raised it** — a handle that paints itself from the theme tokens, and **A-82** measures it against the desk in both themes |

**I-01 · Guides are keyed by an identity that the first save invalidates.** *(Raised 2026-09-19 at T3, while implementing D4-7 rather than by a failing case — which is itself the finding: no case in §5.5 would have caught it, because a round trip through one `SessionState` never changes a document.)*

**Mechanism.** D4-7 says guides *"live in `SessionState`, keyed by `DocumentId`"*, and justifies that key with *"a renamed file keeps its guides and a different file at the same path does not inherit them"*. `core/doc/DocumentId` (D3-16) is derived from **four** facts — the normalized path, the size, the modification time and the SHA-256 of the content — so:

- a **rename** changes the path and therefore the identity, so the renamed file does **not** keep its guides; and
- a **save** changes the size, the modification time and the hash, so the first save after a guide is placed leaves that guide keyed to an identity no longer on disk.

The second is the one that matters in use: rotate a page, press `Ctrl+S`, and the guides are orphaned. Only the *"a different file at the same path does not inherit them"* half of D4-7's reasoning actually follows from the type.

**Why the suite did not catch it.** Every A-18 assertion is a round trip through one object with one document identity; nothing in §5.5 saves a document, because T3 has no document to save. The case that would catch it belongs where capture and restore meet the shell.

**What is absent, and who owns it.** There is no re-keying of a session entry when a document is saved, and this task did not invent one: the repair is a decision for **D4-7**, taken where the mechanism is wired — **T13**. `SessionState` stores what the decision says today. Recorded here rather than in a commit message because the log outlives the fix.

**CLOSED 2026-09-23 AT T13, AND THE REPAIR IS A CAPTURE-TIME RULE RATHER THAN A NEW KEY.** The finding assumed the identity in a session entry is the one taken when the document was OPENED. It is not, once there is a shell to capture from: `app::SessionShell` reads it at **capture**, at quit, from `IDocumentBackend::documentId()` — and `core::Document::refreshFileState` already re-reads that after every successful save (D3-16, Stage 3 T8, for its own reasons). So by the time anything reaches `session.json` the identity names the bytes that are on disk, and **a save during the session cannot orphan a guide**. What is left is the case the key was actually chosen for: a file changed by something else between the quit and the next launch, whose guides are dropped with a log line while the document still opens — a guide placed against page 4 of a document that has since been re-paginated is not a guide, it is a line in the wrong place.

**What was wrong was half of D4-7's justification, and THAT is corrected rather than the code.** *"A renamed file keeps its guides"* does not follow from `DocumentId` and never did, because the normalized path is one of its four facts; D4-7's `Why` column now says so. The other half — *"a different file at the same path does not inherit them"* — is true, is the reason a path alone would not do, and is exactly what the key delivers.

**Why nothing reddens on this, and what that costs** (convention 19). No case asserts that a save re-keys anything, because nothing in the product ever keyed on the open-time identity: the defect was in the reasoning, not in a code path, and a case written against it would be asserting that a function is not called. What **is** covered, in `tst_restore`, is guides surviving a quit and a relaunch. What is **not** covered is the drop path — no case changes a file between the capture and the restore, so the branch that discards the guides and keeps the document has never run in the suite. That is a real gap, it is named here rather than left to be discovered, and it is cheap for whichever task next touches `SessionShell` to close.

**I-02 · A comma in a Catch2 case name makes the case unselectable.** *(Raised and closed 2026-09-19 at T4.)*

**Mechanism.** Convention 2 (`CLAUDE.md`) says a Catch2 test name is ASCII **and contains no comma**, because a comma separates test specs in a filter: `rawpage_tests.exe "a, b"` asks for two cases named `a` and `b`, matches neither, and exits **non-zero with “No tests ran”**. Seven names added at T3 and T4 carried one.

**Why the suite did not catch it.** It cannot: a full `ctest` run executes every case regardless of its name, so all seven were green the whole time. What breaks is selecting one BY NAME — which is how every planted-bug control in this project is run.

**How it surfaced, and what it nearly cost.** The T4 controls reported five reds. They were spurious: the filters matched nothing, and a Catch2 run that matches nothing exits non-zero, which the driver read as *the control fired*. **A no-tests-ran guard was added to the driver before anything was believed**, the names were fixed, and all five were re-run and fired for real. Uncaught, it would have recorded five controls that had never executed a line of the code they were supposed to break — convention 7's *"a control that does not fail for the right reason is worse than none"*, in its purest form.

**I-03 · `core/` used a Qt API newer than the Qt its own CI builds it against.** *(Raised by CI run 35396454515 on T4's push, 2026-09-19; fixed the same day.)*

**Mechanism.** `core/doc/Metadata::parseDate` applied a date's UTC offset with `QTimeZone::fromSecondsAheadOfUtc`, which Qt added in **6.5**. Local builds and the Windows job use the pinned **Qt 6.11.1** and compiled it clean; `portable-asan` and `portable-fuzz` build `core/`, `pdf/cos/` and `pdf/ops/` on `ubuntu-latest` against `qt6-base-dev`, which is **6.4**. Both failed at the first object file: *‘fromSecondsAheadOfUtc’ is not a member of ‘QTimeZone’*. **Fixed with `QTimeZone(int)`** — the same thing, present since Qt 5.2, and not deprecated in 6.11, which was settled by reading the pinned Qt's own header rather than remembered.

**I-04 · A pane's arrow handler ate a chord that belongs to a later stage — and so did Qt, behind it.** *(Raised and closed 2026-09-20 at T5, by the CONTROL case rather than by the case.)*

**Mechanism.** §8's Page Thumbnails table binds `Alt+↑` and `Alt+↓` to `page.moveEarlier` and `page.moveLater` — Stage 7's commands, deliberately unregistered here (D4-1, §11). An unregistered id resolves to `NotImplemented`, which by design does **not** consume the key, so the event reaches the focused widget. `ThumbnailsPane::keyPressEvent` switched on `event->key()` and never looked at `event->modifiers()`, so `Alt+↑` arrived as a bare `Up` and **moved the selection**. Not a no-op and not an error: a documented chord doing something plausible and wrong, which is worse than doing nothing, and a landmine for the stage that implements it — Stage 7 would have found a selection walking under its own reorder.

**Then it did not go away.** Testing the modifiers and falling through to the base class left the control still red, and the second consumer is the one that would not have been guessed: **`QAbstractScrollArea::keyPressEvent` scrolls on an arrow key and does not look at the modifiers either.** So the key was still consumed, by Qt, one layer below the fix. The repair is to `ignore()` a modified arrow and return without calling the base — *leave the key alone* means the widget lets go of it too, not only the binder.

**Why the suite did not catch it — and what did.** No positive case could: every case about the arrows asserts that they move the selection, which they did. It was found by the case written from **what must NOT happen** (convention 22, guide rule 5), which presses each of the nine Stage 7 rows and requires the key to come back unconsumed. Two of the nine were red on its first run. The same case is what refused to go green after the incomplete first fix, which is convention 7 earning its keep twice in one task: a control that fires for the right reason keeps firing until the reason is gone.

**What this does not cover** (convention 19). The control asserts that the key is *not consumed*, which is the claim §8 makes at Stage 4. It does not assert what `Alt+↑` eventually reaches — nothing, today — and it will need re-reading at Stage 7, when the chord acquires a command and the binder consumes it before the widget ever sees it.

**IT RECURRED AT T6, IN THE SECOND PANE, AND THE CONTROL CAUGHT IT AGAIN.** `ui/panes/BookmarksPane` is a `QTreeWidget`, and `QAbstractItemView::keyPressEvent` walks the selection on an arrow without looking at the modifiers exactly as `QAbstractScrollArea` scrolls on one — so `Alt+↓` (§8's *move down among siblings*, Stage 7's `bookmark.moveDown`) moved the selection. Fixed the same way, in `BookmarksPane::keyPressEvent`, and **kept under this number rather than given a new one**: it is one mechanism in two files, and splitting it would make the second look like a different defect. **The lesson generalizes, and is worth stating for the eight panes still to come** (Stages 5 to 13): *an item view or a scroll area will take a modified arrow that §8 has promised to something else, and no positive case can see it* — the bare arrows work in both panes, which is all a positive case checks. Every pane needs the control, and the fix is to `ignore()` the event rather than pass it to the base.

**I-05 · A destination's fit is read, carried, and not applied.** *(Raised 2026-09-21 at T6, while building the panes rather than by a failing case — the case that names it was written from the finding.)*

**Mechanism.** `core::Destination` carries everything ISO 32000-1 §12.3.2.2 defines — the fit kind, and for `/XYZ` a point and a zoom with each of the three separately flagged, because `/XYZ null null null` is legal and common and means *keep the current value*. T4 read all of it and `tst_outline` asserts it. What T6 does with it is take `pageIndex` and call `PageView::goToPage`. So a `/XYZ` destination pointing three-quarters of the way down page 12 puts page 12's TOP at the top of the viewport, and a `/FitR` rectangle is not framed at all.

For `/Fit`, for `/XYZ` with no coordinates, and for any destination at the top of its page — which between them are most destinations in most documents — the result is indistinguishable from correct. That is exactly what makes it worth a log entry rather than a comment: **it will look like it works.**

**Why no case caught it.** None could: nothing before T6 navigated by destination, and the cases T6 added were written from the plan's *"both panes navigate"*, which this satisfies. It was found by reading `Destination`'s own fields while wiring the pane and noticing that five of its eight were going nowhere.

**CLOSED 2026-09-23 at T12, and the repair went where this entry said it would.** `PageView::goToDestination` applies the fit: `/Fit` and `/FitB` become `FitMode::Page` and `FitMode::Visible` — one line each, because D4-14 had just made Fit Visible *the content bounding box*, which is what `/FitB` means — `/FitH` and `/FitV` become the width and height fits plus a one-axis scroll, and `/XYZ` applies its point and, where it carries one, its zoom. **A null stays *keep what you have*** in every case, including a zoom of 0, which ISO 32000-1 §12.3.2.2 defines that way and which a reader treating as a zoom would take to 8 %. The conversion is `PageLayout::toContent`, which already knew that PDF's y grows upwards and that a CropBox's origin need not be (0,0) — so no second reader was written, which is what this entry asked for.

**AND APPLYING IT EXPOSED A SECOND DEFECT THAT HAD NOTHING TO DO WITH DESTINATIONS.** The first version went to the page and then applied the fit, which is the obvious order and is wrong twice: `goToPage` computes its scroll from the layout as it is, and a fit applied afterwards re-lays the document out at a different zoom, so that offset points at a different page and `refresh()`'s dominant-page snap reports THAT page as current — a bookmark to page 2 landed on page **4**. Then, because a fit is re-satisfied for whichever page is current, the zoom was recomputed for a page nobody had asked for: a `/FitB` destination to page 1 came out at page 4's fallback zoom with the content box never read. **Fit Visible is now ANCHORED to the page it was set for** (`PageView::refresh`'s note argues it), and the loop is gone. The defect was latent for Fit Page and Fit Width the whole time and invisible, because on a uniform document every page gives those the same number — Fit Visible is the first fit in the product whose zoom differs page by page by definition.

**What was absent, and who owned it.** There is no conversion from a destination's PDF-space point to a scroll offset, and **this task did not invent one**. It needs the page's slot rectangle in content space and the resolution that produced it — the arithmetic **T12** builds for Fit Visible over the content box (D4-14) — so the repair belongs there, beside the other place in this stage that turns a page-space rectangle into a zoom and a scroll. A second conversion written here would be a second reader to keep in agreement, and a wrong one scrolls the reader silently to the wrong place. The bound is stated at both panes' headers and asserted as a case, so it is visible from the code as well as from this log.

**I-06 · A `Common`-scoped registration took a key that four unbuilt contexts and one widget needed.** *(Raised and closed 2026-09-21 at T7, by an EXISTING case in another suite rather than by anything T7 wrote.)*

**Mechanism.** T7's first version registered `edit.cancel` so that `Esc` could cancel a tool's drag, with an `enable` predicate that answered false when there was nothing to cancel — which looked like the careful thing to do (convention 16). It is not, because of how the five outcomes are defined: `ShortcutBinder::consumes` counts **both** `Dispatched` and `Unavailable` as consuming the key. A registered-but-disabled command therefore eats the keystroke just as thoroughly as a working one; the only outcome that leaves a key alone is `NotImplemented`, which means *nothing is registered at all*.

`edit.cancel` is §8's **Common** row — it applies in every context unless one overrides it. So one registration in the viewer took `Esc` everywhere: from `ui/shell/ToolFinder`'s result popup, and in due course from `find.close` (S5), `form.revertField` (S9) and `present.exit` (T10), none of which exist yet.

**How it surfaced.** `tst_toolfinder`'s *the result list can always be put away* went red — a Stage 1 case written for **I-44**, which is the defect where that popup could not be dismissed. The popup is a `Qt::Popup` holding an application-wide mouse grab, so an `Esc` that does not reach it is an application answering no click anywhere. The case had been green for a year and caught this on the first run after the registration.

**Why T7's own cases did not.** They could not: every one of them presses `Esc` with the viewer focused, where the handler would have run. The failure is in every OTHER context, and only a case belonging to one of those contexts could see it. That is convention 22 from an unusual angle — the control that mattered was not written for this mechanism at all, and would not have been thought of.

**The repair, and the rule it leaves.** `edit.cancel` stays unregistered; `Esc` is handled in `PageView::keyPressEvent`, which the key reaches precisely because nothing consumed it. A drag is not a command and abandoning one is not either — no more than a mouse-move is — so this is where it belongs anyway. **The rule for whoever registers `edit.cancel` one day:** a `Common` row needs an owner that knows every context that overrides it, and registering one to serve a single context is over-claiming even when the handler is careful.

**Why the suite did not catch it.** It structurally cannot. Every local preset and the Windows job use one Qt; the only build that uses the other is on Linux, on a runner, after the push. 965/965 on both Windows presets and four clean gates said nothing about it.

**What changed beyond the fix.** The lesson is not about one function, so it is written where it binds later work: **convention 24** (`Implementation-Plan.md` §1.5) — the three portable layers may use only Qt APIs the distro Qt has, and the way to settle a doubt is to read `\since` in the pinned Qt's header. The issue log keeps the incident; the convention keeps the rule.

---

**I-07 · Half of a `Features.md` line is built, and the other half has nowhere to keep its value.**
*(Raised 2026-09-21 at T8, while writing A-52 rather than by a failing case — the case exists
because the finding did, and asserts the absence instead of leaving it remembered.)*

**Mechanism.** `Features.md` §1.2 offers *"invert / substitute page colors for high-contrast
reading"*, and `core/cmd/ActionId.def` has carried **two** ids for it since Stage 1:
`view.invertColors` and `view.pageBackground`. T8 builds the first — it is D4-11's second half, a
per-pixel map over pixels the scheduler already returned, and it costs no settings key because it is
a per-VIEW reading convenience like the view rotation beside it. The second needs a **colour pair**
(a page colour and a text colour) that has to live somewhere, and **none of D4-5's declared keys
holds one**: this stage's eighteen are one `pageDisplay/`, eleven `units/`, five `fullScreen/` and
two `general/`. Minting a `pageDisplay/pageBackground` here would be a Stage 4 key no document
planned, decided by the task that happened to notice.

**So `view.pageBackground` stays declared and UNREGISTERED**, which is the same posture
`edit.cancel` is in for a different reason (I-06) and the posture 292 of the 366 declared ids are
in: it consumes nothing, it offers nothing, and there is a row to fill in the day the colours have a
home. It is **not** the shape Stage 2's **I-11** warns about — a declared-and-inert *control* that
reads back as set and does nothing — precisely because no control exists.

**Why the suite did not catch it.** Nothing could have: A-52's title names both halves and the guide
was written from a plan whose §6 line for T8 names neither, so the first moment the gap is visible is
the moment somebody implements the case. That is why A-52 is recorded **🟡 rather than ✅** and why
its assertion is `CHECK_FALSE(isRegistered(ViewPageBackground))` — a green tick over a half-built
feature is worth less than an honest amber one, and an assertion that the id is unregistered fails
the day somebody registers it without giving it a key.

**Owner.** Stage 8, whose colour and conversion work is where a page-colour pair belongs; or any
earlier stage that finds it needs one, which would then be a `SettingsKeys.def` row and two lines of
painting, since the paint path (`PageView::paintContent`, over the tiles) is already there.

**I-08 · A test helper had been compiling at a different language standard from the rest of the tree.**
*(Raised and closed 2026-09-23 at T12, by a compile failure in a target this task never touched.)*

**Mechanism.** `cmake/RawPageWarnings.cmake`'s `rawpage_common_flags(target)` is the one place
`CXX_STANDARD 20` is set. **Four targets did not call it**, and they did not call it for two different
reasons — which is what makes this a class rather than a slip:

- **`rawpage_fuzz_seeds`** (Stage 3 T13), the helper that writes the round-trip fuzzer's seeds, simply
  never called it.
- **The three `tests/fuzz` drivers** (`fuzz_cos_lexer` and `fuzz_cos_parser` from Stage 2 T5 and T6,
  `fuzz_cos_roundtrip` from Stage 3 T13) skipped `rawpage_strict()` **deliberately and correctly** —
  its `-Werror` turns a sanitizer's own warnings into build failures on some clang versions, and the
  function's comment said exactly that. What the comment did not say is that `rawpage_strict()` is
  `rawpage_common_flags()` plus `-Werror`: **skipping it to avoid `-Werror` skipped the LANGUAGE**, and
  the two arrive through one function.

So all four had been compiled at the compiler's default standard — `gnu++17` for GCC 13, clang's own
on the portable presets — while every other target in the tree was `-std=c++20`.

**They built anyway, for weeks, because nothing they included needed the difference.**
`rawpage_core`'s headers propagate as include directories and carry no standard with them, and the
particular headers these files pulled in used no C++20 feature that C++17 rejects outright.

**How it surfaced, in two places a day apart.** T12 added `#include "core/Geometry.h"` to
`core/backend.h`, because a `PdfRect` now crosses that boundary, and `core/Geometry.h`'s value types
declare `constexpr bool operator==(...) const = default`. The **Windows** build then failed on
`RoundtripSeeds.cpp` with eleven errors in `core/Geometry.h` and `core/Units.h` saying *"only
available with '-std=c++20'"*, in a file that had not changed — fixed with the call, and that is what
this entry recorded first. **The three drivers are invisible on Windows**: MinGW GCC has no libFuzzer,
so `tests/fuzz` is configured by `portable-fuzz` alone, and they surfaced only in **CI run
35799559455's `Portable libFuzzer (Linux, clang)` job**, with clang reporting *"defaulted comparison
operators are a C++20 extension"* and three hard errors in `core/Units.h`. Every other job in that run
was green.

**A compile error in someone else's file is the honest form of this defect**; the dishonest form is
the one that had been running, where a target silently compiles a different language from the product
it is testing.

**Why the first fix was not enough, and what closed the class.** Fixing the target that failed was
fixing the instance: three more were in the same state, in a file whose comment gave a good reason for
being there, and a fourth would arrive eventually. So **`cmake/RawPageLanguageStandard.cmake` walks the
real build graph at configure time** — every directory, every compiled target — and **fails configure**
naming the target and the call to add. It reads the graph rather than the CMakeLists text, for the
reason `layer-lint` reads the includes the compiler reads: a check on the build files' source would
have to model `add_subdirectory`, functions and conditionals to answer a question CMake answers
exactly. **Its negative control was run**: with the call removed from `rawpage_fuzz_seeds`, configure
fails with *"rawpage_fuzz_seeds (CXX_STANDARD unset -- the compiler's default)"*.

**What the assertion does not cover, stated so a reader subtracts it** (convention 19): only
`CXX_STANDARD`. A target could still be missing the warning flags, the charset flags or `-g`, all of
which are also `rawpage_common_flags`' and none of which it checks. The standard is singled out
because it silently changes the meaning of valid code — a missing `-Wshadow` loses a warning, a
missing `-std=c++20` loses a language. **And it only walks the graph the current preset configures**:
the three fuzz drivers exist only under `RAWPAGE_BUILD_FUZZERS`, so a Windows configure walks 32
targets and never sees them. They were verified on this machine by configuring the Windows preset with
that flag ON — **35 targets walked, all C++20** — and by compiling all four translation units, which
is the exact step CI failed on.

**Why no test case.** None could, and none should be written: this is a property of the build files
rather than of the product, and the thing that catches it is the assertion itself, which runs on every
configure of every preset. **The class, which outlives every instance: a target that does not call
`rawpage_common_flags` is not compiling this project's language, and it will build until the day it
does not.**

**I-09 · `Qt::UniqueConnection` with a lambda slot is a debug-only fatal and a release-only silence.**
*(Raised and closed 2026-09-23 at T13, by the `x64-debug` run after `x64-release` had reported green.)*

**Mechanism.** `app::SessionShell` connects each window's `QObject::destroyed` to a lambda that sweeps that
window's stash entry, and asked for `Qt::UniqueConnection` so a window closed twice would not connect twice.
**Qt implements uniqueness only for a pointer-to-member slot**: with a functor it is
`Q_ASSERT_X(false, "QObject::connect", "Unique connection requires the slot to be a pointer to a member
function of a QObject subclass")`. In `x64-release` that assert compiles away, the connection is **never made
at all**, and the sweep never runs — so a later `MainWindow` allocated at a dead one's address could inherit a
stale session entry. In `x64-debug` the same line is a `qFatal`, and the process fail-fasts with
`0xC0000602`.

**Why the release suite did not catch it.** It cannot. The release symptom is a connection that does not
exist, and its consequence needs an address to be reused by a window that is hidden at quit — a race nothing
in the suite arranges. The debug build turns the same line into an immediate crash in the two cases that close
a window, which is the whole argument for running both presets rather than the fast one: **`x64-release` said
1106/1106 and `x64-debug` fail-fasted twice on the same binary's source.**

**The fix** guards uniqueness with the container the connection is about — the entry is stashed only after
`closing_.contains(window)` has decided whether to connect — so there is nothing left for
`Qt::UniqueConnection` to do. **The class, for every later stage: a connection to a LAMBDA cannot be
`Qt::UniqueConnection`, and the thing that knows whether it has already been made is the state the lambda
mutates.**

**I-10 · Fit Page measures the SHEET while the product draws the CROP.** *(Raised 2026-09-24 by the
user's M-11 walk — the first manual case walked in this stage, and it found in one session what a
complete and green automated half had ratified.)*

**Mechanism.** `core/render/FitPolicy.cpp`'s `fitRect()` answers `boxes.media` for `FitMode::Page`
and `boxes.crop` for `Width`, `Height` and `None`. What the product LAYS OUT and DRAWS is the
CropBox: `core::Page::displaySize()` is `cropBox().size`, and `PagePresentation` gives every slot
`page->cropBox()`. On any document whose CropBox is inset from its MediaBox the two disagree, so
`Ctrl+0` computes a zoom that fits a rectangle the product never draws.

**Measured on the rig, not inferred.** Over `corpus/ordinary/cropbox-margins.pdf` in a
**1991 × 998** device-pixel viewport at ~96 DPI: `Ctrl+0` = **89 %** = 998 ÷ (841.89 × 1.3333), the
MediaBox. The page is then drawn 828 px tall in a 998 px viewport — **17 % of it empty**, with page
2's top edge visible below. A CropBox fit is **107–108 %**, and **the product produced that number
itself in the same window two keystrokes away**: page 2 is empty, so `Ctrl+3` falls back to the
CropBox (D4-14) and reads **108 %**. The number `Ctrl+0` should have given is one this build already
computes correctly.

**Why no case caught it — the suite ASSERTS it.** `tests/unit/tst_fitpolicy.cpp:58` is
`CHECK(fitRect(boxes, FitMode::Page) == boxes.media)`, and the case beside it asserts only that the
three fits give three DIFFERENT zooms, which a wrong-but-different Page fit satisfies. A-68 was
written from I-28's lesson — *a fit is a zoom* — and applies it to Fit **Visible**, whose zoom is
checked against an independently computed content box; **nothing checks Fit PAGE's zoom against what
is drawn**. The rule is even written one line below the defect, in that file's own comment: *"Width,
Height and None all measure what a reader actually sees drawn."* Page is the one that does not, and
no case asks why.

**It predates T12 and it predates this stage.** The comment T12 deleted from
`ui/viewer/PageLayoutEngine.cpp` read *"`Page` fits the SHEET and `Visible` fits the CropBox. They
agree on a document whose CropBox equals its MediaBox, which is most of them"* — so the behaviour is
Stage 2's, carried across into `FitPolicy` unchanged. It is invisible on every corpus file except the
one T1 built for this stage: **the same shape as I-28, one box over.** It is logged here because the
walk that found it is this stage's; whoever repairs it should read I-28 first.

**The decision, taken by the user on 2026-09-24: the CROPBOX is correct and the product is wrong.**
Fit Page must fit what a reader is shown — which is what `Features.md` §1.2's *"Fit Page"* means in
the reader whose chords §8 copies, what this fixture prints on its own page (*"Fit Page fills the
window with the CropBox"*), and what `corpus/PROVENANCE.md` means by calling the CropBox *"what a
reader shows"*.

**What is absent, and who owns it.** Nothing is fixed in the change that raises this. The repair
touches one `switch` arm in `core/render/FitPolicy.cpp` and the assertion that currently pins the
MediaBox — and it is not finished by those two: **a case has to assert Fit Page's ZOOM against the
drawn page**, the shape A-68 already has for Fit Visible, or the next reader inherits a suite that
would ratify the defect a second time. **CLOSED 2026-09-24, the same day, and the repair is three things rather than one.** (1)
`fitRect()` answers `boxes.crop` for `FitMode::Page`, which puts Page in the same arm as Width,
Height and None — **`boxes.media` is now measured by no fit at all**, and it stays in `PageBoxes`
because the struct is the page's three rectangles and because a print path (Stage 8) is the caller
that legitimately wants the sheet. (2) `tests/unit/tst_fitpolicy.cpp`'s assertion is inverted and
joined by an explicit `fitRect(boxes, Page) != boxes.media`, stated separately so that a sixth mode
given the sheet one day fails something. (3) **A-80** asserts Fit Page's ZOOM against the drawn page,
in both the unit and the render suites, because without it the next reader inherits a suite that
would ratify the defect a second time.

**The planted control was run and it is the interesting part.** With `FitMode::Page` returning
`boxes.media` again: A-80 reddens in both suites and the `fitRect` case reddens — **and both
*"three different zooms"* cases stay green.** The defect is invisible to every assertion that asks
only for difference, which is what let it live under a complete suite for two stages, and is why the
new case is an equality against a rectangle computed independently.

**What moved besides the code:** `Features.md` §1.2 now defines Fit Page as *the page as shown* — the
ambiguity there is what let two readings coexist — `Architecture-FileMap.md`'s `FitPolicy` row no
longer says *"the sheet for Fit Page"*, and one multiplier in the *three different zooms* case moved
from 1.5 to 1.3 because the ratio it bounds is now 697.89 / 521.89 = 1.337 rather than
841.89 / 521.89 = 1.613. That number is written down in the case rather than tuned until it passed.

**What it does NOT cover** (convention 19): that the product lays out the CropBox. A-80's unit half
links no widgets and its render half reads the boxes through the backend, so *the slot is built from
the crop box* is asserted where that code lives — `core::Page::displaySize()`'s own cases. If that
changed to the sheet, A-80 would still pass and the product would be wrong in the other direction.

**I-11 · A fit re-measured whichever page the zoom change had scrolled onto.** *(Raised 2026-09-26
by the user's M-01 walk, step 5: `Ctrl+2` from 400 % on `mixed-page-sizes.pdf`'s landscape page read
**250 %**, the page far wider than the window.)*

**Mechanism — two causes, each harmless alone.** (1) **A zoom change kept the device scroll offset.**
`ViewportController::setZoom` changes the zoom and leaves `scroll_` alone, and every command path —
the fit chords, the zoom box, Actual Size, `Ctrl+=`/`Ctrl+-` — went through it; only the wheel
(`zoomAbout`) kept a point under the cursor. Dropping from 400 % to about 125 % therefore left the
viewport about three times further down the document. (2) **A fit was re-satisfied on every
`refresh()` against the CURRENT page**, and every refresh ends by making the page that dominates the
viewport current. So: Fit Width measured the landscape page, the stale offset put the viewport on
the **A5** page, the snap made it current, and the next refresh — a tile arriving is enough — fitted
the A5 page's width. 841.89 / 419.53 = **2.007**, and 2 × the landscape fit is the 250 % the walk
read. **T12 had seen this exact loop** — for Fit Visible, where it walked a bookmark to page 2 onto
page 4 — and anchored Fit Visible alone, writing that Fit Page and Fit Width were safe because *"on a
uniform document every page gives the same zoom"*. `mixed-page-sizes.pdf` is in the corpus because
documents are not uniform.

**Reproduced before the fix, on the fake backend:** the landscape page's slot measured **1753.9 px
against 877 available** — M-01's 2× — and Fit Page landed on page 4.

**Why no case caught it.** `uitest::FakeBackend` built **every page A4**, so no widget case could open
a document of mixed sizes, and the sentence that excused the two fits was true of everything the
suite could see. `tst_viewercommands.cpp` asserts that `Ctrl+2` sets `FitMode::Width` — the MODE,
which I-28 already taught this project is not the zoom — and nothing asserted where the reader was
after a zoom command at all.

**The fix, 2026-09-26, in `ui/viewer/PageView`.** (1) `visibleFitPage_` became **`fitAnchorPage_`**
and holds for **every** fit: set where the mode is chosen, moved by a navigation (`goToPage`, a
destination), cleared by a zoom, and taken lazily in `refresh()` from the current page for a fit set
on the controller by a path that never called `setFitMode`. (2) `setFitMode`, `setZoom`, `zoomIn` and
`zoomOut` take the reader's **place** — the viewport centre as a fraction of the current page — before
the zoom moves and put it back after; Fit Page and Fit Height instead show the fitted page whole, its
top at the viewport's. **A-81** holds four cases and **two planted controls, one per cause**, both run
red; its third case's first draft passed with the defect planted, because the unanchored fit
**oscillates** rather than settling, and it now asserts the zoom at every refresh.

**A consequence to know rather than discover.** Fit Width on a mixed document now keeps the zoom it
was given while the reader scrolls past pages of other widths — the landscape page's zoom, over an A5
page, is a page half the window's width. That is the anchor's whole point (the alternative is the
loop), it is what Fit Visible has done since T12, and a navigation re-fits. If a reader wants each
page fitted as they scroll, that is a different mode and a `Features.md` question, not this fix.

**What it does NOT cover** (convention 19): `ViewportController::setZoom` itself still keeps the raw
offset — the place is kept one layer up, in `PageView`, which is where every command reaches it; a
caller that sets a zoom on the controller directly (`MarqueeZoomTool` does, and positions the scroll
itself) is not covered by it.

**I-12 · A split had no visible divider.** *(Raised 2026-09-26 by the user's M-01 walk, step 3: "I
didn't see the boundary". Offered by the user as a feature; logged as a defect, because M-01's own
*Expect* — "the split's divider lands where you dropped it" — cannot be checked on a divider nobody
can see.)*

**Mechanism.** `SplitViewContainer` built plain `QSplitter`s and nothing styled their handles. The
native style paints a handle in `QPalette::Window`; `PageView::paintEvent` paints the desk around the
pages in `QPalette::Window` too, and a `PageView` has no frame. The boundary was the same colour on
both sides **by construction** — not faint, absent.

**Why no case caught it.** A-53 … A-56 assert what a split IS — one handle, one journal, the scroll
lock, the tickets — and none asked what it LOOKS like; the suite's header named *"it cannot judge a
repaint"* as a blind spot and left it to M-09, which had not been walked.

**The fix, 2026-09-26, in `ui/viewer/SplitViewContainer.cpp`.** Every splitter is a `DividerSplitter`
whose `createHandle()` returns a `DividerHandle` that **paints itself** from the palette
`ThemeManager` writes, rather than asking the style for a role the style may ignore (convention 21):
a 6-logical-pixel band in `QPalette::Mid` (the `Border` token) with a one-pixel line down its middle
in `QPalette::PlaceholderText` (`SubduedText`), and the whole handle in `QPalette::Link` (`Accent`)
under the pointer and while dragged. **The band alone was tried first** and measured ΔE 12.4 against
the light desk — over the floor, and still pale — which is why the line is there. No new token: the
three are existing roles, so both themes were already defined (convention 20). **A-82** measures the
painted pixels in both themes, and its planted control — the stock handle — reads **ΔE 0**.

## 9. Sign-off criteria

Stage 4 is done when **all** of these hold — [`Stage4-ViewingSurface-Plan.md` §13](./Stage4-ViewingSurface-Plan.md#13-definition-of-done-for-stage-4)
restated as a checklist.

1. ✅ **2026-09-26 — all 83 carry a result.** The 81st is **A-80**, added 2026-09-24 with I-10's fix, and the 82nd and 83rd are **A-81** and **A-82**, added 2026-09-26 with the fixes for I-11 and I-12; the last of the original eighty to get one was A-04. Every case
   in this file was run and its actual output recorded against it, not summarized (Stage 1's I-32).
   **A-04 is a `portable-asan` CI job and this machine has no ASan runtime**, so it was read from that
   job's **log** rather than its tick (rule 3): CI run 35879213301, the T14 push, **609/609** with all
   39 Stage 4 `core/` cases present by name and no sanitizer line.
2. ☐ M-01 … M-12 **walked and signed off by you**. I do not mark a manual case. A case not run for
   want of hardware (M-12's touch half) is recorded as such, never as passed. **Two of the twelve are
   walked and signed off: M-11, 2026-09-24, passed — and it raised I-10 (S2); M-01, 2026-09-26,
   failed its first walk — raising I-11 (S2) and I-12 (S3), both fixed the same day — and PASSED its
   re-walk on the fixed build, the same day.**
   The other ten are the stage's largest remaining gap and the honest summary of its state.
3. ✅ **2026-09-23, T14.** `checks` clean — four gates — and **`mutation-lint` reports one command**
   (A-06), with `Commands.def` and `core/cmd/commands/` **byte-unchanged across the whole stage**
   (A-07), which is the stronger claim.
4. ✅ **2026-09-26, re-run with the fixes for I-11 and I-12: 1114/1114 on `x64-release` and on
   `x64-debug`** — the six new cases are A-81's four and A-82's two — with `checks` clean on four
   gates (`layer-lint` 423 files / 3,362 includes; `mutation-lint` still **one command**) and no
   `warning:` line from either incremental build (which compiled only the changed files, so the
   clean-build census stays T14's). **The first run had the 2026-09-24 caveat again** — the walker's
   `rawpage.exe` was still open, so the release `rawpage.exe` did not relink and
   `ui.diagnostics_ignore_the_bus` SKIPPED in `x64-debug` — and **it was cleared the same day**: with
   the window closed the binary relinked and both presets ran **1114/1114** again, the battery case
   the one named skip. *Before that —* **2026-09-24, re-run with I-10's fix.** **1108/1108 on `x64-release` and on `x64-debug`** —
   the two new cases are A-80's halves — `checks` clean on four gates (`layer-lint` 423 files /
   3,354 includes; `mutation-lint` still **one command**). **The first run of this item had two caveats and both are cleared.** The
   product was still OPEN on the walker's desktop from M-11, which holds the binary and the
   single-instance bus: `rawpage.exe` would not relink, and `ui.diagnostics_ignore_the_bus` SKIPPED
   in `x64-debug` — a second named skip that is not normally there. **Both were re-run with the
   window closed**: the binary relinked and that case **passed on both presets**, leaving the battery
   case as the one named skip it should be. Recorded rather than quietly re-run, because a skip
   nobody explains is how a case stops being evidence. The clean-build warning census is T14's and was not re-taken
   (convention: an incremental build inherits it). One case renamed —
   `fitRect measures three different rectangles` →
   `fitRect measures the crop box and the content box and never the sheet`, a Stage 4 case whose old
   name asserted the defect; A-74 is about PRIOR stages' names and is unaffected.
5. ✅ *(2026-09-24: `portable-asan` in CI run 35879213301 — the T14 push, commit `a01192b` —
   **609/609**, with all 39 cases of `tst_sessionstate`, `tst_fingerprint`, `tst_snap`, `tst_outline`
   and `tst_metadata` present by name and **0 `runtime error:`, 0 `ERROR: AddressSanitizer`, 0
   `LeakSanitizer` lines**; `portable-fuzz` in the same run, **three** targets at 61 s each with no
   crash; the nightly soak, `fuzz-soak.yml` run 35837723167, the same three at 601–602 s with no
   crash, on `3f9cb01` with no `pdf/` or `tests/fuzz` change between it and `a01192b` — A-04)*
   `portable-asan` clean including `SessionState`, the snap resolver and the three readers;
   `portable-fuzz` and the soak unchanged and clean on three targets. **Linux CI's to answer**, and it
   was the only automated item on this list this machine could not settle.
6. ✅ **2026-09-23, T14.** The only changes under `golden/` since Stage 3's sign-off are the **nine
   NEW baselines** for T1's three new corpus files and their manifest rows; **every pre-existing
   baseline is byte-unchanged**, read from `git diff` as additions rather than modifications. Stage 4
   rasterizes nothing new, so a MOVED baseline would have been a finding.
7. ✅ **2026-09-23, T14 — and the verdict MOVED.** Both figures are in `Tech-Stack-Plan.md` §3.3 and
   are kept apart: T2's bench (10.59–10.74 ms, `go, with named custom work`) and T14's product
   (**7.03–8.56 ms** under D4-16's workload, **`go`**), the plain verdict unlocked by the two-window
   column falling from 18.91–19.33 ms to **4.01–4.75 ms** once T11's named work was measured rather
   than merely asserted. `WinAcceleratedSurface`'s FileMap row says what was measured. Graded on §3's
   scale, which was fixed before either measurement.
8. ✅ **2026-09-24 — the automated half at T12, and M-11 walked.** Stage 2's I-28 is
   closed in Stage 2's guide, with the measurement, and `Ctrl+3` is asserted **on a zoom** and not on
   `fitMode()` (A-68 ✅ — its own control had to be fixed before it would fail on the defect). **M-11
   passed on the rig**: 89 / 100 / 331 / **143 %** over `cropbox-margins.pdf` with Fit Visible filling
   the window with the ink, 89 / 100 / 251 / **106 %** over `text-a4.pdf`, and the empty page's
   CropBox fallback at **108 %**. Somebody has now looked at a fitted page — and looking is what
   raised **I-10**, which is item 9's problem rather than this one's.
9. ✅ **2026-09-26 — met again, the same day it was broken, for the second time.** M-01's walk
   raised **I-11 (S2)** and **I-12 (S3)** and both closed that day, with **A-81** and **A-82**.
   *Before that, 2026-09-24:* §8 held ten issues and **only I-07
   is open**, at S4: a page colour can be inverted but not substituted, because no declared settings
   key holds a colour pair — a scope finding owned by Stage 8, nothing broken and nothing inert.
   **I-10 (S2) was raised by M-11's walk and closed the same day** — `FitMode::Page` measures the
   CropBox, the assertion that pinned the sheet is inverted, and **A-80** asserts the zoom against
   the drawn page. This item spent one commit red, which is the honest record of a green suite having
   ratified a defect a person could see.
10. ✅ **2026-09-23, T14.** Every §4 row flipped with its file; the plan's §10 debt paid, including
    the two items that had outlived their tasks — **flow C** gained the split and the
    background-window lane (owed at T9 and T11), and **`CLAUDE.md`'s two paragraphs** were
    re-derived rather than edited. `Implementation-Plan.md`'s Stage 4 block is task by task with
    every deviation named.

**WHERE THAT LEAVES THE STAGE: the automated half is COMPLETE, the Linux job included; TWO OF THE
TWELVE MANUAL CASES ARE WALKED AND SIGNED OFF — M-11, AND M-01 ON ITS RE-WALK — AND EVERY ISSUE
EITHER RAISED IS FIXED.** **Item 2 is the whole of what is outstanding** — ten unwalked cases. That
each of the first two manual cases walked in this stage found defects the complete automated half
had ratified, and that each fix needed a new case rather than only a new line of code, is the
argument for the other ten rather than against them. That is a legitimate state to report and it is not *done* — `CLAUDE.md`'s workflow
says so in as many words, and Stage 4's manual half is the largest of any stage because its subject
is what a person sees: two monitors at different scaling, a display unplugged mid-presentation, a
900-page pane judged for smoothness, a guide dragged off a ruler by hand.
