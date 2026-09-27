# Stage 4 — Viewing and Navigation Surface · Testing Guide

> **Authoritative verification document for Stage 4.** Written from
> [`Stage4-ViewingSurface-Plan.md`](./Stage4-ViewingSurface-Plan.md) and
> [`Implementation-Plan.md` § Stage 4](./Implementation-Plan.md#stage-4--viewing-and-navigation-surface),
> **before any Stage 4 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** 🟡 **Written 2026-09-18, the same day as the plan and the day Stage 3 was signed off;
> partly run.** **95 automated** (A-01 … A-94, plus **A-66(a)**; A-66(a) came with T11's A2 named
> work — a lane no case in this file claimed) and **12 manual** (M-01 … M-12); **all 95 carry
> a result** as of 2026-09-27 (re-derived from this file: 95 case headers, none still boxed — the
> 95th is **A-94**, added 2026-09-27 with I-23's fix, which like A-85 SKIPS under `ctest` and is run
> on the `windows` platform — the
> 81st is **A-80**, added 2026-09-24 with I-10's fix, and the 82nd to 87th are **A-81**, **A-82**,
> **A-83**, **A-85** and **A-86**, added 2026-09-26 with the fixes for I-11 to I-15, and **A-84**,
> the case D4-18 was recorded with that day; the 88th and 89th are **A-87** and **A-88**, added
> the same day with I-16's fix and I-17's, both found while preparing M-07's walk, and the 90th is
> **A-89**, the dotted preview of a guide being dragged — a request of the user's that day, not a
> defect; the 91st and 92nd are **A-90** and **A-91**, added 2026-09-27 with I-18's and I-19's fixes — A-91 rewritten the same day with I-20's — the 93rd is **A-92**, I-21's, and the 94th **A-93**, I-22's guard; **A-85 SKIPS under `ctest`** — it needs two monitors at
> different scale factors, which the offscreen platform cannot have — and its result is a run on
> the `windows` platform, recorded against it) — T1's
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
> **Ten manual cases are walked and signed off.** **M-11**, by the user on 2026-09-24, passed and raised **I-10**.
> **M-07**, on 2026-09-26, passed with no finding — after **I-16** and **I-17** had been found and
> fixed while preparing it, so the build walked was the first on which it could pass.
> **M-02**, on 2026-09-26, passed — walked against the PRESENTATION, which is what the monitor
> choice governs (§6 says why) — and raised **I-14 (S2)**: `Ctrl+Shift+M` kept the presentation's
> physical size across a change of scale; fixed the same day. **M-06**, on 2026-09-26, passed with no
> finding — the human half of A2 at Stage 4, and it agrees with the figure. **M-03**, on 2026-09-26,
> passed with no finding — including the display held awake while presenting and released after.
> **M-04**, on 2026-09-26, passed with no finding, in the direction that removes the 150 % primary.
> **M-05**, on 2026-09-26, **failed at step 5** — turning session restore off did not stick — which
> raised **I-15 (S2)**: no `general/` preference had ever been read back from the file after a real
> restart. Fixed the same day, and **step 5's re-walk on the fixed build passed**.
> **M-01**, by the user on 2026-09-26, **failed on its first walk** — it raised **I-11 (S2)**, Fit
> Width drawing the landscape page at twice the window's width, and **I-12 (S3)**, a split with no
> visible divider — both were fixed the same day, and **the re-walk on the fixed build PASSED, the
> same day**. **M-08**, by the user on 2026-09-27, **failed its first walk at step 1** — it raised
> **I-18 (S2)**, a zoom reaching only the body of a spreadsheet split, and **I-19 (S2)**, a frozen pane
> that scrolled on its own — both fixed the same day, with every other step passing; **step 1's re-walk
> the same day confirmed both fixed and raised I-20 (S2)** — a wheel up over a header row frozen at
> the page's top did nothing, and a hand drag over a frozen pane ran away, both from I-19's first fix
> — **and I-21 (S3)**, `Ctrl` with the keypad's `+` zooming nothing; both fixed that day, and **the second re-walk of
> step 1 PASSED, the same day — M-08 is signed off**. **M-09**, by the user on 2026-09-27, **failed
> its first walk at the cascade** — the first window's title bar was above the top of the screen —
> which raised **I-23 (S2)**; measured, the tilings had the same defect, and it was fixed the same
> day; **the re-walk the same day confirmed the cascade fixed and found nothing wrong with the tilings on
> either monitor — M-09 is signed off**. The other two — M-10 and M-12 — are unwalked.
> §8 holds **twenty-three** issues and **only I-07 (S4) is open**;
> I-02, I-03, I-04, I-05, I-06, I-08, **I-01 and I-09 at T13**, **I-10 on 2026-09-24** and **I-11 to
> I-17 on 2026-09-26**, and **I-18 to I-23 on 2026-09-27** are closed — **I-01 where this file said its owner would settle it, I-09 found
> by `x64-debug` after `x64-release` had reported green, and I-10 to I-15 each found by a person
> looking at the product over a green automated half** — I-13, the vertical ruler with no numbers, by
> the user asking about it after M-01 rather than inside a case. **I-16** — `Ctrl+;` snapped no guide,
> because no gesture called the resolver — was found by reading the code to write up how to walk
> M-07, before anyone walked it; and **I-17** — the cursor readout `Features.md` §1.3 promised was
> never built — by the user preparing the same walk, and closed on their decision: a red marker with
> the reading beside it, on the rulers.
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
| All four layout modes plus split views work | A-53 … A-56, **A-90** and **A-91** for the splits; the four layout modes are Stage 2's suites, re-run by name in A-74; **M-01**, **M-08** |
| Rulers, grid, guides and snapping behave identically at 50 % and 400 % | A-45 … A-52, **A-87**, **M-07** |
| Full Screen and presentation work on a chosen monitor | A-57 … A-63, **A-85** (on the `windows` platform), **M-02**, **M-03** |
| Window arrangement and session restore survive a monitor topology change | A-18 … A-24 (the fingerprint and the clamp), A-64 … A-66 and **A-94** (arrangement), A-75 … A-79 (restore), A-60 (a presentation's monitor disappearing), **M-04**, **M-05**, **M-09** |
| *(Plan D4-1)* The viewing surface changes no document | A-06, A-07, A-31, A-35, A-48, A-63, A-69 |
| *(Plan D4-10)* The pane does not starve the viewport | A-14 … A-17, A-32, A-72, **M-06** |
| *(Plan D4-14)* Fit Visible fits the content — Stage 2's **I-28** | A-11, A-67, A-68, **M-11** |
| *(I-10)* Fit Page fits the page a reader is SHOWN, not the sheet | **A-80**, and **M-11**, which found it |
| *(I-11)* A fit measures the page it was CHOSEN on, and a zoom command keeps the reader's place | **A-81**, and **M-01**, which found it |
| *(I-12)* A split's divider is visible against the desk, in both themes | **A-82**, and **M-01**, which found it |
| *(I-13)* Both rulers print their numbers | **A-83**, and **M-07** |
| *(Plan D4-18)* One pair of rulers, following the focused pane of a split | **A-84**, **M-08** |
| *(I-14)* A presentation moved to a monitor of another scale fills that monitor | **A-85**, and **M-02**, which found it |
| *(I-15)* A `general/` preference survives a real restart | **A-86**, and **M-05**, which found it |
| *(I-16)* A guide SNAPS when a person drags it — off a ruler or on the page | **A-87**, and **M-07** |
| *(I-17)* The cursor's page coordinates are read out live, beside a red marker on each ruler | **A-88**, and **M-07** |
| *(User request, 2026-09-26)* A guide being dragged off a ruler previews dotted, where it will land | **A-89**, and **M-07** |
| *(I-18)* A zoom, a fit or a view rotation in a spreadsheet split reaches all four panes, and the freeze keeps its place in the document | **A-90**, and **M-08**, which found it |
| *(I-19)* A frozen pane of a spreadsheet split moves only through the body | **A-91**, and **M-08**, which found it |
| *(I-20)* A frozen pane's wheel and hand move the body one-to-one, in every direction, from a freeze at the document's corner | **A-91** (rewritten), and **M-08**'s re-walk, which found it |
| *(I-21)* `Ctrl` with the keypad's `+` and `-` zooms like the main row's | **A-92**, and **M-08**'s re-walk, where it was found |
| *(I-22)* No tracked text file holds a control byte — a path's backslashes read as escapes | **A-93** |
| *(I-23)* Tile and cascade leave every window's title bar on its monitor and uncovered | **A-94** (on the `windows` platform), and **M-09**, which found it |
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

**A-83 · Both rulers print their numbers — I-13, closed** ✅
✅ **2026-09-26** (`tests/ui/tst_rulers.cpp`, `[I-13]`). A-49 asserts what the rulers READ and hands
*"whether the numbers read"* to M-07; nothing looked at whether they were DRAWN, and the vertical
ruler drew none. This case paints a horizontal and a vertical ruler with one mapping and a black-on-white
palette and counts dark pixels in the **outer third** of each — the strip no tick reaches, so a tick
cannot pass for a number. **The horizontal ruler is the control**: it always had its numbers, so a
threshold it fails is a wrong threshold. **Measured:** horizontal 211, vertical **171**; with the old
origin planted back, vertical **0**. **A trap found on the way, and the reason the case loads a font
file:** the offscreen platform has no font database and draws each glyph as a box — and draws a
ROTATED box as nothing — so the first version of this case read vertical 0 *after* the fix as well as
before it. Run on the `windows` platform it read 123 fixed and 0 planted; loading
`C:/Windows/Fonts/arial.ttf` into the case (`QFontDatabase::addApplicationFont`) lets the offscreen run
see the same difference, and the planted control was re-run there and read 0.
**What it does NOT cover** (convention 19): that the numbers are the right ones (A-49), and whether they
read at `general/uiFontScale` 0.75 or on a 150 % panel (M-07).
**Steps** `ctest -R "print their numbers"`.
**Expect** Ink in the label strip of both rulers. **If it fails** S3 — I-13 is back.

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
the other's is **M-08**'s, and so is whether the panes LOOK locked together while a wheel turns.
*(This line said M-09, which is window arrangement and asks neither — corrected 2026-09-26, when
M-08 gained both checks.)* **And two things it never tried, both found by M-08's first walk on
2026-09-27:** it takes the freeze at ONE zoom and never changes it, and it only ever scrolls the
BODY. A zoom reached the body alone (**I-18**), and a wheel over a frozen pane moved that pane alone
(**I-19**) — the derivation was proved to fire when the body moves, and nothing proved nothing else
moves the frozen panes (convention 22). Those halves are **A-90** and **A-91**.
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
at a glance on a 150 % panel is **M-01**'s and **M-08**'s.
**Steps** `ctest -R "divider"`.
**Expect** Band ΔE ≥ 10 and line ΔE ≥ 30 against the desk in both themes, on every splitter of every
split mode; the pointer lights the handle. **If it fails** S3 — I-12 is back.

**A-84 · The rulers measure the FOCUSED pane of a split — D4-18** ✅
✅ **2026-09-26** (`tests/ui/tst_split.cpp`, `[D4-18]`). **Written with the decision, not with the
code**: T9 built one pair of rulers that rebinds on `activeViewChanged` and no case asserted it; the
user asked during M-01 whether it was intended, chose to keep it, and it was recorded as **D4-18**
(`Stage4-ViewingSurface-Plan.md` §3) with this case. Vertical split, rulers on; the horizontal ruler's
zero — the focused pane's page corner, in the ruler's own coordinates — is read with the left pane
focused, then the right, then the left again. **The claim is a number**: same document, zoom and
scroll, so the two zeros are exactly one pane-offset apart (448 px here, within 1), and focusing back
returns the first. **Planted control:** with `DocumentWindow`'s `activeViewChanged` handler emptied,
the container still reports the right pane active and the ruler's zero moves **0 px** against 448 —
the case fails on the ruler, not on the focus.
**What it does NOT cover** (convention 19): whether a person can tell which pane the ruler is
reading when the pointer is over the other one — **M-08**, step 3.
**Steps** `ctest -R "measure the focused pane"`.
**Expect** The ruler's zero moves with focus by the distance between the panes, and back. **If it
fails** S3 — D4-18 is not what the product does.

**A-90 · A zoom in a spreadsheet split zooms all four panes, and the freeze keeps its place — I-18, closed** ✅
✅ **2026-09-27** (`tests/ui/tst_split.cpp`, `[I-18]`). The freeze is taken at 200 % with the body at
(100, 500) — ON page 1, and required to be: the corner's top-left is read as a **page and a PDF point**
before anything moves, and the first run of this case read a nullopt at 100 %, where x = 100 is in the
gutter beside a centred page. Then four changes of geometry, each through the path a person takes:
**`Ctrl+=`** dispatched to the focused body — the walk's gesture; **`Ctrl`+wheel over the frozen ROW**,
which reaches the pane under the pointer rather than the focused one; **Fit Width** on the body; and
**Rotate View**. After each: all four panes share the body's zoom and view rotation, the three frozen
panes carry **no fit of their own**, A-55's four assignments hold, and — after the first three — the
corner's top-left is over **the same page and the same PDF point within 0.5 pt**. After the rotation
only the geometry and the derivation are asserted, because the point under the corner moves on screen
when the page turns. **Planted control:** `followBody` with its geometry copy removed fails **15
assertions** — the zoom and rotation on every frozen pane, the derived offsets, and the place.
**What it does NOT cover** (convention 19): the `Ctrl`+wheel is an event sent to the viewport by hand;
and whether the four panes LOOK aligned at the new zoom is M-08's step 1.
**Steps** `ctest -R "zooms all four panes"`.
**Expect** One zoom across the four panes after every geometry change, from the body or from a frozen
pane; the corner over the same place in the document. **If it fails** S2 — I-18 is back.

**A-91 · A frozen pane is moved only through the body, and the freeze holds — I-19 and I-20, closed** ✅
✅ **2026-09-27, REWRITTEN the same day** (`tests/ui/tst_split.cpp`, `[I-19][I-20]`). **Its first version
passed over I-20** — it froze mid-page, where a frozen pane has room to move both ways, and it drove
the hand as a relative `scrollBy`, which is not what the hand does. So it is now written against the
freeze M-08's re-walk took: **at 400 % and at the top-left corner, (0, 0)**, where a frozen pane cannot
itself scroll up or left. **The panes:** each frozen pane has **no focus policy, no visible scrollbar
and `isFrozen()`**, and the room the body's scrollbars take is kept empty beside them — the frozen
row's viewport is **exactly the body's width** and the frozen column's **exactly its height**. **The
wheel**, over each frozen pane in turn: **down** moves the BODY down; with the body moved to (600,
1500), **up** moves it up — the half I-20 is; **sideways both ways** over the row and over the column,
which is frozen at x = 0; and **`Ctrl`+wheel over the corner** zooms the body. **The hand, as a real
drag** — press, **five** moves of 10 px, release, with `Shift` held for the temporary hand, on the
frozen column's viewport: the body moves **50 px up times the pixel ratio, within 1** — the first fix
moved it 10 + 20 + 30 + 40 + 50 = 150. **Anything else** that moves a frozen pane — its controller set
directly — is undone, and the body does not follow it. **At the body's end** a wheel over the row
moves nothing. After every one, A-55's four assignments hold at (0, 0). **And a guide added inside
the frozen row reaches the body and the column** — a frozen pane is never the active one, so the
mirror that follows the active pane (D4-7) would not have heard it.
**Planted controls, all run red:** the frozen pane's wheel not re-sent to the body fails **11**
assertions — every wheel, down, up, both sideways and the zoom — and nothing else; the frozen pane's
tools given its OWN controller fails the hand, **1500 against 1450**, and nothing else; and, in the
first version, the frozen pane's guide connection removed failed the two guide assertions and nothing
else. **The old design itself was not re-planted** — it is gone from the tree, not switched off — so
what shows this case would catch it is that its two new observations, a wheel up from 0 and the
hand's distance, are the two the walk and the reading of `HandTool` found.
**What it does NOT cover** (convention 19): the wheel and the mouse are events sent to the viewport by
hand, not a real device; and whether a wheel over the header row FEELS like scrolling the sheet is
M-08's step 1.
**Steps** `ctest -R "moved only through the body"`.
**Expect** A frozen pane's wheel and hand move the BODY, one-to-one, in every direction, from a freeze
at the document's corner; nothing else moves a frozen pane; the frozen row and column are the body's
size to the pixel. **If it fails** S2 — I-19 or I-20 is back.

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

**A-85 · A presentation moved to a monitor of another scale FILLS that monitor — I-14, closed** ✅
✅ **2026-09-26, on the `windows` platform — and it SKIPS under `ctest`, by design**
(`tests/ui/tst_presentation.cpp`, `[I-14]`). **The only case in this file whose evidence is not a
`ctest` run**: it needs two monitors at DIFFERENT scale factors, and the offscreen platform has one
screen, so under `ctest` it calls `SKIP` with the reason — a second named skip beside the battery case.
Its result is `QT_QPA_PLATFORM=windows` on the P-1 rig (MAG 272URDF 3840×2160 at 150 %, LS27B61x
2560×1440 at 100 %), release and debug. **Judged in PHYSICAL pixels**: `GetWindowRect` on the
surface against `GetMonitorInfo`'s rectangle for the monitor it is on, after the first placement
(the control — it was always right) and after each `present.nextMonitor` round the whole ring; Qt's
own logical geometry is not asserted, because the defect was the window and Qt's account of it
disagreeing. **Measured before the fix:** after a move from the 4K panel, `surface 3840x2160 |
monitor 2560x1440` — M-02's report exactly. **After:** 2560×1440 on the 2K, 3840×2160 on the 4K,
and back, every move. **The red run before the fix is the control**: the case was written against
the unfixed code and failed on the move for the reason the walk gave.
**What it does NOT cover** (convention 19): the topology path (A-60), which now moves the surface
by the same function but needs a monitor removed to be driven for real — **M-04**, which walked it on
2026-09-26 and saw the moved presentation fill the remaining 2560×1440 screen; and every `ctest` run,
where it skips.
**Steps** `QT_QPA_PLATFORM=windows` then `rawpage_ui_tests.exe "[I-14]"`, on a desktop with two
monitors at different scale factors.
**Expect** The surface's physical rectangle equals its monitor's after the first placement and after
every move. **If it fails** S2 — I-14 is back.

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
**Corrected 2026-09-27 (I-23):** this case's ✅ covered the cells and never where a window's frame lands in one — *"keeps every title bar on-screen"* was not observable here, and on a real desktop it did not hold. **A-94** asserts it.
**Expect** Tiling N windows fills the work area with no overlap and no window smaller than a stated minimum; cascade steps by the title-bar height and keeps every title bar on-screen. **The control:** arranging with one window changes nothing. **If it fails** S2.

**A-65 · Merge windows re-docks every detached tab, in order** ✅
✅ **2026-09-21, T11.** `ui.` — five documents in one window, two torn off with `detachDocument` into windows of their own, then `WindowArranger::mergeWindows()`: one window, five tabs, in their original order, each tab the SAME `DocumentWindow` object with the same `DocumentHandle`, the same `CommandJournal` and the scroll position it had, which is what distinguishes a MOVE from a close and a re-open. **The order is only a meaningful claim because the target is fixed**: the first window in `shellWindows()` rather than the active one, since merging into whichever window a person stood in would make the answer depend on where they clicked. **What no merge can restore**, said at the case: a detached window records nothing about which tab its document used to be, so the result is window creation order with each window's tabs in tab order — tearing `e` out before `d` puts them back as `e`, `d`, and nothing in the product claims otherwise.
**Expect** Three windows with five documents become one window with five tabs in their original order, each keeping its handle, journal and scroll position. **If it fails** S2.

**A-66 · The window list and `window.new`** ✅
✅ **2026-09-21, T11 — five cases.** The LIST names every window in creation order by its own title, `activateWindow` moves `activeShell()` (which is what every Global command resolves through, so that is the claim rather than anything about a menu), and an out-of-range index is a no-op, because the list a menu was built from can be stale by the time it is clicked. **`window.new` is a second view**: same path, **different `DocumentHandle`** — two windows are two `DocumentWindow`s and each runs the three-step close protocol, so a shared handle would be closed out from under the other, which is the exact opposite of why a split shares one (D4-13) — and **no second journal**, so the companion is not editable and never dirty. A rotation run in the leader's window moves both to the same revision and the second view's laid-out page has its width and height SWAPPED (geometry before pixels, Stage 1's I-36): it lays out from the leader's model tree while rasterizing through its own handle, and a companion reading its own engine tree would show the page upright with nothing on screen to say so. Closing the leader **detaches** the companion rather than taking it down, and it goes on drawing the file through the handle it always had. The two scroll together by default and `window.syncScroll` unlinks them, mirrored in PDF points and both ways, behind an echo guard — without which two windows each following the other is a loop with no fixed point.
**Expect** The list names every window and switching activates it; `window.new` opens a **second view of the same document** in a new window with its own handle (D4-13's exception), and both views stay correct after a rotation in one. **If it fails** S2.

**A-94 · An arranged window's TITLE BAR is on its monitor, and no tile covers another — I-23, closed** ✅
✅ **2026-09-27, on `QT_QPA_PLATFORM=windows`, 86 assertions; under `ctest` it SKIPS by design**
(`ui.an arranged window's title bar is on its monitor and no tile covers another`, tag `[I-23]`) —
the offscreen platform draws no frame, so there `frameGeometry() == geometry()` and the case would
pass against the code that shipped I-23, which is how A-64 stayed green. Three documents, two torn
off, all three moved onto the primary monitor (150 % on the rig), and **each arrangement started with
the first window MAXIMIZED**, because a maximized window's frame is not the one it will have. After a
cascade, a horizontal tiling and a vertical tiling, it reads each window twice: **Qt's frame**, which
must lie inside `availableGeometry()`, and **what Windows draws** — DWM's extended frame bounds, in
physical pixels — whose TOP must be on or below the monitor's work area, which is the claim I-23
broke. Tiled frames may not intersect; each cascaded window must start at least one of the previous
window's OWN title bars below it. **Two planted controls, both run red for the right reason:** the old
`setGeometry(cell)` — cascade window 0 visible from **y = −45**, the top strip and all three columns
likewise, and Qt's frame at y = −30 outside the work area — and the style's cascade step — **21 against
a 30-pixel title bar** on windows 1 and 2.
**What it does NOT cover** (convention 19): the border Windows 11 draws at the left, right and bottom
that Qt counts as client — it is BOUNDED, at one logical pixel plus one physical pixel of rounding (a
column starting at logical 1707, physical 2560.5, measured 3 pixels past the work area where the border
alone is 2), not removed; two monitors, which is M-09's; and a title bar's text, which no rectangle
judges.
**Steps** `$env:QT_QPA_PLATFORM='windows'; .\build\x64-release\bin\rawpage_ui_tests.exe "[I-23]"` —
three windows appear and move on the primary monitor for a few seconds.
**Expect** `All tests passed`. **If it fails** S2 — a title bar is off the screen or under another window.

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
generated corpus, and nothing here draws to a 60 Hz display — **M-06** is the only thing that can say
whether a 900-page pane *feels* smooth, and on 2026-09-26 it said so (§6).
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

**A-86 · A `general/` preference survives a real restart — I-15, closed** ✅
✅ **2026-09-26** (`tests/unit/tst_settings.cpp`, two cases tagged `[S4-I-15]`, plus the Stage 1
`[general]` case strengthened). **The reload is from a COPY of the file under a name QSettings has
never opened**, because QSettings caches a file per process and a reload of the same path in the same
process reads back what this process wrote — which is exactly how the defect lived under a green
suite. The cases: a file shaped like the user's before the untick (the product's own `[%General]`
beside other groups) must read `touchMode=true` from that section, take `restoreSession=false`, read
it back from the copy, and hold **ONE** General section afterwards; and the user's file **as the defect
left it** — two `[%General]` sections — must be read whole. **Planted control:** with the
`General/`↔`general/` mapping removed, all three cases redden — including the Stage 1 case, which
reads the theme back as the default instead of `dark` once it reloads from a copy. That is the
proof the cache hid it: the same case, unchanged but for where it reads from, is red on the old code.
**What it does NOT cover** (convention 19): a real second launch of the product — the unit cases
parse the bytes a launch would parse, through the same `Settings` constructor, and M-05 step 5's
re-walk is the launch.
**Steps** `ctest -R "General section|hand-written \[general\]"`.
**Expect** A `general/` key written by the product is read back by a fresh parse; the file keeps one
General section; a file with two is read whole. **If it fails** S2 — I-15 is back, and no general
preference survives a restart.

**A-87 · A guide SNAPS when a person drags it, at 50 % and at 400 % — I-16, closed** ✅
✅ **2026-09-26** (`tests/ui/tst_rulers.cpp`, two cases tagged `[I-16]`). A-45 and A-46 prove the
resolver's sums with no display, and A-47 drags guides through the widgets — and nothing joined the
two: no case asked where a dragged guide LANDED against a grid line, and no gesture called the
resolver. This case drags through the widgets and asserts the stored page coordinate. With the grid
at **72 pt and one subdivision** (so lines are 48 device pixels apart at 50 % — A-46's trap is why)
and `units/snapToGrid` on, **at 50 % and at 400 %, off the top ruler and off the left one**: a guide
DROPPED 5 device pixels from a line lands on it exactly; one dropped 13 device pixels away does not —
**the control, and the half the criterion does not ask for** (convention 22); and a guide already on
the page, picked up and MOVED to 5 pixels from the line, lands on it — the snap on `dragTo`, the other
half of the gesture. With `units/snapToGrid` off the same drop lands where it was dropped. The second
case is guide-to-guide, as the product ships (`units/snapToGuides` on, `units/snapToGrid` off): a
guide dropped 5 pixels from another takes its coordinate, and **a lone guide nudged 5 pixels MOVES** —
the dragged guide is always within the threshold of its own last position, so a resolver that
counted it would pin every guide in place. **Two planted controls, both run red:** the committed
`GridGuideLayer.cpp` put back fails **9 assertions** — the drop and the move at both zooms on both
axes, and guide-to-guide; the fix with the dragged guide NOT excluded fails the nudge alone,
`288.0 > 288.0` — the guide never left.
**What it does NOT cover** (convention 19): whether 8 device pixels FEELS the same distance at both
zooms on a real panel — M-07's; and object edges, which no product surface supplies until Stage 6
(A-45).
**Steps** `ctest -R "snaps to it at 50|snaps to another guide"`.
**Expect** A drop or a move within 8 device pixels of a grid line or another guide lands on it, at
50 % and at 400 %; 13 pixels does not; a lone guide can be moved 5 pixels. **If it fails** S2 — I-16
is back, and `Ctrl+;` moves no guide.

**A-88 · A red, two-pixel cursor marker on each ruler, with the reading beside it — I-17, closed** ✅
✅ **2026-09-26** (`tests/ui/tst_rulers.cpp`, three cases tagged `[I-17]`). **The string:** a ruler
mapped at one pixel per point with its zero at 10, marker at 154, reads `144.0` pt, `2.00` in, `50.8`
mm, `5.08` cm and `12.00` pc; the same page point at four pixels per point reads `2.00` in; 36 pt
before the page's edge reads `-0.50` — negative, not clamped; no marker reads nothing. **The wiring:**
a real mouse move over the page, two inches right of and one inch below the page's drawn corner,
makes the product's two rulers read `2.00` and `1.00` within a pixel's worth; the pointer leaving
empties both. **The paint, in BOTH themes** (convention 20) and both orientations: the marker's
pixel on the inner edge is `#d93025`, deltaE **86.8** (light) and **82.4** (dark) from the same pixel
painted without a marker, against a floor of 30; the readout's red box lies after the marker, and
BEFORE it near the ruler's far end; and a ruler with no marker has none of that red at all — the
control. **And the marker is 2 to 3 pixels wide**, counted on the inner edge (measured **2**) — the
user's second request of the day, the first one being a hairline that vanished against the ticks.
**Planted controls:** the old marker (`QPalette::Highlight`, no box) fails **16 assertions** —
colour, contrast, box and flip, in both themes and both orientations; a one-pixel marker fails the
width, `1 >= 2`, four times. **The width is asserted as a NUMBER, not as `kMarkerWidth`**: its first
version compared against the constant, and passed the one-pixel plant, because the plant changed the
constant too.
**What it does NOT cover** (convention 19): whether the number is LEGIBLE on a 150 % panel or at
`general/uiFontScale` 0.75 — M-07's. The glyphs are not read; the text is asserted as a string and
the box as painted pixels.
**Steps** `ctest -R "cursor readout|readout follows|cursor marker is red"`.
**Expect** The number beside each marker is the reading at the marker, in the ruler's unit; the
marker is the fixed red in both themes, two pixels wide. **If it fails** S3 — I-17 is back.

**A-89 · A guide being pulled off a ruler is shown DOTTED until the release** ✅
✅ **2026-09-26** (`tests/ui/tst_rulers.cpp`, two cases tagged `[guide-preview]`). The user's request
the same day: while the button is still down over the page, a dotted line where the guide will land.
Each ruler drag is sent in its stages to the ruler, which holds the grab. **Pressed** on the ruler:
nothing on the page. **Dragged** over the page: a preview, and **not** a guide — `guideCount` stays 0.
Painted, the preview's column is guide-coloured on **192 of 577** rows — dotted. **Released** over the
page, the guide it becomes is `==` the preview, and the same measurement reads **577 of 577** — solid,
the control. **Dragged back onto the ruler**, the preview is gone and the column has none of the
guide's colour; released there, no guide. **With Snap to Grid on**, a drag 5 device pixels off a grid
line previews ON the line, and the release lands there — the preview is `dropGuide`'s own guide
(`GridGuideLayer::guideFromRuler`, one function for both), not a second opinion.
**Planted controls:** the preview drawn solid fails `577 < 432`; the preview never wired from the
ruler drag fails both cases at `preview().has_value()`.
**What it does NOT cover** (convention 19): whether the dots read as *"not placed yet"* beside a
placed guide (solid blue) and a locked one (grey dashes) on a real panel — M-07's.
**Steps** `ctest -R "dotted until the release|dotted preview sits"`.
**Expect** A dotted line on the page under a ruler drag, where the release will put the guide, snapped
included; solid once released; nothing once the pointer leaves the page. **If it fails** S3.

**A-92 · The keypad's `+` and `-` zoom under `Ctrl` like the main row's — I-21, closed** ✅
✅ **2026-09-27** (`tests/ui/tst_viewercommands.cpp`, `[I-21]`). The events are the ones **Qt's
Windows key mapper was measured to deliver** that day: a throwaway probe (scratchpad, not in the tree)
put `WM_KEYDOWN` for `VK_SUBTRACT` and `VK_ADD`, with `Ctrl` held in the thread's key state, through
`QWindowsKeyMapper` — **`Key_Minus` and `Key_Plus` (0x2d, 0x2b) with `Ctrl` and `KeypadModifier`**,
text `-` and `+`; the main row's `Ctrl+-` came out as `Key_Minus` with `Ctrl` alone. Each is sent
through the application filter to the focused viewer: keypad `-` zooms OUT, keypad `+` zooms IN, and
— the control — the main row's `Ctrl+-` still zooms out. **Run red before the fix, for the right
reason:** the keypad `+` left the zoom at **0.75 against 0.75**, while the keypad `-` already passed —
which is what the user reported, once corrected.
**What it does NOT cover** (convention 19): the mapper itself, which the probe measured once and this
case takes as given; a real keypad is the walk's.
**Steps** `ctest -R "keypad's minus and plus"`.
**Expect** `Ctrl` with the keypad's `+` zooms in and with its `-` zooms out, exactly as the main row's
chords do. **If it fails** S3 — I-21 is back.

**A-93 · No control byte in a tracked text file — I-22, closed** ✅
✅ **2026-09-27** (`tests/gates/check-text-bytes.py`, `gates.text_bytes.tracked_tree` and
`gates.text_bytes.self_test`, in the `gates` preset and every other). Every file `git ls-files` lists,
minus the five binary types the repository tracks (`.png`, `.pdf`, `.dll`, `.a`, `.exe`), is read as
bytes: nothing below 0x20 but TAB and LF, a CR only as half of a CRLF — about forty tracked files are
CRLF and some mix both, which is not the defect — and no DEL. **Run over the damaged tree first, as its
own negative control: exit 1, exactly the six bytes of I-22 and nothing else**, each with its line and
column (`CLAUDE.md:501:2 BACKSPACE`, `:501:16 BACKSPACE`, `:501:19 LONE CARRIAGE RETURN`, `:627:97
BELL`, `:633:7 LONE CARRIAGE RETURN`, `Stage0-Toolchain-Testing-Guide.md:509:19 VERTICAL TAB`); after
the repair, **686 tracked text files clean**. **`--self-test`** drives nine detections — the four bytes
I-22 found, form feed, NUL, escape, DEL and a CR at the very end of a file — and five inputs that must
pass — TAB, LF, CRLF, mixed endings, UTF-8 — in memory, because a damaged fixture in the tree would fail
the tree case; and it checks the reported positions on I-22's own line. **The enumeration is part of
the claim**: pointed at a directory that tracks nothing, or at one outside any repository, the tree
case exits 1 and says so — it refuses to report clean unless `CLAUDE.md` was among the files it read.
Absent Python it FAILS, through `RequirePython.cmake`, as the corpus cases do.
**What it does NOT cover** (convention 19): a mangled escape that produced a PRINTABLE byte — `\x64`
became `d`, and I-22 was caught only because the same path also held a `\b`; and a file git does not
yet track.
**Steps** `ctest --preset gates -R text_bytes`, or by hand `python tests/gates/check-text-bytes.py --root .`.
**Expect** `text bytes clean -- N tracked text files` and `9 of 9 detections fired, 5 of 5 clean inputs
passed, positions exact`. **If it fails** S3 — a document or source file holds a control byte; the
report names the file, line and column.

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

**M-02 · Full Screen on the monitor you chose** ✅ **signed off by the user, 2026-09-26** — every step
passed, and the walk raised **I-14 (S2)**, fixed the same day.
- **Walk result** On the P-1 rig — **MAG 272URDF**, 4K at 150 %, primary, and **LS27B61x**, 2560×1440
  at 100 % (`--print-screen-metrics`). With `fullScreen/monitor` = `LS27B61x` the presentation opened
  on the 100 % monitor while the window stayed on the 4K; `Ctrl+Shift+M` moved it and moved it back;
  `Esc` left the window where it was. With the key empty it opened on the monitor the window was on.
  `F11` took the window full screen on its own monitor and a second `F11` restored it exactly. **And
  I-14**: every `Ctrl+Shift+M` kept the surface's physical size — 2560×1440 physical in a corner of the
  4K, or 3840×2160 overflowing the 2K. M-02 is the first real test of Stage 2's **I-20** join
  succeeding against a second monitor, and it did.
- **THE STEPS BELOW WERE CORRECTED BEFORE THE WALK, and the walk used them.** As written from the plan
  they pressed `F11` and expected the chosen monitor — but `fullScreen/monitor` governs the
  PRESENTATION (`present.start`, its own surface), `F11` is the window's own full screen on the monitor
  it is on (the key's declaration says so: *"EMPTY means the monitor the window is already on, which is
  what a person expects from pressing `F11`"*), and `Ctrl+Shift+M` is bound only in the presentation's
  `FullScreen` context. **Whether `F11` should honour the setting too was put to the user, who
  decided on 2026-09-26 that it should NOT** — `F11` stays on the window's own monitor, as built.
  `Features.md` §1.2's *"Choose which monitor Full Screen occupies"*, which read both ways, now says
  *presentation* and records the decision.
- **Goal** `fullScreen/monitor` and the `QScreen` join (**I-20**), which no offscreen run can exercise.
- **Pre** P-1. A document copied into `tests\manual\M-02\`.
- **Steps** In Preferences → Full Screen type the second monitor's name, exactly as
  `rawpage.exe --print-screen-metrics` prints it (the field is a text box), and press `Tab`. With the
  window on the **primary**, choose **View → Presentation**. Press `Ctrl+Shift+M`, and again. Press
  `Esc`. Repeat with the field empty, from each monitor. Then press `F11` on each monitor, and again.
- **Expect** The presentation opens on the monitor you named, not the one the window is on, and FILLS
  it; the other monitor keeps showing what it showed; `Ctrl+Shift+M` moves it and it fills each
  monitor it lands on; `Esc` leaves the window exactly where it was. Empty means the window's own
  monitor. `F11` is full screen on the window's own monitor, and a second `F11` restores it exactly.
- **If it fails** S2; S1 if the window does not come back.

**M-03 · A presentation, start to finish** ✅ **signed off by the user, 2026-09-26** — every step passed,
no finding.
- **Walk result** `transitions.pdf` copied to `tests\manual\M-03\`, with `fullScreen/transition` = Fade,
  `fullScreen/autoAdvanceSec` = 2 and `fullScreen/loop` on, so page 4 — which carries neither key — is
  the page the preferences must govern. Each page used its own transition (1 Wipe, 2 Dissolve, 3 a cut,
  because `/Split` is not one of the four the product draws and is read as `None` with its name kept),
  page 4 used the preference's Fade; auto-advance held each page's time (3 s and 5 s from the file's
  `/Dur` on pages 1 and 3, the preference's 2 s on 2 and 4) and looped; `B`/`W` blanked instantly and
  returned to the same page; the laser showed and `Ctrl+L` hid and restored the cursor. **The power
  half, with its control:** this machine has no screen saver program chosen and never sleeps on mains,
  so the observable was the display turning off, set to 1 minute for the walk (`powercfg /change
  monitor-timeout-ac`, restored to 30 afterwards) — it did **not** turn off during the presentation and
  **did** after leaving it, so the request is released on exit (A-58's claim, seen on real hardware).
- **Read before re-walking:** step 5 says *"screen-saver timeout"*, and on a machine with no screen
  saver chosen and sleep set to never, only the display-off timeout can be observed — check it with
  `powercfg /query SCHEME_CURRENT SUB_VIDEO VIDEOIDLE` and shorten it for the walk.
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

**M-04 · Take a monitor away** ✅ **signed off by the user, 2026-09-26** — every step passed, no finding,
**in ONE direction**: the monitor removed was the 150 % MAIN display.
- **Walk result** On the P-1 rig with `Win`+`P` → *PC screen only*, which on this desk switches off the
  **MAG 272URDF** (4K, 150 %, primary) and keeps the **LS27B61x** (2560×1440, 100 %) — so the primary
  itself went away and everything crossed 150 % → 100 %, the direction I-14's surface overflowed in.
  Step 1: the window on the MAG reappeared on the LS27B61x, reachable, at a usable size, pages sharp.
  Step 2: a presentation on the MAG (`fullScreen/monitor` = `MAG 272URDF`), removed mid-slide on page
  3, moved to the LS27B61x — the new primary — still presenting, on page 3, filling the screen: **the
  first real-hardware run of `onTopologyChanged` through I-14's `moveSurfaceTo`**, which A-85 could not
  drive. Step 3: a window left on the MAG at a clean quit was restored reachable onto the one-monitor
  arrangement. **Not walked:** the other direction, *Second screen only*, removing the 100 % monitor
  and keeping the primary — offered as optional and not run.
- **THE STEPS WERE CORRECTED AFTER THIS WALK.** They said *"the secondary monitor"*, assuming *PC
  screen only* keeps the primary; on this desk it keeps the LS27B61x, because Windows' "PC screen" is
  the display it enumerates first, not the main one. The walk therefore used the MAG, and the steps
  now name the monitor by what the removal method does rather than by its role.
- **Goal** D4-8 and D4-12's topology clause, and **P-2**'s reason for existing.
- **Pre** P-2 — **`Win`+`P` → *PC screen only*** (chosen 2026-09-19). Windows keeps the display's
  identity under this method, so step 3 restores onto a fingerprint the product has not seen while
  step 1 returns to one it has. Say so if you use another method instead, because they differ in what
  Windows remembers. **Find out first which monitor your method switches off** — *PC screen only*
  keeps whichever display Windows counts first, which need not be the primary; on the P-1 rig it
  switches off the primary. Call that monitor **the one that goes away**.
- **Steps**
  1. Put a window on the monitor that goes away. Remove it. Restore it.
  2. Start a presentation on the monitor that goes away (`fullScreen/monitor` set to its name). Remove
     it mid-slide.
  3. With both monitors back, leave a window on the monitor that goes away; quit cleanly; remove it;
     relaunch.
  4. *(Optional, the other direction)* Repeat 1 and 2 with *Second screen only*, which removes the
     other monitor.
- **Expect** Step 1: the window reappears on a monitor you can see, at a usable size. Step 2: the
  presentation moves to the primary monitor, still presenting, at the same page. Step 3: every
  restored window is reachable — at least its title bar on a real screen.
- **If it fails** **S1** for anything unreachable; S2 for a window that survives but at a silly size.

**M-05 · Session restore, across a change of shape** ✅ **signed off by the user, 2026-09-26, on the
RE-WALK of step 5** — the first walk that day passed steps 1–4 and FAILED step 5, raising **I-15 (S2)**;
fixed the same day, and step 5 re-walked on the fixed build passed.
- **Re-walk of step 5, 2026-09-26** On the rebuilt binary, with the file the defect had written
  (`restoreSession=false` in a second `[%General]` section): the launch restored nothing and the box
  read **unticked**; ticked again and quit with `Ctrl+Q`, `settings.ini` held **one** `[%General]`
  section — the damaged file converged without repair, as I-15's fix says it should.
- **Walk result** Copies of `text-a4.pdf`, `mixed-page-sizes.pdf` and `pages-900.pdf` in
  `tests\manual\M-05\`, on the P-1 rig. Steps 1–2: documents, tab order, windows on both monitors,
  the split, the Thumbnails pane and each document's place came back. Step 3: a page rotated, `Ctrl+Q`
  answered *Don't Save*, and the document came back clean. Step 4: with the MAG removed every window
  was reachable, and with it back the two-monitor layout returned. **Step 5:** *Reopen the previous
  session at startup* unticked, `Ctrl+Q`, relaunch — **every tab came back, and the box was ticked
  again**. `settings.ini` held `restoreSession=false`, in a SECOND `[%General]` section at its end —
  **I-15**: QSettings reads `[%General]` back as the group `General` and the schema says `general`, so
  no `general/` key had ever been read back after a restart.
- **Step 3 is re-worded** (it said *"close its tab with Don't Save. Quit."*): the tab stays open and the
  quit IS the close — closing the tab first drops the document from the session, and the step would
  then pass vacuously. A-76 had already taken that reading.
- **Goal** D4-9, and the fingerprint on a real topology.
- **Pre** P-1, P-2. Three copies of corpus files in `tests\manual\M-05\`.
- **Steps**
  1. Open all three; split one; open the Thumbnails pane on another; arrange the windows; quit with
     `Ctrl+Q`.
  2. Relaunch. Read what came back.
  3. Rotate a page in one document and, with its tab still open, quit with `Ctrl+Q` and answer
     **Don't Save**. Relaunch.
  4. Remove a monitor, relaunch, restore it, relaunch again.
  5. Turn `general/restoreSession` off (*Reopen the previous session at startup*, Preferences →
     General), quit, relaunch. Turn it back on afterwards.
- **Expect** Steps 1–2: the same documents, tab order, window positions, split and pane. Step 3: the
  document comes back **clean** — no unsaved marker, no rotation. Step 4: windows land on monitors
  that exist in both arrangements, and the two-monitor layout comes back when the monitor does.
  Step 5: nothing is restored.
- **If it fails** **S1** for a resurrected edit or a lost document; S2 for a lost layout.

**M-06 · A 900-page thumbnails pane, judged by a person** ✅ **signed off by the user, 2026-09-26** — every
behaviour as expected, and no finding.
- **Walk result** `pages-900.pdf` copied to `tests\manual\M-06\`, on the P-1 rig. The pane scrolled fast
  both ways; the page view scrolled and paged with `Ctrl+Page Down` while the pane was still filling
  and **stayed responsive**; thumbnails filled in progressively and work scrolled past did not catch up
  afterwards; dragging the pane to its end and clicking page 900 landed promptly; nothing froze; memory
  rose while the pane filled and did not keep climbing under repeated scrolling. **This is the human
  half of A2 at Stage 4** and it agrees with the product figure (A-72, 7.03–8.56 ms against 16.667) —
  recorded beside it in `Tech-Stack-Plan.md` §3.3 as a person's judgement, not a measurement.
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

**M-07 · Rulers, grid, guides and snapping by hand, at 50 % and 400 %** ✅ **signed off by the user,
2026-09-26** — every step walked and passed; no finding reported.
- **Walk result** The user walked every step on the build of commit `9c53635` — the first build on
  which the snap step could pass at all (I-16) and the first with a readout to compare (I-17) — and
  reported all of it passing, with no finding. The two changes made on the user's requests while
  preparing the walk, the two-pixel red marker and the dotted preview of a guide being dragged
  (A-88, A-89), were judged fine on the rig before the commit. **Nothing more specific was reported
  and none is recorded here** — the per-step figures in the steps below are what a re-walk compares
  against, not what this walk measured.
- **Read before walking — written 2026-09-26, before the first walk.** Reading the code to explain
  this case found **I-16 (S2)**: no gesture called the snap resolver, so `Ctrl+;` moved no guide at
  all. It is fixed and **A-87** asserts it, so the snap step is now walkable; it had never been seen
  by a person. **And the readout this case's Expect names did not exist** — `Features.md` §1.3 put
  it in the status bar and nothing was built (**I-17**); on the user's decision the same day it is
  the NUMBER in a red box beside a red marker on each ruler (**A-88**) — and, also on the user's
  requests that day, the marker is two pixels wide and a guide being dragged off a ruler shows
  **dotted** on the page until the release (**A-89**). Three things about the steps below, so a planned behaviour is not reported as a defect
  and a defect is not hidden by the settings:
  - **Set grid subdivisions to 1 (Units & Guides) before judging the snap distance.** At the default
    four, the finest lines are 18 pt apart, which at 50 % on the 150 % panel is 9 device pixels — every
    point is within the 8-pixel threshold of SOME line, so the guide always snaps and the distance
    cannot be judged (A-46's trap, by hand). The finest line is what snaps (`core/Snap.h`).
  - **A guide snaps to another guide before the grid** — `units/snapToGuides` is on by default and
    guides outrank grid lines (D4-6) — so keep the guide you are dragging away from the others. Snap
    to Grid only acts while the grid is **shown**.
  - **Clear All Guides spares a LOCKED guide** — by design (`Features.md` §1.3 lists lock and clear
    side by side). Drag a new guide or two out after the clear, so the relaunch has something to bring
    back besides the locked one. And the relaunch restores them only with *Reopen the previous session
    at startup* on (Preferences → General) — M-05's step 5 turned it off.
- **Goal** The exit criterion's *"identically at 50 % and 400 %"*, which is about how it feels.
- **Pre** `corpus\ordinary\text-a4.pdf` copied to `tests\manual\M-07\`, on the **150 %** monitor —
  A-49 and A-83 hand the rulers' legibility on a 150 % panel to this case.
- **Steps** Show the rulers (`Ctrl+U`) and the grid (`Ctrl+'`). Change the unit through all five,
  and at each put the pointer on the page's right and bottom edges — `text-a4.pdf` is
  595.3 × 841.9 pt, 8.27 × 11.69 in, 210 × 297 mm, 21.0 × 29.7 cm, 49.6 × 70.2 pc.
  Drag two guides out of each ruler. Turn on Snap to Grid (`Ctrl+;`) and drag a guide near a grid
  line, at 50 % and then at 400 %. Lock one guide and try to move it. Clear all guides. Quit and
  relaunch.
- **Expect** The red readout beside each marker matches the ticks under the marker, reads 0 at the
  page's top-left corner and the page's size at its far edges, and is legible; the snap **grabs from the same visual distance at both
  zooms**; a locked guide does not move; guides come back after the relaunch and the file's
  timestamp has not changed.
- **If it fails** S2 for the snap distance; **S1 if the document's timestamp moved.**

**M-08 · The splits** ✅ **signed off by the user, 2026-09-27, on its SECOND re-walk of step 1** — the
first walk and the first re-walk that day FAILED and raised four issues, I-18 to I-21, all fixed the
same day; every other step passed on the first walk *(was "The spreadsheet
split"; widened 2026-09-26 with the three checks T9 and D4-18 had pointed at this case — two of them at
M-09 by mistake — and which no case asked for)*
- **Second re-walk of step 1, 2026-09-27** On the build that fixed I-20 and I-21, every item under
  *What the second re-walk must look at* below: reported by the user as *"all are fixed"*, with
  nothing more specific, and recorded as that rather than as figures nobody gave. No new finding.
- **Re-walk of step 1, 2026-09-27** On the build that fixed I-18 and I-19: **both confirmed fixed**
  by the user. It raised two more. **A wheel UP over the top row did nothing** — only down scrolled —
  **I-20 (S2)**: I-19's fix measured a frozen pane's own movement, and a row frozen at the top of the
  document has nowhere to move up to. Reading `HandTool` to explain it found the same fix's second
  defect: **a hand drag over a frozen pane ran away**, re-sending the whole drag on every mouse move.
  And **`Ctrl` with the keypad's `+` did not zoom in**, anywhere in the product and with or without a
  split — **I-21 (S3)**; first reported as the keypad's `-`, and corrected by the user, whose answers
  also ruled out the IME (English) and NumLock (on). The keypad's `-` works.
- **First walk result, 2026-09-27** Walked by the user to the end, every step, on a copy of
  `text-a4.pdf` in `tests\manual\M-08\` frozen at about 300 %. **Step 1, the zoom:** `Ctrl+=` zoomed
  the body and **not the frozen panes** — the top row slid left and the left column slid up, each at
  the old zoom — **I-18 (S2)**. **Step 1, a wheel over a frozen pane** (asked for during the walk; now
  written into the step below): **all three frozen panes scrolled on their own**, and scrolling the
  top row did not move the body — **I-19 (S2)**. **Passed:** the body scrolled right and down with
  the row following in x only and the column in y only; a window resize; the TOP row's handle and the
  bottom row's moving together, both ways; *Remove Split* leaving one view where the body was; step 2
  — the panes locked with sync on and only one moving with it off; and step 3 — the rulers' shaded
  extent and zero moving to the pane clicked, and, over the pane not clicked, the red marker gone and
  the ticks continuing the clicked page's scale, **which the user judged enough to tell which pane
  the rulers read**.
- **What the second re-walk must look at:** step 1 only, on the build that fixed I-20 and I-21, with
  the split taken at the **top of the page** as before — a wheel **up, down, left and right** over the
  corner, the top row and the left column each scroll the whole sheet and the frozen pane stays put;
  a **`Shift`-drag** (the temporary hand) over a frozen pane moves the sheet by exactly the distance
  dragged; `Ctrl` with the keypad's `+` and `-` zoom in and out; and, again, `Ctrl+=`, `Ctrl`+wheel over
  the top row and `Ctrl+2` each leave all four panes at ONE zoom, aligned, with no scrollbars on the
  frozen panes.
- **Goal** The freeze, which is the split nobody gets right by accident — and what a person sees of a
  two-pane split that no offscreen run can: the scroll lock, the handle, and the rulers.
- **Pre** A copy of `corpus\ordinary\text-a4.pdf` in `tests\manual\M-08\`, zoomed to about **300 %**
  at a FIXED zoom — not a fit — before splitting, so the page is wider and taller than the body.
- **Steps**
  1. Take a spreadsheet split (**View** → **Window** → *Spreadsheet Split*); scroll the body right
     and down; resize the window; change the zoom with `Ctrl+=`, with `Ctrl` and the keypad's `+`
     and `-`, with `Ctrl`+wheel over the TOP row, and with `Ctrl+2`. **Turn a wheel over each frozen
     pane** — the corner, the top row, the left column — **up, down, left and right**, and
     **`Shift`-drag** one of them. **Drag the vertical handle in the TOP row**; then remove the split.
  2. Take a vertical split with rulers on (`Ctrl+U`). Turn a wheel over one pane with synchronized
     scrolling on, then off.
  3. Click in the left pane and read the rulers; click in the right pane and read them again. Hold
     the pointer over the pane you did NOT click and look at what the ruler says there.
- **Expect** The frozen row and column stay put and stay aligned with the body at every zoom and size,
  **all four panes at one zoom whichever pane the zoom was made over**; a wheel in any direction or a
  hand drag over a frozen pane scrolls the body by that much, and the frozen pane does not leave its
  place; the frozen panes have no
  scrollbars; dragging the top row's handle moves the bottom row's with it; removing the split leaves
  one view showing the same place. With sync on the two panes LOOK locked while the wheel turns, and with it
  off only one moves. **The rulers measure the pane you clicked** (D4-18): the shaded page extent and
  the zero move to it; over the other pane the ruler continues the clicked page's scale, and you should
  be able to tell which pane it is reading. If you cannot, that is the finding.
- **If it fails** S2; S3 for the rulers.

**M-09 · Arranging windows** ✅ **signed off by the user, 2026-09-27, on its re-walk** — the first walk
that day FAILED at the cascade and raised I-23 (S2), fixed the same day; every other step passed on
the first walk
- **Re-walk of the tilings, 2026-09-27** On the same build: reported by the user as *"I ran tile on
  both montior and not find anything weird."* — recorded as that, with nothing more specific, and as
  covering the tilings on both monitors. No new finding.
- **Re-walk of the cascade, 2026-09-27** On the build that fixed I-23: reported by the user as *"The
  issue of cascading is fixed."* — recorded as that, and as the cascade only: nothing was reported
  about the two tilings or the two-monitor tiling, so they are not recorded as re-walked.
- **First walk result, 2026-09-27** Walked by the user, every step, on copies of `text-a4.pdf`,
  `cropbox-margins.pdf`, `mixed-page-sizes.pdf` and `pages-900.pdf` in `tests\manual\M-09\`, with
  `mixed-page-sizes` and then `pages-900` torn off. **After *Cascade* the first window sat so high its
  title bar could not be seen** — the topmost thing on the monitor was the Quick Access Toolbar row
  below it — **I-23 (S2)**, with a screenshot. Every other step was reported as passing, and recorded
  as that rather than as figures nobody gave. **Measured afterwards, the tilings were wrong in the same
  way** (§8, I-23): the top strip of *Tile Horizontal* and all three columns of *Tile Vertical* had
  their title bars 45 physical pixels above the screen. The walk did not report them, which is why the
  re-walk below names them.
- **What the re-walk must look at:** on the build that fixed I-23, with the first window MAXIMIZED
  before each arrangement — *Cascade*, *Tile Horizontal* and *Tile Vertical* each leave **every
  window's title bar visible and draggable**, the top one included; no strip's title bar sits over the
  window above it; each cascaded window starts **below the previous one's whole title bar**. A hairline
  where two tiles meet or where the bottom tile meets the taskbar is the bounded residual in I-23, not
  a finding. Then the two-monitor tiling once more.
- **Pre** Four documents open, e.g. copies in `tests\manual\M-09\` of `text-a4.pdf`,
  `cropbox-margins.pdf`, `mixed-page-sizes.pdf` and `pages-900.pdf`. Tear a tab off by dragging it
  **40 px or more** above or below the tab strip and releasing away from any other window's strip; tear
  off the LAST two tabs, left one first, since a merge appends detached windows' tabs in the order the
  windows were made (A-65).
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
| **I-13** | **S3** | **The vertical ruler printed NO numbers: each label was rotated about x = 0, which carried it off the widget's left edge** | ✅ **Closed 2026-09-26, the day the user asked** — the label is anchored at the tick side of its strip, and **A-83** counts painted ink in both rulers |
| **I-14** | **S2** | **`Ctrl+Shift+M` moved the presentation to a monitor of another scale and kept its PHYSICAL size — a quarter of the 4K panel, or overflowing the 2K one** | ✅ **Closed 2026-09-26, the day M-02 raised it** — a move hides, places and shows, as the first placement always did; **A-85** measures physical rectangles on the real monitors |
| **I-15** | **S2** | **No `general/` preference was ever read back after a real restart — theme, UI font scale, touch mode and all three session switches: QSettings reads `[%General]` back as `General/`** | ✅ **Closed 2026-09-26, the day M-05 raised it** — `Settings` maps `general/` ↔ `General/` at its one boundary with QSettings; **A-86** reloads from a copy, past QSettings' per-process cache |
| **I-16** | **S2** | **`Ctrl+;` snapped no guide: `GridGuideLayer::snap` had no caller, so a guide dragged off a ruler or moved on the page stored the raw pointer coordinate** | ✅ **Closed 2026-09-26, the day it was found** — reading the code before M-07's walk; `dragTo` and `dropGuide` resolve through the snap with the dragged guide excluded, and **A-87** asserts where a dragged guide lands |
| **I-17** | **S3** | **No cursor readout anywhere: `Features.md` §1.3 promised the cursor's page coordinates live in the status bar, the stage plan listed "the coordinate readout", and neither was built — the rulers' accent-blue markers were the only indication** | ✅ **Closed 2026-09-26, the day the user raised it** — on their decision, a **red** marker on each ruler with the **reading beside it** in the ruler's unit; `Features.md` §1.3 corrected to say so; **A-88** |
| **I-18** | **S2** | **A zoom reached one pane of a spreadsheet split: `Ctrl+=` zoomed the body, and the frozen row and column slid at the old zoom** | ✅ **Closed 2026-09-27, the day M-08 raised it** — the body leads everything in a spreadsheet split; **A-90** |
| **I-19** | **S2** | **A frozen pane of a spreadsheet split could be scrolled on its own** | ✅ **Closed 2026-09-27, the day M-08 raised it**; **A-91** |
| **I-20** | **S2** | **A wheel up over a frozen header row did nothing, and a hand drag over a frozen pane ran away — a defect in I-19's fix** | ✅ **Closed 2026-09-27, the day M-08's re-walk raised it** — a frozen pane's gestures are the body's before they move anything; **A-91** rewritten |
| **I-21** | **S3** | **`Ctrl` with the keypad's `+` did not zoom in** | ✅ **Closed 2026-09-27, the day M-08's re-walk raised it** — a fallback to `Ctrl+=`; **A-92** |
| **I-22** | **S3** | **Four Windows paths in two documents held control bytes where their backslashes had been** | ✅ **Closed 2026-09-27, the day a byte-level read found it** — repaired, and **A-93** refuses a control byte in any tracked text file |
| **I-23** | **S2** | **An arranged window's TITLE BAR was off the screen: tile and cascade placed each window's CONTENTS in its cell, and the title bar went above it** | ✅ **Closed 2026-09-27, the day M-09 raised it** — the cell is the frame, and the cascade steps by the window's own title bar; **A-94**, on the `windows` platform |

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
repaint"* as a blind spot and left it to M-09 — the wrong case: M-09 is window arrangement, and the
split's manual case is **M-08**, which had not been walked either.

**The fix, 2026-09-26, in `ui/viewer/SplitViewContainer.cpp`.** Every splitter is a `DividerSplitter`
whose `createHandle()` returns a `DividerHandle` that **paints itself** from the palette
`ThemeManager` writes, rather than asking the style for a role the style may ignore (convention 21):
a 6-logical-pixel band in `QPalette::Mid` (the `Border` token) with a one-pixel line down its middle
in `QPalette::PlaceholderText` (`SubduedText`), and the whole handle in `QPalette::Link` (`Accent`)
under the pointer and while dragged. **The band alone was tried first** and measured ΔE 12.4 against
the light desk — over the floor, and still pale — which is why the line is there. No new token: the
three are existing roles, so both themes were already defined (convention 20). **A-82** measures the
painted pixels in both themes, and its planted control — the stock handle — reads **ΔE 0**.

**I-13 · The vertical ruler printed no numbers.** *(Raised 2026-09-26 by the user, who asked after
M-01 why the vertical ruler had none — outside any case: M-07, the rulers' manual case, has not been
walked.)*

**Mechanism.** `RulerWidget::paintEvent` lays a vertical label's box out at `translate(0, pixel + 2)`
and turns it with `rotate(90)`. A quarter turn in Qt's y-down space maps local (x, y) to (−y, x), so the
box's HEIGHT runs towards negative x — the whole box lay left of the widget's edge and was clipped. The
horizontal ruler is not rotated, which is why its numbers were always there. The fix anchors the box at
`across − tick`, the tick side of the label strip, so it runs back across [0, across − tick]: the strip
the horizontal ruler's numbers occupy. Built at T8, so present since 2026-09-21.

**Why no case caught it.** A-49 asserts the rulers' READINGS — a number `readingAt()` returns — and
hands *"whether the numbers read"* to M-07; nothing asserted the numbers were painted, and a reading
is correct whether or not anything is drawn. And a second reason, found while writing **A-83**: the
offscreen platform draws a rotated glyph as nothing at all, so even a pixel case over the vertical ruler
would have been blind to the fix as well as the defect until it loaded a real font.

**Controls.** The planted old origin reads vertical ink **0** on both the `windows` platform and, with
the font loaded, offscreen; the fix reads 123 and 171.

**I-14 · A presentation moved to a monitor of another scale kept its physical size.** *(Raised
2026-09-26 by the user's M-02 walk.)*

**Mechanism.** `FullScreenController::moveToNextMonitor` re-targeted a surface that was already full
screen — `setScreen`, `setGeometry(screen->geometry())`, `showFullScreen` — and a full-screen window
moved in place onto a monitor of another scale kept its size in PHYSICAL pixels. From the 150 % panel
to the 100 % one that is a 3840×2160 surface on a 2560×1440 monitor; the other way, 2560×1440 in a
corner of the 4K. The FIRST placement never had it, because it happens before the surface is shown —
and that is the fix: a move now hides the surface, places it and shows it full screen again
(`moveSurfaceTo`), which is the first placement's path. `onTopologyChanged`, which moves the surface
to the primary when its monitor disappears, went through the same two calls and now goes through the
same function — the same defect, on the path M-04 walks; **M-04 walked it on 2026-09-26**, removing
the 150 % primary mid-slide, and the presentation filled the remaining 100 % screen.

**Measured, not inferred.** On the P-1 rig through `GetWindowRect`: before the fix, after a move
from the 4K, `surface 3840x2160 | monitor 2560x1440`; after it, every move fills its monitor.

**Why no case caught it.** The offscreen platform has ONE screen, so `moveToNextMonitor` returned at
`screens.size() < 2` in every `ctest` run ever made of it — A-57 proves `Ctrl+Shift+M` DISPATCHES,
and nothing had ever seen it move. The suite's header names *"it cannot unplug a monitor"*; it did
not name that it could not have a second one at all. **A-85** is therefore a case that SKIPS under
`ctest` with its reason, and whose evidence is a run on the `windows` platform on a two-monitor desk —
the first of its kind in this file, and recorded as such rather than as a pass it cannot have.

**I-15 · No `general/` preference survived a restart.** *(Raised 2026-09-26 by the user's M-05 walk,
step 5. The defect is Stage 1's — `core/settings/Settings`, T5 — and is logged here because the walk
that found it is this stage's, as I-10 was.)*

**Mechanism.** QSettings writes a group named `general` as the section `[%General]` — the escape for its
own reserved `[General]` — and **reads `[%General]` back as the group `General`, capital G**, measured
with a probe over the user's own `settings.ini`. The schema declares `general/...`, so on every launch
`General/theme`, `General/uiFontScale`, `General/touchMode`, `General/restoreSession`, … matched no
declared key and were preserved as UNKNOWN: **the product has never read a `general/` preference back
after a restart.** It went unnoticed because the defaults are what most people want — the user's theme
is *follow system*, the default. And writing one key produced a **second** `[%General]` section, because
QSettings' in-memory copy then held both `General/...` (read) and `general/...` (written), and it emits
every group it holds.

**Why no case caught it.** Stage 1 T5 measured the `[General]` quirk and wrote a fold for a HAND-WRITTEN
`[general]` section, whose keys QSettings reports with no group at all — and its case reloaded the file
with `Settings reloaded(path)` **in the same process**, which QSettings answers from its per-process
cache of what that process wrote. So the case read `general/theme` back from memory and passed, while
the next launch read `General/theme` from the bytes and forgot it. **Planted control:** with the fix
removed and that case reloading from a copy instead, it reads the theme as the default instead of
`dark` — red on the old code with nothing changed but where it reads from.

**The fix, 2026-09-26.** `Settings` maps a path at its one boundary with QSettings — `general/...` is
stored as `General/...`, the spelling QSettings round-trips, and read back as `general/...` — so nothing
above the boundary sees the file's spelling. A file the defect damaged needs no repair: QSettings merges
the two `[%General]` sections into one group on read, and the next write emits it once. **A-86** holds
two new cases and the strengthened Stage 1 case, all reloading from a copy.

**What else it affected** — every `general/` key, all seven read from `SettingsKeys.def`: `theme`,
`uiFontScale`, `touchMode`, `singleInstance`, and the three session switches `restoreSession`,
`rememberWindowLayout` and `rememberPaneLayout`. The user's file carries `touchMode=true`, never read
until now — and **nothing changes**, because `general/touchMode` is declared and INERT until Stage 14
(`Implementation-Plan.md`; Stage 1's I-38): no code reads it. *(This line first said the fixed build
would apply it — a claim made without reading the code, corrected the same day.)*

**I-16 · `Ctrl+;` snapped no guide.** *(Raised 2026-09-26 — not by a walk: by reading the code to tell
the user how to walk M-07, before anyone had. The defect is T8's.)*

**Mechanism.** T8 built the resolver in `core/Snap` and a `GridGuideLayer::snap()` in front of it that
gathers the page's guides and the grid and calls down — and **nothing called `GridGuideLayer::snap()`**.
The two gestures that place a guide store the raw pointer coordinate: `dragTo` (a guide picked up on the
page, `PageView::mouseMoveEvent`) passed `document->point` straight to `moveGuide`, and `dropGuide` (a
guide pulled off a ruler, through `DocumentWindow`) straight into the new guide. So `Ctrl+;` flipped
`units/snapToGrid`, the ribbon's checked state followed it, and no guide in the product ever snapped —
to the grid or, although `units/snapToGuides` ships ON, to another guide.

**Why no case caught it.** The work was split along a seam and no case crossed it. **A-45 and A-46**
prove the precedence and the device-pixel threshold over synthetic geometry, headless, by calling
`core::snap` directly — correct, and silent about whether anything reaches it. **A-47** drags guides
through the real widgets, and asserts they land at the pointer — which is exactly what a guide that
never snaps does. **A-51** proves `Ctrl+;` toggles the setting. Each half was green, and the claim the
exit criterion makes — *snapping behaves identically at 50 % and 400 %* — was asserted of a function
the product never ran. `tst_rulers.cpp`'s header even named the gap as M-07's (*"whether the 8-device-
pixel tolerance FEELS the same"*), which is a question about a feel that did not exist yet.

**The fix, 2026-09-26.** `dragTo` and `dropGuide` resolve the point through the same path `snap()`
takes, with `activeFlags()` and the layout's presented scale, and keep the coordinate the guide
constrains. **A dragged guide is left out of the candidates** — it sits under the pointer by
construction, and counting it would pin every guide at its own last position the moment guide-snapping
worked (the second planted control proves that happens). The drag snaps on every move, not on release,
so the line a person watches is the line they get. The drop's feedback marker on the ruler still shows
the raw pointer; the guide lands snapped. **A-87** is two cases and two planted controls, both run red.

**I-17 · No cursor readout anywhere.** *(Raised 2026-09-26 by the user, preparing M-07: *"there's no
readout"* — and asking that the rulers' markers be red with the coordinate beside them.)*

**Mechanism.** `Features.md` §1.3 lists *"Read out the cursor's page coordinates live in the status
bar"*, and `Stage4-ViewingSurface-Plan.md` §4 assigned *"the coordinate readout"* to the shell's
files for T5 … T12. T8 built the rulers with a cursor marker in `QPalette::Highlight` — the accent
blue — and `RulerWidget::readingAt()`, whose comment said the shell needed it *"for the status-bar
readout"*; nothing ever called it for one, and the status bar has only the page box and the zoom.
`Architecture-FileMap.md`'s `DocumentWindow` row credited T12 with *"the coordinate readout"* all the
same, which is the claim that made the absence look settled.

**Why no case caught it.** A-49 is titled *"The rulers read out the cursor's page coordinates"* and
asserts `readingAt()` — the arithmetic a readout would print — and that the marker moves; M-07's
Expect says *"the readout matches the ruler"*. Both treat the readout as existing; neither names
where a person reads it. A case asserting a function's answer is not a case asserting anyone sees it.

**The fix, 2026-09-26 — on the user's decision, which also moves the capability.** The readout is on
the RULERS, not the status bar: each ruler's marker is a fixed **red** (`#D93025`), and the reading at
it is painted beside it in white on the same red, in the ruler's unit, after the marker or before it
near the ruler's end. The red is the one colour the ruler does not take from the palette; it carries
its own background for the number and is measured against both themes. `Features.md` §1.3 is
corrected in the same change, and `RulerWidget::cursorReadout()` is the string. **A-88** is three
cases and one planted control run red.

**I-18 · A zoom reached one pane of a spreadsheet split.** *(Raised 2026-09-27 by the user's M-08
walk, step 1: *"I press the Ctrl + = and the body zoom in, and the top row shift left and left column
shift up. Both shifted without zooming."* S2.)*

**Mechanism.** T9 built the spreadsheet split as a constrained SCROLL (D4-13) and derived the three
frozen panes' offsets from the body's — and derived nothing else. Every view command acts on the
focused pane (`MainWindow::registerView`), and after a split the focused pane is the body, so
`Ctrl+=` zoomed the body alone. The scroll mirror then did its job at the wrong zoom: zooming about the
centre moved the body's top-left corner further into the document, and the mirror carried that
corner through PDF-point space into the row and the column **at their own, unchanged scale** — which
is the slide left and up the walk saw. **A second defect sat under it:** the freeze origin was held in
device pixels (`freezeOrigin_`), which change meaning with the zoom, so even a zoom that reached all
four panes would have frozen a different part of the page.

**Why no case caught it.** **A-55** takes the freeze at one zoom and only scrolls; it never changes
the zoom, the fit or the view rotation. Its assertions are the four assignments of the header's
table, and all four held — at a zoom the body no longer had.

**The fix, 2026-09-27 — on the user's decision: all four panes zoom together.** `SplitViewContainer`
now has ONE leader in a spreadsheet split, the body, and **`followBody`** runs on every change of it:
the body's zoom, layout mode and view rotation are copied onto the three frozen panes, **a fit as the
body's NUMBER and never as a fit** — a column a quarter of the window wide would otherwise fit itself
at a quarter of the zoom — and their scroll is derived afterwards. The freeze is held as a **place in
the document** (`FreezeAnchor`: a page, a PDF point on it, and the part of the offset in a margin or a
gap, which `PresentationOptions` keeps in device pixels at every zoom). A zoom made over a frozen pane
— `Ctrl`+wheel reaches the pane under the pointer — is handed to the body (**I-19**'s mechanism). The
scope is the user's choice too: zoom, fit, layout mode and view rotation, because all four broke the
freeze the same way. **A-90**, one case and one planted control run red.

**I-19 · A frozen pane of a spreadsheet split could be scrolled on its own.** *(Raised 2026-09-27 by
the user's M-08 walk, at a wheel over a frozen pane asked for during step 1: *"all 3 frozen panels are
allow to scroll. I think this is a defect?"* — and, dragging the top row's handle, *"the bottom row
does not scroll together when top row is being scrolling"*, the same defect seen from the body. S2.)*

**Mechanism.** `SplitViewContainer.h` said the three frozen panes *"have no scroll of their own"*, and
a comment in the `.cpp` that *"a frozen pane cannot be scrolled by the person"* — and nothing made
either true. Each frozen pane is an ordinary `PageView`, with its own scrollbars, wheel, arrow keys,
hand tool and focus, and only the BODY was allowed to lead the mirror. So any of those moved the frozen
pane it landed on, alone, until the body next moved and re-derived one of its axes — the corner never.

**Why no case caught it.** **A-55** only ever scrolls the body. It proved that the derivation FIRES
when the body moves and never that nothing ELSE moves a frozen pane — convention 22's other half, and
the claim the header made in words.

**The fix, 2026-09-27 — on the user's decision: a scroll over a frozen pane scrolls the body.** Three
parts. **A frozen pane takes no focus** (`PageView::setFrozen`), so a click in it leaves the body
active and every command — navigation, zoom, fit, rotation — lands on the body; which also makes the
rulers measure the body in a spreadsheet split, and the horizontal ruler therefore reads the frozen
row and the vertical one the frozen column, which share those axes. **Whatever still moved a frozen
pane** — the wheel, which reaches the pane under the pointer, and the hand — **was handed to the body
as the same DISTANCE**, measured against where the container last placed that pane. **That part was
WRONG and is replaced — see I-20, found by the re-walk the same day:** a pane frozen at the
document's top cannot move up, so there was no distance to hand on, and the hand's absolute scroll
turned the measurement into a runaway. A frozen pane's gestures are now the body's before they move
anything. **The frozen panes show no scrollbars**,
and the room the body's scrollbars take is kept empty beside them (`setScrollBarReserve`), because a
frozen row one scrollbar wider than the body clamps one scrollbar short of it at the far right. And
since a frozen pane is never the active one, **a guide edited inside it is mirrored from it directly**
(`DocumentWindow::adoptView`), or the mirror that follows the active pane (D4-7) would never hear it.
**A-91**, one case and two planted controls, both run red.

**I-20 · A wheel up over a frozen header row did nothing, and a hand drag over a frozen pane ran
away.** *(Raised 2026-09-27 by the user's re-walk of M-08's step 1, on the build that fixed I-19: *"I
can scroll down on the top row only. Nothing happend when I scroll up."* The runaway was found the same
hour by reading `HandTool` to explain it. S2. **A defect in I-19's fix**, not in T9.)*

**Mechanism.** I-19's fix let a frozen pane MOVE, then measured how far it had moved against where the
container last placed it, handed that distance to the body, and put the pane back. Two things break
that. **A frozen pane is clamped like any other**: the re-walk froze at the top of the page, where the
row sits at offset 0, so a wheel up clamped to 0, nothing moved, and there was no distance to hand on
— the same for the left column scrolling left and the corner in both. **And the hand sets an
ABSOLUTE scroll** — `HandTool::move` writes the offset at the press minus the distance dragged since
(`HandTool.cpp`) — so a pane put back after every move re-sent the whole drag each time: five moves of
10 px moved the body 10 + 20 + 30 + 40 + 50 = 150 px.

**Why no case caught it.** **A-91's first version** froze mid-page, where a frozen pane has room to move
both ways, and drove the hand as a relative `scrollBy` — and said so under *What it does NOT cover*,
which is the exact back door convention 19 exists to make a reader subtract. The declared limitation
was the defect.

**The fix, 2026-09-27.** Nothing measures a frozen pane's movement any more. **Its gestures are the
body's before they move anything** (`PageView::setFrozen(body)`): the wheel is re-sent to the body's
viewport, at the pointer's position pulled inside it — over the frozen row that x is exactly the
body's, and a `Ctrl`+wheel zoom anchors on the body's edge rather than on content it is not showing —
and the tools are given a host (`PageView::ToolTarget`) that is the frozen pane for everything a tool
measures or paints and the BODY's controller for what it scrolls or zooms, so the hand moves the body
one-to-one. Whatever else moves a frozen pane is **undone** (`SplitViewContainer::restoreFrozen`) and
never guessed at. **A-91 is rewritten**: frozen at (0, 0), wheels up, down and both ways sideways, and a
real five-move hand drag; two planted controls run red.

**I-21 · `Ctrl` with the keypad's `+` did not zoom in.** *(Raised 2026-09-27 by the user during M-08's
re-walk, first as the keypad's `-` and corrected: *"Ctrl+- can zoom out, and Ctrl++ cannot zoom in.
Both keys are come from number pad."* Everywhere in the product, split or not. English input, NumLock
on. S3 — the main row's `Ctrl+=` works.)*

**Mechanism.** §8 spells zoom in `Ctrl+=`. The main row's `=` key with `Ctrl` arrives as `Key_Equal`
and binds. The keypad's `+` arrives as **`Key_Plus` without Shift** — measured through
`QWindowsKeyMapper` with a probe, and with `KeypadModifier` — so `ShortcutBinder` formed `Ctrl++`,
which no row binds, and the key did nothing. Stage 2's **I-33** had taught the binder that a SHIFTED
`+` is the `=` key; an unshifted one it left alone, and its own comment said why: a bare keypad `+`
is §8's `+`. The keypad's `-` needed nothing — it arrives as `Key_Minus`, the main row's key.

**Why no case caught it.** Every zoom-chord case parses the chord §8 spells, or sends the event the
main row sends; none sent a keypad key.

**The fix, 2026-09-27.** `ShortcutBinder::chordCandidatesFor` offers `Ctrl+=` as a FALLBACK for a
keypad `+` with `Ctrl` held — tried only when the reported chord binds nothing, and never without
`Ctrl`, so the bare keypad `+` stays §8's `+`. No §8 row changes: it is the same key on another part
of the keyboard, not a new chord. **A-92**, run red before the fix for the right reason.

**I-22 · Four Windows paths in two documents held control bytes where their backslashes had been.**
*(Found 2026-09-27 — not by a walk: by a byte-level read, after an edit script split two of
`CLAUDE.md`'s lines at a lone carriage return while recording M-08. S3 — documentation, and one of the
four a command in `CLAUDE.md`'s Commands section.)*

**What was in the files.** `CLAUDE.md:501` held `.`BACKSPACE`uildd-release`BACKSPACE`in`CR
`awpage_bench_surface.exe` where `.\build\x64-release\bin\rawpage_bench_surface.exe` belonged
(`a01192b`, Stage 4 T14, 2026-09-23); `CLAUDE.md:627` held `` `build\<preset>`` BELL ``4-probe\` ``
for `` `build\<preset>\a4-probe\` `` (`d877992`, Stage 3's sign-off, 2026-09-18); `CLAUDE.md:633` held
`` `bin`` CR ``awpage.exe` `` for `` `bin\rawpage.exe` `` (`aa1879b`, I-10's fix, 2026-09-24); and
`Stage0-Toolchain-Testing-Guide.md:509` held `%USERPROFILE%` VERTICAL TAB `cpkg` for
`%USERPROFILE%\vcpkg` (`a8eccd3`, 2026-08-19). `CLAUDE.md` is loaded into every session, and the
session that found this had read the benchmark command as `.uildd-releaseinawpage_bench_surface.exe`.

**Mechanism — measured, not guessed.** Each was a document edited through a script, and a
here-document sent through the agent's Bash tool **collapses a doubled backslash to one**: `a\\b` sent
in a quoted here-document arrives as `a\b` (measured 2026-09-27 with `od -c`). So a Python or shell
literal that escaped a Windows path correctly reached the interpreter with single backslashes, and
`\b`, `\r`, `\a`, `\v` and `\x64` were read as BACKSPACE, CARRIAGE RETURN, BELL, VERTICAL TAB and `d`.
The repair's first attempt went through the same path and **rebuilt the damaged bytes exactly** — the
replacement strings were re-escaped on the way in — which is how the mechanism was confirmed. The
repair that worked was written with the Write tool and builds every backslash from `chr(92)`.

**Why nothing caught it.** A terminal draws all four invisibly — a backspace and a carriage return
overwrite, a bell makes no mark — so every reader, and every rendered diff, saw a plausible path. No
check read the files as bytes. `layer-lint` and `i18n-lint` read source for their own rules and have
no opinion on a control byte.

**The fix, 2026-09-27.** The four paths restored byte for byte, and **A-93**: a `gates` check that
refuses a control byte in any tracked text file, run red over the damaged tree before the repair. And
`CLAUDE.md` gains rule 10 under *How to write it*, naming the here-document trap.

**I-23 · An arranged window's title bar was off the screen.** *(Raised 2026-09-27 by the user's M-09
walk, with a screenshot: *"after cascasding, the first window is put much higher so that the title bar
cannot be seen. The top-most in the monitor is function bar."* S2 — M-09's *"cascade keeps every title
bar reachable"*, and a window whose title bar is off the screen cannot be dragged by it. Predicted from
the code while M-09's walk was being prepared, before anyone had walked it; the prediction was
recorded as one, not as a finding.)*

**Mechanism — measured.** `WindowArranger::tile` and `cascade` handed each cell to
`QWidget::setGeometry`, which on a top-level window places the **client area**; Windows draws the title
bar above it. A probe on the rig's 150 % primary, three windows, physical pixels, work area 0,0 →
3840 × 2088: Qt reported every frame **30 logical pixels above** its contents, and DWM's visible frame
of the first cascaded window started at **y = −45**, the second at −13. **The tilings had the same
defect, which the walk did not report**: the top strip of *Tile Horizontal* at −45, with each lower
strip's title bar covering the bottom 47 pixels of the strip above, and **all three columns** of *Tile
Vertical* at −45. And a second defect under the first: the cascade stepped by the style's
`PM_TitleBarHeight`, **21**, against a native title bar of **30**, so once the frame was in its cell
each window would still have covered a third of the previous one's title bar.

**Why no case caught it.** **A-64 judged the cells and was right about them** — it is arithmetic over
synthetic work areas, on purpose, and its own file said it *"cannot see a taskbar"*. Nothing looked at
where a FRAME lands in a cell, and nothing could have under `ctest`: the offscreen platform draws no
frame, so there `frameGeometry() == geometry()` and the defect does not exist. A-64's *Expect* claimed
*"keeps every title bar on-screen"* all the same, and was ticked.

**The fix, 2026-09-27.** **The cell is where the FRAME goes** (`WindowArranger::placeFrame`): the
margins Qt reports — `frameGeometry()` minus `geometry()` — are removed from the cell before
`setGeometry`, after a maximized or minimized window is restored and not before, since a maximized
window's frame is not the one it will have. **The cascade steps by the window's own title bar**, read
from the same margins, with the style's metric and the old constant as fallbacks. Still Qt's answer and
still no `platform/` interface (D4-17). **A-94** reads what Windows draws, on the `windows` platform,
with the first window maximized before each arrangement; **two planted controls run red** — the old
`setGeometry(cell)` (−45) and the style's step (21 against 30).

**The residual, measured and not removed.** Qt counts as client the border Windows 11 draws at the
left, right and bottom, so a tile's visible edge lies up to one logical pixel outside its cell — 2
physical pixels at 150 %, 3 where a logical edge lands on a half pixel. Two neighbouring tiles share a
hairline and the bottom tile touches the taskbar's edge by that much. Removing it would need DWM's
extended frame bounds in the product, which is `platform/` and a decision D4-17 made the other way;
recorded in `WindowArranger.h` and bounded by A-94 rather than left for a reader to find.

## 9. Sign-off criteria

Stage 4 is done when **all** of these hold — [`Stage4-ViewingSurface-Plan.md` §13](./Stage4-ViewingSurface-Plan.md#13-definition-of-done-for-stage-4)
restated as a checklist.

1. ✅ **2026-09-27 — all 95 carry a result.** The 95th is **A-94**, I-23's, whose result — like A-85's — is a `windows`-platform run, because under `ctest` it skips by design. The 94th is **A-93**, I-22's guard. The 91st and 92nd are **A-90** and **A-91**, added with the fixes for I-18 and I-19 — A-91 rewritten the same day with I-20's — and the 93rd is **A-92**, I-21's. *Before that, 2026-09-26 — all 90.* The 81st is **A-80**, added 2026-09-24 with I-10's fix, and the 82nd to 87th are **A-81**, **A-82**, **A-83**, **A-85** and **A-86**, added 2026-09-26 with the fixes for I-11 to I-15, and **A-84**, D4-18's; the 88th and 89th are **A-87** and **A-88**, I-16's and I-17's, the same day, and the 90th **A-89**, the dotted preview — **A-85's result is a `windows`-platform run, because under `ctest` it skips by design**; the last of the original eighty to get one was A-04. Every case
   in this file was run and its actual output recorded against it, not summarized (Stage 1's I-32).
   **A-04 is a `portable-asan` CI job and this machine has no ASan runtime**, so it was read from that
   job's **log** rather than its tick (rule 3): CI run 35879213301, the T14 push, **609/609** with all
   39 Stage 4 `core/` cases present by name and no sanitizer line.
2. ☐ M-01 … M-12 **walked and signed off by you**. I do not mark a manual case. A case not run for
   want of hardware (M-12's touch half) is recorded as such, never as passed. **Ten of the twelve are
   walked and signed off: M-11, 2026-09-24, passed — and it raised I-10 (S2); M-01, 2026-09-26,
   failed its first walk — raising I-11 (S2) and I-12 (S3), both fixed the same day — and PASSED its
   re-walk on the fixed build, the same day; M-02, 2026-09-26, passed — and it raised I-14 (S2),
   fixed the same day; M-06, M-03 and M-04, 2026-09-26, passed with no finding — M-04 in one
   direction only, the 150 % primary removed; M-05, 2026-09-26, failed at step 5 and raised I-15
   (S2), fixed the same day, and passed step 5's re-walk; M-07, 2026-09-26, passed with no finding,
   after I-16 (S2) and I-17 (S3) were found and fixed while preparing it.** **M-08, 2026-09-27, FAILED
   its first walk at step 1** — I-18 (S2) and I-19 (S2), both fixed the same day — with every other step
   passing; **step 1's re-walk the same day confirmed both and raised I-20 (S2) and I-21 (S3)**, both
   fixed that day; **and the second re-walk of step 1 PASSED the same day, signing M-08 off.**
   **M-09, 2026-09-27, FAILED its first walk at the cascade** — I-23 (S2), a title bar off the
   screen, which measurement showed in both tilings too — fixed the same day; **the re-walk that day
   confirmed the cascade and found nothing wrong with the tilings on either monitor, signing M-09
   off.** The other two — M-10 and M-12 — are unwalked, and are the stage's remaining gap and the
   honest summary of its state.
3. ✅ **2026-09-23, T14.** `checks` clean — four gates — and **`mutation-lint` reports one command**
   (A-06), with `Commands.def` and `core/cmd/commands/` **byte-unchanged across the whole stage**
   (A-07), which is the stronger claim.
4. ✅ **2026-09-27, re-run after I-23's fix: 1132/1132 on `x64-release` and on `x64-debug`**, the
   one new case A-94's, with **three** named skips — the battery case, A-85 and now **A-94**, both
   of which need a real desktop and were run on the `windows` platform instead, A-94 passing with 86
   assertions — and `checks` clean on four gates (`layer-lint` 423 files / 3,390 includes); no
   `warning:` line from either incremental build. *Before that,* **re-run after I-22's guard: 1131/1131 on `x64-release` and on `x64-debug`**, the
   two new cases `gates.text_bytes.*` (A-93), and the `gates` preset **126/126**, with the same two
   named skips and `checks` clean on four gates. *(A first debug run made WHILE the release suite
   was running reported two failures — the segment-heap manifest case and `recovery.kill_saves` —
   under the previous test numbering; both passed alone and the whole suite passed run alone.
   Recorded rather than explained: the cause was not established.)* *Before that,* **2026-09-27,
   re-run after I-20's and I-21's fixes: 1129/1129 on `x64-release` and on
   `x64-debug`**, the one new case A-92's — A-91 rewritten, not added — with the same two named skips
   and `checks` clean on four gates (`layer-lint` 423 files / 3,384 includes); no `warning:` line from
   either incremental build. *Before that,* **after I-18's and I-19's fixes: 1128/1128 on `x64-release` and on
   `x64-debug`**, the two new cases A-90 and A-91, with the same two named skips and `checks` clean on
   four gates (`layer-lint` 423 files / 3,382 includes); no `warning:` line from either incremental
   build. *Before that,* **2026-09-26, re-run after the dotted preview and the two-pixel marker: 1126/1126 on
   `x64-release` and on `x64-debug`**, the two new cases A-89's, with the same two named skips and
   `checks` clean (`layer-lint` 423 files / 3,376 includes; no `warning:` line from either incremental
   build). *Before that,* **after I-17's fix: 1124/1124 on both presets**, the three new cases A-88's.
   *Before that,* **after I-16's
   fix: 1121/1121 on both presets**, the two new cases A-87's. *Before that,* **after I-15's fix: 1119/1119 on both presets**, the two new
   cases A-86's. *Before that,* **after I-14's
   fix: 1117/1117 on `x64-release` and on `x64-debug`**, with
   **two** named skips — the battery case, and **A-85**, which needs two monitors at different scales
   and was run on the `windows` platform instead, passing on both presets — and `checks` clean
   (`layer-lint` 423 files / 3,368 includes). *Before that,* **after I-13's fix and D4-18's case:
   1116/1116**, the two new cases A-83 and A-84. *Earlier that day,* **with the fixes for I-11 and
   I-12: 1114/1114 on both presets** — the six new cases are A-81's four and A-82's two — with `checks` clean on four
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
   raised **I-11 (S2)** and **I-12 (S3)** and both closed that day, with **A-81** and **A-82**; the
   user's question after it raised **I-13 (S3)**, the vertical ruler with no numbers, closed the same
   day with **A-83**; M-02's walk raised **I-14 (S2)**, a moved presentation keeping its physical
   size, closed the same day with **A-85**; M-05's raised **I-15 (S2)**, no general preference
   surviving a restart, closed the same day with **A-86**; and reading the code before M-07's walk
   raised **I-16 (S2)**, a snap no gesture called, closed the same day with **A-87**; and the user,
   preparing that walk, raised **I-17 (S3)**, no cursor readout anywhere, closed the same day with
   **A-88**. **And again on 2026-09-27:** M-08's first walk raised **I-18 (S2)** and **I-19 (S2)** —
   a zoom reaching one pane of a spreadsheet split, and a frozen pane scrolling on its own — both
   closed the same day with **A-90** and **A-91**; and its re-walk that day raised **I-20 (S2)**, a
   defect in I-19's fix, closed with A-91 rewritten, and **I-21 (S3)**, the keypad's `+`, closed with
   **A-92**. And **I-22 (S3)**, four document paths holding control bytes, found the same day by a
   byte-level read and closed with **A-93**. And M-09's first walk that day raised **I-23 (S2)**, a
   cascaded window's title bar off the screen — the tilings measured the same — closed that day with
   **A-94**.
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

**WHERE THAT LEAVES THE STAGE: the automated half is COMPLETE, the Linux job included; TEN OF THE
TWELVE MANUAL CASES ARE WALKED AND SIGNED OFF — M-11, M-01 ON ITS RE-WALK, M-02, M-06, M-03, M-04,
M-05 ON ITS STEP-5 RE-WALK, M-07, M-08 ON ITS SECOND RE-WALK OF STEP 1 AND M-09 ON ITS RE-WALK — AND
EVERY ISSUE ANY OF THEM RAISED IS FIXED.** **Item 2 is the whole of what is outstanding** — two
unwalked cases, M-10 and M-12. That each of the first three manual cases walked in this stage found a defect the
complete automated half had ratified, and that each fix needed a new case rather than only a new line
of code, is the argument for the other four rather than against them — and M-07's two defects were found
by reading the code to prepare it, before a person had walked it at all; that M-06, the stage's
verdict case, M-03, M-04 and M-07 found nothing is the verdict, the presentation and the topology path holding up
to a person. That is a legitimate state to report and it is not *done* — `CLAUDE.md`'s workflow
says so in as many words, and Stage 4's manual half is the largest of any stage because its subject
is what a person sees: two monitors at different scaling, a display unplugged mid-presentation, a
900-page pane judged for smoothness, a guide dragged off a ruler by hand.
