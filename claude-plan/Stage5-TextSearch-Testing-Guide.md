# Stage 5 — Text Layer: Extraction, Selection, Find, Search, Comparison · Testing Guide

> **Authoritative verification document for Stage 5.** Written from
> [`Stage5-TextSearch-Plan.md`](./Stage5-TextSearch-Plan.md) and
> [`Implementation-Plan.md` § Stage 5](./Implementation-Plan.md#stage-5--text-layer-extraction-selection-find-search-comparison),
> **before any Stage 5 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** ⚪ **Written 2026-09-29 with the plan, before any Stage 5 code; the plan's §0 answered
> the same day.** **No case has a result.** **114 automated** (A-01 … A-114) and **9 manual**
> (M-01 … M-09). Five cases were written against a §0 answer — A-46 (Q2), A-50 (Q3), A-51's `Ctrl+A`
> half (Q6), A-89 (Q1) and A-98 (Q7) — and **all five stand as written**, their answers approved or,
> for Q3, answered with option B; **A-114** was added with Q3's answer, for the snapshot resolution.
> §8's issue log is empty.
>
> **Two halves, per [`CLAUDE.md`](../CLAUDE.md): [§5 Automated](#5-automated-verification--i-run-this)**
> — I run it and report the actual output — and **[§6 Manual](#6-manual-verification--you-run-this)** —
> you walk it on real hardware, and **I never mark a manual case passed on your behalf.**
>
> **The manual half is nine cases, walked on GENERATED documents** — your decision of 2026-09-29
> (plan **D5-23**). The automated half is large because text is mostly arithmetic a test can reach:
> the corpus generator knows exactly which characters it placed, where and in what order (plan §5.2),
> so extraction, selection, search and comparison can all be graded against the generator's own
> record. The manual half asks a person what no assertion can: whether a paste into Word looks
> right, whether a highlight sits on the glyphs, whether a comparison reports what a person would
> call a change. **Because every document here is ours, no Stage 5 case meets a real document** —
> the manual half included — and every claim this guide supports carries the bound *"on our own
> generated corpus"*. **Anything in §6 that could have been
> automated is a defect in this guide — say so and I will move it.**
>
> **Label scoping.** `A-nn`, `M-nn` and `I-nn` here are **this guide's**; Stages 0 to 4 have their
> own, and this guide cites theirs with the stage named — *"Stage 4's I-10"*. `A2` (no hyphen) is
> the rendering-performance **risk** in `Tech-Stack-Plan.md` §3.3. `T1`…`T14` are **Stage 5** tasks,
> `D5-n` Stage 5 decisions and `Q1`…`Q7` the plan's open questions, all in
> `Stage5-TextSearch-Plan.md` (§6, §3 and §0).

---

## 1. Scope

### In scope — must pass for sign-off

- **Text is extracted truthfully**, with geometry, from every corpus file that has text, and **text
  that cannot be extracted is reported as such** rather than extracted as plausible garbage.
- **Selection is accurate on multi-column, RTL, vertical and CJK pages**, across page boundaries,
  and **matches the glyphs a person sees**. Copy delivers plain text, HTML and a table.
- **Find and Advanced Search honour every §1.8 toggle across all five content classes**, over one
  document, a selection, a page range and a folder; **the second search of a 900-page file is
  instant** by §3's definition.
- **Snapshot** copies a region as the document draws it. **Reflow view** re-wraps text in reading
  order.
- **Comparison finds text, appearance and page-level changes**, honours its ignore-lists and
  sensitivities, and stays bounded on documents that have nothing in common.
- **Nothing in this stage changes a document.** `mutation-lint` reports **one** command on the last
  day of the stage as it did on the first (D5-1).
- **The A2-at-Stage-5 verdict, the layout baseline and O7's revisit are recorded.**
- **No regression** in any Stage 0 to 4 case, on **both** presets.

### Out of scope — do NOT file these as bugs

Planned absences, itemized in [`Stage5-TextSearch-Plan.md` §11](./Stage5-TextSearch-Plan.md#11-explicitly-out-of-scope).

| Not a bug this stage | Arrives at |
|---|---|
| **There is no Replace** — the bar is the find half (D5-1, D5-14) | Stage 13 |
| **A search hit inside an attachment opens nothing** — it is listed and selected (D5-12) | Stage 6 |
| **A tagged document is ordered by geometry like any other** — its structure tree is not read (D5-6) | Stage 7 |
| **Reflow view shows text only** — no images, no drawings, no annotations — and **find does not run inside it** (D5-16) | — |
| **A snapshot is a bitmap**: pasted and scaled up far enough, it blurs — raise `pageDisplay/snapshotDpi` for print (D5-10, D5-11) | `DW-2` |
| **No summary PAGE and no PDF report come out of a comparison** — the summary is the Differences pane (D5-17) | Stage 8 |
| **The comparison view has no rulers, grid or guides** (D5-18) | — |
| **No catalog index, no portfolio search, no OCR layer** in Advanced Search | Stage 16 |
| **`Shift`+arrow keys do not extend a selection** — there is no caret in a PDF page | — |
| **`Ctrl+B`, `Ctrl+E`, `Ctrl+.` and `Ctrl+,` still do nothing** and leave the key alone | Stages 7, 6, 6, 6 |
| **A page colour can be inverted and not substituted** — Stage 4's **I-07** | `DW-1` |

### What this evidence cannot tell us

- **Every automated claim is about our own generated corpus.** Its fonts are rectangles, its layouts
  are regular and its producer is us (D2-11). A green `tst_layoutcorpus` says *"geometry recovers the
  structure of documents we generated"* — **never** *"reading order is correct"*, and nothing about
  a real document. **And no manual case supplies one at Stage 5** (plan D5-23): the manual half walks
  generated documents too, so the bound `golden/README.md` puts on the golden suite applies to
  **every** Stage 5 result, and the analyzer's accuracy on real layouts is carried to R1's row
  unmeasured.
- **`ui/` is outside every sanitizer this project has** (O10, `Implementation-Plan.md` §8).
  `portable-asan` and `fuzz_text_pipeline` cover `core/text/` — the normalizer, the analyzer,
  selection, search, the index and the comparer — and nothing about the find bar, the panes or the
  views.
- **An offscreen widget run has no clipboard a real application pastes from.** CF_HTML is read back
  through Win32 on the `windows` platform (A-50), but whether Word renders it well is M-02's.
- **Latency is measured; responsiveness is judged.** T3 measures the viewport's p95 queued latency,
  the quantity A2 is graded on. Whether scrolling *feels* smooth while a first search indexes 900
  pages is M-06's, and the two can disagree.

### Case anatomy

Every case is **ID · Goal · Steps · Expect · If it fails**, with a `☐` changed to ✅ or ❌. A case
either passes or yields a **specific** failure — "looks fine" is not a result.

### The rules this guide is written under

**Rule 1 — assert the EFFECT, not the intent** (Stage 2's I-28, convention 19). A selection range
is not a highlight, a hit list is not a highlighted glyph, a `Background` flag is not a viewport that
was not starved. Where a case can only reach the intent, it says so at the assertion and names what
it therefore does not cover.

**Rule 2 — a repaired case is re-walked from step 1** (Stage 1's I-45).

**Rule 3 — a green tick is not a clean run** (Stage 2's I-38). Linux-only halves are read from the
CI job's **log**, never from its tick.

**Rule 4 — a widget case drives layout itself, and asserts a non-zero visible count first**
(convention 12).

**Rule 5 — a new mechanism gets a control written from what must NOT happen** (convention 22). A
toggle that is off must not match; a hyphen mid-line must not be joined; an identical pair must
report nothing; a text field must keep its own `Ctrl+C`.

**Rule 6 — the oracle for GEOMETRY is the rendered ink** (Stage 4's I-10). A highlight, a hit
rectangle or a snapshot is compared against the **raster** of the page, located by code that shares
nothing with `PageTextModel`'s transform. A case that checks a highlight against the char box it was
computed from proves the arithmetic agrees with itself.

**Rule 7 — the oracle for TEXT is the generator**, never PDFium. Each fixture's expected text,
order and structure is what `corpus/make-text.py` and `corpus/make-compare.py` wrote down when they
built it (plan §5.2). A case that compares the extractor with `FPDFText_GetText` proves PDFium agrees
with itself.

**Rule 8 — both presets, and the UI binary whole.** `x64-debug` fail-fasts where `x64-release`
reports green (Stage 4's I-09), and a UI case that leaves process state behind breaks a case that
runs after it in the same process (Stage 4's I-28). Every run of the suite is both presets, and T14
runs `rawpage_ui_tests.exe` whole under a printed seed.

### Severity

| Sev | Meaning |
|---|---|
| **S1** | **A Stage 5 surface changes the document** — any byte on disk, any journal entry, any `Commands.def` row. **The product presents text that is not in the document** — unmapped glyphs shown as extracted text, a copy that invents characters. **The window stops rendering** while text work runs, or a folder search freezes it. A crash on a corpus file |
| **S2** | A documented behaviour is wrong in a way a later stage builds on — a reading order, a selection that does not match the glyphs, a toggle not honoured, a hit missed or invented, a change the comparison misses, a layout target missed |
| **S3** | A message is unclear, a snippet is badly cut, a figure is misreported, a diagnostic names the wrong thing |
| **S4** | Polish nobody outside the team would notice |

**The S1 that is new at this stage is *text that is not there*.** A wrong pixel is visible and a
wrong character is not: a clipboard, an exported list or a comparison carries it into someone else's
work, where nobody knows to check it.

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
ctest --preset x64-release -R "textmodel|normalize|layout|selection|search|index|compare"
ctest --preset x64-release -R "extract|layoutcorpus|comparecorpus"      # the engine-backed halves
.\build\x64-release\bin\rawpage_bench_pipeline.exe --stage5            # T3's A2 figure
.\build\x64-release\bin\rawpage.exe corpus\ordinary\columns-two.pdf
```

**Notes carried from `CLAUDE.md`** because they still cost time: PowerShell does not wait on a GUI
binary (`Start-Process -Wait -PassThru`); its pipeline captures none of `rawpage.exe`'s output (use
`cmd` redirection); a running `rawpage.exe` blocks the release link and skips a case; a failing
run's output survives only until the next run, in `build\<preset>\Testing\Temporary\LastTest.log`.

**Where things are written**, so a walker can look:

| What | Where |
|---|---|
| The clipboard | The system clipboard — **copy, snapshot and `Ctrl+A` write nothing else** |
| An exported result list | Wherever you choose in the save dialog — for a walk, `tests\manual\M-07\` |
| Settings | `%APPDATA%\RawPage\settings.ini` |
| Logs | `%LOCALAPPDATA%\RawPage\logs\` |
| The search index | **Nowhere on disk** — memory only (D5-4). A file appearing for it is a finding |
| A manual walk's own files | `tests\manual\<case>`, and nowhere else (`CLAUDE.md`) — a **copy** of the corpus file, never the corpus file itself, which is generated and checked byte for byte |

### Settings keys that affect these cases

**Defaults are owned by `core/settings/SettingsKeys.def`** — **re-read the `.def` rather than this
table** (Stage 2's I-26 was this table disagreeing with that file). The fourteen Stage 5 rows are
listed in the plan's §4:

| Key | Affects |
|---|---|
| `search/wholeWords`, `search/caseSensitive`, `search/ignoreDiacritics`, `search/useRegex` | A-56 … A-60, A-73, M-06 |
| `search/includeComments`, `search/includeBookmarks`, `search/includeFields`, `search/includeAttachments` | A-76 … A-79, M-07 |
| `search/maxResults` | A-61, A-88 |
| `search/indexCacheMB` | A-65 — **set it small to reach eviction** |
| `documents/compareSensitivity`, `documents/compareIgnoreHeaderFooter`, `documents/compareIgnoreWhitespace`, `documents/compareIgnorePageNumbers` (Q7) | A-98, A-99, M-08 |
| `performance/tileCacheMB` | A-30 — T3's small-cap column |
| `pageDisplay/snapshotDpi` | A-114, M-03 — **set it to 300 to reach M-03's second half** |

### Test rig

| | |
|---|---|
| **An editor that pastes HTML** | **P-1** ✅: **Microsoft Word, Office LTSC Professional Plus 2024** (x64, build 16.0.20430.20092), and **Excel** from the same install — M-02 |
| **Documents to walk** | **P-2** ✅ **GENERATED by T1, on your decision (plan D5-23)**: `columns-two.pdf`, `columns-three.pdf`, `table-ruled.pdf`, `rtl-text.pdf`, `text-tounicode.pdf`, `cjk-vertical.pdf`, `cjk-embedded.pdf` and the `compare/report-v1.pdf` → `report-v2.pdf` pair — each walk copies its file into `tests\manual\<case>` |
| **A folder to search** | **P-3** ✅ **GENERATED (D5-23)**: a copy of the whole `corpus` tree into `tests\manual\M-07\corpus` — nested folders, five encrypted files, the malformed set — M-07 |
| **An image editor** | **P-5** ✅: Paint (`mspaint.exe`) — M-03 |
| **Monitors** | Stage 4's P-1 rig (150 % and 100 %) is **used but not required**: M-01 asks for the highlight on both monitors because a selection that matches the glyphs at one scale and not the other is Stage 4's I-11 in a new place |
| **Corpus** | 70 files before T1; T1 adds plan §5.2's fixtures, labels and pairs. Every one generated by a script we own, with a `PROVENANCE.md` row |

---

## 3. Verdicts this guide must produce

A stage that is green but has no verdict is not signed off. **Every scale below is fixed here, before
the measurement**, so nothing is graded against whatever it happened to achieve.

### 3.1 A2 at Stage 5

| Verdict | Assembled from | Recorded in |
|---|---|---|
| Does the shipped software path hold the frame budget once text extraction shares the one engine thread with rendering? | **Bench (T3):** A-27 … A-31. **Product (T14):** A-112. **Judgement:** M-06 | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), labelled **Stage 5's workload** |

The quantity is the **viewport's p95 queued latency**, the same quantity as Stage 2 T18's product
figure and Stage 4's (`Tech-Stack-Plan.md` §3.3), so all of them are comparable. Text jobs' own
latency is reported and **not** graded: a search result arriving late is a slow search, and a tile
arriving late is a stutter. **The per-page extraction time is reported with its p95 and maximum**,
because a job inside PDFium cannot be preempted (D5-3), and its maximum is the longest a visible tile
can wait.

| Result | Means |
|---|---|
| **go** | While `text-900.pdf`'s whole index is built at `Background`, and separately while a folder search and a comparison's extraction run, the viewport's p95 stays **inside 16.667 ms** at the shipped cache cap, and **text work causes zero cache evictions** (text is never in the tile cache, D5-4) |
| **go, with named custom work** | The p95 leaves the budget only under a named, reachable condition — a dense page whose extraction exceeds the budget on its own, a small cache cap, a fast scroll during an index build — and a change in `core/render/` or `core/text/` brings it back: the `Background` lane yielding while visible work is outstanding, a pause of text work during a scroll, a cap on text jobs in flight. **The work is named in the verdict and done in the stage** |
| **no-go** | The p95 leaves the budget under an ordinary workload and no scheduling change recovers it. **Extraction leaves the render thread** — a second engine instance in a helper process is the shape — which is a **new decision** recorded before T5, because every later surface queues on it |

### 3.2 The layout baseline (D5-7)

Measured by `tst_layoutcorpus` over every page with a label file, against the generator's labels.
**These targets are the pass bar for T4.** A miss is an S2 in §8: the analyzer is improved, or you
change the target with the measurement beside it — never quietly.

| Measure | Definition | Target |
|---|---|---|
| **Line grouping** | F1 over lines, a predicted line matching a labelled one when their character sets are equal | **≥ 0.98** |
| **Block grouping** | F1 over blocks, matched the same way | **≥ 0.95** |
| **Columns** | Pages whose detected column count equals the label | **≥ 95 %** |
| **Reading order** | Of each labelled pair of consecutive blocks, the fraction that appear in the same relative order in the predicted order | **≥ 0.95** |
| **Tables, detection** | Precision and recall over table regions, matched at intersection-over-union ≥ 0.8; the unruled aligned table counts, the tabular-looking prose is a negative | **≥ 0.90 each** |
| **Tables, grid** | Of correctly detected tables, the fraction whose rows × columns equal the label | **≥ 0.80** |

**Recorded as evidence for R1** (`Tech-Stack-Plan.md` §10), with the bound: *"on our own generated
corpus"*.

### 3.3 *"The second search of a 900-page file is instant"*

The phrase is `Implementation-Plan.md`'s. **Defined here as:** after the index of
`ordinary/text-900.pdf` is complete, a literal query's **every hit delivered within 100 ms at p95**
over 20 queries, measured from submission to the last hit on the UI thread, on this machine. 100 ms
is the usual threshold below which a response reads as immediate. The **first** search, which builds
the index, is reported and **not** graded — it is A2's subject (§3.1), not this one's.

### 3.4 Comparison on generated pairs

Over every `corpus/compare/` pair, against its `expected.json`: **every labelled change found, with
its kind**; **zero** reported differences on the identical pair at every sensitivity; **no
difference outside the labelled ones** at *text only*. The unrelated pair finishes **within 10
seconds** with **peak live heap under 256 MB** (measured, as Stage 2's I-35 taught, as LIVE heap and
never as private bytes). The walkable pair, `report-v1` → `report-v2`, is graded here like every
pair and judged by a person in M-08 — **no real revision pair is compared at Stage 5** (D5-23).

### 3.5 O7's revisit

Recorded in `Implementation-Plan.md` §8's **O7** row at T3: whether the tile cache's cross-document
evictions move under Stage 5's workload (A-30), and that the text index is keyed by `DocumentId`
and outside the tile budget (A-62 … A-66). O7 keeps its resolution unless A-30's figure leaves the
§3.1 budget.

---

## 4. Traceability — exit criterion → case

| Exit criterion ([`Implementation-Plan.md` § Stage 5](./Implementation-Plan.md#stage-5--text-layer-extraction-selection-find-search-comparison)) | Closed by |
|---|---|
| Selection is accurate on multi-column, RTL and CJK pages | A-42 … A-48, A-55; **M-01**, **M-04**, **M-05** |
| Every §1.8 search toggle works and is honoured across all five content classes | A-56 … A-61, A-76 … A-79; **M-06**, **M-07** |
| Cross-file search returns correct snippets | A-80 … A-88; **M-07** |
| Comparison finds text, appearance and page-level changes on real revision pairs | A-95 … A-107 on generated pairs; **M-08** on the generated `report-v1` → `report-v2` pair — **the criterion's *"real revision pairs"* is met on generated ones, by decision (plan D5-23)** |
| *(Implementation-Plan)* The second search of a 900-page file is instant | **A-63**, graded on §3.3 |
| *(Implementation-Plan)* `LayoutAnalyzer`'s accuracy is measured, not asserted | **A-38**, graded on §3.2 |
| *(Plan D5-1)* Nothing changes a document | A-07, A-08, A-107 |
| *(Plan D5-2)* Unmappable text is reported, not invented | A-21, **M-05** |
| *(Plan D5-3, D5-22)* Text work does not starve the page | A-24, A-27 … A-31, A-67, A-88, A-112; **M-06** |
| *(Plan D5-4)* O7's revisit — text outside the tile cache, keyed by `DocumentId` | A-30, A-62 … A-66 |
| *(Plan D5-5)* One normalizer for find, search and compare | A-32 … A-36, A-98 |
| *(Plan D5-6)* Reading order, and RTL measured rather than assumed | A-23, A-37 … A-39, A-47 |
| *(Plan D5-8, D5-9)* Selection and copy | A-42 … A-55; **M-01**, **M-02** |
| *(Plan D5-10)* Formatted copy through Qt's CF_HTML | **A-50**; **M-02** |
| *(Plan D5-11)* Snapshot as the document draws it, at the resolution asked | A-90, A-91, A-114; **M-03** |
| *(Plan D5-12)* Five content classes | A-76 … A-79 |
| *(Plan D5-13)* Advanced search, and one planned waiver | A-09, A-80 … A-89; **M-07** |
| *(Plan D5-14)* The find bar | A-69 … A-75; **M-06** |
| *(Plan D5-16)* Reflow view | A-92 … A-94; **M-09** |
| *(Plan D5-17, D5-18)* Comparison, and its surface | A-95 … A-107; **M-08** |
| *(Plan D5-21)* Hostile geometry | A-40, A-41 |

---

## 5. Automated verification — I run this

Every case names its command and its pass condition. I report the **actual** output; a failing case is
reported as failing, with that output, and is never declared fixed without a re-run. **When a case is
ticked it carries the evidence it was ticked on** — Stage 1's I-32 was a guide whose summary claimed a
green its cases did not record.

### 5.1 Build, boundary, environment

**A-01 · Configure and build, both presets, from clean** ☐
**Steps** `cmake --preset x64-debug`, `--preset x64-release`; `cmake --build --preset <p> --clean-first` for each.
**Expect** Exit 0 on all four; **0 `warning:` and 0 `error:` lines in either log**, `ui/` included. `--clean-first` is required — an incremental build's warning census is inherited, not measured (Stage 3's A-01). The object counts are re-derived, not quoted. **If it fails** S1.

**A-02 · The `ui/` warning grace list is still empty** ☐
**Steps** Read `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake`.
**Expect** Empty. **If it fails** S3, and the entry's reason is recorded in the status block.

**A-03 · The headless suite is still headless** ☐
**Goal** `core/text/` is `core/`, and `core/` still links no display and no engine.
**Steps** `ctest -R "gates.headless_assert"`.
**Expect** Every case as before, both negative controls still caught. **If it fails** S1.

**A-04 · `portable-asan` builds and runs every `core/text/` suite** ☐
**Steps** The `portable-asan` CI job; **read the log, not the tick** (rule 3): `gh run view <run> --log --job <id>`.
**Expect** Every case of `tst_textmodel`, `tst_normalize`, `tst_layout`, `tst_selection`, `tst_search`, `tst_index`, `tst_searchresults` and `tst_compare` present **by case name** among the run lines (CTest registers cases, not files — Stage 4's A-04); **0 `runtime error:`, 0 `ERROR: AddressSanitizer`, 0 `LeakSanitizer` lines**. **What it does not cover:** the extractor (it needs the engine) and every `ui/` surface. **If it fails** S1 for a sanitizer finding; S2 if a suite is absent.

**A-05 · The product starts, and the startup census moves where it should** ☐
**Steps** `$env:QT_QPA_PLATFORM="offscreen"`; `rawpage.exe --dump-ui-strings` and `--list-actions`, **through `cmd` redirection** (the pipeline captures nothing — Stage 4's A-05).
**Expect** Exit 0; every `search.*` and `compare.*` key resolves in `en` and `zh-TW` and **none resolves to itself**; the log says the backend is **4 of 7**, missing `ISigner`, `IOcrEngine`, `IIndexer`; the platform stays **6 of 18** (D5-10, Q3). **If it fails** S2.

**A-06 · `core/text/` uses only the Qt the portable presets have** ☐
**Goal** Convention 24: CI builds `core/` against Ubuntu's Qt **6.4**, and the Windows suite cannot catch a 6.5+ API.
**Steps** The `portable-asan` job's configure and build log.
**Expect** `core/text/` compiles there. `QString::normalized`, `QString::toCaseFolded`, `QChar::category`, `QTextBoundaryFinder` and `QRegularExpression` are the APIs D5-5 names; a use of anything newer fails **this** job, which is the case. **If it fails** S2 (Stage 4's I-03 was this, in `core/`).

### 5.2 The gates — and the numbers that must and must not move

**A-07 · `checks` clean, and `mutation-lint` still says ONE command** ☐
**Goal** **D5-1's mechanical proof.**
**Steps** `cmake --build --preset x64-release --target checks`.
**Expect** Exit 0; four gates; *"mutation-lint: clean -- 1 command(s): RotatePageCommand"*. **If it fails** S1 if a Stage 5 file raised the count.

**A-08 · `core/cmd/Commands.def` and `core/cmd/commands/` are byte-unchanged across the stage** ☐
**Steps** `git diff 1128e7a..HEAD -- core/cmd/Commands.def core/cmd/commands/` (`1128e7a` is Stage 4's sign-off).
**Expect** Empty. **If it fails** S1.

**A-09 · The waiver census is FIFTEEN, and the fifteenth is the export** ☐
**Steps** `layer-lint.exe --root . --explain`.
**Expect** Clean; **15** active filesystem-write escapes — the fourteen of Stage 4 plus **one** on the `QSaveFile` that writes the result list in `ui/panes/SearchPane.cpp` (D5-13), and `--explain` reports it as having **silenced something**. **The control:** removing the marker makes `layer-lint` fail on that line. A sixteenth is a finding. **If it fails** S2 for an unnamed waiver; S3 for a count that disagrees with `CLAUDE.md`.

**A-10 · `i18n-lint` knows `search` and `compare`, and no key is missing** ☐
**Steps** `i18n-lint.exe --root . --explain`; a fixture referencing `search.noSuchKey`.
**Expect** `keyFamilies` lists `search` and `compare`; the fixture fails `unknown-key` naming **that** key; every Stage 5 key exists in both shipped catalogs. **If it fails** S2.

**A-11 · No dependency moved** ☐
**Goal** D5-5: no ICU, no HarfBuzz; the `shaping` feature stays off.
**Steps** `git diff 1128e7a..HEAD -- vcpkg.json`; `license-audit` as `checks` runs it; `objdump -p build\x64-release\bin\rawpage.exe` for its imported DLLs.
**Expect** `vcpkg.json` unchanged; `license-audit` clean with the same component count; **no `icu*` or `harfbuzz` DLL imported**. **If it fails** S2 — a dependency arrived without a decision.

### 5.3 The corpus this stage is judged on (T1)

**A-12 · The new files regenerate byte-identically, and nothing old moved** ☐
**Steps** `python corpus/make-corpus.py --verify`, `--check-provenance`, `--self-test`; `git status corpus/`.
**Expect** All three exit 0; one `PROVENANCE.md` row per PDF, each naming what the file carries **and its control** (plan §5.2); **every pre-existing corpus file byte-unchanged**, which `--verify` cannot claim alone. **If it fails** S2.

**A-13 · The labels are data, checked like the corpus** ☐
**Steps** `--verify` and `--check-provenance` over `corpus/labels/`.
**Expect** Every label file regenerates byte-identically; every label names a corpus file that exists, and every layout fixture has one. **The control:** a label naming a missing file fails `--check-provenance`. **If it fails** S2.

**A-14 · Each fixture carries the fault it claims — read from the BYTES** ☐
**Goal** A fixture that does not carry its fault tests nothing (Stage 3's A-17).
**Steps** A `gates.corpus.*` check over the bytes.
**Expect** `text-tounicode.pdf` has `/ToUnicode` streams, a glyph mapped to `<FB01>` and another mapped to `<0066 0069>`, and a decomposed `e`+U+0301; `rtl-text.pdf` shows Hebrew in **visual** order in its content stream; `cjk-vertical.pdf` uses `/Identity-V`; `hyphenated.pdf` carries line-final hyphens and `well-known` broken mid-line; `search-classes.pdf` carries its unique words in page text, an annotation's `/Contents`, an outline title, a field's `/T` and `/V`, an `/EmbeddedFiles` name, a description, and an attached PDF; the control word appears **nowhere** in the file, decoded streams included; T1's new fixtures carry **distinct `/Author`, `/Subject` and dates** between them, which M-07's filter needs (D5-23); `cjk-embedded.pdf` still has **no** `/ToUnicode`. **If it fails** S2.

**A-15 · Every comparison pair's `expected.json` describes its own bytes** ☐
**Steps** `make-compare.py --self-test`.
**Expect** Each pair differs from its base **only** where `expected.json` says — the walkable `report-v1` → `report-v2` pair included, every one of its mixed edits listed; the identical pair's page content is identical; the unrelated pair shares no page text. **If it fails** S2.

**A-16 · The golden manifest knows the new files** ☐
**Steps** `golden-update.exe --check-manifest`.
**Expect** Every new PDF is a case or an excluded row with a reason; `--explain`'s census is re-derived, not quoted. **If it fails** S3.

### 5.4 Extraction (T2)

**A-17 · The text is the generator's text** ☐
**Goal** Rule 7.
**Steps** `tst_extract` over every T1 fixture and `text-a4.pdf`, `a2-text-page.pdf`, `pages-900.pdf`'s first and last pages.
**Expect** Each page's extracted text, in the model's order and ignoring generated whitespace, equals what the generator wrote, character for character. **If it fails** S1 for an invented character; S2 for a missing one.

**A-18 · Every character's box contains its rendered ink** ☐
**Goal** Rule 6.
**Steps** Render each page at 144 dpi; for each character, locate its ink by scanning the raster in a window around the generator's recorded position — code that never reads `PageTextModel`.
**Expect** The model's loose box, transformed to device pixels, contains that ink within 1 px on every side. **The control:** a model deliberately offset by 2 pt fails. **If it fails** S2.

**A-19 · Font identity, size and weight are read** ☐
**Expect** On `latin-embedded.pdf`, `text-tounicode.pdf` and `font-not-embedded.pdf`, each character's font name, size and weight equal what the generator used; a bold run is bold. **If it fails** S3.

**A-20 · Generated whitespace and soft hyphens are flagged** ☐
**Expect** On `hyphenated.pdf`, every line-final hyphen carries the **hyphen** flag and the mid-line `well-known` hyphen does not; spaces the content never showed carry **generated**. **If it fails** S2.

**A-21 · Unmappable text is REPORTED, not extracted** ☐
**Goal** D5-2, and the S1 this stage is most likely to ship quietly.
**Steps** Extract `cjk-embedded.pdf` (no `/ToUnicode`) and `text-tounicode.pdf`'s CJK page.
**Expect** Every CJK character of `cjk-embedded.pdf` carries **unmapped**, and every consumer treats the page as having no extractable text: find reports it, copy of it produces nothing, and nothing claims a match. **The control:** the same glyphs with `/ToUnicode` extract as the right hanzi. **If it fails** S1.

**A-22 · Coordinates are in page space, whatever the rotation** ☐
**Expect** On `rotated-pages.pdf` and `rotate-inherited.pdf`, each character's box is in unrotated user space, and a highlight drawn through the view's transform lands on the ink at 0°, 90°, 180° and 270°, `/Rotate` and view rotation both (rule 6). **If it fails** S2.

**A-23 · Right-to-left order is MEASURED and the rule applied** ☐
**Goal** D5-6: what PDFium returns is a measurement, not a belief.
**Steps** Extract `rtl-text.pdf`'s visual-order page and its logical-order control.
**Expect** **The measurement is recorded here in words**: whether PDFium's order is logical. Then, whichever it was, the model's **reading-order** text of both pages equals the generator's logical string. If the analyzer had to reorder, the bound is stated at the case: strong-RTL runs only, no explicit embeddings. **If it fails** S2.

**A-24 · Extraction happens on the engine thread and nowhere else** ☐
**Expect** Every `extractPage` call records the render worker's thread id, and **none** records the UI thread's; a selection on an unextracted page enqueues at `Visible` and a search at `Background`, read from the scheduler's own counters. **If it fails** S1.

**A-25 · The backend implements `ITextExtractor`** ☐
**Expect** `implementedInterfaces()` reports it; `textExtractor(handle)` is non-null for an open document and refuses a foreign handle with `InvalidArgument`. **If it fails** S2.

**A-26 · Extraction over the whole corpus neither crashes nor fails a document for one bad page** ☐
**Steps** Extract every page of every openable corpus file, `malformed/` and `encrypted/` (with their passwords) included.
**Expect** No crash; a page the engine cannot read is reported **for that page** and its neighbours still extract. **If it fails** S1 for a crash; S2 otherwise.

### 5.5 A2 at Stage 5, and O7 (T3, D5-22)

**A-27 · The viewport's p95 during a whole-document index build** ☐
**Steps** `rawpage_bench_pipeline.exe --stage5` over `text-900.pdf`: a viewport's requests at `Visible` while all 900 pages are extracted at `Background`; three runs; shipped cap.
**Expect** A reported p95 per column beside Stage 4's figures, with the phase-0 latency control passing first. **The bench never fails on speed** — §3.1 grades. **If it fails** S2 only if the measurement could not be made.

**A-28 · The per-page extraction time is reported with its maximum** ☐
**Expect** p50, p95 and **max** extraction time per page over `text-900.pdf` and `a2-text-page.pdf`, beside the per-tile raster cost; the max is compared against the 16.667 ms budget in words. **If it fails** S2 if not reported.

**A-29 · Strict priority holds across the new lane** ☐
**Expect** Every text job enqueued at `Background`; **no visible tile completes after a background job it was enqueued before**, except the one job already inside the engine, bounded as Stage 4's A-16 bounded it; `droppedOverflow`, cancellations and undelivered counts reported. **If it fails** S1 for an unbounded inversion.

**A-30 · A folder search and a comparison, and the cache** ☐
**Expect** Two further columns: the viewport beside a folder search over a scratch copy of the corpus, and beside a comparison's extraction and 72-dpi rasters. **Text work inserts nothing into `RenderCache`**: its evictions and cross-document evictions are unchanged from the viewport-only column, and the comparison's rasters are never cache hits. At a small cap, reported as Stage 4 reported it. **If it fails** S2 — D5-4 is O7's answer, and this is its evidence.

**A-31 · The verdict is graded on §3.1 and recorded** ☐
**Expect** A written verdict in `Tech-Stack-Plan.md` §3.3 labelled **Stage 5's workload**, with the bench figures in their own block; O7's row in `Implementation-Plan.md` §8 carries A-30's evidence. **A no-go changes D5-3 before T5 is written.** **If it fails** S1 — *cannot be judged* is not a pass.

### 5.6 Normalization and layout (T4)

**A-32 · Ligatures are found both ways** ☐
**Expect** `fi` finds the `ﬁ` glyph mapped to U+FB01 and the one mapped to `fi`; `ﬁ` typed into a query finds both. **If it fails** S2.

**A-33 · Case folding does what D5-5 says** ☐
**Expect** Case-insensitive `LOREM` finds `lorem`; with `search/caseSensitive` on it does not (the control). **What `toCaseFolded` does not do is recorded at the case**: it is simple folding, so `ß` does not match `SS`, and the case asserts that behaviour rather than leaving it to be found. **If it fails** S2.

**A-34 · Diacritics, precomposed and decomposed** ☐
**Expect** With `search/ignoreDiacritics` on, `cafe` finds `café` precomposed and `cafe`+U+0301; with it off, `cafe` finds neither, while `café` finds **both** forms (NFKC composes them). **If it fails** S2.

**A-35 · The offset map highlights the glyphs that matched** ☐
**Expect** A hit on `f` inside a `ﬁ` glyph highlights the whole glyph; a hit on `e` of a decomposed `é` highlights the base and its mark; no highlight starts or ends mid-glyph. **If it fails** S2.

**A-36 · A line-final hyphen joins, and a mid-line one does not** ☐
**Expect** On `hyphenated.pdf`, `example` finds `exam-`⏎`ple`; `wellknown` does **not** find `well-known` (the control); `well-known` finds it. **If it fails** S2.

**A-37 · The analyzer on synthetic geometry** ☐
**Steps** `tst_layout` — headless, synthetic `PageTextModel`s built in the case.
**Expect** Baseline jitter of ±1 pt stays one line; a superscript stays in its line; a 12 pt gutter separates two columns and a 2 pt gap does not; ruled cells form a grid; **the controls** — two lines 30 pt apart are not merged, and aligned prose is not a table. **If it fails** S2.

**A-38 · The layout baseline, graded on §3.2** ☐
**Goal** D5-7 — the stage's second verdict.
**Steps** `tst_layoutcorpus` over every labelled page.
**Expect** Every §3.2 figure printed, and every target met. **The figures are recorded in R1's row** with the bound *"on our own generated corpus"*. **If it fails** S2 — a miss is logged, never smoothed.

**A-39 · Every product caller passes no tag tree, and the analyzer is deterministic** ☐
**Expect** The case states at its assertion that `TagTree` is null for every Stage 5 caller (convention 19) and that `tagged.pdf` is therefore ordered by geometry; the same model, its characters shuffled, gives byte-identical output. **If it fails** S3.

**A-40 · `fuzz_text_pipeline` exists, is counted, and runs** ☐
**Steps** `ctest --preset gates -R fuzz_count`; the `portable-fuzz` job's **log**.
**Expect** The count assertion at **four**, its cases proving it refuses three; the job runs `fuzz_text_pipeline` for its seconds with *"Done … runs"* and no crash; the soak runs all four. **If it fails** S2.

**A-41 · Hostile geometry is survived headless** ☐
**Expect** Characters with NaN, ±∞, zero and negative sizes, 10⁶ characters on one line and every box identical: the analyzer, selection and search return without crashing, looping or allocating beyond a stated bound. **If it fails** S1.

### 5.7 Selection and copy (T5)

**A-42 · A drag selects, and the highlight is on the ink** ☐
**Steps** `tst_selecttool`: a shown, laid-out view (rule 4) over `text-a4.pdf`; press, move and release through the tool.
**Expect** The selection is the generator's text between the two points; **the highlight's device rectangles contain the selected characters' ink and no other character's** (rule 6). **If it fails** S2.

**A-43 · Word, line and block** ☐
**Expect** A double-click selects the word under the pointer, a triple-click its line and a quadruple-click its block, each equal to the generator's word, line and block. **If it fails** S2.

**A-44 · A selection crosses a page boundary** ☐
**Expect** In continuous mode, a drag from page 1's last line to page 2's first selects both, and the copy joins them in order. **If it fails** S2.

**A-45 · A multi-column drag follows the READING order** ☐
**Expect** On `columns-two.pdf`, a drag from the foot of column 1 to the head of column 2 selects the end of column 1 and the start of column 2, and **nothing** of column 2's lower half — which is what an x-ordered selection would take. **If it fails** S2.

**A-46 · `Alt`+drag selects a column** *(Q2, approved 2026-09-29)* ☐
**Expect** On `table-ruled.pdf`, `Alt`+drag over the second column selects that column's cells only, and the copy is a one-column table. **The control:** the same drag without `Alt` selects in reading order. **If it fails** S2.

**A-47 · Right-to-left selection copies in logical order** ☐
**Expect** On `rtl-text.pdf`, dragging from the right edge of a Hebrew line leftwards selects from its first logical character; the plain-text copy equals the generator's logical string; the mixed line's digits keep their order. **If it fails** S2.

**A-48 · Vertical CJK** ☐
**Expect** On `cjk-vertical.pdf`, a drag down a column selects down it; the copy is in reading order, right column first; the highlight is on the ink (rule 6). **If it fails** S2.

**A-49 · One copy, three flavours** ☐
**Expect** After a flow selection over two blocks, the `QMimeData` holds `text/plain` (lines as seen, a blank line between blocks) and `text/html` (two paragraphs, sizes and bold as the generator wrote them); after a column selection, `text/plain` is tab-separated and `text/html` holds a `<table>` with the labelled grid. **If it fails** S2.

**A-50 · The HTML reaches Windows as CF_HTML** *(Q3, answered B 2026-09-29)* ☐
**Goal** D5-10's claim, measured instead of inferred from a string in `qwindows.dll`.
**Steps** `tst_clipboard`, a `ui.` case tagged `[D5-10]` that **skips under offscreen `ctest`** — the offscreen platform's clipboard never reaches Windows — and is run as Stage 4's A-85 and A-94 are: `$env:QT_QPA_PLATFORM='windows'; rawpage_ui_tests.exe "[D5-10]"`, hands off the desktop. Set the copy's `QMimeData`, then read the clipboard through Win32 — `RegisterClipboardFormatW(L"HTML Format")`, `GetClipboardData`.
**Expect** The format is present, its header's offsets are consistent, and its fragment contains the copied paragraphs. **What it does not cover:** how Word renders it — M-02. **If it fails** S2, and D5-10 is reopened.

**A-51 · `Ctrl+C` and `Ctrl+A` from key events** *(the `Ctrl+A` half: Q6, approved 2026-09-29)* ☐
**Expect** Sent as `QKeyEvent`s to the focused view: `Ctrl+C` puts the selection on the clipboard; `Ctrl+A` selects the whole document — on `text-900.pdf`, pages not yet extracted are extracted at `Background` with progress shown, and `Esc` during it cancels and leaves the clipboard **unchanged**. **If it fails** S2.

**A-52 · Every text widget keeps its own `Ctrl+C` and `Ctrl+A`** ☐
**Goal** Stage 4's I-06 turned round: a Common registration consumes the key where it falls through.
**Expect** With text in the find bar's field, the tool finder, a Preferences text editor and the log pane, `Ctrl+A` selects **that widget's** text and `Ctrl+C` copies **it**, not the document's selection. **If it fails** S2.

**A-53 · A selection changes no raster** ☐
**Expect** Making, extending and clearing a selection enqueues **no** render request and leaves every cached tile's key unchanged (the scheduler's counters before and after); the highlight is painted over the tiles (D4-11). **If it fails** S2.

**A-54 · `Esc` clears a selection, and otherwise does nothing** ☐
**Expect** With a selection, `Esc` clears it and is consumed; with none, `Esc` changes nothing and is **not** consumed — Stage 4's A-42 control, kept. **If it fails** S2.

**A-55 · Selecting on an unextracted page, and in a split** ☐
**Expect** On a page never extracted, the drag enqueues that page at `Visible` and the selection completes when it arrives; in a split, the selection shows in both panes; on a tab switch it belongs to its own tab. **If it fails** S2.

### 5.8 Search and the index (T6)

**A-56 · Every flag, with its control** ☐
**Steps** `tst_search`, parameterized over the four flags.
**Expect** Each flag on finds what it should and each flag off does not — whole words (`lore` against `lorem`), case, diacritics, regex (`lo.em`, and a literal `lo.em` that must **not** match `lorem` with regex off). **If it fails** S2.

**A-57 · An invalid regular expression searches nothing and says so** ☐
**Expect** `(`, `[a-`, `*x`: an error naming the pattern, **no** search run, no hit list. **The control:** a valid pattern after it searches normally. **If it fails** S2 — an empty result would read as *not found*.

**A-58 · CJK search, and whole words in CJK** ☐
**Expect** `中日` finds its occurrences in `text-tounicode.pdf`'s CJK page; with whole words on, each ideograph is a word, as §7 records. **If it fails** S2.

**A-59 · Search across a line break** ☐
**Expect** A two-word query found where the words are split across lines, and **not** across a block boundary. **If it fails** S2.

**A-60 · Scope: a selection and a page range** ☐
**Expect** With a selection, only hits inside it; with pages 3–5, only hits on those pages; **the control** — whole document returns hits outside both. **If it fails** S2.

**A-61 · `search/maxResults` bounds the list and says so** ☐
**Expect** With the key at 10 over a query with 900 hits, 10 are listed and a message says the limit was reached. **If it fails** S3.

**A-62 · The index is built LAZILY** ☐
**Expect** Opening `text-900.pdf` and scrolling extracts **only** the pages selected on; the first search then extracts the rest at `Background`. **The control:** no search, no full extraction. **If it fails** S2.

**A-63 · The second search is instant, graded on §3.3** ☐
**Expect** 20 literal queries after the index is complete, p95 of submission-to-last-hit printed and **≤ 100 ms**; the first search's time printed and not graded. **If it fails** S2.

**A-64 · A mutation's `Scope` invalidates exactly its pages** ☐
**Expect** After Stage 3's `page.rotate` on page 4, page 4 is re-extracted on the next search and no other page is. **If it fails** S2.

**A-65 · The index budget evicts whole documents, least recently searched first** ☐
**Expect** With `search/indexCacheMB` small and three documents searched in turn, the first searched is evicted first, the others survive; the budget is never exceeded after a search completes. **If it fails** S3.

**A-66 · Two windows over one file share ONE index** ☐
**Goal** D5-4 against Stage 4 T14's finding.
**Expect** After `window.new`, a search in the second window extracts nothing the first already extracted; both windows' hits agree. **If it fails** S2.

**A-67 · Matching runs off the UI thread, and a newer query cancels the older** ☐
**Expect** Every match call records a thread that is neither the UI thread nor the engine thread; a query issued while another runs cancels it, and only the newer delivers. **If it fails** S2.

**A-68 · The ten search keys exist, the Search page ships, and the values survive a restart** ☐
**Expect** Ten `search/*` rows; `shippedPages()` includes Search (**nine of 26**); each value written, the product closed and reopened **as a process**, read back — Stage 4's I-15 was a preference that never survived a real restart. **If it fails** S2.

### 5.9 The find bar (T7)

**A-69 · Every Find / Search chord, from a key event** ☐
**Steps** `tst_findbar`: a shown, laid-out window; `QKeyEvent`s to the focused widget.
**Expect** `Ctrl+F` opens the bar and focuses it; with focus in it `activeContext()` is `FindSearch`; `F3`, `Shift+F3`, `Ctrl+G`, `Ctrl+Shift+G`, `Enter`, `Alt+R`, `Alt+W`, `Alt+C` and `Esc` each do what §8 says, **asserted on the effect** (rule 1) — the current hit moved, the flag's setting changed, the bar closed. **If it fails** S2.

**A-70 · Incremental, debounced and cancelled** ☐
**Expect** Typing `lorem` one character at a time runs at most one search per debounce interval and delivers only the last query's hits. **If it fails** S3.

**A-71 · Highlight all, the current hit, and wrap** ☐
**Expect** With *highlight all* on, every hit on the visible pages is highlighted and the current one distinguished; off, only the current one; `F3` past the last hit wraps to the first and says so. **If it fails** S2.

**A-72 · `Esc` closes the bar and clears every highlight** ☐
**Expect** No highlight painted after `Esc`, and focus returns to the view. **If it fails** S3.

**A-73 · The toggles ARE the settings** ☐
**Expect** `Alt+R` changes `search/useRegex`, and changing `search/useRegex` in Preferences changes the bar's toggle — both directions, the same for `Alt+W` and `Alt+C`. **If it fails** S2 — two truths.

**A-74 · Hits are painted at presentation, on the ink** ☐
**Expect** No render request enqueued by a search; every visible hit's highlight contains its characters' ink (rule 6). **If it fails** S2.

**A-75 · `Ctrl+F` from the page and from a pane; hits in a split** ☐
**Expect** `Ctrl+F` opens the bar from the Viewer and from the Thumbnails pane (Common); in a split, hits show in both panes. **If it fails** S2.

### 5.10 The other four content classes (T8)

**A-76 · Each class is searched only when its toggle is on** ☐
**Steps** `tst_search` over `search-classes.pdf`, each include flag on and off.
**Expect** Each class's unique word is found **only** with that class's toggle on; page text always; **the control word is never found**. **If it fails** S2.

**A-77 · Each class's hit navigates as D5-12 says** ☐
**Expect** A comment hit goes to its page and rectangle; a field hit to its widget; a bookmark hit to its destination; **an attachment hit is selected and opens nothing**. **If it fails** S2.

**A-78 · An attached PDF's text is searched one level deep** ☐
**Expect** The attached PDF's unique word is found with `search/includeAttachments` on; a PDF attached **inside** that attachment is **not** searched, and the case says so. Nothing is written to disk to open it. **If it fails** S2.

**A-79 · Search units are read once, on the engine thread, and only when needed** ☐
**Expect** No `searchUnits` call before a search that includes a non-page class; exactly one after, on the engine thread. **If it fails** S3.

### 5.11 Advanced search (T9)

**A-80 · A folder, recursive and not** ☐
**Steps** `tst_searchpane` and a `ui.composition.` case over a scratch copy of the corpus with nested folders — never the tester's own.
**Expect** Without recursion, only the top folder's hits; with it, every level's; every hit names its file and page. **If it fails** S2.

**A-81 · An encrypted file is skipped with a reason; a broken one is reported** ☐
**Expect** `encrypted/*.pdf` listed as skipped with *encrypted* and **no password prompt**; each `malformed/` file either searched after repair or listed with its error; the search carries on. **If it fails** S2; S1 for a prompt or a stop.

**A-82 · Snippets are correct** ☐
**Expect** Each snippet contains the hit and the words either side as the generator wrote them, cut at word boundaries, with the hit marked. **If it fails** S2.

**A-83 · Sorting, with relevance defined** ☐
**Expect** By relevance: files in descending hit count, ties by path; by page; by file — each asserted on a list whose order would differ under the others. **If it fails** S3.

**A-84 · Filtering by metadata** ☐
**Expect** Filtering by author, subject and a date range keeps exactly the files whose Info says so; a file with no Info is excluded by an author filter and **included** with no filter (the control). **If it fails** S3.

**A-85 · Refine** ☐
**Expect** A second query over the first's results returns only pages carrying both. **If it fails** S3.

**A-86 · Export writes the list as CSV, through the one waiver** ☐
**Expect** The file begins with a UTF-8 BOM; one row per hit with file, page and snippet; commas, quotes and newlines in a snippet are quoted per RFC 4180; CJK and Hebrew survive a byte round trip. **If it fails** S3.

**A-87 · Activating a hit opens the file at the hit** ☐
**Expect** The file opens in a tab through `MainWindow::openFiles` at the hit's page, highlighted; an already-open file is **not** opened twice. **If it fails** S2.

**A-88 · The UI thread opens nothing, and a folder search can be stopped** ☐
**Expect** Every `open` of a folder search is on the engine thread (thread ids); cancelling between files stops within one file; `search/maxResults` bounds it across files. **If it fails** S1 for a UI-thread open.

**A-89 · `Ctrl+Shift+F` opens Advanced Search from the page** *(Q1, approved 2026-09-29)* ☐
**Expect** From the Viewer, as a key event, the pane opens and focuses its query field; **with focus in the find bar it does the same**, through the Global layer — the row moved there from Find / Search rather than being duplicated (D5-15). **The control:** `tst_keybindings`' class-1 census is still exactly the six §8 names, `Ctrl+Shift+F` not among them. **If it fails** S2.

### 5.12 Snapshot (T10)

**A-90 · The snapshot is the document as drawn, and nothing else** ☐
**Steps** `tst_snapshot`: `S` as a key event; a drag over a region; the clipboard image.
**Expect** The image equals a fresh render of that region at the view's scale within the golden suite's threshold; **with colour inversion, the grid, guides, a selection and search highlights all on, the image is unchanged** (the control); a view rotation of 90° rotates it. **If it fails** S2.

**A-91 · `Esc` abandons, and a click is not a snapshot** ☐
**Expect** `Esc` mid-drag leaves the clipboard unchanged; a drag under the minimum size copies nothing, as the marquee refuses one. **If it fails** S3.

**A-114 · The snapshot's resolution is `pageDisplay/snapshotDpi`** *(added 2026-09-29 with Q3's answer)* ☐
**Goal** D5-11's resolution, its sentinel and its bound.
**Steps** `tst_snapshot`: the same region snapshotted with the key at 0, 72, 300, 600 and 40; a whole A3 page at 600.
**Expect** At **0** the image's size is the region at the view's device scale; at 72, 300 and 600 it is the region at that resolution, within one pixel; **40 is taken as 72**; the A3 page at 600 dpi is **reduced to fit 50 megapixels** and the status bar says so — asserted on the image's size, not on the message alone (rule 1). The key survives a restart of the product as a process (Stage 4's I-15). **The control:** at 300 the image equals a fresh render of the region at 300 dpi within the golden threshold, which a bitmap merely scaled up from the view would fail. **If it fails** S2; S1 if the bound fails and memory runs out.

### 5.13 Reflow view (T11)

**A-92 · `Ctrl+4` toggles, and the text is the analyzer's order** ☐
**Steps** `tst_reflow` over `columns-two.pdf`.
**Expect** `Ctrl+4` as a key event shows the reflow; its text is column 1 then column 2, equal to the analyzer's reading order; `Ctrl+4` again returns to the page. **If it fails** S2.

**A-93 · The reader's place survives both ways** ☐
**Expect** The block at the top of the page view is at the top of the reflow, and after scrolling the reflow the page view returns to the block now at its top. **If it fails** S3.

**A-94 · Zoom changes text size; nothing is rasterized; a hit leaves reflow** ☐
**Expect** `Ctrl+=` and `Ctrl+-` change the reflow's font size; no render request is enqueued while reflow shows; activating a find hit returns to the page view at the hit. **If it fails** S3.

### 5.14 The comparer (T12)

**A-95 · Every labelled change, with its kind** ☐
**Steps** `tst_comparecorpus` over every `corpus/compare/` pair against its `expected.json`.
**Expect** §3.4's bar: every insertion, deletion, replacement, move, image move, page insertion, deletion and reordering, header change and page-number shift found and classified. **If it fails** S2.

**A-96 · An identical pair reports nothing** ☐
**Expect** Zero differences at **every** sensitivity. **If it fails** S2.

**A-97 · Two unrelated documents finish, bounded** ☐
**Expect** Within §3.4's 10 s and 256 MB of live heap; pages reported *replaced* past the cap rather than diffed. **If it fails** S2; S1 for a hang.

**A-98 · Each ignore-list, with its control** *(page numbers: Q7, approved 2026-09-29)* ☐
**Expect** With header/footer ignored, the header change is not reported, and with it off it is; the same for page numbers and for whitespace. **A body-text number change is reported with page numbers ignored** — the control that separates the band from the digits. **If it fails** S2.

**A-99 · The sensitivities layer as D5-17 says** ☐
**Expect** *Text only* reports no image move; *text and layout* reports a moved paragraph's shift; *appearance* reports the image move, and **not** as a second report of a text change it already covers. **If it fails** S2.

**A-100 · A moved paragraph is a move** ☐
**Expect** One move, not a deletion and an insertion; below the move length it is a deletion and an insertion (the control). **If it fails** S3.

**A-101 · Pages are aligned before text is diffed** ☐
**Expect** After an inserted page, every later page is matched to its counterpart and reported unchanged. **If it fails** S2.

**A-102 · The diff runs off the UI thread, with progress and cancel** ☐
**Expect** Thread ids; progress reported monotonically; a cancel stops within one page pair. **If it fails** S2.

### 5.15 The comparison surface (T13)

**A-103 · `Ctrl+Alt+C` from a key event** ☐
**Expect** From the Viewer it dispatches `compare.documents`. **Offscreen the dialog is refused** (convention 16), so the view is then built from the dialog's parameters directly — and the case says at its assertion that this is a back door that does not cover the dialog's own widgets (convention 19); the four `documents/compare*` keys exist. **If it fails** S2.

**A-104 · Side by side scrolls by ALIGNED page** ☐
**Expect** On the inserted-page pair, scrolling to the older document's page 5 shows the newer document's page 6 beside it. **If it fails** S2.

**A-105 · Overlay composites both documents** ☐
**Expect** Where the pair is identical the overlay shows one image; where it differs, both tints are visible — asserted on the raster (rule 6). **If it fails** S3.

**A-106 · `Ctrl+]` and `Ctrl+[` step both views and the pane** ☐
**Expect** Each step moves both views to the difference and selects its row in the Differences pane; the ends wrap with a message. **If it fails** S2.

**A-107 · A comparison changes neither document** ☐
**Expect** Both journals empty, both files byte-identical after a comparison and its view are closed; no save offered. **If it fails** S1.

### 5.16 The stage-wide sweeps (T14)

**A-108 · The action census, and the Viewer census** ☐
**Steps** `rawpage.exe --list-actions` through `cmd`; `tst_viewercommands`.
**Expect** The declared, registered and unregistered counts **re-derived** and equal to the census case; the Viewer census at **35 registered, 4 not** — `Ctrl+B`, `Ctrl+E`, `Ctrl+.`, `Ctrl+,` — each unregistered chord leaving its key alone. **If it fails** S2.

**A-109 · Every prior stage's case is present by name** ☐
**Expect** The list of test names before Stage 5 (taken at T1) is a subset of the list after. A case renamed says so in the status block. **If it fails** S2.

**A-110 · The golden suite is green and no existing baseline moved** ☐
**Expect** `git diff 1128e7a..HEAD --stat -- golden/` shows only additions for T1's files. **If it fails** S2 — Stage 5 rasterizes nothing new.

**A-111 · Both presets green** ☐
**Expect** `ctest` green on `x64-release` **and** `x64-debug`; the named skips are Stage 4's three plus **A-50**, which runs on the `windows` platform instead, and no other; `gates` preset green. **If it fails** S1.

**A-112 · A2 re-read through the shipped surfaces** ☐
**Steps** `rawpage_bench_surface.exe` with Stage 5 columns: the shipped find bar's first search over `text-900.pdf`, and the shipped search pane over a scratch folder, the harness issuing no request itself.
**Expect** The viewport's p95 per column, **with the dpi and viewport it reached printed** (Stage 4's A-72), graded on §3.1 and recorded beside T3's figure — never instead of it. **If it fails** S2 if not measurable.

**A-113 · The UI binary runs whole** ☐
**Steps** `rawpage_ui_tests.exe` in declaration order and under three printed seeds, both presets.
**Expect** Every run green; `ProcessStateGuard` (Stage 4's A-103) silent. **If it fails** S2 — a case leaks.

---

## 6. Manual verification — you run this

Nine cases, **each on a copy of a generated corpus file** (P-2, P-3, plan D5-23) and each asking for a
judgement no assertion makes. **Walk
each at the task that makes it walkable**, not at the end — Stage 4's first three walks each found a
defect the complete automated half had ratified:

| Walk at | Cases |
|---|---|
| **T5** | M-01, M-02, M-04, M-05 |
| **T7** | M-06 |
| **T9** | M-07 |
| **T10** | M-03 |
| **T11** | M-09 |
| **T13** | M-08 |

Three things about the state you will be walking, so a planned absence is not reported as a defect:

- **Nothing you do in Stage 5 changes a document.** If a file's *modified* time moves, that is a
  finding.
- **There is no Replace**, no PDF comparison report, and a search hit in an attachment opens nothing.
- **Walk on copies in `tests\manual\<case>\`**, never on the corpus files themselves.

---

**M-01 · Selecting text in a multi-column document** ☐
- **Goal** D5-8 judged by a person: the highlight on the glyphs, the order a reader expects.
- **Pre** T5. Copies of `corpus\ordinary\columns-two.pdf`, `columns-three.pdf` and `table-ruled.pdf` in `tests\manual\M-01`. The P-1 rig from Stage 4 if available.
- **Steps**
  1. Open `columns-two.pdf`. With the Selection tool (`V`), drag across a paragraph, then from the foot of one column to the head of the next. Repeat on `columns-three.pdf`.
  2. Double-, triple- and quadruple-click a word.
  3. In `table-ruled.pdf`, `Alt`+drag down one column of each table.
  4. In continuous mode, drag across a page break.
  5. Repeat step 1 at 50 % and 400 %, and — if you have two monitors — on each.
- **Expect** The highlight sits **on the glyphs** at every zoom and on both monitors; a drag between columns takes the end of one and the start of the next, not a band across both; the word, line and block are what a reader would call them.
- **If it fails** S2; S1 if the copy contains text that is not on the page.

**M-02 · Copy into Word, Notepad and a spreadsheet** ☐
- **Goal** D5-9's flavours judged by the programs that consume them. P-1.
- **Pre** T5. M-01's copies.
- **Steps**
  1. Select two paragraphs with a heading; `Ctrl+C`; paste into Word. Paste the same into Notepad.
  2. `Alt`+drag a table's columns; `Ctrl+C`; paste into Word and into Excel (or the spreadsheet you have).
  3. `Ctrl+A`, `Ctrl+C`; paste into Notepad.
- **Expect** Word shows paragraphs, not a line per line, with the heading larger or bold; Notepad shows the lines as they were on the page; the table pastes as cells in both; step 3 pastes the whole document in reading order, after a progress indication if it was long.
- **If it fails** S2; S3 for formatting that is merely plain.

**M-03 · A snapshot into an image editor** ☐
- **Pre** T10. P-5. A copy of `corpus\ordinary\image-dct-rgb.pdf` and of `columns-two.pdf` in `tests\manual\M-03`.
- **Steps** Press `S`; drag over a region with text and part of an image; paste into Paint. Repeat with colour inversion and the grid on. Repeat at 400 %. Then set **Preferences → Page Display → snapshot resolution** to **300**, take the same region at 100 %, paste it into Word and enlarge it to a page's width.
- **Expect** The pasted image is the region as the document draws it, sharp at the zoom you took it at; the inversion and the grid **do not** appear in it; at 300 dpi the image in Word stays sharp at a page's width, where the one taken at the default is visibly soft.
- **If it fails** S2.

**M-04 · Right-to-left text** ☐
- **Pre** T5. A copy of `corpus\ordinary\rtl-text.pdf` in `tests\manual\M-04`. **Its Hebrew is drawn in synthetic glyphs** — no predefined CMap reaches Hebrew without embedding a third-party font (D5-23) — so this case judges the **paste, the highlight and the find**, not the letterforms on the page.
- **Steps** Select a sentence by dragging from its start (the right); copy; paste into Notepad. `Ctrl+F` a word from it. Select a line mixing RTL text and numbers.
- **Expect** The pasted sentence reads correctly — in logical order, not reversed; the find highlights the word where it is; numbers keep their order.
- **If it fails** S2.

**M-05 · CJK text, horizontal and vertical** ☐
- **Pre** T5. Copies of `corpus\ordinary\text-tounicode.pdf`, `cjk-vertical.pdf` and `cjk-embedded.pdf` in `tests\manual\M-05`. **`cjk-embedded.pdf` has no Unicode mapping** and is here on purpose: step 1 on it must be refused, not pasted.
- **Steps** In each file, select, copy and paste a line into Notepad; `Ctrl+F` a two-character word; in `cjk-vertical.pdf`, select down a column.
- **Expect** The paste is the characters on the page; the find highlights them; a vertical selection runs down the column and copies right column first. **On `cjk-embedded.pdf` the product says its text cannot be extracted** and pastes nothing — that is D5-2 working; a paste of symbols there is the S1.
- **If it fails** S2; S1 for pasted characters that are not on the page.

**M-06 · Find on a long document, and the page stays responsive** ☐
- **Goal** The human half of A2 at Stage 5 (§3.1).
- **Pre** T7. A copy of `corpus\ordinary\text-900.pdf` in `tests\manual\M-06\`, freshly opened.
- **Steps**
  1. `Ctrl+F`, type a word, and **while the first search is still indexing**, scroll the page view fast, page with `Ctrl+Page Down`, and zoom.
  2. When it finishes, search another word. `F3` through several hits, past the last.
  3. Toggle `Alt+W`, `Alt+C`, `Alt+R` with suitable queries; `Esc`.
- **Expect** The page keeps drawing while the index builds; the second search answers at once; `F3` wraps with a message; the toggles change the hits as their names say; `Esc` clears every highlight.
- **If it fails** S1 if the page stops drawing; S2 otherwise.

**M-07 · Advanced Search over a folder tree** ☐
- **Pre** T9. A copy of the whole `corpus` tree in `tests\manual\M-07\corpus` (P-3).
- **Steps** `Ctrl+Shift+F` (Q1); search the folder with and without subfolders; sort by relevance, page and file; filter by author; refine; export the list to `tests\manual\M-07\` and open it in Excel; activate a hit in a file that is not open.
- **Expect** The window stays responsive throughout; the five files under `encrypted` are listed as skipped, with no password prompt, and the `malformed` ones are searched or listed with their error; snippets read correctly; the author filter keeps only the files T1 gave that author; the export opens in Excel with its columns and its CJK and Hebrew intact; the hit opens its file at the right page, highlighted.
- **If it fails** S2; S1 for a freeze.

**M-08 · Compare two revisions of a report** ☐
- **Pre** T13. Copies of `corpus\compare\report-v1.pdf` and `report-v2.pdf` in `tests\manual\M-08`, and **the list of edits in `report.expected.json`** open beside them.
- **Steps** `Ctrl+Alt+C`; compare at *text only*, then *appearance*; turn the header/footer and page-number ignores on and off; step with `Ctrl+]` and `Ctrl+[`; switch to overlay.
- **Expect** Every edit in the list is reported, and what is reported reads as the change a person made — note any that do not; nothing outside the list is reported at *text only*; the two views stay on corresponding pages after the inserted page; the ignores silence the footer date and the page numbers; the overlay shows both tints where the report changed. Neither file's modified time moves.
- **If it fails** S2 for a missed or invented change; S1 for a changed file.

**M-09 · Reflow view on a multi-column document** ☐
- **Pre** T11. A copy of `corpus\ordinary\columns-three.pdf` in `tests\manual\M-09`.
- **Steps** `Ctrl+4`; read a page's worth; resize the window; `Ctrl+=`; `Ctrl+4` back.
- **Expect** The text reads in the order a person would read it, re-wrapped to the window, and `Ctrl+4` returns to the place you were reading.
- **If it fails** S2 for an order a reader would not follow; S3 for the place lost.

---

## 7. Known caveats — verify, do not assume broken

Behaviours that look like defects and are not. **A wrong row here is worse than a missing one.**

| Looks broken | Is |
|---|---|
| **Some documents' text cannot be selected, copied or found** | Correct when the font carries no usable Unicode mapping — the product reports it rather than inventing text (D5-2). Record which document; a document whose text **is** mappable and is not found is the defect |
| **Whole-words-only finds single CJK characters inside longer words** | Correct at Stage 5: without a dictionary segmenter each ideograph is a word (D5-5) |
| **`ß` is not found by `SS`** | Correct: Qt's case folding is simple folding (D5-5, A-33) |
| **The Hebrew in `rtl-text.pdf` is drawn as blocks, not letters** | Correct — the corpus embeds no third-party font, and nothing else reaches Hebrew (plan D5-23). The paste into Notepad is what reads |
| **A tagged document reads in geometric order** | Correct until Stage 7 reads its tags (D5-6) |
| **Reflow view loses the pictures and the typefaces** | Correct — it is text re-wrapped (D5-16) |
| **The first search of a long document takes a while; the second does not** | Correct — the first builds the index at background priority (D5-3, D5-4) |
| **A search hit in an attachment does not open it** | Correct until Stage 6's Attachments pane (D5-12) |
| **A pasted snapshot blurs when scaled up in the editor** | Correct at the default, *as displayed* — it is a bitmap (D5-10, `DW-2`). Raising `pageDisplay/snapshotDpi` is the answer (D5-11) |
| **The comparison view has no rulers** | Correct (D5-18) |
| **A folder search skips a protected file** | Correct — a folder search never prompts (D5-13) |
| **`Ctrl+C` in a tree pane copies the page selection** | Correct — `Ctrl+C` is §8's Common row, and a pane with nothing of its own to copy falls through to it (D5-9) |

---

## 8. Issue log

Numbered `I-nn`, **this stage's own namespace**. Each entry carries its mechanism, why the suite
missed it, and the control that was run red before the fix — the shape every earlier stage's log
uses.

| # | Sev | Title | State |
|---|---|---|---|
| | | *No issues yet — no Stage 5 code exists.* | |

---

## 9. Sign-off criteria

Stage 5 is done when **all** of these hold — [`Stage5-TextSearch-Plan.md` §13](./Stage5-TextSearch-Plan.md#13-definition-of-done-for-stage-5)
restated as a checklist.

1. ✅ **2026-09-29.** The plan's §0 answered — all seven, the same day it was written — and every case whose title depends on an answer checked against it: A-46, A-50, A-51, A-89 and A-98 stand as written (A-89's Expect gained the find-bar half and its control when Q1's row moved), and **A-114** was added for Q3's resolution key.
2. ☐ Every automated case carries a result, recorded against it rather than summarized (Stage 1's I-32); Linux-only halves read from the job's **log**.
3. ☐ M-01 … M-09 **walked and signed off by you**. I do not mark a manual case.
4. ☐ `checks` clean — **`mutation-lint` one command** (A-07), `Commands.def` byte-unchanged (A-08), **15** waivers with the new one named (A-09).
5. ☐ `ctest` green on **both** presets and the `gates` preset, every earlier case present by name (A-109, A-111); the UI binary green whole (A-113).
6. ☐ `portable-asan` clean over every `core/text/` suite; `portable-fuzz` and the soak clean on **four** targets (A-04, A-40).
7. ☐ The golden suite green with every existing baseline unchanged (A-110).
8. ☐ **A2 at Stage 5** recorded, bench and product, graded on §3.1; **the layout baseline** recorded against §3.2 in R1's row; **O7's revisit** in its row (A-31, A-38, A-112).
9. ☐ No open S1 or S2 in §8.
10. ☐ `Architecture-FileMap.md` rows flipped with their files; the plan's §10 debt paid; `Implementation-Plan.md`'s Stage 5 block updated to reality, deviations included.
