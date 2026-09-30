# Stage 5 — Text Layer: Extraction, Selection, Find, Search, Comparison · Execution Plan

> **What this document owns.** The **execution order** of Stage 5 and the **Stage-5-local design
> choices** that no other document decides. It owns no product facts. Every stack, layering,
> key-binding and verification fact is cross-referenced to its owner and never restated as a new
> decision:
>
> | For… | Read |
> |---|---|
> | The Stage 5 objectives, components, key bindings handled, settings keys, exit criteria and deferrals | [`Implementation-Plan.md` § Stage 5](./Implementation-Plan.md#stage-5--text-layer-extraction-selection-find-search-comparison) |
> | The layering rule, the engine boundary (`ITextExtractor`, `IIndexer`), risk **A2**, risks **R1** and **R2** | [`Tech-Stack-Plan.md` §3.3, §4.2, §10](./Tech-Stack-Plan.md) |
> | What each file is for, and flows **G** (find, then advanced search) and **C** (a tile) | [`Architecture-FileMap.md` §3.4, §3.5, §3.8, §3.10, §3.12, §3.14, §3.15, §5](./Architecture-FileMap.md) |
> | The authoritative key bindings — the whole **Find / Search** table, the Viewer rows `Ctrl+4`, `S`, `V`, `Ctrl+Alt+C`, `Ctrl+]`/`Ctrl+[`, and the Common rows `Ctrl+C`, `Ctrl+A`, `Ctrl+F` | [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference); the capabilities in §1.2 (Reflow), §1.4, §1.5 and §1.8 |
> | How Stage 5 is verified | [`Stage5-TextSearch-Testing-Guide.md`](./Stage5-TextSearch-Testing-Guide.md) — written from this plan, **before** the code |
>
> **Status:** ⚪ **Written 2026-09-29, two days after Stage 4 was signed off, as a draft; §0 ANSWERED
> the same day, all seven — so this is now the plan. No Stage 5 code exists.** Q1, Q2, Q5, Q6 and Q7
> were approved as proposed (Q1 with the correction D5-15 records); Q3 and Q4 were answered with
> **option B** after the user asked for more detail — a snapshot resolution setting (D5-10, D5-11) and
> the comparison's PDF outputs given to **Stage 8** (D5-17). §0 is kept as the record of what was
> asked, because several decisions in §3 change scope or a `Features.md` §8 binding. This file is a plan, not a progress log: Stage 5's live status, task by task and
> deviation by deviation, will be the **Stage 5 status block in
> [`Implementation-Plan.md`](./Implementation-Plan.md#stage-5--text-layer-extraction-selection-find-search-comparison)**,
> which is the authority.
>
> **Preconditions answered 2026-09-29:** P-1 is Word from Office LTSC Professional Plus 2024; P-2 and
> P-3 are **generated**, on the user's decision — **D5-23**, which also bounds every Stage 5 claim to the
> generated corpus.
>
> **Label scoping, because five namespaces now collide across stages.** `T1`…`T14` here are
> **Stage 5** tasks and are unrelated to any earlier stage's; this document writes "Stage 5 T6"
> wherever the stage is not already obvious. `D5-n` are this stage's decisions, distinct from
> `Tech-Stack-Plan.md`'s `D1`…`D10` and from `D2-n` … `D4-n`. `A2` is a **risk**
> (`Tech-Stack-Plan.md` §3.3); `A-nn` are **test cases**
> (`Stage5-TextSearch-Testing-Guide.md` §5). A bare `I-nn` in this file is **Stage 4's**
> (`Stage4-ViewingSurface-Testing-Guide.md` §8) unless it says otherwise, because the lessons this
> stage carries forward are mostly Stage 4's.

---

## 0. Decisions this draft needs from you before T1

Each is argued in §3. None of these can be settled from the documents alone. Each one moves a
`Features.md` §8 binding, a capability's stage, or a file's stage. A later stage would inherit
whatever this plan wrote down, so it is asked now rather than decided here.

| # | The question | This draft proposes | Why it is yours |
|---|---|---|---|
| **Q1** ✅ | `Ctrl+Shift+F` (Advanced Search) is bound only in the **Find / Search** context, so from the page it does nothing — while §8's own *Global defaults recap* calls it a global default | **Add a Global row to §8** so it opens the pane from anywhere (**D5-15**) | §8 is the authority on bindings, and the table and its recap disagree |
| **Q2** ✅ | Column (rectangle) text selection needs a gesture | **`Alt`+drag with the Selection tool**, added to §8's Viewer table as a row (**D5-8**) | A new §8 row |
| **Q3** ✅ | `platform::IRichClipboard` — the vector-metafile flavour is the only part Qt cannot do, and producing one would need PDFium's HDC path, a Windows call `pdf/render/` may not make | **Answered 2026-09-29: option B.** Snapshots are bitmaps, taken at a resolution the user sets (`pageDisplay/snapshotDpi`, **D5-11**); formatted copy goes through Qt's own CF_HTML; `WinRichClipboard` is **not built at Stage 5** and moves to **Stage 12**, whose entry already lists it for its paste path; the vector snapshot is **`DW-2`**, unassigned (**D5-10**) | Moves a file between stages and leaves one capability with no owner |
| **Q4** ✅ | *Generate a difference summary page* and *Export the comparison as an annotated PDF report* (`Features.md` §1.5) both **write a PDF**, which needs Stage 6's appearance streams and a stage that writes new files | **Answered 2026-09-29: option B.** The summary at Stage 5 is the Differences pane; **both PDF outputs are taken by Stage 8** (*Save, export and optimize*), in its `Implementation-Plan.md` entry (**D5-17**) | Gives two catalog lines to a later stage |
| **Q5** ✅ | `pdf/cos/ContentStreamParser` is a Stage 5 component with **no Stage 5 consumer**: extraction is PDFium's, and PDFium also answers marked content per character | **Move it to Stage 10**, whose `RedactionOps` is its first real caller (**D5-19**) | Moves a component between stage entries |
| **Q6** ✅ | `Ctrl+A` in the Viewer: the current page, or the whole document? | **The whole document** — Acrobat's meaning — with the text extracted in the background before a copy completes (**D5-9**) | A behaviour a person will feel on a 900-page file |
| **Q7** ✅ | The ignore-lists in §1.5 name *page numbers* separately from *headers/footers*, and `Implementation-Plan.md` declares no key for them | **Declare `documents/compareIgnorePageNumbers`**, a fourteenth key (**D5-20**) | A settings key the roadmap did not list |

**All seven answered on 2026-09-29.** Q1, Q2, Q5, Q6 and Q7 were approved as proposed — Q1 with one
correction found while carrying it out (D5-15: the row MOVES to Global rather than being duplicated).
Q3 and Q4 were answered with **option B** of the alternatives set out for the user that day. Every
correction the answers carry is made, and §10 records where.

---

## 1. What Stage 5 is actually for

Stages 2 to 4 made a document something a person can **see** and **move around in**. Stage 5 makes
its **words** usable: select them, copy them, find them, search a folder for them, and see which of
them changed between two revisions. It is the last stage of milestone **M1 — Reader**
(`Implementation-Plan.md` §6). It is also the first stage whose output is **the document's meaning**
rather than its pixels. A pixel is wrong where anyone can see it. A wrong reading order is wrong in
the clipboard, in someone else's Word document, an hour later.

Three things get permanently harder if this stage gets them wrong.

1. **`LayoutAnalyzer` is the foundation of Stage 13, not a detail of Stage 5.** Reflow view here,
   and Write mode's *Convert to Writable* later, both rest on how glyphs are grouped into lines, lines
   into blocks and blocks into a reading order (`Architecture-FileMap.md` §3.5: *"the quality of
   Write mode and Reflow view both rest on this one file"*). Risk **R1** is the largest unknown in the
   product (`Tech-Stack-Plan.md` §10), and this is the stage that can hand it a **measured** starting
   point instead of a hope. → **D5-6**, **D5-7**.
2. **Rendering now has to share PDFium with text.** PDFium cannot be called from two threads at once
   (Stage 2's I-05). The product's answer is **one render thread** with an engine lock. Extracting
   the text of 900 pages for a first search, a folder search or a comparison is new engine work, and
   it competes with the tile a person is waiting for. A2's product figure has held since Stage 2, and
   Stage 4 T14 re-read it through the shipped panes at **7.03–8.56 ms**
   (`Tech-Stack-Plan.md` §3.3). Stage 5 is where that headroom is spent or kept. → **D5-3**,
   **D5-22**.
3. **Text is the first derived model that outlives a frame.** A tile is thrown away and re-rendered.
   An extracted page, a search index and a comparison result are kept, so they have a size, an owner,
   an identity and a moment when they go stale. **O7** (`Implementation-Plan.md` §8) was resolved
   for one consumer, tiles, and names Stage 5 as its revisit. Stage 4 T14 left evidence for it:
   `window.new` makes one file two documents to the cache. → **D5-4**.

**What Stage 4 should change about how this stage is built.** Stage 4 raised thirty-one issues, and
**twenty of them came from a person looking at the product, or from running something, after a
complete, green automated suite.** Four of its lessons apply here with more force:

- **The oracle for a geometric claim is the RASTER, not the model** (Stage 4's **I-10**: Fit Page
  measured the sheet while the product drew the crop, and the suite agreed with the product because
  it asserted the same arithmetic the product used). A selection highlight computed from a char box,
  and a test that checks the highlight against the same char box, agree when both are wrong. **Every
  Stage 5 geometry case compares against the rendered ink**, found in the raster by a method that
  shares no code with the model's transform.
- **The oracle for a text claim is the GENERATOR, not the engine.** A test that compares
  `PdfiumTextExtractor` with PDFium's own `FPDFText_GetText` proves that PDFium agrees with itself.
  The corpus generator knows exactly which characters it put where, in which order, and with which
  meaning. That is the ground truth, and T1 makes the generator write it down.
- **Walk early.** Stage 4's first three manual walks each found a defect the complete automated half
  had ratified. So the guide schedules each manual case at the task that makes it walkable, not at
  the end.
- **A decision made during a walk gets a `D5-n` row the day it is made.** Stage 4 recorded D4-18 …
  D4-22 after the fact, and D4-19 described behaviour that had shipped undecided for three days.

---

## 2. Inherited ledger — what Stage 4 hands over

Recorded so the tasks below are concrete. None of these is re-decided here. Each was **re-measured on
2026-09-29** unless it says otherwise.

| Fact | State at the end of Stage 4 | Consequence for Stage 5 |
|---|---|---|
| The suite | **1147/1147** on `x64-release` and `x64-debug` on 2026-09-27, three named skips; `gates` **127/127** (the Stage 4 guide's §9 item 4 — a figure from a named run, not re-run here). The last CI run, **36325491277**, on `1128e7a` (the sign-off commit), passed; the nightly soak passed on 2026-09-28 and 2026-09-29 | Stage 5 adds to a green suite |
| `mutation-lint` | *"clean -- 1 command(s): RotatePageCommand"* (re-run 2026-09-29) | **D5-1**: it says one on the last day of Stage 5 too |
| `layer-lint` | *"clean -- 425 files, 3433 includes"*, **14** live escapes | **D5-13** adds **one** waiver, named and planned — the census moves 14 → 15 in the change that adds it, and not otherwise |
| `ActionRegistry` | **373 declared, 94 registered, 279 unregistered** (`--list-actions`, 2026-09-29). Every id Stage 5's bindings name is **already declared with its §8 chord**: the ten `find.*`, `edit.copy`, `edit.selectAll`, `view.reflow`, `tool.snapshot`, `compare.documents`, `compare.showReport`, `nav.nextDifference`, `nav.prevDifference`. `tool.select` is registered (Stage 4 T7) and selects nothing | Stage 5 registers handlers, and mints only what a surface needs and §8 does not name (**T13**'s view modes, if any); the census is re-measured at T14, not predicted |
| `tests/ui/tst_viewercommands.cpp` | Viewer census **30 registered, 9 not** — `Ctrl+4`, `S`, `Ctrl+B`, `Ctrl+E`, `Ctrl+Alt+C`, `Ctrl+.`, `Ctrl+,`, `Ctrl+]`, `Ctrl+[` | Stage 5 registers five of the nine; **`Ctrl+B`** (Stage 7), **`Ctrl+E`** (Stage 6) and **`Ctrl+.`/`Ctrl+,`** (Stage 6) stay unregistered and leave the key alone |
| `core/backend.h` | `ITextExtractor::extractPage(DocumentHandle, int, text::PageTextModel&)` declared since Stage 2 T1 (D2-7), `text::PageTextModel` forward-declared *"-- Stage 5"*; the backend census **3 of 7**, *missing: ITextExtractor, ISigner, IOcrEngine, IIndexer* | T2 implements it: **4 of 7**. `IIndexer` stays null — it is Stage 16's catalog index (risk R2), not the per-document index |
| `platform/platform.h` | `IRichClipboard` declared (vector metafile, HTML); census **6 of 18** | **D5-10**: stays **6 of 18** — Q3 answered B |
| The engine | **One render thread** with a queue and a process-wide engine lock (`pdf/render/PdfiumInit.h`); lanes `Visible`, `Prefetch`, `Speculative`; **document open runs on the UI thread** (`MainWindow`'s three `backend.open` calls) | **D5-3**: text jobs go through the same thread. A folder search opens files there too, never on the UI thread |
| PDFium's text API (pin 153.0.8009.0) | `FPDFText_LoadPage`, `_CountChars`, `_GetUnicode`, `_GetLooseCharBox`, `_GetCharOrigin`, `_GetCharAngle`, `_GetFontInfo`, `_GetFontSize`, `_GetFontWeight`, `_IsGenerated`, `_IsHyphen`, `_HasUnicodeMapError`, `_GetTextObject`; `FPDFDoc_GetAttachment`, `FPDFAttachment_GetName`/`_GetFile`; `FPDFAnnot_GetStringValue`, `FPDFAnnot_GetFormFieldValue`; `FPDFPageObj_GetMark` — each present in `third_party/pdfium/include/` | Everything D5-2 and D5-12 need is in the pinned C API; **no new engine** |
| `ui/viewer/tools/SelectTool.h` | Arms, shows an I-beam, and does nothing with a drag — *"Stage 5 gives this tool its subject"* | T5 |
| `ui/viewer/LoupeRaster` | A third scheduler client since D4-20: one region, re-rasterized at a scale, cancelled when it moves, request built by `TilePresenter`'s own builder | **D5-11**: the snapshot is the same shape, and reuses that builder |
| `corpus/` | **70 files**, all regenerating byte-identically (re-run 2026-09-29). Text fixtures exist, but **no corpus file has a `/ToUnicode` map**, none is multi-column, none carries right-to-left or vertical *text* (`rtl-binding.pdf` is binding only), none carries a ligature, a decomposed diacritic, an embedded file, or a before/after pair, and `pages-900.pdf`'s text is one line a page | T1 |
| `core/settings/SettingsKeys.def` | **53** rows; the `Search` group exists in `SettingsGroup` with **no keys**; **8 of 26** Preferences pages ship | T6 and T13 — **D5-20** |
| `vcpkg.json` | The `shaping` feature (HarfBuzz + ICU) is labelled *"(Stage 5)"* and is **off** | **D5-5**: it stays off. Stage 5 needs QtCore's Unicode support and nothing else |
| `i18n-lint` | `keyFamilies`: `action shell ribbon prefs error command panes` | Stage 5 adds **`search`** and **`compare`** — one family per new namespace (Stage 3's **I-16**) |
| Open issues carried | Stage 4's **I-07** (S4, `DW-1`); Stage 2's I-06, I-11, I-34 (all open by decision or S3) | None is Stage 5's. Stage 5 starts with no open S1 or S2 |

---

## 3. Stage-5-local decisions

Choices this plan makes because nothing upstream decides them. Each is scoped to Stage 5 and listed
here so a later stage can find and revisit it rather than rediscover it. **Rows marked ✅ carry a §0
questions** and read as proposals until you answer.

| # | Decision | Why | Revisit |
|---|---|---|---|
| **D5-1** | **Stage 5 constructs no `Command`, adds no `core/cmd/Commands.def` row, and touches no `MutationSink`.** A selection, a highlight, a search result, a snapshot and a comparison are view or session state; copy and snapshot write **the clipboard**, never the document. The proof is Stage 4's: `mutation-lint` says *"1 command(s): RotatePageCommand"* on the last day of the stage, and `Commands.def` and `core/cmd/commands/` are byte-unchanged across it. | `Implementation-Plan.md` § Stage 5 says *"No document mutation"*, and D4-1 showed that a check with a number that must not move is the only form of that sentence that survives fourteen tasks. Replace — the half of find-and-replace that would mutate — is Stage 13's, and its absence is what makes this stage's find bar *the find half* of `FindReplaceBar`. | Stage 13 (replace) |
| **D5-2** | **Text is read through PDFium and nothing else — `pdf/render/PdfiumTextExtractor` fills `core/text/PageTextModel` with one entry per Unicode scalar:** its loose box and origin in **page space** (PDF points, unrotated user space — a view rotation or a `/Rotate` is applied at presentation, like every other page-space fact since D2-12), its angle, its font identity (name, flags, size, weight), and three flags the engine already knows: **generated** (a space or line break PDFium inferred rather than one the content showed), **hyphen** (a soft line-end hyphen) and **unmapped** (`FPDFText_HasUnicodeMapError` — the glyph has no trustworthy Unicode). **PDFium's own find (`FPDFText_Find*`) and `FPDFText_GetText` are not used.** | *"We read through PDFium and write through our own code"*: extraction is a read. The model keeps geometry, not a string, because every consumer — hit-testing, highlights, the analyzer, the comparison's appearance pass — needs the boxes. PDFium's find matches one page at a time, with no diacritic folding, no regex, no content classes and no index, on the engine thread. Using it would put search behind the engine lock that rendering depends on. The **unmapped** flag is what lets the product say *this page's text cannot be extracted* rather than returning plausible garbage. The corpus has no `/ToUnicode` map at all today, so the flag has never been exercised (§2). | Stage 16, when OCR adds a second text source per page |
| **D5-3** | **One engine thread, now with two kinds of job.** Text extraction runs on the render scheduler's worker, under the same engine lock, as a job of its own kind alongside the raster requests. It uses a **fourth lane, `Background`, below `Speculative`**, for work nobody is watching: building a search index, a folder search, a comparison's extraction and rasters. **A page a person is SELECTING on is extracted at `Visible`**, because someone is waiting for it. **The UI thread never extracts.** **Search matching and diffing are pure CPU with no engine**, so they run on a `core/` worker of their own and never on the engine thread or the UI thread. Every job is cancellable, and a newer keystroke cancels the older search. | I-05's arrangement is **structural** — one consumer means PDFium's single-threadedness cannot be broken by a later caller forgetting a rule. A second thread that took the engine lock page by page would hold it for a whole page's text parse while a visible tile waited, and a mutex is not a priority queue. Strict priority is what kept Stage 4's 900-page pane from starving the page (D4-10), and a text lane below it gets the same guarantee for free. **What this does NOT give:** preemption. A job already inside PDFium runs to the end, as a speculative raster does, so a page's extraction time becomes the worst delay a visible tile can see. **That is the number T3 measures** (D5-22). | T3's verdict. A no-go moves extraction to a second process (§3.3's scale), which is a new decision |
| **D5-4** | **Extracted text and the search index live in `core/text/DocumentIndex`, keyed by `DocumentId` — NOT in `RenderCache`, and not by `DocumentHandle`.** It has its own byte budget, `search/indexCacheMB`, evicting whole documents least-recently-searched first. It is **memory only** and is dropped when the last window over the document closes. A mutation's `Scope` invalidates the pages it names. **Two windows over one file (`window.new`) share one index.** The comparison's rasters are consumed and released and **never inserted into the tile cache**. | **This is O7's Stage 5 revisit, answered by construction and then measured (T3).** O7 kept ONE shared tile cache because *"a shared cache is free until the cap is too small"* — and the cap is the viewport's. Text is kept for minutes and tiles for a scroll. Putting 900 pages of text models into the tile budget would turn every first search into an eviction storm for the page a person is reading. Stage 4 T14 measured what keying by handle costs: `window.new` gives one file two `DocumentHandle`s, which showed up as **1,369 cross-document evictions**. Keying text by `DocumentId` means a second window does not extract twice. **Memory only** follows O7's own *"the disk tier does not ship"*: `DiskTileStore` does not ship, and the FileMap row's *"shares the cache directory policy"* describes a policy that does not exist (corrected in §10). **What it does not change:** the tile cache's topology, which T3 re-measures under the new workload and does not reopen unless the graded figure leaves the budget. | T3; and Stage 16's catalog index, which is the persistent one (R2) |
| **D5-5** | **ONE normalization pipeline, `core/text/TextNormalizer`, shared by find, search, whole-word matching and comparison, built on QtCore's Unicode support alone.** In order: **NFKC** (ligatures such as `ﬁ` become `fi`, Arabic presentation forms become base letters, full-width forms become ASCII). Then **case folding** (`QString::toCaseFolded`) when matching ignores case. Then **diacritic stripping** (NFD, drop nonspacing marks) when `search/ignoreDiacritics`. Every step keeps an **offset map** back to the model's character indices, so a hit highlights the glyphs it matched. Across lines, a **line-final soft hyphen** (PDFium's hyphen flag) followed by a lowercase letter is **joined**. Every other line break is a space — *except (clarified at T4, 2026-09-30)* a line-final hyphen before anything but a lowercase letter, which STAYS with no space added: `North-` / `West` is the compound `North-West` the generator's label names, and a space inside it is text that is not there. **No ICU, and the `shaping` vcpkg feature stays off.** | Three consumers with three normalizers would disagree in exactly the cases a person tests first: *café* found by find but reported as a change by compare. `QString::normalized`, `QChar`'s categories, `QTextBoundaryFinder` (word boundaries) and `QRegularExpression` (PCRE2) are all **QtCore** and all exist in the distro Qt 6.4 CI builds `core/` against (**convention 24** — the check is a guide case, not an assumption). ICU would add a dependency to the portable presets for collation this stage does not need. HarfBuzz is shaping, which is Stage 13's. **The known cost:** without ICU's dictionary break iterator, whole-word matching in CJK treats each ideograph as a word. The guide records that as a caveat rather than a defect. The hyphen rule's **control** is a real compound (`well-known`) broken mid-line, which must not be joined. | Stage 13, if Write mode's spelling and line breaking bring ICU in anyway — then this pipeline may adopt it |
| **D5-6** | **Reading order comes from `core/text/LayoutAnalyzer`, from GEOMETRY ONLY at Stage 5.** It clusters baselines into lines, lines into blocks by gap analysis, detects columns from whitespace and tables from ruling lines plus alignment, and orders blocks into a reading sequence. *(T4: a model of characters carries no ruling lines, so `PageTextModel` gained the page's THIN paths — `rules()` — filled by the extractor; the analyzer's steps and thresholds are argued in `core/text/LayoutAnalyzer.h`.)* **`const TagTree*` is passed null by every product caller**, because `core/doc/TagTree` is Stage 7's, and a case says so at its assertion (convention 19). **Right-to-left order is MEASURED at T2, not assumed**: T1 writes RTL text in the visual order producers actually emit, and T2 records what order PDFium's text page returns. **If PDFium returns logical order, the model keeps it. If it does not, the analyzer reorders strong-RTL runs itself** (UAX #9's reordering rules L1–L2 only, no explicit embeddings), and the guide states that bound. | The analyzer feeds selection, copy, find across lines, Reflow view and comparison, so it is written once and measured once. PDFium's text page is known to do some bidi handling internally, and *"known to"* is the phrase Stage 2's I-05 taught this project to replace with a measurement. Geometry only is honest: most documents are untagged, a tagged document's structure tree is Stage 7's model, and building a second tag reader here would be D4-3's *second reader to keep in agreement*. | Stage 7 (`TagTree` becomes a real argument) and Stage 13 (Write mode, the analyzer's second consumer) |
| **D5-7** | **The analyzer's accuracy is MEASURED against a labelled generated subset, with targets fixed in the guide (§3) BEFORE T4 runs**, and the result is recorded as **evidence for R1** (`Tech-Stack-Plan.md` §10) — a baseline Stage 13 inherits, not a verdict on A1. A missed target is an **S2**: the analyzer is improved inside the stage, or the target is changed **by you, with the measurement beside it**, never quietly lowered. **The claim is bounded the way `golden/README.md` bounds the golden suite:** *"on our own generated corpus"* — never *"on real documents"*, which **no Stage 5 case measures** (D5-23). | `Implementation-Plan.md` § Stage 5 already says *"its accuracy is measured, not asserted — so Stage 13 inherits a known quantity rather than a hope"*. A target written after the measurement grades the analyzer against whatever it achieved, and Stage 4's §3 fixed the A2 scale before T2 for that reason. The labels come from the **generator**, which placed every glyph and knows every line, block, column, table and the order it wrote them in — a better oracle than any hand annotation, and one that regenerates byte-identically. | Stage 13, whose spike (R1's mitigation) starts from this number |
| **D5-8** | ✅ *(approved by the user 2026-09-29)* **A selection is a range over the DOCUMENT's reading order** — two positions of the form (page, logical index), so it crosses page boundaries in the continuous modes — **or a column selection**: `Alt`+drag with the Selection tool selects the spans of every line the rectangle crosses, which is how a table's column is copied. Double-click selects a word, triple-click a line, quadruple-click a block. `Shift`+click extends the selection. **One selection per document window**, shown in every pane of a split. It is **not session state** and does not survive a close. **It is painted at presentation, over the tiles, and never enters a raster or a cache key** — D4-11's rule, applied to a fourth overlay. `Esc` clears a selection **where the selection is**. `edit.cancel` stays unregistered (Stage 4's **I-06**). *Clarified at T5, 2026-10-01, where two decided things met:* **`Shift` is also Stage 4's temporary-hand modifier** (`ui/viewer/ToolController.h`), so the gesture decides — a `Shift`+CLICK (a press and a release that did not move) extends the selection, a `Shift`+DRAG pans as it did; `Alt` with a double, triple or quadruple click is the unit, not a column; a column drag stays on the page it started on wherever the pointer goes; and a drag into the gap between two pages is held at the nearer page's edge. | A per-page selection could not copy a paragraph that runs over a page break, which is the second thing anyone tries. A column selection is the only way to copy a column out of a multi-column page or a table, and `Alt`+drag is the convention readers already know. **It needs a §8 row** because §8 lists gesture modifiers (`Shift`+drag in the Annotation tools table), so this is **Q2**. `Alt`+drag does not collide with D4-22: a lone `Alt` shows key tips only when nothing else happened while it was held, and a mouse button is something else. Painting at presentation keeps a selection instant and keeps the cache from depending on it. | Stage 13 (a Write-mode caret has its own selection) |
| **D5-9** | ✅ *(approved by the user 2026-09-29)* **One `Ctrl+C` puts every flavour on the clipboard at once, and the paste target chooses.** Plain text keeps the line breaks a person sees and separates blocks with a blank line. HTML keeps paragraphs (lines joined within a block), family, size, bold and italic. A **column** selection is copied as a **table** (tab-separated plain text and an HTML `<table>`). **No `copyAsTable` or `copyFormatted` id is minted.** **`Ctrl+A` selects the whole DOCUMENT**, lazily. A copy of a range whose pages are not yet extracted extracts them at `Background` first, with progress in the status bar, and `Esc` cancels it. `edit.copy` and `edit.selectAll` are registered at §8's **Common** layer, so they consume their key wherever it falls through to Common. **The control is Stage 4's I-06 turned round**: every widget that natively owns `Ctrl+C`/`Ctrl+A` (the find bar's field, the tool finder, Preferences' editors, the log pane) must keep it, through the binder's `ShortcutOverride` handshake. | Word takes HTML, Notepad takes plain text and Excel takes a table from HTML. Offering all three in one `QMimeData` is how a paste into each does the right thing with no menu to choose from. Three copy commands would be three ids, three ribbon buttons and three chords that §8 does not have. **The whole document for `Ctrl+A`** is Acrobat's meaning and §8's *"whatever 'all' means in the focused context"*. It costs a first search's extraction and no more, because it is the same extraction (**Q6**). | Stage 13 (Write mode's `Ctrl+A` is the reflow document's) |
| **D5-10** | ✅ *(Q3, answered 2026-09-29 with option B)* **Formatted copy and snapshots go through Qt's clipboard, and `platform::IRichClipboard` is NOT implemented at Stage 5.** `QMimeData` with `text/html` is published by Qt's Windows backend as **CF_HTML** itself. `qwindows.dll` carries the `HTML Format` registration (measured 2026-09-29, a string in the binary; **T5 confirms it by reading the clipboard back through Win32** — a `ui.` case on the real `windows` platform, because the offscreen platform's clipboard never reaches Windows and the `platform.` suite runs no `QGuiApplication`). A snapshot is a bitmap, and `QClipboard::setImage` publishes one — **at the resolution `pageDisplay/snapshotDpi` asks for** (D5-11), which is what makes a pasted snapshot print cleanly without a vector flavour. What remains is the **vector metafile**: to produce one, PDFium would render into an EMF device context through `FPDF_RenderPage(HDC, …)` — exported by the pinned DLL (`third_party/pdfium/pdfium.def`) — a Windows-only engine call that `pdf/render/` cannot make without becoming OS code, and whose output for transparency and soft masks is unmeasured. So **`platform/win/WinRichClipboard` is not built here and its row is re-tagged ○ (S5) → ○ (S12 paste path)** — `Implementation-Plan.md` § Stage 12 already lists it for *create from clipboard*, which READS the clipboard — and **the vector snapshot, which no stage lists, is registered as `DW-2`** (`Deferred-Work.md`). The platform census stays **6 of 18**. | `Tech-Stack-Plan.md` §4.3's rule: *what Qt does cross-platform gets no interface*. Half of `IRichClipboard` is what Qt already does, and implementing the other half requires breaking the layering. A partly implemented interface would also contradict convention 6, which recognizes only *implemented* or *null*. **The user chose B over building the vector path now (C) or a bitmap at view scale alone (A)**: a resolution setting removes most of the practical cost — a blurry print — for one key. | `DW-2`: when an engine or a first-party path can emit vector page content without an HDC |
| **D5-11** | **The Snapshot tool (`S`) copies the dragged region AS THE DOCUMENT DRAWS IT.** The region is re-rasterized through the shipped scheduler at the view's current device scale, at `Visible`, with a request built by `TilePresenter`'s own builder, as the sharp loupe does (D4-20). It includes annotations and form appearances as displayed and the view's rotation, and **excludes every view transform and overlay**: colour inversion, the grid, guides, the selection and search highlights. It lands on the clipboard as an image, and the status bar says so. `Esc` abandons the drag. **Its resolution is `pageDisplay/snapshotDpi`** (Q3, answered B on 2026-09-29): **0, the default, means as displayed** — the view's device scale, which is what a person sees — and otherwise a resolution in dots per inch, **72 to 600**, a value from 1 to 71 being taken as 72. **The image is bounded at 50 megapixels** (200 MB of pixels): a region that would exceed it at the asked resolution is taken at the largest resolution that fits, and the status bar says the resolution was reduced. | A snapshot is a copy of the **document's** appearance. The inversion and the grid are **how a reader chose to look at it** (D4-11), and a pasted image with the reader's grid across it is a defect somebody would file. Rasterizing rather than grabbing the widget means a snapshot at 50 % is not a blurry 50 % crop, and reusing the loupe's request builder keeps one place where a request is shaped. **One sentinel rather than two keys**: a boolean *as displayed* beside a resolution would be two facts about one choice, which D4-5 refuses; `fullScreen/monitor`'s empty value is the precedent. **The pixel bound** is there because 600 dpi over a whole A3 page is 280 megapixels, over a gigabyte, and a snapshot that exhausts memory is worse than one that says it was reduced. | `DW-2`, if a vector flavour is ever built |
| **D5-12** | **The five content classes of §1.8 are searched from what already exists, and Stage 5 builds none of Stage 6's or Stage 9's models.** **Page text** comes from `DocumentIndex`, and **bookmarks** from Stage 4's `core/doc/Outline` (titles). **Comments, form fields and attachments** come through **one read-only facade reader**, `IDocumentBackend::searchUnits(handle)`, run on the engine thread the first time a search includes one of those classes. It returns plain *search units*: class, text, page (or none), a locator and a rectangle. It reads each annotation's `/Contents` and `/Subj` and each field's name and value, and for an attachment its **name and description, plus the page text of an attachment that is itself a PDF, one level deep** (opened from bytes, never written to disk). Other attachment types match by name and description only. **Navigation:** a comment or field hit goes to its page and rectangle, and a bookmark hit to its destination. **An attachment hit is listed and selected but opens nothing**, because opening an attachment is Stage 6's Attachments pane. | `AnnotationStore` (S6), `FormModel` (S9) and `Attachments` (S6) are editing models with commands. Building their read halves here would be D4-2's *pane before its model* in a new form, and D4-3's *second reader to keep in agreement* once those stages arrive. A search needs strings with places, not models. **When those models exist they become the source** and the reader is theirs to fold, which their stages decide. Opening an attached PDF from bytes is `openBytes`, which Stage 4's metadata case already uses. One level, because an attachment inside an attachment is rare, unbounded and easy to add. | Stages 6 and 9, when the models arrive |
| **D5-13** | **Advanced Search (`ui/panes/SearchPane`) searches a FOLDER, optionally recursing, entirely on the engine thread at `Background`** — each file opened, extracted, searched and closed in turn, **and a file's OPEN is split into pieces no longer than one page's extraction** *(amended 2026-09-30 at T3, on the user's answer to A2 at Stage 5: `PdfiumDocument::open` loads every page, ~145 ms of engine for a 900-page file, and as one job it would hold a visible tile for all of it — `Tech-Stack-Plan.md` §3.3)*, **never shown in a tab**, cancellable between files, results streamed into the pane with file, page and a snippet. **An encrypted file is skipped with a reason**: a folder search never prompts for a password. **Relevance** is defined, not implied, as the file's hit count, with ties broken by path. Results sort by relevance, page or file, and **filter by Stage 4's Info metadata** (the date from `/ModDate`, else `/CreationDate`; author; subject). *Refine* runs a second query over the first query's hit pages. *Export* writes the list as **UTF-8 CSV with a BOM** (so Excel reads it) through **one `QSaveFile`, under ONE new filesystem-write waiver** (`ui/panes/SearchPane.cpp`), which moves the census from 14 to 15 in the same change. `search/maxResults` bounds the list, and hitting it is said, not silent. Activating a hit opens the file through `MainWindow::openFiles`, goes to the page and highlights the match. | `Features.md` §1.8's *Advanced Search* rows, minus the two that are Stage 16's (the catalog index and portfolios, R2). Opening files on the UI thread, where document open runs today (§2), would freeze the window for the length of a folder. The engine thread already exists and already serializes PDFium. **A defined relevance** is the difference between a sort a test can check and a sort nobody can argue with. **The waiver is planned, not discovered**: the export is a user-chosen file outside every allowed directory. `IDurableFiles` replaces files and does not write them, and a new interface for writing a CSV would be a Windows dependency for something Qt does. The census exists to make exactly this visible. | Stage 16 (the catalog index and persistent folder indexes, R2) |
| **D5-14** | **The find bar is the FIND HALF of `ui/write/FindReplaceBar`, at the path the FileMap gives it**, although `ui/write/` otherwise waits for Stage 13. It is incremental: it searches as the user types, debounced, and a newer keystroke cancels the older search. It highlights every match or only the current one (a toggle), wraps with a message at either end, and scopes to **the whole document, the current selection or a page range**. `Esc` closes it and clears every highlight. The bar declares `KeyContext::FindSearch`. **Its toggles ARE the settings** — `Alt+R`, `Alt+W` and `Alt+C` write `search/useRegex`, `search/wholeWords` and `search/caseSensitive` — so the bar and Preferences cannot disagree. **An invalid regular expression is reported in the bar and searches nothing.** | One file for the bar is the FileMap's design: Write mode's find is the same bar with replace switched on (`Architecture-FileMap.md` §3.15). Moving it now would put a Stage 13 rename on the critical path. **The toggles are the settings** for Stage 4's D4-5 reason: *a second key for the same fact is two truths*. An invalid pattern that silently matched nothing would read as *not found*, which is a wrong answer and not an error. | Stage 13 (replace) |
| **D5-15** | ✅ *(approved by the user 2026-09-29)* **`Ctrl+Shift+F` opens Advanced Search from anywhere.** §8's **Find / Search** table lists it in that context only, and `core/cmd/KeyBindingDefaults.def` follows the table, so from the page, where a person is when they want it, the chord does nothing. §8's own *Global defaults recap* names `Ctrl+Shift+F` a global default, and Acrobat binds it globally. **The correction is a Global row in §8 — and the Find / Search row is REMOVED, not kept.** The draft proposed keeping both; carrying it out showed why not: `KeyBindingStore::census()` counts any context binding of a Global chord as *class 1*, a chord the context takes back from Global, **whatever its action**, so a duplicate would have made `tst_keybindings`' *exactly six* red and made `shadows()` report a shadow that is not one. A Global row reaches the Find / Search context anyway, because resolution falls back to Global. Made 2026-09-29 in `Features.md` §8 and `core/cmd/KeyBindingDefaults.def` together, since `tst_keybindings` compares the two row for row. | The table and its recap disagree, and only §8 can say which is right (**Q1**). A stage plan does not re-decide a binding. It says what it found and proposes the fix to the owner, as D4-15 did for `Ctrl+4` and `S`. | — |
| **D5-16** | **Reflow view (`Ctrl+4`) is `ui/viewer/ReflowView`**, a new FileMap row. It is a read-only text surface built from `LayoutAnalyzer`'s blocks in reading order, re-wrapped to the window's width. **Text only**: no images, no vector art, no annotations. Fonts are approximated by size, weight and italic from the model, not by face. It works **for every document**, tagged or not, because at Stage 5 there is no tag tree to prefer (D5-6). `Ctrl+4` toggles it per document window and keeps the reader's place: the block at the top of the page view is the block at the top of the reflow, and back. Zoom chords change the text size. **Find does not run inside it.** Activating a search hit leaves Reflow view for the page. | `Features.md` §1.2 says *"re-wrap the text of a tagged document"*, and D4-15 moved it here because it needs this stage's analyzer. An untagged document reflowed by geometry is what every reader that offers the mode does, and refusing untagged documents would refuse most of them. A read-only rich-text surface is the smallest thing that re-wraps. It draws with Qt's fonts, not PDFium's, which is the honest meaning of *reflow*: the page's typography is exactly what is given up. Keeping find out of it avoids a second highlight implementation for a view with no page geometry. | Stage 7 (tags improve the order); Stage 13 (the reflow *model* is Write mode's, and this view may become a reader of it) |
| **D5-17** | ✅ *(Q4, answered 2026-09-29 with option B)* **`core/text/DocumentComparer` works in three layers, all pure `core/`, over inputs the engine thread produced.** (1) **Pages are aligned** by text similarity into identical, modified, inserted, deleted and moved. (2) **Each aligned pair's text is diffed** with Myers' O(ND) algorithm in its linear-space form over `TextNormalizer`'s word tokens. A deleted run and an inserted run that are identical and at least a stated number of tokens long are reported as a **move**. (3) At the **appearance** sensitivity, the pair's rasters are compared at **72 dpi** outside the regions the text diff already claimed, and pixel differences are clustered into rectangles above a threshold. **The ignore-lists** drop a **header/footer band** (lines in the top or bottom tenth of the page that recur, digits normalized, on at least half the pages and at least three), **page numbers** (a number-only token in that band, or one equal to the page's label) and **whitespace-only** changes. **Sensitivity** chooses the layers: *text only* is (1)+(2); *text and layout* adds position shifts of matched text beyond a threshold; *appearance* adds (3). **Work is bounded**: a page pair whose edit distance passes a cap is reported as *replaced* rather than diffed. The diff runs on a `core/` worker with progress and cancel. **The summary is the Differences pane**, and **generating a summary PAGE and exporting an annotated PDF report are taken by Stage 8**, in its `Implementation-Plan.md` entry, on the user's answer to Q4. | This is `Implementation-Plan.md`'s algorithm note made specific enough to test. Text-only diffing misses a moved image, and raster-only diffing reports every reflowed line. Aligning pages **first** is what keeps a 900-page comparison from being one 300,000-token Myers run, and the **cap** is what keeps two unrelated documents from taking O(N²) memory. Its control is two completely different files that must finish, and finish quickly. **Both PDF outputs** write a new document with annotation appearances, which needs Stage 6's `AppearanceStreamBuilder` and a stage that writes new files. **Stage 8** — Save-As, export, and after Stage 6 — is where both exist, and its entry now names them, so the owner has heard of them (**Q4**, B; `Deferred-Work.md` was the alternative, A). | Stage 8 (the PDF outputs); Stage 11 reuses the comparer for *"was this post-signature change permitted?"* (FileMap) |
| **D5-18** | **The comparison view is its own surface over TWO documents, not a `SplitViewContainer` mode.** `ui/viewer/ComparisonView` holds two `DocumentHandle`s and two `TilePresenter`s. It shows them **side by side**, scrolled together **by aligned page** rather than by pixel, or as an **overlay**: the newer document drawn over the older, tinted per document and composited at presentation. The Differences pane lists every difference, and `Ctrl+]`/`Ctrl+[` step through them in both views. **It shows no rulers, grid or guides.** | This answers **two Stage 4 revisits**. D4-13 said a split holds views of **one** document over one handle and one journal. A comparison is two documents with two journals, and forcing it into the split would break the invariant the split's close protocol depends on. D4-18 gave one pair of rulers to a window, following the focused pane, and asked what two *different* documents would want. At Stage 5 the answer is none: a comparison is for reading differences, and a second pair of rulers measuring the other document's page is a feature nobody has asked for. **Scrolling by aligned page** is the point of the view: after an inserted page, pixel-synchronized scrolling shows page 5 beside page 6 for the rest of the document. | A later stage that wants to measure inside a comparison |
| **D5-19** | ✅ *(approved by the user 2026-09-29)* **`pdf/cos/ContentStreamParser` moves from Stage 5 to Stage 10**, re-tagged ○ (S5) → ○ (S10). | Its FileMap row calls it *"the substrate for text extraction, redaction, and object-level editing"*, and lists `PdfiumTextExtractor` only as a *cross-check*. At Stage 5 extraction is PDFium's (D5-2), and the marked content a header/footer detector might want is answered per character by `FPDFText_GetTextObject` plus `FPDFPageObj_GetMark`. So the parser's only Stage 5 caller would be a test. That is the shape Stage 1 was caught by five times (a capability real, tested and reachable only from a test), and the generator is a better oracle than a second parser in any case (§1). **Stage 10's `RedactionOps` is its first product caller**: redaction has to rewrite content streams, so it must parse them. Building the parser in the stage that consumes it means building it to that stage's needs (**Q5**). | Stage 10 |
| **D5-20** | ✅ *(approved by the user 2026-09-29)* **Stage 5 declares FIFTEEN settings keys** — corrected 2026-09-29 from the fourteen Q7 approved, by Q3's answer: the ten `search/*` keys and three `documents/compare*` keys `Implementation-Plan.md` lists, plus **`documents/compareIgnorePageNumbers`** (Q7) and **`pageDisplay/snapshotDpi`** (Q3, D5-11). **One Preferences page ships — Search, nine of 26.** The four `documents/compare*` keys join the existing Documents page, and `pageDisplay/snapshotDpi` the existing Page Display page. Catalog families **`search`** and **`compare`** join `i18n-lint`'s `keyFamilies`. | §1.5 lists *page numbers* apart from *headers/footers*, and they are different changes. A renumbered document has every page number changed while its headers are identical, and a person who wants one ignored may want the other reported (**Q7**). The families follow Stage 3's **I-16**: a family is added for a new **namespace**, and `search.` and `compare.` are two. | — |
| **D5-21** | **A fourth fuzz target, `fuzz_text_pipeline`**: bytes → a synthetic `PageTextModel` → `LayoutAnalyzer` → `TextSelection` hit-tests and expansions → `TextNormalizer` → a search. The model's boxes include NaN, infinities, zero and negative sizes, overlaps and 10⁶ characters on one line. The fuzz-count assertion moves **3 → 4** (`cmake/RawPageFuzzTargets.cmake`) and the ten-minute soak runs it. | Every input to `core/text/` is derived from an **untrusted PDF**: a hostile file controls every coordinate PDFium hands back. The analyzer is geometric clustering over those coordinates, which is where degenerate input loops, divides by zero or allocates without bound. `core/` is portable by rule, so the target builds under `portable-fuzz` with no engine. The count assertion exists because *"ten minutes, no crashes"* is passed trivially by a target that builds nothing. | — |
| **D5-22** | **A2 is re-measured for Stage 5's workload at T3, BEFORE the find bar, the search pane or the comparison are designed.** `bench_pipeline` gains a Stage 5 mode. It drives the shipped scheduler and cache with a viewport **while a 900-page index is built at `Background`**, and separately **while a folder search and a comparison run**, and reports the **viewport's** p95 queued latency (the same quantity as D2-1's product figure and Stage 4's), the per-page extraction time's distribution, and the cache's cross-document evictions. The scale is fixed in the guide's §3 before the run. T14 re-reads it through the shipped surfaces. | Stage 4's D4-16 worked: measuring at T2 meant a no-go would reshape the pane before the pane existed. Here a no-go reshapes **where extraction runs**, and learning that after the find bar, the search pane and the comparison all queue on the engine thread would mean redesigning three surfaces. The **per-page extraction time** is the number D5-3 makes load-bearing: it is the longest a visible tile can wait behind a background job, because a job cannot be preempted. | T14; the next engine consumer (Stage 6's appearance generation) |
| **D5-23** | **The manual half walks GENERATED documents, not real ones — decided by the user on 2026-09-29** (*"Generate those corpus as before"*, answering P-2 and P-3). **No Stage 5 case, automated or manual, meets a real document.** So: (1) every claim this stage makes — the manual half's included — carries the bound *"on our own generated corpus"*; (2) the exit criterion's *"comparison … on real revision pairs"* is met on a **generated** revision pair built to read like one (`compare/report-v1.pdf` → `report-v2.pdf`, many mixed edits at once, §5.2), and `Implementation-Plan.md` § Stage 5 says so beside the criterion; (3) the walk fixtures are made **as legible as the engine allows without a third-party byte**: Latin text in the **base-14** faces PDFium carries itself; CJK through a **non-embedded** Adobe CID font with a predefined Unicode CMap, drawn by the OS's own CJK face through Stage 2's font path — **if T1 measures that PDFium renders it that way** — **it does, measured at T1 on 2026-09-29** (`STSong -> SimSun`, `ordinary/cjk-readable.pdf`) — else the synthetic glyphs; Hebrew in synthetic glyphs, because no predefined CMap reaches it without embedding a font, so **M-04 judges the paste, the highlight and the find, not the letterforms on the page**; (4) the Advanced Search folder is **a copy of the whole `corpus\` tree** — nested `ordinary/`, `encrypted/`, `malformed/`, `signed/` and `compare/` — and T1's new fixtures carry **distinct `/Author`, `/Subject` and dates** so the metadata filter has something to filter. | P-2 and P-3 asked for documents the generator did not make, because a generated corpus is regular where real ones are not; the user chose the generated corpus, which keeps D2-11's rule that every corpus byte is ours and makes every walk reproducible from the tree. **The cost is named rather than hidden**: the analyzer's accuracy on real layouts, and the comparison's false-positive rate on real revisions, are **unmeasured at Stage 5**. They are carried to **R1**'s row (`Tech-Stack-Plan.md` §10) with the layout baseline, so Stage 13's spike is where a real document first meets `LayoutAnalyzer`. A machine-dependent rendering (the OS's CJK face) is a golden case only if `golden/README.md`'s machine-dependent rule allows it, else an excluded row with that reason. | Stage 13 (R1's spike), or any stage that brings real documents into verification |

---

## 4. File manifest

Every file Stage 5 creates or turns from ○, with the [`Architecture-FileMap.md`](./Architecture-FileMap.md)
row it must flip **in the same change**. Rows marked ⚠️ have no FileMap row yet and are lockstep debt
(§10), paid with the file.

| File | FileMap row | Task |
|---|---|---|
| `corpus/make-text.py` and the text fixtures (§5.2); `corpus/labels/*.json`; `corpus/make-compare.py`, `corpus/compare/*.pdf` and `corpus/compare/*.expected.json` | **None** — the corpus is data and has no FileMap row; its record is one `corpus/PROVENANCE.md` row per PDF (the routing table's owner for a corpus file), plus `golden/manifest.json` rows | T1 |
| `core/text/PageTextModel.{h,cpp}` | §3.5 ○ (S5) → ✅ | T2 |
| `pdf/render/PdfiumTextExtractor.{h,cpp}` | §3.10 ○ (S5) → ✅ | T2 |
| `core/backend.h` — `ITextExtractor` implemented; `searchUnits()` on `IDocumentBackend` | §3.8's `core/backend.h` row; the implemented census **3 → 4 of 7** | T2, T8 |
| `core/render/RenderScheduler.{h,cpp}` — the text job and the `Background` lane (D5-3) | §3.4's scheduler row | T2 |
| `tests/bench/bench_pipeline.cpp` — the Stage 5 mode | §3.17's `tests/bench/` row (D5-22) | T3 |
| `core/text/LayoutAnalyzer.{h,cpp}` | §3.5 ○ (S5) → ✅ | T4 |
| `core/text/TextNormalizer.{h,cpp}` — D5-5's one pipeline | ⚠️ **no row** — §3.5, added with the file | T4 |
| `core/text/TextSelection.{h,cpp}` | §3.5 ○ (S5) → ✅ | T5 |
| `ui/viewer/SelectionOverlay.{h,cpp}` | §3.14 ○ (S5) → ✅ — selection **and** search highlights (flow G) | T5, T7 |
| `ui/viewer/tools/SelectTool.{h,cpp}` — given its subject | §3.14's `tools/*` row | T5 |
| `core/text/SearchQuery.h`, `SearchEngine.{h,cpp}`, `DocumentIndex.{h,cpp}` | §3.5 ○ (S5) → ✅; `DocumentIndex`'s *"shares the cache directory policy"* corrected (D5-4) | T6 |
| `core/settings/SettingsKeys.def` — the fifteen keys; `PreferencesDialog::shippedPages()` — Search | §3.8's `core/settings/` rows and §3.15's `PreferencesDialog` row | T6, T13 |
| `ui/write/FindReplaceBar.{h,cpp}` — the find half | §3.15 ○ (S5 find, S13 replace) → 🟡 (S5 find ✅, S13 replace) | T7 |
| `pdf/render/PdfiumDocument.{h,cpp}` — the search-units reader (D5-12) | §3.10's row | T8 |
| `ui/panes/SearchPane.{h,cpp}` | §3.15 ○ (S5) → 🟡 (S5 folder ✅, S16 catalog and portfolio) | T9 |
| `core/text/SearchResults.{h,cpp}` — sort, filter, refine, and the CSV bytes (D5-13) | ⚠️ **no row** — §3.5, added with the file | T9 |
| `ui/viewer/tools/SnapshotTool.{h,cpp}` | §3.14's `tools/*` row | T10 |
| `ui/viewer/ReflowView.{h,cpp}` | ⚠️ **no row** — §3.14, added with the file (D5-16) | T11 |
| `core/text/DocumentComparer.{h,cpp}` | §3.5 ○ (S5) → ✅ (its *"ICU"* in *Interacts with* corrected, D5-5) | T12 |
| `ui/viewer/ComparisonView.{h,cpp}`, `ui/panes/DifferencesPane.{h,cpp}`, `ui/dialogs/CompareDialog.{h,cpp}` | §3.14, §3.15 ○ (S5) → ✅; the dialog-family row's *S5* entry | T13 |
| `tests/fuzz/fuzz_text_pipeline.cpp`; `cmake/RawPageFuzzTargets.cmake` 3 → 4 | §3.17's `tests/fuzz/` row (D5-21) | T4 |
| `tests/render/tst_extract.cpp`, `tst_layoutcorpus.cpp`, `tst_comparecorpus.cpp` | §3.17's `tests/render/` row | T2, T4, T12 |
| `tests/unit/tst_textmodel.cpp`, `tst_normalize.cpp`, `tst_layout.cpp`, `tst_selection.cpp`, `tst_search.cpp`, `tst_index.cpp`, `tst_searchresults.cpp`, `tst_compare.cpp` | §3.17's `tests/unit/` row | T2 … T12 |
| `tests/ui/tst_selecttool.cpp`, `tst_findbar.cpp`, `tst_searchpane.cpp`, `tst_snapshot.cpp`, `tst_reflow.cpp`, `tst_comparison.cpp` | §3.17's `tests/ui/` row | T5 … T13 |
| `tests/ui/tst_clipboard.cpp` — CF_HTML read back through Win32, **a real-desktop case** that skips under offscreen `ctest` like Stage 4's A-85 and A-94 | §3.17's `tests/ui/` row | T5 |

**Settings keys** — **fifteen** (D5-20): `search/wholeWords`, `search/caseSensitive`,
`search/ignoreDiacritics`, `search/useRegex`, `search/includeComments`, `search/includeBookmarks`,
`search/includeFields`, `search/includeAttachments`, `search/maxResults`, `search/indexCacheMB`;
`documents/compareSensitivity`, `documents/compareIgnoreHeaderFooter`,
`documents/compareIgnoreWhitespace`, `documents/compareIgnorePageNumbers`; `pageDisplay/snapshotDpi` (D5-11). Each is a row in
`core/settings/SettingsKeys.def` and therefore an automatic Preferences editor (convention 17).

**Files Stage 5 does NOT build**, recorded so their absence is read as decided: `pdf/cos/ContentStreamParser`
(D5-19, Stage 10), `platform/win/WinRichClipboard` (D5-10 — Stage 12's paste path; the vector snapshot is `DW-2`), `core/text/CatalogIndex` (Stage 16,
R2), `ui/panes/DocumentMapPane` (Stage 13).

---

## 5. Component design

Only the pieces whose shape is not obvious from a one-line component entry.

### 5.1 The library graph after Stage 5

Unchanged in kind. `rawpage_core` gains `core/text/` — portable by rule, headless, and built by
`portable-asan` and `portable-fuzz` like the rest of `core/`. `rawpage_pdfium` gains the extractor and
the search-units reader. Everything else is `rawpage_ui`. **No new library and no new `platform/`
interface** (D5-10). **`rawpage_assert_headless()` is unchanged**, and most of Stage 5's provable
behaviour — normalization, layout, selection arithmetic, search, the index, the diff — lives in the
half that builds with no display and no engine.

### 5.2 The corpus this stage is judged on (T1)

Every file is generated by a script we own (D2-11), regenerates byte-identically, and — new at this
stage — **writes its own ground truth**: what text it placed, in which order, with which meaning.

| Fixture | Carries | Its control |
|---|---|---|
| `ordinary/text-tounicode.pdf` | Embedded Latin and CJK subsets **with `/ToUnicode`**, a `ﬁ` ligature glyph mapped to U+FB01 and another mapped to the two letters `fi`, precomposed `é` beside decomposed `e`+U+0301 | `cjk-embedded.pdf` (no `/ToUnicode`): its text must be reported **unmapped**, not extracted as plausible garbage |
| `ordinary/columns-two.pdf`, `columns-three.pdf` | Body text in two and three columns, a spanning headline, a footnote, headers and footers | A one-column page in the same file |
| `ordinary/table-ruled.pdf` | A ruled table and an unruled aligned one | Tabular-looking prose that is **not** a table |
| `ordinary/rtl-text.pdf` | Hebrew **in visual order** as producers emit it, one line of mixed Hebrew and Latin digits | The same words in logical order on another page |
| `ordinary/cjk-vertical.pdf` | Identity-V vertical CJK with `/ToUnicode`, two columns right to left | A horizontal CJK line on the same page |
| `ordinary/cjk-readable.pdf` *(added at T1)* | A **non-embedded** `/STSong-Light` through `/UniGB-UCS2-H` and `-V`: CJK drawn with the machine's own face, horizontal and vertical — D5-23's readable CJK, **measured at T1 to render** (`STSong -> SimSun`). A file of its own, not a page of `text-tounicode.pdf`, because the capture records a substituted face per document | The one Stage 5 golden case marked machine-dependent |
| `ordinary/hyphenated.pdf` | Line-final soft hyphens | `well-known` broken mid-line, which must not be joined |
| `ordinary/search-classes.pdf` | One unique word in **each** content class — page text, a comment's `/Contents`, a bookmark title, a field name and a field value, an attachment's name, a `.txt` attachment's description, and the text of an attached PDF | A control word present in **none** |
| `ordinary/text-900.pdf` | 900 dense pages — each page's text a function of its index, with a page-unique marker line — sharing content streams where the text allows, so the file stays small | The measured workload for T3 and for *"the second search is instant"* |
| `compare/*.pdf` + `*.expected.json` | Before/after pairs, one change kind each: a word inserted, deleted, replaced; a paragraph moved; an image moved; a page inserted, deleted, reordered; a header changed; page numbers shifted; whitespace only | An **identical** pair (zero differences) and a pair of **unrelated** documents (bounded time) |
| `compare/report-v1.pdf` → `report-v2.pdf` + `report.expected.json` | **The walkable revision pair** (D5-23): a multi-page, multi-column report in base-14 faces, revised the way a person revises — several words and sentences edited, a paragraph moved, a figure moved, a table cell changed, a page inserted, the footer's date and every page number changed — all at once, listed in its `expected.json` | Graded by A-95 like every pair; judged by a person in M-08 |
| `labels/*.json` | For the layout fixtures: every line, block, column, table grid and the block reading order, in the generator's own coordinates | `--verify` regenerates them byte-identically; `--check-provenance` requires every label to name a corpus file that exists |

**Every byte stays ours** (D2-11). Extraction needs codes, positions and `/ToUnicode`, not beautiful
outlines, so the automated fixtures may use `corpus/fontenc.py`'s rectangle glyphs. **The fixtures a
person walks are made as legible as the engine allows without a third-party byte** (D5-23): base-14
faces for Latin, a non-embedded Adobe CID font with a Unicode CMap for CJK **if T1 measures that
PDFium draws it with the OS's face**, synthetic glyphs for Hebrew. T1's new fixtures also carry
**distinct `/Author`, `/Subject` and dates**, so Advanced Search's metadata filter has something to
filter over a copy of the corpus tree (M-07).

### 5.3 One selection, as it will actually run

```
drag with the Selection tool ─▶ SelectTool::press/move (ToolHost, device px)
 ├─ page under the pointer, point in PAGE space                   (core/Geometry.h's transform)
 ├─ PageTextModel for that page? ─ no ─▶ scheduler.enqueue(TextJob{doc, page}, Visible)   (D5-3)
 │                                        └─ delivered on the pumping thread; the drag resumes
 └─ TextSelection::hitTest(point) ─▶ (page, logical index)          reading order: LayoutAnalyzer
     └─ extend the range; SelectionOverlay repaints OVER the tiles  (D4-11 -- no raster, no cache key)
Ctrl+C ─▶ edit.copy (Common) ─▶ TextSelection::{asPlainText, asFormatted, asTable}
 └─ one QMimeData: text/plain + text/html  ─▶ QClipboard  (Qt publishes CF_HTML itself -- D5-10)
```

### 5.4 The first search, and what bounds it

```
Ctrl+F, type "lorem" ─▶ FindReplaceBar (KeyContext::FindSearch), debounced
 └─ SearchQuery{pattern, flags from search/*, scope, classes}
     └─ DocumentIndex(DocumentId) has every page?  (D5-4)
          ├─ no ─▶ one TextJob per missing page at Background                   (D5-3)
          │        progress in the bar; hits stream in as pages complete
          └─ yes ─▶ SearchEngine::find on a core/ worker, never the UI thread
                    TextNormalizer: NFKC → fold case → strip marks, with offset maps (D5-5)
     ─▶ hits (page, logical range, rects) ─▶ SelectionOverlay highlights; F3 steps
```

**What must be measured rather than assumed** (T3, D5-22): the per-page extraction time's p95 and
maximum on `text-900.pdf`, which is the longest a visible tile can wait behind a background job, and
the viewport's p95 while all 900 pages are being extracted.

### 5.5 A comparison

```
Ctrl+Alt+C ─▶ CompareDialog: two documents (open tabs or paths), sensitivity, ignore-lists
 └─ engine thread, Background: extract both; rasters at 72 dpi only for "appearance"   (D5-17)
     └─ core/ worker: DocumentComparer
          1. align pages by similarity    ─▶ identical · modified · inserted · deleted · moved
          2. Myers per aligned pair       ─▶ insert · delete · replace · move   (capped: "replaced")
          3. appearance, outside (2)'s rects ─▶ clustered pixel rectangles
          ignore: header/footer band · page numbers · whitespace
     ─▶ DiffSet ─▶ ComparisonView (side by side by ALIGNED page, or overlay) + DifferencesPane
                   Ctrl+] / Ctrl+[ step both views
```

### 5.6 Test targets after Stage 5

| Target | Links | Stage 5 suites | Runs where |
|---|---|---|---|
| `rawpage_tests` | unchanged | `tst_textmodel`, `tst_normalize`, `tst_layout` (synthetic geometry), `tst_selection`, `tst_search`, `tst_index`, `tst_searchresults`, `tst_compare` (synthetic pairs), and the scheduler's text-job cases | Windows + `portable-asan` |
| `rawpage_render_tests` | unchanged | `tst_extract` (the extractor over the corpus against the generator's truth), `tst_layoutcorpus` (**D5-7's measurement**), `tst_comparecorpus` (the pairs against their `expected.json`) | Windows |
| `rawpage_ui_tests` | unchanged | `tst_selecttool`, `tst_findbar`, `tst_searchpane`, `tst_snapshot`, `tst_reflow`, `tst_comparison`, and the census edits in `tst_viewercommands`; **`tst_clipboard`** — the HTML flavour read back as CF_HTML through Win32 (D5-10) — which needs the real `windows` platform and **skips under offscreen `ctest`**, a fourth named skip run by hand as A-85 and A-94 are | Windows, `offscreen`; `tst_clipboard` on `windows` |
| `rawpage_composition_tests` | unchanged | Advanced search through `CompositionRoot` over a scratch folder | Windows |
| `portable-fuzz` | unchanged | `fuzz_text_pipeline` (**D5-21**) | Linux CI |
| `gates.` | no binary | the new corpus files in `gates.corpus.*`; `gates.fuzz_count.*` at four | `gates` preset |

**A case that searches a folder must search a scratch folder it made**, for the reason the recovery
and session roots taught (`CLAUDE.md` → *Traps*): a suite that searched the tester's documents would
depend on them.

---

## 6. Task breakdown

Ordering is risk-first. **T3 comes before any surface that queues on the engine**: a no-go on the
Stage 5 workload changes where extraction runs, and learning that after T7, T9 and T12 each queue on
the engine thread would mean redesigning three surfaces. **T4 comes second of the risks** because
Stage 13 inherits its number. Stages 2, 3 and 4 put their go/no-go early for the same reason.

| # | Task | Depends on | Content | Done when |
|---|---|---|---|---|
| **T1** | **The corpus this stage can be judged on** | — | §5.2's fixtures, labels and pairs; `PROVENANCE.md` rows; `golden/manifest.json` rows | `make-corpus.py --verify`, `--check-provenance` and `--self-test` green; **every existing file byte-unchanged**; each fixture's row names what it carries and its control; `golden-update --check-manifest` green |
| **T2** | **`PageTextModel`, `PdfiumTextExtractor`, and the engine thread's text job** | T1 | D5-2's model; D5-3's job kind, `Background` lane and on-demand `Visible` extraction; **RTL order measured** (D5-6) | `tst_extract` green over the corpus **against the generator's truth**, not against PDFium: the text, the boxes inside the ink, the three flags; `cjk-embedded.pdf` **reported unmapped**; every extraction on the engine thread (thread id asserted); the backend census **4 of 7** |
| **T3** | **A2 at Stage 5, and O7's revisit** | T2 | `bench_pipeline --stage5` (D5-22): the viewport beside a 900-page index build, a folder search and a comparison's extraction; the per-page extraction distribution | The viewport p95 reported beside Stage 4's figures, graded on the guide's §3 scale, **recorded** in `Tech-Stack-Plan.md` §3.3 as *A2 at Stage 5*; O7's row gains the revisit's evidence; **a no-go changes D5-3 before T5** |
| **T4** | **`TextNormalizer` and `LayoutAnalyzer`, measured** | T2 | D5-5's pipeline; D5-6's analyzer; D5-7's measurement against the labels; `fuzz_text_pipeline` (D5-21) | `tst_normalize` and `tst_layout` green headless; **`tst_layoutcorpus` meets every §3 target** or the miss is an S2 in the guide's log; the result recorded as R1's evidence; the fuzz count asserted at 4 |
| **T5** | **Selection and copy** | T4 | D5-8's `TextSelection`, `SelectionOverlay` and `SelectTool`; D5-9's flavours and `Ctrl+A`; D5-10's clipboard read-back | Selection over multi-column, RTL, vertical and cross-page text asserted **against the rendered ink**; the three flavours on the clipboard, **CF_HTML read back through Win32**; `Ctrl+C`/`Ctrl+A` from key events; **the I-06 control** — every text widget keeps its own `Ctrl+C` and `Ctrl+A` |
| **T6** | **`SearchQuery`, `SearchEngine`, `DocumentIndex`; the Search keys and page** | T4 | Page text over D5-5; D5-4's index, budget, `Scope` invalidation and sharing; the ten `search/*` keys; the Search page | `tst_search`: every flag, and each flag's **control**; `tst_index`: built lazily, **the second search inside §3's *instant***, invalidated by a rotation's `Scope`, evicted at the budget, **one index for two windows** |
| **T7** | **The find bar** | T5, T6 | D5-14; highlights through `SelectionOverlay`; the whole Find / Search table | Every chord dispatches **from a key event** in `KeyContext::FindSearch`; incremental, cancelled by the next keystroke; wraps; `Esc` closes and clears; an invalid regex reports and searches nothing; the toggles **are** the settings |
| **T8** | **The other four content classes** | T6 | D5-12's reader and navigation | Over `search-classes.pdf` each word is found **only** with its class's toggle on, and the control word never; each hit navigates as D5-12 says, the attachment hit opening nothing |
| **T9** | **Advanced search** | T6, T7 | D5-13; `SearchResults`; the one waiver | A scratch folder, recursive and not; an encrypted file **skipped with a reason**; snippets asserted; each sort and filter; refine; the CSV's bytes; `layer-lint` **15** escapes with the new one named; the UI thread never opens a file; **a 900-page file's open never holds the engine longer than one page's extraction, and `bench_pipeline --stage5`'s folder-search column is inside §3.1's budget** (D5-13 as amended at T3) |
| **T10** | **Snapshot** | T3 | D5-11, and `pageDisplay/snapshotDpi` | `S` arms from a key event; the clipboard image **matches a render of the region** and carries none of the overlays or the inversion; `pageDisplay/snapshotDpi` sets its resolution, **0 is the view's** and the 50-megapixel bound reduces and says so; `Esc` abandons |
| **T11** | **Reflow view** | T4 | D5-16 | `Ctrl+4` toggles from a key event; the reflow's text is the analyzer's order; the reader's place survives a toggle both ways; a search hit leaves reflow |
| **T12** | **`DocumentComparer`, measured** | T4 | D5-17's three layers, ignore-lists, sensitivities and cap | `tst_comparecorpus`: **every** labelled change in every pair found and classified, **zero** differences on the identical pair, the unrelated pair **inside its bound**; each ignore-list with its control |
| **T13** | **The comparison surface; the compare keys** | T12, T5 | D5-18; `CompareDialog`; `DifferencesPane`; the four `documents/compare*` keys | `Ctrl+Alt+C` from a key event; side-by-side scrolls **by aligned page**; overlay composites both; `Ctrl+]`/`Ctrl+[` step both views; **nothing mutates either document** |
| **T14** | **The re-measurement, the census, CI, lockstep** | all | D5-22's second reading through the shipped surfaces; `--list-actions`; §9; §10 | The T3 figure re-read and recorded as the product's; the census cases and `--list-actions` agree; `Implementation-Plan.md`'s Stage 5 block reflects reality, deviations included |

---

## 7. Blocking preconditions — setup checklist

| # | Precondition | Why | State |
|---|---|---|---|
| **P-1** | **Microsoft Word, or another editor that pastes HTML** — owner: **you** | *"Copy into Word and confirm formatting"* (`Implementation-Plan.md` § Stage 5, manual) is guide case **M-02**. The CF_HTML flavour is checked automatically (T5), but whether a paste *looks* right is a judgement | ✅ **Microsoft Word, Office LTSC Professional Plus 2024** (`ProPlus2024Volume`, x64, Click-to-Run build **16.0.20430.20092**, read from `WINWORD.EXE` and the ClickToRun configuration on 2026-09-29); **Excel** from the same install is present for M-02's table paste |
| **P-2** | **Documents to walk**: a multi-column article, a **Hebrew or Arabic** document, a **CJK** document (vertical too), and **two revisions of one document** | M-01, M-02, M-04, M-05, M-08, M-09 | ✅ **Answered 2026-09-29: GENERATED, by T1** — *"Generate those corpus as before"* (**D5-23**): `columns-two.pdf`, `columns-three.pdf`, `table-ruled.pdf`, `rtl-text.pdf`, `text-tounicode.pdf`, `cjk-vertical.pdf` and the `report-v1`/`report-v2` pair, each walk copying its file into `tests\manual\`. **What this does not reach:** a real document, which no Stage 5 case now meets |
| **P-3** | **A folder of PDFs, some in subfolders, one of them password-protected** | Advanced search over a folder (M-07) | ✅ **Answered 2026-09-29: GENERATED** (**D5-23**) — a copy of the whole `corpus\` tree into `tests\manual\M-07\`: nested folders, **five** encrypted files, the malformed set, and T1's fixtures with distinct metadata |
| **P-4** | **Python 3** on every job that runs the corpus checks | T1's new generators. Already a verification dependency | ✅ Python 3.13.9 on this machine; `gates.corpus.*` already require it |
| **P-5** | **An image editor that accepts a pasted bitmap** — Paint is enough | M-03, the snapshot | ✅ **Paint is present** (`mspaint.exe`, read 2026-09-29) |

---

## 8. Verdict ledger — what Stage 5 owes

| Verdict | Produced by | Recorded in |
|---|---|---|
| **A2 at Stage 5** — does the shipped software path hold the frame budget once text extraction shares the one engine thread with rendering, for a first search of 900 pages, a folder search and a comparison? | **T3** (the bench, before the surfaces) and **T14** (re-read through them) — D5-22 | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), beside A2's figures, **labelled as Stage 5's workload** |
| **The layout baseline** — how well geometry alone recovers lines, blocks, columns, tables and reading order, on our generated corpus | **T4** — D5-7 | `Tech-Stack-Plan.md` §10, **R1**'s row, as evidence and not as a verdict on A1; the figures themselves in the guide's A-cases |
| **O7's revisit** — does the next cache consumer change the answer? | **T3** — D5-4 | [`Implementation-Plan.md` §8](./Implementation-Plan.md#8-open-decisions-carried-forward), **O7**'s row, which keeps its resolution and gains the evidence, or is reopened with it |

**A stage that is green but has no verdict is not signed off.**

---

## 9. CI changes

| Job | Before | After |
|---|---|---|
| `gates` (ubuntu, no Qt) | 127 cases, four gates | The new corpus files and labels in `gates.corpus.*`; `gates.fuzz_count.*` at four. **`mutation-lint` must still say one command** (D5-1) |
| `windows` (self-hosted) | 1147 cases | Plus every Stage 5 suite. The `ui.` suites stay offscreen, so `tst_clipboard` **skips** there as A-85 and A-94 do, and its result is a by-hand run on the `windows` platform |
| `portable-asan` (ubuntu) | runs `core/`, `pdf/cos/`, `pdf/ops/` | Plus every `core/text/` suite. **`ui/` is still outside ASan's reach** (O10), and the guide says so |
| `portable-fuzz` (ubuntu, clang) | three targets, 60 s each | **Four** — `fuzz_text_pipeline` (D5-21) |
| `fuzz-soak.yml` | three targets, ten minutes | Four |
| `coverage-report` | names what did not run | Gains the text surfaces, which are Windows-only |

---

## 10. Lockstep debt this stage creates

**Paid in the same change as this draft** — facts this draft establishes whatever the §0 answers are:

- **`Implementation-Plan.md`**: § Stage 5's status line (a plan and a guide exist, no code); §5's
  guide row and its prose (*"Stages 0 to 5 have theirs"*), and the list of stage plans.
- **`CLAUDE.md`**: *Where the work is right now* — the next action is your §0 answers, then T1 —
  and the document-set counts (six stage plans, six testing guides). **Its Stage 4 row's CI bound is
  corrected in place**: it said the five commits after sign-off were not pushed, and CI run
  **36325491277** on `1128e7a`, the sign-off commit, passed with them (re-read with `gh run list` on
  2026-09-29).
- **`claude-plan/README.md`**: ⑤'s and ⑥'s lists, and Stage 5's row in *Current state*.
- **D5-23, decided by the user 2026-09-29**: `Implementation-Plan.md` § Stage 5's exit criterion and
  manual list say, beside *"real revision pairs"* and *"two real revisions"*, that they are met on
  generated documents. The criterion is annotated rather than rewritten, so what was asked for and
  what will be shown stay side by side.

**Owed on your §0 answers** — each made only once its answer is given, because each carries out a decision
that is yours:

- ✅ **Q1, paid 2026-09-29** → `Features.md` §8: `Ctrl+Shift+F` MOVED from the Find / Search table to Global, with a note left under the Find / Search table; `core/cmd/KeyBindingDefaults.def` moved in the same change; §8's census paragraph re-derived.
- ✅ **Q2, paid 2026-09-29** → `Features.md` §8's Viewer table gains `Alt`+drag. **No binding-table row**: `tst_keybindings` classes a cell naming *drag* as descriptive, as it does the Annotation tools' `Shift`+drag, and `SelectTool` reads the modifier at T5. The multi-table census moved **52 → 53**, re-derived by parsing the tables.
- ✅ **Q3 (B), paid 2026-09-29** → `Architecture-FileMap.md`'s `WinRichClipboard` row re-tagged ○ (S12 paste path),
  and `TextSelection`'s row no longer names `IRichClipboard`; `Implementation-Plan.md` § Stage 5 loses it from
  *Components* and gains `pageDisplay/snapshotDpi`; its Stage 4 section's D4-15 sentence corrected in place;
  `Deferred-Work.md` gains **`DW-2`**, the vector snapshot. **Owed with T10's code:** `platform/platform.h`'s
  comment on `IRichClipboard` still says *(Stage 5)*, and is corrected in the change that builds the snapshot.
- ✅ **Q4 (B), paid 2026-09-29** → `Implementation-Plan.md` § Stage 8 gains both PDF outputs in its objectives and
  components; §7's §1.5 row names Stage 8 beside Stage 5; § Stage 5's deferrals say so.
- ✅ **Q5, paid 2026-09-29** → `ContentStreamParser`'s FileMap row re-tagged ○ (S10); moved from § Stage 5's to
  § Stage 10's *Components*; `Stage2-Rendering-Plan.md`'s out-of-scope row, which named Stage 5, corrected in place.
- ✅ **Q6, 2026-09-29** → nothing outside this plan.
- ✅ **Q7, paid 2026-09-29** → `Implementation-Plan.md` § Stage 5's settings list gains the fourteenth key.

**Owed with the code**:

- Every ⚠️ row in §4, with its file; `DocumentIndex`'s and `DocumentComparer`'s FileMap corrections
  (D5-4, D5-5).
- **Flow G** (`Architecture-FileMap.md` §5) redrawn at T7 and T9 with the engine thread and the
  `Background` lane, which the flow does not show. **Flow C** gains the text job at T2. Stage 4 paid
  two flow updates a task late and wrote down that the list is what noticed.
- `Tech-Stack-Plan.md` §3.3's A2 entry, at T3 and T14, kept in two blocks as Stage 4's were; §10's
  **R1** row at T4.
- `Implementation-Plan.md` §8's **O7** row at T3.
- **`CLAUDE.md`'s *What works today* and *deliberately absent* paragraphs**, re-derived at T14 from
  the product: `Select` selects, the unregistered count, the backend census **4 of 7**, the waiver
  census **15**, and the fuzz target count **4** — each re-measured, none predicted here.

---

## 11. Explicitly out of scope

| Not in Stage 5 | Arrives at |
|---|---|
| **Any document change** — no command, no `Commands.def` row (D5-1). **Replace** is not built; the bar is the find half | Stage 13 |
| **The catalog index**, persistent folder indexes, index build logs, embedding an index in a PDF, portfolio search | Stage 16 (R2) |
| **Searching the OCR text layer** — there is none until OCR exists | Stage 16 |
| **Opening an attachment** from a search hit (D5-12) | Stage 6 |
| **Tag-tree reading order**; a tagged document is ordered by geometry like any other (D5-6) | Stage 7 |
| **A vector (EMF) snapshot** (D5-10) — and `WinRichClipboard`, whose paste path is Stage 12's | `DW-2`; Stage 12 |
| **A difference summary PAGE, and an annotated PDF comparison report** (D5-17) | Stage 8 (Q4) |
| **`pdf/cos/ContentStreamParser`** (D5-19) | Stage 10 (Q5) |
| **Keyboard caret browsing** — extending a selection with `Shift`+arrows | Not scheduled; `Features.md` does not list it |
| **Rulers, grid and guides inside the comparison view** (D5-18) | Not scheduled |
| **Find inside Reflow view** (D5-16) | Not scheduled |
| **`Ctrl+B`, `Ctrl+E`, `Ctrl+.`, `Ctrl+,`** — the four Viewer chords still unregistered | Stages 7, 6, 6, 6 |
| **`find.inComments`** (`Ctrl+F` in the Comments pane) | Stage 6 |
| **Substitute page colours** — Stage 4's I-07 | `DW-1`, untouched |

---

## 12. Risks inside Stage 5

| Risk | Shape | Mitigation |
|---|---|---|
| **Background extraction starves the page** | A 900-page first search, a folder search or a comparison holds the engine thread while a visible tile waits, and a job inside PDFium cannot be preempted | D5-3's lane below `Speculative`; **T3 measures the per-page extraction time and the viewport's p95 before any surface queues work** |
| **The analyzer is good on our corpus and poor on real documents** | Generated text is regular, and real text is not | D5-7 bounds every claim to the generated corpus and fixes the targets before measuring. **Not mitigated at Stage 5, by decision** (D5-23): no case meets a real document, so the gap is carried to R1's row and Stage 13's spike |
| **A highlight or a selection is offset from the glyphs, and the suite agrees** | Stage 4's I-10 in a new form: the case asserts the same transform the product uses | §1's rule: **geometry is checked against the rendered ink**, found in the raster by code that shares nothing with the model |
| **Unmapped text is extracted as plausible garbage** | A font with no usable `/ToUnicode` yields codes, and codes look like characters | D5-2's **unmapped** flag; `cjk-embedded.pdf` is the control that must be reported, not extracted |
| **Find, search and compare disagree about the same word** | Three normalizers | D5-5: one pipeline, one set of cases, all three consumers |
| **A comparison of two large or unrelated documents never finishes** | Myers is O(ND), and D is large when documents differ | D5-17's page alignment and cap; the unrelated pair is a case with a time bound |
| **`Ctrl+C` or `Ctrl+A` stops working in a text field** | Stage 4's I-06: a Common registration consumes the key | D5-9's control over every text-owning widget |
| **A folder search freezes the window** | Document open runs on the UI thread today | D5-13: every open of a folder search is on the engine thread; the case asserts the UI thread opened nothing |
| **A hostile PDF's coordinates hang or crash the analyzer** | NaN, infinities and degenerate boxes from an untrusted file | D5-21's fuzz target, and ASan over `core/text/` |

---

## 13. Definition of Done for Stage 5

[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions),
made concrete:

1. §0 answered, and T1…T14 complete; the
   [exit criteria](./Implementation-Plan.md#stage-5--text-layer-extraction-selection-find-search-comparison)
   met.
2. `cmake --build --preset x64-release --target checks` clean — four gates, **`mutation-lint` still
   reporting one command** (D5-1), `layer-lint` at **15** escapes with the new one named (D5-13).
3. `ctest` green on Windows across all six targets and the `gates` preset, **on both presets** (Stage
   4's I-09), every Stage 0–4 case included and present **by name**.
4. `portable-asan` green including every `core/text/` suite, read from the job's **log**;
   `portable-fuzz` and the soak clean on **four** targets.
5. **Golden suite green with every existing baseline unchanged** — Stage 5 changes no
   rasterization; only T1's new files add baselines.
6. **The A2-at-Stage-5 verdict recorded**, bench and product, in `Tech-Stack-Plan.md` §3.3; **the
   layout baseline** in §10's R1 row; **O7's revisit** in `Implementation-Plan.md` §8.
7. [`Stage5-TextSearch-Testing-Guide.md`](./Stage5-TextSearch-Testing-Guide.md) — written from this
   plan before the code, its automated half run green by me with **actual** output, its manual half
   walked and signed off **by you**. I do not mark a manual case.
8. No open S1 or S2 in the guide's issue log.
9. `Architecture-FileMap.md` rows flipped and §10's debt paid, in the same changes as the code.
10. `Implementation-Plan.md`'s Stage 5 status block updated to reality, deviations included.
