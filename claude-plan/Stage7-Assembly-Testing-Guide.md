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
> as written.** **T1 done 2026-10-29, T2 and T3 2026-10-30, T4 and T5 2026-10-31, T6 2026-11-04, T7 2026-11-05, T8 2026-11-07: A-12 … A-34, A-36 … A-45, A-47 … A-57, A-59, A-122 … A-126 and
> A-128 carry a result, all ✅ (A-55's second process `[D7-13]` run by hand on `windows` 2026-11-07, with the user's leave; A-47's `qpdf` half from the next CI run's log; **A-58 moved to T9**, `Stage7-Assembly-Plan.md` D7-31(j)) (A-25 from CI runs 40995950997's and 41180314162's logs; A-122's `qpdf` half ✅ from run 41770714539's log, three faults inherited from damaged sources (I-13, closed); A-33 is §3.1's verdict, `go, with named custom work`,
> the user's choice, re-read at T5 with the real insert and inside); A-35 carries T4's half and T5's reading (🟡) --
> **§3.7's move half NOT TAKEN at T5** (+20.9 … +23.8 ms after ~3,600 copied objects), the bookmark half T11's;
> A-45 carries T5's half and T8's (✅); **A-46 ✅ 2026-11-04** -- its 60 s log found the target UNRUN (I-09), then I-12 at T6's push and the two after, and run 41832843136's 60 s and soak 41833219442's ten minutes found nothing; A-03 carries its T2 half (🟡) and A-27 its T2 and T8 halves (✅); A-08 … A-10 carry T5's readings; no
> other case has one.** T1 raised **I-01 (S2), closed inside T1** — the engine facade showed a page that inherits its
> boxes at US Letter, uncropped, and refused its document for editing; T4's A-128 raised **I-03 (S2), closed inside
> T4** — a moved link whose target page is then deleted refused the delete; CI's fuzzer then raised **I-04 (S2),
> closed at T4** — the compose's first new number ignored the object limit — and CI's Windows job **I-05 (S3), OPEN** —
> a Stage 5 index case read one page more than expected, once; T5's A-38 raised **I-06 (S3)** — the parser read an
> unterminated dictionary into the next object — and **I-07 (S2)** — an inserted page drew its hidden layers — both
> **closed inside T5** by the user's choice, and its first full run **I-08 (S3), OPEN** — a Stage 5 timing bound red
> once (§8); T6 raised **I-09 (S3), fixed** -- `fuzz_assembly` built and fuzzed by nothing, a gate added -- **I-10 (S2), closed
> inside T6** -- a page an undo put back after a save drawn as one the engine never opened, older than T6 -- and **I-11 (S2),
> OPEN** -- recovery stops at an undo, after a save, of a page delete; T2, T3, T7 and T8 raised none (T8's full `x64-debug` run met **I-08** a fourth time). **T8 made M-01, M-12 and M-13 walkable; none is walked.** **P-3, a
> duplex printer for M-04, is open.** **Cases written against a §0 proposal name it in their heading or row**
> (Q*n* ✅); none was declined, so none moved or was struck. **128 automated** (A-01 … A-128, A-128 added at T4)
> and **14 manual** (M-01 … M-14).
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

**READ AT T5 AND T11, not at T4** -- the user's choice, 2026-10-31: the workload above needs inserts from
`text-900.pdf` (T5's `InsertPages`) and a bookmark rename (T11's), and neither exists at T4, so the verdict is not
read on a stand-in. **What T4 grades instead is the mechanism** (A-35): what a one-page move and a rotation BUILD,
counted, after 0, 300 and 3,000 unsaved objects of highlights -- and reports `journal.run`'s times beside it, figures
that are not this verdict.

**T5's reading (2026-10-31): the move half is NOT TAKEN** -- a one-page move's p95 is 23.5–25.8 ms after ~3,600 unsaved
copied objects against 2.0–2.6 ms with none, past both bars; A-35 names the cause (every inserted page's dictionary built
again by every compose). The bookmark-rename half is T11's; the verdict stays open until it is read.


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
| Q4 ✅ — `DW-4`'s second cause | A-35 … A-37, A-128, §3.7 |
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

**A-03 · The headless suite is still headless** 🟡 *T2's files 2026-10-30; whole at T19*
**Goal** The page tree, the map, the generators, the imposition order, the token expansion, `LayerTree`, `TagTree` and every command's apply and revert are `core/`, and `core/` links no display and no engine.
**Steps** `ctest -R "gates.headless_assert"`; configure both presets.
**Expect** Every case as before, the negative controls still caught; `rawpage_tests` configures (`rawpage_assert_headless()` fails configure otherwise) with every Stage 7 `core/` file in it. **If it fails** S1.
**Result (T2)** Both presets configure with `core/doc/PageMap.cpp` in `rawpage_core` and `rawpage_tests` linking it -- `rawpage_assert_headless()` passed; `gates.headless_assert.*` green in `ctest --preset gates` (137/137) and in `x64-release`'s `^gates\.` run (137/137). The page tree's set operation, the map, the store's and the refusals' remap and the three commands' apply and revert all run in `rawpage_tests` (`[pagemap]`, `[pagetree]`, `[pages]`, `[reversibility]`). Re-run whole at T19 with every later `core/` file.

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

**A-07 · Every new target compiles C++20** ✅ *2026-10-31, T5*
**Steps** Configure `x64-release`, then configure it again with `-DRAWPAGE_BUILD_FUZZERS=ON`; read `rawpage_assert_language_standard`'s `targets walked` line each time.
**Expect** Both configure; the second walks one target more than at Stage 6 — `fuzz_assembly`'s driver — and the counts are re-derived, not quoted (Stage 4's I-08). **If it fails** S2.
**Result** `x64-release` configured into scratch trees: *"C++20 on every compiled target (35 targets walked)"* plain -- the same 35 as at T4 -- and with `-DRAWPAGE_BUILD_FUZZERS=ON` *"(41 targets walked)"*, the six drivers, one more than Stage 6's 40, the configure's own *"fuzz targets ... fuzz_assembly (6, as expected)"*. (`CLAUDE.md`'s *"that flag adds the three drivers"* was stale since Stage 5 and is corrected.)

### 5.2 The gates — and the numbers that must and must not move

**A-08 · `checks` clean, and `mutation-lint` names exactly the plan's commands** ☐ *every task; whole at T19* *(Q1 ✅, Q2 ✅)*
**Goal** D7-1's mechanical proof.
**Steps** `cmake --build --preset x64-release --target checks`; `mutation-lint.exe --root . --explain`.
**Expect** Exit 0; four gates; `mutation-lint` names **exactly** the twelve commands of Stage 6's sign-off and plan §4's — **43** — re-derived from the plan's §4 after Q1 and Q2 (2026-10-29), and again if a later answer moves a command — each with its `Commands.def` row and its `[serialize:]` and `[revert:]` cases listed by `--explain`. **No `CreateLink`, `EditLink`, `DeleteLink`** (D7-12), and no `ExtractPages` or `MergeDocuments` (Q1 ✅). **If it fails** S1 for an unlisted command; S2 for a missing test.
**T5 (2026-10-31)** `checks` clean, *"mutation-lint: clean -- 17 command(s)"* -- T4's fifteen and `InsertPagesCommand`, `ReplacePagesCommand`, each with its row and both tags (`[serialize:]` in `tst_command_serialize`, `[revert:]` in `tst_reversibility` and `tst_copier`); `gates.mutation_lint.clean_tree`'s roster updated to the seventeen. **T4 (2026-10-31)** `checks` clean, *"mutation-lint: clean -- 15 command(s)"* -- T4 adds no command. **T3 (2026-10-30)** `checks` clean, *"mutation-lint: clean -- 15 command(s)"* -- T3 adds no command. **T2 (2026-10-30)** `checks` clean; *"mutation-lint: clean -- 15 command(s)"* -- Stage 6's twelve and `MovePagesCommand`, `DeletePagesCommand`, `DuplicatePagesCommand`, each with its row and both tags; `gates.mutation_lint.clean_tree`'s roster updated to the fifteen. The gate first refused a shared `PageSetCommand` base (*row-without-class*), since it reads derivation from `Command` as text -- the three now derive from `Command` directly.

**A-09 · `layer-lint`: no new waiver, no new edge** ☐ *every task; whole at T19*
**Steps** `layer-lint.exe --root . --explain`.
**Expect** Clean; **15 escapes**, the same fifteen; the extract, split, export and import paths write through `pdf/` (D7-5, D7-10). **The control:** a planted `QFile::open(WriteOnly)` in `ui/dialogs/PageOpsDialogs.cpp` is refused. **If it fails** S1 for an unwaived write; S2 for a new waiver.
**T5 (2026-10-31)** *"layer-lint: clean -- 691 files, 7198 includes"*, `--explain`'s census the same fifteen escapes -- the copier writes nothing (its payload is bytes in memory) and `copyPages` only reads. **T4 (2026-10-31)** *"layer-lint: clean -- 684 files, 7081 includes"*, `--explain`'s census the same fifteen escapes -- the two files more are T4's test files; the verify's thread and the gate add no edge. **T3 (2026-10-30)** *"layer-lint: clean -- 682 files, 7022 includes"*, the same fifteen escapes. **T2 (2026-10-30)** *"layer-lint: clean -- 679 files, 6939 includes"*, `--explain`'s census the same fifteen escapes. The control waits for `PageOpsDialogs` (T7).

**A-10 · `i18n-lint`: six new families, each a context of its own** ☐ *every task; whole at T19*
**Steps** `i18n-lint.exe --root . --explain`; `checks`.
**Expect** Clean, **0 literal waivers**; the families `pages`, `bookmarks`, `layers`, `tags`, `decorate`, `links` each in `en` and `zh-TW`; **`wrong-context` clean** — and no family spelled like a command or action id (Stage 5's I-08). **If it fails** S2.
**T5 (2026-10-31)** *"i18n-lint: clean -- 427 files, 2 catalog(s), 0 waiver(s)"*; seven keys in the existing `command.` and `error.` families, in both locales -- `command.page.insert`, `.replace`; `error.edit.pageNotSaved`, `.replaceCount`; `error.copy.tooLarge`, `.malformed`; `error.pdf.pageSizeOutOfRange` -- T5 has no surface, so no new family. **T4 (2026-10-31)** *"i18n-lint: clean -- 424 files, 2 catalog(s), 0 waiver(s)"*; T4 adds no key -- an edit taken back after it returned is said by Stage 6's `shell.status.annotationRefused` with `error.edit.notShown`. **T3 (2026-10-30)** *"i18n-lint: clean -- 424 files, 2 catalog(s), 0 waiver(s)"*; T3 adds no key. **T2 (2026-10-30)** *"i18n-lint: clean -- 424 files, 2 catalog(s), 0 waiver(s)"*. T2 has no surface, so it adds no new family: seven keys in the existing `command.` and `error.` families, in both locales (`command.page.move`, `.delete`, `.duplicate`; `error.edit.noPagesLeft`, `.pageReadingsDisagree`, `.copyNotSaved`; `error.pdf.pageTreeDamaged`), and `unit.every command name is a catalog key present in both shipped locales` covers the three names.

**A-11 · `license-audit` unchanged** ☐ *T19*
**Steps** `checks`' `license-audit` line.
**Expect** Clean; **five** Qt modules, the same five; no new dependency in `vcpkg.json` (Stage 7 adds none). **If it fails** S1.

**A-12 · The fuzz count is six, and the assertion still fires** ✅ *2026-10-31, T5*
**Steps** `ctest --preset gates -R fuzz_count`.
**Expect** Every `gates.fuzz_count.*` case green with `fuzz_assembly` counted; a **new** case where five of six fails configure, and the existing control (a helper standing in for a target) still caught. **If it fails** S2.
**Result (T5, 2026-10-31)** `ctest --preset gates`: **139/139**, every `gates.fuzz_count.*` case green -- the two new, `six_of_six_with_a_helper` (the shipped count, the control) and `five_of_six_fails` (*"miscounted"*), beside the earlier ones and `helper_is_not_a_target`. `tests/fuzz/CMakeLists.txt` asserts **6**.

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

**A-17 · The page tree's mutation set** ✅ *2026-10-30, T2*
**Steps** `ctest -R "unit.pagetree"`.
**Expect** Insert, remove, move and duplicate of single pages and ranges at the start, middle and end; a move to its own position changes nothing and makes no command; an index outside the tree, an overlapping move and **a removal of every page** are refused, changing nothing (D7-21). **If it fails** S1.
**Result** Five `pagetree` cases (`tests/unit/tst_pagetree.cpp`, and one through the journal in `tests/unit/tst_reference_repair.cpp`). The tree's ONE operation, `PageTree::apply(PageMap, inserted)`, against hand-written orders graded BY NAME (each page labelled `a`…`f`): six moves -- a page and a range at the start, middle and end, and a set that is not contiguous -- each carrying its boxes, rotation and label, and each undone by the inverse map; a removal of three and a duplication of two, each undone with `removedBy(map)`'s pages; refused, unchanged, for every page removed, a wrong count and the wrong number of inserted pages. The store's half: listings carried with their pages, owners renumbered, an annotation listed by two pages re-owned when its page goes and given back on undo, a direct dictionary's name unmoved, a copy's listing added and dropped. Through the journal: a move to where the pages are (a page and a set), a block that would run past the end, a page set not ascending, one outside the tree, an empty one -- each refused, nothing recorded, the model's hash unchanged, the sink with nothing to write; the control, a real move, runs. **Control:** P7 (the store keeps an owner where its page was) turned the store case red, 5 assertions.

**A-18 · The page map's arithmetic** ✅ *2026-10-30, T2*
**Steps** `ctest -R "unit.pagemap"`.
**Expect** Each command's map against hand-written tables; a map composed with its inverse is the identity; two maps compose to the map of the two commands run in order; removed and inserted indices exact. **If it fails** S1.
**Result** Five `pagemap` cases (`tests/unit/tst_pagemap.cpp`), 75,676 assertions with `[pagetree]`. Each map against a hand-written order -- moves of a page, a range and a set, a removal, a duplication -- with its removed and inserted indices exact; refused for an empty, unsorted, repeated or out-of-range set and a block past the end. **The expectation was corrected by its first run:** a map composed with its inverse is the identity only for a MOVE -- a removal's inverse brings each removed page back as an INSERTION at its old index, since a map names pages by place and a page that went has none. So the case now asserts, over **every** map the three commands make over documents of 1 to 5 pages (280), that every page both keep is where it started, every removed page is an insertion there, and a permutation is exactly the identity; and that two maps compose to running both, checked by running them over a list of names (every pair over documents of 1 to 4 pages, more than 1,000). The page command's `Scope`: `WholeDocument` with the map, an undo's the inverse, two page commands composed, and every other scope with none.

**A-19 · Every reference lands on the page it named — by what that page shows** ✅ *2026-10-30, T2; re-run at T5, T8, T9, T10, T11, T14* — §3.2 *(T5's re-run ✅ 2026-10-31; T8's ✅ 2026-11-07, unchanged -- T8 adds no command that writes a reference, and the suite ran whole and green)*
**Steps** `ctest -R "render.reference_repair"`: for each page command and each fixture with references, apply, save, reopen in a fresh engine document, resolve each labelled reference through PDFium, and extract the identity of the page it lands on.
**Expect** **0 failures** against §3.2's table. **The control (rule 5):** a planted repair that rewrites every `/Dest` to the page now at the OLD index — the error that looks like a repair — fails the case for every moved reference; a planted no-op `/PageLabels` rewrite fails the label rows. **If it fails** S1.
**Re-run (T5, 2026-10-31)** Two insert runs added over the six fixtures -- two blank pages at the front, one in the middle, every index after them shifted (blank, so no reference can land on one and be taken for the page it named): **406 references landed on the page showing the identity their label names** (296 at T2), 19 cleared, 0 failures; each saved, reopened, graded, then undone after the save and graded against the original.
**Result** **0 failures.** `tests/render/tst_reference_repair.cpp`, six fixtures -- `refs-every-kind` (every kind), `merge-a`/`-b`/`-c` (outlines, links), `annots-on-pages` (links), `split-blanks` (an outline) -- each through four to six commands (the first page to the end, the last to the front, the last two to the front, a duplicate of the first and last, a delete of the second, a delete of the first and last), each SAVED through the shipped backend and `SaveCoordinator`, reopened in a fresh engine and graded; then UNDONE after the save, saved, and graded against the original. **296 references landed on the page showing the identity their label names, 19 cleared as D7-2 says.** Resolved by PDFium: the outline, the named destinations and the page labels; read first-party by the case's own reader (no code shared with `pdf/ops/ReferenceRepair`): links, structure elements, beads and `/OpenAction`, each landing page's identity extracted by PDFium; and a first-party sweep that EVERY reference in the file -- labelled or not -- names a page or nothing. The model's page labels equal the saved file's, read by PDFium, after every command. **Controls:** P1, the repair that sends every explicit destination to the page now at its OLD index, red (25 failed assertions); P2, the no-op `/PageLabels` rewrite, red in A-21 (32). **P3 STAYED GREEN** at first -- outline items to a deleted page left as they were -- because PDFium answers *unresolved* alike for a destination cleared and one pointing at a page the tree no longer lists; the grade gained the first-party dangling sweep, and P3 then went red (7). **What it does not cover:** the screen (Stage 7 T3's A-29), and which of two pages showing one identity a reference lands on after a duplicate (the headless half shows the source's annotations are not the copy's, and a move writes no reference).

**A-20 · A reference to a deleted page is never left dangling** ✅ *2026-10-30, T2*
**Steps** `ctest -R "reference_repair.deleted"` over `refs-every-kind.pdf`, deleting each page in turn.
**Expect** Each kind treated as D7-2 says — outline item kept with no destination, link kept with no action, named destination removed, structure content items removed, bead unlinked, `/OpenAction` removed — **every one listed in the command's report**; **every reference in the saved file resolves to a page or to nothing**; undo restores the structural hash. **If it fails** S1.
**Result** All twelve pages, each deleted in a fresh copy: the command's report is EXACTLY the references the label says target that page -- outline items and links *cleared*, named destinations and structure elements *removed*, beads *unlinked*, `/OpenAction` *removed*, each with its page -- plus the `GoToR` *reported*; the saved file graded as A-19 (every remaining reference on its page, every one to the deleted page gone, nothing dangling, a thread left with no bead out of `/Threads`); then UNDONE after the save -- the model's hash the original's, and every reference back on its page in the file saved again, which is what the sink's LEDGER is for. **Found while building, by this case:** a thread whose beads all survived was not written back, so after an undo-after-save its ring kept the repaired links (5 beads); fixed in `ReferenceRepair::compose` -- every object a pass visits goes through the desired state. **Controls:** P3 (outline items left alone) red once the dangling sweep existed; P4 (the ledger never kept after a save) red, 12 assertions, every one in the undo-after-save grade.

**A-21 · Page labels follow their pages** ✅ *2026-10-30, T2*
**Steps** `ctest -R "reference_repair.labels"` over `refs-every-kind.pdf` and `outline-labels.pdf`.
**Expect** After a move, every moved page shows **the label it had**, read by PDFium (`FPDF_GetPageLabel`); an inserted or duplicated page takes the next label of the range it lands in, against hand-written tables. **If it fails** S1.
**Result** Three moves of `refs-every-kind.pdf` (the first page to the end; three pages from two ranges to the front; the last to the middle): every page, by the identity PDFium extracts, shows the label it had. A duplicate of pages 0 and 6, against the hand-written `i ii ii iii 1 2 3 4 5 5 6 A-1 A-2 A-3` (each copy the next number of its range, the page after it keeping its own); a delete of pages 3 and 10, against `i ii iii 2 3 4 5 6 A-1 A-3`. `outline-labels.pdf`: the first page moved to the end, every label as PDFium read it before, carried. **Control:** P2 (the `/PageLabels` rewrite a no-op) red, 32 assertions.

**A-22 · What names a page by index is reported, not "fixed"** ✅ *2026-10-30, T2*
**Steps** `ctest -R "reference_repair.by_index"` over `refs-by-index.pdf` and the `GoToR` of `refs-every-kind.pdf`.
**Expect** Those references **byte-identical in value** after every command, and each in the report. **If it fails** S2.
**Result** `refs-by-index.pdf`'s outline item with an INTEGER `/Dest` and `refs-every-kind.pdf`'s `GoToR` link, each through a move, a delete and a duplicate: in the saved file, read from the original's bytes and the saved bytes alike, the value is EQUAL (`CosObject` semantic equality, which is the bytes' meaning), and each command's report lists it -- `IndexDestination` and `RemoteGoTo`, *reported*. `refs-by-index.pdf`'s link to a page outside the page tree is untouched by a delete. Every page command reports them, a move included (`MutationSink::reorderPages` returns a report for this).

**A-23 · Serialize, revert and reversibility for every page command** ✅ *2026-10-30, T2; extended at T5, T8, T9 (T8's ✅ 2026-11-07: `RotatePageCommand` over a set -- A-59's result)*
**Steps** `ctest -R "serialize:|revert:|reversibility"` for `MovePagesCommand`, `DeletePagesCommand`, `DuplicatePagesCommand` (later tasks add theirs).
**Expect** Each command's line round-trips; apply then revert restores the structural hash **over every editable corpus file**; the journal line names pages by index (D7-24). **If it fails** S1.
**Extended (T5, 2026-10-31)** `page.insert` and `page.replace` -- the payload as base64 in the line, the key, an insert's place and count -- round-trip (51 round trips, from 48); **12 hostile lines** refused by `error.command.malformed` (a graph missing, empty, not base64, a number, 10 MB of not-base64; `at` -1 and 1.5; a count of 0 and a million and one; a key missing; pages descending). **Reversibility:** an annotated page and an inheriting one inserted before the page and at the end, and the page replaced by each -- *"reversibility: 4469 combinations over 121 editable corpus files; 0 failures"* (3649 at T2).
**Result** `{"type": "page.move", "pages": [...], "to": n}`, `page.delete` and `page.duplicate` with their `key` -- pages by INDEX -- round-trip through `CommandRegistry` (7 samples, the limit of a page index among them; the round-trip count now 48, from 41); a new hostile case refuses 22 malformed lines by `error.command.malformed` -- a set not ascending, repeated, out of range, a million and one pages, a 10 MB string, a key short, upper-case or not hex -- with its control accepting the boundaries and keeping the key. **Reversibility:** the generators move the page to the other end, delete it, duplicate it, and the first and last as a SET, wherever the document has room and its tree is whole -- *"reversibility: 3649 combinations over 121 editable corpus files; 0 failures"*, model hash restored and the sink's delta empty after every revert, on both presets. **Control:** P10 (a delete undone that restores the model and forgets the sink) red, *"revert left the sink with a non-empty delta"*.

**A-24 · The saved update holds exactly what changed** ✅ *2026-10-30, T2*
**Steps** `ctest -R "pageops.update"`: move, save; read the update.
**Expect** The update holds **only** the page-tree nodes whose `/Kids` changed, the label tree when it changed, and the objects D7-2 repaired; the original is a **byte-identical prefix**; **a move and its undo before a save write nothing** (rule 5). **If it fails** S1.
**Result** Six `pageops.update` cases (`tests/unit/tst_reference_repair.cpp`), each update named from the ORIGINAL bytes read independently: `pages-900.pdf` -- a move inside one node writes **that node alone**; a move of the first page to the end writes **the two nodes and that page** (its `/Parent`), not the nodes between; a delete writes the node that lost the page and every `/Count` above it; the saved files read back in the order asked, the original a byte-identical prefix. `refs-every-kind.pdf` -- a move writes the page-tree node and the catalog (its labels), and **no reference**. A move, a delete and a duplicate undone before a save write NOTHING, the composed bytes equal to the original, on three files. A duplicate is a new page object whose annotations are new objects naming it as `/P`, its widget not copied and reported, no `/StructParents`; undone after a save it leaves the tree, and REDONE it is listed again as the SAME object. **Design fixed by this case:** the first composition filled the tree's page slots in order, so moving page 0 to the end rewrote all nine nodes of `pages-900.pdf`; it now keeps the longest run of pages already in order where they are (`PageOps::composeTree`). **Controls:** P9 (every page re-listed) red; P6 (a copied annotation's `/P` left on its source) red -- refused at the save's own verify before any file was replaced.

**A-25 · `qpdf --check` over every file a page command saves** ✅ *2026-10-30, T2; the log read at each push*
**Steps** The `portable-asan` job's `check-qpdf.py` step; **read the log** (rule 3).
**Expect** *"No syntax or stream encoding errors found"* for every Stage 7 output, each named; the file count re-derived. **If it fails** S2.
**Result (T2)** Every file `tests/unit/tst_reference_repair.cpp` saves is kept as `written/pages-NN.pdf` beside Stage 6's, which the job's existing step judges. Run **40995950997** on `2da9e8e`, read from the job's log 2026-10-30: `pages-01.pdf` … `pages-08.pdf` each *"No syntax or stream encoding errors found"*, and *"qpdf check: 79 file(s), 0 failed; the truncated-stream control rejected"* -- the eight over run 40875904825's 71. Later tasks' outputs are read at their own pushes. **T5 (2026-10-31):** every file `tst_copier` saves -- inserts, a replace and its undo, blank pages, the layers' union -- is kept as `written/copies-<line>.pdf`, **11 files**. Run **41180314162** on `bcfbb1b`, read from the job's log 2026-11-04: `copies-453.pdf` … `copies-731.pdf`, all eleven *"No syntax or stream encoding errors found"*, and *"qpdf check: 90 file(s), 0 failed; the truncated-stream control rejected"* -- the eleven over T2's 79; the same job ran `tst_copier`'s cases and `unit.fuzz.assembly_replay` under ASan + UBSan, 1048/1048, no `runtime error` in the log. **T6 (2026-11-04)** Run **41758087362** on `b3083f9`: *"qpdf check: 210 file(s), 3 failed"* -- T2's and T5's files all clean, and three of T6's Save As rewrites refused for damage their `malformed/` sources carry (§8 **I-13**, A-122). Run **41770714539** on `d238834`: *"210 file(s), 0 failed, 3 inherited from a source qpdf refuses too"*; runs **41808943466** on `37ec179`, **41824929474** on `f68f0c1` and **41832843136** on `279fe66` the same.

**A-26 · What is refused is refused by key** ✅ *2026-10-30, T2*
**Steps** `ctest -R "pageops.refused"`.
**Expect** A page command on an encrypted document, one removing every page, and one naming a page whose two readings disagree are refused with their keys, the document untouched; a page command on a **repaired** document runs and its first save is Save-As. **If it fails** S2.
**Result** Three `pageops.refused` cases: every page deleted -- `error.edit.noPagesLeft`, the hash and the sink untouched, and all but one accepted (the control); a move, a delete and a duplicate on a document assessed as encrypted -- `error.edit.encrypted` before anything runs; a page whose `/Annots` the readers read differently -- `error.edit.pageReadingsDisagree` with the page named, for a delete and for a move that names it, while a move of ANOTHER page past it runs and the refusal moves with the page (`mayEditPage` and `mayAnnotate` at its new index). **Added by D7-21's T2 amendment:** a page tree whose walk noted a kid that is no reference refuses a move and a duplicate by `error.pdf.pageTreeDamaged`, writing nothing, while a rotation runs. A repaired file (the first of five malformed fixtures the first-party writer opens that needs repair) runs a duplicate, and its save is `SaveAsRequired`; the Save As writes the pages. **Control:** P8 (a refusal left at its old index) red, 3 assertions.

**A-27 · A signed document: warned once, and every signature still verifies** ✅ *OpenSSL half 2026-10-30, T2; the warning 2026-11-07, T8*
**Steps** `ctest -R "a4_product" -V` with a `MovePages` and a `DeletePages` through the product on each of the four `corpus/signed/` files; the warning case in `rawpage_ui_tests`.
**Expect** OpenSSL verifies **all four** after each save, each with one byte flipped first as its control (D3-2's order); the message bar warns **once** per document before its first page command, and **never** on an unsigned document. **If it fails** S1 for a signature broken by the bytes; S3 for the warning.
**Result (OpenSSL)** `render.a4_product.pages.openssl`, a third run of `RunA4Product.cmake`: each signed file has ONE page, so the case makes room before it moves and deletes -- duplicate, duplicate again, move the last copy to the front, delete the other copy -- through the shipped backend, **saved after each**, each save's file a byte-identical prefix of the next. Then *"signature check ok: 8 of 8 controls rejected, 4 of 4 signatures verify"*, each signed range ending where the original did (`approval.pdf`: byte 19370 of 20660). **Result (warning, T8)** ✅ *2026-11-07.* `ui.pages a signed document is warned once ...` over the fake backend with two signatures: nothing before a change is asked; before the first page command the message bar's `Signed` banner names the two and the command RUNS; a second command does not raise it again (the banner is the document's condition, kept); an unsigned document is never warned. Through the product (`ui.composition.pages a signed corpus file is warned once ...`) on `signed/approval.pdf` and `signed/certified-nochanges.pdf`, and an unsigned control. **Controls:** P6 (never raised) and P7 (raised before every command) red.

**A-28 · Recovery replays every Stage 7 command kind** ✅ *2026-10-30, T2 for the three page commands; extended by every task that adds a command*
**Steps** `ctest -R "platform.recovery.kill" -V` with each Stage 7 command in its rotation.
**Expect** After `TerminateProcess`, the replay against the file as opened gives a document whose every page shows the identity it showed before the kill and whose references grade as §3.2; an insert replays **with its source deleted** (D7-6). Where each kill landed is printed. **If it fails** S1.
**Extended (T5, 2026-10-31)** The page cycle gained a fifth step, `InsertPagesCommand` of `pages-900.pdf`'s first 300 pages at the middle, copied from a TEMPORARY COPY of the file deleted as soon as it was copied -- its line over 64 KiB, so a side file (D6-23): *"between apply and the flush: 16; inside apply: 2; inside the flush: 2; most steps replayed: 7"*, each replay's model hash and composed bytes the in-process run's. A replace is not in the cycle: a replace of a page no save has written is refused (D7-28), which a cycle of unsaved steps would meet; its undo on either side of a save is `unit.pageops replace` (A-44).
**Result** `platform.recovery.kill_pages`: the kill probe's new `pages` mode runs `tests/platform/killprobe/PageSteps.h`'s cycle over `refs-every-kind.pdf` -- a move, a duplicate, a delete, a move of a set, each key a function of the step -- and twenty `TerminateProcess` kills land *"between apply and the flush: 11; inside the flush: 9"*, up to 9 steps replayed. Each replay is graded against the SAME steps run in-process on a pristine copy: the model's hash equal, and the update a save would write **byte-identical** (SHA-256) -- the page tree, the labels and every repaired reference -- whose rightness is A-19's and A-20's. **Bound:** the replay is graded by the composed bytes, not by PDFium; an insert's replay without its source is T5's. **Control:** P11 (a duplicate's copies named afresh each run) red, 36 assertions.

### 5.5 How a moved page reaches the screen — A2 at Stage 7 (T3)

**A-29 · Shown is saved after every page command** ✅ *2026-10-30, T3; re-run at T19 through the surfaces (A-119)* — §3.3
**Steps** `ctest -R "composition.assembly_shown"`.
**Expect** §3.3's bar: 0 differing pixels live against reopened, every page, 72 and 144 dpi, and under a 90° view rotation; after undo, 0 against the original. **If it fails** S1.
**Extended (T5, 2026-10-31)** Two steps more -- three pages of `inherit-resources.pdf` inserted, and page 3 REPLACED by a page of `annots-on-pages.pdf` -- each saved and every page compared: **16 + 16 pages × 3 views, 0 differing bytes**, the undo of all six steps every page of the original, 0.
**Result** **0 differing pixels.** `tests/composition/tst_assemblyshown.cpp`, the product composed as `main.cpp` builds it over a scratch copy of `refs-every-kind.pdf`, T2's commands run through the WINDOW's journal -- its show step, its mutation handler -- one after another: the first page to the end, the last two to the front, the first and last duplicated, the second deleted. After each, SAVED through the window and reopened in a fresh engine: every page the live engine draws, through the engine's tree and the model's rotations as the screen asks, against the same page of the saved file, at 72 dpi, 144 dpi and 72 dpi turned 90° -- **51 pages × 3 views, 0 differing bytes**, the page counts equal (12, 12, 14, 13); then all four UNDONE after the saves, and every page against the ORIGINAL corpus file, 0. **Controls:** P13 (`pages()` the tree read at open) red; P14 (the rasterizer reading the opened tree) red; P16 (the engine not told the order) red. **What it does not cover:** the commands are CONSTRUCTED, not made by a surface (none runs one before T8) -- §3.3's *through its surface* is T19's re-run; and they are T2's three: an insert, a decoration and a crop join with their tasks.

**A-30 · A moved page is not drawn again** ✅ *2026-10-30, T3*
**Steps** `ctest -R "render.remap"` on `pages-900.pdf`: render a viewport over pages 10 … 19; move them after page 500; scroll to them.
**Expect** Every one of their tiles **delivered from the cache under its new index**, showing the identity it should (rule 7), and **0 rasterizations of those pages** counted at the engine. **The control:** a planted `Scope::wholeDocument()` for moves makes the count non-zero. **If it fails** S2 — and S1 if a tile shows another page's identity.
**Result** `tests/render/tst_remap.cpp`, the shipped backend and journal with the product's show step and mutation handler, the shipped scheduler and cache, a counting rasterizer between the scheduler and PDFium: pages 10 … 19 drawn whole at 36 dpi and cached (10 engine calls), moved to 491 … 500 by `MovePagesCommand`, asked for there at the new revision -- **10 of 10 served from the cache, 0 engine calls for those indices**, the engine's total still 10, plus the ONE one-pixel warm-up tile A-33's named work asks for. Each graded by what it shows: the text the reloaded engine extracts at 491 + i reads `page 11 + i of 900`, the tile equals the one drawn before the move byte for byte, and equals the SAVED file's page at that index reopened fresh; the page the move shifted into 10 differs from the old page 10 (the control that the comparison tells pages apart). **The guide's control, P15** (`MovePagesCommand::scope()` answering `wholeDocument()`), red; P1 (the memory tier not remapping) red here and in the unit case. The headless half, `tests/unit/tst_remap.cpp` over a fake engine with an ORDER: the cache carries every tile of a kept page to its new index at the new revision and drops a removed page's; the scheduler moves a QUEUED tile with its page -- the three queued pages delivered at their new indices, the removed one never drawn -- and DROPS the one that was running when the command landed, which the fake drew after the reload at its old index (another page's pixels); P2 (queued tiles kept at their old index) red, P3 (running tiles re-keyed, the tempting optimization) red.

**A-31 · Every index-keyed consumer is told** ✅ *2026-10-30, T3*
**Steps** `ctest -R "remap"` across `rawpage_tests`, `rawpage_render_tests` and `rawpage_ui_tests`.
**Expect** After a move: a find for a moved page's identity hits it at its NEW index **with 0 re-extractions** counted; `AnnotationStore` lists its annotations at the new index; the find bar's hit, the view's current page, a split pane's position, the view history (Back returns to the page, not the index) and **both panes' selections** follow it; a removed page leaves none of them pointing at its index. **The control:** each consumer's remap planted out in turn reddens its own case and no other. **If it fails** S1.
**Result** **The text** (`tests/unit/tst_remap.cpp`): every page of a 20-page document read by a search, pages 3 … 5 moved to the end -- a find for their identity hits each at its NEW index, **0 extractions** to find it (20 stay 20), every stored model's index the page's new one; a removal drops its text and the count, a duplicate's copy is read when asked (one extraction); the `WholeDocument` control reads all 20 again. A read that FINISHED before the command and was delivered after it is refused by its EPOCH and read again at the page's new index, the old pages' text never filed under the new pages' numbers; a read RUNNING across the command is cancelled and its owner gets ITS page at the new index; a search running across it is ended, never finishing with the old numbering; the index re-reads its bookmarks and a moved page's units and keeps its attachments. A selection follows its pages while they are still a run, and is cleared when one is removed, the run is broken or a copy lands inside it; `Ctrl+A` stays the whole document; the find bar's hits follow their pages in document order, a removed page's going. **The product** (`tests/composition/tst_assemblyshown.cpp`, `pages-900.pdf`, a split unsynchronized): the find bar's hit, its overlay copy, the primary pane's page (100 → 10, by the identity the engine shows there), the second pane's (50 → 51), the thumbnails' selection, a text selection, and Back (40 → 41, `page 41 of 900`) all follow; the presenter's stale tiles are where their pages now stand before any event is pumped; then the moved page DELETED -- the hit and the selection on it gone, the panes on the page that took its place (`page 11 of 900`), no history entry naming it, both panes 899 pages. `refs-every-kind.pdf`: every bookmark and named destination in both panes opens the page it named after the first page moves to the end. **The bookmarks and destinations** (`tests/render/tst_remap.cpp`): after a move, a duplicate and a delete, every outline item and named destination lands on the page showing the identity it named at open -- the ORIGINAL, not the copy -- or is unresolved when its page was deleted, the search's bookmark units likewise, and every one back where it opened after the three undos. `AnnotationStore` at the new index is T2's (A-17). **Controls (convention 7), each red on its own case:** P4 (models not moved), P5 (no epoch), P6 (the index dropping instead), P7 (the views not told), P9 (the find bar not told), P10 (the presenter keeping its indices), P11 (the selection not carried), P12 (the history not carried), P18 (the outline not carried), P19 (a copy taken for the original), P20 (the panes not handed the carried outline), P21 (the index keeping its bookmarks). **P8 STAYED GREEN** -- the thumbnails pane not told -- and that is a fact about the pane: its selection IS the active view's page (`MainWindow` sets it from the view on every change), so its own remap is redundant until T8 gives it a selection of its own; the case is T8's to grow. **Deviation:** A-31's UI half runs in `rawpage_composition_tests`, not `rawpage_ui_tests` -- it needs the real engine, journal and sink to move a page.

**A-32 · An unsaved rotation survives a move** ✅ *2026-10-30, T3*
**Steps** `ctest -R "remap.rotation"`: rotate page 5 by 90 (unsaved); move it to 2; then undo both in turn.
**Expect** The page drawn turned once — not twice, not zero — at every step, its raster equal to the same page rotated and saved (D3-15's delta now against the revision the engine draws). **If it fails** S1.
**Result** `tests/render/tst_remap.cpp`, over TWO fixtures: `refs-every-kind.pdf` (page 5 rotated, moved to 2) and `inherit-resources.pdf` (page 4, inheriting its `/Rotate`, moved to 0, its neighbours inheriting 90 and 180 from other nodes). At every step the live page equals the page rotated and SAVED alone, reopened fresh -- after the rotation, after the move at its new index (the page now at its old index the one before it, unchanged), after undoing the move, and the original after undoing the rotation; redone and saved, the file draws what the screen drew, and after the save (no reload) still turned once. **Found by its control, before it was a result:** P16 (the engine not told the new order) **stayed green** on the first fixture alone -- every page of `refs-every-kind.pdf` has one size and one `/Rotate`, so a page read from the wrong tree is sized and turned like the right one; the second fixture was added, and P16 went red (A-29 caught it too).

**A-33 · A2 at Stage 7, the bench** ✅ *2026-10-30, T3 — **`go, with named custom work`**, the user's choice* — a **VERDICT**, §3.1
**Steps** `rawpage_bench_pipeline.exe --stage7 > bench-stage7.txt`, three runs; read the dpi and viewport it reports first (`CLAUDE.md` → *Traps*).
**Expect** §3.1's table decides; the figures recorded in `Tech-Stack-Plan.md` §3.3 as *Stage 7's workload*, beside the remap and reload times. **If it fails** — a *no-go* is a result, not a failure: D7-3 changes before T4.
**Re-read (T5, 2026-10-31), the real insert** (D7-19: the stand-in replaced): `InsertPagesCommand` of 100 pages (601 … 700, copied by `copyPages` before the clock, from the file the column runs on -- §3.1's `text-900.pdf` on that file, `pages-900.pdf` itself on the other), three runs at 288 dpi, 1920×1080, nothing else running: viewport p95 **11.17–11.30 ms on `pages-900.pdf`** (viewer alone 9.63–9.76) and **14.86–15.14 on `text-900.pdf`** (13.52–13.76), command-to-pixels p95 **0.069–0.083 ms**, max ≤ 0.19, 0 wrong-identity tiles; the insert's `journal.run` **4.5–5.1 ms**, its first page **to the pixels in 8.5–10.7 ms**. **Every §3.1 bound inside on both files** -- the verdict stands, `go, with named custom work`.
**Result** Phase 8 of the bench (288 dpi, 4.000 px/pt, 1920×1080 at `Visible`, the dpi and viewport printed first): a held `Alt+↓` -- a `MovePagesCommand` of a ten-page range one place down every 100 ms for 30 s -- with a scroll between, then the stand-in insert. **Before the named work (three runs):** command-to-pixels p95 **0.04 ms**, max 2.2–3.3, **0 wrong-identity tiles in 1,800 commands**; but viewport p95 **17.6–18.0 ms on `pages-900.pdf`** (11.2–11.5 with no command) and **21.9–22.3 on `text-900.pdf`** (17.2–17.4 with no command -- that file's own baseline already outside on this rig today). Split by leg: the whole of it QUEUED, the engine's per-tile p95 unchanged and its MAXIMUM 8.8 ms against 1.4 -- the first tile drawn from each freshly reloaded document pays PDFium's cold start, and the scroll's tiles wait it out. **The named work, built** (the user's choice, 2026-10-30): after every page command the scheduler asks for one pixel of the first page it moved at `Background` (`RenderScheduler::documentChanged`), so the reload and the cold start are paid while the engine is idle. **After it (three runs on the final code, nothing else running): every bound inside on both files** -- viewport p95 **10.6–10.7 ms on `pages-900.pdf`** (9.2 with no command) and **14.3 on `text-900.pdf`** (13.1), command-to-pixels p95 **0.04–0.06 ms**, max ≤ 0.8, 0 wrong-identity tiles, `journal.run` p95 6.5–6.9 ms, the remap p95 0.33–0.44, the reload p95 1.3–1.6, the stand-in insert to its pixels 4.0–5.3 ms. **The machine read faster that hour** -- the no-command columns 9.2 / 13.1 against 11.2–12.4 / 17.2–17.5 earlier the same day, no code between them touching them -- so what is compared is what page commands ADD over the no-command column: **+6.1–6.7 / +4.6–5.0 ms before the warm-up, +1.4 / +1.2 after**, the engine's per-tile maximum 3.5–4.5 ms against 8.8. `Tech-Stack-Plan.md` §3.3 records it; P17 (no warm-up) red in the unit and render cases. **Found in the instrument while measuring, and fixed before the graded runs:** the warm-up's pixel is a tile at (0, 0), and the bench first counted it as a wrong-identity tile once (the duplicate's first copy, where the warm-up lands); and the bench read the reload record -- which takes the engine lock -- from its delivery callback, adding the warm-up's engine time to command-to-pixels. Neither is the product's. **Deviation:** the insert is a stand-in, a `DuplicatePagesCommand` of 100 pages (`InsertPagesCommand` is T5's), re-read at T5.

**A-34 · A consumer that cannot remap redraws, never shows stale** ✅ *2026-10-30, T3*
**Steps** `ctest -R "remap.fallback"`: a consumer registered without a remap.
**Expect** It is handed `WholeDocument` and redraws; **0 tiles of the wrong identity** at any point. **If it fails** S1.
**Result** The consumer is the shipped DISK TIER (`core/render/DiskTileStore`), which answers a page command's scope as the `WholeDocument` it is: with every tile held on disk alone (the memory tier given no room), a move drops all six of the document's tiles, remaps none, and a lookup at every index -- at the old revision and the new -- misses, so each page is drawn again and none is ever served another page's tile. The memory tier's control (the same move as a plain `WholeDocument`) keeps nothing either. **What it does not cover:** the disk tier ships OFF (D2-14), so this is the fallback's mechanism, not a configuration a person runs.

### 5.6 The compose stops growing (T4) — Q4 ✅

**A-35 · `journal.run` does not grow with the unsaved update** 🟡 *T4's half 2026-10-31; T5's reading 2026-10-31 -- NOT TAKEN; the bookmark half at T11* — §3.7
**Steps** `ctest -R "builds only what it changed" -V` (`[run-serial]`, convention 25); at T5 and T11 the same over §3.7's real workload.
**Expect** §3.7's bars at 0, 300 and 3,000 unsaved copied objects, for a one-page move and a bookmark rename. **At T4** (the user's choice, 2026-10-31, §3.7): the objects a one-page move and a rotation BUILD -- written anew rather than as the last compose wrote them -- the same at every size, and the times reported. **If it fails** S2, and §3.7 records *not taken*.
**Result (T5, 2026-10-31) -- §3.7's move half: NOT TAKEN.** `render.a one-page move after thousands of unsaved copied objects -- the guide's 3.7 reading` (`[run-serial]`), `text-900.pdf` with **0, about 300 and 3,624** unsaved copied objects (one insert of its first pages -- 140 pages, 292 objects, in the first three runs; 150 pages, 312, in the fourth, the size moved past 300 -- and two inserts of all 900), ten one-page moves each, settled untimed before each. Four runs, nothing else running: `journal.run` p95 **2.01–2.63 / 3.59–5.32 / 23.52–25.78 ms**, max to 32.1; the p95 at the largest size **+20.9 … +23.8 ms** over none, against §3.7's 2 ms, and over 16.667 ms itself. **The cause, named:** a move builds every INSERTED page's dictionary again -- 1 object at 0, 143 and 153 at about 300, **1,803 at 3,624** -- because T4's units (D7-26) cover annotations, listings and shared objects, and a new page is rebuilt from its payload at every compose. The payload's own objects ARE written again as they were (one unit per insert). `Deferred-Work.md`'s `DW-4` records it; the case reports and does not fail (A-35's *not taken is a result*), asserting only that a move with nothing unsaved builds at most four.
**Result (T4)** `tests/render/tst_composegrowth.cpp`, the shipped backend shown and settled as `DocumentWindow` does, `text-900.pdf` with **0, 300 and 3,000** unsaved objects (100 and 1,100 highlights, one a page from page 101 -- a stand-in for T5's copied pages), ten one-page moves and ten rotations each, settled untimed before each: **a move builds 2 objects and a rotation 2 at every size**. *Reported, not graded* (§3.7 is T5's and T11's): three runs, nothing else running: `journal.run` p95 for the move **2.32–2.82 / 2.35–3.03 / 7.55–9.34 ms** at 0 / 300 / 3,000 unsaved objects, max 3.86–5.06 / 4.69–5.04 / 15.66–17.16; for the rotation **1.74–1.85 / 2.01–2.17 / 4.69–5.47**; the verify, off the thread, p95 **4.35–4.93 / 5.33–6.35 / 19.04–20.11 ms** -- against, before T4 (a throwaway probe of the same workload at 1,500 highlights, 3,701 objects), **5.95 ms with nothing unsaved and 49.8 ms after**: 25 ms of compose and 21 of verify, both on the UI thread. **What still grows is not small:** +5.2–6.5 ms for the move at 3,000 objects over none, +2.9–3.6 for the rotation -- the writing of what is reused (the writer about 0.35 µs an object), the block's names asked again, the update copied for the engine. **On this stand-in, §3.7's 2 ms bar would NOT be met**; whether T5's copied pages, one unit per insert, meet it is that task's reading, and the remaining cost is named here so it is not rediscovered. **Control:** P12 (every unit forgotten before each compose) red -- T4's row in `Implementation-Plan.md` §4 lists the plants.

**A-36 · A composition Save could not write is still refused** ✅ *2026-10-31, T4*
**Steps** `ctest -R "verify refuses"`: a planted compose whose verify fails -- refused, since T4, AFTER the run returned.
**Expect** The command refused with `error.edit.notShown`, the model untouched, nothing queued to the engine — D6-2's rule unchanged by the verify moving off the UI thread. **If it fails** S1.
**Result** Five cases. **The journal** (`tests/unit/tst_journal.cpp`, the settle handler the case's own): a run refused at its settle is taken back -- the rotation reverted, the step removed, `mutated` once more with `WholeDocument`, the refusal told once, the recovery recorder told `run` then `undo` -- and the run that settled it is refused with the same error; an undo refused is re-applied, a redo refused reverted; a command inside a group is settled before it returns, as before T4. **Through a real sink** (`tests/unit/tst_composememo.cpp`, `CosBackend` with its verify deferred to the settle): Stage 6 guide A-30's `/Annots` fault planted at the compose and CLEARED before the settle -- the verify checks the seam as it stood when the update was composed -- refused by `error.save.verifyFailed`, the model as opened, nothing shown, the sink's delta empty; the control, the same edit unfaulted, made and shown. **Through the shipped backend** (`tests/render/tst_composegrowth.cpp`, `pages-900.pdf`): a move of page 1 to the end with `/Kids` renamed in its update -- the run returns, the window's call is made when the verify ends, and BETWEEN that refusal and the settle a tile at the refused index finds no engine; settled, the refusal by key, the model and the history as they were, **no reload made and nothing pending**, `page 1 of 900` extracted at index 0 and its pixels as before; the same move unfaulted made and drawn. `refs-every-kind.pdf`: the bookmarks the refused move had re-pointed are back on their pages. And Stage 6's own case (`render.an edit whose composition fails verify is refused by key and the page keeps drawing`, that guide's A-39) now takes its refusal from the settle. **Controls, each red:** P6 (no gate), P7 (no take-back), P9 (the readers not put back), P10 (the old engine left to draw).

**A-37 · Commands and saves wait for a verify in flight** ✅ *2026-10-31, T4*
**Steps** `ctest -R "wait for the verify in flight"` (`[run-serial]`): three commands, a tile and a save issued while a verify runs.
**Expect** Shown and saved in issue order; the save writes the composition that verified; no command shown before the one issued earlier. **If it fails** S1.
**Result** `tests/render/tst_composegrowth.cpp`, `pages-900.pdf`, every verify slowed to 150 ms by the seam (which corrupts nothing and keeps what it was handed): a move RETURNS in under 100 ms with its verify still running; a tile of the moved page asked for then, through the shipped scheduler, is drawn on the render thread only after that verify ended, and shows the moved page (page 1's pixels); the next command waits for it, and the one after waits **150 ms or more**; the log reads **show, verified, show, verified, show, verified** -- none shown ahead of the verify before it; the save waits for the last verify and **writes exactly the file the third show verified**, byte for byte. **Control:** P8 (the verify joined on the caller's thread) red. *Found while writing it:* the tile first came from a bare `std::thread`, which drew index 10 after the NEXT command had swapped in -- the scheduler drops such a job, a bare thread does not -- and the failing pixel `CHECK` on two `QByteArray`s crashed Catch2's reporter (`CLAUDE.md` -> Traps); the case asks the scheduler, and compares digests.

**A-128 · The compose that keeps what it composed writes what a compose from nothing writes** ✅ *2026-10-31, T4* *(added at T4, D7-26)*
**Steps** `ctest -R "writes what a compose from nothing writes"`.
**Expect** Over every editable corpus file, after each operation of a script -- two highlights, a typed comment (a FONT, the shared object), a created annotation moved, one of the file's moved twice, a rotation, a move, a duplicate, a delete, the first annotation deleted, every undo, three redos, a save and three operations after it -- the update the show composed, a second compose and a compose with everything composed FORGOTTEN (the book kept, so at the same numbers) are the same bytes; and no step is refused by its verify. **If it fails** S1 -- a stale object is a page or a comment drawn as it was.
**Result** `tests/unit/tst_composememo.cpp`, headless over `CosBackend` (so under `portable-asan` too): **2,854 operations over 121 editable corpus files; 0 differ**. **It found two defects before it was green:** T4's own first draft ended the book's round AFTER the writer had chosen `/Size` and the free rows, so after a deletion the update shown was not the next compose's -- the round now ends with `delta()` (P4 plants it back: 497 differ); and **I-03** (§8), older than T4. **Controls, each red:** P1 (the epoch not moved by a deletion), P2 (a listing reused uncompared), P3 (a created annotation reused uncompared), P4 (the round ended after the write), P5 (an edit reused uncompared -- green until the script moved one of the file's annotations twice, so that its edit changed while its unit was held). P1 and P2 were first REFUSED by the verify rather than compared by the case -- the safety net working -- so the case now counts a refusal by `error.edit.notShown` as a failure.

### 5.7 Pages from another document (T5)

**A-38 · A copied page draws as it drew** ✅ *2026-10-31, T5* — §3.4
**Steps** `ctest -R "render.copier"`.
**Expect** §3.4's bar over every page of every editable corpus file. **If it fails** S1.
**Result** `tests/render/tst_copierengine.cpp`: every page of every corpus file but `encrypted/`, copied by `copyPages` and inserted after the one page of a blank target through the SHIPPED backend and journal, SAVED, reopened fresh and drawn against its source opened alone, with annotations: *"render.copier: 118 files, 3114 pages compared in 3828 renders, 13 pages with a widget skipped, 0 differ"* -- refused by key or not opening: `merge-not-a-pdf.pdf` and six damaged `malformed/` files (`pages-loop.pdf` by `error.pdf.pageTreeDamaged`). **Bounds, stated by the case (convention 19):** the 13 pages carrying a widget, which by the user's choice stay with their fields until T10 and so draw without them; at 144 dpi a file over 200 pages is drawn at every ninth page (`pages-900.pdf`, `text-900.pdf`). **It found four defects before it was green:** I-06 and I-07 (§8); and in T5's own code, an annotation listed by two copied pages listed by the first alone, and then the verify refusing that copy listed twice (its `/P` names the first page) -- both fixed. ~100 s, the longest case in the suite. *Engine half of A-42:* `render.copier a copied link lands on the copy of its page` -- a link copied with its page lands, in the saved file, on the page whose text is the source page's. **Controls:** P10 (a second listing dropped) red; P1 red in A-40's case.

**A-39 · What a source shares, the copy shares** ✅ *2026-10-31, T5*
**Steps** `ctest -R "unit.copier.shared"` over `inherit-resources.pdf` and `pages-900.pdf`.
**Expect** Two pages sharing a font and an image, copied in one call: the font and the image written **once**, counted in the target; copying all 900 pages of `pages-900.pdf` writes no object twice. **If it fails** S2.
**Result** `unit.copier shared: ...`: for every pair of pages a resource is the same object in the copy exactly when it is in the source -- read up the source's `/Parent` chain, the reading the copier does NOT use -- *"inherit-resources.pdf -- 5 pages, 19 objects in the copy, 17 shared resource pairs; pages-900.pdf -- 900 pages, 1815 objects in the copy, 68640 shared resource pairs"*; the copy holds no more fonts and images than the source. **Control:** P3 (every reference a new object) red.

**A-40 · Inherited attributes are materialized** ✅ *2026-10-31, T5*
**Steps** `ctest -R "copier.inherit"` over `inherit-resources.pdf` and `rotate-inherited.pdf`.
**Expect** Each copied page carries its own `/Resources`, `/MediaBox`, `/CropBox` and `/Rotate`, equal to the source walk's resolved values, and renders as §3.4. **The control:** a planted copier that copies the page dictionary alone fails §3.4 on those pages — and passes on `pages-900.pdf`, which is why that file is not the fixture. **If it fails** S1.
**Result** `unit.copier inherit: ...` over `inherit-resources.pdf` and `rotate-inherited.pdf`: every copied page carries `/Resources`, `/MediaBox` and `/Rotate` of its own (and `/CropBox` exactly where the walk resolved one), equal to the walk's values, its resources naming what the source's resolved up `/Parent` names; the control that these fixtures DO inherit counted. Drawn: A-38, 0 px on both. **The guide's control, P1** (the page dictionary copied alone) red.

**A-41 · The copy stops at the page tree, and at its budget** ✅ *2026-10-31, T5*
**Steps** `ctest -R "copier.bounds"`.
**Expect** No source page-tree node and no source catalog in the copied set; a graph past the object budget refused by key, the target untouched. **If it fails** S2.
**Result** `unit.copier bounds: ...`: page 4 of `refs-every-kind.pdf` copied -- one catalog, one node, one page, the payload's own, no outline or structure root, fewer than a quarter of the source's objects; a page whose `/Resources` names the catalog, the tree and another page has all three NULL in its copy; ten pages of `pages-900.pdf` over a budget of five objects refused by `error.copy.tooLarge` (the control: within the default budget, copied); a set empty, descending, past the end or negative refused by `error.pdf.pageOutOfRange`. **Control:** P2 (no stop at the tree) **stayed green** against the corpus page alone -- nothing there reaches the tree but `/Parent` and links, both handled before the general rule -- so the hostile page was added, and P2 went red.

**A-42 · Annotations come with their page, and only what can follow them** ✅ *2026-10-31, T5*
**Steps** `ctest -R "copier.annots"` over `annots-on-pages.pdf`.
**Expect** A note and its pop-up, a reply and its parent, copied together, still name each other; a reply whose parent stayed behind loses `/IRT`; a link to a page copied in the same call points at the copy, **by the identity it shows**; a link to a page left behind keeps its rectangle with no action, and is reported; `/StructParent(s)` gone. **If it fails** S1.
**Result** `unit.copier annots: ...` over `annots-on-pages.pdf` pages 1 and 2: each copied page lists a note, its pop-up, a reply and a link, every one an object of its own with `/P` the copy and no `/StructParent`; the note and its pop-up name each other, the reply names the note; page 1's link to page 2 points at page 2's COPY, page 2's link to page 3 (left behind) has no `/Dest` and no `/A`, its rectangle kept, and is reported (`Link`, *cleared*, source page 2); the two widgets not copied and reported (`WidgetNotCopied`, the user's choice); the control, pages 2 and 3 copied together, clears only page 3's link. By what the target page shows: A-38's link case. **Controls:** P4 (a link left behind kept) red, P5 (a widget copied) red.

**A-43 · An encrypted source is refused** ✅ *2026-10-31, T5*
**Steps** `ctest -R "copier.encrypted"` over `corpus/encrypted/`.
**Expect** Refused by key, nothing copied. **If it fails** S2.
**Result** `unit.copier encrypted: ...`: every file of `corpus/encrypted/` refused by `error.pdf.encrypted`, nothing produced.

**A-44 · Insert and replace carry the copied graph, and recover without the source** ✅ *2026-10-31, T5*
**Steps** `ctest -R "serialize:InsertPagesCommand|revert:InsertPagesCommand|serialize:ReplacePagesCommand|revert:ReplacePagesCommand"`; the A-28 rotation with the source deleted before the replay.
**Expect** A payload over 64 KiB is a side file named by its SHA-256 beside the journal, written by `RecoveryJournal`'s existing write (the waiver census unchanged, A-09); the replay equals the document before the kill. **If it fails** S1.
**Result** The lines: A-23's extension. Recovery without the source: A-28's extension, a 300-page payload in a side file named by its SHA-256 written by `RecoveryJournal`'s existing whole-file write (the census 15, A-09). The undo on either side of a save: `unit.pageops insert: ...` -- an insert of two annotated pages saved, undone and saved (the pages gone), redone and saved: **the same two page objects**, their four annotations each, the link between them in the file; an insert and its undo before a save write nothing -- and `unit.pageops replace: ...` -- page 4 of `refs-every-kind.pdf` replaced: the SAME page object (D7-28), the payload page's `/MediaBox` and content, no `/StructParents`, its own annotations and the four copies; undone after the save, its dictionary **value for value the original's**; redone, the first save's again; a replace of a page no save has written refused by `error.edit.pageNotSaved`, and of two pages by a one-page payload by `error.edit.replaceCount`. **Controls:** P6 (the dictionary not written back after a save) red -- its first run found the key order differing, fixed in place; P11 (an undone insert forgetting its pages) red.

**A-45 · Blank pages of a chosen size** ✅ *the size given 2026-10-31, T5; `documents/defaultPageSize` 2026-11-07, T8*
**Steps** `ctest -R "pageops.blank"`.
**Expect** Inserted at the size given, or `documents/defaultPageSize` when none is, with no content stream (an empty `/Contents` or none), shown blank. **If it fails** S3.
**Result (T5)** `unit.pageops blank: ...`: `blankPages(595 × 842, 2)` inserted at 2 of `refs-none.pdf`, saved: two pages `/MediaBox [0 0 595 842]`, no `/Contents`, an empty `/Resources`; a side of 2 or of 14,401 points refused by `error.pdf.pageSizeOutOfRange`, a count of 0 refused. **Result (T8)** ✅ *2026-11-07.* `documents/defaultPageSize` declared in the Documents group (`core/doc/PaperSize.h`: A4, Letter, Legal, A3, A5): A4 by default, Legal written and read back, every token round-trips and an unknown one is refused (`unit.pageops.blank documents/defaultPageSize ...`); the Insert Pages dialog STARTS at it and a landscape Letter is 792 × 612 (`ui.pageops the insert dialog ...`); through the product with the key set to A5, the dialog is offered A5 and the blank pages asked of the backend are 420 × 595, and a file's range `3-` is read against the FILE's four pages, so pages 3 and 4 are copied (`ui.pageops.blank the insert through the product ...`). **Control:** P11 (a file's range read against the open document's count) red.

**A-46 · `fuzz_assembly`, replayed here and run on Linux** ✅ *Windows replay 2026-10-31, T5; the 60 s log and the soak's 2026-11-04, from run 41832843136's and soak 41833219442's logs; re-read at T19*
**Steps** `ctest -R "fuzz.assembly_replay"` (the body over the corpus and the adversarial inputs, headless); the `portable-fuzz` job's log; the soak's.
**Expect** No crash, no hang beyond the body's time bound; 60 s and ten minutes with no finding, read from the **logs**. **If it fails** S1 for a crash; each finding gets a regression case before it counts as fixed.
**Result (Windows replay)** `unit.fuzz.assembly_replay: ...` -- `tests/fuzz/AssemblyFuzzBody.h`, the target's body: *"199 inputs, 123 reached the copy"* (every corpus PDF, each `assembly/` file cut at a half, a third and a fifth, and three hostile graphs -- resources naming the tree, a node listed twice, content naming itself and its page), every invariant held: refusal by key, the payload opening with no repair, one catalog and one node, every reference closed, each page its own inheritable keys, the insert and the replace composed and VERIFIED and undone to nothing.
**Result (the 60 s log, 2026-11-04) -- NOT RUN, raised as I-09.** Run **41180314162** on `bcfbb1b`, T5's push, read from its `Portable libFuzzer` log: configure counted *"fuzz_assembly (6, as expected)"* and the build linked `bin/fuzz_assembly`, and **no step ran it** -- the job fuzzed the other five, 60 s each, with no finding, and `fuzz-soak.yml`'s matrix lists the same five, so the four daily soaks since (41189182957 … 41622024204) did not run it either. Both workflows now run it, and `gates.fuzz_workflows.*` refuses a target either omits; **the 60 s half is read at the fix's push.**
**Result (the 60 s log, T6's push) -- RAN, and found I-12.** Run **41758087362** on `b3083f9`: `fuzz_assembly` loaded 1,484 seeds -- 1,462 from a `corpus/malformed/` the job's lexer and parser steps had written into -- and stopped on its first pass over them, *"the payload holds the source's tree"*, the input kept nowhere (§8 **I-12**, fixed, with the job's directories and artifacts corrected and a gate). **The 60 s with no finding is still owed**, at the next push.
**Result (the 60 s log, run 41770714539 on `d238834`) -- RAN, and found I-12 again.** Seeded from the 46 checked-in files (22 `assembly/`, 24 `malformed/`), it stopped on a mutation that left a `/Type /Pages` node with no `/Kids` -- a page to the walk, copied saying `/Pages` -- and the input was KEPT this time; fixed (§8 **I-12**). The job's five other targets each ran 60 s with no finding.
**Result (the 60 s log, run 41808943466 on `37ec179`) -- RAN, and found I-12 a third time**, about 9 s in: a page's `/Annots` naming the tree node above it, copied as an annotation; the input kept, fixed (§8 **I-12**). The five other targets again ran 60 s each with no finding.
**Result (the soak, run 41816834053 on `37ec179`, 2026-11-04) -- RAN for the first time, and found I-12** about 5 s into its 600: the same `/Annots` path the push's run found, the input kept (`fuzz-soak-fuzz_assembly`), and green against the fix (§8 **I-12**). The soak's five other targets ran their ten minutes with no finding.
**Result (the 60 s log, run **41832843136** on `279fe66`, 2026-11-04)** ✅ *"Done 11347 runs in 61 second(s)"*, no finding and no sanitizer line -- the first push since I-09's fix whose `fuzz_assembly` ran its 60 s through. The five other targets 60 s each with no finding (lexer 140,031 runs, parser 141,552, round trip 2,425, text 5,015, annotations 3,362).
**Result (the soak, run **41833219442** on `279fe66`, 2026-11-04)** ✅ dispatched by hand at 600 s: *"Done 92800 runs in 601 second(s)"*, *"no sanitizer line in fuzz_assembly's log"*; the five others ten minutes each with no finding (lexer 1,382,973 runs, parser 1,009,309, round trip 11,659 in 604 s, text 25,805, annotations 39,966). What the two claim, and only this: libFuzzer, seeded from `corpus/assembly/` and `corpus/malformed/`, drove the body for that long under ASan + UBSan and found no input breaking an invariant `tests/fuzz/AssemblyFuzzBody.h` checks -- not that the copier is correct, and each run is one independent draw.

### 5.8 Save As that rewrites (T6) — D7-25

**A-122 · Save As writes one revision of what is reachable, and loses nothing** ✅ *T6 2026-11-04; the `qpdf` half ✅ from run 41770714539's log (I-13 closed); re-run at T19* — §3.9
**Steps** `ctest -R "fullwriter"` over every editable corpus file, each after a `MovePages` and an `AddAnnotation`; the `qpdf` step's log.
**Expect** §3.9's bars for every file. **The control (rule 5):** a planted writer that skips one reachable object type (`/XObject` resources) fails the raster bar on the image fixtures. **If it fails** S1.
**Result** ✅ *2026-11-04, T6.* `unit.fullwriter every editable corpus file Saved As after a move and a highlight is one revision of what it reaches`: *"122 files Saved As -- 118 rewritten, 16739 objects kept, 25 dropped; 4 signed, kept by appending; 0 failures"* (not editable by the first-party writer: `merge-not-a-pdf.pdf`, `objstm-corrupt.pdf`, `truncated-mid-stream.pdf`) -- each rewrite opened with no repair, ONE section and no `/Prev`, what it holds exactly what it reaches and exactly what the composed file reached (a walk of the case's own, not the writer's), every object value for value and generation for generation the composed file's, every dropped number a free row at its generation, `/Size` above every dropped number. The raster bar: `render.fullwriter every page of every Save As that rewrote draws as the save it replaces`: *"118 files Saved As, 114 rewritten, 7708 renders, 0 differ or refused"* -- every page at 72 and 144 dpi, annotations on, against the APPEND of the same state reopened in a fresh engine (the bound: a file over 200 pages at 144 dpi every ninth page). The four signed files are appended, nobody having been asked (A-126). **Controls:** P1 (every in-use object kept) and P2 (`/XObject` resources skipped) red -- refused by the rewrite's own verify (`unreachable`, `reachable`) before any pixel -- so **P2b**, the same plant with the verify's two reachability checks disabled, is the raster bar's own control: *"26 differ or refused"*, the five the runner kept each a page drawing differently (`decorated-elsewhere.pdf`, `inherit-resources.pdf`, `compare/base.pdf` among them); P3 (a dropped number's free row at generation + 1) red; P10 (Save As appending, as before T6) red. **The `qpdf` half** is the push's: the kept files are `written/rewrite-*.pdf`.
**Result (the `qpdf` half, T6's push).** Run **41758087362**'s `Portable ASan + UBSan` log: *"qpdf check: 210 file(s), 3 failed"* -- every rewrite clean but the three of `malformed/bad-flate-stream.pdf`, `duplicate-object-number.pdf` and `pages-loop.pdf`, each refused for the damage its source carries (§8 **I-13**). The rewrites draw every page as PDFium draws their ORIGINALS, which a new case now holds every file to: `render.fullwriter an unedited Save As draws every page as the original drew it` (`[I-13]`), *"118 files Saved As unedited, 7708 renders, 0 differ"* -- the oracle the source before anything was written, not the append (P2b red in it). Those rewrites are now kept as `written/rewrite-malformed-<name>.pdf` and judged with `--source`, a failure accepted only where qpdf refuses the source too.
**Result (the `qpdf` half, run 41770714539 on `d238834`)** ✅ *"qpdf check: 210 file(s), 0 failed, 3 inherited from a source qpdf refuses too; the truncated-stream control rejected"* -- every other rewrite *"No syntax or stream encoding errors found"*, and each of the three refused for the same fault qpdf names in its source.

**A-123 · What is unreachable goes, and the file shrinks** ✅ *T6 2026-11-04*
**Steps** `ctest -R "fullwriter.compaction" -V`: delete 800 of `pages-900.pdf`'s pages; Save, and Save As, to two scratch files.
**Expect** The rewrite holds **no** object unreachable from its trailer (counted); it is smaller than both the incremental save and the original, all three sizes printed. **The control:** a planted writer that keeps every in-use entry reachable or not fails the count. **If it fails** S2.
**Result** ✅ *2026-11-04, T6.* `unit.fullwriter.compaction ...`: *"pages-900.pdf, 800 pages deleted -- original 126451 bytes, save 126960 (1629 objects unreachable), Save As 76241 (0 unreachable, 219 objects)"*: the incremental save GREW the file and kept 1,629 unreachable objects (the control the case asserts), the rewrite holds none and is 40 % smaller than either. **Control:** P1 (every in-use object kept) red; P10 red.

**A-124 · Numbers kept: undo, identities and recovery survive a Save As** ✅ *T6 2026-11-04*
**Steps** `ctest -R "fullwriter.identity"`: annotate, move pages, Save As; undo every step, redo every step; then the A-28 rotation with a kill after the Save As; then `documents/saveMode = full` and `Ctrl+S`.
**Expect** Every surviving object's number and generation equal before and after; each undo and redo after the Save As rasterizes as the same step did before it (0 px); every annotation identity resolves; the replay after the kill equals the document before it; with `full`, `Ctrl+S` rewrites as Save As does. **If it fails** S1.
**Result** ✅ *2026-11-04, T6.* `unit.fullwriter.identity ...` over `assembly/refs-every-kind.pdf`: a highlight, a move and a two-page delete (the highlighted page among them), Saved As -- every surviving object at its number and generation, value for value; the deleted pages gone from the file; then every undo reaching its step's model hash and composing (what the rewrite dropped written back, D7-29), saved by appending, and the file's pages the original objects in the original order, each dictionary but `/Parent` and every annotation it lists value for value the original's; every redo after it; then `documents/saveMode = full` -- `Save` rewrote, one section, nothing unreached, two retired revisions held. The pixels: `render.fullwriter.identity ...` -- each undo and redo after the rewrite drawn, live, as its step drew before it (0 differ, 252 renders), which found **I-10** (§8). Recovery: `platform.recovery.kill_rewrite ...` (`rawpage_killprobe rewrite`, a mode added for it) -- the page cycle, a Save As that rewrote, then twenty kills: *"between apply and the flush: 12; inside apply: 2; inside the flush: 6; most steps replayed after the rewrite: 8"*, the journal naming the rewrite each time, the probe's rewrite and the case's own byte-identical, and every replay equal -- by model hash and by the composed bytes -- to the same steps run over that rewrite opened fresh. Fresh, not the model carried across the save, and measured why: after an APPEND save as after a rewrite, a model carried across it differs from a fresh reading of its file in two ways neither is the replay's question -- the headless backend reads no `/PageLabels`, and a created annotation keeps its `/NM` identity in its own session where a reading names it by its object. The `Ctrl+S` half through the shell: `ui.documents/saveMode full makes Save rewrite ...`. **Controls:** P4 (no revival) and P5 (no retired fallback) red; P7 (I-10's fallback removed) red in the raster case; P11 (a rewrite's save not telling the recovery journal the document moved) red in the kill case. **Not covered** (stated at the kill case): an undo, after a save, of a step from before it, replayed by recovery -- which does not replay for a page delete after any save, **I-11** (§8).

**A-125 · Save a Copy leaves the tab where it was** ✅ *T6 2026-11-04*
**Steps** `ctest -R "ui.save_a_copy"`.
**Expect** The copy written by the rewrite at the chosen path and passing A-122; the tab keeps its own path, title and unsaved state, and a later `Ctrl+S` writes to its own file. **If it fails** S2.
**Result** ✅ *2026-11-04, T6.* `ui.Save a Copy writes the rewrite elsewhere and the tab keeps its file, title and changes` (`file.saveACopy` dispatched, the path answered by the twin): the copy written by the REWRITE form, the tab's path, title and dirty state unchanged, its own file untouched; a later `file.save` appended to its own file and left the copy as it was. The coordinator's half, over a real file: `unit.fullwriter.copy ...` -- the copy one section with nothing unreached, the journal dirty and on its own path, a later Save appending to the original. **Control:** P9 (the copy adopted) red in both.

**A-126 · A signed document asks; an encrypted one is copied** ✅ *T6 2026-11-04*
**Steps** `ctest -R "fullwriter.signed|fullwriter.encrypted|a4_product" -V` over the four `corpus/signed/` files and `corpus/encrypted/`; the question answered each way, and once with no user.
**Expect** *Keep the signatures*: copied and appended, and OpenSSL verifies all four, each with one byte flipped first as its control; *rewrite*: written, the question having named the signatures as becoming invalid; **no user: the signatures kept** (convention 16). Encrypted: copied and appended, the output's prefix byte-identical to the original. A **repaired** document's forced Save As now rewrites, and the output opens with **no repair**. **If it fails** S1.
**Result** ✅ *2026-11-04, T6.* `unit.fullwriter.signed ...` over the four `corpus/signed/` files: each counts its signature (`pdf/ops/SignatureFields`, D7-20's reading); *cancel* writes nothing and leaves no temporary file, the question having been handed the count; *keep* appends, the opened bytes a prefix; *rewrite* writes one section; nobody to ask appends. And an unsigned file is never asked (the control). `render.a4_product.saveas.openssl`: a duplicate then a Save As with nobody to ask, on each signed file, and OpenSSL *"8 of 8 controls rejected, 4 of 4 signatures verify"*, each signed range ending where the original's did (`approval.pdf`: byte 19370 of 19768). The question through the shell: `ui.Save As asks before a rewrite breaks a signature ...` -- each answer writing what it says, *cancel* reporting no failure. Encrypted: `render.fullwriter.encrypted ...` -- the one `corpus/encrypted/` file the engine opens with no password Saved As, byte for byte the original, the document then living at the copy. Repaired: `unit.fullwriter.repaired ...` -- *"12 repaired files rewritten"*, each opening with no repair, one section, nothing unreached. **Controls:** P6 (signatures ignored) red; P8 (the encrypted copy short) red.

### 5.9 New documents: extract and split (T7) — Q1 ✅

**A-47 · Every new document opens everywhere it can be checked here** ✅ *T7 2026-11-05 (the `qpdf` half from the next CI run's log); re-run by T10 and T16*
**Steps** `ctest -R "assembler"`; the `qpdf` step's log.
**Expect** Each output: **one** revision (one `xref`, no `/Prev`), a fresh `/ID`, PDFium opens it with **no repair**, every page as planned and §3.4's bar against its source page; `qpdf --check` clean. **If it fails** S1.
**Result** ✅ *2026-11-05, T7.* **First-party** (`unit.assembler every corpus file assembled whole and in part ...`): every corpus file but `encrypted/`, opened by `pdf/ops/DocumentAssembler::Source` and made whole, at its odd pages and at its last page, each read back by the first-party reader against its SOURCE read the same way -- *"A-47: 121 files, 253 new documents, 121 kept for qpdf; 4 refused by key"* (`merge-not-a-pdf.pdf` `error.pdf.noObjects`, `objstm-corrupt.pdf` and `truncated-mid-stream.pdf` `error.pdf.noPageTree`, `pages-loop.pdf` `error.pdf.pageTreeDamaged`), **0 failures**: one section and no `/Prev`, a two-part `/ID` not the source's, nothing held unreached, the page count, each page's rotation, media box and content-stream bytes its source page's. `unit.assembler a 900-page document is a balanced tree ...`: at 1, 32, 33 and 900 pages every node lists at most 32 kids, every page at one depth, every `/Count` and `/Parent` true, the objects numbered from 1 with no gap; two documents of the same pages have two identifiers. **Through the engine** (`render.assembler every new document opens with no repair and draws as its source`): every file the shipped backend opens, its snapshot assembled whole and opened from its bytes as an extract is -- *"118 files, 714 pages compared at 72 dpi, 13 pages with a widget skipped, 0 differ"*, each with an EMPTY repair report and refused for editing by `error.edit.unsavedAssembly`; seven refused (six the engine does not open, `pages-loop.pdf` by key). **`qpdf`**: the first-party sweep keeps each whole-file document in `written/` (`assembly-NNN.pdf`, and `assembly-malformed-<source>.pdf` for a damaged source, whose failure `check-qpdf.py --source` accepts only where qpdf refuses the source too -- I-13's rule, `ci.yml` given the prefix); read from the next push's `portable-asan` log. **Controls:** P1 (no balanced tree), P2 (the `/ID` not fresh), P3 (verify skips the objects) and P14 (a page left naming the payload's node as its parent) red -- P14 first-party only: PDFium walks `/Kids` and never reads a page's `/Parent`, so the engine's split cases stayed green under it, which is why the balanced-tree case checks every `/Parent` itself; and `unit.assembler verify refuses what it was not built to be` -- a content byte flipped, a second revision appended, another document's three pages -- each refused.

**A-48 · Extract: to one, per page, and with delete** ✅ *T7 2026-11-05*
**Steps** `ctest -R "extract"`.
**Expect** The right pages by identity in each output; *extract and delete* deletes **only after** the write verifies — **the control:** a planted write failure leaves the source document untouched and no file. **If it fails** S1.
**Result** ✅ *2026-11-05, T7.* `render.assembler extract to one and per page ...` over `odd-count-9.pdf`: pages 2, 5 and 9 as one document show `C09-0002`, `C09-0005`, `C09-0009` in order, as PDFium extracts them; pages 1 and 7 as a file each, opened from their paths with no repair, show theirs. **The document as it stands**: `render.assembler extract takes the shipped backend's document as it stands ...` -- a rotation and a move made and not saved, then pages 2 and 5 extracted: the turned page at 90 degrees and the moved page where the move put it, the file on disk untouched (`unit.assembler takes the document as it stands ...` the same over `CosBackend`). **Extract and delete through the product** (`ui.composition.extract and delete removes the pages in one undo step ...`): the new tab holds 2 pages, the source 7, and ONE undo gives back 9. **The control** (`ui.composition.extract and delete a planted write failure ...`): the third of three per-page files corrupted after it is built, two already on disk -- the extract refused, the source 9 pages, clean, nothing to undo, the folder empty; the same choice unplanted writes three files and deletes three pages. The file half alone, `unit.assembler extract a file that fails its verify takes back every file ...`: no file and no temporary left. **Controls:** P5 (a failed verify takes nothing back), P6 (a written file not read back), P10 (the snapshot the file on disk, unsaved edits ignored) and P11 (the delete run before the write) red.

**A-49 · The five splits, graded against the labels** ✅ *T7 2026-11-05*
**Steps** `ctest -R "split"` over `split-blanks.pdf`, `refs-every-kind.pdf`, `tagged-headings.pdf` and `pages-900.pdf`.
**Expect** Every N; at every top-level bookmark; at a chosen level; at blank pages (by the **content box**, so the white-ink page is blank to the eye and **not** blank to the split — its label says which); at a text pattern — each output's first and last identities as labelled; **by size**: every output at or under the bound, except a single page larger than the bound, written alone and reported. **If it fails** S2.
**Result** ✅ *2026-11-05, T7.* Six rules, not five -- `Features.md` §5 lists top-level bookmarks and a chosen level apart, and both are built (D7-30). Through `core::SplitRunner` as the product runs it -- the engine's content box one page per engine task, the search engine's match over the text index -- each written file opened by PDFium from its path with no repair, its first and last identity extracted and compared with the label: **`split-blanks.pdf`** at blank pages *SB-0001…SB-0002, SB-0004…SB-0005, SB-0007…SB-0009* -- the two pages with no object left out, the white-ink page OPENING the third file -- at the label's `headingPattern` and at its top-level bookmarks *SB-0001…page 3 (which shows none), SB-0004…SB-0007, SB-0008…SB-0009*; **`refs-every-kind.pdf`** at top-level bookmarks and at level 2, the starts read from the label's outline; **`tagged-headings.pdf`** at a level-1 heading pattern, the starts its label's level-1 headings; **`pages-900.pdf`** every 250 pages (250, 250, 250, 150, each file's first and last page drawn as the source's, 0 px) and by size under 40,960 bytes -- *"11 files ... 40807 40893 40904 ... 19477"*, 900 pages, and no two neighbouring files would have fit as one. The planning half, `unit.splitplan ...` (seven cases), against hand values and the split-blanks label read as facts; a heavy page alone and flagged. Through the product: `ui.composition.page.split at blank pages writes its files through the product's pumps`. **Controls:** P7 (a blank page written), P8 (bookmarks at every depth), P9 (a pattern splitting after its page) and P13 (a heavy page not flagged) red. P7 first CRASHED the planning binary -- a case read `error()` of a rule that, planted, succeeded -- so its refusal checks now read a key or `(success)` (`keyOf`), and P7 is red without a crash.

**A-50 · Nothing is overwritten, and nothing lands outside the chosen folder** ✅ *T7 2026-11-05*
**Steps** `ctest -R "split.collision"`: a scratch folder holding one of the names a split would write.
**Expect** The split refused before writing anything, the collision named; a directory listing of the scratch root before and after shows only the chosen folder changed. **If it fails** S1.
**Result** ✅ *2026-11-05, T7.* `render.assembler split.collision ...`: a split of `odd-count-9.pdf` into threes over a folder already holding `doc part 2.pdf` -- refused `error.assembly.exists` naming that path, and the listing of the whole scratch root, before and after, IDENTICAL (nothing written, not even the first file); `unit.assembler split.collision ...` the same through the snapshot directly, the existing file's bytes unchanged, plus `../escape.pdf`, `sub/x.pdf`, `..` and an empty name refused `error.assembly.badName` and `x.pdf` beside `X.pdf` `error.assembly.duplicateName`, the listing unchanged after each. A file is written beside its target and MOVED onto it, which a rename does only where nothing is (`unit.assembler write makes every file, read back from its path`: the folder created and holding exactly the two files). **Control:** P4 (a name the folder holds not checked) **stayed GREEN at first in both cases** -- the rename's own refusal, and the write taking back the file it had already made, left the listing unchanged and named the same path, while the promise *before anything is written* was broken. The unit case now counts the files BUILT through the write seam -- none, for a write refused up front -- and P4 is red there; the render case grades the listing and is kept as it was.

**A-51 · An assembled document opens as an untitled tab** ✅ *T7 2026-11-05*
**Steps** `ctest -R "ui.untitled_assembly"`.
**Expect** A tab titled as untitled, its pages shown; `Ctrl+S` asks for a path (Save As); closing asks first; nothing on disk until saved. **If it fails** S2.
**Result** ✅ *2026-11-05, T7.* `ui.composition.ui.untitled_assembly ...`, the product as `main.cpp` composes it over `odd-count-9.pdf`: `page.extract` of pages 2-3 opens a second tab titled `shell.document.untitled`'s text, of 2 pages, DIRTY, not editable (`error.edit.unsavedAssembly`, D7-30), the source unchanged and clean, and the scratch folder's listing unchanged; closing it with nobody to ask is refused `error.close.needsDisposition`; `Ctrl+S` (`file.save`, enabled for it) asks for a path, proposing one beside the source, and the saved file is opened in the tab's place -- an ordinary document, editable, clean, of 2 pages. **Control:** P12 (an assembly not dirty) red -- the close went through unasked. *Not covered (convention 19):* the dialogs are answered through their twins; their widgets are `ui.pageops ...`'s (four cases) and the walker's.

### 5.10 The thumbnails pane edits (T8)

**A-52 · The whole Page Thumbnails pane table, from key events** ✅ *2026-11-07, T8*
**Steps** `ctest -R "ui.thumbnails_edit"`: for each row of `Features.md` §8's **Page Thumbnails pane** table, a `QKeyEvent` sent through `ShortcutBinder`.
**Expect** Each chord runs its command on the **selection** (`Ctrl+R` on a set, D7-13); `Delete`, `Alt+↑`/`Alt+↓`, `Ctrl+Shift+D` one undo step each; `Ctrl+Shift+X` opens extract with the selection filled in; `Ctrl+A` selects every page; the census rows in `tst_keybindings` compare §8 row for row. **If it fails** S2.
**Result** ✅ *2026-11-07, T8.* `ui.thumbnails_edit every row of the Page Thumbnails table runs from its key on the set` (`tests/ui/tst_thumbnailsedit.cpp`), real key events to the focused pane through the window's own binder over a twelve-page editable document: `Ctrl+A` selects 12; `↓` moves a one-page set, `Shift+↓` twice extends it to {3, 4, 5}; `Alt+↓` and `Alt+↑` each ONE `MovePagesCommand` and the set follows its pages ({4, 5, 6}, back to {3, 4, 5}); `Ctrl+Shift+D` one duplicate (15 pages); `Ctrl+R` the dialog offered the SET, one `RotatePageCommand`, one undo step, exactly the set turned 90; `Ctrl+Shift+X` extract handed the set; `Ctrl+C` the set on the clipboard under `application/x-rawpage-pages`, `Ctrl+V` one insert landing after the set's last page; `Ctrl+X` a copy and ONE delete; `Delete` the set with no question; `Enter` the view to the selected page; `+`/`-` the size -- and seven undos restore twelve unturned pages, one gesture each. `ui.thumbnails_edit a set at the end ...`: `Alt+↓` on the last page and `Alt+↑` on the first DISABLED, the key left alone and nothing run; every page selected, `Delete` refused by `error.edit.noPagesLeft` and `Ctrl+X` disabled. The census: `tst_keybindings`' §8 rows unchanged (every row was bound since Stage 1) and green; `tst_shell` and `tst_documenttabs` re-counted 186 registered (+10), `tst_panes`' Stage 4 case of unregistered rows turned round to a census of registered ones. **Controls:** P5 (the set not remapped), P10 (the window counting the engine's pages, not the model's), P12 (a later move one place too far) red; **P13 (a paste landing before the set) STAYED GREEN** until this case checked where the sink was told the paste lands -- it had counted pages only. *Not covered (convention 19):* the fake backend draws no page; what each page SHOWS after a move, a paste or a cut is A-54's and A-55's product halves.

**A-53 · Selection is a set, and follows its pages** ✅ *2026-11-07, T8*
**Steps** `ctest -R "thumbnails.selection"`.
**Expect** Click, `Shift`+click and `Ctrl`+click give the sets they give in a file manager; after a move the set is the same **pages** (D7-3). **If it fails** S2.
**Result** ✅ *2026-11-07, T8.* Mouse events on the viewport: click 3 → {2}, anchor 2; `Shift`+click 6 → {2 … 5}; `Shift`+click 1 → {0, 1, 2} (the anchor stays); `Ctrl`+click 8 adds, `Ctrl`+click 2 removes and moves the anchor; `Ctrl+Shift`+click adds the run from it. The view sent to page 10 leaves a set of several as it was (D7-31(a)); a `Ctrl`+click does not move the view, a plain click does. After `{1, 4}` moved to the end the set is `{10, 11}`; after a delete of 10 from `{0, 10, 11}` it is `{0, 10}`. **Control:** P5 red.

**A-54 · A drag reorders in one step, and copies across windows** ✅ *2026-11-07, T8*
**Steps** `ctest -R "thumbnails.drag"`: drag a set within a pane; drag it into another window's pane of the same process.
**Expect** Within: one `MovePages`, one undo step, the drop position as indicated. Across: one `InsertPages` in the target, **the source unchanged**, the pages showing their identities in the target. **If it fails** S2.
**Result** ✅ *2026-11-07, T8.* Real `QDragEnterEvent`/`QDragMoveEvent`/`QDropEvent`s sent to the target's viewport with the source pane's `dragData()`: within, the marker reads the insertion index while the drag is over the pane and clears on leave; `{1, 2}` dropped before page 5 is ONE `MovePagesCommand`, one undo step, landing at {3, 4}; dropped where they already are, no command. Across, two windows of one process: `{0, 3}` dropped before page 2 of the other is ONE insert there, copied out of the SOURCE (`AssemblySource::copy`), the source clean and unchanged; a drag naming another process's id refused, nothing run. Through the PRODUCT (`ui.composition.thumbnails.drag ...`, `tests/composition/tst_pagesproduct.cpp`): `refs-every-kind.pdf`'s pages 3 and 4 dropped into `odd-count-9.pdf` read **P-0003, P-0004** between C09-0001 and C09-0002 by the live engine; after P-0012 is moved to the front of the source, a second drop takes **P-0012** -- the source as it stands at the drop. **Controls:** P8 (another process's drag accepted) and P14 (the drop copying the target's own pages) red. *Not covered (convention 19):* `QDrag::exec`, the source half, a nested event loop -- M-01 step 4.

**A-55 · Cut, copy and paste of pages — across processes** ✅ *in-process 2026-11-07, T8; the second process `[D7-13]` 2026-11-07, by hand on `windows` with the user's leave*
**Steps** `ctest -R "thumbnails.clipboard"` (in-process); `$env:QT_QPA_PLATFORM='windows'; rawpage_composition_tests.exe "[D7-13]"` with your leave — **it replaces the system clipboard's contents** (Stage 5's A-50). *(The binary was `rawpage_ui_tests` as drafted; the case needs the real engine to read the pasted pages' identities, so it is the composition binary's.)*
**Expect** Copy puts `application/x-rawpage-pages` on the clipboard; paste in a **second process** inserts the pages, showing their identities; cut is copy then one `DeletePages`. **If it fails** S2.
**Result (in-process)** ✅ *2026-11-07, T8.* The payload's form (`unit.thumbnails.clipboard the payload round-trips ...`): `{version, pages, graph}` round-trips a 70 KB graph; refused by `error.clipboard.malformed` -- not JSON, version 2, no version, 0, 1.5, two million or a string of pages, no graph, a graph not base64, a GIF, and a payload past the bound unread; the same shape right read (the control). The snapshot's copy (`unit.thumbnails.clipboard the snapshot's copy ...`): two pages of `refs-every-kind.pdf` with an UNSAVED rotation, through the clipboard's form and inserted into `odd-count-3.pdf` -- the rotation carried, the source untouched. Through the product (`ui.composition.thumbnails.clipboard ...`): pages 1 and 12 copied and pasted after page 1 read **P-0001, P-0012** between P-0001 and P-0002; cut, ONE undo step, the document back to twelve; pasted into another window's `odd-count-3.pdf` at its end, **P-0001, P-0012**. **Control:** P3 (a graph not beginning `%PDF-` accepted) red. **The second process:** `[D7-13]` copies pages 5 and 7 of `refs-every-kind.pdf`, runs this binary again as `[.][D7-13-child]` -- hidden from every other run -- which pastes the clipboard into a copy of `odd-count-3.pdf` and saves, and reads the saved file's pages 4 and 5 for **P-0005, P-0007**. Under `ctest` it SKIPS (offscreen, the clipboard never leaves the process) -- a sixth named skip.
**Result (the second process)** ✅ *2026-11-07, T8, on `windows` with the user's leave and hands off the desktop.* *"All tests passed (14 assertions in 1 test case)"*: the child read this process's clipboard (2 pages), pasted them at the end of its copy of `odd-count-3.pdf` and saved; the saved file, opened by a fresh product here, has 5 pages, its 4th and 5th **P-0005** and **P-0007**. **Its first run FAILED** -- the child never finished inside 120 s -- and the defect was the CASE's: Windows hands a clipboard's bytes over by delayed rendering, asking the COPYING process for them when another reads, and the parent sat in `QProcess::waitForFinished`, which pumps nothing, so it could not answer its own child. The parent now pumps its event loop while it waits, as a person's first RawPage does; the product was not changed. No process was left behind either time.

**A-56 · Every text field keeps its own `Ctrl+X`, `Ctrl+C` and `Ctrl+V`** ✅ *2026-11-07, T8 -- three fields; the bookmark's rename editor is T11's*
**Steps** `ctest -R "thumbnails.clipboard_controls"`: the page box, the find bar, the tool finder and a bookmark's rename editor, each focused.
**Expect** Each edits its text, and no page is cut, copied or pasted (Stage 5's I-04 turned round, rule 5). **If it fails** S2.
**Result** ✅ *2026-11-07, T8.* With PAGES on the clipboard first, so a misrouted `Ctrl+V` would insert them: the status bar's page box, the find bar's field and the tool finder's box, each focused, cut its selected text, pasted it back and copied it -- the clipboard holding the text -- and no page command ran and no copy was made (the journal empty, the document twelve pages). **Bound:** a bookmark's rename editor does not exist until T11 (`F2`, D7-10); this case gains it there.

**A-57 · The badges say what the page carries** ✅ *2026-11-07, T8*
**Steps** `ctest -R "thumbnails.badges"` over `annots-on-pages.pdf` and `forms-acroform.pdf`.
**Expect** Comment, field and link badges exactly on the pages the labels name. **If it fails** S3.
**Result** ✅ *2026-11-07, T8.* The store's reading (`unit.thumbnails.badges ...`, `AnnotationStore::carries`): every page of `annots-on-pages.pdf` carries all three (its labels: a note with its pop-up, a reply, a link, a widget); **`refs-every-kind.pdf` a link on pages 1 … 4 and nothing else** (its labels' `links`, the control that the badge is per page); `forms-acroform.pdf`'s one page fields only (its `PROVENANCE.md` row). The paint (`ui.thumbnails.badges are painted ...`): a page's badge square carries ink, the same square a slot lower on a page with none is the white sheet, and three badges are wider than one. Through the product (`ui.composition.thumbnails.badges ...`): the pane's badges for `refs-every-kind.pdf` exactly the labels', and a move carries the first page's badge to the end with it. **Control:** P4 (links not counted) red.

**A-58 · Embedded thumbnails written and removed** ☐ *T9 -- moved from T8 by `Stage7-Assembly-Plan.md` D7-31(j): `SetEmbeddedThumbnails` is T9's in the plan's §4 and its T9 row*
**Steps** `ctest -R "serialize:SetEmbeddedThumbnailsCommand|revert:SetEmbeddedThumbnailsCommand|thumbnails.embed"`.
**Expect** Each page's `/Thumb` an image PDFium reads back (`FPDFPage_GetThumbnailAsBitmap`) at the size written; removal leaves no `/Thumb`; reversible. **If it fails** S3.

**A-59 · Rotation over a set, a range, odd, even, or one orientation** ✅ *2026-11-07, T8*
**Steps** `ctest -R "rotate.ranges"`; `ctest -R "serialize:RotatePageCommand"`.
**Expect** Each filter rotates exactly its pages; **a journal line Stage 3 wrote still replays** — the command's format grew without breaking its old lines. **If it fails** S2.
**Result** ✅ *2026-11-07, T8.* `unit.rotate.ranges each filter keeps exactly its pages`: over `mixed-page-sizes.pdf` (both orientations, asserted first), odd, even, landscape, portrait and odd-and-landscape each the set computed independently from the page NUMBER and the shown size; a page turned 90 becomes landscape to the filter. `... a set is one command and one undo step ...`: pages 2, 5 and 9 of `odd-count-9.pdf` turned 270 and only they, one step, undone to the original, and the SAVED file's pages read back by a fresh first-party open turned exactly so. `... a sink refusing part-way ...`: the third of four refused, the first two turned back at the sink and the model untouched. `... a journal line Stage 3 wrote still replays ...`: `{"degrees":90,"page":2,"type":"page.rotate"}` read as a one-page command, applied, and a one-page command written back as exactly those bytes; a set written as `pages`. `unit.every Commands.def row round-trips ...` gained three set samples (51 → 54 round trips) and the hostile table seven set lines (both forms at once, empty, descending, repeated, a string, past the limit, no degrees), each refused by `error.command.malformed`; `unit.reversibility ...` gained the page and the last as a set over every editable corpus file. The dialog (`ui.pageops the rotate dialog ...`): the set offered and chosen first and named by its ranges, a range field, the filters; a backward range refused in the dialog; even pages of 1-6 are 2, 4, 6; a filter leaving nothing refused by `error.pages.noneMatch`. **Controls:** P1 (no turning back) and P2 (one page written in the set form) red.

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
| **I-02** | S3 | The fuzz soak built nothing: `ubuntu-latest` moved it to Ubuntu 26.04, whose Clang 21 refuses an unused field in `core/text/FolderSearch.h` under `-Werror` | **Closed** 2026-10-29, soak 40875917394 read from its log |
| **I-03** | S2 | Move a link the file holds, then delete the page it points at: the delete was refused (`error.edit.notShown`), because its compose wrote that link twice | **Closed** at T4, 2026-10-31 |
| **I-04** | S2 | A file referencing an object number past ISO 32000-1's limit: the compose's first new number overflowed (2^63-1, UBSan) or lay past the limit (8,388,608), and an edit was refused by its verify | **Closed** at T4, 2026-10-31, after CI run 41141004141 |
| **I-05** | S3 | `ui.a rotation drops the rotated page from the index and the next search re-reads only it` (Stage 5 guide A-64) failed in CI twice (41141004141, 41758087362): the search read 2 pages where 1 was expected | **Open** -- not reproduced locally |
| **I-06** | S3 | A page dictionary missing its `>>` was read by the first-party parser ON INTO THE NEXT OBJECT, whose `/Contents` then won -- so a copy or a duplicate of the page drew another page | **Closed** at T5, 2026-10-31 (the user's choice: fixed in the parser) |
| **I-07** | S2 | A page inserted from another document drew the content of its optional-content groups that the source HID: the target listed no such group, and a reader shows a group no `/OCProperties` lists | **Closed** at T5, 2026-10-31 (the user's choice: the groups united now) |
| **I-08** | S3 | `render.a 900-page file's open never holds the engine longer than one page's extraction` (Stage 5's T9 bound, `[run-serial]`) failed in two full `x64-release` runs: T5's first, an engine piece of 5.36 ms against an extraction maximum of 3.80, and the one after T6's push, 4.28 against 4.01 -- and T7's full `x64-debug` run, 5.25 against 5.17 | **Open** -- 5 of 5 green alone, all three times. **A fourth red at T8 (2026-11-07)**, its full `x64-debug` run: 13.46 against 4.48 ms, the widest margin yet -- and 5 of 5 green alone again (14.2-14.5 s each). T8 touched no file the case reaches; the mechanism is still not established |
| **I-09** | S3 | `fuzz_assembly` was built, linked and counted by CI, and fuzzed by nothing: neither `ci.yml`'s 60 s steps nor `fuzz-soak.yml`'s matrix named it | **Closed** 2026-11-04 by run 41832843136's log: `fuzz_assembly`'s 60 s with no finding (T6's push and the two after ran it, and so did soak 41816834053, and each found I-12) |
| **I-10** | S2 | After ANY save, a deleted page put back by an undo was drawn on screen as a page the engine never opened -- not as it drew before the delete, and not as Save then wrote it | **Closed** at T6, 2026-11-04 (fixed inside the task) |
| **I-11** | S2 | A recovery replay STOPS at an undo, made after a save, of a page delete from before it -- the deleted page not put back and every later step lost -- after an append save as after a rewrite | **Open** -- unreachable by a person until a surface runs a page command (T8); owner the user's decision |
| **I-12** | S3 | `fuzz_assembly`'s first CI run: a copy's payload held a catalog or a page-tree node of the SOURCE'S -- the copier refused only the trailer's catalog and the nodes and pages its walk met, so a reference reaching an orphan `/Pages` node or an earlier revision's `/Catalog` copied it and every page it lists -- and, found by the next run, a page the walk reads as a page that SAYS `/Type /Pages` was copied saying so -- and, by the run after, a tree node a page's `/Annots` lists was copied as an annotation | **Closed** 2026-11-04, fixed three times (after runs 41758087362, 41770714539 and 41808943466; soak 41816834053 found the third form too) -- run 41832843136's 60 s and soak 41833219442's 600 s with no finding |
| **I-13** | S3 | `qpdf --check` refused three of T6's rewrites -- of `malformed/bad-flate-stream.pdf`, `pages-loop.pdf` and `duplicate-object-number.pdf` -- for the damage each source carries on purpose, which a rewrite keeps as PDFium draws it: the check judged a fault we were given as one we wrote, and no case held a rewrite to what PDFium draws of the ORIGINAL | **Closed** 2026-11-04 by run 41770714539's `qpdf` log: *"210 file(s), 0 failed, 3 inherited"*, each source refused for the same fault |
| **I-14** | S3 | The FIRST unknown font PDFium substitutes in a process draws unlike every later one: the same bytes, opened twice in one process, give two digests | **Open** -- measured, cause inside the engine not established; the user's decision |
| **I-15** | S3 | Every CI job's vcpkg binary cache restored NOTHING -- *"Restored 0 package(s)"* in `Windows x64`, `Portable ASan + UBSan`, `Portable libFuzzer` and every soak job -- so each built every port from source and downloaded its sources, and run 41824929474's `Portable libFuzzer` stopped at configure on gitlab.com's *"response code 429"* for libtiff, before any fuzz target ran | **Fixed** 2026-11-04, pushed in `279fe66`: run 41832843136 and soak 41833219442 each missed and SAVED, one key per image; closed when a later run on a saved image logs the ports restored |

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
with Clang 18 again, so **Clang 21's verdict on the fix is not measured by CI**. *Closed* 2026-10-29 on
`afb6f0d`, read from the logs: soak **40875917394**, dispatched by hand, every target on `ubuntu-24.04`,
`Clang 18.1.3` and `qt6-base-dev 6.4.2`, each about 601 s with no sanitizer line — `fuzz_cos_lexer`
2,857,939 runs, `fuzz_cos_parser` 1,623,517, `fuzz_text_pipeline` 34,687, `fuzz_annot_pipeline` 15,569,
`fuzz_cos_roundtrip` 12,402 (611 s); and CI run **40875904825**, all five jobs green on `ubuntu-24.04`
for the Linux four.

**I-03 (S2) — an annotation the file holds, edited and then repaired, was written twice.** *Found* at T4 by A-128's
first green-going run, on `assembly/merge-a.pdf`: after its link -- the label's *"a link to its last page"* -- was
moved, deleting a page was refused by `error.edit.notShown`, context `error.pdf.writeDuplicateObject`. *Mechanism:*
two writers, one object. `ReferenceRepair` (T2, D7-2) clears the action of a link whose target page a command
removes, into the compose's pending map, written at the end; `composeAnnotations`' edit loop (Stage 6 T5) rewrites
an annotation edited in place straight into the delta. A link both moved and repaired got both, and the writer
refuses an object written twice -- so the verify refused the delete, which no person could then make. *Why no suite
saw it:* T2's reference cases (A-19 … A-21) run each page command over fixtures with references, and Stage 6's edit
cases edit -- **no case ever edited an annotation the repair then had to change**; A-128's script happened to do both,
on the one fixture with a link to a page it deletes. *Fix* (the user's choice, 2026-10-31, inside T4): an annotation
the repair has rewritten in this compose is edited ON the repaired dictionary and goes back where the repair put it
-- one object, written once; and a version the file held is not restored over a repair, the edit then made from the
model instead (`MutationSinkImpl::composeAnnotations`). *Regression:* `unit.a moved link whose target page is
deleted is written once` (`tests/unit/tst_composememo.cpp`): the link moved, its page deleted, saved, and the link
read back where it was moved to with no `/Dest` and no `/A`; and A-128 itself. *Control:* the fix reverted alone
(P11), the regression red by `error.pdf.writeDuplicateObject`; restored, green.

**I-04 (S2) — the number book's first number ignored the object limit.** *Found* by CI run **41141004141** on
`6cf1219` (T4's commit), its `Portable libFuzzer` job: `fuzz_annot_pipeline` stopped on
`pdf/ops/MutationSinkImpl.cpp:325: runtime error: signed integer overflow: 9223372036854775807 + 1`. *Mechanism:*
D7-26's book starts above every number the file references, and T4 computed that start as `largestReferenced + 1`.
The allocator it replaced raised its floor through `ObjectAllocator::reserveThrough`, which ignores a reference past
ISO 32000-1 Annex C's limit (8,388,607 -- Stage 3 guide I-12's rule) because such a reference names nothing a reader
resolves. Without that rule a reference to 2^63-1 overflowed -- undefined behaviour, a wrap to a negative number on
MinGW -- and a reference just past the limit numbered every new object past it, where the reader refuses it, so the
edit was refused by its verify. *Why no suite saw it:* no corpus file references a number past the limit; the fuzzer
built one, and only UBSan sees the overflow -- on this machine it wraps silently. *Fix:* the start is the allocator's
floor raised through `reserveThrough`. *Regression:* `unit.a reference past the object limit does not move where new
objects are numbered` (`tests/unit/tst_composememo.cpp`): two built files, one referencing `8388608 0 R` and one
`9223372036854775807 0 R`, each edited, every new object at or under the limit, and saved. *Control:* the fix
reverted alone (P13), the 8,388,608 file's edit refused by `error.save.verifyFailed`; restored, green. *Graded in
CI:* run **41148688029** on `b4d5ab5`, the fix's push, read from its logs 2026-10-31 -- green in all five jobs; the
regression passed under `Portable ASan + UBSan` (1034/1034, no `runtime error`), and `fuzz_annot_pipeline` ran its
60 s (15,020 runs) with no finding.

**I-05 (S3) — a Stage 5 index case read one page more than it expected, once, in CI.** *Found* by the same run's
`Windows x64` job, the only red case of 1,903: `ui.a rotation drops the rotated page from the index and the next
search re-reads only it` (Stage 5 guide **A-64**), `tests/ui/tst_documentindex.cpp:277`,
`CHECK( uitest::fakeTextExtractions.load() == before + 1 )` with expansion `10 == 9` -- after a rotation of page 1, the
next search extracted two pages, where the case expects page 1 alone. *Not reproduced:* 30 runs of the case alone and
40 more beside six copies of the unit suite on this machine, all green; T3's CI run, **41042157448** on `daee550`,
passed it. *What is known:* the case runs over `tests/ui/FakeBackend.h`, whose `settleEdits` answers at once, so T4's
verify thread and gate are not in its path; the case's selection has read page 1 before the rotation, and the
selection and the search can each ask for page 1 again afterwards. *Mechanism:* NOT ESTABLISHED -- a race between
those two reads is the suspicion, and a suspicion is not a cause. *Severity S3* because what was miscounted is an
extraction, not a result: the search found what it should. *Since:* the next run, **41148688029** on `b4d5ab5`, passed it (1904/1904) -- one
green run says nothing about an intermittent cause. **Again** in run **41758087362** on `b3083f9` (T6's push), the only
red case of 1,943 in its `Windows x64` job: the same line, the same `10 == 9`. *Tried, 2026-11-04:* a 30 ms sleep in
`FakeTextExtractor::extractPage`, ten runs of the case, all green -- so a slow extraction alone does not open the window.
*A candidate path, not a mechanism:* after a search the hit painter asks again for every page on screen that has hits
(`SelectionOverlay.cpp`, `paintSearchHits`' `ensurePage`), and page 1 is on screen and has just been dropped, so a
repaint between the rotation and the case's `before` can ask for it a second time. The next two runs, **41770714539** and
**41808943466**, passed it (1946/1946 each). *Open*; the next CI runs are read for it.

**I-06 (S3) — the parser read an unterminated dictionary on into the next object.** *Found* at T5 by **A-38**: of
3,114 pages copied and inserted, `malformed/unbalanced-dictionary.pdf` page 1 drew differently from its source at
both scales. *Mechanism:* the page dictionary has no `>>`; `pdf/cos/Parser` read past `endobj` into the next object's
dictionary, keeping both `/Contents` entries -- and the LAST wins -- so our reading of page 1 was page 2's content,
while PDFium ends the object at `endobj`. Every write that copies the dictionary as we read it carried page 2's
content: T5's copy, and T2's duplicate before it. *Why the suite missed it:* Stage 2's tolerance case
(`tst_cos_parser`, *"closes and reports an unterminated container"*) asserts the note, not what follows the
dictionary; and no case before A-38 compared a written copy of a malformed page with what PDFium draws of it.
*Fixed* (the user's choice, of logging and bounding it): an `endobj` where a key belongs ends the dictionary, said by
the same `UnterminatedDictionary` note, and is left for `parseIndirectObject` -- PDFium's reading. *Regression:*
`unit.cos parser ends an unterminated dictionary at endobj, as PDFium does` (`[I-06]`), and A-38's sweep, now 0 differ.
*Control:* P9 (the stop reverted) red.

**I-07 (S2) — an inserted page drew its hidden layers.** *Found* at T5 by **A-38**: `assembly/layers.pdf` page 2
differed from its source at both scales. *Mechanism:* the page marks content with four optional-content groups, two of
which -- `fr` and `pr` -- the source's `/OCProperties /D /OFF` turns off; the copier copied the groups as objects the
page's `/Properties` reach, but the target's catalog listed none of them, and PDFium, like Acrobat, draws content of a
group no `/OCProperties` lists. Content its author hid became visible -- *S2*, because hidden content is often hidden
on purpose. *Why the suite missed it:* nothing before T5 brought pages from another document; A-38 is the first case
to draw a copy against its source. *Fixed* (the user's choice of two -- the other was bounding it and leaving it to
T10's merge): the copier narrows the source's `/OCProperties` to the groups it reached into the payload's catalog
(`ObjectGraphCopier::layers`), and the sink unions them into the target's -- `/OCGs`, `/OFF`, `/ON` under a target
`/BaseState /OFF`, `/Order`, `/Locked`, `/RBGroups`, `/AS` -- while a page draws from that payload, inserted or
replaced, before a save and after one, and takes them out when none does (`MutationSinkImpl::composeLayers`).
*Regression:* `unit.pageops layers: ...` (`[I-07]`) -- four groups and two hidden arrive in `refs-none.pdf`, leave
on undo after a save, return on redo, unite with `layers.pdf`'s own eight; and A-38 now 0 differ. *Found by its own
case while building:* the first union counted only unsaved pages as drawing from a graph, so after a save its groups
were taken out again (the redo and the undo-before-save control both red) -- a page saved from a payload is now
remembered (`graphOfPage_`). *Controls:* P7 (no union) and P8 (the saved page not remembered) red.

**I-08 (S3) — a Stage 5 timing bound red once.** *Found* in T5's first full `x64-release` run (1918 of 1919):
`render.a 900-page file's open never holds the engine longer than one page's extraction` (`tests/render/
tst_foldersearch.cpp:494`), *"T9 bound: 262 engine pieces, max 5.356 ms, p95 1.168 ms; ... one page's extraction ...
max 3.797 ms"* -- `pieceMax <= extractionMax` false. *Not reproduced:* five runs alone, max 3.14–3.49 ms against
3.68–4.86, all green. *What is known:* the case is `[run-serial]`, so no other ctest case ran beside it; it compares
one maximum with another, so a single slow piece decides it; T5's code is not in an open's path except the parser's
one new keyword comparison per dictionary key. *Mechanism:* NOT ESTABLISHED. **Again** 2026-11-04, the full `x64-release` run after T6's push fixes (1945 of 1946): *"274 engine pieces, max 4.281 ms ... extraction ... max 4.014 ms"*; five runs alone, max 3.06-3.76 ms against 3.45-4.02, all green. Those fixes are a test, the copier's reference rule and CI, none in an open's path. **A third time** 2026-11-05, T7's full `x64-debug` run (1976 of 1977), the first on that preset: *"259 engine pieces, max 5.245 ms ... extraction ... max 5.167 ms"*; five runs alone, max 3.02-3.43 ms against 4.36-4.64, all green; the same build's `x64-release` run was 1977 of 1977. T7 adds nothing to an open's path (the assembler is reached only from extract and split). *Open*; each full run is read for it.

**I-09 (S3) — a fuzz target built and counted, and run by nothing.** *Found* 2026-11-04 by reading **A-46**'s 60 s half
from run **41180314162** on `bcfbb1b` (T5's push): the `Portable libFuzzer` job's configure printed *"fuzz_assembly (6,
as expected)"*, its build linked `bin/fuzz_assembly`, and it then ran five targets -- the step list and the *"all five
targets linked"* loop were Stage 6's. `fuzz-soak.yml`'s matrix named the same five, so the soaks of 2026-10-31 …
2026-11-03 did not fuzz it either. No product behaviour is wrong; *S3*, like I-02, because what was lost is evidence --
§9 item 6 and A-46 are read from these logs, and D7-23 says *"the soak runs it"*. *Mechanism:* the targets a workflow
fuzzes are written by hand in two YAML files no build reads, while `cmake/RawPageFuzzTargets.cmake` asserts only how
many tests/fuzz BUILDS; T5 changed the build and its count and not the workflows. *Why no suite saw it:* the count
assertion was written for the opposite failure -- a target that does not exist passing *"no crashes"* -- and every job
was green, because a target nobody runs finds nothing. *Fix:* `fuzz_assembly` added to `ci.yml`'s linked loop and as a
60 s step, seeded from `corpus/assembly/` and `corpus/malformed/`, and to the soak's matrix with the same seeds; and the
class closed by `tests/gates/CheckFuzzWorkflows.cmake`, which reads `tests/fuzz/CMakeLists.txt`'s
`rawpage_add_fuzz_target()` calls and both workflows as text, comments dropped, and refuses a target with no ci.yml run
step, no place in its linked loop, or no soak entry. *Bound, stated at the script:* it does not check seeds, times, or
that a step is not skipped by an `if:`. *Regression:* `gates.fuzz_workflows.repository` over the real three files,
with `clean_pair_passes` (the control), `ci_unrun_fails`, `soak_unrun_fails` and `no_targets_fails` over
`tests/gates/fixtures/fuzz-workflows/`. *Control:* the repository case run BEFORE the workflow edits, red with all three
omissions named (`fuzz_assembly: no ci.yml step runs it`, `… not in ci.yml's linked loop`, `… no fuzz-soak.yml matrix
entry`); after them, green. *Fixed*, not closed: closed when a push's `Portable libFuzzer` log shows `fuzz_assembly`'s
60 s with no finding. *Since:* T6's push, run **41758087362** on `b3083f9`, RAN it -- the fix works -- and its first input
stopped on **I-12**, and so did the next two, **41770714539** and **41808943466**. The soak's
half works too: **41816834053**, the scheduled soak on `37ec179`, ran `fuzz_assembly` for the first time, and stopped on
**I-12**. **Closed** 2026-11-04 by run **41832843136** on `279fe66`: *"Done 11347 runs in 61 second(s)"* with no finding (A-46).

**I-10 (S2) — after a save, a page an undo put back was drawn as a page the engine never opened.** *Found* at T6 by
**A-124**'s raster half (`render.fullwriter.identity`): after a Save As that rewrote, each undo and redo drew the two pages
the rewrite had dropped and the undo brought back differently from the same step before the save -- render indices *0–3*
and *2–5*, both scales of exactly those two pages, every other page equal. A probe settled where: what Save would write
at that moment, reopened in a fresh engine, drew every page as before the save (0 differ), so the BYTES were right and
the screen was not -- D6-2's *what is shown is what is saved*, broken. A second probe found it older than T6: the same
undo after an ordinary APPEND save drew the same two pages wrong. *Mechanism:* the backend's page tree (D7-3) places each
drawn page in the tree read at open through `MutationSinkImpl::desiredDiskIndices` and `diskFrom` -- the newest revision
ON DISK. A page deleted and saved is in no disk revision, so an undo that puts it back answered `-1`, and the engine's
tree carried it as a page it had never opened, without the boxes, rotation and identity it read at open; `drawnOriginals`
likewise called it not the original object, so a destination naming it was not carried to it. *Why no suite saw it:*
T2's undo-after-a-save cases grade the SAVED file (A-24, `unit.roundtrip chains three saves and an undo after a save
writes the page back`) and T3's remap cases (A-30 … A-34) a move with no save between; no case drew the live engine after
a delete, a save and an undo. *Fix:* the page OBJECT each desired page shows (`MutationSinkImpl::desiredPageObjects`) is
looked up in the open tree's page objects (`DocumentBackendImpl::Open::openObjects`, read at open) when no disk revision
has it -- a page put back is the object the document had at open, and after a rewrite the object D7-29 writes back.
*Regression:* `render.fullwriter I-10 a page an undo puts back after an append save draws as it did` (`[I-10]`), and
A-124's raster case. *Control:* P7 (the fallback removed) red in both.

**I-11 (S2) — recovery cannot replay an undo, after a save, of a page delete from before it.** *Found* at T6 while writing
A-124's recovery clause, by a probe (removed after the run): `assembly/refs-every-kind.pdf`, page 2 deleted, saved, the
delete undone, the owner gone, the journal replayed over the saved file -- *"applied 0 stopped 1 pages 11"*, both after an
APPEND save and after a Save As that rewrote. *Mechanism, as far as read:* `RecoveryJournal` replays an undo of a step the
rebuilt history no longer holds as that command's `revert()` (`RecoveryJournal.h`), and `DeletePagesCommand`'s revert is
`MutationSink::restorePages(pages, key)` -- which puts back the pages the SINK took out under that key (`stash_`), held by the
process that ran the delete. The replaying process never ran it, so the key is unknown and the step refused -- by the
code, `error.command.malformed`; the probe read only that the replay stopped; after a rewrite the deleted pages' objects are not even in the file (D7-29 keeps them in the
memory of the session that rewrote). Stage 3's A-64 (`unit.recovery replays an undo after a save as the command's revert`)
proves the rule over a rotation, whose revert needs nothing the session held. *Why it is S2:* a replay that stops loses the
undo and every step after it; *open, not fixed in T6*, because the repair is a decision about what a page command's line
must carry across a save -- the pages' objects, or the file they lived in -- and no surface runs a page command before T8, so no
person can meet it yet. The duplicate and the insert, whose undos are removals by key too, were not probed. *Open.*

**I-12 (S3) — a catalog or a tree node the walk never met was copied with the pages it lists.** *Found* by **A-46**'s
first 60 s run, CI run **41758087362** on `b3083f9` (T6's push), the first run that fuzzed the target at all (I-09's
fix): *"assembly invariant violated: the payload holds the source's tree"*, before any mutation (`MS: 0`), on one of the
1,484 seeds -- of which 1,462 were in `corpus/malformed/`, a directory of 24: ci.yml's lexer and parser steps, earlier in
the same job, had been given it as libFuzzer's FIRST directory, which is where libFuzzer writes what it finds, so the
crashing seed was one of their inputs and no checked-in file. The input was too large for libFuzzer to print and the job
kept no artifact, so it is lost. *Mechanism, read from the code and reproduced:* `ObjectGraphCopier::localFor` stopped at
the trailer's `/Root` and at every node and page in the walk's sets (D7-4's *"the page tree never followed"*, A-41); an
object that IS a catalog or a tree node but that the walk never MET -- a `/Pages` node no `/Kids` lists, an earlier
revision's `/Catalog` -- was copied like any resource, and with it, through its `/Kids`, every page it lists.
*Consequence:* objects of the source's structure in the payload, then in the document an insert lands in, referenced by
nothing it draws; no drawing changes -- *S3*. *Why the suite missed it:* `copier bounds` and the replay's hostile graph
name the trailer's catalog and the walked root, the two objects the sets hold. *Fix:* an object TYPED `/Catalog`, `/Pages`
or `/Page` that is not a copied page is never followed, by its type and not by the walk's sets -- the reference reads null,
as one naming nothing does. *Regression:* `unit.copier I-12: no part of the source's tree comes along and a page is copied as a page`
(`[I-12]`), each input through the fuzz body and read directly. *Control:* the case run BEFORE the fix, red on both inputs
with the fuzz job's own words. **And the job:** each fuzz step's first directory is now scratch (`fuzz-work/<target>`),
every run writes a crashing input to `fuzz-artifacts/` (`-artifact_prefix`), a failed job uploads it, and
`gates.fuzz_workflows.corpus_written_fails` refuses a run whose first directory is under `corpus/` -- the repository case
red against the pushed `ci.yml`, naming `fuzz_cos_lexer` and `fuzz_cos_parser`. **Found again** by the next run,
**41770714539** on `d238834`, the same words on a MUTATED input (`MS: 1 CMP- DE: "\012\000"`) from the 46 checked-in
seeds, this time kept (`fuzz-artifacts`, `crash-e0e7077d…`): the mutation split one intermediate node's `/Kids` key, so
that node -- still `/Type /Pages` -- has no `/Kids` array and the walk reads it as a PAGE (its rule, and PDFium's: a node
is what has `/Kids`). It was one of the pages copied, and `copyPage` kept a page's own `/Type`, so the payload held a
second `/Pages`. *Fix:* a copied page is written `/Type /Page` whatever its source says. *Regression:* the `[I-12]` case's
third input, `a leaf typed /Pages`, red before the fix with the job's words; and the CI input itself, run through the fuzz
body by a temporary case (removed after the run) -- *"copied 0 ok 0 the payload holds the source's tree"* with the fix
stashed, *"copied 1 ok 1"* with it. In the same run every other target ran its 60 s with no finding, each seeded from the
24 checked-in `malformed/` files rather than ~1,460. **Found a third time** by the run after, **41808943466** on
`37ec179`, the same words (`MS: 3 InsertByte-InsertByte-CrossOver-`, kept as `crash-24182741…`): a page's `/Annots` lists
the `/Type /Pages` node above it, and `listAnnotations` took every dictionary `/Annots` names for an annotation and
`copyAnnotation` wrote it -- a path out of the source that does not go through `localFor`'s stop. *Fix:* an `/Annots`
entry that is the catalog, a node or a page the walk met, or a dictionary typed `/Catalog`, `/Pages` or `/Page`, is no
annotation and is not copied. *Regression:* the case's fourth input, `an /Annots entry that is the tree` -- the node above
the page, the catalog, the page itself, an untyped root, a direct dictionary typed `/Pages`, and a node and a page the
walk never met, beside one real annotation that must still be copied -- red before the fix with the job's words. The CI input through the fuzz body by a temporary
case (removed): *"copied 0 ok 0 the payload holds the source's tree"* with the fix stashed, *"copied 1 ok 1"* with it.
*Control* for the other half (convention 22): a plant dropping every `/Annots` entry, red at the case's check that the
real annotation stays. **And the soak**, the same day: `fuzz-soak.yml`'s scheduled run **41816834053**, also on
`37ec179` and the first soak to run `fuzz_assembly` (I-09), stopped on the same words about 5 s in (`MS: 1 CrossOver-`,
kept as `crash-be1ef340…` in the `fuzz-soak-fuzz_assembly` artifact): the same path -- two pages' `/Annots` listing two
`/Type /Pages` nodes and three `/Type /Page` dictionaries, each written with the `/P` `copyAnnotation` adds. Through the
fuzz body by a temporary case (removed): *"copied 0 ok 0 the payload holds the source's tree"* with `37ec179`'s copier,
*"copied 1 ok 1"* with the fix above -- so it needed no fix of its own. The fourth input gained a node and a page typed so
and met by the walk nowhere, which only the fix's type half stops; *control:* that half planted out (the number half
kept), red at the job's words. **Closed** 2026-11-04: run **41832843136** on `279fe66`, `fuzz_assembly` 60 s (*"Done 11347 runs"*), and soak **41833219442** on `279fe66`, 601 s
(*"Done 92800 runs"*), each with no finding (A-46) -- a bound of that search time, not a proof no fourth form exists.

**I-13 (S3) — `qpdf` judged a fault Save As was given as one it wrote.** *Found* by **A-122**'s `qpdf` half, CI run
**41758087362**, the `Portable ASan + UBSan` job: *"qpdf check: 210 file(s), 3 failed"* -- `rewrite-40.pdf` (*"stream
inflate: invalid bit length repeat"*, object 3), `rewrite-44.pdf` (*"page object 9 0: object is supposed to be a stream
... but is neither"*) and `rewrite-54.pdf` (*"Loop detected in /Pages structure"*), every other rewrite clean. Read
locally, the three are the rewrites of `malformed/bad-flate-stream.pdf`, `duplicate-object-number.pdf` and
`pages-loop.pdf` -- a corrupted stream copied as its bytes, the object 5 the repair reads (the second, a font, so page 3's
`/Contents` names a font and the first object 5, page 3's content stream, is unreached and dropped), and a page-tree
cycle kept as its objects. *Is anything lost?* Measured with a case written for it, `render.fullwriter an unedited Save As
draws every page as the original drew it` (`[I-13]`): every corpus file Saved As with no edit, reopened in a fresh engine,
against the SOURCE as PDFium drew it before anything was written -- *"118 files Saved As unedited, 7708 renders, 0 differ"*.
PDFium reads all three sources as the rewrite does, `duplicate-object-number.pdf`'s page 3 included. So the files are
right and the CHECK was wrong: it holds every file we write to qpdf's clean line, and these keep damage they were given.
*Why no suite saw that question:* A-122's raster bar grades a rewrite against the APPEND of the same state, which carries
the first-party reading of a damaged file as the rewrite does -- the two depart from PDFium together or not at all.
*Fix:* a rewrite of `malformed/<name>.pdf` is kept as `written/rewrite-malformed-<name>.pdf`, and `check-qpdf.py --source
rewrite-malformed-=corpus/malformed` (ci.yml) counts its failure as INHERITED, printed with both verdicts, only when qpdf
refuses the source too; a rewrite of a source qpdf accepts, or one whose source is missing, fails as any file does.
*Regression:* the `[I-13]` case, which stays as the stronger bar; the script's rule run here over a stand-in qpdf (an
inherited failure passes, a clean source's and a missing source's fail, and without `--source` nothing is exempt).
*Control:* P2b (the rewrite skips `/XObject`, the verify's reachability off) red in the `[I-13]` case, *"26 differ or
refused"*. **Closed** by run **41770714539**'s `qpdf` log: *"qpdf check: 210 file(s), 0 failed, 3 inherited from a source
qpdf refuses too"*, and qpdf names the SAME fault in each source as in its rewrite -- the inflate error on object 3, the
page-tree loop at object 5, and *"page object 9 0: object is supposed to be a stream"* in `duplicate-object-number.pdf`
itself, so qpdf reads that file's object 5 as we and PDFium do. Whether a rewrite should
MEND a damaged page tree rather than keep it is not decided -- D7-25 writes objects value for value.

**I-14 (S3) — PDFium's first substitution of an unknown font draws unlike every later one.** *Found* 2026-11-04 by the
`[I-13]` case's first run: `malformed/bad-name-hex-escape.pdf` (`/BaseFont /Helve#GGtica`) differed on every page from
its unedited rewrite, which writes the name `/Helve#23GGtica`. *Measured* with a probe (removed after the run), one
process, the same file with the name respelled at the same length so no offset moves: the source opened TWICE gave two
digests; the second equalled `/Helve#00tica`, `/Helve` alone, `/HelveGGtica`, `/Helve#23GGti` and `/Xyzzy`; `/Helvetica`
was stable in both places; and with `/Helvetica` drawn first, the first UNKNOWN name drawn (`/Helve#00tica`) took the odd
digest instead. So the spelling does not matter and the order does: the first unknown font substituted in a process draws
one way, every later one another. *Consequence:* the first document of a session with a font PDFium does not know draws
unlike the same document opened again -- *S3*, nothing written changes. *Why no suite saw it:* every engine case compares
two drawings inside one process, and the first substitution happened to fall on a file both sides of a comparison drew
after it. *Cause inside the engine:* not established. *Bound, at the case:* the `[I-13]` case takes the first substitution
before its sweep and says it does not cover what a session's first document shows. *Open*; the user's decision.

**I-15 (S3) — the vcpkg binary cache restored nothing, and a rate limit stopped a fuzz run.** *Found* 2026-11-04 by
run **41824929474** on `f68f0c1`, the `Portable libFuzzer` job: configure stopped on *"curl operation failed with
response code 429"* for `gitlab.com/libtiff/…/libtiff-v4.7.2.tar.gz`, three attempts, and *"building tiff:x64-linux failed"*
-- no fuzz target was built or run, so the run says nothing of I-12. Not a product defect, and not the fault the job
reported: why a job downloads libtiff at all is. *Mechanism, read from the logs:* every vcpkg cache step reported a hit
on its key, and vcpkg then printed *"Restored 0 package(s)"* -- in run 41808943466's `Windows x64`, `Portable ASan +
UBSan` and `Portable libFuzzer`, in all six jobs of soak 41816834053 and in this run -- and built all nine ports. The key
hashed `vcpkg.json` and `vcpkg-configuration.json` alone, but a port's ABI also follows the RUNNER IMAGE, which ships the
vcpkg tool and its scripts: on image `20261025.353.1` the ABIs were identical across runs and across the ASan, fuzz and
soak jobs (`tiff` `64562705…`, `zlib` `e5a44375…`), and on `20261101.360.1` every one differed (`tiff` `f2df6356…`).
Archives saved under an older image matched nothing, and a key that still HIT is never saved again -- and on Linux
`actions/cache`'s combined step saves only when the whole job passes, so a red run never saved what it built. *Why
nothing caught it:* a cache that restores nothing is slower, never red. *Fix* (`ci.yml`'s three vcpkg caches,
`fuzz-soak.yml`'s): the key names the image (`ImageOS`-`ImageVersion`, a step that FAILS when either is unset rather than
fall back to the old key); no `restore-keys`, since another image's archives match nothing; restore and save as two
steps on Linux, as the Windows job already had; and every save only after a configure that SUCCEEDED -- a configure
stopped part-way holds some ports' archives, which saved under the key would be restored as a hit and never saved again
(the Windows save ran on any configure that ran). *Bound:* the image is what the logs show the ABIs following; anything
else that moves them is not keyed, and the same *"Restored 0 package(s)"* line is where it would show. `gates` 145/145
over the edited workflows (`gates.fuzz_workflows.*` read both files). *Pushed* in `279fe66`, 2026-11-04, and read from the logs of run **41832843136** on `279fe66` and soak **41833219442** on `279fe66`, which ran
together: every vcpkg cache step logged *"Cache not found"* for its image's key, built the ports, and the first job on each key
logged *"Cache saved with key"* -- `vcpkg-mingw-win25-vs2026-20261030.294.1-…`, `vcpkg-linux-ubuntu24-20261026.354.1-…`, and
`vcpkg-linux-fuzz-` for THREE Linux images, `20261018.96.1`, `20261026.354.1` and `20261101.360.1`, handed out to jobs
started within a minute of each other (12 MB on Windows and 15 MB each on Linux, `gh cache list`). The jobs that lost the race
to a key -- CI's `Portable libFuzzer` and three soak jobs -- logged *"Failed to save: Unable to reserve cache with key …,
another job may be creating this cache"* and *"Cache save failed"* as a warning, and stayed green. Every job still printed
*"Restored 0 package(s)"*, as a miss must. *Bound:* with the image in the key, a job restores only on an image a job before
it saved, and the runners hand out several images at once. *Fixed*, not closed: closed when a later run on one of those
images logs the ports restored rather than built.

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
