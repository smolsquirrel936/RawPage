# Stage 4 — Viewing and Navigation Surface · Testing Guide

> **Authoritative verification document for Stage 4.** Written from
> [`Stage4-ViewingSurface-Plan.md`](./Stage4-ViewingSurface-Plan.md) and
> [`Implementation-Plan.md` § Stage 4](./Implementation-Plan.md#stage-4--viewing-and-navigation-surface),
> **before any Stage 4 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** ⚪ **Written 2026-09-18, the same day as the plan and the day Stage 3 was signed off.
> No case has a result.** **79 automated** (A-01 … A-79) and **12 manual** (M-01 … M-12). No issue is
> open, because none has been raised: §8 is empty and stays empty until something is run or read.
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
| Most of the 361 declared commands are still unregistered | per feature stage |

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

**A-01 · Configure and build, both presets, from clean** ☐
**Goal** Stage 4's sources compile under the pin with `-Werror` in `core/`, `pdf/` and `ui/`.
**Steps** `cmake --preset x64-debug`, `--preset x64-release`; `cmake --build --preset <p> --clean-first` for each.
**Expect** Exit 0 on all four; **0 `warning:` and 0 `error:` lines in either log**, `ui/` included. **`--clean-first` is required** — an incremental build compiles nothing and its warning census is inherited rather than measured (Stage 3 A-01). **If it fails** S1.

**A-02 · The `ui/` warning grace list is still empty** ☐
**Steps** Read `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake`.
**Expect** Empty. Stage 4 is the largest `ui/` stage since Stage 1 and adds no legacy. **If it fails** S3 — and the entry's reason is recorded in the status block.

**A-03 · The headless suite is still headless** ☐
**Goal** `SessionState` and the snap resolver are `core/`, and `core/` still links no display.
**Steps** `ctest -R "gates.headless_assert"`.
**Expect** Every case as before; `rawpage_tests` links no Qt GUI module and no engine. **If it fails** S1 — the portable half is where this stage's sanitizer coverage lives.

**A-04 · `portable-asan` builds and runs the Stage 4 `core/` suites** ☐
**Goal** O10's coverage, applied to what this stage puts in `core/`.
**Steps** The `portable-asan` CI job. **Read the log, not the tick** (rule 3).
**Expect** The log names `tst_sessionstate`, `tst_fingerprint`, `tst_snap`, `tst_outline` and `tst_metadata` as run; **0 `runtime error:`, 0 `ERROR: AddressSanitizer`, 0 `LeakSanitizer` lines**. **What it does not cover, and the case says so:** every pane, the split, the presentation and the arranger, which are `ui/`. **If it fails** S1 for a sanitizer finding; S2 if the suites are absent from the log.

**A-05 · The product still starts, and the startup census is unchanged where it should be** ☐
**Steps** `$env:QT_QPA_PLATFORM="offscreen"; rawpage.exe --dump-ui-strings`; `--list-actions`.
**Expect** Exit 0; every `panes.*` and `shell.*` key resolved in `en` and `zh-TW` with **no key resolving to itself**; the platform census still **6 of 18** (Stage 4 adds no interface, D4-17) and the backend census still **3 of 7** (the three readers are `IDocumentBackend` members, not interfaces). **If it fails** S2.

### 5.2 The gates — and the one number that must not move

**A-06 · `checks` clean, and `mutation-lint` still says ONE command** ☐
**Goal** **D4-1's mechanical proof that the viewing surface mutates nothing.**
**Steps** `cmake --build --preset x64-release --target checks`.
**Expect** Exit 0; four gates; *"mutation-lint: clean -- 1 command(s): RotatePageCommand"*. **If it fails** S1 if the count is above one and a Stage 4 file is the cause — a view gesture became a document change; S2 otherwise.

**A-07 · `core/cmd/Commands.def` is byte-unchanged across the stage** ☐
**Steps** `git diff <Stage 3 sign-off commit>..HEAD -- core/cmd/Commands.def core/cmd/commands/`.
**Expect** Empty. **The control:** a planted row makes A-06 report two commands, which is what proves A-06 reads what it claims. **If it fails** S1.

**A-08 · The layer and waiver census** ☐
**Steps** `layer-lint.exe --root . --explain`.
**Expect** Clean; **fourteen** active escapes, unchanged — Stage 4 writes no file of its own except through `core/settings/`, which is already allowed. A fifteenth is named and justified in the status block or removed. **If it fails** S2 for an unnamed waiver; S3 for a count that disagrees with `CLAUDE.md`, which is then corrected.

**A-09 · `i18n-lint` knows the new family, and no key is missing** ☐
**Steps** `i18n-lint.exe --root . --explain`; a fixture referencing `panes.noSuchKey`.
**Expect** `keyFamilies` lists `panes`; the fixture fails `unknown-key` naming **that** key; every key Stage 4 references exists in `en` and `zh-TW`. **Do not add a family for a feature — add one for a namespace** (Stage 3's I-16, where a `recovery` family was required by a case and never existed because the strings shipped as `shell.recovery.*`). **If it fails** S2.

### 5.3 The corpus this stage is judged on

**A-10 · The three new files regenerate byte-identically and say what they claim**
✅ **2026-09-19, T1.** `--verify` → *"corpus check ok: 70 files regenerate byte-identically"*; `--check-provenance` → *"corpus check ok: 70 PROVENANCE.md rows, one per file"*; `--self-test` → *"5 of 5 controls fired"*, each naming the input that fired it. `git status corpus/` lists only the three new files and `make-ordinary.py`: **no existing corpus file moved a byte**, which is the half `--verify` cannot claim on its own — it compares the tree against the generators, not against what was there yesterday. Each new row says what its file carries, including the two deliberate faults (an empty page, a destination naming object 9999).
**Steps** `python corpus/make-corpus.py --verify`, `--check-provenance`, `--self-test`.
**Expect** All three exit 0; **70 files**, 70 `PROVENANCE.md` rows; every existing file byte-unchanged (`git status`). Each new file's row says what fault or feature it carries. **If it fails** S2.

**A-11 · `cropbox-margins.pdf` separates the three rectangles** ☐
🟡 **2026-09-19, T1 — the fixture is built and measured; the CASE is owed to T12 and this box stays unticked.** From the bytes: `/MediaBox [ 0 0 595.276 841.89 ]`, `/CropBox [ 72 72 523.276 769.89 ]`, ink confined to `144 160 451.276 681.89` — **72 pt between each rectangle and the next on every side, twice the 36 pt this case requires**. That the engine honours the crop is measured rather than assumed: `golden-update --survey` reports the page as **602×931 px at 96 dpi**, which is the CropBox, not the 794×1123 sheet. **Why it is not run:** these Steps name a `render.` case reading a **content box**, and `core/backend.h` has no box accessor of any kind until **T12** builds one (D4-14) — so the observable this case is written against does not exist yet, and a tick here would be the intent-for-effect substitution §1 forbids. *(The ink claim was checked against the RENDERED baseline, not the generator: the first capture showed body text spilling across the whole sheet — see the status block's T1 deviation (b).)*
**Goal** The fixture Stage 2's **I-28** said did not exist.
**Steps** A `render.` case reads the page's media box, crop box and content box.
**Expect** **Three different rectangles**, each strictly inside the last, with at least 36 pt between the content and the CropBox and between the CropBox and the MediaBox. **If it fails** S2 — a fixture that does not carry its fault tests nothing (Stage 3's A-17).

**A-12 · `dests-named.pdf` carries both destination forms, and one that dangles** ☐
🟡 **2026-09-19, T1 — the fixture is built and read; the CASE is owed to T4 and this box stays unticked.** From the bytes: a `/Names /Dests` root with `/Kids`, two kid nodes each with `/Limits` and `/Names` (`chapter-one`…`chapter-two`, `ghost-page`…`table-of-figures`, both in byte order), the old-style catalog `/Dests` dictionary beside it, `/XYZ`, `/Fit` and `/FitH` present, the first kid writing `<< /D […] >>` and the second bare arrays, and `9999 0 R` appearing exactly once — the dangling destination. **Why it is not run:** the resolver these Steps call is `core/doc/Destinations` and its facade reader, which **T4** builds; nothing today can resolve a destination, so nothing can yet report one as unresolvable.
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

**A-18 · A session round-trips** ☐
**Expect** Three documents, tab order, active tab, per-window geometry, per-document layout mode, zoom intent, scroll position and guides survive capture → write → read → restore, field by field. **If it fails** S2.

**A-19 · The fingerprint is stable, and sensitive to what it must be** ☐
**Goal** D4-8, over **synthetic** `MonitorInfo` values — the case says so, and names what it therefore does not cover: a real topology, which is M-05's.
**Expect** Identical arrangements hash identically whatever order `monitors()` returned them in; a changed `scaleFactor`, position, pixel size, `primary` flag or monitor count changes the hash; **`friendlyName` does not** (it may be empty and is not unique — `platform.h`). **If it fails** S2.

**A-20 · An unknown arrangement falls back to the most recent** ☐
**Expect** Geometry saved under arrangement A, restored under unknown arrangement B, comes from A rather than from nothing — and is then clamped (A-21). **If it fails** S2.

**A-21 · Every restored window is clamped onto a desktop that exists** ☐
**Goal** The S1 this stage most plausibly ships: a window nobody can reach.
**Steps** Restore a session captured on a 5760×2160 virtual desktop onto a synthetic 1920×1080 one.
**Expect** Every window's rectangle **intersects** an available screen by at least a stated minimum, and its title bar is on-screen. **The control:** a window already fully on-screen is **not moved by a single pixel**. **If it fails** S1.

**A-22 · The arrangement store is bounded** ☐
**Expect** Remembering N arrangements evicts the least recently used and never grows without limit; the bound is read from the code, not from this guide. **If it fails** S3.

**A-23 · A document that no longer exists is dropped with one report** ☐
**Expect** Restore reports the missing path **once**, opens nothing for it, and the next capture does not carry it. **If it fails** S3; S2 if it is reported on every launch.

**A-24 · A corrupt or future session file is ignored, not fatal** ☐
**Steps** Truncated JSON, a schema version above ours, an empty file, a directory where the file should be.
**Expect** The product starts with no session restored and one log line each; the file is replaced on the next clean quit. **If it fails** S1 for a failure to start.

### 5.6 The read-only models (T4)

**A-25 · The outline is read with its structure and its open state** ☐
**Steps** `tst_outline` over `ordinary/outline-labels.pdf`.
**Expect** Two levels, the parent/child relationships, the open/closed flag, and each node's destination resolved to a page index. **If it fails** S2.

**A-26 · Destinations of three kinds resolve, and a dangling one is reported** ☐
**Steps** `tst_outline`/`tst_metadata` over `dests-named.pdf`.
**Expect** `/XYZ` gives a page and a point, `/Fit` a page, `/FitH` a page and a top; **the dangling destination is reported and resolves to nothing**, and the reader does not stop at it. **If it fails** S2.

**A-27 · A document with no outline and no destinations answers empty** ☐
**Expect** `outline()` and `destinations()` on `text-a4.pdf` return an empty model, not a failure, and the panes show their empty state rather than an error. **If it fails** S3.

**A-28 · Info is read; XMP is not claimed** ☐
**Steps** `tst_metadata` over a file with `/Info`, one without, and one whose `/Info` holds a malformed date.
**Expect** Every Info field read as text; a missing `/Info` is empty rather than an error; a malformed date is shown as it was stored rather than invented. **The case states that no XMP is read** (D4-4). **If it fails** S3; S2 for a field invented.

**A-29 · Page labels, and the page box that reads them** ☐
**Goal** §1.4's *jump by logical page label*, on the file `PROVENANCE.md` says exists for it.
**Expect** Over `outline-labels.pdf`, index 3 is labelled **"1"**; the status bar shows the label and the physical index; *Go to Page* accepts `iv` and `A-12` as well as a number, and an unknown label is refused without moving the view. **If it fails** S2.

**A-30 · The readers are hydrated once, at open** ☐
**Expect** Opening a document calls each reader once; opening the Bookmarks pane on an already-open document reads nothing from the engine. **If it fails** S3 — a pane that re-reads on every show is a cost nobody measured.

### 5.7 The panes (T5, T6)

**A-31 · The Thumbnails pane declares its context, and its chords resolve from a KEY EVENT** ☐
**Goal** `KeyContext::PageThumbnails` has been reachable in tests only since Stage 3 T10 (D3-13).
**Expect** With focus in the pane, `activeContext()` is `PageThumbnails`; `Enter`, `+`, `-` and the arrows do what §8 says, sent as key events to a shown, laid-out widget (rule 4); **`Ctrl+R` dispatches Stage 3's `page.rotate`**, and the pane itself constructs nothing (D4-1). **If it fails** S2.

**A-32 · The pane is a viewport, not a document** ☐
**Goal** D4-10's bound.
**Steps** Open `pages-900.pdf`, show the pane, record every enqueued request.
**Expect** Only the visible thumbnail slots plus the stated margin are enqueued, **all at `Speculative`**; scrolling the pane cancels what left and enqueues what arrived; **the control** — the count does not grow with the document's page count. **If it fails** S1 for 900 requests; S2 for a wrong priority.

**A-33 · Clicking a thumbnail moves the view, and the view moves the selection — without a loop** ☐
**Expect** A click scrolls the view to that page and records a view-history entry; scrolling the view updates the pane's selection; **neither triggers the other a second time** (a counter asserts exactly one round per gesture). **If it fails** S2 — a feedback loop here is a scroll that fights the user.

**A-34 · Thumbnail size, and its bounds** ☐
**Expect** `+`/`-` step the size between a minimum and a maximum; the size survives closing and reopening the pane in one session; a size at the bound does not step past it. **If it fails** S3.

**A-35 · The Bookmarks pane navigates, and edits nothing** ☐
**Expect** `Enter` and a click go to the destination; `→`/`←` expand and collapse; `*`/`/` expand and collapse all. **The control:** `F2`, `Delete`, `Ctrl+B`, `Tab`, `Shift+Tab`, `Alt+↑`, `Alt+↓` and `Ctrl+Shift+D` are **`NotImplemented` and leave the key unconsumed**, and the document is byte-unchanged after the whole case. **If it fails** S1 for an edit; S2 for a navigation failure.

**A-36 · The Destinations pane lists and navigates** ☐
**Expect** Over `dests-named.pdf`: every resolvable destination listed with its name and page, sortable; the dangling one shown as unresolvable rather than omitted silently; activating one moves the view. **If it fails** S2.

**A-37 · The sidebar lists only panes that exist** ☐
**Goal** D4-2.
**Expect** The pane list holds exactly Thumbnails, Bookmarks and Destinations (plus the bottom log dock, which is a view of the application, not of the document); the seven remaining `Ctrl+Shift+*` chords are `NotImplemented` and leave the key alone; **no greyed or placeholder row appears**. **If it fails** S2 — Stage 2's I-11 shape.

**A-38 · Dock, float, auto-hide and peek still work with real content in them** ☐
**Expect** Stage 1's `SidebarDock` behaviours, re-asserted with a pane rather than a placeholder: dock left and right, float, auto-hide, the strip peeking on pointer arrival (Stage 1's I-37) and the strip disappearing while the pane is out (I-39). **If it fails** S2.

**A-39 · Two documents, two pane states** ☐
**Expect** Switching tabs shows the new document's thumbnails, bookmarks and destinations — not the previous document's, and not a rebuild of an identical pane. **If it fails** S2.

### 5.8 Tools (T7)

**A-40 · `H`, `V`, `Z` arm their tool from a key event, and the cursor follows** ☐
**Expect** Each chord dispatches and the controller reports the armed tool and its cursor. **The case states that `SelectTool` selects nothing** (Stage 5) — convention 19. **If it fails** S2.

**A-41 · The temporary hand modifier pans without disarming the tool** ☐
**Expect** Holding the modifier pans; releasing it restores the previously armed tool and its cursor, including when the modifier is released outside the widget. **If it fails** S2.

**A-42 · `Esc` cancels a drag and leaves the view where it was** ☐
**Expect** A marquee-zoom drag cancelled with `Esc` leaves the zoom and scroll exactly as before; **the control** — `Esc` with no drag in progress changes neither the tool nor the view. **If it fails** S2.

**A-43 · Marquee zoom zooms to the rectangle, and the loupe magnifies without moving the view** ☐
**Expect** The zoom after a marquee matches the dragged rectangle within a stated tolerance; the loupe changes no scroll position and no zoom. **If it fails** S2.

**A-44 · Auto-scroll runs at the configured speed, pauses and reverses** ☐
**Expect** `pageDisplay/autoScrollSpeed` changes the rate measurably; a pause stops it; a reverse reverses it; it stops at the end of the document rather than spinning. **If it fails** S3.

### 5.9 Rulers, grid, guides, snapping (T8)

**A-45 · The snap precedence is object → guide → grid** ☐
**Goal** D4-6, over synthetic geometry, exhaustively.
**Expect** With candidates of all three kinds inside the threshold, the object edge wins; with no object, the guide; with neither, the grid. **The case states that no product surface supplies object edges until Stage 6** (convention 19). **If it fails** S2.

**A-46 · One threshold, in device pixels, at every zoom** ☐
**Goal** The stage's exit criterion, made a number.
**Expect** At 50 % and at 400 %, a point the same number of **screen** pixels from a grid line snaps, and a point one pixel further **does not** — the control, and the half the criterion does not ask for (convention 22). **If it fails** S2.

**A-47 · Guides are created, moved, locked, deleted and cleared** ☐
**Expect** A guide dragged from a ruler lands at the pointer's page coordinate; a locked guide does not move; clear-all removes them; a guide's coordinate is unchanged by a zoom or a rotation of the view. **If it fails** S2.

**A-48 · Guides round-trip through `SessionState` and never through the file** ☐
**Goal** D4-7.
**Expect** Guides survive a close and a reopen in the same session and across a restore; **the document's bytes on disk are identical before and after** (SHA-256, read back from the path). **If it fails** S1 for a changed byte.

**A-49 · The rulers read out the cursor's page coordinates in the chosen unit** ☐
**Expect** `units/rulerUnit` changes the readout and the ruler's ticks; the readout at a known page point matches `core/Units.h`'s conversion; at 400 % the readout is the same page coordinate as at 50 %. **If it fails** S3.

**A-50 · The grid obeys spacing, subdivisions, colour and origin offset** ☐
**Expect** Each key changes the drawn grid; the offset moves the origin rather than the whole page; **the tiles are not re-rendered** when any of them changes (D4-11) — asserted on the scheduler's request count, which must not move. **If it fails** S2 for a re-render.

**A-51 · `Ctrl+U`, `Ctrl+'` and `Ctrl+;` dispatch from a key event and toggle what they name** ☐
**Expect** Rulers, grid and snap-to-grid each toggle, and the setting they write round-trips. **If it fails** S2.

**A-52 · Colour inversion and a custom page background are applied over the tiles** ☐
**Goal** D4-11's second half.
**Expect** Toggling inversion changes what is painted and **enqueues no render request**; the cache's entry count is unchanged; toggling back is exact. **If it fails** S2 — an inversion in the cache key doubles the cache for a toggle.

### 5.10 Splits (T9)

**A-53 · A split is two views over one document** ☐
**Expect** After a horizontal split: two `PageView`s, **one** `DocumentHandle`, one `CommandJournal`, one revision. **If it fails** S1 — a second handle is a second copy of the document and a second close protocol.

**A-54 · A mutation in one view appears in both** ☐
**Expect** `Ctrl+R` in one pane of a split rotates the page in both, at the same revision, geometry before pixels (Stage 1's I-36). **If it fails** S2.

**A-55 · Synchronized scrolling, and the spreadsheet freeze** ☐
**Expect** Sync on: both views scroll together; off: independently. The frozen row and column stay put while the body scrolls, and the frozen regions scroll **only** in their own axis. **If it fails** S2.

**A-56 · Removing a split leaves one view and no leaked tickets** ☐
**Expect** One view remains, the scheduler reports no outstanding tickets for the removed view, and no tile of the removed view is left in the cache keyed to a dead viewport. **If it fails** S2.

### 5.11 Full Screen and presentation (T10)

**A-57 · Every §8 Full Screen chord dispatches in the `FullScreen` context** ☐
**Expect** With the controller active, `activeContext()` is `FullScreen`; `Esc`, `F11`, the paging chords, `B`, `W`, `P`, `Ctrl+L`, `Ctrl+Shift+H` and `Ctrl+Shift+M` each dispatch; the Viewer's chords do **not** resolve there unless §8 lists them. **If it fails** S2.

**A-58 · `setPresentationModeActive` is called true on entry and FALSE on every exit** ☐
**Goal** D4-12, and the S1 a presentation most plausibly ships.
**Steps** A `platform.`/`ui.` case over a fake `IPowerSession`: normal exit, `Esc`, `F11`, window close, a topology change, and an exception on the way out.
**Expect** Exactly one `true` per entry and exactly one `false` per exit, **on every path**; the fake's final state is inactive. **If it fails** S1 — a machine that cannot lock.

**A-59 · The monitor comes from `IDisplayTopology`, and the join is by `friendlyName`** ☐
**Expect** `fullScreen/monitor` selects a monitor by a stable key; the `QScreen` join uses `friendlyName` and never `id` (Stage 2's **I-20**); an unknown or absent monitor falls back to the primary with one log line. **If it fails** S2.

**A-60 · A topology change moves the presentation rather than losing it** ☐
**Steps** Drive `IDisplayTopology::subscribe`'s handler with a fake arrangement in which the presenting monitor is gone.
**Expect** The presentation reappears on the primary monitor, still presenting, at the same page. **If it fails** S1 — a presentation on a monitor that does not exist is unreachable.

**A-61 · Transitions, auto-advance, loop and blank** ☐
**Expect** Over `transitions.pdf`: a per-page `/Trans` overrides `fullScreen/transition`; auto-advance steps at `fullScreen/autoAdvanceSec` (measured, not assumed); `loop` restarts and off stops at the last page; `B` and `W` blank and restore the exact page. **If it fails** S2; S3 for a transition that is merely ugly.

**A-62 · The cursor and the laser pointer obey their settings** ☐
**Expect** `fullScreen/showCursor` hides and shows it; `Ctrl+L` toggles at runtime; `P` toggles the laser and leaves the page unchanged. **If it fails** S3.

**A-63 · A presentation changes no document** ☐
**Expect** After a full presentation pass over `transitions.pdf` including blanks and the laser, the file's SHA-256 read back from disk is unchanged and the journal is empty. **If it fails** S1.

### 5.12 Window arrangement (T11)

**A-64 · Tile and cascade over synthetic screen geometry** ☐
**Expect** Tiling N windows fills the work area with no overlap and no window smaller than a stated minimum; cascade steps by the title-bar height and keeps every title bar on-screen. **The control:** arranging with one window changes nothing. **If it fails** S2.

**A-65 · Merge windows re-docks every detached tab, in order** ☐
**Expect** Three windows with five documents become one window with five tabs in their original order, each keeping its handle, journal and scroll position. **If it fails** S2.

**A-66 · The window list and `window.new`** ☐
**Expect** The list names every window and switching activates it; `window.new` opens a **second view of the same document** in a new window with its own handle (D4-13's exception), and both views stay correct after a rotation in one. **If it fails** S2.

### 5.13 Fit Visible, Document Properties, reading mode (T12)

**A-67 · The content box is the union of the page's objects, and it is cached** ☐
**Steps** `tst_contentbox` over `cropbox-margins.pdf`, `text-a4.pdf` and an empty page.
**Expect** The content box is inside the CropBox and tight around the ink; a page with no objects falls back to the CropBox; asking twice calls the engine once. **If it fails** S2.

**A-68 · `Ctrl+3` produces the CONTENT's zoom — Stage 2's I-28, closed** ☐
**Goal** Rule 1, on the case that taught it.
**Expect** Over `cropbox-margins.pdf`, Fit Page, Fit Visible and Actual Size give **three different zoom factors**, and Fit Visible's is the one that makes the content box fill the viewport. **The assertion is on the zoom, never on `fitMode()`** — the old case asserted the mode and was green for a month. **The control:** on a page whose content fills its CropBox, Fit Visible and Fit Page agree, which is correct and is why the corpus file had to be built. **If it fails** S2, and I-28 stays open in Stage 2's guide.

**A-69 · Document Properties shows what it read, and edits nothing** ☐
**Expect** `Ctrl+D` opens the dialog; every Info field, the version, the page count, the current page's size, the file size and path, and whether the document was repaired or is encrypted; **no field is editable and the dialog constructs no command**; the file's hash is unchanged after opening and closing it. **If it fails** S1 for a change; S3 for a wrong figure.

**A-70 · Reading mode hides the chrome and restores it exactly** ☐
**Expect** `Ctrl+L` hides the ribbon and panes and keeps the window chrome; a second press restores **the same** panes, widths and dock sides. **If it fails** S3.

### 5.14 Settings, pages, and everything before

**A-71 · The fifteen keys round-trip, and the two pages ship** ☐
**Expect** Each of the fifteen writes and reads back through `settings.ini` with its declared default and, where ranged, its clamp; **Preferences shows eight of 26 groups**, Units & Guides and Full Screen generated with no widget code (convention 17); `--lang zh-TW --dump-ui-strings` resolves both page names and every row label. **The control:** `pageDisplay/continuousScroll` and `units/snapToObjects` **do not exist** (D4-5), and a case that reads them fails to compile. **If it fails** S2.

**A-72 · The Stage 4 workload, re-measured through the shipped panes** ☐
**Goal** D4-16's second reading — the bench is a request pattern, the product is the product (D2-1's lesson, where the probe said 31× and the product said 1.18×).
**Steps** `rawpage_bench_openclose_ui.exe`-style composition, or the product under `--stage4-bench`, with a real thumbnails pane and a real split over `pages-900.pdf`.
**Expect** A viewport p95 beside T2's figure, and **the verdict either confirmed or corrected in `Tech-Stack-Plan.md` §3.3 in the product's words**. **If it fails** S2 — a bench figure quoted as the product's is the exact error D2-1 exists to prevent.

**A-73 · The two census cases, and `--list-actions`** ☐
**Expect** `rawpage.exe --list-actions` reports the new registered/unregistered split; `tst_viewercommands`' derived census and `tst_context`'s agree with it. **The thirteen Viewer rows that were unregistered at Stage 3's sign-off** — `view.readingMode`, `view.togglePropertiesBar`, `view.reflow`, `view.toggleRulers`, `view.toggleGrid`, `view.toggleSnapToGrid`, `nav.nextComment`, `nav.prevComment`, `nav.nextDifference`, `nav.prevDifference`, `tool.snapshot`, `compare.documents`, `bookmark.new` (read from `--list-actions`, 2026-09-18) — **lose the four this stage registers, and every one that remains belongs to Stage 5, 6 or 7 by name**. Both census numbers are read from the run rather than predicted here. **If it fails** S3, corrected in the same change.

**A-74 · Every prior stage's case is still present, by name, and green** ☐
**Steps** `ctest --preset x64-release -N` against the Stage 3 sign-off list; then the full run on both presets and the `gates` preset.
**Expect** Every Stage 0–3 test name present or its rename named; all green, one known skip; the **golden suite green with its baselines byte-unchanged** — Stage 4 changes no rasterization, so a moved baseline is a finding (D2-3). **If it fails** S1 for a regression; S2 for a silently renamed case.

### 5.15 Session restore through the product's own composition (T13)

**A-75 · Quit and relaunch brings back the documents, the order and the layout** ☐
**Goal** D4-9 end to end, through `CompositionRoot` — `tst_restore` in `rawpage_composition_tests`, the only target that links the engine, the widgets and the OS bundle together.
**Steps** Open three documents, split one, open a pane on another, arrange the windows, quit cleanly, relaunch.
**Expect** The same three documents, in the same tab order, with the same active tab, the same split and the same pane, and each at the scroll position and layout mode it had. **If it fails** S2; S1 if a document is lost.

**A-76 · A document closed with *Don't Save* comes back CLEAN** ☐
**Goal** D4-9's control — the clause that separates restore from recovery.
**Expect** After rotate → close with *Don't Save* → quit → relaunch: the document reopens, is **not dirty**, has no rotation, and its recovery directory is gone (Stage 3 D3-14 removed it on the Discard). **If it fails** **S1** — a resurrected edit is the product telling the user their decision did not count.

**A-77 · Restore runs after recovery, and never opens the same document twice** ☐
**Steps** Plant a crashed session's journal for a document that is also in the saved session (the kill probe writes one — Stage 3 A-74); relaunch.
**Expect** `RecoveryPrompt` offers it first; whatever it opens, restore **skips that path**; exactly one window holds that document. **The control:** with no crashed journal, restore opens it as usual. **If it fails** S1 for two copies of one document, which is two journals over one file.

**A-78 · The three memory switches** ☐
**Expect** `general/restoreSession` off restores nothing and captures nothing new; `general/rememberWindowLayout` off restores documents at default geometry; `general/rememberPaneLayout` off restores documents with no panes. Each is asserted **on what came back**, not on the setting. **If it fails** S2.

**A-79 · A test composition writes its own session file, never the user's** ☐
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

---

**M-01 · Two monitors, two scale factors, one document** ☐
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

**M-11 · Fit Visible, beside Fit Page** ☐
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

Numbered `I-nn`, **this stage's own namespace**. Empty: nothing has been run or read yet. The first
entry will carry its mechanism, why the suite missed it, and the control that was run red before the
fix — the shape every earlier stage's log uses.

| # | Sev | Title | State |
|---|---|---|---|
| — | — | *(none yet)* | — |

---

## 9. Sign-off criteria

Stage 4 is done when **all** of these hold — [`Stage4-ViewingSurface-Plan.md` §13](./Stage4-ViewingSurface-Plan.md#13-definition-of-done-for-stage-4)
restated as a checklist.

1. ☐ A-01 … A-79 **run**, with actual output recorded against each case — not a summary claiming they
   were (Stage 1's I-32). Linux-only halves (A-04) are recorded from the CI job's **log**, not its
   tick (rule 3).
2. ☐ M-01 … M-12 **walked and signed off by you**. I do not mark a manual case. A case not run for
   want of hardware (M-12's touch half) is recorded as such, never as passed.
3. ☐ `checks` clean — four gates, and **`mutation-lint` reporting one command** (A-06, A-07).
4. ☐ `ctest` green across all six Windows targets and the `gates` preset, every prior stage's case
   present by name (A-74).
5. ☐ `portable-asan` clean including `SessionState`, the snap resolver and the three readers;
   `portable-fuzz` and the soak unchanged and clean on three targets.
6. ☐ **Golden suite green with its baselines byte-unchanged** — this stage rasterizes nothing new.
7. ☐ **The A2-at-Stage-4 verdict is recorded**, bench and product, in `Tech-Stack-Plan.md` §3.3,
   graded on §3's scale **fixed before the measurement**, and `WinAcceleratedSurface`'s FileMap row
   says what was measured rather than what would decide it.
8. ☐ **Stage 2's I-28 is closed in Stage 2's guide**, with the measurement, and `Ctrl+3` is asserted
   on a zoom (A-68, M-11).
9. ☐ No open S1 or S2 in §8.
10. ☐ `Architecture-FileMap.md` rows flipped and the plan's §10 debt paid;
    `Implementation-Plan.md`'s Stage 4 status block updated to reality, deviations included.
