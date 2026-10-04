# Stage 6 — Annotations and Review · Execution Plan

> **What this document owns.** The **execution order** of Stage 6 and the **Stage-6-local design
> choices** that no other document decides. It owns no product facts. Every stack, layering,
> key-binding and verification fact is cross-referenced to its owner and never restated as a new
> decision:
>
> | For… | Read |
> |---|---|
> | The Stage 6 objectives, components, key bindings handled, settings keys, exit criteria and deferrals | [`Implementation-Plan.md` § Stage 6](./Implementation-Plan.md#stage-6--annotations-and-review) |
> | The layering rule, mutation-through-commands, risks **A2** and **A4** | [`Tech-Stack-Plan.md` §3.3, §4.2, §5](./Tech-Stack-Plan.md) |
> | What each file is for, and flows **D** (a mutation), **E** (adding a highlight) and **F** (save and recovery) | [`Architecture-FileMap.md` §3, §5](./Architecture-FileMap.md) |
> | The authoritative key bindings — the whole **Annotation tools** and **Comments pane** tables, the Viewer rows `Ctrl+E`, `Ctrl+.`/`Ctrl+,`, the Global rows `Ctrl+Shift+C`, `Ctrl+Shift+A`, and the Common rows `Ctrl+X`, `Ctrl+V` | [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference); the capabilities in §4.1 and §4.2 |
> | How Stage 6 is verified | [`Stage6-Annotations-Testing-Guide.md`](./Stage6-Annotations-Testing-Guide.md) — written from this plan, **before** the code |
>
> **Status:** ⚪ **Written 2026-10-04, the day Stage 5 was signed off, as a draft; §0 ANSWERED the
> same day, all eight — so this is now the plan.** No Stage 6 code exists. Q1 … Q6 and Q8 were
> approved as proposed; Q7 was answered by moving the custom stamp from a PDF page, with its copier,
> to Stage 7. **Q1 carried one consequence the proposal had not named**: `Ctrl+Alt+M` in the Viewer
> collides with Write mode's margin comment, so `Features.md` §8's *twelve deliberate collisions*
> became **thirteen** — put to the user before the table was edited, and accepted (D6-9). §0 is kept
> as the record of what was asked. This file is a plan, not a
> progress log: Stage 6's live status, task by task and deviation by deviation, will be the **Stage 6
> status block in [`Implementation-Plan.md`](./Implementation-Plan.md#stage-6--annotations-and-review)**,
> which is the authority.
>
> **Label scoping, because five namespaces now collide across stages.** `T1`…`T15` here are
> **Stage 6** tasks and are unrelated to any earlier stage's; this document writes "Stage 6 T4"
> wherever the stage is not already obvious. `D6-n` are this stage's decisions, distinct from
> `Tech-Stack-Plan.md`'s `D1`…`D10` and from `D2-n` … `D5-n`. `A2` and `A4` are **risks**
> (`Tech-Stack-Plan.md` §3.3); `A-nn` are **test cases**
> (`Stage6-Annotations-Testing-Guide.md` §5). A bare `I-nn` in this file is **Stage 5's**
> (`Stage5-TextSearch-Testing-Guide.md` §8) unless it says otherwise.

---

## 0. Decisions this draft needed from you before T1 — all answered 2026-10-04

Each is argued in §3. None can be settled from the documents alone: each moves a `Features.md` §8
binding, a capability's stage, a dependency or a component between stages. A later stage would
inherit whatever this plan wrote down, so it is asked now rather than decided here.

| # | The question | This draft proposes | Why it is yours |
|---|---|---|---|
| **Q1** ✅ | **The fourteen tool chords cannot be pressed.** `Ctrl+Alt+H` and its thirteen siblings are bound only in the **Annotation tools** context, which `core/cmd/KeyContext.h` defines as *"an annotation tool is armed or an annotation is selected"* — so from the page, with no tool armed, every one of them falls through Viewer → Common → Global and does nothing. Stage 5's `Ctrl+Shift+F` was the same defect (D5-15) | **Move the fourteen arming rows to §8's Viewer table**, keep the in-tool rows (`K`, `Enter`, `Esc`, `Delete`, `Ctrl+E`, `Ctrl+Alt+Enter`, the drags, the arrows) in Annotation tools, and **make Annotation tools resolve through Viewer** — a context chain, so zoom and scroll still work with a tool armed (**D6-9**) | §8 is the authority on bindings, and the change touches `KeyBindingStore`'s resolution model |
| **Q2** ✅ | **Typewriter text.** `Implementation-Plan.md` lists `ContentCommands` (*"typewriter text only this stage"*) and an `AddTypewriterText` command, which writes the text **into the page content**. Acrobat's typewriter is a `FreeText` **annotation** with `/IT /FreeTextTypeWriter` | **An annotation**, `/IT /FreeTextTypeWriter`: editable, movable, undoable and listed in the Comments pane like any comment; **`ContentCommands` moves wholly to Stage 13** and `AddTypewriterText` is not built — **eleven commands, not twelve** (**D6-8**) | Moves a component between stages and removes a command the roadmap lists |
| **Q3** ✅ | **Which characters a typed comment can show.** A free-text, callout, typewriter or dynamic-stamp appearance needs a font in its `/AP`. Embedding a font needs subsetting, which is Stage 8's (`OptimizeOps`) or Stage 13's (HarfBuzz) | **Base-14 Helvetica (WinAnsi) for Latin, and the four predefined Adobe CJK CID fonts, NON-embedded, for Chinese, Japanese and Korean** (Stage 5's `cjk-readable.pdf` measured PDFium drawing that path with the OS face). **Any other character is refused at input, by name**, and the gap is registered as **`DW-3`** (**D6-7**) | Leaves a capability — Cyrillic, Greek, Hebrew, Arabic, Thai in a typed comment — with no owner |
| **Q4** ✅ | **Comment summaries and printing with comments** (`Features.md` §4.2) write a **new** PDF in four layouts — three of which place the document's pages beside the comments — or print one. That needs a new-document writer and page import, which are Stage 8's (`FullWriter`) and Stage 7's | **Move both to Stage 8**, as Stage 5's Q4 moved the comparison report (D5-17): `CommentSummaryDialog`, `comment.summarize`, and the keys `commenting/summaryLayout` and `commenting/printComments` with them (**D6-19**) | Moves a capability, a dialog, two keys and part of an exit criterion to a later stage |
| **Q5** ✅ | **Audio comments** need audio capture and playback. The pinned Qt install carries **Qt Multimedia** (LGPL-3.0) with two Windows backends: Media Foundation (`windowsmediaplugin.dll`) and FFmpeg (`ffmpegmediaplugin.dll` plus `avcodec-61`, `avformat-61` and three more DLLs) | **Qt Multimedia, Media Foundation backend ONLY** — the FFmpeg plugin and its DLLs never deployed; `Sound` annotations carry raw PCM (ISO 32000-1 12.5.6.16), recorded or taken from a WAV file; **its task is last and slips alone**, as the roadmap already says (**D6-18**) | Adds a Qt module to a locked stack (D4's licence posture) and a new deployment surface |
| **Q6** ✅ | **Spell-checking comment text** (`commenting/spellCheckComments`) needs a spelling engine and dictionaries — Hunspell and the `spelling/*` keys are **Stage 13's** | **Move it to Stage 13**, the key with it (**D6-13**) | Moves a capability and a key |
| **Q7** ✅ | **A custom stamp made from a PDF page** has to carry that page into the annotated document as a vector form: its content and **every object it reaches** (fonts, images, colour spaces) copied across documents. That copier is Stage 7's merge machinery, and Stage 7 has not started | **Build `pdf/ops/ObjectGraphCopier` here**, its first consumer the stamp; Stage 7's merge is its second, and Stage 7's plan inherits it (**D6-16**). **Answered 2026-10-04: NO — the page source and the copier move to STAGE 7.** Stage 6's custom stamps come from images; `Implementation-Plan.md` § Stage 7 now names both | Moves a component into this stage from one that may run in parallel |
| **Q8** ✅ | **An independent structural check of what Stage 6 writes.** Every annotation dictionary and appearance stream is new first-party output. Stage 3's precedent is OpenSSL as an independent validator (D3-2) | **`qpdf --check` (Apache-2.0) in CI's `gates` job** over every file the product writes in the suite — a verification tool, never linked or shipped, like OpenSSL (**D6-6**) | A new verification dependency in CI, as D3-2 was |

**Answered 2026-10-04: Q1, Q2, Q3, Q4 and Q5 approved as proposed; Q7 answered by moving the page
stamp and its copier to Stage 7.** Q1's consequence — a thirteenth Write-mode collision — was put to the
user before the binding table was edited, and accepted (D6-9). **Q6 and Q8 were approved later the
same day.** Each answer's
edits are made, and §10 records where.

**Not asked, because the documents already decide it:** encrypted documents cannot be annotated
until Stage 10 (D2-6, D3-7); applying redaction is Stage 10; shared review is Stage 16; links are
Stage 7 (`annot.linkTool`); the AI assistant is Stage 16.

---

## 1. What Stage 6 is actually for

Stages 2 to 5 made a document something a person can see, move around in and read. **Stage 6 is the
first stage at which a person changes a document in a way they meant to change it** — Stage 3's
rotation was the mutation path's proof, not a reason anyone opens an editor. It opens milestone
**M2** (`Implementation-Plan.md` §6), and everything it writes is read by other people's software.

Four things get permanently harder if this stage gets them wrong.

1. **Every annotation RawPage writes is read by a reader RawPage has never met.** A highlight that
   looks right here and wrong in Acrobat is a defect the user cannot see and their reviewer can. The
   roadmap's answer is that **we generate every `/AP` ourselves** (`Implementation-Plan.md` §
   Stage 6). That answer is only as good as the oracle that checks it, and **PDFium cannot be that
   oracle**: Chrome and Edge are PDFium too, so *"it looks the same in Chrome"* proves PDFium agrees
   with itself. → **D6-6**, **D6-26**.
2. **Stage 3's display rule ends here.** D3-15 kept the engine read-only by drawing an unsaved
   rotation through a render parameter, and named this stage as its revisit: *"the first mutation a
   render parameter cannot express (an annotation's appearance)"*. How an unsaved highlight reaches
   the pixels decides the cost of every edit, the truth of what the screen shows, and what undo
   costs. → **D6-2**, measured at **T4** before any tool is built.
3. **Identity has to survive undo, save, recovery and other readers.** A command names the annotation
   it changes. That name must mean the same annotation after a save renumbers nothing, after a crash
   replays the journal against the file as opened, and after an XFDF round trip through Acrobat.
   → **D6-4**.
4. **The model must not lose what it does not model.** Real annotations carry keys RawPage will never
   understand. An edit that rewrites a dictionary from the model alone deletes them — silently, in
   someone else's document. → **D6-5**.

**What Stage 5 should change about how this stage is built.** Stage 5 raised twenty-four issues, and
**thirteen of them (I-12 … I-24) came after its last task was done** — from a walk, from the user,
from the nightly soak, from re-reading a bench. Its lessons that apply here with more force:

- **The oracle for geometry is the rendered ink** (Stage 4's I-10, Stage 5's rule 6). A highlight's
  quadpoints checked against the char boxes they were computed from agree when both are wrong.
- **The oracle for content is the generator** (Stage 5's rule 7). The corpus generator knows every
  annotation it wrote — subtype, rectangle, author, thread, state — so the reader is graded against
  it, never against PDFium's own `FPDFAnnot_*` getters.
- **Walk early.** Each manual case is scheduled at the task that makes it walkable (guide §6).
- **A decision made during a walk gets a `D6-n` row the day it is made.**
- **Both presets, and the UI binary whole** (Stage 4's I-09 and I-28).

---

## 2. Inherited ledger — what Stage 5 hands over

Each figure was **re-measured on 2026-10-04** unless it says otherwise.

| Fact | State at the end of Stage 5 | Consequence for Stage 6 |
|---|---|---|
| The suite | **1422/1422** on `x64-release` and `x64-debug`, four named skips (a figure from the named runs of 2026-10-04, `CLAUDE.md`); CI run **37188585103** on `10d049d`, all five jobs green | Stage 6 adds to a green suite |
| `mutation-lint` | *"clean -- 1 command(s): RotatePageCommand; 1 table row(s); 165 test file(s) scanned"* | **D6-1**: it names exactly §4's commands at T15 — **eleven**, Q2 approved 2026-10-04 |
| `core/backend.h`'s `MutationSink` | **One method**, `setPageRotation(int, int)`; `core/doc/Permissions`' `Operation` has **one value**, `EditPages` | T3 adds the annotation intents and `Operation::Annotate` (D6-3) |
| `pdf/ops/MutationSinkImpl` | Desired state, not a log (D3-5): per-page rotation compared against the newest revision on disk; `IncrementalWriter::append` **returns the update** rather than writing | D6-2's display path composes from exactly this |
| `layer-lint` | *"clean -- 507 files, 4642 includes"*, **15** live escapes | **D6-17, D6-20, D6-23**: Stage 6 plans **no** new waiver — every file it writes is written by `pdf/` or by `RecoveryJournal`'s existing write |
| `ActionRegistry` | **374 declared, 111 registered, 263 unregistered** (`--list-actions`). Declared, and registered by nothing: the 30 `annot.*`, 12 `comment.*`, 5 `redact.*`, `nav.nextComment`/`prevComment`, `find.inComments`, `pane.comments`, `pane.attachments`, `view.togglePropertiesBar`, `edit.cut`, `edit.paste` | Stage 6 registers handlers, and mints only what a surface needs and §8 does not name (§4); the census is re-measured at T15, not predicted |
| `core/cmd/KeyBindingDefaults.def` | The fourteen arming chords were `AnnotationTools` rows; their ids' home context is `Viewer` | **Q1, answered 2026-10-04: moved to Viewer** with §8, before any Stage 6 code; the context chain itself is T5's |
| `core/cmd/KeyBindingDefaults.def`, `Esc` | `(AnnotationTools, Esc, EditCancel)` and `(Common, Esc, EditCancel)` are one id, and **`edit.cancel` is unregistered on purpose** (Stage 4's I-06, `ui/shell/MainWindow.cpp`) | **D6-9**: Stage 6 does not register it either |
| `core/settings/SettingsKeys.def` | **69 rows**; the `Identity`, `Commenting` and `Measuring` groups exist in `SettingsGroup` with no keys; **9 of 26** Preferences pages ship (`PreferencesDialog::shippedPages()`) | T6, T8, T12 — **D6-13** |
| `core/Snap.h` | Precedence object edge > guide > grid, one threshold in device pixels; `SnapEdge` exists and **the product passes it an empty list** (D4-6); `units/snapToObjects` absent (D4-5) | **D6-14**: point targets, path geometry, and the key |
| `core/text/SearchUnit` + `IDocumentBackend::searchUnits` | Comments, fields, bookmarks and attachments read through PDFium for search, *"until those models exist"* (D5-12) | **D6-11**: comments in an open document come from the store |
| `pdf/ops/EmbeddedFileReader` | An embedded file's `/Desc`, first-party (Stage 5 T8) | The Attachments pane's reader (D6-17) |
| `res/ribbon.json` | 25 annotation, comment and redaction controls already placed, every one naming an unregistered id | Registered as their handlers arrive |
| PDFium pin 153.0.8009.0 | `fpdf_annot.h` carries 66 `FPDF_EXPORT` declarations, edit calls among them (`FPDFPage_CreateAnnot`, `FPDFAnnot_SetAP`, `FPDFPage_RemoveAnnot`); the engine loads a document with `FPDF_LoadMemDocument64` | **D6-2** — and `FPDF_LoadCustomDocument`, which reads without a copy |
| `corpus/` | **97 PDFs**, 92 golden cases and 5 excluded (`golden-update --check-manifest`). One annotation fixture: `ordinary/annots-mixed.pdf`, nine types with appearances. **None** has threads, state replies, a Caret, Ink, Polygon, Stamp, Sound, Redact, a measurement, a file-attachment annotation, an annotation without `/AP`, a locked flag, or a direct annotation dictionary | **T1** |
| `i18n-lint` | Families `action shell ribbon prefs error command panes search compare` | Stage 6 adds **`annot comments measure stamps`** — one per new namespace (Stage 3's I-16) |
| The rig | **Acrobat 26.2.21931** at `C:\Program Files\Adobe\Acrobat DC\` (Stage 3's P-1 read 26.001.21771); **Firefox 157** (pdf.js — an engine that is not PDFium); Chrome 154 and Edge 154 (**PDFium**, not independent); a headset microphone; **no `qpdf`** on this machine | §7 |
| Open issues carried | Stage 4's **I-07** (`DW-1`); Stage 2's I-06, I-11 (open by decision) | None is Stage 6's. Stage 6 starts with no open S1 or S2 |

---

## 3. Stage-6-local decisions

Choices this plan makes because nothing upstream decides them. **Rows marked ✅ carry a §0
question**, and every one was answered on 2026-10-04 (§0); each row reads as its answer left it.

| # | Decision | Why | Revisit |
|---|---|---|---|
| **D6-1** | **Every document change in Stage 6 is a command over a SET of annotations, and the set of commands is closed.** §4 lists them; `mutation-lint` names exactly those at T15. One command may add, edit, move or delete many annotations — *duplicate onto all pages*, *delete everything matching the filter*, an import, an alignment, a migration — and is **one undo step**. Each ships with its `Commands.def` row, a `[serialize:Class]` case, a `[revert:Class]` case, and a `tst_reversibility` sweep over every corpus file it applies to. **Nothing else changes the document**: opening a pop-up, selecting, filtering, numbering and sorting are view state, and the store is never mutated except by a command's `apply`/`revert`. | D3-1's journal is the only path, and Stage 5 proved a number that must not move survives fourteen tasks. A command per annotation would make *duplicate onto 200 pages* 200 undo steps. | Stage 7 (page commands), Stage 9 (field commands) |
| **D6-2** | **An unsaved annotation is drawn by PDFium rendering the bytes Save would write — the engine RELOADS the composed revision.** After each command the sink composes the update it already knows how to compose (D3-5), and the render thread opens **the original bytes followed by that update** through `FPDF_LoadCustomDocument` — read through a callback over the two buffers, **so the original is never copied** — and swaps it in **between jobs**, keeping the `DocumentHandle`. Tiles of pages outside the command's `Scope` stay cached; the page tree, sizes and labels are not re-read (no annotation command changes them). **The invariant this buys, and the case that holds it: what the screen shows IS what Save writes** — a live view and the saved file reopened rasterize to **0 differing pixels**. During a drag the overlay draws a Qt preview, held until the new tiles arrive, so nothing flashes. **This is D3-15's revisit, decided by measurement at T4** (*A2 at Stage 6*, guide §3.1). **The alternative, if T4's figure fails:** mirror each command into PDFium's in-memory document with its edit API (`FPDFPage_CreateAnnot`, `FPDFAnnot_SetAP`) — cheaper per edit, but it makes PDFium's model the second unsaved copy D3-15 refused, it cannot carry an image `/AP` through `FPDFAnnot_SetAP`, and the invariant would then be a claim to test rather than a construction. | D3-15's objection to mutating the engine was that *"nothing journals and nothing can revert"* PDFium's copy. Reloading from the sink's own output has no second copy at all: the journal drives the sink, the sink drives the bytes, the engine draws the bytes. **A render parameter cannot express an annotation**, so D3-15's own mechanism ends here. Not copying the original matters: `pages-900.pdf` and Stage 3's 200 MB bench file would otherwise cost a full copy per keystroke-sized edit. **What it costs, to be measured rather than assumed:** an xref parse per command, and a page re-parse when a page outside the scope is next rendered. | T4's verdict. A no-go chooses the mirror, a new decision recorded before T5 |
| **D6-3** | **One annotation model, `core/doc/AnnotationStore`, hydrated FIRST-PARTY by `pdf/ops/AnnotationReader`; PDFium stays the renderer.** Editing needs each annotation's object identity and its whole dictionary, which PDFium's public API does not give. **The two readers are held to agree, per page** — count, subtype and rectangle (within the engine's `float` tolerance) — as `core/doc/Permissions` holds them to agree on pages (D3-7): **a page where they disagree is read-only**, refused by key, and says why. `Permissions` gains **`Operation::Annotate`**. **An encrypted document is refused** (the first-party reader declines it, D2-6) — its comments are neither listed nor added until Stage 10. Annotations whose page the reader cannot place (no `/P`, listed by no page) are reported and ignored. | Two readers of one fact must agree or say so, which is D4-3's rule in the form D3-7 already built. Writing through our own code means reading the object we will rewrite through our own code. | Stage 10 (crypt) |
| **D6-4** | **An annotation's identity is its object number when it was read from the file, and a generated `/NM` when RawPage created it.** Every annotation RawPage writes carries **`/NM`** (a 128-bit random identifier, hex, generated once and serialized with the command), **`/M`**, **`/CreationDate`**, **`/T`** (the identity, D6-13) and **`/P`**. A **direct** annotation dictionary in `/Annots` is identified by (page, position) and becomes indirect when first edited. The recovery journal replays against the file as opened, so a command never names an object number that a compose allocated (`ObjectAllocator` numbers are assigned at compose and differ between composes). | `/NM` is the key XFDF and FDF merge on (ISO 32000-1's *Forms Data Format*), and Acrobat writes one on every annotation; an identity that survived undo and recovery but not an exchange would be half an identity. | — |
| **D6-5** | **An edit changes what it models and preserves everything else.** `pdf/ops/AnnotationOps` starts from the annotation's **dictionary on disk** and overwrites only the keys the model owns; every other key — `/RC` aside — survives byte-for-byte in value. **`/RC` (rich text) is rewritten** as a plain XHTML body when `/Contents` changes, because Acrobat prefers `/RC` and a stale one would show the old text. **`/AP` is regenerated** for an annotation whose appearance-relevant fields changed, and **left untouched** otherwise — an annotation RawPage only reads keeps its producer's appearance. | Real annotations carry keys no editor understands. Rewriting from the model would delete them in someone else's document, and the suite would agree with itself (rule 7 applied to writes: the case compares the unmodelled keys before and after). | — |
| **D6-6** | ✅ *(Q8, approved by the user 2026-10-04)* **We generate every `/AP` normal appearance ourselves** (`pdf/ops/AppearanceStreamBuilder` over `pdf/cos/ContentStreamBuilder`), for every type Stage 6 creates or edits; rollover and down appearances are not generated (no Stage 6 type has one; widgets are Stage 9's). **Four oracles, none of them our builder:** (1) **golden rasters** per type and property (`tst_appearance`), captured from a declarative spec and LOOKED AT before they are kept; (2) **PDFium's own appearance generation** as a tolerance control for the types it can generate (highlight, underline, strikeout, squiggly, square, circle, line, ink, note) — our appearance and PDFium's must overlap at ink IoU ≥ 0.9; (3) **`qpdf --check`** in CI's `gates` job over every file the suite writes (Q8); (4) **Acrobat and Firefox (pdf.js)**, by a person (M-02). **Chrome and Edge are PDFium and are not an oracle.** Every generated stream is re-read by `pdf/cos/` with an operator allowlist and balanced `q`/`Q` and `BT`/`ET`. | The roadmap names this the stage's biggest interoperability decision. An appearance checked only by the renderer that will draw it in RawPage proves nothing about Acrobat. | Stage 9 (widget appearances) |
| **D6-7** | ✅ *(Q3, approved by the user 2026-10-04)* **Typed text in an appearance uses base-14 Helvetica with WinAnsi encoding for Latin, and the predefined Adobe CID fonts, NOT embedded, for CJK**: `STSong-Light`/`UniGB-UCS2-H` (Simplified Chinese), `MSung-Light`/`UniCNS-UCS2-H` (Traditional), `KozMinPro-Regular`/`UniJIS-UCS2-H` (Japanese), `HYSMyeongJo-Medium`/`UniKS-UCS2-H` (Korean), chosen per character run by script — **the four names are measured at T7** against what PDFium and Acrobat draw, as Stage 5 T1 measured `STSong-Light`. **A character in neither set is refused at input** — the editor says which character and why — and **`DW-3`** registers the gap. `/DA` names the same font the `/AP` uses. | Embedding a font needs subsetting (Stage 8's `OptimizeOps`, Stage 13's HarfBuzz) or a whole system font per document — tens of megabytes for CJK. A non-embedded CID font is what Stage 5 T1 measured PDFium drawing with the machine's face (`STSong -> SimSun`), and what Acrobat draws with its Asian font support. Refusing beats a box of `?`: *text that is not there* was Stage 5's S1. | `DW-3`; Stage 13 (subsetting) |
| **D6-8** | ✅ *(Q2, approved by the user 2026-10-04)* **Typewriter text is a `FreeText` annotation with `/IT /FreeTextTypeWriter`**, no border and no fill, created by `annot.typewriter` (minted, §4). **`core/cmd/commands/ContentCommands` is not built at Stage 6** and its FileMap row is re-tagged to Stage 13 alone; `AddTypewriterText` is not built. | Acrobat's typewriter is this annotation, and every other reader shows it. Content-stream text could not be edited again until Stage 13 can parse content, and `ContentStreamParser` is Stage 10's (D5-19). | Stage 13 |
| **D6-9** | ✅ *(Q1, approved by the user 2026-10-04)* **The arming chords live in the Viewer table; Annotation tools is a context that falls back to Viewer.** §8 and `KeyBindingDefaults.def` move the fourteen `Ctrl+Alt+` arming rows to Viewer in one change (`tst_keybindings` compares them row for row). `KeyBindingStore` resolves **Annotation tools → Viewer → Common → Global** — the first context chain; every other context keeps its single step. **`Esc` with an annotation in progress is handled by `PageView`, as a drag's `Esc` already is**: `edit.cancel` stays unregistered (Stage 4's I-06), so the tool finder, the find bar and a text editor keep their `Esc`. **The arrows** are a `RAWPAGE_BINDING_NATIVE` row in Annotation tools — the overlay owns them while an annotation is selected — and **a burst of nudges is ONE `MoveAnnotation`**, committed when no arrow has been pressed for 500 ms or the focus leaves. **`Ctrl+E` in both contexts opens one properties bar** (`ui/viewer/PropertiesBar`, ⚠️), showing the armed tool's defaults or the selection's properties. `Space` in the Comments pane is the checkmark, Viewer's `Space` scrolls, and the pane wins only while focused (§8's three-way collision, unchanged). **The consequence, decided by the user on 2026-10-04:** `Ctrl+Alt+M` in the Viewer (a sticky note) collides with Write mode's `Ctrl+Alt+M` (a margin comment), so §8's *twelve deliberate collisions* are **thirteen** — nine over Viewer, four over Global. Offered with leaving the note's chord in Annotation tools (unreachable from the page) or choosing it a new chord, **the user accepted thirteen**; `tst_keybindings`' census, §8's table and the live statements of the figure moved with it. | A chord that cannot be pressed from where a person is when they want it is Stage 5's D5-15 again. Without the chain, arming a tool would take zoom, scroll and every other Viewer chord away. A direction is an argument, and dispatch carries none (O11). | Stage 14 (O11) |
| **D6-10** | **An annotation command's `Scope` says ANNOTATIONS of these pages.** `core/doc/Scope` gains an aspect — content or annotations — so the tile cache drops the pages' tiles while **`DocumentIndex` keeps their text** and drops only their annotation search units. A rotation's scope (content) behaves as before. | Precise invalidation is one of the three things mutation-through-commands buys (`CLAUDE.md`). Without the aspect, every highlight on page 450 of `text-900.pdf` would re-extract the page's text, which an annotation cannot change. | Stage 7 (page commands), Stage 13 (content) |
| **D6-11** | **Comments in an OPEN document are searched from the store** — so an unsaved comment is found — and D5-12's `searchUnits` keeps its other three classes and every file Advanced Search opens that is not open in a tab. The search unit for a comment carries its `AnnotationId`, and a hit selects the annotation as well as revealing it. **A search hit in an attachment now selects it in the Attachments pane** (D5-12's *"opens nothing"* ends). | D5-12: *"when those models exist they become the source and the reader is theirs to fold, which their stages decide."* Fields are Stage 9's to fold. | Stage 9 (fields) |
| **D6-12** | **Threads, status and checkmarks are ISO 32000-1 §12.5.6.3's, not a private model.** A reply is an annotation with `/IRT` and `/RT /R`; a **group** (a replace edit's strikeout and caret) is `/IRT` with `/RT /Group`, shown as one item; a **review status** is a hidden `Text` reply with `/StateModel /Review` and `/State` (Accepted, Rejected, Cancelled, Completed, None); the **status history** is that list of replies with author and date; a **checkmark** is `/StateModel /Marked` by **the current identity**, so it is personal as `Features.md` §4.2 says. Group, sort, filter and search are `AnnotationStore::query`, **headless and tested in `rawpage_tests`**; the pane is a view of it. A reply cycle, a missing parent and a reply on another page are read, reported, and shown as top-level. | Every other reader understands these forms, and a private status would vanish in Acrobat. Putting the query in `core/` is what lets the batch runner (flow K) and the summary (Stage 8) reach it. | Stage 8 (the summary reads the same query) |
| **D6-13** | **Stage 6 declares ELEVEN settings rows and one key FAMILY, and ships three Preferences pages — Identity, Commenting and Measuring (9 → 12 of 26).** Rows: `identity/author`, `identity/email`, `identity/organization`; `commenting/stickyTool`, `commenting/popupBehaviour`; `measuring/scaleRatio`, `measuring/displayUnit`, `measuring/precision`, `measuring/snapToPaths`, `measuring/writeToComment`; `units/snapToObjects` (D4-5). **The per-tool defaults are a key family**, `commenting/default<Property>.<tool>`, generated over the tool table × the property table — colour, fill colour, opacity, thickness, line style, blend mode, font size, each only where the tool has it — declared once with a validator, as `shortcuts/<context>/<action>` is (D1-10); the Commenting page carries a per-tool editor, and `Ctrl+Alt+Enter` writes the armed tool's row. **Not declared:** `commenting/showPopupsOnHover` — *always, on hover or never* is ONE fact, which `commenting/popupBehaviour` holds (D4-5: a second key for one fact is two truths); `commenting/printComments` and `commenting/summaryLayout` (Q4, Stage 8); `commenting/spellCheckComments` (Q6, approved 2026-10-04: Stage 13). **`identity/author` empty means the Windows login name**, which `app/` supplies (convention 11). | The roadmap's `commenting/defaultColour.<tool>` names a family it cannot declare as rows — about twenty tools times seven properties. `Features.md` §4.1 asks for five properties per tool (colour, opacity, thickness, line style, blend mode) where the roadmap lists three; text tools need a size and filled shapes a fill. | — |
| **D6-14** | **A measurement is an annotation, and its scale travels with it.** Distance, perimeter and area are `Line`, `PolyLine` and `Polygon` with `/IT /LineDimension`, `/PolyLineDimension`, `/PolygonDimension` and a **`/Measure` dictionary** (ISO 32000-1 12.9, `/Subtype /RL`) holding the scale string and conversion factors; the readout is also written to `/Contents` when `measuring/writeToComment`. The default scale is `measuring/scaleRatio` in `measuring/displayUnit` at `measuring/precision` decimals; **calibration** measures a known length and sets the ratio. The arithmetic is `core/Measure` (⚠️), headless. **Snapping** extends Stage 4's `core/Snap.h` with **point** targets — path endpoints, midpoints, intersections — under the precedence *object point > object edge > guide > grid* and the one device-pixel threshold; the path geometry is read on the **engine thread** per page (`IDocumentBackend::pageGeometry`, ⚠️ in `core/backend.h`), bounded at 100,000 segments a page (beyond it: no object snaps on that page, said once), and kept beside the page's text in `DocumentIndex`. `units/snapToObjects` turns object snapping on. The **history** is the session's list in the properties bar, never written. | A scale held only in a preference would be lost the moment the file is opened elsewhere; ISO's `/Measure` is what Acrobat reads. A page `/VP` viewport would change the page dictionary for every measurement, which is a page mutation this stage does not need. | Stage 13 (object editing snaps to the same points) |
| **D6-15** | **Ink keeps its raw samples; smoothing is presentation.** `/InkList` holds the samples as sampled (deduplicated within 0.25 device px); the `/AP` draws a **centripetal Catmull–Rom** curve through them, which passes through every sample and does not self-intersect within a segment. **The eraser splits strokes at the samples it covers** — whole-stroke and partial — as one `EditAnnotation`; undo restores the samples exactly. | Raw samples are the data; a stored smoothed curve could never be split where the eraser crossed it. Centripetal parameterization is the variant that does not overshoot at sharp turns. | — |
| **D6-16** | ✅ *(Q7, answered 2026-10-04: the page source moves to Stage 7)* **Stamps are `Stamp` annotations with our `/AP`; custom stamps live in `core/settings/ProfileStore`.** Built-in standard stamps use Acrobat's `/Name` values (`/Approved`, `/Draft`, `/Confidential`, … ISO 32000-1 Table 181) so other readers recognize them, drawn by our builder in base-14 faces. **Dynamic stamps** bake the identity, date and time at placement into the `/AP` and `/Contents` — nothing is recomputed on open. **Custom stamps** from an **image** (PNG with alpha as Flate + `/SMask`, JPEG passed through as `/DCTDecode` byte-for-byte); one image placed many times is **one** image XObject. **A custom stamp from a PDF PAGE is Stage 7's** (Q7, answered 2026-10-04): it needs every object the page reaches copied into the annotated document, which is `pdf/ops/ObjectGraphCopier`, and Stage 7's merge needs the same copier — `Implementation-Plan.md` § Stage 7 names both, and Stage 6's custom-stamp dialog offers no PDF source rather than a disabled one. A scanned signature and drawn or typed initials are custom stamps in a *Signatures* category — **markup, not a digital signature** (Stage 11). Categories and favourites are the profile store's; stamps are stored under `%APPDATA%\RawPage\stamps\`, which `core/settings/` may write. | A vector stamp from a page is what the capability means; a raster of it is a different, worse thing, so Stage 6 offers neither rather than the raster. The copier is built once, in the stage whose merge needs it most. | Stage 7 (the page source and the copier) |
| **D6-17** | **Attachments: document-level (`/Names /EmbeddedFiles`) and `FileAttachment` annotations, through `core/doc/Attachments`.** `AddAttachment` and `DeleteAttachment` are commands. **Extract** writes the embedded bytes to a path the person chose **through `pdf/ops`** (`IDocumentBackend::extractAttachment`), so no waiver. **Open**: an attached **PDF** opens in a tab from bytes (`openBytes`, read-only until Saved As); **any other type asks first**, then is extracted by `pdf/ops` into `%LOCALAPPDATA%\RawPage\attachments\<session>\` and handed to the shell; **an executable type is refused** by a fixed extension list in `core/doc/Attachments` — the *policy hook* the FileMap names, which Stage 10's `TrustPolicy` replaces. The session folder is emptied at a clean quit. | The FileMap row asks for *open* with *the trust warning*; the trust model is Stage 10's, and opening an executable from a document before it exists would be the one defect worse than not opening it. | Stage 10 (`TrustPolicy`) |
| **D6-18** | ✅ *(Q5, approved by the user 2026-10-04)* **Audio comments are `Sound` annotations with raw PCM** (`/R`, `/C`, `/B`, `/E /Signed`), recorded through **Qt Multimedia's Media Foundation backend only** and played inline the same way; an existing **WAV** (PCM, 8 or 16 bit, 1 or 2 channels) can be attached. The FFmpeg plugin and its six DLLs are **not deployed**, which `license-audit`'s Qt-module list and a deployment case check. **Its task, T14, is last; if it slips, it slips alone** (the roadmap's own words). | Media Foundation is the operating system's; FFmpeg would add six DLLs and a licence review for a capability that needs PCM only. A WAV's PCM is exactly what a `Sound` stream holds, so no codec is involved. | — |
| **D6-19** | ✅ *(Q4, approved by the user 2026-10-04)* **Comment summaries (all four layouts) and printing with comments move to Stage 8**, with `CommentSummaryDialog`, `comment.summarize`, `commenting/summaryLayout` and `commenting/printComments`. The exit criterion *"comment summaries generate in all four layouts"* is annotated, not rewritten. | A summary is a NEW document, and three of its layouts carry the document's pages — `FullWriter` (Stage 8) and page import (Stage 7, D6-16's copier). Printing is Stage 8's `PrintDialog`. D5-17 moved the comparison report there for the same reason. | Stage 8 |
| **D6-20** | **FDF is COS syntax and XFDF is XML, so they are read in different layers:** `pdf/ops/FdfCodec` (⚠️) over `pdf/cos/`, and `core/doc/XfdfCodec` (⚠️) over `QXmlStreamReader` — QtCore, present in the distro Qt 6.4 (convention 24), **external entities never resolved**. Both produce `core/doc/Fdf` (⚠️), the exchange model `AnnotationStore::importFdf` and `exportFdf` take. **Merge** is by `/NM`: an unknown `/NM` is added, a known one with a newer `/M` replaces, an older one is kept and reported; one `ImportAnnotations`, one undo step, and a `MergeResult` the import shows. **Export** writes through `pdf/ops`, so no waiver; *export only the filtered* is the pane's current query. | FDF needs the COS lexer that only `pdf/` may hold; XFDF needs nothing but QtCore. | — |
| **D6-21** | **Migrating comments to a revised document is `core/doc/CommentMigrator`** (⚠️), over Stage 5's `DocumentComparer` alignment and the two documents' text: **text markup is matched by the text it covers** in the aligned page, then by position; **other annotations by position relative to the nearest text anchor** on the aligned page. **An annotation that cannot be placed is REPORTED and never placed** — a comment moved to the wrong paragraph is worse than one reported. The result is one `ImportAnnotations` into the revised document. | The roadmap's own rule (*"matches on text content first, then position, and reports"*). Stage 5 built the alignment, so migration is a consumer, not a second comparer. | — |
| **D6-22** | **Copy, cut and paste carry annotations, across documents and processes.** With annotations selected, `edit.copy` puts a private MIME type (`application/x-rawpage-annotations`, the serialized annotations) **and** their text on the clipboard, and `edit.cut` is copy then one `DeleteAnnotation`. **`edit.paste` and `edit.cut` are registered at Common**: a page receiving annotations adds them at their page position (or the view's centre on a page of another size); an **image** on the clipboard becomes an image stamp. **The control is Stage 5's I-04 turned round**: every widget that owns `Ctrl+V` or `Ctrl+X` keeps it. | `Features.md` §4.1 lists both. A private MIME type on the system clipboard is how two RawPage processes exchange them without a file. | — |
| **D6-23** | **The recovery journal stores a payload over 64 KiB as a side file** — an image stamp, an attachment, a sound — named by its SHA-256 beside the journal, written through **`RecoveryJournal`'s existing whole-file write**, and named by hash in the command's line. The waiver census stays **15**. | Stage 3's journal fsyncs one line per command; a 50 MB attachment inlined as base64 would be a 67 MB line, written and flushed on the UI thread. | — |
| **D6-24** | **A2 is re-measured at Stage 6, at T4, with D6-2**: `bench_pipeline --stage6` drives the shipped scheduler with a viewport **while a burst of annotation commands** — one every 100 ms for 30 s, on `pages-900.pdf` and `text-900.pdf` — composes, reloads and re-renders, and reports **the viewport's p95** (D2-1's quantity), **the edit-to-pixels latency** (from `journal.run` returning to the scope page's first tile delivered with the change visible), the compose and reload times, and the path-geometry extraction time (D6-14). The scale is fixed in the guide's §3.1 before the run. **T15 re-reads it through the shipped surfaces.** | D5-22 named *"the next engine consumer (Stage 6's appearance generation)"*. Appearance generation turned out to be CPU (D6-6), and the engine's new work is the reload and the path geometry — which is what the bench must load. | T15 |
| **D6-25** | **Redaction MARKS only.** `Redact` annotations over an area or a text selection, with overlay text, colour and repeat (`/OverlayText`, `/IC`, `/Repeat`, `/DA`), an outline `/AP /N` and a preview `/RO`. **The page content is untouched** — a case asserts every content stream byte-identical after marking. **Not here:** applying (`redact.apply`), code sets, `redact.markSearch`, `redact.markPage`, `redact.properties` — Stage 10's, with `RedactionSetupDialog`. | The roadmap's deferral. A mark that removed anything would be applying, and applying without Stage 10's verification would be the worst S1 this product could ship. | Stage 10 |
| **D6-26** | **The corpus is generated and the oracles are independent of the engine.** Every fixture is ours (D2-11), with its ground truth written by the generator (`corpus/labels/annots-*.json`); **FDF and XFDF fixtures are generated in the shapes Acrobat writes**, and the exchange with Acrobat itself is a manual case (M-04), because a file Acrobat wrote cannot enter the corpus. **Interoperability is judged by Acrobat and Firefox** (M-02). Every Stage 6 claim carries the bound *"on our own generated corpus and the readers named"*. | Stage 5's D5-23 bound, applied to writes: a claim about other readers is a claim about the readers actually opened. | — |
| **D6-27** | **Flags are honoured, and changed only by command.** `/F Locked` refuses move, resize and delete; `/F LockedContents` refuses editing the text; `/F ReadOnly` refuses every interaction but selection; `/F Hidden` and `NoView` are not drawn and are listed in the pane as hidden; `NoZoom` and `NoRotate` are drawn as ISO 32000-1 12.5.3 says (PDFium's), and hit-tested where they are drawn. `LockAnnotation` sets and clears `Locked`, `LockedContents` and `ReadOnly`. | *"Lock a comment so it cannot be moved or deleted"* and *"read-only for other reviewers"* are flags every reader honours; a private lock would not survive the file leaving. | — |
| **D6-28** | **Pop-ups are a UI surface, never rasterized and never a command.** A note's pop-up is a widget in `AnnotationOverlay`; opening, closing and moving it changes no byte. A `Popup` annotation is written **once, when the note is created** (Acrobat's convention), with its rectangle; `/Open` is not rewritten afterwards. `commenting/popupBehaviour` decides when they show: always, on hover, or never. | D5-1's rule — view state is not document state — applied to the one surface where readers disagree most about what is saved. | — |
| **D6-29** | **A fifth fuzz target, `fuzz_annot_pipeline`**: bytes → `pdf/cos/` → `AnnotationReader` → `AnnotationStore` (threads, states, queries) → `AppearanceStreamBuilder` → the generated stream re-read; and an XFDF branch through `XfdfCodec`. NaN and infinite rectangles, a million-point `/InkList`, `/IRT` cycles, `/Annots` that is not an array, one annotation listed by two pages. The fuzz-count assertion moves **4 → 5** (`cmake/RawPageFuzzTargets.cmake`); the soak runs it. | Every input to the reader and the importer is untrusted, and the builder consumes what the reader produced. `core/`, `pdf/cos/` and `pdf/ops/` are portable by rule, so it builds under `portable-fuzz`. | — |

---

## 4. File manifest

Every file Stage 6 creates or turns from ○, with the [`Architecture-FileMap.md`](./Architecture-FileMap.md)
row it must flip **in the same change**. Rows marked ⚠️ have **no FileMap row** and are lockstep debt
(§10), paid with the file.

| File | FileMap row | Task |
|---|---|---|
| `corpus/make-annots.py`, the annotation fixtures (§5.2), `corpus/labels/annots-*.json`, `corpus/exchange/*.fdf`, `*.xfdf`, `corpus/stamps/*.png`, `*.jpg` | **None** — data; one `corpus/PROVENANCE.md` row per file, `golden/manifest.json` rows | T1 |
| `core/doc/Annotation.{h,cpp}`, `AnnotationStore.{h,cpp}` | §3 ○ (S6) → ✅ | T2, T9 |
| `pdf/ops/AnnotationReader.{h,cpp}` — the first-party reading (D6-3) | ⚠️ | T2 |
| `core/doc/Permissions.{h,cpp}` — `Operation::Annotate`, the per-page agreement | its row | T2 |
| `core/backend.h` — the annotation intents on `MutationSink`; `pageGeometry` (D6-14); `extractAttachment` (D6-17) | §3.8's row | T3, T11, T12 |
| `pdf/ops/MutationSinkImpl.{h,cpp}`, `RevisionWriter.{h,cpp}` — annotations in the desired state and in `verify()` | their rows | T3 |
| `pdf/ops/AnnotationOps.{h,cpp}`, `AppearanceStreamBuilder.{h,cpp}` | §3 ○ (S6) → ✅ | T3 … T14 |
| `pdf/cos/ContentStreamBuilder.{h,cpp}` | §3 ○ (S6) → ✅ | T3 |
| `core/cmd/commands/AnnotationCommands.{h,cpp}`, `AttachmentCommands.{h,cpp}` (⚠️ — the FileMap names no home for the two attachment commands); `core/cmd/Commands.def` rows | `AnnotationCommands` ○ (S6) → ✅ | T3 … T13 |
| `core/doc/Scope.{h,cpp}` — the aspect (D6-10) | its row | T4 |
| `pdf/render/PdfiumDocument.{h,cpp}` — the reload over two buffers (D6-2) | §3.10's row | T4 |
| `tests/bench/bench_pipeline.cpp` — `--stage6` (D6-24) | §3.17's `tests/bench/` row | T4 |
| `core/cmd/KeyBindingStore.{h,cpp}` — the context chain (D6-9); `core/cmd/KeyBindingDefaults.def` and `Features.md` §8 (Q1) | their rows | T5 |
| `ui/viewer/AnnotationOverlay.{h,cpp}` | §3.14 ○ (S6) → ✅ | T5 |
| `ui/viewer/PropertiesBar.{h,cpp}` (D6-9) | ⚠️ | T5 |
| `ui/viewer/tools/{MarkupTool,NoteTool,TypewriterTool,DrawingTool,InkTool,StampTool,MeasureTool,RedactTool}` | §3.14's `tools/*` row | T6 … T14 |
| `core/settings/SettingsKeys.def`, `SettingsSchema` — the rows and the family (D6-13); `PreferencesDialog::shippedPages()` | §3.8's and §3.15's rows | T6, T8, T12 |
| `ui/panes/CommentsPane.{h,cpp}` | §3.15 ○ (S6) → ✅ | T9 |
| `core/text/SearchEngine`, `DocumentIndex` — comments from the store (D6-11) | their rows | T9 |
| `core/settings/ProfileStore.{h,cpp}` | §3 ○ (S6) → ✅ | T10 |
| `ui/panes/StampPalettePane.{h,cpp}`, `ui/dialogs/CustomStampDialog.{h,cpp}` | ○ (S6) → ✅; the dialog family's *S6* entry | T10 |
| `core/doc/Attachments.{h,cpp}`, `ui/panes/AttachmentsPane.{h,cpp}` | ○ (S6) → ✅ | T11 |
| `core/Measure.{h,cpp}` (D6-14); `core/Snap.{h,cpp}` — point targets | ⚠️ for `Measure`; `Snap`'s row | T12 |
| `ui/dialogs/MeasurementScaleDialog.{h,cpp}` | the dialog family's *S6* entry | T12 |
| `core/doc/Fdf.{h,cpp}`, `core/doc/XfdfCodec.{h,cpp}`, `pdf/ops/FdfCodec.{h,cpp}` (D6-20) | ⚠️ ×3 | T13 |
| `core/doc/CommentMigrator.{h,cpp}` (D6-21) | ⚠️ | T13 |
| `core/doc/SoundData.{h,cpp}` — WAV in, PCM out (D6-18); `ui/viewer/AudioRecorder.{h,cpp}` | ⚠️ ×2 | T14 |
| `tests/fuzz/fuzz_annot_pipeline.cpp`; `cmake/RawPageFuzzTargets.cmake` 4 → 5 (D6-29) | §3.17's `tests/fuzz/` row | T2 |
| `tests/unit/tst_annotation.cpp`, `tst_annotstore.cpp`, `tst_annotationops.cpp`, `tst_measure.cpp`, `tst_fdf.cpp`, `tst_migrate.cpp`, `tst_profilestore.cpp`, `tst_sounddata.cpp` | §3.17's `tests/unit/` row | T2 … T14 |
| `tests/render/tst_annotreader.cpp`, `tst_appearance.cpp`, `tst_annotdisplay.cpp` | §3.17's `tests/render/` row | T2, T3, T4 |
| `tests/ui/tst_annotoverlay.cpp`, `tst_annottools.cpp`, `tst_commentspane.cpp`, `tst_stamps.cpp`, `tst_attachments.cpp`, `tst_measuretool.cpp` | §3.17's `tests/ui/` row | T5 … T12 |
| `tests/composition/tst_annotink.cpp` — the shown-is-saved raster equality and every geometry case against ink | §3.17's `tests/composition/` row | T4 … T12 |

**Commands — eleven** (the roadmap's twelve less `AddTypewriterText`, Q2 approved 2026-10-04):
`AddAnnotationCommand`, `EditAnnotationCommand`, `MoveAnnotationCommand`, `DeleteAnnotationCommand`,
`ReplyToAnnotationCommand`, `SetAnnotationStatusCommand`, `SetAnnotationAppearanceCommand`,
`LockAnnotationCommand`, `ImportAnnotationsCommand`, `AddAttachmentCommand`, `DeleteAttachmentCommand`.
Each acts on a set (D6-1). With Stage 3's `RotatePageCommand`, `mutation-lint` names **twelve**.

**Settings** — eleven rows and one family (D6-13). **Action ids** — every §8 id named in the roadmap's
*Key bindings handled* is registered; ids are minted only for menu items no §8 row or declared id
names — candidates are `annot.typewriter`, `annot.replaceText`, `annot.insertText`, `annot.cloud`,
`annot.dimension`, `annot.lock`, `annot.align`, `annot.group`, `annot.duplicateToPages`,
`annot.recordAudio`, `annot.attachAudio`, `annot.measureCalibrate`, `comment.deleteMatching`,
`comment.migrate`, `comment.numbering` — all chordless, so §8 is not touched by them. **The census is
re-measured at T15, not predicted here.**

**Files Stage 6 does NOT build**, so their absence reads as decided: `core/cmd/commands/ContentCommands`
(Q2, Stage 13); `ui/dialogs/CommentSummaryDialog` (Q4, Stage 8); `ui/viewer/tools/LinkTool` (Stage 7);
`ui/viewer/tools/ObjectEditTool` (Stage 13); `core/security/TrustPolicy` (Stage 10); `pdf/ops/ObjectGraphCopier`
(Q7, Stage 7 — its FileMap row added ○ (S7) on 2026-10-04).

---

## 5. Component design

### 5.1 The library graph after Stage 6

Unchanged in kind. `rawpage_core` gains `core/doc/`'s annotation model, `core/Measure` and the
exchange model; `rawpage_ops` gains the reader, the ops, the builder and the FDF codec;
`rawpage_cos` gains `ContentStreamBuilder`; `rawpage_pdfium` gains the reload. **No new library and no
new `platform/` interface.** By Q5 (approved 2026-10-04), `rawpage_ui` links `Qt6::Multimedia` — `ui/` only,
never `core/`. `rawpage_assert_headless()` is unchanged: the store, its queries, threads, states,
measurement arithmetic, XFDF, migration and every command's apply and revert are provable with no
display and no engine.

### 5.2 The corpus this stage is judged on (T1)

Generated by `corpus/make-annots.py`, byte-identical on regeneration, and each fixture **writes its
own ground truth** to `corpus/labels/annots-<name>.json`: every annotation's page, subtype, rectangle,
quadpoints, colour, author, dates, `/NM`, `/IRT`, state and flags.

| Fixture | Carries | Its control |
|---|---|---|
| `ordinary/annots-every-type.pdf` | One of every subtype Stage 6 writes — Text, FreeText (box, callout, typewriter), Line (with heads), Square, Circle, Polygon (and a cloud), PolyLine, Ink, Highlight, Underline, StrikeOut, Squiggly, Caret, Stamp, FileAttachment, Sound, Redact — each with an `/AP` our generator drew | `annots-mixed.pdf` (Stage 2), unchanged |
| `ordinary/annots-no-ap.pdf` | The markup types **without** `/AP` | The same annotations with one, on page 2 |
| `ordinary/annots-threads.pdf` | Three authors, replies to replies, a group (`/RT /Group`), Review and Marked state replies with a history, dates across a month | An `/IRT` cycle, a reply whose parent is missing, a reply on another page |
| `ordinary/annots-flags.pdf` | `Locked`, `LockedContents`, `ReadOnly`, `Hidden`, `NoView`, `NoZoom`, `NoRotate` | An unflagged twin of each |
| `ordinary/annots-rotated.pdf` | Annotations on pages with `/Rotate` 90, 180, 270 | Page 1 at 0 |
| `ordinary/annots-direct.pdf` | Direct annotation dictionaries in `/Annots`; an annotation listed by two pages; an annotation with no `/P` | A page whose `/Annots` are all indirect |
| `ordinary/annots-unknown-keys.pdf` | Annotations carrying private keys (`/PieceInfo`, `/RawPageTestKey`) and `/RC` rich text | The same without them |
| `ordinary/annots-900.pdf` | 900 pages, ten annotations each — **9,000** — sharing appearance streams so the file stays small | The measured workload for the pane (guide §3.6) |
| `ordinary/attachments.pdf` | Document-level files with descriptions — a `.txt`, a PDF, a `.exe`-named file of text bytes — and a `FileAttachment` annotation | A document with none |
| `ordinary/measure-scale.pdf` | A vector drawing of known geometry (endpoints, midpoints, intersections at known coordinates) and existing measurement annotations with `/Measure` | A page with no paths |
| `ordinary/annots-report-v1.pdf` + `labels/annots-migration.json` | Stage 5's `compare/report-v1.pdf`, annotated: markup on text that survives in `report-v2`, on text `v2` deleted, on a moved paragraph, and notes beside figures | The deleted-text annotations, which must be **reported** |
| `exchange/*.fdf`, `exchange/*.xfdf` | Every type, in the shapes Acrobat writes (`/F` naming the target, `/NM`, `/IRT`, state replies, rich text) | A malformed XFDF, an XFDF with an external entity |
| `stamps/*.png`, `stamps/*.jpg` | An RGBA PNG and a baseline JPEG for custom stamps | — (no PDF page source: Q7, Stage 7) |
| `ordinary/annots-signed-*` | **Not new**: the four `corpus/signed/` files are the A4 carry-over's fixtures | — |

**Every byte stays ours** (D2-11); appearances in fixtures use base-14 faces and Stage 5's
`cjk-readable.pdf` path for CJK.

### 5.3 One highlight, as it will actually run (flow E, made concrete)

```
Ctrl+Alt+H ─▶ Viewer row (Q1) ─▶ annot.highlight ─▶ ToolController.activate(MarkupTool{Highlight})
 └─ KeyContext::AnnotationTools → Viewer → Common → Global                          (D6-9)
     drag over text ─▶ TextSelection range (S5) ─▶ quadpoints from the glyphs' boxes, page space
     release ─▶ AddAnnotationCommand{[Highlight], colour/opacity from the tool's family keys}
       └─ journal.run ─▶ apply: AnnotationStore.add + sink.addAnnotation (intent)      (D6-1)
            ├─ Scope{page 3, annotations}  ─▶ tiles of page 3 dropped; text index kept  (D6-10)
            └─ sink: desired state ─▶ compose update (AnnotationOps + AppearanceStreamBuilder)
                 └─ render thread, between jobs: FPDF_LoadCustomDocument(original ⧺ update) (D6-2)
                      └─ page 3's tiles re-rendered ─▶ the overlay's preview released
Ctrl+S ─▶ SaveCoordinator (unchanged, flow F) ─▶ the same update, written and verified
```

### 5.4 What the reload must be measured against (T4)

The compose is the sink's (D3-5) and costs what the delta costs. The reload's cost is PDFium parsing
the newest cross-reference section and following `/Prev` — measured, not assumed, on `pages-900.pdf`
(a classic table), `text-900.pdf` and `objstm-xrefstream.pdf` (a stream). **Graded on the guide's
§3.1**: the viewport's p95 inside the frame budget, and the edit-to-pixels p95 inside 100 ms.

### 5.5 Test targets after Stage 6

| Target | Stage 6 suites | Runs where |
|---|---|---|
| `rawpage_tests` | `tst_annotation`, `tst_annotstore`, `tst_annotationops` (over `tests/unit/CosBackend.h`), `tst_measure`, `tst_fdf`, `tst_migrate`, `tst_profilestore`, `tst_sounddata`, every command's `[serialize:]` and `[revert:]` | Windows + `portable-asan` |
| `rawpage_render_tests` | `tst_annotreader` (against the labels and the engine), `tst_annotdisplay` (D6-2), `tst_appearance`'s PDFium control | Windows |
| `rawpage_golden_tests` | `tst_appearance`'s baselines | Windows |
| `rawpage_ui_tests` | `tst_annotoverlay`, `tst_annottools`, `tst_commentspane`, `tst_stamps`, `tst_attachments`, `tst_measuretool`; the census edits in `tst_viewercommands` and `tst_keybindings` | Windows, offscreen; audio capture on `windows` only |
| `rawpage_composition_tests` | `tst_annotink` — shown is saved, geometry against ink | Windows |
| `portable-fuzz` | `fuzz_annot_pipeline` (D6-29) | Linux CI |
| `gates.` | the new corpus files in `gates.corpus.*`; `gates.fuzz_count.*` at five; **`qpdf --check`** over the suite's outputs (Q8) | `gates` preset and CI |

**A case that writes a file writes it in a scratch directory it made**, and a case that composes the
product points its recovery, session and **stamp** roots at scratch directories (`CLAUDE.md` →
*Traps*): a suite that wrote the tester's stamp library would depend on it.

---

## 6. Task breakdown

Ordering is risk-first. **T4 comes before any tool**: a no-go on D6-2 changes how every tool's
result reaches the screen. **T3 comes before T4** because the reload has nothing to load until the
sink can compose an annotation. Stages 2 to 5 put their go/no-go early for the same reason.

| # | Task | Depends on | Content | Done when |
|---|---|---|---|---|
| **T1** | **The corpus this stage can be judged on** | — | §5.2's fixtures, labels, exchange files and images; `PROVENANCE.md` rows; golden rows | `make-corpus.py --verify`, `--check-provenance`, `--self-test` green; **every existing file byte-unchanged**; each fixture carries what its row claims, read from the bytes; `golden-update --check-manifest` green |
| **T2** | **The model and its first-party reading** | T1 | `Annotation`, `AnnotationStore` (read side, threads, states, queries); `AnnotationReader`; `Operation::Annotate` and the per-page agreement (D6-3); `fuzz_annot_pipeline` (D6-29) | `tst_annotreader` green **against the generator's labels**, every field; engine agreement over every corpus file both readers open; an encrypted document refused by key; `tst_annotstore` headless; fuzz count asserted at 5 |
| **T3** | **The write path: one type end to end** | T2 | The sink's annotation intents; `AnnotationOps`; `ContentStreamBuilder`; `AppearanceStreamBuilder` for **Highlight** first; `AddAnnotation` and `DeleteAnnotation`; `RevisionWriter::verify` extended; identity (D6-4), preservation (D6-5) | An added highlight saved: **the update holds exactly the new objects and the page's `/Annots`**, the original a byte-identical prefix; add-then-undo writes nothing; unmodelled keys survive an edit; `[serialize:]`, `[revert:]` and reversibility green; `qpdf --check` clean (Q8); **A4's automated half** — OpenSSL verifies all four signed files after a comment through the product |
| **T4** | **How it reaches the pixels — A2 at Stage 6** | T3 | D6-2's reload; D6-10's aspect; `bench_pipeline --stage6` (D6-24) | **Shown is saved**: live and saved rasters 0 px apart; tiles outside the scope survive; the text index keeps its text; the bench graded on the guide's §3.1 and **recorded** in `Tech-Stack-Plan.md` §3.3 as *A2 at Stage 6*; **a no-go changes D6-2 before T5** |
| **T5** | **The overlay, the context chain, the properties bar** | T4 | `AnnotationOverlay` (hit-test, handles, drag, resize, nudge, delete, duplicate); D6-9's chain and §8 move (Q1); `PropertiesBar`; `MoveAnnotation`, `EditAnnotation`, `SetAnnotationAppearance`, `LockAnnotation`; D6-27, D6-28 | Every in-tool chord **from a key event**; the fourteen arming chords from the page with nothing armed; hit-testing against the **ink**; one undo step per gesture; `Esc` still reaches the tool finder and the find bar |
| **T6** | **Text markup and notes; Identity** | T5 | Highlight, underline, strikeout, squiggly, replace, insert (D6-12's group); sticky notes and pop-ups; the three `identity/*` keys and their page | Markup quads cover **the selected glyphs' ink** on multi-column, rotated and vertical pages; a replace is one undo step; pop-ups change no byte |
| **T7** | **Free text, callout, typewriter** | T5 | D6-7 (Q3), D6-8 (Q2) | Typed text shown in the `/AP` decodes to what was typed; CJK through the CID path; a refused character named; `/DA` and `/AP` agree |
| **T8** | **Drawing, ink, eraser; tool defaults** | T5 | Line, arrow, rectangle, oval, polygon, polyline, cloud; ink (D6-15) and eraser; the family keys, the Commenting page, `K`, `Ctrl+Alt+Enter`; `tst_appearance` complete | Golden baselines per type and property; **PDFium's own appearance within IoU 0.9**; ink passes through every sample; an erase is one undo |
| **T9** | **The Comments pane** | T6 | D6-12; group, sort, filter, search, threads, reply, status and history, checkmark, delete all and matching; numbering; `Ctrl+.`/`Ctrl+,`; `find.inComments`; D6-11 | The whole Comments pane table **from key events**; queries graded against the labels; the 9,000-annotation file inside the guide's §3.6; an unsaved comment found by the find bar |
| **T10** | **Stamps** | T7 | D6-16; `ProfileStore`; the palette; built-in, dynamic and image stamps; signatures and initials as stamps; D6-22's paste | Each stamp's appearance golden; a JPEG passed through byte-for-byte; one image placed twice written once; the store survives two processes |
| **T11** | **Attachments** | T3 | D6-17; `Attachments`, the pane, the two commands, `annot.attachFile` | Listed against the labels; extract's bytes equal; open as specified; an executable refused; nothing written outside the scratch and session folders |
| **T12** | **Measurement** | T5 | D6-14; `core/Measure`; point snapping; `pageGeometry`; the five `measuring/*` keys, `units/snapToObjects`, the Measuring page; `MeasurementScaleDialog` and calibration; dimension callouts | Arithmetic against hand values; `/Measure` written and read back; snapping to the generator's known points inside the device-pixel threshold; calibration reads true |
| **T13** | **Exchange: FDF, XFDF, migration, copy and paste** | T9 | D6-20, D6-21, D6-22; `ImportAnnotations` | Round trip equal in every modelled field; the Acrobat-shaped fixtures imported; merge by `/NM`; migration graded on the guide's §3.5; an external entity never resolved |
| **T14** | **Redaction marks and audio** | T6 | D6-25; D6-18 (Q5) | Marks written with their appearances and **every content stream byte-identical**; PCM round-trips exactly; the FFmpeg plugin absent from the deployment. **Audio may slip alone** |
| **T15** | **The re-measurement, the census, CI, lockstep** | all | D6-24's second reading through the shipped surfaces; `--list-actions`; §9; §10 | T4's figure re-read and recorded as the product's; the census cases and `--list-actions` agree; `Implementation-Plan.md`'s Stage 6 block reflects reality, deviations included |

---

## 7. Blocking preconditions — setup checklist

| # | Precondition | Why | State |
|---|---|---|---|
| **P-1** | **Adobe Acrobat with a signature panel and comment import/export** — owner: **you** | Interop (M-02), A4 through the product (M-03), FDF/XFDF exchange (M-04) | ✅ **Acrobat 26.2.21931** at `C:\Program Files\Adobe\Acrobat DC\Acrobat\Acrobat.exe` (read 2026-10-04; Stage 3 used 26.001.21771) |
| **P-2** | **A second reader that is NOT PDFium** — owner: **you** | D6-6's fourth oracle: Chrome and Edge render with PDFium | ✅ **Firefox 157** (pdf.js) at `C:\Program Files\Mozilla Firefox\` (read 2026-10-04). **A phone reader is optional**: the roadmap names one, and the guide records whichever you walk |
| **P-3** | **A microphone** — owner: **you** | M-08 (Q5, approved 2026-10-04) | ✅ A headset microphone is present (*Headset Microphone (Jabra EVOLVE 20 MS)*, read 2026-10-04) |
| **P-4** | **`qpdf` on CI's Ubuntu `gates` job** — owner: **me** (Q8, approved 2026-10-04) | D6-6's third oracle | ⚪ Not installed; T3 adds it to `ci.yml` (`apt-get install qpdf`) and records its version in the job's log. Not needed on this machine |
| **P-5** | **Python 3** on every job that runs the corpus checks | T1's generator | ✅ As Stage 5 |

---

## 8. Verdict ledger — what Stage 6 owes

| Verdict | Produced by | Recorded in |
|---|---|---|
| **A2 at Stage 6** — does the frame budget hold while annotation commands reload the engine, and does an edit reach the pixels inside 100 ms? | **T4** (bench) and **T15** (through the shipped surfaces) — D6-24 | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), labelled **Stage 6's workload**, beside Stages 2, 4 and 5 |
| **D3-15's revisit** — the engine reloads the composed revision, or mirrors | **T4** — D6-2 | This plan's D6-2, and `Stage3-Mutation-Plan.md` D3-15's *Revisit* cell pointed at it |
| **A4 through the product, the permitted edit** — Stage 3's carried exit criterion | **T3** (OpenSSL, automated) and **M-03** (Acrobat) | `Tech-Stack-Plan.md` §3.3's A4 row, beside Stage 3's figures; its product verdict moves from *partial* only on both halves |
| **Interoperability of our appearances** — Acrobat and Firefox show what RawPage shows | **M-02** | The guide's §6, bounded to the readers named (D6-26) |

**A stage that is green but has no verdict is not signed off.**

---

## 9. CI changes

| Job | Before | After |
|---|---|---|
| `gates` (ubuntu, no Qt) | 119 cases on Ubuntu (run 37188585103) | The new corpus files and labels in `gates.corpus.*`; `gates.fuzz_count.*` at five; **`qpdf --check`** over the suite's written files (Q8) |
| `windows` (self-hosted) | 1422 cases | Plus every Stage 6 suite; audio capture skips offscreen and is a by-hand run on the `windows` platform |
| `portable-asan` (ubuntu) | `core/`, `pdf/cos/`, `pdf/ops/` | Plus every Stage 6 suite in those layers, read by name from the log |
| `portable-fuzz` | four targets, 60 s each | **Five** — `fuzz_annot_pipeline` |
| `fuzz-soak.yml` | four targets, ten minutes | Five |

---

## 10. Lockstep debt this stage creates

**Paid in the same change as this draft:**

- **`Implementation-Plan.md`**: § Stage 6's status line (a draft plan and a guide exist, §0 open, no
  code); §5's guide row and its prose; the list of stage plans.
- **`CLAUDE.md`**: *Where the work is right now* — the next action is your §0 answers, then T1 — and
  the document-set counts (seven stage plans, seven testing guides).
- **`claude-plan/README.md`**: ⑤'s and ⑥'s lists, and Stage 6's row in *Current state*.

**Owed on your §0 answers** — each made only once its answer is given:

- ✅ **Q1, paid 2026-10-04** → `Features.md` §8: the fourteen arming rows moved to *Viewer*, with a note
  under the Annotation tools table; `core/cmd/KeyBindingDefaults.def` in the same change; §8's collision
  census **twelve → thirteen** (9 + 4 + 0) with the old heading's anchor kept, and its wider counts
  re-parsed before and after (unmoved); `tests/unit/tst_keybindings.cpp` (two cases renamed *twelve* →
  *thirteen*, `Ctrl+Alt+M` added) and `tests/ui/tst_viewercommands.cpp` (Viewer rows not yet registered
  **4 → 18**); the live statements of the figure in `CLAUDE.md`, `Implementation-Plan.md` § Stage 13,
  `README.md`'s test command and two `ui/viewer/PageView` comments. Signed-off stage documents keep
  *twelve* as history.
- ✅ **Q2, paid 2026-10-04** → `Implementation-Plan.md` § Stage 6: `ContentCommands` struck from
  *Components* and `AddTypewriterText` from *Cross-cutting* (eleven commands), each saying why; § Stage
  13 needed no note — it already owns `ContentCommands` in full; `ContentCommands`' FileMap row re-tagged
  ○ (S13).
- ✅ **Q3, paid 2026-10-04** → `Deferred-Work.md` gains **`DW-3`**, typed comments in scripts outside
  WinAnsi and the four CJK collections, unassigned; `CLAUDE.md`'s document-set row names all three
  `DW-n` entries.
- ✅ **Q4, paid 2026-10-04** → `Implementation-Plan.md` § Stage 8 gains comment summaries and printing
  with comments in its objectives, components, keys and exit criteria; § Stage 6's exit criterion and
  keys annotated; §7's §4.2 row names Stage 8; the dialog family row's *S6* entry moved
  `CommentSummaryDialog` to *S8*.
- ✅ **Q5, paid 2026-10-04** → `Tech-Stack-Plan.md` §6.1 records the Media Foundation backend as the
  only one deployed and the FFmpeg plugin's five DLLs as never deployed. **Correction to this bullet as
  drafted:** `QtMultimedia` was already one of §6.1's nine modules, `tools/license-audit` already allows
  it (`LicenseAudit.cpp`) and `cmake/RawPageQt.cmake` already lists it, so no licensing row and no audit
  change was owed — the generated `qt-modules.txt` names it when T14 first links it, and A-07 checks the
  deployment.
- ✅ **Q6, paid 2026-10-04** → `Implementation-Plan.md` § Stage 13's keys gain
  `commenting/spellCheckComments`, § Stage 6's say it moved, and §7's §4.2 row names Stage 13.
- ✅ **Q7, answered and paid 2026-10-04** (moved to Stage 7) → `Implementation-Plan.md` § Stage 7 gains
  *custom stamps from a PDF page* and `pdf/ops/ObjectGraphCopier` in its objectives and components, and
  §7's §4.1 row names Stage 7; `Architecture-FileMap.md` gains the copier's row, ○ (S7); this plan's
  D6-16, §4, §6 and §11 and the guide's A-78, M-07 and status line corrected.
- ✅ **Q8, paid 2026-10-04** → `Tech-Stack-Plan.md` §9 (verification) names `qpdf` beside OpenSSL.
  **`ci.yml` installs it at T3**, with the first file it has to judge (P-4) — not now, because a gate
  step over no files would be a green tick that checked nothing.

**Owed with the code:**

- Every ⚠️ row in §4, with its file.
- **Flow E** (`Architecture-FileMap.md` §5) redrawn at T4 with the context chain, the aspect and the
  reload; **flow D** gains the annotation intents at T3; **flow F** the side files at T11.
- `Tech-Stack-Plan.md` §3.3: *A2 at Stage 6* at T4 and T15; A4's product row at T3 and M-03.
- `Stage3-Mutation-Plan.md` D3-15's *Revisit* cell pointed at D6-2 at T4.
- **`CLAUDE.md`'s *What works today* and *deliberately absent* paragraphs**, re-derived at T15 from the
  product: the command count, the unregistered count, the fuzz target count **5**, the waiver census —
  each re-measured, none predicted here.

---

## 11. Explicitly out of scope

| Not in Stage 6 | Arrives at |
|---|---|
| **Comments on an encrypted document** — neither listed nor added (D6-3) | Stage 10 |
| **Applying redaction**, code sets, mark-by-search, mark-page (D6-25) | Stage 10 |
| **Comment summaries, printing with comments** (Q4) | Stage 8 |
| **Typed comments outside WinAnsi and CJK** (Q3) | `DW-3` |
| **Spell-checking comments** (Q6) | Stage 13 |
| **Content-stream editing**, `ContentCommands` (Q2) | Stage 13 |
| **Links** (`annot.linkTool`) | Stage 7 |
| **A custom stamp from a PDF page**, and `ObjectGraphCopier` (Q7, D6-16) | Stage 7 |
| **Form fields** — widgets are read, drawn and searched as before, never edited | Stage 9 |
| **Digital signatures** — a signature *image* is a stamp (D6-16) | Stage 11 |
| **Shared review, review tracking, collaborative sessions** (§4.3) | Stage 16 |
| **The trust manager** — Stage 6 refuses executables by a fixed list (D6-17) | Stage 10 |
| **Substitute page colours** — Stage 4's I-07 | `DW-1`, untouched |
| **`Ctrl+B`** — the one Viewer chord still unregistered after this stage | Stage 7 |

---

## 12. Risks inside Stage 6

| Risk | Shape | Mitigation |
|---|---|---|
| **An edit is slow to appear, or stalls the page** | A reload per command on a 900-page file | D6-2's no-copy reload and scope-kept tiles; **T4 measures it before any tool queues work**; the mirror is the named fallback |
| **An appearance looks right only in RawPage** | PDFium draws what PDFium generated, and Chrome agrees because it is PDFium | D6-6's four oracles, two of them other engines |
| **An edit deletes what it did not model** | A dictionary rewritten from the model | D6-5; `annots-unknown-keys.pdf` with a byte comparison of every unmodelled key |
| **The store and the engine disagree about an annotation** | Two readers of one page | D6-3's per-page agreement: a disagreeing page is read-only and says so |
| **Identity breaks across recovery or exchange** | Object numbers assigned at compose | D6-4: `/NM` for everything RawPage creates; recovery cases replay every command kind |
| **A highlight sits beside the glyphs, and the suite agrees** | Quadpoints checked against the boxes they came from | Rule 6: every geometry case against the rendered ink |
| **A redaction mark removes content, or appears to** | A mark that is not an application | D6-25: every content stream byte-identical after marking |
| **A malicious XFDF or annotation hangs or exhausts the reader** | Entity expansion, a million-point ink list, an `/IRT` cycle | D6-20 (no external entities), D6-29's fuzz target, ASan over the reader and the store |
| **`Ctrl+V`, `Ctrl+X` or `Esc` stops working in a text field** | A Common registration consumes the key | D6-9 and D6-22's controls over every text-owning widget |
| **Opening an attachment runs something** | A document carries an executable | D6-17: asked first, executables refused, Stage 10's trust model named as the owner |

---

## 13. Definition of Done for Stage 6

[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions),
made concrete:

1. §0 answered, and T1…T15 complete; the
   [exit criteria](./Implementation-Plan.md#stage-6--annotations-and-review) met, as amended by the
   answers.
2. `cmake --build --preset x64-release --target checks` clean — four gates, **`mutation-lint` naming
   exactly §4's commands**, `layer-lint` at **15** escapes.
3. `ctest` green on Windows across all six targets and the `gates` preset, **on both presets**, every
   Stage 0–5 case present **by name**.
4. `portable-asan` green including every Stage 6 suite in `core/`, `pdf/cos/` and `pdf/ops/`, read from
   the job's **log**; `portable-fuzz` and the soak clean on **five** targets; `qpdf --check` clean in
   the `gates` job's log (Q8).
5. **Golden suite green with every existing baseline unchanged** — the new baselines are T1's files
   and `tst_appearance`'s.
6. **A2 at Stage 6** recorded, bench and product; **A4's product half** recorded with M-03.
7. [`Stage6-Annotations-Testing-Guide.md`](./Stage6-Annotations-Testing-Guide.md) — written from this
   plan before the code, its automated half run green by me with **actual** output, its manual half
   walked and signed off **by you**. I do not mark a manual case.
8. No open S1 or S2 in the guide's issue log.
9. `Architecture-FileMap.md` rows flipped and §10's debt paid, in the same changes as the code.
10. `Implementation-Plan.md`'s Stage 6 status block updated to reality, deviations included.
