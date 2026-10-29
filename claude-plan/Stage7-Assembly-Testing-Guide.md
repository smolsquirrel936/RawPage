# Stage 7 — Document Assembly · Testing Guide

> **Authoritative verification document for Stage 7.** Written from
> [`Stage7-Assembly-Plan.md`](./Stage7-Assembly-Plan.md) and
> [`Implementation-Plan.md` § Stage 7](./Implementation-Plan.md#stage-7--document-assembly),
> **before any Stage 7 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** ⚪ **Written 2026-10-28 from the plan's draft, the day Stage 6 was signed off, and before any
> Stage 7 code. The plan's §0 was ANSWERED 2026-10-29, all five, every case re-read against its answer: Q1
> ANSWERED, approved as proposed — every case written
> against it re-read and standing as written (A-08, A-47 … A-51, §2's untitled-tab row, §6's note, §7's
> caveat); Q2 ANSWERED the same day, approved as proposed — A-08, §1's two layer rows and §4's layers
> row re-read and standing as written, A-08's command count at 43; Q3 ANSWERED the same day, approved — §9 applied
> twice, T1 … T13 as Stage 7a; and the same day, outside the five, D7-25 brought Save As's rewrite into the stage as
> T6, which renumbered T6 … T18 as T7 … T19 throughout, added §3.9, A-122 … A-127 and M-13's Save As steps; Q4
> ANSWERED the same day, approved — §3.7 and A-35 … A-37 stand, their *struck if declined* clauses removed; Q5
> ANSWERED the same day, all five parts approved — §1's five out-of-scope rows, A-80, A-96 and §6's note stand
> as written.** **T1 done 2026-10-29: A-13 … A-16 carry a result, all ✅; no other case has one.** It raised
> **I-01 (S2), closed inside T1** — the engine facade showed a page that inherits its boxes at US Letter,
> uncropped, and refused its document for editing (§8). **P-3, a duplex printer for M-04, is open.** **Cases
> written against a §0 proposal name it in their heading or row** (Q*n* ✅); none was declined, so none moved
> or was struck. **127
> automated** (A-01 … A-127) and **14 manual** (M-01 … M-14).
>
> **Two halves, per [`CLAUDE.md`](../CLAUDE.md): [§5 Automated](#5-automated-verification--i-run-this)**
> — I run it and report the actual output — and **[§6 Manual](#6-manual-verification--you-run-this)** —
> you walk it on real hardware, and **I never mark a manual case passed on your behalf.**
>
> **What the manual half is for at this stage.** Almost everything Stage 7 does is arithmetic a test
> can reach: which page a reference lands on is read from the page itself (plan D7-17), what a copy
> draws is compared with its source pixel for pixel, and a removed decoration is compared with the
> original byte for byte. Three questions are left that no assertion here can answer: **whether
> Acrobat and Firefox follow our bookmarks, links and labels the way RawPage does**; **whether a
> booklet is right when it is printed and folded**; and **whether moving pages by hand feels like
> moving pages**. **Anything in §6 that could have been automated is a defect in this guide — say so and
> I will move it.**
>
> **Label scoping.** `A-nn`, `M-nn` and `I-nn` here are **this guide's**; Stages 0 to 6 have their own,
> and this guide cites theirs with the stage named — *"Stage 6's I-06"*. `A2` and `A4` (no hyphen) are
> **risks** in `Tech-Stack-Plan.md` §3.3. `T1`…`T19` are **Stage 7** tasks, `D7-n` Stage 7 decisions and
> `Q1`…`Q5` the plan's open questions, all in `Stage7-Assembly-Plan.md` (§6, §3 and §0).

---

## 1. Scope

### In scope — must pass for sign-off

- **Every page command repairs what points at the pages it touches** — graded by what the target page
  shows, for every reference kind (plan D7-2, D7-17, D7-18).
- **The product keeps showing the right page after a page moves**: tiles, thumbnails, text, comments,
  hits, the current page and every selection follow it (D7-3), and **what the screen shows is what
  Save writes** (D6-2's invariant through page commands).
- **Pages copied from another document draw as they drew there** (D7-4), and **a new document opens
  everywhere** (D7-5, Q1 ✅).
- **The Page Thumbnails and Bookmarks pane tables of `Features.md` §8 work from the keyboard**, with
  `Ctrl+B` in the Viewer and `Ctrl+Shift+L`, `Ctrl+Shift+E` globally.
- **Merge preserves bookmarks, links, comments, fields and layers**, and says what it skipped (D7-16).
- **Headers, footers, Bates numbers and watermarks apply, update and remove exactly** (D7-7).
- **Imposition is correct on paper** (D7-8, M-04).
- **The tag tree can be viewed, retyped and reordered** (D7-10, T18), and **layers viewed and managed**
  as far as Q2 leaves them here (D7-9, Q2 ✅).
- **The A2-at-Stage-7 verdict is recorded**, and `DW-4`'s second cause (Q4 ✅).
- **Save As rewrites, compacts and loses nothing**, and Save a Copy leaves the tab where it was (D7-25).
- **No regression** in any Stage 0 to 6 case, on **both** presets.

### Out of scope — do NOT file these as bugs

Planned absences, itemized in [`Stage7-Assembly-Plan.md` §11](./Stage7-Assembly-Plan.md#11-explicitly-out-of-scope).

| Not a bug this stage | Arrives at |
|---|---|
| **There is no batch** — sequences, folders, watched folders | Stage 14 |
| **A merge refuses a Word, image or other non-PDF file** (Q5(a) ✅) | Stage 12 |
| **Paste makes no page from copied text or HTML** — RawPage pages and images only (Q5(d) ✅) | Stage 12 |
| **RawPage does not print**, and imposition is not offered at print time (Q5(b) ✅) | Stage 8 |
| **Bates numbering runs across open documents only** — not a file list or a folder (Q5(c) ✅) | Stage 14 |
| **Save As does not renumber densely, pack object streams, target a PDF version or linearize, and nothing exports to Word, Excel, images or HTML** — Save As rewrites and compacts from T6 (D7-25) | Stage 8 |
| **Layers cannot be flattened, a layer's content cannot be deleted or reassigned, and nothing is auto-tagged** (Q2 ✅) | Stage 10 |
| **An encrypted document cannot be edited, and is skipped as a merge input** (D7-21) | Stage 10 |
| **A signature is not validated after a page command** — RawPage warns; Acrobat judges (D7-20) | Stage 11 |
| **A link's JavaScript, form or media action does nothing when followed** — written and kept, never run (D7-12) | not planned |
| **A header, footer or watermark refuses Cyrillic, Greek, Hebrew, Arabic, Thai…** (Q5(e) ✅) | `DW-3` |
| **Pages inserted from another document arrive untagged** (D7-4) | not planned |
| **A page with hundreds of annotations is slow to draw** — `DW-4`'s first cause; Q4 took only the second | `DW-4` |
| **A page colour can be inverted and not substituted** — Stage 4's **I-07** | `DW-1` |

### What this evidence cannot tell us

- **Every automated claim is about our own generated corpus** (D7-17). Real documents from real
  producers are met only in the manual walks, on whatever files you bring to them.
- **"Another reader follows it" is judged by a person, in two readers** — Acrobat and Firefox (pdf.js).
  Chrome and Edge are PDFium and agree with RawPage by construction.
- **`ui/` is outside every sanitizer this project has** (O10). `portable-asan` and `fuzz_assembly` cover
  the page tree, the map, the copier, the assembler, the readers and the decoration and imposition
  arithmetic, and nothing about the panes, the tools or the dialogs.
- **PDFium is not a sanitized dependency.** A reload that hands PDFium a freed buffer would not be seen
  by ASan, which never runs PDFium.
- **Latency is measured; responsiveness is judged.** A-33 measures a move's time to the pixels and the
  viewport's p95; whether dragging thumbnails *feels* immediate is M-12's.
- **Paper is judged by a person, and only on the printer you print to** (P-3).

### Case anatomy

Every case is **ID · Goal · Steps · Expect · If it fails**, with a `☐` changed to ✅ or ❌. A case
either passes or yields a **specific** failure — "looks fine" is not a result.

### The rules this guide is written under

**Rule 1 — assert the EFFECT, not the intent** (Stage 2's I-28, convention 19). A command in the
journal is not a page moved in the file; a page moved in the file is not the right page on the screen.
Where a case can only reach the intent, it says so at the assertion and names what it therefore does not
cover.

**Rule 2 — a repaired case is re-walked from step 1** (Stage 1's I-45).

**Rule 3 — a green tick is not a clean run** (Stage 2's I-38). Linux-only halves — `portable-asan`,
`portable-fuzz`, `qpdf` — are read from the CI job's **log**, never from its tick.

**Rule 4 — a widget case drives layout itself, and asserts a non-zero visible count first**
(convention 12).

**Rule 5 — a new mechanism gets a control written from what must NOT happen** (convention 22). A move
must re-render no moved page; an undo before a save must write nothing; a removed decoration must leave
no byte behind; another producer's header must not be listed; a link's script must not run; a text
field must keep its own `Ctrl+X` and `Ctrl+V`.

**Rule 6 — the oracle for GEOMETRY is the rendered ink** (Stage 4's I-10). A crop, a resize, an
imposed cell, a link's rectangle and the order overlay are compared against the **raster**, located by
code that shares nothing with the transform that produced them.

**Rule 7 — the oracle for a PAGE'S IDENTITY is what the page shows** (plan D7-17, D7-18). A reference
is graded by the identity text PDFium extracts from the page it lands on, against
`corpus/labels/assembly-*.json` — never by an object number, an index or a page map our code computed.
A case comparing our page map with our page map proves only that it is deterministic.

**Rule 8 — the oracle for a NEW FILE is not our reader**: `qpdf --check`, PDFium opening it with no
repair, and Acrobat and Firefox by a person (D7-18).

**Rule 9 — both presets, and the UI binary whole** (Stage 4's I-09 and I-28).

### Severity

| Sev | Meaning |
|---|---|
| **S1** | **A page command leaves a reference pointing at the wrong page, or dangling.** **The screen shows another page's pixels or text** after a page command, or shows what the file does not contain (D6-2). **A page, a bookmark, a comment, a field value or a decoration is lost** on save, undo, redo, recovery, merge or extract. **The product changes a byte it did not mean to** — another producer's decoration, an unmodelled key, a signed range. **A file is overwritten without being asked**, or a link opens a program. A crash on a corpus file |
| **S2** | A documented behaviour is wrong in a way a later stage builds on — a copied page drawn differently, a new document another reader refuses, an imposed page in the wrong cell, a generator that misses a heading, a merge that renames what it should merge, a chord that cannot be pressed |
| **S3** | A message is unclear, a report misstates a count, a pane mislabels, a diagnostic names the wrong thing |
| **S4** | Polish nobody outside the team would notice |

**The S1 that is new at this stage is *the right content on the wrong page*.** Every earlier stage
could draw a page wrongly; this one can draw the right page in the wrong place — a bookmark that opens
page 7 where page 4 used to be — and nothing about the pixels looks broken.

---

## 2. Build, launch, environment

`cmake`, `ninja` and `g++` are **not on this machine's `PATH`**. Every command assumes
`scripts\env.ps1` has been dot-sourced first.

```powershell
. .\scripts\env.ps1
cmake --preset x64-release
cmake --build --preset x64-release --target checks      # four gates; mutation-lint names plan §4's commands
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure
ctest --preset x64-release -R "page|reference|copier|assembl|merge|outline|bookmark|link|decor|impos|layer|tag"
ctest --preset x64-release -R "reversibility|roundtrip|a4_product"
.\build\x64-release\bin\rawpage_bench_pipeline.exe --stage7 > bench-stage7.txt   # T3's A2 figure — to a FILE
.\scripts\run.ps1 tests\manual\M-01\pages-900.pdf      # a walk's launch: waits, so the log stays readable
.\scripts\run.ps1 -Preset x64-debug tests\manual\M-01\pages-900.pdf
```

**Notes carried from `CLAUDE.md`** because they still cost time: launch a walk with `scripts\run.ps1`,
never `Start-Process -NoNewWindow`; PowerShell's pipeline captures none of `rawpage.exe`'s output; a
running `rawpage.exe` blocks the release link; a failing run's output survives only until the next run,
in `build\<preset>\Testing\Temporary\LastTest.log`; a bench whose pipeline is cut short keeps running;
**a corpus PDF held open by a viewer stops `make-corpus.py` part-way** — close Acrobat before T1's
regeneration.

**Where things are written**, so a walker can look:

| What | Where |
|---|---|
| An edited document | Only where **Save** or **Save As** puts it — a walk saves into `tests\manual\<case>\` |
| A merge, an imposition, an extract to one file | **An untitled tab** — nothing on disk until you Save As (Q1 ✅) |
| A split, an extract to one file per page | The folder you choose — for a walk, `tests\manual\<case>\out\`; **an existing file is never overwritten** (D7-5) |
| Watermark, header/footer and imposition profiles | Beside the custom-stamp library, one kind each (plan D7-22) |
| Recovery journals and their side files | `%LOCALAPPDATA%\RawPage\recovery\<docid>-<pid>-<n>\` — an insert's copied pages are a side file (D7-6) |
| Settings | `%APPDATA%\RawPage\settings.ini` |
| A manual walk's own files | `tests\manual\<case>`, and nowhere else (`CLAUDE.md`) — a **copy** of the corpus file, never the corpus file itself |

### Settings keys that affect these cases

**Defaults are owned by `core/settings/SettingsKeys.def`** — **re-read the `.def` rather than this
table** (Stage 2's I-26). The fifteen rows are the roadmap's (plan D7-22); they appear on the
**Documents** page of Preferences.

| Key | Affects |
|---|---|
| `documents/defaultPageSize` | A-45, M-01 |
| `documents/cropRemoveWhiteMargins` | A-61, M-08 |
| `documents/splitMode`, `documents/splitValue` | A-49 |
| `documents/mergeBookmarkPerFile`, `documents/renameCollidingFields` | A-66, A-68, M-03 |
| `documents/watermarkProfile`, `documents/headerFooterProfile` | A-97, A-99, M-07, M-09 |
| `documents/batesStart`, `documents/batesDigits`, `documents/batesPrefix`, `documents/batesSuffix` | A-92, A-93, M-07 |
| `documents/impositionProfile` | A-102, M-04 |
| `documents/saveMode` | A-124 |
| `documents/autoDetectUrls`, `documents/linkAppearance` | A-84, A-86, M-06 |

### Test rig

| | |
|---|---|
| **Acrobat** | **P-1** ✅ Acrobat **26.2.22031** (read 2026-10-28) — M-02, M-03, M-06, M-07, M-09, M-10, M-13, M-14 |
| **A reader that is not PDFium** | **P-2** ✅ **Firefox 158.0.1** (pdf.js) — M-02, M-06. Chrome and Edge are **PDFium**: a control that should agree with RawPage |
| **A duplex printer and paper** | **P-3** ❌ **OPEN** — no physical printer on this machine (`Get-Printer`, 2026-10-28). M-04 cannot be walked until one is reachable; a printer on another machine with Acrobat serves |
| **`qpdf`** | **P-4** — CI's Ubuntu `portable-asan` job (Stage 6's), not this machine |
| **Monitors** | Stage 4's rig (150 % and 100 %) — M-01, M-08 |
| **Corpus** | **108 PDFs before T1** (2026-10-28); T1 adds plan §5.2's fixtures, every one generated by `corpus/make-assembly.py` with a `PROVENANCE.md` row and a label |

---

## 3. Verdicts this guide must produce

A stage that is green but has no verdict is not signed off. **Every scale below is fixed here, before
the measurement**, so nothing is graded against whatever it happened to achieve.

### 3.1 A2 at Stage 7

| Verdict | Assembled from | Recorded in |
|---|---|---|
| Does the frame budget hold while page commands remap and reload (D7-3), and does a move reach the pixels promptly? | **Bench (T3):** A-33. **Product (T19):** A-114. **Judgement:** M-12 | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), labelled **Stage 7's workload** |

Two quantities, both reported, both graded:

- **The viewport's p95 queued latency** — the same quantity as Stage 2 T18's product figure and
  Stages 4 to 6, so all are comparable.
- **The command-to-pixels latency** — from `CommandJournal::run` returning to the first tile of the
  moved range delivered **showing the right page's identity in its pixels** (rule 1, rule 7). Reported as
  p95 and maximum.

The workload: a `MovePages` of a ten-page range every 100 ms for 30 s on `pages-900.pdf` and on
`text-900.pdf` (a held `Alt+↓`), then one `InsertPages` of 100 pages from `text-900.pdf`, with a
viewport scrolling at Stage 4's D4-16 rate. `journal.run`'s p95, the remap time and the reload time
are reported and **not** graded here (§3.7 grades `journal.run`).

| Result | Means |
|---|---|
| **go** | On both files, the viewport's p95 stays **inside 16.667 ms** at the shipped cache cap, **and** the command-to-pixels p95 is **≤ 100 ms** with its maximum **≤ 250 ms** |
| **go, with named custom work** | A figure leaves its bound only under a named, reachable condition — a burst faster than one reload per frame, an insert large enough that its pages' first tiles queue behind the viewport — and a change in `core/render/` or `pdf/render/` brings it back. **The work is named in the verdict and done in the stage** |
| **no-go** | No scheduling change recovers it. **D7-3 changes** — a new decision recorded before T4, because every later surface moves pages through it |

### 3.2 References survive assembly (D7-2)

For **every page command**, applied to **every fixture with references** (plan §5.2), **every
reference** in `corpus/labels/assembly-*.json` is graded after the command, after save and reopen, and
after undo:

| The reference's target page | Pass bar |
|---|---|
| **Still in the document** | The page it lands on — resolved by PDFium in the saved file, reopened — **shows the identity the label names**. **0 failures** |
| **Deleted by the command** | Exactly D7-2's treatment for its kind (cleared, removed or unlinked), and listed in the command's report. **0 dangling references** — every reference in the saved file resolves to a page or to nothing |
| **Named by INDEX by the producer** (`refs-by-index.pdf`), or a `GoToR` | **Untouched**, byte for byte in value, and reported |
| **After undo** | The document's structural hash equals the original's (Stage 3's `tst_reversibility` measure) |

### 3.3 What is shown is what is saved, through page commands (D6-2)

For **every Stage 7 command**, run **through its surface** (not by constructing the command): every
page of the document rasterized from the live engine after the reload, and the same page from **the
saved file reopened in a fresh engine document**, at 72 and 144 dpi: **0 differing pixels**, and the
page count equal. After an undo, every page equals the original file's raster, **0 pixels**.

### 3.4 A copied page draws as it drew (D7-4)

Every page of every editable corpus file, copied by `ObjectGraphCopier` into an empty target and
written by the assembler (or inserted by `InsertPages`), rasterizes with annotations at 72 and 144 dpi
to **0 differing pixels** against the same page in its source — the same engine drawing the same
content must agree exactly. **A link whose target page was not copied keeps its border and loses its
action**, so it draws the same and is not an exception.

### 3.5 Merge at scale (D7-16)

A **30-way merge** of corpus files — `merge-a.pdf`, `merge-b.pdf` and `merge-c.pdf` ten times each,
with `pages-900.pdf` as the thirty-first input — on this machine, `x64-release`:

| Quantity | Bound |
|---|---|
| Wall time, dialog accepted to the untitled tab shown | **≤ 30 s** |
| Live-heap peak (Stage 4's measure, convention 23) | **≤ 1 GiB** |
| Cancel to stopped | **≤ 1 s**, and no tab and no file left behind |
| The UI thread during the merge | No gap between processed events **> 100 ms** (a progress bar that moves) |

### 3.6 Imposition on paper (D7-8)

**M-04**, by a person, on P-3: a booklet of `odd-count-17.pdf` printed duplex and folded reads
P-0001 … P-0017 in order with blank pages at the end, every page upright, the binding on the side
chosen; a 4-up sheet reads its cells in the order chosen. **Any page out of order or upside down is
S2.**

### 3.7 `DW-4`'s second cause (Q4 ✅)

`journal.run`'s p95 and maximum for **a one-page `MovePages`** and **a bookmark rename**, each after
**0, 300 and 3,000 unsaved copied objects** in the document's update (inserts from `text-900.pdf`):

| Result | Means |
|---|---|
| **taken** | **p95 ≤ 16.667 ms at every size**, and the p95 at 3,000 within **2 ms** of the p95 at 0 — the compose no longer grows with the unsaved update — **and** A-36's refusal still fires |
| **not taken** | Either bound missed: recorded with its figures, and `DW-4` is not split |


### 3.8 A decoration is removed exactly (D7-7)

For every decoration kind and every editable corpus file: **apply, save, remove, save** — the page
objects, their `/Contents` arrays and `/Resources` in the final revision **equal the original's, byte
for byte in value**, and every page rasterizes to **0 differing pixels** against the original. A
decoration written by **another producer** (`decorated-elsewhere.pdf`) is neither listed nor removed.

### 3.9 Save As loses nothing (D7-25)

For **every editable corpus file**, after a page command and an annotation, **Save As** (the rewrite):

| Quantity | Pass bar |
|---|---|
| Revisions in the output | **One** — one cross-reference section, no `/Prev` |
| Objects not reachable from the trailer | **0** — a first-party walk's reachable count equals the in-use entries |
| Every page, against the document before the save | **0 differing pixels** at 72 and 144 dpi |
| Every reference | Graded as §3.2 |
| Every surviving object's number and generation | **Unchanged** |
| Opening the output | PDFium with **no repair**; `qpdf --check` clean, read from the log |
| `pages-900.pdf` with 800 pages deleted | **Smaller** than the incremental save of the same edit and than the original — both sizes printed |

---

## 4. Traceability — exit criterion → case

| Exit criterion (`Implementation-Plan.md` § Stage 7) or plan decision | Cases |
|---|---|
| *Every §5.1 page operation works and repairs all references* | A-17 … A-28, A-38 … A-51, A-59 … A-65, §3.2, M-01, M-02 |
| *Merge preserves bookmarks/links/fields/layers* | A-66 … A-72, §3.5, M-03 |
| *Bookmark hierarchy editing works by drag and by chord* | A-73 … A-82, M-05 |
| *Watermarks and headers/footers apply, update, and remove cleanly* | A-91 … A-101, §3.8, M-07, M-09 |
| *Imposition output is correct on paper* | A-102 … A-104, §3.6, M-04 |
| *The tag tree can be viewed, retyped and reordered* | A-110 … A-113, M-11 |
| Layers (`Features.md` §5.6, as Q2 leaves them) | A-105 … A-109, M-10 |
| Links and destinations (`Features.md` §5.10) | A-82 … A-88, M-06 |
| A custom stamp from a PDF page (Stage 6's Q7) | A-89, A-90, M-14 |
| D7-3 — the remap; **A2 at Stage 7** | A-29 … A-34, A-114, §3.1, §3.3, M-12 |
| D7-4 — the copier | A-38 … A-46, §3.4 |
| D7-5 — new documents (Q1 ✅) | A-47 … A-51 |
| D7-20 — a signed document | A-27, M-13 |
| D7-25 — Save As that rewrites; the bookmark and tag-tree exports | A-122 … A-127, A-81, §3.9, M-13 |
| Q4 ✅ — `DW-4`'s second cause | A-35 … A-37, §3.7 |
| The roadmap's *"reversibility and round-trip for all commands"* | A-23, A-120, A-121 |
| No regression | A-01 … A-12, A-115 … A-119 |

---

## 5. Automated verification — I run this

Every case names its command and its pass condition. I report the **actual** output; a failing case is
reported as failing, with that output, and is never declared fixed without a re-run. **When a case is
ticked it carries the evidence it was ticked on** — Stage 1's I-32 was a guide whose summary claimed a
green its cases did not record. Every *"byte-unchanged"* diff below is taken against **`db09265`**,
the tree at Stage 6's sign-off. Each heading names the task that first runs the case; a case run again
by a later task says so in its result.

### 5.1 Build, boundary, environment

**A-01 · Configure and build, both presets, from clean** ☐ *T19*
**Steps** `cmake --preset x64-debug`, `--preset x64-release`; `cmake --build --preset <p> --clean-first` for each, the log kept.
**Expect** Exit 0 on all four; **0 `warning:` and 0 `error:` lines in either log**, `ui/` included. `--clean-first` is required (Stage 3's A-01). Object counts re-derived from the logs, not quoted. **If it fails** S1.

**A-02 · The `ui/` warning grace list is still empty** ☐ *T19*
**Steps** Read `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake`.
**Expect** Empty. **If it fails** S3, and the entry's reason is recorded in the status block.

**A-03 · The headless suite is still headless** ☐ *T2, T19*
**Goal** The page tree, the map, the generators, the imposition order, the token expansion, `LayerTree`, `TagTree` and every command's apply and revert are `core/`, and `core/` links no display and no engine.
**Steps** `ctest -R "gates.headless_assert"`; configure both presets.
**Expect** Every case as before, the negative controls still caught; `rawpage_tests` configures (`rawpage_assert_headless()` fails configure otherwise) with every Stage 7 `core/` file in it. **If it fails** S1.

**A-04 · `portable-asan` runs every Stage 7 suite in `core/`, `pdf/cos/` and `pdf/ops/`** ☐ *each task's push; whole at T19*
**Steps** The `portable-asan` CI job; **read the log, not the tick** (rule 3).
**Expect** Every case of `tst_pagetree`, `tst_pagemap`, `tst_reference_repair`'s headless half, `tst_copier`, `tst_assembler`, `tst_outline`, `tst_imposition`, `tst_headerfooter`, `tst_bates`, `tst_decoration`, `tst_layers`, `tst_tags`, `tst_linkactions` and every `[serialize:]`/`[revert:]` case present **by case name**; **0 `runtime error:`, 0 `ERROR: AddressSanitizer`, 0 `LeakSanitizer` lines**. **What it does not cover:** PDFium, the reload, and every `ui/` surface. **If it fails** S1 for a sanitizer finding; S2 if a suite is absent.

**A-05 · The product starts, and every new key resolves** ☐ *T19*
**Steps** `$env:QT_QPA_PLATFORM="offscreen"`; `rawpage.exe --dump-ui-strings`, `--lang zh-TW --dump-ui-strings` and `--list-actions`, **through `cmd` redirection**.
**Expect** Exit 0; every key of the families Stage 7 adds (`pages.`, `bookmarks.`, `layers.`, `tags.`, `decorate.`, `links.` and every minted `action.*`) resolves in `en` and `zh-TW` and **none resolves to itself**; the log says the backend is **4 of 7** and the platform **6 of 18** — Stage 7 adds no interface (plan §5.1). **If it fails** S2.

**A-06 · Stage 7's `core/` uses only the Qt the portable presets have** ☐ *each task's push*
**Goal** Convention 24: CI builds `core/` against Ubuntu's Qt **6.4**.
**Steps** The `portable-asan` job's build log.
**Expect** Every Stage 7 `core/doc/` file listed in its build, the job green — including the bookmark import's XML reader (`QXmlStreamReader`, QtCore). **If it fails** S2.

**A-07 · Every new target compiles C++20** ☐ *T5*
**Steps** Configure `x64-release`, then configure it again with `-DRAWPAGE_BUILD_FUZZERS=ON`; read `rawpage_assert_language_standard`'s `targets walked` line each time.
**Expect** Both configure; the second walks one target more than at Stage 6 — `fuzz_assembly`'s driver — and the counts are re-derived, not quoted (Stage 4's I-08). **If it fails** S2.

### 5.2 The gates — and the numbers that must and must not move

**A-08 · `checks` clean, and `mutation-lint` names exactly the plan's commands** ☐ *every task; whole at T19* *(Q1 ✅, Q2 ✅)*
**Goal** D7-1's mechanical proof.
**Steps** `cmake --build --preset x64-release --target checks`; `mutation-lint.exe --root . --explain`.
**Expect** Exit 0; four gates; `mutation-lint` names **exactly** the twelve commands of Stage 6's sign-off and plan §4's — **43** — re-derived from the plan's §4 after Q1 and Q2 (2026-10-29), and again if a later answer moves a command — each with its `Commands.def` row and its `[serialize:]` and `[revert:]` cases listed by `--explain`. **No `CreateLink`, `EditLink`, `DeleteLink`** (D7-12), and no `ExtractPages` or `MergeDocuments` (Q1 ✅). **If it fails** S1 for an unlisted command; S2 for a missing test.

**A-09 · `layer-lint`: no new waiver, no new edge** ☐ *every task; whole at T19*
**Steps** `layer-lint.exe --root . --explain`.
**Expect** Clean; **15 escapes**, the same fifteen; the extract, split, export and import paths write through `pdf/` (D7-5, D7-10). **The control:** a planted `QFile::open(WriteOnly)` in `ui/dialogs/PageOpsDialogs.cpp` is refused. **If it fails** S1 for an unwaived write; S2 for a new waiver.

**A-10 · `i18n-lint`: six new families, each a context of its own** ☐ *every task; whole at T19*
**Steps** `i18n-lint.exe --root . --explain`; `checks`.
**Expect** Clean, **0 literal waivers**; the families `pages`, `bookmarks`, `layers`, `tags`, `decorate`, `links` each in `en` and `zh-TW`; **`wrong-context` clean** — and no family spelled like a command or action id (Stage 5's I-08). **If it fails** S2.

**A-11 · `license-audit` unchanged** ☐ *T19*
**Steps** `checks`' `license-audit` line.
**Expect** Clean; **five** Qt modules, the same five; no new dependency in `vcpkg.json` (Stage 7 adds none). **If it fails** S1.

**A-12 · The fuzz count is six, and the assertion still fires** ☐ *T5*
**Steps** `ctest --preset gates -R fuzz_count`.
**Expect** Every `gates.fuzz_count.*` case green with `fuzz_assembly` counted; a **new** case where five of six fails configure, and the existing control (a helper standing in for a target) still caught. **If it fails** S2.

### 5.3 The corpus this stage is judged on (T1)

**A-13 · The corpus regenerates byte-identically, and nothing old moved** ✅ *2026-10-29, T1*
**Steps** `python corpus/make-corpus.py --verify`; `git diff --stat db09265 -- corpus/` excluding the new files.
**Expect** `--verify` reports every file byte-identical; **every file that existed at `db09265` is byte-unchanged**. **If it fails** S1 — the golden baselines and every earlier stage's labels stand on those bytes.
**Result** `--verify`: *"197 files regenerate byte-identically"* — the 154 of Stage 6's sign-off and 43 new: 22 files in `assembly/` (21 PDFs and `merge-not-a-pdf.pdf`) and 21 labels `labels/assembly-*.json`; the in-process rerun agreed. `git diff --stat db09265 -- corpus/`: three files, all tooling — `PROVENANCE.md`, `fixturecheck.py`, `make-corpus.py` — and **no data file**; every other change is an untracked new file.

**A-14 · Provenance both ways, and the self-test fires** ✅ *2026-10-29, T1*
**Steps** `python corpus/make-corpus.py --check-provenance`; `--self-test`.
**Expect** Every new file has a `PROVENANCE.md` row and every row a file; each self-test detection fires on its planted fault. **If it fails** S3.
**Result** `--check-provenance`: *"140 PROVENANCE.md rows, one per file; 43 labels and 14 comparison records name files that exist"* — a new `assembly/` section of 22 rows, each naming what the file carries and its control. `--self-test`: *"19 of 19 controls fired"*; the three new ones each matched to the file it damaged — a wrong identity for page 3 of `refs-every-kind`'s label (*"page 3 shows ['P-0004'], the label says P-0099"*), `/Resources` planted on `inherit-resources.pdf`'s first page, and `labels/assembly-layers.json` removed (`UNLABELLED assembly/layers.pdf`). `gates.corpus.self_test` now expects *19 of 19*.

**A-15 · Each fixture carries what its row claims — read from the bytes, and from the engine** ✅ *2026-10-29, T1*
**Goal** Rule 7's oracle must itself be true.
**Steps** `python corpus/make-corpus.py --check-faults` for the assembly fixtures; `ctest -R "assembly.labels"`, which opens each fixture in PDFium.
**Expect** Every page's identity text **extracted by PDFium** equals its label; every reference in the label exists in the file in the form its row names (outline item, both destination forms, `/Dest` by reference and by name, `GoToR`, label range, `/Pg`, bead, `/OpenAction`); `inherit-resources.pdf`'s pages carry **no** `/Resources` of their own; `layers.pdf`'s groups are as labelled. **The control:** a label naming a wrong identity for one page fails the case. **If it fails** S2.
**Result** **The bytes:** `--check-faults` *"every Stage 5, 6 and 7 fixture carries what its row claims"* — `fixturecheck.py`'s new `check_assembly` walks each file's page tree from the trailer and holds it to the label's page order, reads each page's identity from the strings its content shows, and checks every reference in its written form and at the page whose identity it names; per fixture, its feature (no `/Resources`, `/MediaBox` or `/CropBox` on an inheriting page and its ancestor path; each layer's lock, state, radio group, usage and `/Properties` entry; the heading twins' identical text; the blank pages' three kinds; the content box; another producer's decorations with no `/RawPageDecoration`). **A throwaway probe planted 24 faults, one per detection, and all 24 fired** — the one that first fired for another page's reason was re-planted on the page with no `/Contents` and fired for its own. **The engine:** `render.assembly labels: every page shows PDFium the identity its label gives` — all 21 labels, **114 pages**, each page's extracted identity tokens equal to the label's, and **its control** (page 0 relabelled `ZZ-9999`) reported as one mismatch; `render.assembly labels: the engine's outline and named destinations land on the pages labelled` — **20 outline items and 5 named destinations**, each resolved by PDFium to the page showing the identity its label names. **Measured, not graded:** PDFium resolves `refs-by-index.pdf`'s integer destination `[ 2 /Fit ]` to page index 2 (`X-0003`) — a lenient reading of a destination ISO 32000-1 makes legal only in remote form, which a repair must leave as written. 517 assertions over the three `[assembly]` cases.

**A-16 · Every new corpus file is a golden case or excluded with a reason** ✅ *2026-10-29, T1*
**Steps** `golden-update.exe --check-manifest`.
**Expect** Green both ways; `103` baselines before T1, each new file counted on one side. **If it fails** S3.
**Result** *"130 corpus files, 124 with baselines and 6 excluded with a reason"* — the 21 PDFs cases, `merge-not-a-pdf.pdf` excluded (`error.open.notPdf`, measured by `--survey`). **99 baselines** in one filtered capture (`--filter assembly/`), **every page looked at first** in a scratch golden root — which is how I-01 was found, and two fixtures were corrected (the merge sources' field rows overprinted the identity; `boxes.pdf`'s caption crossed its ArtBox) — captured AFTER I-01's fix, so no baseline holds the wrong page size; **no tracked PNG changed**.

### 5.4 Moving pages without breaking what points at them (T2)

**A-17 · The page tree's mutation set** ☐ *T2*
**Steps** `ctest -R "unit.pagetree"`.
**Expect** Insert, remove, move and duplicate of single pages and ranges at the start, middle and end; a move to its own position changes nothing and makes no command; an index outside the tree, an overlapping move and **a removal of every page** are refused, changing nothing (D7-21). **If it fails** S1.

**A-18 · The page map's arithmetic** ☐ *T2*
**Steps** `ctest -R "unit.pagemap"`.
**Expect** Each command's map against hand-written tables; a map composed with its inverse is the identity; two maps compose to the map of the two commands run in order; removed and inserted indices exact. **If it fails** S1.

**A-19 · Every reference lands on the page it named — by what that page shows** ☐ *T2; re-run at T5, T8, T9, T10, T11, T14* — §3.2
**Steps** `ctest -R "render.reference_repair"`: for each page command and each fixture with references, apply, save, reopen in a fresh engine document, resolve each labelled reference through PDFium, and extract the identity of the page it lands on.
**Expect** **0 failures** against §3.2's table. **The control (rule 5):** a planted repair that rewrites every `/Dest` to the page now at the OLD index — the error that looks like a repair — fails the case for every moved reference; a planted no-op `/PageLabels` rewrite fails the label rows. **If it fails** S1.

**A-20 · A reference to a deleted page is never left dangling** ☐ *T2*
**Steps** `ctest -R "reference_repair.deleted"` over `refs-every-kind.pdf`, deleting each page in turn.
**Expect** Each kind treated as D7-2 says — outline item kept with no destination, link kept with no action, named destination removed, structure content items removed, bead unlinked, `/OpenAction` removed — **every one listed in the command's report**; **every reference in the saved file resolves to a page or to nothing**; undo restores the structural hash. **If it fails** S1.

**A-21 · Page labels follow their pages** ☐ *T2*
**Steps** `ctest -R "reference_repair.labels"` over `refs-every-kind.pdf` and `outline-labels.pdf`.
**Expect** After a move, every moved page shows **the label it had**, read by PDFium (`FPDF_GetPageLabel`); an inserted or duplicated page takes the next label of the range it lands in, against hand-written tables. **If it fails** S1.

**A-22 · What names a page by index is reported, not "fixed"** ☐ *T2*
**Steps** `ctest -R "reference_repair.by_index"` over `refs-by-index.pdf` and the `GoToR` of `refs-every-kind.pdf`.
**Expect** Those references **byte-identical in value** after every command, and each in the report. **If it fails** S2.

**A-23 · Serialize, revert and reversibility for every page command** ☐ *T2; extended at T5, T8, T9*
**Steps** `ctest -R "serialize:|revert:|reversibility"` for `MovePagesCommand`, `DeletePagesCommand`, `DuplicatePagesCommand` (later tasks add theirs).
**Expect** Each command's line round-trips; apply then revert restores the structural hash **over every editable corpus file**; the journal line names pages by index (D7-24). **If it fails** S1.

**A-24 · The saved update holds exactly what changed** ☐ *T2*
**Steps** `ctest -R "pageops.update"`: move, save; read the update.
**Expect** The update holds **only** the page-tree nodes whose `/Kids` changed, the label tree when it changed, and the objects D7-2 repaired; the original is a **byte-identical prefix**; **a move and its undo before a save write nothing** (rule 5). **If it fails** S1.

**A-25 · `qpdf --check` over every file a page command saves** ☐ *T2; the log read at each push*
**Steps** The `portable-asan` job's `check-qpdf.py` step; **read the log** (rule 3).
**Expect** *"No syntax or stream encoding errors found"* for every Stage 7 output, each named; the file count re-derived. **If it fails** S2.

**A-26 · What is refused is refused by key** ☐ *T2*
**Steps** `ctest -R "pageops.refused"`.
**Expect** A page command on an encrypted document, one removing every page, and one naming a page whose two readings disagree are refused with their keys, the document untouched; a page command on a **repaired** document runs and its first save is Save-As. **If it fails** S2.

**A-27 · A signed document: warned once, and every signature still verifies** ☐ *T2 (OpenSSL); T8 (the warning)*
**Steps** `ctest -R "a4_product" -V` with a `MovePages` and a `DeletePages` through the product on each of the four `corpus/signed/` files; the warning case in `rawpage_ui_tests`.
**Expect** OpenSSL verifies **all four** after each save, each with one byte flipped first as its control (D3-2's order); the message bar warns **once** per document before its first page command, and **never** on an unsigned document. **If it fails** S1 for a signature broken by the bytes; S3 for the warning.

**A-28 · Recovery replays every Stage 7 command kind** ☐ *T2; extended by every task that adds a command*
**Steps** `ctest -R "platform.recovery.kill" -V` with each Stage 7 command in its rotation.
**Expect** After `TerminateProcess`, the replay against the file as opened gives a document whose every page shows the identity it showed before the kill and whose references grade as §3.2; an insert replays **with its source deleted** (D7-6). Where each kill landed is printed. **If it fails** S1.

### 5.5 How a moved page reaches the screen — A2 at Stage 7 (T3)

**A-29 · Shown is saved after every page command** ☐ *T3; re-run at T19 through the surfaces (A-119)* — §3.3
**Steps** `ctest -R "composition.assembly_shown"`.
**Expect** §3.3's bar: 0 differing pixels live against reopened, every page, 72 and 144 dpi, and under a 90° view rotation; after undo, 0 against the original. **If it fails** S1.

**A-30 · A moved page is not drawn again** ☐ *T3*
**Steps** `ctest -R "render.remap"` on `pages-900.pdf`: render a viewport over pages 10 … 19; move them after page 500; scroll to them.
**Expect** Every one of their tiles **delivered from the cache under its new index**, showing the identity it should (rule 7), and **0 rasterizations of those pages** counted at the engine. **The control:** a planted `Scope::wholeDocument()` for moves makes the count non-zero. **If it fails** S2 — and S1 if a tile shows another page's identity.

**A-31 · Every index-keyed consumer is told** ☐ *T3*
**Steps** `ctest -R "remap"` across `rawpage_tests`, `rawpage_render_tests` and `rawpage_ui_tests`.
**Expect** After a move: a find for a moved page's identity hits it at its NEW index **with 0 re-extractions** counted; `AnnotationStore` lists its annotations at the new index; the find bar's hit, the view's current page, a split pane's position, the view history (Back returns to the page, not the index) and **both panes' selections** follow it; a removed page leaves none of them pointing at its index. **The control:** each consumer's remap planted out in turn reddens its own case and no other. **If it fails** S1.

**A-32 · An unsaved rotation survives a move** ☐ *T3*
**Steps** `ctest -R "remap.rotation"`: rotate page 5 by 90 (unsaved); move it to 2; then undo both in turn.
**Expect** The page drawn turned once — not twice, not zero — at every step, its raster equal to the same page rotated and saved (D3-15's delta now against the revision the engine draws). **If it fails** S1.

**A-33 · A2 at Stage 7, the bench** ☐ *T3* — a **VERDICT**, §3.1
**Steps** `rawpage_bench_pipeline.exe --stage7 > bench-stage7.txt`, three runs; read the dpi and viewport it reports first (`CLAUDE.md` → *Traps*).
**Expect** §3.1's table decides; the figures recorded in `Tech-Stack-Plan.md` §3.3 as *Stage 7's workload*, beside the remap and reload times. **If it fails** — a *no-go* is a result, not a failure: D7-3 changes before T4.

**A-34 · A consumer that cannot remap redraws, never shows stale** ☐ *T3*
**Steps** `ctest -R "remap.fallback"`: a consumer registered without a remap.
**Expect** It is handed `WholeDocument` and redraws; **0 tiles of the wrong identity** at any point. **If it fails** S1.

### 5.6 The compose stops growing (T4) — Q4 ✅

**A-35 · `journal.run` does not grow with the unsaved update** ☐ *T4* — §3.7
**Steps** `ctest -R "journal.compose_growth" -V` (`[run-serial]`, convention 25).
**Expect** §3.7's bars at 0, 300 and 3,000 unsaved copied objects, for a one-page move and a bookmark rename. **If it fails** S2, and §3.7 records *not taken*.

**A-36 · A composition Save could not write is still refused** ☐ *T4*
**Steps** `ctest -R "journal.compose_refused"`: a planted compose whose verify fails.
**Expect** The command refused with `error.edit.notShown`, the model untouched, nothing queued to the engine — D6-2's rule unchanged by the verify moving off the UI thread. **If it fails** S1.

**A-37 · Commands and saves wait for a verify in flight** ☐ *T4*
**Steps** `ctest -R "journal.verify_order"`: three commands and a save issued while a verify runs.
**Expect** Shown and saved in issue order; the save writes the composition that verified; no command shown before the one issued earlier. **If it fails** S1.

### 5.7 Pages from another document (T5)

**A-38 · A copied page draws as it drew** ☐ *T5* — §3.4
**Steps** `ctest -R "render.copier"`.
**Expect** §3.4's bar over every page of every editable corpus file. **If it fails** S1.

**A-39 · What a source shares, the copy shares** ☐ *T5*
**Steps** `ctest -R "unit.copier.shared"` over `inherit-resources.pdf` and `pages-900.pdf`.
**Expect** Two pages sharing a font and an image, copied in one call: the font and the image written **once**, counted in the target; copying all 900 pages of `pages-900.pdf` writes no object twice. **If it fails** S2.

**A-40 · Inherited attributes are materialized** ☐ *T5*
**Steps** `ctest -R "copier.inherit"` over `inherit-resources.pdf` and `rotate-inherited.pdf`.
**Expect** Each copied page carries its own `/Resources`, `/MediaBox`, `/CropBox` and `/Rotate`, equal to the source walk's resolved values, and renders as §3.4. **The control:** a planted copier that copies the page dictionary alone fails §3.4 on those pages — and passes on `pages-900.pdf`, which is why that file is not the fixture. **If it fails** S1.

**A-41 · The copy stops at the page tree, and at its budget** ☐ *T5*
**Steps** `ctest -R "copier.bounds"`.
**Expect** No source page-tree node and no source catalog in the copied set; a graph past the object budget refused by key, the target untouched. **If it fails** S2.

**A-42 · Annotations come with their page, and only what can follow them** ☐ *T5*
**Steps** `ctest -R "copier.annots"` over `annots-on-pages.pdf`.
**Expect** A note and its pop-up, a reply and its parent, copied together, still name each other; a reply whose parent stayed behind loses `/IRT`; a link to a page copied in the same call points at the copy, **by the identity it shows**; a link to a page left behind keeps its rectangle with no action, and is reported; `/StructParent(s)` gone. **If it fails** S1.

**A-43 · An encrypted source is refused** ☐ *T5*
**Steps** `ctest -R "copier.encrypted"` over `corpus/encrypted/`.
**Expect** Refused by key, nothing copied. **If it fails** S2.

**A-44 · Insert and replace carry the copied graph, and recover without the source** ☐ *T5*
**Steps** `ctest -R "serialize:InsertPagesCommand|revert:InsertPagesCommand|serialize:ReplacePagesCommand|revert:ReplacePagesCommand"`; the A-28 rotation with the source deleted before the replay.
**Expect** A payload over 64 KiB is a side file named by its SHA-256 beside the journal, written by `RecoveryJournal`'s existing write (the waiver census unchanged, A-09); the replay equals the document before the kill. **If it fails** S1.

**A-45 · Blank pages of a chosen size** ☐ *T5*
**Steps** `ctest -R "pageops.blank"`.
**Expect** Inserted at the size given, or `documents/defaultPageSize` when none is, with no content stream (an empty `/Contents` or none), shown blank. **If it fails** S3.

**A-46 · `fuzz_assembly`, replayed here and run on Linux** ☐ *T5 (Windows replay, 60 s log); the soak's log at T19*
**Steps** `ctest -R "fuzz.assembly_replay"` (the body over the corpus and the adversarial inputs, headless); the `portable-fuzz` job's log; the soak's.
**Expect** No crash, no hang beyond the body's time bound; 60 s and ten minutes with no finding, read from the **logs**. **If it fails** S1 for a crash; each finding gets a regression case before it counts as fixed.

### 5.8 Save As that rewrites (T6) — D7-25

**A-122 · Save As writes one revision of what is reachable, and loses nothing** ☐ *T6; re-run at T19* — §3.9
**Steps** `ctest -R "fullwriter"` over every editable corpus file, each after a `MovePages` and an `AddAnnotation`; the `qpdf` step's log.
**Expect** §3.9's bars for every file. **The control (rule 5):** a planted writer that skips one reachable object type (`/XObject` resources) fails the raster bar on the image fixtures. **If it fails** S1.

**A-123 · What is unreachable goes, and the file shrinks** ☐ *T6*
**Steps** `ctest -R "fullwriter.compaction" -V`: delete 800 of `pages-900.pdf`'s pages; Save, and Save As, to two scratch files.
**Expect** The rewrite holds **no** object unreachable from its trailer (counted); it is smaller than both the incremental save and the original, all three sizes printed. **The control:** a planted writer that keeps every in-use entry reachable or not fails the count. **If it fails** S2.

**A-124 · Numbers kept: undo, identities and recovery survive a Save As** ☐ *T6*
**Steps** `ctest -R "fullwriter.identity"`: annotate, move pages, Save As; undo every step, redo every step; then the A-28 rotation with a kill after the Save As; then `documents/saveMode = full` and `Ctrl+S`.
**Expect** Every surviving object's number and generation equal before and after; each undo and redo after the Save As rasterizes as the same step did before it (0 px); every annotation identity resolves; the replay after the kill equals the document before it; with `full`, `Ctrl+S` rewrites as Save As does. **If it fails** S1.

**A-125 · Save a Copy leaves the tab where it was** ☐ *T6*
**Steps** `ctest -R "ui.save_a_copy"`.
**Expect** The copy written by the rewrite at the chosen path and passing A-122; the tab keeps its own path, title and unsaved state, and a later `Ctrl+S` writes to its own file. **If it fails** S2.

**A-126 · A signed document asks; an encrypted one is copied** ☐ *T6*
**Steps** `ctest -R "fullwriter.signed|fullwriter.encrypted|a4_product" -V` over the four `corpus/signed/` files and `corpus/encrypted/`; the question answered each way, and once with no user.
**Expect** *Keep the signatures*: copied and appended, and OpenSSL verifies all four, each with one byte flipped first as its control; *rewrite*: written, the question having named the signatures as becoming invalid; **no user: the signatures kept** (convention 16). Encrypted: copied and appended, the output's prefix byte-identical to the original. A **repaired** document's forced Save As now rewrites, and the output opens with **no repair**. **If it fails** S1.

### 5.9 New documents: extract and split (T7) — Q1 ✅

**A-47 · Every new document opens everywhere it can be checked here** ☐ *T7; re-run by T10 and T16*
**Steps** `ctest -R "assembler"`; the `qpdf` step's log.
**Expect** Each output: **one** revision (one `xref`, no `/Prev`), a fresh `/ID`, PDFium opens it with **no repair**, every page as planned and §3.4's bar against its source page; `qpdf --check` clean. **If it fails** S1.

**A-48 · Extract: to one, per page, and with delete** ☐ *T7*
**Steps** `ctest -R "extract"`.
**Expect** The right pages by identity in each output; *extract and delete* deletes **only after** the write verifies — **the control:** a planted write failure leaves the source document untouched and no file. **If it fails** S1.

**A-49 · The five splits, graded against the labels** ☐ *T7*
**Steps** `ctest -R "split"` over `split-blanks.pdf`, `refs-every-kind.pdf`, `tagged-headings.pdf` and `pages-900.pdf`.
**Expect** Every N; at every top-level bookmark; at a chosen level; at blank pages (by the **content box**, so the white-ink page is blank to the eye and **not** blank to the split — its label says which); at a text pattern — each output's first and last identities as labelled; **by size**: every output at or under the bound, except a single page larger than the bound, written alone and reported. **If it fails** S2.

**A-50 · Nothing is overwritten, and nothing lands outside the chosen folder** ☐ *T7*
**Steps** `ctest -R "split.collision"`: a scratch folder holding one of the names a split would write.
**Expect** The split refused before writing anything, the collision named; a directory listing of the scratch root before and after shows only the chosen folder changed. **If it fails** S1.

**A-51 · An assembled document opens as an untitled tab** ☐ *T7*
**Steps** `ctest -R "ui.untitled_assembly"`.
**Expect** A tab titled as untitled, its pages shown; `Ctrl+S` asks for a path (Save As); closing asks first; nothing on disk until saved. **If it fails** S2.

### 5.10 The thumbnails pane edits (T8)

**A-52 · The whole Page Thumbnails pane table, from key events** ☐ *T8*
**Steps** `ctest -R "ui.thumbnails_edit"`: for each row of `Features.md` §8's **Page Thumbnails pane** table, a `QKeyEvent` sent through `ShortcutBinder`.
**Expect** Each chord runs its command on the **selection** (`Ctrl+R` on a set, D7-13); `Delete`, `Alt+↑`/`Alt+↓`, `Ctrl+Shift+D` one undo step each; `Ctrl+Shift+X` opens extract with the selection filled in; `Ctrl+A` selects every page; the census rows in `tst_keybindings` compare §8 row for row. **If it fails** S2.

**A-53 · Selection is a set, and follows its pages** ☐ *T8*
**Steps** `ctest -R "thumbnails.selection"`.
**Expect** Click, `Shift`+click and `Ctrl`+click give the sets they give in a file manager; after a move the set is the same **pages** (D7-3). **If it fails** S2.

**A-54 · A drag reorders in one step, and copies across windows** ☐ *T8*
**Steps** `ctest -R "thumbnails.drag"`: drag a set within a pane; drag it into another window's pane of the same process.
**Expect** Within: one `MovePages`, one undo step, the drop position as indicated. Across: one `InsertPages` in the target, **the source unchanged**, the pages showing their identities in the target. **If it fails** S2.

**A-55 · Cut, copy and paste of pages — across processes** ☐ *T8 — the cross-process half on the `windows` platform, `[D7-13]`*
**Steps** `ctest -R "thumbnails.clipboard"` (in-process); `$env:QT_QPA_PLATFORM='windows'; rawpage_ui_tests.exe "[D7-13]"` with your leave — **it replaces the system clipboard's contents** (Stage 5's A-50).
**Expect** Copy puts `application/x-rawpage-pages` on the clipboard; paste in a **second process** inserts the pages, showing their identities; cut is copy then one `DeletePages`. **If it fails** S2.

**A-56 · Every text field keeps its own `Ctrl+X`, `Ctrl+C` and `Ctrl+V`** ☐ *T8*
**Steps** `ctest -R "thumbnails.clipboard_controls"`: the page box, the find bar, the tool finder and a bookmark's rename editor, each focused.
**Expect** Each edits its text, and no page is cut, copied or pasted (Stage 5's I-04 turned round, rule 5). **If it fails** S2.

**A-57 · The badges say what the page carries** ☐ *T8*
**Steps** `ctest -R "thumbnails.badges"` over `annots-on-pages.pdf` and `forms-acroform.pdf`.
**Expect** Comment, field and link badges exactly on the pages the labels name. **If it fails** S3.

**A-58 · Embedded thumbnails written and removed** ☐ *T8*
**Steps** `ctest -R "serialize:SetEmbeddedThumbnailsCommand|revert:SetEmbeddedThumbnailsCommand|thumbnails.embed"`.
**Expect** Each page's `/Thumb` an image PDFium reads back (`FPDFPage_GetThumbnailAsBitmap`) at the size written; removal leaves no `/Thumb`; reversible. **If it fails** S3.

**A-59 · Rotation over a set, a range, odd, even, or one orientation** ☐ *T8*
**Steps** `ctest -R "rotate.ranges"`; `ctest -R "serialize:RotatePageCommand"`.
**Expect** Each filter rotates exactly its pages; **a journal line Stage 3 wrote still replays** — the command's format grew without breaking its old lines. **If it fails** S2.

### 5.11 Boxes, crop, resize, renumber (T9)

**A-60 · The five boxes, written and read back** ☐ *T9*
**Steps** `ctest -R "pageops.boxes"`; `serialize:`/`revert:CropPagesCommand`.
**Expect** Each box set alone and together, read back by PDFium (`FPDFPage_Get*Box`) as written; a box outside the MediaBox clipped to it and reported; *undo a crop* removes `/CropBox`. **If it fails** S2.

**A-61 · White margins are removed to the CONTENT box** ☐ *T9*
**Steps** `ctest -R "crop.white_margins"` over `boxes.pdf` and `cropbox-margins.pdf`.
**Expect** The CropBox equals Stage 4's content box (`IDocumentBackend::contentBox`) padded by `documents/cropRemoveWhiteMargins`, and the cropped page's **ink** (rule 6) touches each edge within the padding ± 1 device pixel at 144 dpi; *all pages of the same size* crops exactly those pages. **If it fails** S2.

**A-62 · A resized page's content is scaled and placed as asked — on the ink** ☐ *T9*
**Steps** `ctest -R "resize"` over `text-letter.pdf` → A4 and `mixed-page-sizes.pdf`.
**Expect** With scaling: the ink's bounding box scaled by the ratio asked and centred (or anchored as asked) within ± 1 device pixel at 144 dpi; without scaling: the boxes alone change and the ink does not move; the wrapper streams marked (D7-7), and undo byte-identical. **If it fails** S2.

**A-63 · Renumbering pages** ☐ *T9*
**Steps** `ctest -R "renumber"`; `serialize:`/`revert:RenumberPagesCommand`.
**Expect** Each style (arabic, roman upper and lower, letters upper and lower), a prefix and a start, per range, read back by `FPDF_GetPageLabel` against hand-written tables; ranges outside the selection unchanged. **If it fails** S2.

**A-64 · The crop tool's box is where it was dragged** ☐ *T9*
**Steps** `ctest -R "ui.croptool"`, rendering into an image at 1.5 at an odd logical offset (Stage 5's A-123).
**Expect** The written CropBox, drawn back, coincides with the dragged rectangle on the **raster** within 1 device pixel; snapping as `core/Snap.h` resolves it; `Esc` cancels with no command. **If it fails** S2.

**A-65 · Crop through the product, saved, undone** ☐ *T9*
**Steps** `ctest -R "composition.assembly_shown"` with a crop and a resize in its rotation.
**Expect** §3.3's bar. **If it fails** S1.

### 5.12 Merge (T10)

**A-66 · A bookmark per file, the sources' outlines beneath, landing on the right pages** ☐ *T10*
**Steps** `ctest -R "merge.outline"` over `merge-a`, `merge-b`, `merge-c`, with `documents/mergeBookmarkPerFile` each way.
**Expect** One top-level bookmark per input named from its file name or `/Title` as set, each landing on that input's first page **by the identity it shows**; every source bookmark beneath its file's, landing as §3.2 grades. **If it fails** S1.

**A-67 · Links and comments come with their pages** ☐ *T10*
**Steps** `ctest -R "merge.links"`.
**Expect** A link between two pages of one source lands on the copy **by identity**; every comment of every input present on its page, its thread intact. **If it fails** S1.

**A-68 · Colliding fields: renamed, or merged on purpose, and reported** ☐ *T10*
**Steps** `ctest -R "merge.fields"` with `documents/renameCollidingFields` each way.
**Expect** Renamed: every field present, the colliding top-level names suffixed, each widget's `/Parent` chain consistent, PDFium's form layer reading every name as the report states. Merged: one field per colliding name whose widgets share its value. **Either way the report lists every collision.** **If it fails** S1 for a field or value lost; S2 otherwise.

**A-69 · Layers unioned** ☐ *T10*
**Steps** `ctest -R "merge.layers"` over `layers.pdf` and `merge-a.pdf`.
**Expect** Every input's groups in `/OCGs`, `/D`'s `/Order` grouped per input, `/ON`/`/OFF`/`/Locked` unioned; `LayerTree` (T17) or, before it, the first-party reader lists them as the labels say. **If it fails** S2.

**A-70 · Skipped inputs are reported with their reason** ☐ *T10*
**Steps** `ctest -R "merge.skipped"`: an encrypted input, a file that is not a PDF, a truncated PDF.
**Expect** The merge completes with the others; the report names each skipped input and its reason by key. **If it fails** S2.

**A-71 · A separator page between inputs** ☐ *T10*
**Steps** `ctest -R "merge.separator"`.
**Expect** One blank page between consecutive inputs, at the size of the page before it, none after the last. **If it fails** S3.

**A-72 · A 30-way merge, inside its bound** ☐ *T10* — §3.5
**Steps** `ctest -R "merge.scale" -V` (`[run-serial]`).
**Expect** §3.5's four bounds, each printed; the result graded by A-47 and A-66. **If it fails** S2.

### 5.13 Bookmarks and destinations (T11, T12)

**A-73 · The whole Bookmarks pane table, from key events** ☐ *T11*
**Steps** `ctest -R "ui.bookmarks_edit"`: each row of `Features.md` §8's **Bookmarks pane** table through `ShortcutBinder`.
**Expect** `F2` renames in place; `Delete` asks about children; `Ctrl+B` adds from the current view; `Tab`/`Shift+Tab` demote and promote; `Alt+↑`/`Alt+↓` move among siblings; `Ctrl+Shift+D` sets the destination to the current view — each one undo step; the navigation rows Stage 4 registered unchanged. **If it fails** S2.

**A-74 · `Ctrl+B` in the Viewer, and the Viewer census at zero** ☐ *T11*
**Steps** `ctest -R "ui.viewer_commands"`.
**Expect** `Ctrl+B` with the page focused adds a bookmark to the current page and zoom; **no Viewer chord of `Features.md` §8 is unregistered** (`tst_viewercommands`' census, one at Stage 6's sign-off). **If it fails** S2.

**A-75 · Two readings of the outline agree, or the outline is read-only** ☐ *T11*
**Steps** `ctest -R "render.outline_reader"` over every corpus file with an outline.
**Expect** `OutlineReader` and PDFium agree in count, depth, titles and target page on every one; **the control:** a planted disagreement makes the outline read-only, refused by key, and the pane says why. **If it fails** S2.

**A-76 · An edit changes what it means to and nothing else** ☐ *T11*
**Steps** `ctest -R "outline.preserve"`: rename one item of `outline-labels.pdf` and of a fixture whose items carry private keys.
**Expect** The update holds that item's dictionary **only**; its unknown keys equal before and after in value; no other item rewritten. **If it fails** S1.

**A-77 · The hierarchy: delete, re-parent, re-order — and no cycle** ☐ *T11*
**Steps** `ctest -R "outline.hierarchy"`; `serialize:`/`revert:` for the six commands.
**Expect** Delete with its children and with them promoted; a drag onto its own descendant refused; `/First`, `/Last`, `/Next`, `/Prev`, `/Parent` and `/Count` consistent after every command, read back by PDFium; reversible. **If it fails** S1.

**A-78 · Destination, action, colour, style, open state** ☐ *T11*
**Steps** `ctest -R "outline.properties"`.
**Expect** Each written and read back: a destination to a page and view, each action kind (written, never run — D7-12), `/C`, `/F` (italic, bold), the sign of `/Count`. **If it fails** S2.

**A-79 · The generators, graded against the labels** ☐ *T11*
**Steps** `ctest -R "outline.generate"` over `tagged-headings.pdf`, `untagged-headings.pdf`, `text-a4.pdf`.
**Expect** From headings: exactly the labelled `H1`…`H3` hierarchy, from the structure tree when tagged and from the layout when not; by font size and style thresholds and by a text pattern: exactly the labelled lines. Each run one `GenerateBookmarks`. **If it fails** S2.

**A-80 · A table-of-contents page whose links land** ☐ *T11*
**Steps** `ctest -R "outline.toc"`.
**Expect** One inserted page, one undo step; each entry's link lands on the page its bookmark names **by identity**; its text drawn in D6-7's fonts, a title it cannot draw refused by name (Q5(e) ✅); golden. **If it fails** S2.

**A-81 · Import and export as indented text and as XML** ☐ *T11*
**Steps** `ctest -R "outline.exchange"`.
**Expect** Export then import gives the same tree (titles, depth, targets); a malformed file refused by key with nothing changed; **an XML external entity never resolved** (a planted entity naming a scratch file is not read). Written through `pdf/ops` (A-09). **If it fails** S2.

**A-82 · Named destinations created, renamed, deleted** ☐ *T12*
**Steps** `ctest -R "destinations"`; `serialize:`/`revert:` for the three commands; over `dests-named.pdf`.
**Expect** Created from the current view; renamed with every link and bookmark naming it renamed in the same command; deleted with the links naming it cleared and reported; the tree written flat, a pair not changed written as it was; listed and sorted by name and by page. **If it fails** S1 for a reference broken; S2 otherwise.

### 5.14 Links (T12)

**A-83 · A link drawn, or made from text, covers what was meant** ☐ *T12*
**Steps** `ctest -R "ui.linktool"`.
**Expect** A dragged rectangle written as dragged (rule 6, on the raster); a link from a text selection one per line, its quadpoints on the **glyphs' ink**; each made by one `AddAnnotation` with `/Subtype /Link`. **If it fails** S2.

**A-84 · URLs found in text, in one step** ☐ *T12*
**Steps** `ctest -R "links.autodetect"` over a labelled page of URLs and near-misses.
**Expect** Exactly the labelled URLs linked, no near-miss; one command, one undo step. **If it fails** S2.

**A-85 · Following a link — and what is never done** ☐ *T12*
**Steps** `ctest -R "links.follow"` with `Services::openExternally` replaced by a recorder.
**Expect** `GoTo` and a named destination navigate to the page **by identity**; `GoToR` asks, then opens the file in a tab at its destination; `URI` asks, then reaches the recorder once; `Launch` of an executable refused by D6-17's list with **nothing** recorded; a JavaScript, form, media or print action does nothing and says so. **The control:** `objdump -p rawpage.exe` and the PDFium DLL import no script engine (PA-4 asserts the build). **If it fails** S1 for a program launched; S2 otherwise.

**A-86 · A link's appearance and highlight** ☐ *T12*
**Steps** `ctest -R "links.appearance"`.
**Expect** Visible and invisible, border width, style and colour, and each `/H` mode, written and read back; what PDFium draws for each recorded in the case and not graded — whether a reader draws a link's border is Acrobat's and Firefox's to show in M-06. **If it fails** S3.

**A-87 · Verify links reports what is broken, and only that** ☐ *T12*
**Steps** `ctest -R "links.verify"` over `refs-every-kind.pdf` with one page deleted, `refs-by-index.pdf`, and a `GoToR` to a missing file.
**Expect** Exactly the labelled broken targets reported; nothing fetched. **If it fails** S2.

**A-88 · Links ride on Stage 6's commands** ☐ *T12*
**Steps** `ctest -R "serialize:AddAnnotationCommand|revert:EditAnnotationCommand|reversibility"` with Link payloads; `mutation-lint --explain`.
**Expect** Every link field round-trips through the existing commands; reversible over the corpus; no link command class exists (D7-12). **If it fails** S2.

### 5.15 A custom stamp from a PDF page (T13)

**A-89 · The stamp shows the page** ☐ *T13*
**Steps** `ctest -R "stamps.page_source"`.
**Expect** A stamp made from page 3 of `refs-every-kind.pdf`, placed at its natural size, rasterizes within `golden/README.md`'s thresholds of the source page at that scale and shows its identity; made through `CustomStampDialog`, which now lists PDF files. **If it fails** S2.

**A-90 · Placed twice, written once; kept across processes** ☐ *T13*
**Steps** `ctest -R "stamps.page_source_shared|profile_store.restart"`.
**Expect** Two placements share one form XObject graph in the saved file (counted); the stamp library lists it in a second process. **If it fails** S2.

### 5.16 Headers, footers, Bates (T14)

**A-91 · Apply, then remove: byte-identical** ☐ *T14* — §3.8
**Steps** `ctest -R "decoration.roundtrip"` for headers, footers and Bates over every editable corpus file.
**Expect** §3.8's bar. **If it fails** S1.

**A-92 · Every token, on every page** ☐ *T14*
**Steps** `ctest -R "unit.headerfooter"` with a fixed clock and file name.
**Expect** Page number, count, label, date, time, filename, path, custom text and Bates expanded per page against hand-written tables; extracted from the rendered page by PDFium where drawn. **If it fails** S2.

**A-93 · Bates across open documents is one run** ☐ *T14*
**Steps** `ctest -R "bates.across"`: three open documents, a chosen order.
**Expect** Consecutive numbers across the three in that order; the report's first and last per document equal what each document's pages show; each document's own undo removes only its numbers. **If it fails** S1 for a gap or a repeat.

**A-94 · Another producer's decoration is not ours** ☐ *T14*
**Steps** `ctest -R "decoration.foreign"` over `decorated-elsewhere.pdf`.
**Expect** Its header and watermark neither listed nor removed; applying ours beside them leaves theirs byte-identical. **If it fails** S1.

**A-95 · Shrinking content to clear the header** ☐ *T14*
**Steps** `ctest -R "headerfooter.shrink"`.
**Expect** The page's ink and the header's do not overlap on the raster (rule 6); removing the header removes the wrapper too (§3.8). **If it fails** S2.

**A-96 · Drawn text outside the fonts is refused by name** ☐ *T14* *(Q5(e) ✅)*
**Steps** `ctest -R "decoration.characters"`.
**Expect** Latin and CJK drawn and read back by PDFium as typed; a Cyrillic letter refused at input with its name, nothing applied. **If it fails** S2.

**A-97 · Profiles saved, loaded, deleted — across processes** ☐ *T14, T15, T16*
**Steps** `ctest -R "profile_store"` for the three new kinds.
**Expect** Each kind round-trips its whole spec; a second process lists it; a case's profile root is a scratch directory. **If it fails** S2.

**A-98 · Headers and footers look as specified** ☐ *T14*
**Steps** `ctest -R "render.golden.decoration"`.
**Expect** Each alignment, font size, colour, underline and edge distance within `golden/README.md`'s thresholds; each baseline **looked at** before it is kept, recorded in `golden/UPDATES.md`. **If it fails** S2.

### 5.17 Watermarks (T15)

**A-99 · Every source and placement** ☐ *T15*
**Steps** `ctest -R "render.golden.watermark|watermark"`.
**Expect** Text, image and PDF-page sources; opacity, rotation, absolute and relative scale, anchor and offset, tiling — golden, looked at; **behind** is hidden where the page's own content is opaque and **in front** is not (measured on the raster). **If it fails** S2.

**A-100 · Apply, update, remove** ☐ *T15* — §3.8
**Steps** `ctest -R "decoration.roundtrip"` for watermarks; `serialize:`/`revert:` for both commands.
**Expect** §3.8's bar; an update is one command and leaves one watermark. **If it fails** S1.

**A-101 · Print-only and screen-only** ☐ *T15*
**Steps** `ctest -R "watermark.usage"`.
**Expect** A print-only watermark drawn when PDFium renders with its print flag and not without; screen-only the reverse; its group created by `CreateLayer` inside the same command. **If it fails** S2.

### 5.18 Imposition (T16)

**A-102 · The page order of every layout** ☐ *T16*
**Steps** `ctest -R "unit.imposition"`.
**Expect** n-up 2, 4, 6, 8, 9 and 16, across, down and reversed; booklets at 1 … 17 pages, left and right binding, padded to a multiple of four with blanks at the end; creep per sheet; duplex flip on the long and short edge — **each against a hand-written table**, odd counts first. **If it fails** S2.

**A-103 · Each identity in its planned cell** ☐ *T16*
**Steps** `ctest -R "render.imposition"` over `odd-count-*.pdf`.
**Expect** On each sheet, the identity text PDFium extracts falls inside the cell `tst_imposition`'s table assigns it, upright or turned as planned (rule 7). **If it fails** S2.

**A-104 · Marks, frames, labels, gutters** ☐ *T16*
**Steps** `ctest -R "render.golden.imposition"`; A-47 over the outputs.
**Expect** Crop and registration marks, colour bar, frames, sheet labels and gutters golden, looked at; each output passes A-47. **If it fails** S2.

### 5.19 Layers (T17)

**A-105 · The layer tree as the file says** ☐ *T17*
**Steps** `ctest -R "unit.layers|render.layers"` over `layers.pdf`.
**Expect** Nesting, labels, the radio group, the locked group and each usage exactly as labelled; a cycle in `/Order` and a million-entry `/OCGs` bounded and reported. **If it fails** S2.

**A-106 · Showing and hiding change the picture and no byte** ☐ *T17*
**Steps** `ctest -R "layers.view_state"`.
**Expect** A hidden group's content absent from the raster and present again when shown; **the document not marked changed, the update empty, and Save writing nothing**; a radio group's members exclusive; a locked group's state unchanged by a toggle. **If it fails** S1 for a byte written.

**A-107 · Create, delete, merge, properties** ☐ *T17*
**Steps** `ctest -R "serialize:|revert:"` for the four commands; `ctest -R "layers.commands"`.
**Expect** Created empty; merged by repointing **every** `/Properties` entry naming the merged group, **no content stream changed** (byte comparison); deleted with its content orphaned, drawn as recorded in the case for PDFium (M-10 records Acrobat); properties written and read back; reversible over the corpus. **If it fails** S1 for content changed; S2 otherwise.

**A-108 · The Layers pane** ☐ *T17*
**Steps** `ctest -R "ui.layerspane"`.
**Expect** The tree as A-105, visibility toggled by its eye, lock, rename in place, each command reachable from its menu; `Ctrl+Shift+L` shows the pane (`pane.layers` registered). **If it fails** S2.

**A-109 · `Ctrl+Shift+L` — and Write mode's** ☐ *T17*
**Steps** `ctest -R "keybindings"`.
**Expect** Global `Ctrl+Shift+L` shows the Layers pane; Write mode's row (a bulleted list) unchanged — the thirteen collisions still thirteen. **If it fails** S2.

### 5.20 Tags (T18)

**A-110 · The tag tree as the file says** ☐ *T18*
**Steps** `ctest -R "unit.tags|render.tags"` over `tagged.pdf` and `tagged-headings.pdf`.
**Expect** Types (with `/RoleMap`), order, languages and `/Pg` exactly as labelled; a cycle and a depth past the bound reported. **If it fails** S2.

**A-111 · Retype, move, language** ☐ *T18*
**Steps** `serialize:`/`revert:` for the three commands; `ctest -R "tags.commands"`.
**Expect** Each written and read back first-party and by PDFium's structure API (`fpdf_structtree.h`); no content stream changed; reversible. **If it fails** S2.

**A-112 · The order overlay sits on its content** ☐ *T18*
**Steps** `ctest -R "ui.tagspane"`, rendered at 1.5 at an odd offset.
**Expect** Each element's numbered box covers the **ink** of its marked content (rule 6), in logical order. **If it fails** S2.

**A-113 · `Ctrl+Shift+E` shows the Tags pane** ☐ *T18*
**Steps** `ctest -R "keybindings|ui.tagspane"`.
**Expect** `pane.tags` registered and reached by the chord. **If it fails** S2.

**A-127 · The tag tree exported** ☐ *T18* *(D7-25)*
**Steps** `ctest -R "tags.export"` over `tagged.pdf` and `tagged-headings.pdf`.
**Expect** The exported text and XML list every element with its type, language and logical order as A-110 reads it, and nothing else; written through `pdf/ops` (A-09). **If it fails** S3.

### 5.21 The stage-wide sweeps (T19)

**A-114 · A2 at Stage 7, through the shipped surfaces** ☐ *T19* — §3.1
**Steps** `rawpage_bench_surface.exe --stage7`: the real thumbnails pane moving a range by drag every 100 ms while the page view scrolls, three 30-second runs; read the dpi and viewport it reports first.
**Expect** Graded on §3.1 and recorded in `Tech-Stack-Plan.md` §3.3 as the product's figure beside A-33's. **If it fails** — a figure outside §3.1 is a result, reported with the verdict.

**A-115 · The census** ☐ *T19*
**Steps** `rawpage.exe --list-actions` through `cmd`; `ctest -R "keybindings|viewer_commands|action_census"`.
**Expect** Every Page Thumbnails and Bookmarks row of §8 registered, `Ctrl+B`, `Ctrl+Shift+L` and `Ctrl+Shift+E` registered; the declared, registered and unregistered counts re-derived, and the census cases agreeing with `--list-actions`. **If it fails** S2.

**A-116 · The whole suite, both presets, every earlier case by name** ☐ *T19*
**Steps** `ctest --preset x64-release`, `--preset x64-debug`, `--preset gates`; the names compared with Stage 6's sign-off run.
**Expect** Green; every Stage 0–6 case present by name, any rename said. **If it fails** S1.

**A-117 · The golden suite, every existing baseline unchanged** ☐ *T19*
**Steps** `ctest -R "render.golden"`; `git diff db09265 -- golden/manifest.json`.
**Expect** Green; every earlier manifest entry identical, only additions. **If it fails** S1.

**A-118 · The UI binary whole** ☐ *T19*
**Steps** `rawpage_ui_tests.exe` whole, eight times, each seed printed (Stage 4's I-28).
**Expect** Green eight times; `ProcessStateGuard` silent. **If it fails** S2, reproduced with its seed.

**A-119 · Shown is saved through every surface** ☐ *T19* — §3.3
**Steps** `ctest -R "composition.assembly_shown"` with every Stage 7 command reached through its pane, tool or dialog.
**Expect** §3.3's bar for each. **If it fails** S1.

**A-120 · Reversibility for every Stage 7 command** ☐ *T19*
**Steps** `ctest -R reversibility`.
**Expect** Every command of plan §4 swept over every corpus file it applies to; the structural hash restored. **If it fails** S1.

**A-121 · Open, change, save, reopen — for every command kind** ☐ *T19*
**Steps** `ctest -R roundtrip`.
**Expect** Each Stage 7 command applied to every editable corpus file, saved, reopened with no repair, its pages and references as §3.2 grades. **If it fails** S1.

---

## 6. Manual verification — you run this

Fourteen cases, **each on copies of corpus files** in `tests\manual\<case>\`, each asking for a
judgement no assertion makes. **Walk each at the task that makes it walkable**, not at the end:

| Walk at | Cases |
|---|---|
| **T8** | M-01, M-12, M-13 |
| **T9** | M-08 |
| **T10** | M-02, M-03 |
| **T11** | M-05 |
| **T12** | M-06 |
| **T13** | M-14 |
| **T14** | M-07 |
| **T15** | M-09 |
| **T16** | M-04 (**P-3 open**) |
| **T17** | M-10 |
| **T18** | M-11 |

Three things about the state you will be walking, so a planned absence is not reported as a defect:

- **Every change is undoable and changes the file only when you save**; a merge, an extract to one
  file or an imposition is an **untitled tab** until you Save As (Q1 ✅).
- **RawPage does not print** — M-04 and M-09 print from Acrobat (Q5(b) ✅).
- **Walk on copies in `tests\manual\<case>\`**, never on the corpus files themselves. **Before any walk
  that uses a chord, check that no other program holds it** (`CLAUDE.md` → *Traps*; Stage 5's I-17,
  Stage 6's I-23).

---

**M-01 · Moving pages by hand** ☐
- **Goal** The thumbnails pane judged by a person: selecting, dragging, cutting and pasting pages feels like a file manager and lands where it looks like it will.
- **Pre** T8. Copies of `pages-900.pdf` and `refs-every-kind.pdf` in `tests\manual\M-01`; both monitors; two RawPage windows, then two RawPage processes.
- **Steps**
  1. In `refs-every-kind.pdf`, select pages 3 to 5 by click and `Shift`+click; `Alt+↓` twice; `Ctrl+Z` once.
  2. `Ctrl`+click pages 1, 7 and 9; drag them to the end. Open the Bookmarks pane and click each bookmark.
  3. `Ctrl+X` page 2, select page 8, `Ctrl+V`. `Ctrl+Shift+D` on page 4. `Delete` on page 6; read the report.
  4. Drag pages from `pages-900.pdf` in one window into `refs-every-kind.pdf` in the other, then across monitors.
  5. Copy two pages in one RawPage process; paste them in a second.
  6. Save; close; reopen; click every bookmark again.
- **Expect** Every drop where the insertion marker said; every bookmark opens the page showing the identity its title names (the generator titles them so); the deleted page's bookmark says it has no destination; undo steps one gesture at a time; after reopening, all as before.
- **If it fails** Note the step, the pages and the window; a bookmark landing on the wrong identity is S1.

**M-02 · What other readers do with our pages** ☐
- **Goal** D7-18's fourth oracle: Acrobat and Firefox follow our references and open our new documents.
- **Pre** T10. M-01's saved file; an extract, a split folder and a merge of `merge-a`, `merge-b`, `merge-c` saved in `tests\manual\M-02`; P-1, P-2.
- **Steps**
  1. Open each in **Acrobat** and in **Firefox**.
  2. Click every bookmark and every link; read the page labels in each reader's page box.
  3. In Acrobat, open the merged file's form fields and layers panels.
  4. As a **control**, open them in Chrome: it should agree with RawPage, because it is PDFium.
- **Expect** Each reader opens every file **without a repair message**; every bookmark and link lands on the page showing the identity it names; labels as RawPage shows them; the merged fields and layers listed as the merge report said.
- **If it fails** Note the reader, the file and the reference; a reader refusing a file is S1, a reference landing elsewhere S1.

**M-03 · A 30-way merge** ☐
- **Goal** The roadmap's emphasis: a large merge, judged by the person running it.
- **Pre** T10. Thirty copies of corpus files in `tests\manual\M-03\in`, one of them encrypted and one a `.txt` renamed `.pdf`.
- **Steps**
  1. Open the merge dialog; add the thirty by drag from Explorer; reorder five; remove one; set a page range on two.
  2. Merge; watch the progress; cancel once and start again.
  3. Read the report; look at the result's bookmarks.
- **Expect** The dialog takes thirty files without lag; the progress moves and cancel stops promptly with nothing left behind; the encrypted file and the false PDF are reported with reasons; the result's bookmarks are one per input in the order you chose.
- **If it fails** Note the step; a lost input is S1, a frozen window S2.

**M-04 · A booklet on paper** ☐ — **needs P-3**
- **Goal** The roadmap's exit criterion: imposition correct on paper.
- **Pre** T16; **a duplex printer and paper (P-3)**. `odd-count-17.pdf` and `pages-900.pdf`'s first 16 pages extracted, in `tests\manual\M-04`.
- **Steps**
  1. Impose `odd-count-17.pdf` as a booklet, left binding, with crop marks; Save As.
  2. Print it **from Acrobat**, duplex, flip on the short edge; fold the sheets.
  3. Impose the 16 pages 4-up, across; print one-sided.
  4. Repeat step 1 with right binding.
- **Expect** §3.6: the folded booklet reads P-0001 … P-0017 in order, upright, blanks at the end, binding on the side chosen; the 4-up sheets read their cells across; crop marks where the sheet would be trimmed.
- **If it fails** Photograph the sheets; a page out of order or upside down is S2.

**M-05 · Bookmarks by hand** ☐
- **Goal** Hierarchy editing by drag and by chord, judged by a person — the exit criterion's own words.
- **Pre** T11. Copies of `outline-labels.pdf` and `tagged-headings.pdf` in `tests\manual\M-05`.
- **Steps**
  1. From the page, `Ctrl+B`; type a title. In the pane, `F2`, `Tab`, `Shift+Tab`, `Alt+↑`, `Alt+↓`, `Ctrl+Shift+D`, `Delete` (both answers).
  2. Drag bookmarks to re-parent and re-order; try dropping one onto its own child.
  3. Generate bookmarks from headings on `tagged-headings.pdf`; make a table-of-contents page; click its lines.
  4. Save; open in Acrobat.
- **Expect** Each key does what §8 says and nothing else; the drop onto its own child is refused visibly; the generated tree is the document's headings; Acrobat shows the same tree and the TOC page's lines link.
- **If it fails** Note the key or the drag; a bookmark lost is S1.

**M-06 · Links by hand** ☐
- **Goal** Making and following links, and what another reader shows of them.
- **Pre** T12. Copies of `text-a4.pdf` and `refs-every-kind.pdf` in `tests\manual\M-06`; P-1, P-2.
- **Steps**
  1. Draw a link to page 3; make a link from a selected sentence to a URL; auto-detect URLs.
  2. Set one visible with a red dashed border and inverted highlight, one invisible.
  3. Follow each with the Hand tool: the page link, the URL (answer *No*, then *Yes*), the `GoToR`.
  4. Run *Verify links*. Save; open in Acrobat and Firefox and click each.
- **Expect** Each link where it was drawn; the URL asks before anything opens; *Verify links* lists what is broken; Acrobat and Firefox show the borders and highlights as RawPage does and follow each link.
- **If it fails** Note the link and the reader; anything opened without asking is S1.

**M-07 · Headers, footers and Bates numbers** ☐
- **Goal** The preview, the result and removal, judged by a person, and the numbering across documents.
- **Pre** T14. Copies of `mixed-page-sizes.pdf`, `rotated-pages.pdf`, `merge-a`, `merge-b`, `merge-c` in `tests\manual\M-07`; P-1.
- **Steps**
  1. Add a header with a date and a footer `Page <n> of <count>`, on odd pages only; compare the preview with the result.
  2. Save the configuration as a profile; update the header in place; remove it.
  3. Open the three `merge-*` files; Bates-number them in an order you choose with a prefix; read the report.
  4. Save; open in Acrobat; look at print preview.
- **Expect** The result is what the preview showed, on every page size and rotation; update leaves one header; remove leaves the page as it was; the Bates numbers run on across the three; Acrobat shows and previews the same.
- **If it fails** Note the page; a number repeated or skipped is S1.

**M-08 · Crop and resize** ☐
- **Goal** The crop tool and *remove white margins* judged by eye, on both monitors.
- **Pre** T9. Copies of `boxes.pdf` and `text-letter.pdf` in `tests\manual\M-08`; both monitors.
- **Steps**
  1. Drag a crop box on the 150 % monitor; move the window to the 100 % monitor and drag another.
  2. Remove white margins on `boxes.pdf`; apply to all pages of the same size.
  3. Resize `text-letter.pdf` to A4 with scaling, centred; then without scaling.
  4. Undo the crop.
- **Expect** The crop is the box you dragged on both monitors; white margins go to the edge of the ink; the A4 page holds the letter content centred; undo restores the page.
- **If it fails** Note the monitor and the page; a crop off the dragged box is S2.

**M-09 · Watermarks** ☐
- **Goal** Placement, layering and print-only behaviour, judged by eye and on a print.
- **Pre** T15. Copies of `text-a4.pdf` and `image-dct-rgb.pdf` in `tests\manual\M-09`; P-1.
- **Steps**
  1. Add a text watermark diagonally at 30 % behind the content; an image watermark tiled in front; a page watermark from `refs-every-kind.pdf`.
  2. Make one print-only. Save; print it from Acrobat to **Microsoft Print to PDF**; open that output.
  3. Update one; remove one.
- **Expect** *Behind* shows only where the page is empty; the print-only watermark is absent on screen in RawPage and Acrobat and present in the printed PDF; update and remove act on the one chosen.
- **If it fails** Note the watermark; one removed that was not chosen is S1.

**M-10 · Layers** ☐
- **Goal** The Layers pane, and what Acrobat does with our changes.
- **Pre** T17. A copy of `layers.pdf` in `tests\manual\M-10`; P-1.
- **Steps**
  1. `Ctrl+Shift+L`; hide and show each layer; try the radio group and the locked layer.
  2. Close without saving: the document must not ask.
  3. Rename one, merge two, delete one; set one's default to hidden and another to print-only. Save; open in Acrobat's Layers panel.
- **Expect** Hiding and showing never mark the document changed; the radio group is exclusive; the locked layer does not toggle; Acrobat shows the renamed, merged and deleted layers and the defaults as set, and draws the deleted layer's orphaned content as RawPage does — record what each draws.
- **If it fails** Note the layer; a close that asks after only showing and hiding is S2.

**M-11 · Tags and the order overlay** ☐
- **Goal** The tag tree and its overlay, judged on a real tagged document.
- **Pre** T18. A copy of `tagged.pdf` and any tagged PDF you have, in `tests\manual\M-11`.
- **Steps**
  1. `Ctrl+Shift+E`; walk the tree; turn on the order overlay.
  2. Retype a `P` to `H2`; move an element; set a language on the document and on an element.
  3. Save; reopen; look at Acrobat's Tags panel.
- **Expect** The overlay's numbers sit on their content in reading order; each change visible after reopening and in Acrobat.
- **If it fails** Note the element; a change lost is S2.

**M-12 · Does moving pages feel immediate?** ☐ — the judgement half of §3.1
- **Goal** A2 at Stage 7 judged by the person dragging.
- **Pre** T8. A copy of `pages-900.pdf` in `tests\manual\M-12`; the 150 % monitor.
- **Steps**
  1. Scroll to page 450; select ten thumbnails; hold `Alt+↓` for five seconds while scrolling the page view with the wheel.
  2. Drag thirty thumbnails from page 100 to page 800.
- **Expect** The thumbnails and the page follow the keys without a visible stall; the dragged pages appear at once in their new place with their pixels, not blank.
- **If it fails** Note when it stalled; record A-33's figures beside your judgement.

**M-13 · Changing a signed document** ☐
- **Goal** D7-20's warning, and what Acrobat's signature panel says afterwards.
- **Pre** T8. Copies of the four `corpus/signed/` files in `tests\manual\M-13`; P-1.
- **Steps**
  1. Open each; move a page; read the message bar. Move another.
  2. Save; open in Acrobat; read the signature panel.
  3. On a fresh copy, move a page and **Save As**: answer *keep the signatures*. On another, Save As and answer *rewrite*. Open both in Acrobat.
- **Expect** The warning appears once per document, before the first change; Acrobat reports each signature as valid for the signed revision, with the document changed after signing. Save As asks before writing; the copy whose signatures were kept reads as step 2's; the rewritten one is reported by Acrobat as no longer valid, as the question said it would be.
- **If it fails** Note the file; a signature Acrobat calls *invalid* (not *changed after*) is S1.

**M-14 · A stamp made from a PDF page** ☐
- **Goal** Stage 6's Q7, judged in RawPage and in Acrobat.
- **Pre** T13. A copy of `refs-every-kind.pdf` and of `text-a4.pdf` in `tests\manual\M-14`; P-1.
- **Steps**
  1. Make a custom stamp from page 3 of `refs-every-kind.pdf`; place it twice on `text-a4.pdf`, once rotated.
  2. Save; open in Acrobat and Firefox.
- **Expect** The stamp shows the page sharply at every zoom (a vector, not a picture); Acrobat and Firefox show it the same.
- **If it fails** Note the reader; a blurred stamp is S2.

---

## 7. Known caveats — verify, do not assume broken

Behaviours that look like defects and are not. **A wrong row here is worse than a missing one.**

| Looks broken | Is |
|---|---|
| **Chrome or Edge follows our links exactly as RawPage does** | Not evidence — both are PDFium (D7-18). Acrobat and Firefox are the oracles |
| **A file saved with `Ctrl+S` is larger than the pages left in it** | Correct: Save appends, unless `documents/saveMode` is `full`; **Save As** rewrites and compacts (D7-25) |
| **Save As on a signed document asks a question first** | Correct — a rewrite invalidates the signatures, so you choose (D7-25) |
| **A bookmark says it has no destination after its page was deleted** | Correct — D7-2 keeps it rather than dropping your title |
| **Hiding a layer does not ask to save** | Correct — visibility on screen is view state (D7-9) |
| **A merge, an extract or an imposition opens untitled** | Correct — Q1, approved 2026-10-29: Save As to keep it (D7-5) |
| **Inserted pages lose their tags** | Correct — D7-4 |
| **A JavaScript link does nothing** | Correct — never executed (D7-12) |
| **Another program's watermark cannot be removed in RawPage** | Correct — only RawPage's (D7-7, `Features.md` §5.8) |
| **The white-ink page is not split as blank** | Correct — blank means no ink in the content box (A-49) |
| **A `Ctrl+Shift+` or `Alt+` chord does nothing** | Another program may hold it as a system-wide hotkey (Stage 5's I-17) — try `RegisterHotKey` on it |

---

## 8. Issue log

Numbered `I-nn`, **this stage's own namespace**. Each entry carries its mechanism, why the suite
missed it, and the control that was run red before the fix — the shape every earlier stage's log uses.

| # | Sev | Title | State |
|---|---|---|---|
| **I-01** | S2 | A page that inherits its `/MediaBox` or `/CropBox` from a `/Pages` node was shown at US Letter, uncropped, and its whole document refused for editing | **Closed** at T1, 2026-10-29 |
| **I-02** | S3 | The fuzz soak built nothing: `ubuntu-latest` moved it to Ubuntu 26.04, whose Clang 21 refuses an unused field in `core/text/FolderSearch.h` under `-Werror` | **Fixed** 2026-10-29, not yet read from a CI log |

**I-01 (S2) — the engine facade read a page's boxes from the page dictionary alone.** *Found* at T1 by
the first scratch render of `assembly/inherit-resources.pdf`, whose pages inherit both boxes: an A4 page
turned 90° came out 1056 × 816 px, US Letter, uncropped, the identity off-centre. *Mechanism:*
`pdf/render/PdfiumDocument` read each page's boxes with `FPDFPage_GetMediaBox` and `FPDFPage_GetCropBox`,
which read the PAGE DICTIONARY only — PDFium's own renderer inherits, those two calls do not — and took
their silence for *"no box anywhere"*, substituting US Letter. So the page model disagreed with what the
engine drew, and with `pdf/ops/PageOps`' walk, which inherits `/MediaBox`: `Permissions::assess` compared
the two media sizes and refused the WHOLE document by `error.edit.pageSizeDisagrees`. *Why no suite saw
it:* **no corpus file before T1 inherited either box** (a scan of every page dictionary in `ordinary/`,
`signed/`, `compare/` and the seeds, 2026-10-29) — `rotate-inherited.pdf` inherits `/Rotate` and states
its `/MediaBox` on every page. *Fix:* the page's own boxes where it states them; otherwise
`FPDF_GetPageBoundingBox` — the box the engine DRAWS, media intersected with crop, which does inherit —
as the media box when the page states none, and as the crop box when it is smaller than the media box the
page states. `PageOps`' walk carries `/CropBox` down as it carries `/MediaBox`, and `Permissions` compares
the DRAWN box of both readers (`CosPageFacts::drawnSize`), the one box both can state through
inheritance. *Bound, stated at the code:* PDFium exposes no inherited media box apart from the crop, so a
page inheriting its `/MediaBox` AND a smaller `/CropBox` reports the drawn box as its media box — what is
shown is right; the media box is first read apart from the crop by the plan's page-box commands (D7-15,
T9). *Regression:* `render.assembly labels: a page shows the boxes and rotation it inherits (I-01)` (each
page's drawn box and rotation against the label, the document editable, and `boxes.pdf`'s own boxes as
the control); `unit.page walk resolves the media and crop boxes a page inherits`; and
`render.the shipped backend lets every ordinary, signed and assembly corpus file be edited`, which now
sweeps `assembly/`. *Control:* with the facade change reverted alone, the I-01 case failed on every
page's drawn box (off by 20 pt and the crop's width) and the sweep refused `inherit-resources.pdf` by
`error.edit.pageSizeDisagrees` on page 1; restored, both pass. The 99 baselines were captured after the
fix.

**I-02 (S3) — the runner image moved under CI, and the soak stopped at the build.** *Found* 2026-10-29
by the scheduled fuzz soak, run **40853107406** on `0ed701e`: all five targets failed at their build
step on `core/text/FolderSearch.h:163: error: private field 'engine_' is not used
[-Werror,-Wunused-private-field]`, so no target was fuzzed for a second — the *no sanitizer report* step
failed only because no log existed. No product behaviour is wrong; the severity is S3 because what was
lost is evidence: §9 item 6 is read from these jobs. *Mechanism:* two faults. `FolderSearch` stored
`engine_` (Stage 5) and read it nowhere — `session_` is built from the constructor's argument, and every
engine call goes through it. And every Linux job named `ubuntu-latest`, which GitHub began moving to
Ubuntu 26.04 on 2026-10-19: the run's own log reads `Image: ubuntu-26.04`, `Clang 21.1.8` and
`qt6-base-dev 6.10.2`, where the soak the day before (40665395393, on `db09265`) and CI run
40783851053's `portable-fuzz` job on `0ed701e` itself read `ubuntu-24.04` and `Clang 18.1.3`. *Why no suite saw it:*
MinGW's GCC has no `-Wunused-private-field`, and Clang 18 compiled the file clean; why the two Clang
versions differ on this field was not established. *Fix:* the field and its initializer removed; the
four Linux jobs in `ci.yml` and the soak in `fuzz-soak.yml` pinned to `runs-on: ubuntu-24.04`, because
the image's `qt6-base-dev` is the floor convention 24 (`Implementation-Plan.md` §1.5) holds the portable
layers to and 26.04 would raise it to 6.10.2 with nothing deciding to — `ci.yml`'s header says so, and
moving off 24.04 is a change to both files and that convention together. *Control:* none on this
machine, which has no clang; the red is run 40853107406 itself, and the pin means the next run builds
with Clang 18 again, so **Clang 21's verdict on the fix is not measured by CI**. *Closed* when a soak on
the fix is read from its log.

---

## 9. Sign-off criteria

Stage 7 is done when **all** of these hold — [`Stage7-Assembly-Plan.md` §13](./Stage7-Assembly-Plan.md#13-definition-of-done-for-stage-7)
restated as a checklist. **Q3 was approved on 2026-10-29, so the same list is applied twice**: to T1 … T13 and the
cases and walks they own (**Stage 7a**, after which Stage 8 may start), then to the rest (**Stage 7b**).

1. ✅ **2026-10-29.** The plan's §0 answered — all five approved as proposed, Q1 amended by D7-25 the same
   day — and every case written against a proposal re-read against its answer: all stand as written, with
   §3.9, A-122 … A-127 and M-13's Save As steps added for D7-25.
2. ☐ Every automated case carries a result, recorded against it rather than summarized (Stage 1's
   I-32), the Linux halves read from the job's **log**.
3. ☐ M-01 … M-14 walked and signed off **by you** (M-04 once P-3 is cleared). I do not mark a manual case.
4. ☐ `checks` clean — `mutation-lint` naming exactly the plan's commands (A-08), **15** waivers (A-09).
5. ☐ `ctest` green on **both** presets and the `gates` preset, every earlier case present by name
   (A-116); the UI binary green whole (A-118).
6. ☐ `portable-asan` clean over every Stage 7 suite (A-04); `portable-fuzz` and the soak clean on
   **six** targets (A-46); `qpdf --check` clean (A-25, A-47).
7. ☐ The golden suite green with every existing baseline unchanged (A-117).
8. ☐ **A2 at Stage 7** recorded, bench and product (A-33, A-114); §3.7 recorded (A-35).
9. ☐ No open S1 or S2 in §8.
10. ☐ `Architecture-FileMap.md` rows flipped with their files; the plan's §10 debt paid;
    `Implementation-Plan.md`'s Stage 7 block updated to reality, deviations included.
