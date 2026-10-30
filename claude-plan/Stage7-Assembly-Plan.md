# Stage 7 — Document Assembly · Execution Plan

> **What this document owns.** The **execution order** of Stage 7 and the **Stage-7-local design
> choices** that no other document decides. It owns no product facts. Every stack, layering,
> key-binding and verification fact is cross-referenced to its owner and never restated as a new
> decision:
>
> | For… | Read |
> |---|---|
> | The Stage 7 objectives, components, key bindings handled, settings keys, exit criteria and deferrals | [`Implementation-Plan.md` § Stage 7](./Implementation-Plan.md#stage-7--document-assembly) |
> | The layering rule, mutation-through-commands, risk **A2** | [`Tech-Stack-Plan.md` §3.3, §4.2, §5](./Tech-Stack-Plan.md) |
> | What each file is for, and flows **D** (a mutation) and **F** (save and recovery) | [`Architecture-FileMap.md` §3, §5](./Architecture-FileMap.md) |
> | The authoritative key bindings — the whole **Page Thumbnails pane** and **Bookmarks pane** tables, the Viewer row `Ctrl+B`, the Global rows `Ctrl+Shift+L` and `Ctrl+Shift+E` | [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference); the capabilities in §5.1–§5.10 and §4.1's *custom stamp from a PDF page* |
> | How Stage 7 is verified | [`Stage7-Assembly-Testing-Guide.md`](./Stage7-Assembly-Testing-Guide.md) — written from this plan, **before** the code |
>
> **Status:** ⚪ **Written 2026-10-28, the day Stage 6 was signed off, as a draft; §0 ANSWERED 2026-10-29, all
> five — Q1 … Q5 approved as proposed, and Q1 AMENDED the same day by your choice to bring Save As's rewrite into
> this stage (D7-25), which added T6 and renumbered T6 … T18 as T7 … T19 — so this is now the plan.** No Stage 7
> code exists. The guide was written from the draft the same day, and every case written against a §0 proposal
> was re-read against its answer on 2026-10-29 (the guide's status line says how each stands). §0 is kept as the
> record of what was asked. This file is a plan, not a progress log: Stage 7's live status, task by task and deviation by
> deviation, will be the **Stage 7 status block in
> [`Implementation-Plan.md`](./Implementation-Plan.md#stage-7--document-assembly)**, which is the
> authority.
>
> **Label scoping, because five namespaces collide across stages.** `T1`…`T19` here are **Stage 7**
> tasks and are unrelated to any earlier stage's; this document writes "Stage 7 T4" wherever the
> stage is not already obvious. `D7-n` are this stage's decisions, distinct from `Tech-Stack-Plan.md`'s
> `D1`…`D10` and from `D2-n` … `D6-n`. `A2` and `A4` are **risks** (`Tech-Stack-Plan.md` §3.3); `A-nn`
> are **test cases** (`Stage7-Assembly-Testing-Guide.md` §5). `Q1`…`Q5` are this plan's §0 questions.
> A bare `I-nn` in this file is **Stage 6's** (`Stage6-Annotations-Testing-Guide.md` §8) unless it
> says otherwise.

---

## 0. Decisions this draft needed from you before T1 — all answered 2026-10-29

Each is argued in §3. None can be settled from the documents alone: each moves a capability, a
component or a dependency between stages, or takes in work the user deferred. A later stage would
inherit whatever this plan wrote down, so it is asked now rather than decided here.

| # | The question | This draft proposes | Why it is yours |
|---|---|---|---|
| **Q1** ✅ | **Half of §5 makes a NEW document, and the writer for one is Stage 8's.** Extract (to one file, to one file per page, and *extract and delete*), the five splits, merge and imposition each produce a PDF that did not exist. The roadmap's only whole-file writer is `pdf/cos/FullWriter`, Stage 8's — Stage 6's Q4 moved comment summaries to Stage 8 for exactly this reason (D6-19) | **A first-party assembler, `pdf/ops/DocumentAssembler`, writes a NEW single-revision file from object graphs `ObjectGraphCopier` copied** — one classic cross-reference section, no object streams, no compaction, no version targeting: it never rewrites an existing document, which is what `FullWriter` does and keeps. Merge, imposition and extract-to-one open the result as an **untitled tab** (Stage 6's `openBytes`, Saved As like any untitled document); split and extract-per-page write files to a chosen folder **through `pdf/ops`**, so no waiver. **`ExtractPages` and `MergeDocuments` stop being commands** — they change no open document — so the roadmap's command list loses them (**D7-5**). **Approved by the user 2026-10-29**, and **amended the same day**: `pdf/cos/FullWriter` came into this stage (D7-25), so the assembler BUILDS a new document and `FullWriter` WRITES it — one writer for new and existing files | Builds a writer the roadmap gave Stage 8, and removes two commands the roadmap lists. The alternatives are to move every new-document output to Stage 8, leaving Stage 7 the in-place operations only, or to build `FullWriter` here |
| **Q2** ✅ | **Seven capabilities rewrite EXISTING page content, and the content-stream parser is Stage 10's.** *Flatten all layers*, *delete a layer and its content*, *assign selected content to a layer*, *auto-tag an untagged document*, *mark content as an artifact*, *set a language on a run*, and the roadmap's `AutoTag` all have to find and rewrite marked content inside streams someone else wrote. `pdf/cos/ContentStreamParser` moved to Stage 10 on 2026-09-29 (`Stage5-TextSearch-Plan.md` D5-19), its first caller `RedactionOps` | **Move those seven to Stage 10**, beside the parser and the redaction rewrite that needs the same machinery. **Stage 7 keeps everything expressible at the OBJECT level**: layers viewed, shown, hidden, locked, renamed, created, grouped, merged (by repointing `/Properties` resource entries, D7-9), deleted with their content *orphaned*, their default and per-usage visibility set; the tag tree viewed, retyped, re-ordered, its document and element languages set, and its order overlay. Every decoration Stage 7 writes is a whole stream of its own, so removing one needs no parser (D7-7). **Approved by the user 2026-10-29** | Moves seven capabilities and two commands (`FlattenLayers`, `AutoTag`) to Stage 10, and narrows the exit criterion *"the tag tree can be viewed, retyped and reordered"* not at all — it never named auto-tagging |
| **Q3** ✅ | **The stage is the roadmap's largest after Stage 13**, and Stage 8 waits on all of it (`Implementation-Plan.md` §3). What Stage 8 actually needs from Stage 7 is page import — its comment summaries place the document's pages (D6-19) — which is `ObjectGraphCopier` and the assembler, both early here | **One plan, one guide, nineteen tasks (eighteen as drafted; D7-25 added T6) — and a SIGN-OFF CHECKPOINT after T13**: T1 … T13 (pages, copying, new documents, the thumbnails pane, boxes, merge, bookmarks, links and destinations, the PDF-page stamp) signed off as **Stage 7a**, after which **Stage 8 may start**; T14 … T19 (headers, footers, Bates, watermarks, imposition, layers, tags, the re-measurement) as **Stage 7b**, which may run beside Stage 8. §3's graph gains the split, as Stage 13's 13a/13b already allows. **Approved by the user 2026-10-29**; with D7-25, 7a also delivers Save As that rewrites | Changes the stage dependency graph and what a sign-off means for this stage |
| **Q4** ✅ | **`DW-4` (`Deferred-Work.md`) is unassigned, and Stage 7 makes its second cause much worse.** That cause: *the journal's show step composes and verifies the WHOLE unsaved update on the UI thread on every edit*. Stage 6's unsaved update was hundreds of annotations; Stage 7's is whole copied documents — after a 30-page insert or a merged source of 2,000 objects, every later bookmark rename or `Alt+↓` would re-serialize and re-verify all of it before the screen moves | **Take DW-4's second cause into Stage 7, as T4**: the sink composes only what changed since the last show and keeps the rest composed; the verify runs off the UI thread and a command is refused only when it fails (D6-2's rule kept). **DW-4's first cause — a tile of a page with hundreds of annotations costs ~50 ms in PDFium — stays deferred**, unassigned, and `DW-4`'s entry is split to say which half Stage 7 took. **Approved by the user 2026-10-29** | `DW-4` was deferred by your decision (2026-10-20), and `Deferred-Work.md` says a stage's plan takes an item only by naming it |
| **Q5** ✅ | **Five capabilities in §5 need another stage's machinery** | **(a)** *Include non-PDF files in a merge, converting them* → **Stage 12** (conversion). **(b)** *Impose only at print time* → **Stage 8** (printing). **(c)** *Bates numbering across documents that are not open* — a file list or folder → **Stage 14** (batch); Stage 7 numbers across the **open** documents you pick, each one its own undoable command (D7-14). **(d)** *Insert pages from the clipboard* takes **RawPage pages and images** only; a page made from other clipboard content (text, HTML, Office) is Stage 12's *Create from clipboard*. **(e)** **Text Stage 7 DRAWS** — header, footer, watermark, Bates prefix and suffix, the table-of-contents page — **uses D6-7's fonts**: base-14 Helvetica for WinAnsi, the four predefined CJK CID fonts, anything else refused by name, and **`DW-3` widened** to say so. Bookmark titles, layer names and link text are strings, not drawn, and take any Unicode. **All five approved by the user 2026-10-29** | Moves four capabilities to later stages and widens a deferral you accepted for comments to every drawn text |

**All five answered on 2026-10-29: Q1 … Q5 approved as proposed — and Q1 amended the same day by a
decision outside the five, D7-25 (`FullWriter` and two structure exports brought in from Stage 8, your choice of
three options)**; their edits are made, and §10 records where.

**Not asked, because the documents already decide it:** batch is Stage 14, scanner insert Stage 16,
flattening annotations into content Stage 8, portfolios Stage 16 (`Implementation-Plan.md` § Stage 7,
*Deferrals*); an encrypted document is not edited until Stage 10 (D2-6, D3-7, D6-3); signatures are
validated at Stage 11; accessibility is out of scope (`Features.md` → *Removed from scope*), and
`Features.md` §5.5 keeps the structure tree for Write mode (decision **O2**, `Implementation-Plan.md`
§8).

---

## 1. What Stage 7 is actually for

Stage 6 changed what is ON a page. **Stage 7 changes which pages a document has, in what order, and
what points at them** — and it is the first stage that makes a PDF that did not exist. It completes
milestone **M2**'s assembly half (`Implementation-Plan.md` §6).

Five things get permanently harder if this stage gets them wrong.

1. **Moving a page must not break what points at it.** Bookmarks, links, named destinations, page
   labels, structure-tree page references, article threads and `/OpenAction` all name pages — some by
   object, some by INDEX. The roadmap calls a *"move pages"* that breaks bookmarks *"the classic bug
   of this feature area"*, and it is classic because the obvious oracle — our own page map — agrees
   with itself. → **D7-2**, graded by what the target page SHOWS (**D7-18**).
2. **Six stages of the product assume a page never moves.** The tile cache, the thumbnails, the text
   index, the annotation store, the find bar's hits, the view's current page, a split pane's position,
   the view history and every selection are keyed by page INDEX, and `IDocumentBackend::pages()` is
   *"read once at open and never changes"* (`core/backend.h`). Each one that is not told about a move
   shows the wrong page's pixels or text, which is an S1 that looks like a rendering glitch. → **D7-3**.
3. **Copying a page between documents copies a GRAPH.** A page reaches fonts, images, colour spaces,
   patterns, annotations and their appearances, and inherits `/Resources`, `/MediaBox` and `/Rotate`
   from ancestors it leaves behind. Two pages sharing a font must share it in the copy; a reference
   back to the source's page tree must not come along. → **D7-4**.
4. **What RawPage adds, RawPage must be able to remove — without guessing.** A header, a Bates number
   or a watermark that cannot be found again exactly is a permanent change to someone else's
   document. → **D7-7**.
5. **A new document is read by readers RawPage has never met**, exactly as Stage 6's annotations
   were. → **D7-5**, judged by `qpdf`, Acrobat and Firefox (**D7-18**).

**What Stage 6 should change about how this stage is built.** Stage 6 raised fifty-one issues, and
**twenty-nine of them, I-23 … I-51, came after its last task (T15, 2026-10-20)** — from the manual
walks, the nightly soak and the user (`Stage6-Annotations-Testing-Guide.md` §9, item 9). Its lessons
that apply here with more force:

- **The oracle for a page's identity is what the page SHOWS** (Stage 6's rule 7, generalized): every
  corpus page used by a reference case prints its own identity in large text, and a reference is graded
  by the text extracted from the page it lands on — never by an object number our code computed.
- **A cache keyed by something that can move is a cache that will lie** — Stage 6's I-06 (every
  revision cancelled every queued tile) was the mild form.
- **The show step is on the UI thread** (`DW-4`); Stage 7 must not make it the stage's bottleneck. → Q4 ✅, T4.
- **Walk early**: each manual case is scheduled at the task that makes it walkable (guide §6).
- **A decision made during a walk gets a `D7-n` row the day it is made.**
- **Both presets, and the UI binary whole** (Stage 4's I-09 and I-28).

---

## 2. Inherited ledger — what Stage 6 hands over

Each figure was **re-measured on 2026-10-28** unless it says otherwise.

| Fact | State at the end of Stage 6 | Consequence for Stage 7 |
|---|---|---|
| The suite | **1846/1846** on `x64-release` and `x64-debug` at Stage 6 guide I-50's fix, five named skips (a figure from the named runs of 2026-10-28, `CLAUDE.md`); CI run **40626533138** on `f052a09`, all five jobs green | Stage 7 adds to a green suite |
| `mutation-lint` | *"clean -- 12 command(s): AddAnnotationCommand … RotatePageCommand; 12 table row(s); 222 test file(s) scanned"* | **D7-1**: it names exactly §4's commands at T19 |
| `core/doc/PageTree` | Read half and **one mutation**, `setRotation`; insert / move / remove / duplicate *"deliberately absent rather than present-and-refusing"* (its header, D3-8) | T2 |
| `IDocumentBackend::pages()` | Read **once at open**; `adoptRevision` keeps it; an unsaved rotation is a render DELTA against it (D3-15) | **D7-3**: from T2 the backend's page tree is the model's, and the delta is against the revision the engine draws |
| Save As | **Copies the newest revision on disk and appends the update** (D3-4, D3-16); `pdf/cos/FullWriter` ○ (S8); `file.saveACopy` and `file.revert` declared and unregistered (`--list-actions`); `documents/saveMode` undeclared — D3-9 sent it to Stage 8 | **D7-25** (2026-10-29): T6 |
| `core/doc/Scope` | `Nothing`, `Pages`, `Objects`, `WholeDocument`; an aspect (content, annotations) and annotation regions since Stage 6 T4 (D6-10). *"A page insert"* is its own example of `WholeDocument` | **D7-3** adds a page MAP beside the scope, so a move remaps rather than drops |
| `pdf/ops/PageOps` | The page walk (PDFium's rules: `/Count` never read, loops skipped, iterative, depth-bounded) and `rotatedPage`. Inheritance carried: `/Rotate` and `/MediaBox` only | T2 adds insert, remove, move, duplicate; **D7-4** materializes all four inheritable attributes on a copied page |
| `MutationSink` | `setPageRotation`; the annotation and attachment intents (Stage 6 T3, T12) | T2 onward add the page, outline, destination, label, box, layer, structure and decoration intents (§4) |
| `showEdits` (D6-2) | The engine draws the newest revision on disk followed by the composed update, swapped in between jobs; **compose and verify run on the UI thread per command** | **Q4**: T4 |
| `core/doc/Outline`, `Destinations` | Read through **PDFium** at open (D4-3), read-only; `BookmarksPane` and `DestinationsPane` navigate | **D7-10**: editing needs object identity, so a first-party reader beside PDFium's, held to agree (D6-3's rule) |
| `core/doc/AnnotationStore` | Every annotation read first-party; **`Link` is a subtype it reads and never edits** | **D7-12**: links are annotations, edited through Stage 6's commands |
| Link following | **None.** No file under `pdf/`, `core/` or `ui/` reads a link's action (searched 2026-10-28) | **D7-12** |
| `ActionRegistry` | **389 declared, 173 registered, 216 unregistered** (`--list-actions`). Declared and registered by nothing: `page.insert`, `.delete`, `.extract`, `.replace`, `.split`, `.crop`, `.resize`, `.renumber`, `.duplicate`, `.moveEarlier`, `.moveLater`, `.selectAll`, `.cut`, `.copy`, `.paste`; `merge.documents`; `bookmark.new`, `.rename`, `.delete`, `.demote`, `.promote`, `.moveUp`, `.moveDown`, `.setDestination`, `.generate`; `pane.layers`, `pane.tags`; six `tag.*`, six `layer.*`; `headerFooter.add/update/remove`, `bates.add/remove`, `watermark.add/update/remove`; `link.create/edit/delete/autoDetect`. **No `imposition.*`, `destination.*` or `link.verify` id exists** | Stage 7 registers handlers and mints only what a surface needs and §8 does not name (§4); the census is re-measured at T19 |
| `KeyBindingDefaults.def` | Every Page Thumbnails and Bookmarks row of `Features.md` §8 is present, bound to its id; `Ctrl+B` in the Viewer is the one Viewer chord still unregistered (`Shipped-Surface.md` §2) | T8 and T11 register them; `tst_keybindings` compares the rows |
| `layer-lint` | *"clean -- 670 files, 6823 includes, 15 escapes"* | **No new waiver planned**: every file Stage 7 writes is written by `pdf/` (D7-5, D7-10) |
| `core/settings/SettingsKeys.def` | **80** `RAWPAGE_SETTING` rows; **none** of the roadmap's fifteen `documents/*` keys for Stage 7; the **Documents** Preferences page ships (`PreferencesDialog::shippedPages()`), so a key declared in that group appears on it | **D7-22** |
| `core/settings/ProfileStore` | The custom-stamp library (Stage 6 T10); *"the other profile kinds below, S7–S14"* | Watermark, header/footer and imposition profiles (D7-22) |
| `corpus/` | **108 PDFs**; `golden-update --check-manifest`: *"103 with baselines and 5 excluded with a reason"*. Fixtures that touch this stage: `outline-labels.pdf` (outline, `/PageLabels`), `dests-named.pdf` (both forms), `tagged.pdf` (`/StructTreeRoot`), `forms-acroform.pdf`, `cropbox-margins.pdf`, `pages-900.pdf`, `text-900.pdf`, `mixed-page-sizes.pdf`, `rotated-pages.pdf`, `rotate-inherited.pdf`, the `encrypted/` and `signed/` sets. **No fixture carries optional content or article threads**; one generator writes a `/Link` | **T1** (§5.2) |
| Fuzz targets | **Five** (`cmake/RawPageFuzzTargets.cmake`) | **D7-23**: six |
| The rig | **Acrobat 26.2.22031** and **Firefox 158.0.1** (read 2026-10-28); Chrome and Edge are PDFium; **no physical printer** — `Get-Printer` lists Fax, Microsoft Print to PDF, XPS Document Writer and OneNote only | §7 — **P-3 is open** |
| Open issues carried | Stage 4's **I-07** (`DW-1`); Stage 2's I-06, I-11 (open by decision); `DW-2`, `DW-3`, `DW-4` unassigned | **`DW-4`'s second cause is Stage 7's T4** (Q4 ✅, 2026-10-29); none other is Stage 7's except by and Q5(e). Stage 7 starts with no open S1 or S2 |

---

## 3. Stage-7-local decisions

Choices this plan makes because nothing upstream decides them. **Rows marked ✅ carry a §0
question**, every one answered on 2026-10-29 (§0), or D7-25's decision made the same day; each row reads as
its answer left it.

| # | Decision | Why | Revisit |
|---|---|---|---|
| **D7-1** | **Every document change in Stage 7 is a command, and the set of commands is closed.** §4 lists them; `mutation-lint` names exactly those at T19. One command may move, insert, delete or decorate many pages — a drag of 40 thumbnails, a header on 900 pages, a generated outline of 300 items — and is **one undo step**. Each ships with its `Commands.def` row, a `[serialize:Class]` case, a `[revert:Class]` case, and a `tst_reversibility` sweep over every corpus file it applies to. **Nothing else changes the document**: a layer shown or hidden, a selection, the order overlay, an expanded bookmark and a thumbnail's size are view state. Writing a NEW document (D7-5) changes no open one and is not a command. | D3-1's journal is the only path (`Stage3-Mutation-Plan.md` §3). | — |
| **D7-2** | **A page is its page OBJECT, and a page command repairs what names pages any other way.** `MovePages` reorders `/Kids` and never copies a page, so everything that names a page by REFERENCE — an outline item's or a link's `/Dest`, a named destination, a structure element's `/Pg`, a thread bead's `/P`, `/OpenAction` — follows it with no write at all. **What is repaired is what names pages by INDEX or POSITION**: `/PageLabels` (a number tree keyed by index, rewritten so each page keeps the label it had); every `core/` model hydrated with page indices (D7-3); and a remote `GoToR` into this file, which names a page number in another document and **cannot be repaired from here — it is reported, not touched**. **A reference to a DELETED page is never left dangling**: an outline item keeps its title with its destination cleared and is shown as having none; a link elsewhere keeps its rectangle with its action cleared; a named destination to it is removed; a structure element's content items on it are removed from the tree; a thread's bead on it is unlinked; `/OpenAction` to it is removed — **in the same command, restored by its revert, and listed in the command's report**. `DuplicatePages` makes new page objects whose own annotations are copied and whose references keep pointing at the ORIGINALS. | A reference that already follows the object needs no code, and code that "repairs" it is how a correct reference is made wrong. The index-keyed half is where the classic bug lives. A dangling reference is a defect another reader shows (Acrobat reports a bad destination); silently dropping the bookmark loses the user's work. | — |
| **D7-3** | **A page command returns a PAGE MAP beside its `Scope`, and every index-keyed consumer REMAPS rather than drops.** `core/doc/PageMap` (⚠️) maps each old index to its new one or to *removed*, and lists the inserted indices. The consumers, each with a case: the backend's page tree (from T2 **the model's**, and the engine reloaded through D6-2's path so its index is the model's), the tile cache and the thumbnails (a moved page **keeps its tiles**: keys are remapped, pixels not redrawn), `DocumentIndex` (a moved page **keeps its text**), `AnnotationStore`'s page indices, the find bar's hits, the view's current page and every split pane's, the view history, both panes' selections, and the D3-15 rotation delta (now against the revision the ENGINE draws, which a reload moves). Inserted pages are a content `Scope` of their own; a removed page's state is dropped. **A consumer that cannot remap answers to `WholeDocument`**, which is slower and never wrong (Scope's widening rule). ***Amended at T3 (2026-10-30):*** **the backend's tree is the tree the ENGINE DRAWS**, not the model's: page n is the page the reloaded engine draws at n, as it was read at OPEN -- its boxes and `/Rotate` the opened page's wherever it moves -- so D3-15's delta keeps meaning *the model's rotation of this page against the one it opened with*, and the engine's own copy is swapped in WITH the engine document under the engine lock (`PdfiumDocument::drawnPagesLocked`). **Two consumers the list above did not name are remapped too: the outline and the named destinations** -- the bookmarks and destinations panes and a bookmark found by a search open the page OBJECT a destination named, a copy never taken for it, and an unresolved destination where the command removed it, which is what the file's repair makes of it (D7-2). **The text index numbers its reads by an EPOCH** a page command moves (`TextRequest::epoch`), so a page's text extracted before a command and delivered after it is refused rather than filed under the number's new page; **a running or finished tile is dropped, never moved**, because the reload swaps in before the engine's next call and nothing says which engine drew it; **a search running across a command is ended and run again**; and **a selection survives only where its pages are still a run**. **A2's named work** (D7-19, guide A-33, the user's choice): after every page command one pixel of the first page it moved is asked for at `Background`, so the reload and the reloaded document's cold first page are paid while the engine is idle (`RenderScheduler::documentChanged`). | Without the map, a move of one page in `pages-900.pdf` is either a full re-render and re-extraction (`WholeDocument`) or a wrong one (a stale index). Remapping is a permutation — O(pages) of arithmetic against seconds of PDFium. T3 measures both before any surface moves a page. | T3's verdict |
| **D7-4** | **`pdf/ops/ObjectGraphCopier` copies a page's graph into another document — each reached object exactly once, shared objects shared, references renumbered — and stops at the page tree.** `/Parent` is never followed; the four inheritable attributes (`/Resources`, `/MediaBox`, `/CropBox`, `/Rotate`) are **materialized** on the copied page from the walk's resolved values (`PageOps`' rule: down the path the walk took, never up `/Parent`). **Annotations come with their page**: `/P` repointed, `/Popup`/`/IRT` kept inside the copied set and dropped when their target was not copied; a **link** whose destination page was copied in the same call points at the copy, and one whose page was not keeps its rectangle with its action cleared, reported (D7-2's rule). `/StructParent(s)` are **removed** from a copied page and its annotations — a copied page arrives UNTAGGED and the report says so, because a structure tree cannot be grafted without the parent tree it indexes. Widgets come with their page; their FIELDS are D7-16's. An **encrypted source is refused** (the first-party reader declines it, D2-6). Bounded: an object budget and a depth bound, reported by key; a cycle is a visited set. One copy call over several pages of one source shares every object those pages share. | The roadmap's component (Stage 6's Q7 moved it here). Materializing inheritance is the copier's whole difficulty — a copied page whose `/Resources` lived on its old parent draws with no fonts, and every test that copies a page from `pages-900.pdf` (two levels, resources on the page) passes over it; `rotate-inherited.pdf` and §5.2's `inherit-*.pdf` are the fixtures. | — |
| **D7-5** ✅ | ✅ *(Q1, approved by the user 2026-10-29; amended the same day by D7-25)* **A new document is written by `pdf/ops/DocumentAssembler`, from copied graphs, as ONE classic revision.** Header `%PDF-1.7` and the binary comment line; a catalog, a balanced page tree (`kMaxKids` per node), the copied objects renumbered from 1, one `xref` table and trailer with a fresh `/ID`; **no object streams, no compression of objects that were not compressed, no linearization**. Then it is **read back from its bytes and verified** — opening with no repair, every page as planned, every object reachable — exactly as `RevisionWriter::verify` verifies an update. Merge, imposition and extract-to-one open the bytes as an **untitled tab** (`openBytes`, D6-17), Saved As like any untitled document; split and extract-per-page write to a chosen folder through `pdf/ops`, **never overwriting** an existing file (a collision is reported, nothing written). **`ExtractPages` and `MergeDocuments` are not commands**: *extract and delete* is the assembly followed, only once the file is written and verified, by one `DeletePages`. ***Amended 2026-10-29 (D7-25):*** the assembler BUILDS the new document — catalog, balanced page tree, the copied graphs numbered from 1 — and `pdf/cos/FullWriter`, now this stage's, WRITES and verifies it: one writer for new and existing files. | A new document has no previous revision to append to, so the incremental writer cannot make one, and writing one fresh section from objects we numbered ourselves is a small fraction of what `FullWriter` does to an existing file (renumbering, dropping the unreachable, object streams, version targets — Stage 8's and kept there). | — (the writing half is `FullWriter`'s since D7-25) |
| **D7-6** | **A command that brings pages from elsewhere carries the COPIED GRAPH, not the source file.** `InsertPages` and `ReplacePages` serialize the objects `ObjectGraphCopier` produced, with source-local numbers that the sink renumbers at every compose (D6-4's rule: a command never names a number a compose allocated). A payload over 64 KiB is a **side file named by its SHA-256** beside the journal (D6-23's mechanism, `RecoveryJournal`'s existing whole-file write — the waiver census stays 15). Recovery therefore replays without the source, and a source changed or deleted after the insert changes nothing. | The source is not the document's and may be gone by the time recovery runs; the copied graph is what the command means. Copying at the command, not at every compose, is also what keeps a compose from re-reading a 200 MB source. | — |
| **D7-7** | **Every decoration Stage 7 writes is a STREAM OF ITS OWN, marked, so it can be found and removed without parsing anything.** A header, footer, Bates number or watermark is a form XObject drawn by a three-operator stream (`q /RawPageDec<n> Do Q`, wrapped in `/Artifact <</Type /Pagination /Subtype /Header|/Footer|/Watermark>> BDC … EMC`) **appended to** (in front) or **prepended to** (behind) the page's `/Contents` array, both streams' dictionaries carrying **`/RawPageDecoration << /Kind /HeaderFooter|/Bates|/Watermark /Id <hex> >>`**. *Remove* deletes exactly the marked streams from `/Contents` and the marked XObject from `/Resources /XObject`; *update* is remove and apply, one command. *Shrink content to avoid the header* and *resize with scaling* wrap the original content in a marked `q … cm` / `Q` pair of streams the same way. **A decoration RawPage did not write is never touched** — `Features.md` §5.7 and §5.8 say *added by RawPage*. | The roadmap asks for marked content tagged with a RawPage key. Putting the key on whole streams rather than inside them is what lets removal be a list operation on `/Contents` — byte-exact, so the guide's apply → remove → compare case is exact — and keeps every Stage 7 write away from Stage 10's parser (Q2). Acrobat recognizes `/Artifact … /Pagination` as pagination artifacts and leaves ours to us. | Stage 10 (the parser can read them back too) |
| **D7-8** | **Imposition is arithmetic in `core/` and a new document from `pdf/ops`.** `core/doc/Imposition` (⚠️) answers, for a spec and a page count, which source page goes where on which side of which sheet — n-up (2, 4, 6, 8, 9, 16; across, down, reversed), booklet (left and right binding, blanks padded to a multiple of four, **creep** shifting each sheet's pages by its fold depth), gutters, duplex flip — headless, and graded against hand-written tables (`tst_imposition`). `pdf/ops/ImpositionOps` turns each source page into a form XObject ONCE (the copier), draws sheets with `ContentStreamBuilder` — the pages, frames, crop and registration marks, a colour bar, a sheet label — and hands them to the assembler (D7-5). **Print-time imposition is Stage 8's** (Q5(b)). | The order of pages in a booklet is the part that is wrong on paper and right on screen; it is pure arithmetic and belongs where `rawpage_tests` can prove it for every count, odd ones first. | Stage 8 |
| **D7-9** | **Layers are read first-party into `core/doc/LayerTree` and their VISIBILITY ON SCREEN is view state.** The tree is `/OCProperties`' `/OCGs` and `/D`'s `/Order` (nesting, labels), `/RBGroups`, `/Locked`, `/ON`/`/OFF`, `/AS` usage. **Show and hide change no byte** (Acrobat's behaviour): the engine draws the view's state from a **view-only** update composed after the document's own — `/D`'s `/ON`/`/OFF` rewritten — which `showEdits` hands the engine and **Save never writes**. D6-2's invariant is restated, not broken: *what the screen shows is what Save writes, drawn under the view's layer state*, and every shown-is-saved case runs at the default state. **Commands** (§4): create (an empty group, the target of D7-7's print-only watermark), delete (its content **orphaned**: the group leaves `/OCGs` and `/D`, and content naming it draws as PDFium and Acrobat draw a group no longer listed -- **measured at T17 in both**, not assumed, because what a reader does with an unlisted group is the kind of corner two readers answer differently), merge (every `/Properties` resource entry naming the merged group repointed at the survivor — no content stream touched), and properties — name, lock, default state, print and export usage, grouping, radio groups. **Flatten, delete-with-content and assign-content are Stage 10's** (Q2 ✅, 2026-10-29). | No header in `third_party/pdfium/include/` names an optional-content group, its state or its configuration (searched 2026-10-28), so reading is first-party, and a view state the engine must draw has to reach it as bytes. Making show/hide a document change would dirty a file a person only looked at. | Stage 10 (Q2 ✅) |
| **D7-10** | **Bookmarks and named destinations are read FIRST-PARTY for editing, beside PDFium's reading, and held to agree.** `pdf/ops/OutlineReader` (⚠️) reads the outline's item objects — title, colour, style, open count, destination or action — and the destination name trees; per document they must agree with PDFium's reading (D4-3) in count, depth, titles and target page, or the outline is **read-only and says why** (D6-3's rule). **An edit changes what the model owns and preserves the rest** (D6-5's rule): an outline item's unknown keys survive; items not edited are not rewritten. The name tree is written flat, as D6-38(a) writes `/EmbeddedFiles` — a pair not changed written as it was. Generators (§5.4: from headings — the structure tree's `H`, `H1`…`H6` when the document is tagged, else `LayoutAnalyzer`'s heading blocks — by font size and style thresholds, by a text pattern) are `core/doc/OutlineGenerator` (⚠️), headless over `PageTextModel`. **Import and export** as indented text and XML are written and read through `pdf/ops`, so no waiver. | Editing needs each item's object identity, which PDFium's `FPDFBookmark_*` does not give. Two readers of one fact must agree or say so. | — |
| **D7-11** | **A table-of-contents page is a GENERATED PAGE inserted by one `InsertPages`**, its entries drawn in D6-7's fonts (Q5(e)) with dot leaders and page labels, each line a `Link` annotation to its bookmark's destination. One command, one undo step. | §5.4's *"with working links"*: a page and its links made in one step, undone in one. | — |
| **D7-12** | **A link is a `Link` ANNOTATION and is edited through Stage 6's commands.** `AnnotationStore` already reads it (D6-3); Stage 7 gives the model `/A`, `/Dest`, `/H`, `/Border`/`/BS`, `/C` and `/QuadPoints`, and `AddAnnotation`, `EditAnnotation`, `MoveAnnotation` and `DeleteAnnotation` create, edit, move and delete it — **so the roadmap's `CreateLink`, `EditLink` and `DeleteLink` are not built**, and links inherit identity (D6-4), preservation (D6-5), the reload (D6-2), the overlay's hit-test, align and distribute, and undo. `ui/viewer/tools/LinkTool` drags a rectangle or makes one link per line of a text selection (quadpoints, Stage 6's markup path); `link.autoDetect` finds URLs over `PageTextModel` (headless) and adds every link in one command. **Following a link** (Hand and Select tools, `Enter` on a focused link): `GoTo` and a named destination navigate; `GoToR` opens the file in a tab **after asking**, and its destination; `URI` **asks, then** hands the URL to the platform; `Launch` is **refused** by D6-17's executable list and asks for any other file. **JavaScript, form actions (submit, reset, import, show/hide), media and print are WRITTEN and EDITED, never executed** — the pinned PDFium is the non-V8 build, which `cmake/PdfiumAssert.cmake`'s `PA-4` fails configure to keep, and the product has no script engine. *Verify links* reports a destination page that does not exist, a named destination not in the tree, a `GoToR` file that is not on disk; it fetches nothing. | A link is an annotation in the file; a second model of it would be a second truth. Following one is reading, and opening a URL or a file from someone else's document is the trust decision D6-17 already made for attachments. | Stage 10 (trust model) |
| **D7-13** | **The thumbnails pane selects SETS, drags pages, and carries pages across documents.** Extended selection (click, `Shift`, `Ctrl`); a drag reorders by one `MovePages`; a drag into ANOTHER window's pane of the same process copies by one `InsertPages`; **cut, copy and paste** put a private MIME type (`application/x-rawpage-pages`, the copied graph, D7-6's payload) on the system clipboard, so two RawPage processes exchange pages without a file — D6-22's shape. `page.cut` is copy then one `DeletePages`. Badges show which pages carry comments, form fields or links. The selection is remapped by D7-3's map. **Stage 4's single selection ends** (Stage 4 T5's deviation (b), `Implementation-Plan.md` § Stage 4, which named this stage): `Ctrl+R` rotates the SELECTED pages. | `Features.md` §5.2. The private MIME type is the only cross-process path that needs no file. | — |
| **D7-14** | **Headers, footers and Bates numbers are one engine; Bates across documents is one ALLOCATOR.** `core/doc/HeaderFooter` (⚠️) expands the field tokens — page number, count, label, date, time, filename, path, custom text, Bates — per page, headless; `DecorationOps` draws them (D7-7). **Bates across documents** numbers the **open** documents you pick, in your order, as one logical run (`core/doc/BatesAllocator`, ⚠️): each document gets its own `ApplyBates` in its own journal — each undoable, each saved by you — and the report states the first and last number per document. A file list or a folder is Stage 14's batch (Q5(c)). | The roadmap's *"one logical run"*. A command belongs to one document's journal; a run over several is several commands that share a number line. | Stage 14 |
| **D7-15** | **Page boxes, crop and resize.** `CropPages` sets any of the five boxes on a page set; *remove white margins* reads **Stage 4's content box** (`IDocumentBackend::contentBox`, the engine's union of object bounds) — so the fit and the crop agree on what the ink is — padded by `documents/cropRemoveWhiteMargins`; *apply to all pages of the same size* is a filter in `core/`. *Undo a crop* removes `/CropBox`, restoring the MediaBox. `ResizePages` with scaling wraps the content (D7-7) and sets the MediaBox; without scaling it sets the boxes alone. `CropTool` drags a box on the page in the overlay, snapping as Stage 4's `core/Snap.h` does. **A box outside the MediaBox is clipped to it** and the report says so. | The content box is the measurement Stage 4 already trusts (guide A-29's `cropbox-margins.pdf`); a second definition of *white margin* would disagree with Fit Visible. | — |
| **D7-16** | **Merge builds a new document; fields and layers are merged on purpose.** `MergeDialog` collects inputs, order, ranges and options; the assembler writes the result (D7-5). Per input: its pages through the copier; a **bookmark per file** named from its file name or `/Title` (`documents/mergeBookmarkPerFile`) with the source's own outline beneath it, each destination repointed at the copy; its links between its own copied pages kept (D7-4); its comments with their pages; its **form fields**: the `/AcroForm /Fields` trees merged, a top-level name that collides **renamed** (`name` → `name_2`, every widget's `/Parent` chain consistent) or **merged deliberately** into one field whose widgets share a value, per `documents/renameCollidingFields` — the outcome reported either way; its **layers**: `/OCGs` unioned, `/D`'s `/Order` concatenated under a group per input, `/ON`/`/OFF`/`/Locked` unioned. **Skipped inputs are reported with their reason** — encrypted, unreadable, repaired-with-loss, not a PDF (Q5(a)). An optional blank separator page between inputs, at the size of the page before it. | `Features.md` §5.3 and the roadmap's *"`MergeDocuments` is the only page command that can hit form-field name collisions"* — which Q1 turns from a command into an assembly, the policy unchanged. | — |
| **D7-17** | **The corpus is generated, and every page used by a reference case SHOWS ITS OWN IDENTITY.** `corpus/make-assembly.py` writes §5.2's fixtures with their ground truth in `corpus/labels/assembly-*.json`: per page its identity string, and per reference (outline item, named destination in both forms, link by reference and by name, `GoToR`, page label range, structure element, thread bead, `/OpenAction`) the identity of the page it targets. Each page draws its identity — `P-0007` — in 48-point Helvetica across its centre. | The oracle for a reference must not be our page map (D7-18). | — |
| **D7-18** | **The oracles are independent of the code under test.** (1) **What the page shows**: a reference is graded by the text PDFium extracts from the page it lands on, against the label — never by an object number or an index our code computed; (2) **`qpdf --check`** over every file the suite writes — assemblies, splits, merges, impositions and saved edits — in the `portable-asan` job (Stage 6's P-4); (3) **the byte comparison**: apply → remove of every decoration, apply → revert of every command, against the original; (4) **Acrobat and Firefox** for what other readers do with our bookmarks, links, labels, layers and new documents (guide §6); (5) **paper** for imposition (M-04). | Stage 6's rules 6 to 8, carried: a check that shares a computation with the code it checks agrees with it when both are wrong. | — |
| **D7-19** | **A2 is re-measured at Stage 7, at T3, before any surface moves a page.** `bench_pipeline --stage7` drives the shipped scheduler with a viewport scrolling at D4-16's rate on `pages-900.pdf` and `text-900.pdf` **while** page commands arrive — a `MovePages` of a ten-page range every 100 ms for 30 s (a held `Alt+↓`), then one `InsertPages` of 100 pages from `text-900.pdf` — and reports the viewport's p95 (D2-1's quantity), the **command-to-pixels** latency (from `journal.run` returning to the moved range's first visible tile delivered showing the right page's identity, rule 1), `journal.run`'s own p95, and the remap time. Graded on the guide's §3.1, fixed before the run. **T19 re-reads it through the shipped surfaces** (`bench_surface --stage7`: the real thumbnails pane, a drag). ***At T3 (2026-10-30):*** **the insert is a stand-in** -- `InsertPagesCommand` is T5's, so the 100 pages are a `DuplicatePagesCommand` of 100 pages, the first copy timed to the pixels; T5 replaces it and the reading is taken again. **The verdict, `go, with named custom work`** (the user's choice): the named work is the warm-up D7-3's T3 amendment records, built at T3; `Tech-Stack-Plan.md` §3.3 holds the figures. | D7-3's remap is the design; a measurement is what says it holds. Stage 6 measured its display path at T4 before any tool, and its reading was the stage's most consequential fact. | — |
| **D7-20** | **A document carrying a signature is edited, and warned about once.** Before the first page command on a document whose `/AcroForm` holds a signature field with a value (read first-party; nothing is validated — Stage 11's), the message bar says that the change will be reported as a modification by signature validators, and the command runs. An incremental save leaves every signed byte range intact (A4, `Tech-Stack-Plan.md` §3.3) — the guide checks that OpenSSL still verifies all four `corpus/signed/` files after a page command through the product. | A4 is about integrity, which an incremental save keeps; what validators call a *permitted* change is policy, which page insertion and deletion are not. Refusing would make a signed contract unextendable; silence would let a person break a signature without knowing. | Stage 11 |
| **D7-21** | **What is refused, by key.** An **encrypted** document is not edited (D3-7) and is **skipped** as a merge or insert source; a page command on a **repaired** document runs, and its first save is Save-As (Stage 3's rule); a page command that would leave **zero pages** is refused — *delete every page* is not a document; **a page whose engine and first-party readings disagree** (`Permissions`) refuses page commands that name it, as Stage 3 T4 refuses its rotation (`error.edit.pageReadingsDisagree`, and the refusal moves with the page when other pages move past it). ***Added at T2 (2026-10-30):*** **a page tree the first-party walk noted anything about** — a loop, a kid that is no reference or does not fetch, one past the depth bound — refuses every command that changes the page TREE, by `error.pdf.pageTreeDamaged` naming the object, before the command runs (`Permissions::mayRestructurePages`); a rotation, which writes no node, stays allowed. Its shape would be a guess, and a guessed `/Kids` is how pages are lost. | Each is an existing rule meeting a new command, and the T2 addition is the walk's own notes (Stage 3 T3) made a refusal rather than a log line: the first command that WRITES the tree is the first that needs them. | Stage 10 |
| **D7-22** | **Stage 7 declares the roadmap's fifteen `documents/*` keys and three profile kinds — and no new Preferences page.** The keys are in the **Documents** group, whose page already ships, so each appears there with no new code (`SettingsPage` builds one editor per declared key). Watermark, header/footer and imposition profiles are `ProfileStore` kinds (Stage 6 T10's store), named by the person and stored by `ProfileStore` beside the stamp library, one kind each. | `Features.md` §9.1 puts these settings on the Documents page; a page per feature would be twenty-six pages of one row. | — |
| **D7-23** | **A sixth fuzz target, `fuzz_assembly`**: bytes → `pdf/cos/` → the page walk → `ObjectGraphCopier` (every page, into an empty target) → `DocumentAssembler` → the result re-read and verified; and the `OutlineReader` and `LayerTree` readers over the same bytes. Inheritance chains, a `/Kids` loop, a `/Resources` that reaches the page tree, a million-entry `/OCGs`, an outline cycle. The fuzz-count assertion moves **5 → 6** (`cmake/RawPageFuzzTargets.cmake`); the soak runs it. | The copier and both readers consume untrusted graphs, and the assembler consumes the copier's output. `pdf/ops/` is portable by rule, so it builds under `portable-fuzz`. | — |
| **D7-24** | **Page commands replay against the file as opened, naming pages by INDEX at the time of the command.** The journal's order is the order the commands ran, so an index is unambiguous at replay; a payload is D7-6's side file. The recovery cases replay every Stage 7 command kind after a `TerminateProcess` (Stage 3's harness, guide A-68's shape). | Stage 3's journal replays in order against the opened file; page identity by index is exactly what that ordering makes safe. | — |
| **D7-25** ✅ | ✅ *(your choice B of three, 2026-10-29 — a decision outside §0's five questions)* **`pdf/cos/FullWriter` is built in Stage 7, for Save As that REWRITES and COMPACTS, Save a Copy, and the writing of every new document.** Save As writes one classic revision of what is reachable from the trailer: superseded revisions and unreachable objects — a deleted page's, a replaced page's, an orphaned appearance — are dropped, so deleting 800 of 900 pages shrinks the file. **Every surviving object KEEPS its number and generation** (a dropped one is a free entry), so annotation identities (D6-4), journal lines (D7-24) and the undo history survive a Save As, and the sink compares its desired state against the rewritten file as against any revision (D3-5). The written file is re-read from its path and verified — no repair, every page and every reachable object as the model says — before the replace (D3-4's order, unchanged). **Save a Copy** (`file.saveACopy`, declared since Stage 1) writes the same to another path and leaves the tab on its own path and state; **`documents/saveMode`** (`incremental` | `full`, the key D3-9 sent to Stage 8) is declared here and makes `Ctrl+S` rewrite too. **What is not rewritten:** an **encrypted** document — the first-party reader declines it (D2-6), so Save As copies and appends as today; and a document **carrying a signature** (D7-20's reading) — Save As **asks**: keep the signatures (copy and append, as today) or rewrite (the signatures named as becoming invalid); where no one can be asked, the signatures are kept (convention 16). **Also brought in from `Features.md` §2.5:** *export bookmarks as an indented text outline* (T11, D7-10's export) and *export the document's tag tree* (T18). **Not brought in, and Stage 8's:** dense renumbering, object-stream packing, a target PDF version, linearization, conformance, every export to another format. | Stage 7 creates the problem a rewrite solves — every deletion, replacement and merge leaves objects an incremental save keeps forever — and Q1's assembler was already most of a writer. **Keeping numbers is what makes a rewrite safe mid-session**: renumbering would invalidate every identity the store, the journal and undo hold, for a gain — a dense table — that Stage 8's object streams make anyway. A rewrite of a signed file breaks its signatures, which a person must choose, never meet. | Stage 8 (renumbering, object streams, versions, linearization) |
| **D7-26** ✅ | ✅ *(the user's choice, 2026-10-31, at T4 -- recommended of two)* **The compose numbers every new object by NAME and keeps what it composed.** A new object's number comes from a `cos::NumberBook` under a name the sink gives it (`annot:<NM>`, `ap:<NM>`, `page:<n>`, `shared:<key>`, ...), kept from one compose to the next until a save adopts a revision; a unit whose inputs are what they were -- a created annotation, one edited in place, a page's listing, a shared appearance object, and the annotations' whole block when no annotation command has run -- is written again as the bytes the last compose wrote (`cos::WrittenObject`). A name no compose asks for any more gives its number back: the highest is simply un-given, one below it is an UNUSED number and is written as a **free row**, so `/Size` covers no number without one. **So a saved update's numbering depends on the order things were made in; what it means does not**, and a recovery replay, which composes once at its end, numbers its unsaved objects by its own compose -- the model identical, the bytes not the crashed session's. The inputs are COMPARED each compose, not tracked by the mutators: a wrong comparison rebuilds, where a missed mark would write a stale object. | Q4: a one-page move after 1,500 unsaved highlights rebuilt and re-serialized 3,701 objects -- 25 ms of the UI thread, measured at T4 before the change. The other choice, renumbering compactly whenever a removal opens a gap, would make that removal cost a full compose again. | A history-independent numbering is wanted -- a saved file compared byte for byte across sessions |
| **D7-27** ✅ | ✅ *(the user's choice, 2026-10-31, at T4 -- recommended of three)* **The verify runs off the UI thread and the ENGINE WAITS FOR IT** (`IDocumentBackend::settleEdits`). `showEdits` composes on the caller's thread -- a compose that fails is still refused at once -- hands every engine the update behind a `render::ShowGate`, starts the verify on a thread of its own and returns. The engine swaps the update in only once the gate passes; a job that needs the engine before then waits, holding no lock; a gate that fails drops the revision, and until the settle every job of that document finds no engine rather than draw the old pages at the refused revision's indices. **The journal settles** (`CommandJournal::settle`) before its next run, undo or redo, before a save, and when the backend reports a verify finished (the window posts itself the settle); a refusal TAKES THE OPERATION BACK after it returned -- a run's step reverted and removed, an undo re-applied, a redo reverted -- announced as `WholeDocument`, said in the status bar, the recovery journal told the inverse, and the operation that settled it refused with the same error. A command inside a group, and an abandoned group, are settled at once. **D6-2 holds**: the screen never draws a composition Save refused. The show handler is set WITH its settle handler (`setShowHandler(show, settle)`), because a show whose verify runs elsewhere, with nothing to wait for it, lets the next command change the sink the verify reads (found at T4: a case without one crashed). | Q4 said *the verify off the UI thread and a command refused only when it fails*; the guide's A-36 (*nothing queued to the engine*) and A-37 (*commands and saves wait*) said how. Showing at once and taking back on failure would have let the screen draw what Save refuses for the length of a verify; keeping the verify on the UI thread, made incremental, was a deviation from Q4. | A verify that cannot finish in a person's time between commands (its p95 is 19–20 ms after 3,000 unsaved objects on `text-900.pdf`, T4) |

---

## 4. File manifest

Every file Stage 7 creates or turns from ○, with the [`Architecture-FileMap.md`](./Architecture-FileMap.md)
row it must flip **in the same change**. Rows marked ⚠️ have **no FileMap row** and are lockstep debt
(§10), paid with the file. A row a §0 answer moves out of the stage says so.

| File | FileMap row | Task |
|---|---|---|
| `corpus/make-assembly.py`, §5.2's fixtures, `corpus/labels/assembly-*.json` | **None** — data; one `corpus/PROVENANCE.md` row per file, `golden/manifest.json` rows | T1 |
| `core/doc/PageTree.{h,cpp}` — insert, remove, move, duplicate | its row: ○ (S7) → ✅ | T2 |
| `core/doc/PageMap.{h,cpp}` — D7-3 | ⚠️ | T2 |
| `core/doc/Scope.{h,cpp}` — the map beside the scope | its row | T2 |
| `core/backend.h` — the page intents on `MutationSink`; `pages()` the model's | §3.8's row | T2, T5, T9 |
| `pdf/ops/PageOps.{h,cpp}` — insert, remove, move, duplicate, boxes, labels | its row: 🟡 → ✅ at T9 | T2, T9 |
| `pdf/ops/MutationSinkImpl.{h,cpp}`, `RevisionWriter.{h,cpp}` — pages in the desired state and in `verify()` | their rows | T2 |
| `pdf/ops/ReferenceRepair.{h,cpp}` — D7-2's deleted-page rule and the label rewrite | ⚠️ | T2 |
| `core/cmd/commands/PageCommands.{h,cpp}` — `MovePages`, `DeletePages`, `DuplicatePages` (T2); `InsertPages`, `ReplacePages` (T5); `CropPages`, `ResizePages`, `RenumberPages`, `SetEmbeddedThumbnails` (T9); `RotatePage` over a page set (T8) | its row: 🟡 → ✅ at T9 | T2, T5, T8, T9 |
| `core/render/` — the tile cache, the scheduler and `DocumentIndex` remapping through D7-3's map | their rows | T3 |
| `tests/bench/bench_pipeline` — `--stage7` (D7-19) | its row | T3 |
| `pdf/ops/MutationSinkImpl` — the compose that keeps what it composed; the verify off the UI thread (Q4 ✅) | its row | T4 |
| `pdf/ops/ObjectGraphCopier.{h,cpp}` | its row: ○ (S7) → ✅ | T5 |
| `tests/fuzz/fuzz_assembly.cpp`, its body header (D7-23) | `tests/fuzz/`'s row | T5, T7 |
| `pdf/cos/FullWriter.{h,cpp}` — D7-25: the rewrite that keeps numbers; Save a Copy | its row: re-tagged ○ (S7 rewrite and compaction; S8 packing, versions) 2026-10-29; 🟡 at T6 | T6 |
| `core/doc/SaveCoordinator.{h,cpp}`, `ui/shell/` — Save As's question for a signed document; `file.saveACopy` registered; `documents/saveMode` declared | their rows; `SettingsSchema`'s | T6 |
| `pdf/ops/DocumentAssembler.{h,cpp}` (Q1 ✅) | its row: ○ (S7), added 2026-10-29 with Q1's answer | T7 |
| `ui/dialogs/PageOpsDialogs.{h,cpp}` — insert, delete, extract, replace, split, crop, resize, renumber, and rotate's ranges | the dialog family row's *S7* entry | T7, T8, T9 |
| `ui/panes/ThumbnailsPane.{h,cpp}` — sets, drag, cross-document, clipboard, badges | its row: 🟡 → ✅ | T8 |
| `ui/viewer/tools/CropTool.{h,cpp}` | `ui/viewer/tools/*`'s row | T9 |
| `ui/dialogs/MergeDialog.{h,cpp}` | the dialog family row | T10 |
| `pdf/ops/OutlineReader.{h,cpp}`, `OutlineOps.{h,cpp}` (D7-10) | ⚠️ | T11 |
| `core/doc/Outline.{h,cpp}`, `Destinations.{h,cpp}` — the editing half | their rows: 🟡 → ✅ | T11, T12 |
| `core/doc/OutlineGenerator.{h,cpp}` (D7-10) | ⚠️ | T11 |
| `core/cmd/commands/BookmarkCommands.{h,cpp}` — `CreateBookmark`, `EditBookmark`, `DeleteBookmark`, `ReparentBookmark`, `SetBookmarkTarget`, `GenerateBookmarks` | its row: ○ (S7) → ✅ | T11 |
| `core/cmd/commands/DestinationCommands.{h,cpp}` — `CreateDestination`, `RenameDestination`, `DeleteDestination` | ⚠️ (the roadmap names the commands, the FileMap no file) | T12 |
| `ui/panes/BookmarksPane.{h,cpp}`, `DestinationsPane.{h,cpp}` — the editing half | their rows: 🟡 → ✅ | T11, T12 |
| `core/doc/Annotation.{h,cpp}`, `pdf/ops/AnnotationOps`, `AnnotationReader` — the `Link` fields (D7-12) | their rows | T12 |
| `ui/viewer/tools/LinkTool.{h,cpp}`, `ui/dialogs/LinkPropertiesDialog.{h,cpp}` | `ui/viewer/tools/*`'s row; the dialog family row | T12 |
| `core/doc/LinkActions.{h,cpp}` — following, verification (D7-12) | ⚠️ | T12 |
| `ui/dialogs/CustomStampDialog.{h,cpp}` — a PDF page as the source | its row | T13 |
| `pdf/ops/DecorationOps.{h,cpp}` (D7-7) | its row: ○ (S7) → ✅ | T14, T15 |
| `core/doc/HeaderFooter.{h,cpp}`, `BatesAllocator.{h,cpp}` (D7-14) | ⚠️ | T14 |
| `core/cmd/commands/DecorationCommands.{h,cpp}` — `ApplyHeaderFooter`, `RemoveHeaderFooter`, `ApplyBates`, `RemoveBates` (T14); `ApplyWatermark`, `RemoveWatermark` (T15) | its row: ○ (S7) → ✅ | T14, T15 |
| `ui/dialogs/HeaderFooterDialog`, `BatesDialog`, `WatermarkDialog` | the dialog family row | T14, T15 |
| `core/settings/ProfileStore.{h,cpp}` — three kinds (D7-22) | its row | T14, T15, T16 |
| `core/doc/Imposition.{h,cpp}` (D7-8) | ⚠️ | T16 |
| `pdf/ops/ImpositionOps.{h,cpp}`, `ui/dialogs/ImpositionDialog` | its row: ○ (S7) → ✅; the dialog family row | T16 |
| `core/doc/LayerTree.{h,cpp}`, `pdf/ops/LayerOps.{h,cpp}` (⚠️), `ui/panes/LayersPane.{h,cpp}` | LayerTree's and LayersPane's rows: ○ (S7) → ✅ | T17 |
| `core/doc/TagTree.{h,cpp}`, `pdf/ops/StructureOps.{h,cpp}` (⚠️), `ui/panes/TagsPane.{h,cpp}` — with the tree's export (D7-25) | TagTree's and TagsPane's rows: ○ (S7) → ✅ (the Write-mode half of TagTree's row stays Stage 13's) | T18 |
| `core/cmd/commands/StructureCommands.{h,cpp}` — `CreateLayer`, `DeleteLayer`, `MergeLayers`, `SetLayerProperties` (T17); `RetagElement`, `MoveStructureElement`, `SetStructureLanguage` (T18). **`FlattenLayers` and `AutoTag` moved to Stage 10** (Q2 ✅, 2026-10-29) | its row: ○ (S7) → ✅ | T17, T18 |
| `core/settings/SettingsKeys.def` — the fifteen `documents/*` rows (D7-22) | `SettingsSchema`'s row | as each feature lands |
| `core/cmd/ActionId.def` — the ids §8 names are declared; **minted** where a surface needs one §8 does not: `imposition.create`, `destination.create`, `destination.rename`, `destination.delete`, `link.verify`, `page.embedThumbnails`, `bookmark.import`, `bookmark.export`, `bookmark.properties` — **the final list is re-derived at T19**, not predicted here | `ActionId`'s row | as each surface lands |
| `res/ribbon.json` — the Assemble surfaces | its row | as each surface lands |
| `i18n/` — new families `pages`, `bookmarks`, `layers`, `tags`, `decorate`, `links` (the roadmap's) — **each a namespace no command or action id spells** (Stage 5's I-08; Stage 6 D6-31(b)) — checked by `i18n-lint`'s `wrong-context` | the catalog rows | every task |

**Not built, by decision:** `CreateLink`, `EditLink`, `DeleteLink` (D7-12); `ExtractPages`,
`MergeDocuments` (Q1 ✅, D7-5); `FlattenLayers`, `AutoTag` and the content-rewriting half of
`StructureCommands` (Q2 ✅).

---

## 5. Component design

### 5.1 The library graph after Stage 7

Unchanged in kind. `rawpage_core` gains the page map, the outline generator, the imposition
arithmetic, the header/footer engine, the Bates allocator, `LayerTree`, `TagTree` and the link
actions; `rawpage_ops` gains the copier, the assembler, the reference repair, the outline, layer,
structure, decoration and imposition ops; `rawpage_ui` the panes, tools and dialogs. **No new
library and no new `platform/` interface** — a URL or a file reached by a link is handed over through
`Services::openExternally` (`app/Services.h`), the hook Stage 6 T11 added for attachments, which
`app/CompositionRoot` installs over `QDesktopServices` and a test replaces with a recorder, so nothing
a suite does launches a program. `rawpage_assert_headless()` is
unchanged: the page tree, the map, the generators, the imposition order, the token expansion and
every command's apply and revert are provable with no display and no engine.

### 5.2 The corpus this stage is judged on (T1)

Generated by `corpus/make-assembly.py`, byte-identical on regeneration; every page used by a
reference case **prints its identity** (D7-17), and each fixture writes its ground truth to
`corpus/labels/assembly-<name>.json`.

| Fixture | Carries | Its control |
|---|---|---|
| `assembly/refs-every-kind.pdf` | Twelve identity pages and **every reference kind**: a two-level outline to pages by reference, each item titled with the identity of the page it targets, named destinations in both forms (`/Names /Dests` tree and `/Dests` dictionary), links by `/Dest` and by name, a `GoToR` to itself, `/PageLabels` in three ranges (roman, arabic, `A-` prefixed), a structure tree with `/Pg` on every element, two article threads with beads, an `/OpenAction` | `assembly/refs-none.pdf`: the same pages with no references at all |
| `assembly/refs-by-index.pdf` | A producer's errors a repair must not "fix": an outline item whose `/Dest` names a page by **integer** (legal only in remote destinations), a link to a page outside the document | — |
| `assembly/inherit-resources.pdf` | Pages whose `/Resources`, `/MediaBox`, `/CropBox` and `/Rotate` are **inherited** from three levels of intermediate nodes, two pages sharing one font and one image | `pages-900.pdf`, whose pages carry their own |
| `assembly/annots-on-pages.pdf` | Every page carries annotations — a note with its pop-up, a reply, a link to another page of the file, a widget of a field | — |
| `assembly/merge-a.pdf`, `merge-b.pdf`, `merge-c.pdf` | Three merge sources: each an outline, links, comments, **form fields whose top-level names collide** across sources, layers, different page sizes and a `/Title` | `encrypted/` (an input to be skipped) and a file that is not a PDF |
| `assembly/layers.pdf` | Optional content: nested groups, a radio group, a locked group, print-only and view-only usage, content in two groups on one page, `/Properties` resource entries | — |
| `assembly/tagged-headings.pdf` | A tagged document with `H1`…`H3` and paragraphs, for the outline generator and the tags pane | `assembly/untagged-headings.pdf`: the same text with no structure, for the layout path |
| `assembly/split-blanks.pdf` | Pages with no ink between sections, a page that is white ink on white (blank to the eye, not to the content box), and a recurring heading pattern | — |
| `assembly/boxes.pdf` | Pages with all five boxes set, a CropBox outside the MediaBox, wide white margins around a known content box | `cropbox-margins.pdf` (Stage 4) |
| `assembly/decorated-elsewhere.pdf` | A header and a watermark written by **another producer** (Acrobat's `/Artifact` shapes) — which RawPage must neither list nor remove | — |
| `assembly/odd-count-*.pdf` | 1, 2, 3, 5, 7, 9 and 17 identity pages, for imposition's padding | — |

**Every byte stays ours** (D2-11); appearances use base-14 faces. The signed, encrypted and
existing ordinary fixtures are reused as they are.

### 5.3 One page move, as it will actually run (flow D, made concrete)

```
Alt+↓ in the thumbnails pane ─▶ page.moveLater ─▶ MovePagesCommand{pages 3..5 → after 6}   (D7-1)
  └─ journal.run ─▶ apply: PageTree.move + sink.movePages (intent)
       ├─ PageMap{3→6, 4→7, 5→8, 6→3}  ─▶ tiles, thumbnails, text index, annotation store,
       │                                  hits, current page, panes' selections REMAPPED   (D7-3)
       ├─ Scope: nothing re-rendered — no page's pixels changed
       └─ sink: desired state ─▶ compose /Kids (only what changed since the last show: T4, Q4 ✅)
            └─ render thread, between jobs: FPDF_LoadCustomDocument(original ⧺ update)    (D6-2)
                 └─ engine index == model index; the moved pages' tiles shown from cache
Ctrl+S ─▶ SaveCoordinator (unchanged, flow F) ─▶ the same update, written and verified
```

### 5.4 What the remap must be measured against (T3)

The remap is a permutation; its cost is O(pages) per consumer and **is reported, not assumed**. What
it saves is graded: on `pages-900.pdf`, a ten-page move must leave **every** cached tile of the moved
pages in the cache under its new index and re-render **none** of them — a case counts the engine's
calls (rule 1: the pixels of the moved pages shown, checked against their identity text, and zero
rasterizations of them). The bench (D7-19) grades the product's figure on the guide's §3.1.

### 5.5 Test targets after Stage 7

| Target | Stage 7 suites | Runs where |
|---|---|---|
| `rawpage_tests` | `tst_pagetree` (the full set), `tst_pagemap`, `tst_reference_repair`'s headless half (over `tests/unit/CosBackend.h`), `tst_copier`, `tst_assembler`, `tst_fullwriter` (D7-25), `tst_outline` (hierarchy and the three generators), `tst_imposition`, `tst_headerfooter`, `tst_bates`, `tst_decoration`, `tst_layers`, `tst_tags`, `tst_linkactions`, every command's `[serialize:]` and `[revert:]` | Windows + `portable-asan` |
| `rawpage_render_tests` | `tst_reference_repair` graded by **what the target page shows** (D7-18), `tst_outlinereader` (agreement with PDFium), `tst_remap` (no moved page re-rendered), `tst_mergeengine`, `tst_decorationengine`, `tst_layersengine` | Windows |
| `rawpage_golden_tests` | Decorations, imposition sheets, the table-of-contents page, a PDF-page stamp | Windows |
| `rawpage_ui_tests` | `tst_thumbnailsedit`, `tst_bookmarksedit`, `tst_destinationsedit`, `tst_linktool`, `tst_croptool`, `tst_layerspane`, `tst_tagspane`, the dialogs; the census edits in `tst_keybindings` and `tst_viewercommands` | Windows, offscreen; the system clipboard on `windows` only |
| `rawpage_composition_tests` | `tst_assemblyshown` — a moved, inserted and decorated document **shown is saved** (D6-2's invariant through page commands) | Windows |
| `portable-fuzz` | `fuzz_assembly` (D7-23) | Linux CI |
| `gates.` | the new corpus files in `gates.corpus.*`; `gates.fuzz_count.*` at six; `qpdf --check` over every file the suite writes (`tests/gates/check-qpdf.py`) | `gates` preset and CI |

**A case that writes a file writes it in a scratch directory it made**, and a case that composes the
product points its recovery, session, stamp **and profile** roots at scratch directories
(`CLAUDE.md` → *Traps*).

---

## 6. Task breakdown

Ordering is risk-first. **T2 and T3 come before any surface**: the reference repair and the remap are
where this stage's S1s live, and a no-go on D7-3 changes how every later surface sees a moved page.
**T4 (Q4 ✅) comes before T5** because an insert is the first command whose unsaved update is large.
**T6 comes before T7** because the assembler's documents are written by `FullWriter` (D7-25). **T5 and T7 come before the panes** because the clipboard, the drag between documents, merge,
extract and the PDF-page stamp all stand on the copier and the assembler. **T13 closes Stage
7a** (Q3 ✅).

| # | Task | Depends on | Content | Done when |
|---|---|---|---|---|
| **T1** | **The corpus this stage can be judged on** | — | §5.2's fixtures and labels; `PROVENANCE.md` rows; golden rows | `make-corpus.py --verify`, `--check-provenance`, `--self-test` green; **every existing file byte-unchanged**; each fixture carries what its row claims, read from the bytes; every identity page's identity extracted by PDFium as written; `golden-update --check-manifest` green |
| **T2** | **Moving pages without breaking what points at them** | T1 | `PageTree`'s set; `PageMap`; `PageOps`' insert/remove/move/duplicate; `ReferenceRepair`; `MovePages`, `DeletePages`, `DuplicatePages`; the sink and `verify()` | `tst_reference_repair` green **graded by what each target page shows**, after every command over every fixture with references; deleted-page references cleared and reported, restored by undo; labels kept per page; `[serialize:]`, `[revert:]`, reversibility, `qpdf --check` green; a page command on a signed file leaves OpenSSL verifying all four (D7-20) |
| **T3** | **How a moved page reaches the screen — A2 at Stage 7** | T2 | D7-3's remap through `core/render/`, `DocumentIndex`, `AnnotationStore`, the view state; `showEdits` with a changed page tree; `bench_pipeline --stage7` (D7-19) | **Shown is saved** after every T2 command (0 px, D6-2); **no moved page re-rendered**; the text index keeps every moved page's text; the bench graded on the guide's §3.1 and **recorded** in `Tech-Stack-Plan.md` §3.3 as *A2 at Stage 7*; **a no-go changes D7-3 before T4** |
| **T4** | **The compose stops growing — DW-4's second cause** (Q4 ✅) | T3 | A compose that keeps what it composed (D7-26); the verify off the UI thread (D7-27) | *(Amended 2026-10-31, the user's choice: §3.7's VERDICT is graded at T5 and T11, on its real workload -- inserts from `text-900.pdf`, a bookmark rename -- neither of which exists at T4.)* What a one-page move and a rotation **BUILD is independent of the unsaved update's size** -- counted after 0, 300 and 3,000 unsaved objects, `journal.run`'s times reported beside it (guide A-35); **a compose that keeps what it composed writes what one from nothing writes** (A-128); D6-2's refusal still fires -- a planted unverifiable compose refused after the run returned and taken back, never drawn (A-36) -- and commands, a tile and a save wait for a verify in flight (A-37); `DW-4`'s entry records what T4 built and that its verdict is §3.7's at T5 and T11 |
| **T5** | **Pages from another document** | T3 (T4) | `ObjectGraphCopier` (D7-4); `InsertPages`, `ReplacePages` with D7-6's payload; insert blank pages; `fuzz_assembly` (D7-23) | Every page of every corpus file copied into an empty target **renders identically** to its source (0 px at 72 and 144 dpi); shared objects copied once (counted); inherited attributes materialized; recovery replays an insert with the source deleted; fuzz count asserted at 6; **the guide's §3.7 read for its one-page move after 0, 300 and 3,000 copied objects** (A-35, T4's deferral) |
| **T6** | **Save As that rewrites — `FullWriter`** (D7-25) | T2 | `pdf/cos/FullWriter`: one revision of the reachable graph, numbers kept; Save As and Save a Copy through `SaveCoordinator`; `documents/saveMode`; the signed and encrypted cases | Every editable corpus file Saved As: **one revision, no unreachable object, every page 0 px** against before and every reference graded as the guide's §3.2; numbers and generations unchanged and undo after a Save As exact; deleting 800 of `pages-900.pdf`'s pages and Saving As shrinks it; a signed file asks, and kept signatures verify under OpenSSL; `qpdf --check` clean |
| **T7** | **New documents: extract and split** (Q1 ✅) | T5, T6 | `DocumentAssembler`, written by T6's `FullWriter`; extract to one, per page, and with delete; the five splits; `PageOpsDialogs`' extract and split | Each output **opens with no repair**, its pages render as their sources (0 px) and `qpdf --check` is clean; split-by-size within its bound; no file overwritten; extract-and-delete deletes only after the write verifies |
| **T8** | **The thumbnails pane edits** | T5, T7 | D7-13: sets, drag, cross-document drag, cut/copy/paste, `Delete`, `Alt+↑`/`Alt+↓`, `Ctrl+A`, `Ctrl+Shift+X`, `Ctrl+Shift+D`, `Ctrl+R` on a set; badges; embedded thumbnails; `PageOpsDialogs`' insert, delete, replace, rotate ranges | The whole **Page Thumbnails pane** table **from key events**; a drag of a set is one undo step; pages pasted into a second process; every text-owning widget keeps its own `Ctrl+X`/`Ctrl+V` |
| **T9** | **Boxes, crop, resize, renumber** | T2 | D7-15; `CropTool`; `CropPages`, `ResizePages`, `RenumberPages`, `SetEmbeddedThumbnails`; their dialogs | Boxes written and read back; white margins removed to the **content box** (Stage 4's); a resized page's content scaled and centred, measured on the ink; labels as specified per range |
| **T10** | **Merge** | T7 | D7-16; `MergeDialog` | Bookmarks per file with sources' outlines beneath, landing on the right pages **by what they show**; links between a source's pages kept; fields renamed or merged and reported; layers unioned; skipped inputs reported; `qpdf --check` clean; a 30-way merge inside the guide's bound |
| **T11** | **Bookmarks** | T2 | D7-10, D7-11; `OutlineReader`, `OutlineOps`, `OutlineGenerator`; the six commands; the Bookmarks pane table; `Ctrl+B` in the Viewer; actions, colour, style, open state; the TOC page; import and export | The whole **Bookmarks pane** table **from key events**; agreement with PDFium over every corpus file with an outline; unknown keys survive an edit; generators graded against the labels; a TOC page whose links land by what the pages show; **the guide's §3.7 read for a bookmark rename after 0, 300 and 3,000 copied objects** -- with T5's, its verdict (A-35, T4's deferral) |
| **T12** | **Destinations and links** | T11 | D7-12; the three destination commands; `LinkTool`; `LinkPropertiesDialog`; auto-detect; following; verify | A link made by drag and from text, followed to the page **it shows**; URI and `GoToR` ask first, `Launch` of an executable refused; JavaScript never run (a planted script's side effect absent); verify reports the labels' broken targets and nothing else |
| **T13** | **A custom stamp from a PDF page** — **closes Stage 7a** (Q3 ✅) | T5 | Stage 6's Q7: the page as a form XObject in the stamp library and in a `Stamp`'s `/AP` | The stamp's raster equals the source page's at its scale; placed twice, its graph written once; the library survives two processes; the 7a sign-off checklist (guide §9) |
| **T14** | **Headers, footers, Bates** | T2 | D7-7, D7-14; `DecorationOps`; the four commands; profiles | Apply → remove **byte-identical** to the original; tokens expanded per page against hand values; Bates across three open documents one run with its report; another producer's decoration untouched |
| **T15** | **Watermarks** | T14, T5 | Text, image and PDF-page sources; behind and in front; opacity, rotation, scale, anchor, tile; print-only and screen-only through a layer (D7-9); update; profiles | Golden per variant; apply → remove byte-identical; print-only drawn with the print flag and not without |
| **T16** | **Imposition** | T7 | D7-8 | `tst_imposition`'s tables for every n-up and booklet at counts 1…17; the sheets' rasters show the identities in the planned cells; `qpdf --check` clean |
| **T17** | **Layers** | T2 | D7-9; `LayerTree`, `LayerOps`, `LayersPane`; the commands; `Ctrl+Shift+L` | The tree against the labels; show/hide drawn **and no byte changed**; merge repoints every `/Properties` entry; delete orphans as measured in PDFium and Acrobat |
| **T18** | **Tags** | T2 | D7-10's heading source; `TagTree`, `StructureOps`, `TagsPane`; the three commands; the order overlay; the tree's export (D7-25); `Ctrl+Shift+E` | The tree against the labels; retype, move and language written and read back; the overlay's boxes on the ink of each element's marked content |
| **T19** | **The re-measurement, the census, CI, lockstep** | all | D7-19's second reading through the shipped surfaces; `--list-actions`; §9; §10 | T3's figure re-read and recorded as the product's; the census cases and `--list-actions` agree; `Implementation-Plan.md`'s Stage 7 block reflects reality, deviations included |

---

## 7. Blocking preconditions — setup checklist

| # | Precondition | Why | State |
|---|---|---|---|
| **P-1** | **Adobe Acrobat** — owner: **you** | What another reader does with our bookmarks, links, labels, layers, decorations and new documents (M-02 … M-10); the signature panel after a page command (M-13) | ✅ **Acrobat 26.2.22031** at `C:\Program Files\Adobe\Acrobat DC\Acrobat\Acrobat.exe` (read 2026-10-28) |
| **P-2** | **A reader that is NOT PDFium** — owner: **you** | D7-18's fourth oracle | ✅ **Firefox 158.0.1** (pdf.js) at `C:\Program Files\Mozilla Firefox\` (read 2026-10-28) |
| **P-3** | **A printer that prints DUPLEX on paper, and paper** — owner: **you** | M-04: *"booklet imposition printed duplex on paper and physically folded"* is the roadmap's own manual emphasis and the exit criterion *"imposition output is correct on paper"*. Printed **from Acrobat**: RawPage's printing is Stage 8's | ❌ **OPEN** — `Get-Printer` (2026-10-28) lists Fax, Microsoft Print to PDF, Microsoft XPS Document Writer and OneNote (Desktop), **no physical printer**. Needed by T16; a printer reachable from another machine with Acrobat serves |
| **P-4** | **`qpdf` in CI** — owner: **me** | D7-18's second oracle | ✅ Since Stage 6 T3, in `ci.yml`'s `portable-asan` job (`tests/gates/check-qpdf.py`); Stage 7's written files join its list as each suite writes them |
| **P-5** | **Two monitors at different scales** — owner: **you** | M-01's cross-window drag, M-08's crop | ✅ Stage 4's P-1 rig (150 % and 100 %) |
| **P-6** | **Python 3** on every job that runs the corpus checks | T1's generator | ✅ As Stage 6 |

---

## 8. Verdict ledger — what Stage 7 owes

| Verdict | Produced by | Recorded in |
|---|---|---|
| **A2 at Stage 7** — does the frame budget hold while page commands remap and reload, and does a move reach the pixels promptly? | **T3** (bench) and **T19** (through the shipped thumbnails pane) — D7-19 | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), labelled **Stage 7's workload**, beside Stages 2, 4, 5 and 6 |
| **References survive assembly** — the roadmap's *"repairs all references"* | **T2** onward (`tst_reference_repair` after every page command) and **M-02** (Acrobat follows them) | The guide's §3.2 |
| **Imposition is correct on paper** — the roadmap's exit criterion | **M-04** (P-3) | The guide's §6 |
| **`DW-4`'s second cause** — `journal.run` no longer grows with the unsaved update (Q4 ✅) | **T4 built it; §3.7's verdict at T5 and T11** (the user's choice, 2026-10-31) | `Deferred-Work.md`'s `DW-4` entry, split |

**A stage that is green but has no verdict is not signed off.**

---

## 9. CI changes

| Job | Before | After |
|---|---|---|
| `gates` (ubuntu, no Qt) | 123 cases (run 40626533138) | The new corpus files and labels in `gates.corpus.*`; `gates.fuzz_count.*` at **six** |
| `windows` | 1846 cases (run 40626533138) | Plus every Stage 7 suite; the clipboard cases skip offscreen and are a by-hand run on the `windows` platform, as Stage 5's A-50 is |
| `portable-asan` (ubuntu) | `core/`, `pdf/cos/`, `pdf/ops/`; `qpdf --check` over 71 files | Plus every Stage 7 suite in those layers, read by name from the log; `qpdf --check` over every assembly, split, merge, imposition, Save As rewrite and saved page edit the suite writes |
| `portable-fuzz` | five targets, 60 s each | **Six** — `fuzz_assembly` |
| `fuzz-soak.yml` | five targets, ten minutes | Six |

---

## 10. Lockstep debt this stage creates

**Paid in the same change as this draft:**

- **`Implementation-Plan.md`**: § Stage 7's status line (a draft plan and a guide exist, §0 open, no
  code); §5's guide row and its prose; the list of stage plans.
- **`CLAUDE.md`**: *Where the work is right now* — the next action is your §0 answers, then T1 — and
  the document-set counts (eight stage plans, eight testing guides).
- **`claude-plan/README.md`**: ⑤'s and ⑥'s lists, and Stage 7's row in *Current state*.

**Owed on your §0 answers** — each made only once its answer is given:

- ✅ **Q1, paid 2026-10-29** → `Implementation-Plan.md` § Stage 7: `ExtractPages` and `MergeDocuments`
  struck from *Cross-cutting* with the reason, *extract* struck from `PageCommands`' list, the merge note
  and the *Testing* count reworded, `pdf/ops/DocumentAssembler` added to *Components*, and the status
  line; § Stage 8's `FullWriter` gains a line saying the assembler exists and what it does not do;
  §5's guide row; `Architecture-FileMap.md` gains the assembler's row, ○ (S7), and its census re-counted
  by script; the guide's cases written against Q1 re-read — A-08, A-47 … A-51, §2's untitled-tab row,
  §6's note and §7's caveat stand as written; `CLAUDE.md`'s *Next action* row and `README.md`'s Stage 7
  row.
- ✅ **Q2, paid 2026-10-29** → `Implementation-Plan.md` § Stage 7: the seven capabilities named out of
  *Objectives*, `FlattenLayers` and `AutoTag` struck from *Cross-cutting* with the reason, *Deferrals*
  naming Stage 10, the status line; § Stage 10 gains them in *Objectives* and *Components* beside
  `ContentStreamParser`; §7's §5.5 and §5.6 rows name Stage 10; §5's guide row; the FileMap rows of
  `StructureCommands`, `LayersPane` and `TagsPane` say which half is Stage 10's (no row added or
  re-classed, so the census does not move); the guide's cases written against Q2 re-read — A-08,
  §1's in-scope and out-of-scope rows, §4's layers row — stand as written, A-08's count at **43**;
  `CLAUDE.md`'s *Next action* row and `README.md`'s Stage 7 row.
- ✅ **Q3, paid 2026-10-29** → `Implementation-Plan.md` §3's graph gains 7a and 7b — Stage 8 depending on
  7a, Stage 10 on 7b, whose `LayerTree` and `TagTree` the capabilities Q2 moved there extend; § Stage 7's
  status line; § Stage 8's status, which also records the two §2.5 layer items that need both 7b and Stage
  10's parser, for Stage 8's plan to settle; the guide's §9 applied twice. T13 closes 7a.
- ✅ **D7-25, paid 2026-10-29** (your choice B, outside §0) → `Implementation-Plan.md` § Stage 7's
  *Objectives*, *Components* (`FullWriter`; the assembler's bullet amended), keys (`documents/saveMode`) and
  status line; § Stage 8's status, *Objectives*, `FullWriter` bullet and keys; §7's §2.5 row;
  `Architecture-FileMap.md`'s `FullWriter` row re-tagged and the assembler's amended (no row added or
  re-classed, so the census does not move); the guide gains §3.9, A-122 … A-127 and M-13's Save As steps;
  T6 inserted and T6 … T18 renumbered T7 … T19 in both documents, every other stage's task labels left as
  they were. **Owed with the code:** `core/settings/SettingsKeys.def`'s comment sending `documents/saveMode`
  to Stage 8, corrected when T6 declares the key.
- ✅ **Q4, paid 2026-10-29** → `Deferred-Work.md`'s `DW-4` split: its register row and its section say its
  second cause is *taken by Stage 7, T4* and its first stays unassigned (the row kept, never deleted);
  `Implementation-Plan.md` § Stage 7's status line and §5's guide row; `CLAUDE.md`'s *Owed* row and the
  document-set row re-read and corrected; the guide's §3.7, A-35 … A-37 and their markers re-read — they
  stand, the *struck if declined* clauses removed. **Owed with the code:** at T4, `DW-4`'s second cause
  marked done with the task, and `Tech-Stack-Plan.md` §3.3's *A2 at Stage 6* row pointed at it. **Paid at T4,
  2026-10-31, as the user then decided:** `DW-4`'s entry records what T4 built and that §3.7's verdict is read at
  T5 and T11 -- so the second cause is BUILT, not yet marked done -- and §3.3's row points at it; the closing half
  is owed at T11, with §3.7's verdict.
- ✅ **Q5, paid 2026-10-29** → `Implementation-Plan.md` § Stage 8 gains (b), § Stage 12 (a) and (d), § Stage
  14 (c), each in *Objectives* with the reason; § Stage 7's *Deferrals* lists all four and its status line
  says §0 is answered; §7's §5.1–5.2, §5.3, §5.7 and §5.9 rows; `Deferred-Work.md`'s `DW-3` widened to the
  text Stage 7 draws (e), in its register row and its section; the guide's cases and rows written against Q5
  re-read and standing as written (§1's five out-of-scope rows, A-80, A-96, §6's note); `CLAUDE.md`'s *Next
  action* and document-set rows, `README.md`.

**Owed with the code:**

- Every ⚠️ row in §4, with its file.
- **Flow D** (`Architecture-FileMap.md` §5) gains the page intents and the page map at T2; **flow F**
  the copied-graph payload at T5.
- `Tech-Stack-Plan.md` §3.3: *A2 at Stage 7* at T3 and T19.
- `Stage3-Mutation-Plan.md` D3-15's rotation delta, and `core/backend.h`'s comment on `pages()`
  (*"read once at open and never changes"*), corrected at T2 when they stop being true.
- `ui/panes/ThumbnailsPane.h`'s and `BookmarksPane.h`'s *"Stage 7's"* comments, corrected as each
  becomes this stage's work done.
- **`Shipped-Surface.md`** §1 and §2, and `CLAUDE.md`'s status table, re-derived at T19 (and at T13,
  7a's close — Q3 ✅) from the product: the command count, the unregistered count, the fuzz target count, the
  waiver census — each re-measured, none predicted here.

---

## 11. Explicitly out of scope

| Not in Stage 7 | Arrives at |
|---|---|
| **Batch sequences** (`Features.md` §5.11) | Stage 14 |
| **Bates across documents that are not open** (Q5(c) ✅) | Stage 14 |
| **Non-PDF inputs to a merge** (Q5(a) ✅), and pages from non-RawPage clipboard content (Q5(d) ✅) | Stage 12 |
| **Imposition at print time** (Q5(b) ✅); **printing** at all | Stage 8 |
| **Flattening annotations into content** | Stage 8 |
| **Dense renumbering, object streams, a target PDF version, linearization, conformance, and every export to another format** — Save As rewrites and compacts from T6 (D7-25) | Stage 8 |
| **Flattening layers, deleting a layer's content, assigning content to a layer, auto-tagging, marking an artifact, a language on a run** (Q2 ✅) | Stage 10 |
| **Inserting pages from a scanner** | Stage 16 |
| **Portfolios** | Stage 16 |
| **Editing an encrypted document**, or using one as a source (D7-21) | Stage 10 |
| **Validating a signature** after a page command — RawPage warns, Acrobat judges (D7-20) | Stage 11 |
| **Running a link's or bookmark's JavaScript, form or media action** — written, never executed (D7-12) | not planned |
| **Drawn text outside WinAnsi and the four CJK collections** (Q5(e) ✅) | `DW-3` |
| **A structure tree for copied pages** — they arrive untagged (D7-4) | not planned |
| **DW-4's first cause** — a tile of a page with hundreds of annotations (Q4 ✅ took only the second) | `DW-4` |
| **Substitute page colours** — Stage 4's I-07 | `DW-1`, untouched |

---

## 12. Risks inside Stage 7

| Risk | Shape | Mitigation |
|---|---|---|
| **A move breaks a bookmark, and the suite agrees** | The repair graded by the page map it computed | D7-17 and D7-18: every reference graded by the identity the target page SHOWS |
| **The screen shows another page's pixels or text after a move** | An index-keyed cache not told | D7-3's map through every consumer, each with a case; `WholeDocument` as the fallback, never a stale index |
| **A copied page draws without its fonts** | Inherited `/Resources` left on the old parent | D7-4's materialization; `inherit-resources.pdf`; every copied page compared by raster with its source |
| **A copy duplicates a shared font a thousand times, or drags in the whole source** | A copier that follows `/Parent`, or copies per page | D7-4: one call, one visited set, `/Parent` never followed; the copy counted |
| **A decoration cannot be removed exactly** | Content mixed into the page's own stream | D7-7: a stream of its own; apply → remove byte-identical |
| **A merge silently loses a form field's value** | Colliding names merged by accident | D7-16's explicit policy, reported either way |
| **A Save As loses something, or breaks a signature** | A rewrite of what the writer thinks is reachable | D7-25: numbers kept, every page and every reference graded after each rewrite, verify before replace; a signed file asks |
| **A new document opens in RawPage and nowhere else** | Our writer agreeing with our reader | `qpdf --check`, Acrobat and Firefox (D7-18) |
| **A large insert makes every later edit slow** | `DW-4`'s second cause, multiplied (Q4 ✅) | T4 before T5, measured at 0, 300 and 3,000 unsaved objects |
| **A link opens something from someone else's document** | `Launch`, `URI`, JavaScript | D7-12: asked, refused or never executed, by D6-17's list |
| **A malicious page tree, outline or optional-content dictionary hangs a reader** | Cycles, depth, size | Bounds by key; D7-23's fuzz target; ASan over the copier and the readers |
| **Imposition is right on screen and wrong folded** | A creep or binding sign error invisible in a raster | `tst_imposition`'s hand tables, and paper (M-04, **P-3 open**) |

---

## 13. Definition of Done for Stage 7

[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions),
made concrete:

1. §0 answered, and T1…T19 complete; the
   [exit criteria](./Implementation-Plan.md#stage-7--document-assembly) met, as amended by the answers.
2. `cmake --build --preset x64-release --target checks` clean — four gates, **`mutation-lint` naming
   exactly §4's commands**, `layer-lint` at **15** escapes.
3. `ctest` green on Windows across all six targets and the `gates` preset, **on both presets**, every
   Stage 0–6 case present **by name**.
4. `portable-asan` green including every Stage 7 suite in `core/`, `pdf/cos/` and `pdf/ops/`, read from
   the job's **log**; `portable-fuzz` and the soak clean on **six** targets; `qpdf --check` clean in
   the `portable-asan` job's log.
5. **Golden suite green with every existing baseline unchanged** — the new baselines are T1's files
   and the decoration, imposition, TOC and stamp cases.
6. **A2 at Stage 7** recorded, bench and product; `DW-4`'s second cause recorded as taken and done (Q4 ✅).
7. [`Stage7-Assembly-Testing-Guide.md`](./Stage7-Assembly-Testing-Guide.md) — written from this
   plan before the code, its automated half run green by me with **actual** output, its manual half
   walked and signed off **by you**. I do not mark a manual case.
8. No open S1 or S2 in the guide's issue log.
9. `Architecture-FileMap.md` rows flipped and §10's debt paid, in the same changes as the code.
10. `Implementation-Plan.md`'s Stage 7 status block updated to reality, deviations included.
