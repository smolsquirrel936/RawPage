# Stage 4 — Viewing and Navigation Surface · Testing Guide

> **Authoritative verification document for Stage 4.** Written from
> [`Stage4-ViewingSurface-Plan.md`](./Stage4-ViewingSurface-Plan.md) and
> [`Implementation-Plan.md` § Stage 4](./Implementation-Plan.md#stage-4--viewing-and-navigation-surface),
> **before any Stage 4 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** 🟡 **Written 2026-09-18, the same day as the plan and the day Stage 3 was signed off;
> partly run.** **79 automated** (A-01 … A-79) and **12 manual** (M-01 … M-12); **33 of the 79 carry
> a result** as of 2026-09-21 — T1's corpus cases, T2's four A2 cases, T3's seven session cases,
> T4's four reader cases, T5's seven pane cases, T6's two plus the two re-reads T5 owed, and
> **T7's five tool cases**. **No manual case has been walked.** §8 holds six issues: **I-01 and
> I-05 open, both S3**; I-02, I-03, I-04 and I-06 closed.
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

**A-29 · Page labels, and the page box that reads them** ☐
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

Numbered `I-nn`, **this stage's own namespace**. Each entry carries its mechanism, why the suite
missed it, and the control that was run red before the fix — the shape every earlier stage's log
uses.

| # | Sev | Title | State |
|---|---|---|---|
| **I-01** | **S3** | **Guides are keyed by `DocumentId`, and a save changes the `DocumentId`** | ⚪ **Open — owned by D4-7, revisited at T13** |
| **I-02** | **S4** | **Seven new Catch2 case names carried a COMMA, so none could be selected by name** | ✅ **Closed 2026-09-19 at T4**, by renaming them |
| **I-03** | **S3** | **A Qt 6.5+ API in `core/` broke both Linux jobs: the portable subset builds against Ubuntu's Qt 6.4** | ✅ **Closed 2026-09-19**, and written up as **convention 24** |
| **I-04** | **S2** | **A pane swallowed `Alt+↑`/`Alt+↓` and moved the selection — a Stage 7 chord doing something plausible and wrong** | ✅ **Closed 2026-09-20 at T5** (Thumbnails) **and again 2026-09-21 at T6** (Bookmarks): one mechanism, two panes, two layers of Qt |
| **I-05** | **S3** | **A destination's FIT is read, carried and not applied — a `/XYZ` point lands at its page's top** | ⚪ **Open — the arithmetic is T12's (D4-14), and this task did not guess it** |
| **I-06** | **S2** | **Registering `edit.cancel` — §8's `Common` row — swallowed `Esc` application-wide, including the tool finder's mouse-grabbing popup** | ✅ **Closed 2026-09-21 at T7**: the id is left unregistered and `Esc` is handled where the drag is |

**I-01 · Guides are keyed by an identity that the first save invalidates.** *(Raised 2026-09-19 at T3, while implementing D4-7 rather than by a failing case — which is itself the finding: no case in §5.5 would have caught it, because a round trip through one `SessionState` never changes a document.)*

**Mechanism.** D4-7 says guides *"live in `SessionState`, keyed by `DocumentId`"*, and justifies that key with *"a renamed file keeps its guides and a different file at the same path does not inherit them"*. `core/doc/DocumentId` (D3-16) is derived from **four** facts — the normalized path, the size, the modification time and the SHA-256 of the content — so:

- a **rename** changes the path and therefore the identity, so the renamed file does **not** keep its guides; and
- a **save** changes the size, the modification time and the hash, so the first save after a guide is placed leaves that guide keyed to an identity no longer on disk.

The second is the one that matters in use: rotate a page, press `Ctrl+S`, and the guides are orphaned. Only the *"a different file at the same path does not inherit them"* half of D4-7's reasoning actually follows from the type.

**Why the suite did not catch it.** Every A-18 assertion is a round trip through one object with one document identity; nothing in §5.5 saves a document, because T3 has no document to save. The case that would catch it belongs where capture and restore meet the shell.

**What is absent, and who owns it.** There is no re-keying of a session entry when a document is saved, and this task did not invent one: the repair is a decision for **D4-7**, taken where the mechanism is wired — **T13**. `SessionState` stores what the decision says today. Recorded here rather than in a commit message because the log outlives the fix.

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

**What is absent, and who owns it.** There is no conversion from a destination's PDF-space point to a scroll offset, and **this task did not invent one**. It needs the page's slot rectangle in content space and the resolution that produced it — the arithmetic **T12** builds for Fit Visible over the content box (D4-14) — so the repair belongs there, beside the other place in this stage that turns a page-space rectangle into a zoom and a scroll. A second conversion written here would be a second reader to keep in agreement, and a wrong one scrolls the reader silently to the wrong place. The bound is stated at both panes' headers and asserted as a case, so it is visible from the code as well as from this log.

**I-06 · A `Common`-scoped registration took a key that four unbuilt contexts and one widget needed.** *(Raised and closed 2026-09-21 at T7, by an EXISTING case in another suite rather than by anything T7 wrote.)*

**Mechanism.** T7's first version registered `edit.cancel` so that `Esc` could cancel a tool's drag, with an `enable` predicate that answered false when there was nothing to cancel — which looked like the careful thing to do (convention 16). It is not, because of how the five outcomes are defined: `ShortcutBinder::consumes` counts **both** `Dispatched` and `Unavailable` as consuming the key. A registered-but-disabled command therefore eats the keystroke just as thoroughly as a working one; the only outcome that leaves a key alone is `NotImplemented`, which means *nothing is registered at all*.

`edit.cancel` is §8's **Common** row — it applies in every context unless one overrides it. So one registration in the viewer took `Esc` everywhere: from `ui/shell/ToolFinder`'s result popup, and in due course from `find.close` (S5), `form.revertField` (S9) and `present.exit` (T10), none of which exist yet.

**How it surfaced.** `tst_toolfinder`'s *the result list can always be put away* went red — a Stage 1 case written for **I-44**, which is the defect where that popup could not be dismissed. The popup is a `Qt::Popup` holding an application-wide mouse grab, so an `Esc` that does not reach it is an application answering no click anywhere. The case had been green for a year and caught this on the first run after the registration.

**Why T7's own cases did not.** They could not: every one of them presses `Esc` with the viewer focused, where the handler would have run. The failure is in every OTHER context, and only a case belonging to one of those contexts could see it. That is convention 22 from an unusual angle — the control that mattered was not written for this mechanism at all, and would not have been thought of.

**The repair, and the rule it leaves.** `edit.cancel` stays unregistered; `Esc` is handled in `PageView::keyPressEvent`, which the key reaches precisely because nothing consumed it. A drag is not a command and abandoning one is not either — no more than a mouse-move is — so this is where it belongs anyway. **The rule for whoever registers `edit.cancel` one day:** a `Common` row needs an owner that knows every context that overrides it, and registering one to serve a single context is over-claiming even when the handler is careful.

**Why the suite did not catch it.** It structurally cannot. Every local preset and the Windows job use one Qt; the only build that uses the other is on Linux, on a runner, after the push. 965/965 on both Windows presets and four clean gates said nothing about it.

**What changed beyond the fix.** The lesson is not about one function, so it is written where it binds later work: **convention 24** (`Implementation-Plan.md` §1.5) — the three portable layers may use only Qt APIs the distro Qt has, and the way to settle a doubt is to read `\since` in the pinned Qt's header. The issue log keeps the incident; the convention keeps the rule.

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
