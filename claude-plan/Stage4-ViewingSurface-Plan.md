# Stage 4 — Viewing and Navigation Surface · Execution Plan

> **What this document owns.** The **execution order** of Stage 4 and the **Stage-4-local design
> choices** that no other document decides. It owns no product facts. Every stack, layering,
> key-binding and verification fact is cross-referenced to its owner and never restated as a new
> decision:
>
> | For… | Read |
> |---|---|
> | The Stage 4 objectives, components, key bindings handled, settings keys and exit criteria | [`Implementation-Plan.md` § Stage 4](./Implementation-Plan.md#stage-4--viewing-and-navigation-surface) |
> | The layering rule, the interface catalogue, risk **A2** and the `IAcceleratedSurface` clause | [`Tech-Stack-Plan.md` §3.3, §4.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified) |
> | What each file is for, and flows B, C and E | [`Architecture-FileMap.md` §3.2, §3.13, §3.14, §3.15, §5](./Architecture-FileMap.md) |
> | The authoritative key bindings — the whole **Page Thumbnails**, **Bookmarks** and **Full Screen & Presentation** tables, and the Viewer rows `Ctrl+U`, `Ctrl+'`, `Ctrl+;`, `Ctrl+L`, `Ctrl+3` | [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference); the capabilities in §1.1 … §1.4 |
> | How Stage 4 is verified | [`Stage4-ViewingSurface-Testing-Guide.md`](./Stage4-ViewingSurface-Testing-Guide.md) — written from this plan, **before** the code |
>
> **Status:** ⚪ **Written 2026-09-18, the day Stage 3 was signed off. No Stage 4 code exists.** This
> file is a plan, not a progress log: Stage 4's live status, task by task and deviation by deviation,
> will be the **Stage 4 status block in
> [`Implementation-Plan.md`](./Implementation-Plan.md#stage-4--viewing-and-navigation-surface)**,
> which is the authority. Stage 2's plan carried a per-task narrative until its T16 and removed it
> then, having watched one file disagree with itself in three places; Stage 3's did not start one and
> neither does this.
>
> **Label scoping, because four namespaces now collide across stages.** `T1`…`T14` here are
> **Stage 4** tasks and are unrelated to Stage 0's, 1's, 2's or 3's; this document writes "Stage 4 T5"
> wherever the stage is not already obvious. `D4-n` are this stage's decisions, distinct from
> `Tech-Stack-Plan.md`'s `D1`…`D10` and from `D2-n` and `D3-n`. `A2` is a **risk**
> (`Tech-Stack-Plan.md` §3.3); `A-nn` are **test cases**
> (`Stage4-ViewingSurface-Testing-Guide.md` §5). A bare `I-nn` in this file is **Stage 2's**
> (`Stage2-Rendering-Testing-Guide.md` §8) unless it says otherwise, because the two issues Stage 4
> inherits — **I-28** and **I-11** — are both Stage 2's.

---

## 1. What Stage 4 is actually for

Stages 2 and 3 gave the product one page surface and one mutation. Stage 4 is the stage where a
person can *work in* a document rather than look at one: navigate it from a thumbnail or a bookmark,
split the window, measure with rulers and guides, present it on the second monitor, arrange windows,
and come back tomorrow to the layout they left. It is the first stage whose subject is almost
entirely **state that is not in the file**, and that is what makes it a different kind of risk from
the three before it.

Three things get permanently harder if this stage gets them wrong.

1. **View state must never become document state.** Everything Stage 4 builds — a zoom, a guide, a
   split, a pane width, an open-document set — is the *application's* memory of how someone was
   reading, and none of it belongs in the PDF. The moment a guide is written into the file, a
   reader's window layout becomes a document mutation that `CommandJournal` never saw, which is
   exactly what `ui/` is forbidden to do (`Tech-Stack-Plan.md` §4.2) and what
   `Implementation-Plan.md` § Stage 4 calls *"a useful check that view and document changes stay
   separate."* → **D4-1**, **D4-7**.
2. **The render pipeline gains three new consumers at once.** A thumbnails pane over a 900-page
   document, a split view rendering the same pages twice, and a presentation at full-screen size all
   go through one scheduler, one cache and — by Stage 2's measurement — **one worker**. A2's product
   figure is p95 **13.03–13.20 ms** against a 16.667 ms frame budget, about **1.27×** headroom
   (`Tech-Stack-Plan.md` §3.3), and that figure was measured with a single viewport. Stage 4 is
   where the headroom is spent or kept, and where the `IAcceleratedSurface` question its FileMap row
   defers to *"Stage 4, if A2 says it is needed"* gets an answer. → **D4-10**, **D4-16**.
3. **A window restored onto a monitor that is gone is a window nobody can reach.** Session restore is
   the one Stage 4 feature that fails *between* runs, on hardware the developer does not have plugged
   in, and its failure mode is a document the user cannot get back to. → **D4-8**, **D4-9**.

**What Stage 3 should change about how this stage is built.** Stage 3 produced sixteen issues, and
three of its lessons apply here with more force, because Stage 4's subject is mostly invisible to a
headless run:

- **A case that asserts an INTENT has not asserted the EFFECT** (Stage 2's **I-28**, and convention
  19). `tests/ui/tst_viewercommands.cpp` pressed `Ctrl+3` and checked `fitMode() == Visible` — the
  mode, never the zoom — and Fit Visible has been Fit Page under another name ever since. Stage 4 is
  full of that shape: a pane that reports the right selection and scrolls nowhere, a snap that
  returns the right point and is applied to nothing, a fingerprint that is computed and not used.
  **Every Stage 4 case names the observable it checks, and where it checks an intent it says so at
  the assertion.**
- **A mechanism that fires needs a control for when it must NOT fire** (convention 22, Stage 2's
  I-13). Snapping has to *not* snap outside the threshold; restore has to *not* resurrect a document
  the user closed; a presentation has to *not* keep the screen awake after it exits.
- **A green tick is not a clean run** (Stage 2's I-38, Stage 3's rule 3). For this stage the tick to
  distrust is a widget case that never laid anything out: convention 12 — *a widget test must drive
  layout itself, and `resize()` alone is not driving it* — is the one Stage 4 breaks most easily,
  because a pane with no layout pass reports zero visible rows and every assertion about "what is
  visible" passes vacuously.

---

## 2. Inherited ledger — what Stage 3 hands over

Recorded so the tasks below are concrete. None of these is re-decided here; each was re-measured on
2026-09-18 unless it says otherwise.

| Fact | State at the end of Stage 3 | Consequence for Stage 4 |
|---|---|---|
| The suite | **923/923** on `x64-release` and on `x64-debug`, one named skip; the `gates` preset **124/124**. Six test targets, **seven** benchmark targets — this row said six when it was written and was wrong then too, re-derived 2026-09-23: the seventh is `rawpage_bench_openclose_ui_classicheap`, the manifest CONTROL rather than a seventh measurement | Stage 4 adds to a green suite; a red case is this stage's |
| `ActionRegistry` | **361 declared, 53 registered, 308 unregistered** (`rawpage.exe --list-actions`, 2026-09-18). Of the **70** ids in this stage's namespaces — `view.*`, `pane.*`, `nav.*`, `tool.*`, `present.*`, `window.*` — **34 are already registered** and 36 are not, counted from the same run: Stages 1 and 2 wired the tab, window, zoom, fit, layout, rotate-view, paging and tool chords, and every `present.*` id, every pane id but `pane.log`, the ruler/grid/snap ids, `window.tile*`/`cascade`/`split` and `view.readingMode` are declared with their §8 chords and no handler | Stage 4 registers handlers and edits no `KeyBindingDefaults.def` row that already exists. **“It declares no new id” was wrong, and is corrected here rather than left standing** (2026-09-21): that clause was a reading of the 70 ids already in this stage's namespaces, not a decision, and **T7 declared two** — `tool.loupe` and `view.autoScroll`, the Loupe and auto-scroll of `Features.md` §1.2, which §6 gives this stage and which had no row and no chord. Neither gets a §8 binding, so §10's *§8 is not touched* still holds. **The count is re-measured at T14**, not predicted here |
| `tests/ui/tst_viewercommands.cpp` | *"the rest of the Viewer table is still NotImplemented and leaves the key alone"* is **derived from the table**, not typed, and asserts a census: **26 registered, 13 not** | Stage 4 changes both numbers in the same change as the registrations. The case needs no rewrite — that is what D3-derived list bought |
| `core::render::RenderPriority::Speculative` | Declared at Stage 2 with the comment *"a page the user has not reached; **Stage 4's thumbnails**"*. **Used by two scheduler unit cases and by no product code** | T5 is its first product caller — **D4-10** |
| `platform::IPowerSession::setPresentationModeActive(bool)` | Implemented in `WinPowerSession` at Stage 2 T12 and **called by nothing** | T10 is its first caller — **D4-12**. ✅ **Done 2026-09-21**: called from `ui/viewer/FullScreenController::applyPowerState()`, which has ONE caller and runs before any work that could throw, so every exit path pairs it (guide case A-58) |
| `platform::IDisplayTopology` | Implemented (`WinDisplayTopology`); `MonitorInfo` carries `id`, `friendlyName`, physical `x/y/width/height`, `scaleFactor`, `physicalDpi`, `primary`; `subscribe()` fires on a mid-session change. The `QScreen` join is by `friendlyName`, and `id` never matches one (**I-20**) | The fingerprint (**D4-8**) is built from `monitors()`, and the full-screen monitor choice from the same join |
| `platform/win/WinAcceleratedSurface` | ○ **(S4)** — re-tagged from ○ (S2) at Stage 2 T16 rather than built, on the Stage 2 guide's own terms: accelerated presentation is Stage 4's *if A2 says it is needed*. `TilePresenter` holds a null | **D4-16**: T2 measures, and the row is answered rather than carried again |
| `core/settings/SettingsKeys.def` | 31 rows; the `FullScreen` and `UnitsAndGuides` groups exist in `SettingsGroup` with **no keys**. `general/restoreSession` is declared (Stage 1) and **read by nothing** — `app/Application.h` says so in a comment | T3 is `restoreSession`'s first reader (**D4-9**); T8 and T10 fill the two empty groups |
| Preferences | **6 of 26 pages** ship (General, Documents, Page Display, Performance & Cache, Logging, Shortcuts). A page is two lines in `shippedPages()` and no widget code (convention 17) | T8 and T10 ship **Units & Guides** and **Full Screen** — eight of 26 |
| `ui/shell/SidebarDock` | Dock left/right, float, auto-hide with a peek, width memory **per session only** — and it hosts a **placeholder**, not panes (`setPlaceholderText`). Its row still owes *"real panes (S4+) and per-document open-pane state"* | T5 gives it a pane registry; T13 gives it `SessionState` |
| `ui/panes/` | One file, `LogPane` — a **bottom** dock and a view of the application rather than of a document | The three document panes are the first users of the sidebar |
| `ui/viewer/ViewportController` | Zoom, fit, scroll, page snapping and the view history. A fit is stored as an **intent** and re-satisfied on every resize. *"Auto-scroll is Stage 4's"* | T7 adds auto-scroll; T12 gives `FitMode::Visible` something to mean (**D4-14**) |
| Stage 2 **I-28**, open, S3 | **Fit Visible fits the CropBox**, so on every document whose CropBox is its MediaBox `Ctrl+3` is `Ctrl+0` under another name. No corpus generator writes a `/CropBox`. The repair was deliberately not guessed at | **D4-14** decides it here, with the corpus file that can show the three readings apart |
| Stage 2 **I-11**, open by decision, S3 | `pageDisplay/lineWeight` is declared and inert: PDFium enforces a one-pixel minimum and exposes no flag over it | Untouched by Stage 4. It is a capability gap, not a viewer defect, and re-reading it is not a Stage 4 task |
| Stage 2 **I-34**, open, S3 | The second level of ribbon key tips is inert — a group's digit does nothing | Untouched by Stage 4; it is `ui/shell/Ribbon`'s, and no Stage 4 task opens that file |
| `corpus/` | **67 files**, all regenerating byte-identically, one `PROVENANCE.md` row each; **62 golden baselines, 5 excluded with a reason**. `ordinary/outline-labels.pdf` already carries a two-level `/Outlines` tree and `/PageLabels`; `ordinary/cover-odd-count.pdf` carries `/PageLayout /TwoPageRight`; `ordinary/rtl-binding.pdf` carries `/Direction /R2L`. **No file has a `/CropBox`, named destinations, or a page transition** | T1 — **D4-3**, **D4-14** |
| `mutation-lint` | *"clean — 1 command(s): RotatePageCommand"* | **D4-1**: it still says one at the end of Stage 4, and that is the stage's mechanical proof that the viewing surface mutates nothing |
| `layer-lint` | 357 files, 2,721 includes, **14 live escapes** | Stage 4 adds no filesystem write and should add no waiver. A fifteenth is a finding |
| Open issues carried | Stage 2's I-06, I-11, I-28 and I-34, all S3; Stage 3's log has none open | Stage 4 starts with no open S1 or S2, and owns **I-28** |

---

## 3. Stage-4-local decisions

Choices this plan makes because nothing upstream decides them. Each is scoped to Stage 4 and listed
here so a later stage can find and revisit it rather than rediscover it.

| # | Decision | Why | Revisit |
|---|---|---|---|
| **D4-1** | **Stage 4 constructs no `Command`, adds no `core/cmd/Commands.def` row, and touches no `MutationSink`. The proof is mechanical: `mutation-lint` reports *"1 command(s): RotatePageCommand"* on the last day of the stage as it did on the first.** Every piece of Stage 4 state lives in `core/settings/` or in a widget, and the one place a Stage 4 surface reaches a command is `Ctrl+R` in the Thumbnails pane, which dispatches **Stage 3's** `page.rotate` through `ActionRegistry` and constructs nothing itself. | `Implementation-Plan.md` § Stage 4 already says *"No new commands (this stage is all view state, no document mutation — a useful check that view and document changes stay separate)"*, and a check nothing performs is a hope. This is the check: a gate that already runs, with a number that must not move. It also settles the question a pane invites — *should a thumbnail drag reorder pages?* — by making the answer cost a `Commands.def` row that this stage has decided not to spend. | Never; Stage 7 is where the panes gain commands, and the number moves then |
| **D4-2** | **Three document panes ship: `ThumbnailsPane`, `BookmarksPane` (navigate only) and `DestinationsPane` (read-only). The seven remaining `Ctrl+Shift+*` pane chords — `C`, `I`, `L`, `E`, `A`, `G`, `O` — stay unregistered and leave the key unconsumed, and the sidebar's pane list contains only panes that exist** — no greyed rows, no "coming soon". | Convention 6's rule for `platform/` interfaces — *an unimplemented thing is a null member, never a stub* — applies to a pane list for the same reason: a row that opens an empty dock is indistinguishable from a pane that is broken, and Stage 2's **I-11** is what a declared-and-inert surface costs when it is a control rather than a row. Every other pane waits on a model that does not exist (`AnnotationStore` S6, `FormModel` S9, `LayerTree` and `TagTree` S7, `Attachments` S6, `ValidationReport` S11, `SearchEngine` S5, `ReflowDocument` S13): a pane cannot precede its model, which is D3-8's rule read in the other direction. | Each pane's own stage |
| **D4-3** | **`core/doc/Outline`, `core/doc/Destinations` and `core/doc/Metadata` arrive as READ-ONLY halves, hydrated through the PDFium facade** (`IDocumentBackend` gains `outline()`, `destinations()` and `metadata()`), not through `pdf/cos/`. **`Destinations` and `DestinationsPane` are re-tagged ○ (S7) → ○ (S4 read, S7 edit)**, matching `Outline`'s split. | *"We read through PDFium and write through our own code"* (`CLAUDE.md`, D3): the outline, the destination name tree and the Info dictionary are reads, PDFium's `FPDFBookmark_*`/`FPDFDest_*`/`FPDF_GetMetaText` already resolve them including the inherited and indirect cases, and a second first-party walk would be a second reader to keep in agreement — which is the disagreement D3-7 refuses editing over. The re-tag resolves a **contradiction between two owning documents**, found while writing this plan: `Implementation-Plan.md` § Stage 4 lists *"`ui/panes/DestinationsPane` (read-only)"* among Stage 4's components while `Architecture-FileMap.md` §3.15 tags the file and its model ○ (S7). One of them had to be wrong; the pane is a navigation surface and navigation is this stage. | Stage 7, which adds the editing half and, with it, a first-party writer's view of both trees |
| **D4-4** | **`Metadata` at Stage 4 is the INFO DICTIONARY and the computed file facts only.** Document Properties shows title, author, subject, keywords, creator, producer, the two dates, the PDF version, the page count, the page size of the current page, the file size and path, whether the document was repaired, and whether it is encrypted. **XMP is not read, not shown and not round-tripped**, and nothing is editable. | The FileMap's `Metadata` row promises *"Info dictionary and XMP as one editable model, with round-trip preservation of XMP we do not understand"* — and every clause after *"Info dictionary"* is about **writing**, which is Stage 8's. Reading an XMP packet to display it duplicates the Info fields for most documents and disagrees with them for some, and deciding which wins is a policy that belongs with the writer that must keep both consistent (D3-5 left `/Info` and XMP untouched on save for exactly that reason). A properties dialog that shows what it read is honest; one that shows a merge nobody specified is a second source of truth. | Stage 8, with `/ModDate` policy and the XMP writer |
| **D4-5** | **Stage 4 declares NINETEEN settings keys — corrected at T13, 2026-09-23, from the EIGHTEEN this row's own headline claimed while its own enumeration below listed nineteen, which itself corrected at T8 the FIFTEEN this row first claimed and the seventeen `Implementation-Plan.md` listed. Re-derived from `core/settings/SettingsKeys.def` rather than carried: 53 keys in the table, of which `units/` (11) and `fullScreen/` (5) are wholly Stage 4's, plus `pageDisplay/autoScrollSpeed` and the two `general/` keys T13 added; the two EXCLUSIONS below are what this decision actually owns and they are untouched.** The count moved because the three documents disagreed before any code existed (this row said fifteen, §4's manifest listed seven `units/*` keys and §6 said eight) and because building it settles the arithmetic: **the grid ORIGIN is one fact in two rows** — there is no point codec, and a single `units/gridOffset` holding "12,18" is the compound value convention 4 forbids — and **`showRulers`, `showGrid` and `showGuides` are three visibility rows**, because `Ctrl+U`, `Ctrl+'` and `view.toggleGuides` have to write their state somewhere or the toggles do not survive a restart. Eleven `units/*`, one `pageDisplay/`, five `fullScreen/`, two `general/` — **which is nineteen, and the arithmetic is the correction**. `pageDisplay/continuousScroll` is **not declared** — `pageDisplay/layoutMode` already encodes it, `SingleContinuous` against `Single` — and `units/snapToObjects` **moves to Stage 6**, with the objects it would snap to. Two Preferences pages ship, **Units & Guides** (T8, seven of 26) and **Full Screen** (T10, eight of 26). | A second key for the same fact is two truths that agree until the first Friday, which is `SettingsKeys.def`'s own rule 2 written across keys rather than across call sites: a `continuousScroll` that disagreed with `layoutMode` would make *"which one does the view obey"* a question with no answer in the table. `snapToObjects` follows **D3-9** exactly — a control that reads back as set and does nothing is Stage 2's I-11 by construction rather than by constraint, and at Stage 4 there is no annotation, field or content object to snap to. The snap *resolver* still takes an object-edge list (D4-6), so Stage 6 adds a source and a key, not an algorithm. | Stage 6 (`snapToObjects`); never for `continuousScroll` |
| **D4-6** | **Snapping resolves in a fixed precedence — object edges, then guides, then grid — with ONE threshold expressed in DEVICE pixels and converted into page space per view.** `GridGuideLayer::snap(...)` takes the object edges as an argument; the product passes an **empty** span until Stage 6, and every case that exercises the object branch says at its assertion that no product surface reaches it (convention 19). | A threshold in page units is a threshold that grabs from a centimetre away at 50 % and needs a steady hand at 400 %; the whole point of *"behaves identically at 50 % and 400 %"* (the stage's exit criterion) is that the tolerance is what the **hand** does, and the hand works in screen pixels. Taking the edges as a parameter rather than reading a store is what lets the precedence — the part that is easy to get wrong and impossible to see in a screenshot — be tested exhaustively now, over synthetic geometry, instead of in Stage 6 under a drag. | Stage 6 supplies the edges; the arithmetic does not change. **As built (T8), the arithmetic is `core/Snap.h` and the signature is `GridGuideLayer::snap(int pageIndex, core::PdfPoint, SnapFlags, std::span<const core::SnapEdge>, double scale)`** — §5.1 and §5.6 of this plan put `tst_snap` in `rawpage_tests`, which links no display, so the sums cannot live in a widget class; and a bare `QPointF` cannot say whether it is page space or device space, which is the single bug `core/Geometry.h` exists to prevent |
| **D4-7** | **Guides are VIEW state and are never written to the document.** They live in `SessionState`, keyed by `DocumentId`, and survive a close and a relaunch; nothing about them reaches `pdf/`. | PDF has no representation for a layout guide. Writing one would mean inventing a private key or an invisible annotation — a document mutation, which D4-1 says this stage does not make, and which Stage 10's sanitize path would then have to know about (the argument that settled **O2**). Keying them by `DocumentId` rather than by path means a different file at the same path does not inherit them, which is the identity Stage 3 already computes at open (D3-16). **CORRECTED AT T13, 2026-09-23 (guide I-01): this column also claimed *"a renamed file keeps its guides"*, and that does not follow from `DocumentId` and never did** — the normalized path is one of its four facts, so a rename loses them. The other clause is true and is the whole reason a bare path would not do. **The save half of I-01 was never a defect in the key**: `app::SessionShell` reads the identity at CAPTURE, where `Document::refreshFileState` has already re-read it after every save, so a save during the session cannot orphan a guide; a file changed by something else between the quit and the next launch loses its guides and keeps its document. | Never — unless a later stage adds a document-embedded guide format, which would be a new decision |
| **D4-8** | **Window geometry is stored per MONITOR-ARRANGEMENT FINGERPRINT and clamped on restore.** The fingerprint is a hash over `IDisplayTopology::monitors()` — each monitor's `id`, position, pixel size, `scaleFactor` and `primary` flag, sorted by `id`. `SessionState` keeps the last geometry for each fingerprint it has seen (bounded, most-recent-first); an **unknown** fingerprint restores the most recent geometry it has and then **clamps every window into the current virtual desktop**, so a window is always at least partly on a monitor that exists. | The common case is not exotic: a laptop docked to two monitors at the office and undocked on a train produces two arrangements a day, and Qt will happily restore a window to *x = 3200* on a desktop 1920 wide. Storing one geometry and clamping would lose the dual-monitor layout every time the user undocked; storing per arrangement and **not** clamping would put a window off-screen the first time a monitor moved within an arrangement it thinks it knows. The clamp is the backstop that makes a wrong fingerprint a cosmetic error rather than an unreachable window. | Never; the bound on how many arrangements are remembered may move |
| **D4-9** | **Session restore reopens DOCUMENTS at their saved state; it never restores unsaved work, and it runs AFTER `RecoveryPrompt` and skips any path recovery has already opened.** `general/restoreSession` (declared at Stage 1, read by nothing) is its first reader. A document closed with *Don't Save* comes back **clean**, at the bytes on disk. | Unsaved work has an owner — the recovery journal (**D3-11**), which is flushed per command and offered after a crash — and two mechanisms that both claim to bring back an edit would eventually both bring it back, i.e. twice. Ordering restore after the prompt is what makes *"skip what recovery opened"* possible at all; the alternative, opening the file and then replaying into it, is the *recover into a file that has changed* path D3-11 deliberately routes to an untitled document. The *Don't Save* clause is the control: a person who declined to save and sees their edit again has been told the product lies. | Stage 13, if Write mode's autosave changes what "unsaved" means |
| **D4-10** | **Thumbnails ride the shipped pipeline: `RenderScheduler` at `RenderPriority::Speculative`, the shared `RenderCache`, one worker. The pane owns no rasterizer, no second cache and no thread — and it is a VIEWPORT, enqueueing only the thumbnails it can show plus a margin, never the document.** | `RenderPriority::Speculative` was declared at Stage 2 with the comment *"Stage 4's thumbnails"* and has been exercised only by unit cases: this stage is what it was for, and strict priority is what keeps a 900-page pane from starving the page the user is reading (`RenderScheduler.h`: *"speculative work that runs instead of visible work is the defect"*). A second cache would fragment the 256 MiB budget **O7** resolved to keep whole, and its answer — *sharing is free until the cap is too small* — was measured over one workload per document, which Stage 4 changes. Enqueueing 900 thumbnails at once would spend that cap on pages nobody is looking at, and the pane's own scroll position is the natural bound. | T2's measurement may force a per-pane cap; a larger change is Stage 5's, which adds the next cache consumer |
| **D4-11** | **Rulers, the grid, the guides and the page-colour transforms are applied at PRESENTATION, over the tiles — never composed into a raster and never in the cache key.** A grid line, a guide, a ruler and an inverted or substituted page colour are all painted by `ui/viewer/` over what the scheduler returned. `RenderFlags::Grayscale` stays a render flag. | This is Stage 2's transparency-grid argument (`TilePresenter`) applied twice over, and it cuts both ways: the transparency grid **had** to be a render flag because a checkerboard behind an opaque page is invisible, while a grid **over** the page is a paint operation and composing it into the tile would put the grid's spacing, colour and offset into the cache key — so dragging the grid spacing slider would re-rasterize the document. Colour inversion is a per-pixel map over pixels we already have; making it a key would double the cache for a toggle, and the toggle must be instant. Grayscale is different and stays where it is: PDFium's grey is a colour conversion inside the rasterizer, not a map we can apply afterwards. | Stage 6, when an annotation's appearance makes "what is in the tile" a real question again |
| **D4-12** | **Full Screen and Presentation are ONE controller with two levels.** Level 1 (`F11`, `view.fullScreen`) hides the chrome and keeps the viewer's tools; level 2 (presentation) adds transitions, auto-advance, blank-to-black/white and the laser pointer, and declares `KeyContext::FullScreen`. Entering **either** level calls `IPowerSession::setPresentationModeActive(true)` — its first caller — and leaving calls it with `false`, including on an abnormal exit path. The monitor comes from `IDisplayTopology`, joined to `QScreen` by `friendlyName` (**I-20**); a topology change while presenting **moves the presentation to the primary monitor** rather than leaving it on one that is gone. | Two controllers would mean two answers to *"is the screen saver allowed to start"*, and the window state is already one thing at the OS level. The screen-saver call on level 1 as well as level 2 is deliberate: a person reading full-screen for ten minutes without touching the mouse is exactly whom a lock screen interrupts, and the interface has one switch. `MainWindow::toggleFullScreen` — which is today *"the WINDOW STATE ONLY"* and says so — becomes this controller's level 1 and keeps its `ActionId`. | Stage 16, if a presenter view (notes on one monitor, slides on the other) is ever built |
| **D4-13** | **A split is N `PageView`s inside ONE `DocumentWindow`, over one handle, one `Document`, one `CommandJournal` and one revision. `SplitViewContainer` owns views, never documents. Scroll synchronization is an option on the container, and the spreadsheet split is a constrained scroll — a frozen row and column are views whose scroll is derived from the main view's, not a second document.** **Amended 2026-09-27 (guide I-18 and I-19, M-08's first walk): in a spreadsheet split the main view leads EVERYTHING, not only the scroll** — its zoom, its fit (copied as the zoom it produced, never as a fit), its layout mode and its view rotation are the frozen panes' too, and the freeze is a place in the document rather than a pixel offset; a frozen pane takes no focus and shows no scrollbar, and its gestures — the wheel, the hand, a zoom about the cursor — are the main view's BEFORE they move anything (guide I-20: measuring the frozen pane's movement afterwards failed at the document's edge and ran away under the hand), while anything else that moves one is undone. | `DocumentWindow`'s destructor holds the three-step close protocol that keeps a worker out of a document being destroyed (`Architecture-FileMap.md` §3.13), and a split that opened a second handle would need a second copy of that protocol and would double the document's memory to show the same pages. One journal is also what makes *"the same document in two panes, one of them rotated"* correct rather than a race. **Two views of the same document in two WINDOWS** (`window.new`, Features §1.1) is a different thing and is T11's, where the second window has its own handle by necessity. | Stage 5, when a comparison view puts two *different* documents side by side |
| **D4-14** | **Fit Visible gets a real content box, closing Stage 2's I-28.** `pdf/render/` gains a per-page **content bounding box** — the union of the page's object bounds, taken through the engine's C API and cached per page on the handle — exposed on `IDocumentBackend` beside the media and crop boxes; `core/render/` decides the fit from it, falling back to the CropBox when a page has no objects (an empty page) or the engine cannot answer. `corpus/ordinary/cropbox-margins.pdf` (T1) is the first file on which the three readings — MediaBox, CropBox, content — are three different rectangles. | I-28 is open, it is in this stage's area, and its *"what is absent, and who owns it"* line names exactly these two things. Computing the box in `pdf/render/` rather than in `core/` is the layering rule: the object bounds come from `FPDF_*`, which nothing outside that directory may name (D2-9). Caching it per page matters and is not free — a 900-page document asked for every content box would walk every page's objects — so the box is computed on demand, for the page being fitted, and kept. **What this does not do:** change what Fit Page or Actual Size mean, or introduce a `/CropBox` anywhere in the writer. | Never; the fallback list may grow as pathological pages appear |
| **D4-15** | **Two chords `Implementation-Plan.md` § Stage 4 lists do NOT ship here, and its list is corrected in the same change as this plan:** `Ctrl+4` (`view.reflow`) needs `core/text/LayoutAnalyzer` and is **Stage 5's**; `S` (`tool.snapshot`) needs `platform::IRichClipboard`, which is **Stage 5's** (`WinRichClipboard`) and whose own component list already names `ui/viewer/tools/SnapshotTool`. Both are added to Stage 5's *Key bindings handled* in the same change, and both stay unregistered here and leave their key unconsumed. **`Ctrl+E`** (`view.togglePropertiesBar`, a Viewer row) is named here only so it is not expected: Stage 4 does not list it and does not build it — a contextual properties bar has nothing to put in it until something is selected, and Stage 6's entry already carries the chord. | Each is a §4 entry written before the stage that owns the *dependency* was detailed, and Stage 5's own component list is where one of them already is — the roadmap contradicts itself in exactly one direction, and this is the correction rather than a scope cut. Shipping a Reflow view over no text model would mean building a second text layout in the stage before the one that builds the first. `Features.md` §8 is untouched: the chords are right, their stage was wrong. | Stage 5 |
| **D4-16** | **A2 is re-measured for Stage 4's workloads at T2, BEFORE the panes exist, through `tests/bench/bench_pipeline`, and the verdict decides `platform::IAcceleratedSurface`.** The new mode issues, through the shipped scheduler and cache, the request pattern a viewer plus a thumbnails pane plus a split view produce over `pages-900.pdf` and `mixed-page-sizes.pdf`, and reports the **viewport's** p95 queued latency under that load — the same quantity D2-1's product figure names, so the two are comparable. The result is recorded in `Tech-Stack-Plan.md` §3.3 beside A2's existing figures, and `WinAcceleratedSurface`'s ○ (S4) row is answered: built, or re-tagged with the measurement that says it is not needed. | Stage 3 put its go/no-go second for this reason and it worked: *"a no-go costs the writer, not the stage"*. Here a no-go costs the **pane's design** — a per-pane cap, a coarser thumbnail, a serialized lane — and learning it after T5 means rebuilding the pane under a finished sidebar. Measuring through the bench rather than through the UI is what makes it possible before the pane exists: `bench_pipeline` already drives the real scheduler and cache with no window, which is how D2-14 and O7 were answered. **The probe-versus-product lesson is honoured**: this is the *product* quantity measured with a synthetic request pattern, and T14 re-reads it through the real panes before the verdict is final. | Stage 5, the next new cache consumer |
| **D4-17** | **`WindowArranger` arranges THIS process's windows with Qt geometry and adds no `platform/` interface.** Tile horizontally/vertically, cascade, the window list and *merge all windows* (which re-docks every detached tab into one window) are all operations over `MainWindow`s this process owns. | `Tech-Stack-Plan.md` §4.3's rule is that what Qt does cross-platform gets no interface, and placing our own top-level windows is `QWidget::setGeometry` over `QScreen::availableGeometry()`. The OS-specific half — *which* monitor, at what scale — is `IDisplayTopology`, which already exists. An interface here would be a Windows dependency for arithmetic. | Never |
| **D4-18** | **A document window has ONE pair of rulers, and in a split they measure the FOCUSED pane.** The rulers sit on the outer edge of the whole split surface (`ui/shell/DocumentWindow`'s grid), and on `SplitViewContainer::activeViewChanged` they rebind: zero at the focused pane's page corner, offset by that pane's position in the split (`DocumentWindow::viewportOffset()`), with the cursor marker, a guide dragged off a ruler and the guide menu following the same pane. **Over an unfocused pane the ruler CONTINUES the focused page's scale** -- the shaded extent is the focused page -- so a reading there is a distance from the focused page's corner, not a measurement of the pane underneath. | **Recorded 2026-09-26, after the fact, and chosen by the user.** T9 built it as deviation (f) of its `Implementation-Plan.md` row -- `DocumentWindow::view()` means the active pane -- without a decision here, and the user asked during M-01 whether one ruler in a split was intended. Rulers per pane would put eight rulers round a spreadsheet split, four of them measuring frozen strips, and would make a guide drag choose between rulers of panes that show the same page; one pair following focus is what every one of the twenty-five `Features.md` §8 Viewer commands already does, so the rulers agree with everything else per-view in the window. **The cost is the unfocused pane**, whose ruler reads the focused page -- judged by a person in guide case M-08. **In a spreadsheet split the focused pane is always the body** (2026-09-27, guide I-19): the frozen panes take no focus, so the rulers measure the body -- and because the frozen row shares the body's x and the frozen column its y, the horizontal ruler reads the row and the vertical one the column as well. M-08's first walk judged the two-pane case readable. | Stage 5's comparison view, where two DIFFERENT documents side by side would each want their own scale |
| **D4-19** | **The sidebar's open pane belongs to the WINDOW, not to the document.** One `SidebarDock` per window, one pane in front; switching tabs keeps it, and only its CONTENTS follow the document in front (T5's panes are re-pointed, never rebuilt). Nothing records which pane each document had open, and the session stores the pane layout per window (`MainWindow::paneLayoutState`). | **Recorded 2026-09-27, after the fact, and chosen by the user.** `Features.md` §1.1 lists *"Remember which panes were open, globally and per document"* and §9.2 *per-document open panes*, `SidebarDock`'s FileMap row carried *per-document open-pane state* as owed since Stage 1, and guide case M-10 expected *"each document keeps its own open panes"* — but no Stage 4 task was given it and none built it. M-10's walk showed the per-window behaviour and the user judged it the expected one: *"this is work as expect. Only the last choice of sidebar is shown after switch between tabs."* Recorded as this stage's decision rather than as a removal from the product: `Features.md` is a catalog and decides no scope, and nothing was said about the feature for good. | If a later stage finds a reason one document needs a different pane in front than another — Stage 7's comment and form panes are the likeliest |
| **D4-20** | **The Loupe's lens is SHARP: it re-rasterizes its own region at the magnified scale**, and shows the enlarged frame only until that raster arrives. The lens appears at once as before (the last frame enlarged, so it never lags the pointer); once the lens's region has been still for ~100 ms it asks the SHIPPED `RenderScheduler` — through a third client beside `TilePresenter` and the thumbnails pane, `ui/viewer/LoupeRaster` — for exactly that region of the page at `presentedScale × kMagnification`, in the **Visible** lane, one request at a time, the previous one CANCELLED when the region changes; the arrived raster is drawn into the lens with the page-colour inversion and the grid and guides composed over it at the same magnification, so the lens shows what the page shows, only sharper. The request is built by `TilePresenter`'s own request builder (its flags, revision and unsaved-rotation delta), made public for it, so there is one place a request is shaped. | **Recorded 2026-09-27, on the user's instruction during M-12, and it REVERSES T7's choice**: `ui/viewer/tools/LoupeTool.h` had recorded *"the lens magnifies the last painted frame … it makes small things bigger, and it does not make blurry things sharp. A loupe that re-rasterized is a different feature and would be measured against A2 before it was built."* The user: *"I want it to be sharp in len, not just enlarge the pixel."* So it is measured against A2 as that sentence required — a `+ loupe` column in `bench_surface`, whose lens renders share the graded Visible lane (guide case A-100). The progressive design keeps T7's reason for the frame: the lens never waits for the engine. | If the measurement leaves the Visible lane's p95 outside the 16.667 ms budget, the lens moves to the `Prefetch` lane — which T11's two-window column showed keeps the viewport's lane its own — and is re-measured |
| **D4-22** | **`Alt`+wheel sets the Loupe's magnification, and the ribbon's key tips move to `Alt`'s RELEASE.** While the Loupe is the tool in effect, `Alt`+wheel over the page steps the lens's magnification along a fixed ladder — 1.5, 2, 2.5 (the default), 3, 4, 5, 6, 8 — one rung a notch, wheel away for more, clamped at both ends; it scrolls and zooms nothing, the lens shows its magnification in a corner, and the sharp lens (D4-20) re-asks at the new scale once still. The magnification belongs to the view for the session and is not a setting. **Because a lone `Alt` is `Features.md` §8's key-tips row**, which fired on the key going DOWN and took focus into the ribbon, key tips now show on `Alt`'s **release**, and only when nothing else happened while it was held — no other key, no mouse button, no wheel — which is Windows' own menu convention; so an `Alt`+wheel never shows them. | **Recorded 2026-09-27, chosen by the user during M-12**: *"Use alt+wheel to adjust zoom level of lens"*, and, offered the conflict with the key tips, *"Key tips on Alt release"*. It changes the TIMING of Stage 1's key tips (Stage 1 guide I-42, criterion R-5 in `Stage1-AppShell-Plan.md` §5.3), not what they do; the §8 row is annotated rather than changed. Qt turns an `Alt`+wheel into a horizontal one on some platforms, so the step reads whichever axis moved. | If a later stage binds `Alt`+wheel elsewhere, or if the release-time key tips feel late to a person — Stage 1's key-tips walk is where that would be seen |
| **D4-21** | **The wheel's zoom modifier is `Ctrl`, fixed.** `pageDisplay/wheelZooms` swaps what the bare wheel and `Ctrl`+wheel do; no setting chooses a different modifier. | **Recorded 2026-09-27, chosen by the user during M-12.** `Features.md` §1.2 lists *"Choose whether the wheel scrolls or zooms, and which modifier swaps the two"*; T7 built the first half, and the user judged the second not wanted: *"holding Ctrl with scroll as zoom modifier."* A stage decision rather than a removal from the catalog, as D4-19 is. | If a later stage binds `Ctrl`+wheel to something else in some context |

---

## 4. File manifest

Every file Stage 4 creates or turns from ○, with the [`Architecture-FileMap.md`](./Architecture-FileMap.md)
row it must flip **in the same change**. Rows marked ⚠️ have no FileMap row yet and are lockstep debt
(§10), paid with the file.

| File | FileMap row | Task |
|---|---|---|
| `corpus/ordinary/cropbox-margins.pdf`, `dests-named.pdf`, `transitions.pdf` | §3.18's `ordinary/` row; three `PROVENANCE.md` rows; `golden/manifest.json` rows | T1 |
| `tests/bench/bench_pipeline.cpp` — the Stage 4 workload mode | §3.17's `tests/bench/` row (D4-16) | T2 |
| `core/settings/SessionState.{h,cpp}` | §3.10 ○ (S4) → ✅ | T3 |
| `core/doc/Outline.{h,cpp}` | §3.2 ○ (S4 read, S7 edit) → 🟡 (S4 read ✅, S7 edit) | T4 |
| `core/doc/Destinations.{h,cpp}` | §3.2 — **re-tagged ○ (S7) → ○ (S4 read, S7 edit)** when this plan was written (D4-3), then → 🟡 | T4 |
| `core/doc/Metadata.{h,cpp}` | §3.2 ○ (S4) → 🟡 **(S4 Info read ✅, S8 XMP and editing)** (D4-4) | T4 |
| `core/backend.h` — `outline()`, `destinations()`, `metadata()`, `contentBox()` | §3.5's row; the implemented-interface census is unchanged (these are `IDocumentBackend` members, not new interfaces) | T4, T12 |
| `pdf/render/PdfiumDocument.{h,cpp}` — the three readers and the content box | §3.10's rows | T4, T12 |
| `ui/shell/SidebarDock.{h,cpp}` — the pane registry | §3.13's row: *"still owed: real panes (S4+)"* is paid | T5 |
| `ui/panes/ThumbnailsPane.{h,cpp}` | §3.15 ○ (S4) → 🟡 (S4 navigate ✅, S7 page ops) | T5 |
| `ui/panes/BookmarksPane.{h,cpp}` | §3.15 ○ (S4 view, S7 edit) → 🟡 | T6 |
| `ui/panes/DestinationsPane.{h,cpp}` | §3.15 — **re-tagged ○ (S7) → ○ (S4 read, S7 edit)** (D4-3), then → 🟡 | T6 |
| `ui/viewer/ToolController.{h,cpp}` | §3.14 ○ (S4) → ✅ | T7 |
| `ui/viewer/tools/{Tool.h,HandTool,SelectTool,MarqueeZoomTool,LoupeTool}.{h,cpp}` | §3.14's `tools/*` row ○ (S4–S10) → 🟡, naming the four | T7 |
| `core/Snap.{h,cpp}` — the resolver's arithmetic (D4-6) | ⚠️ **no row when this plan was written; added with the file at T8** (§3.2's `core/` family) | T8 |
| `ui/viewer/RulerWidget.{h,cpp}`, `ui/viewer/GridGuideLayer.{h,cpp}` | §3.14 ○ (S4) → ✅ | T8 |
| `ui/viewer/SplitViewContainer.{h,cpp}` | §3.14 ○ (S4) → ✅ | T9 |
| `ui/viewer/FullScreenController.{h,cpp}` | §3.14 ○ (S4) → ✅ | T10 |
| `ui/shell/WindowArranger.{h,cpp}` | §3.13 ○ (S4) → ✅ (D4-17) | T11 |
| `ui/dialogs/DocumentPropertiesDialog.{h,cpp}` | §3.15's dialog-family row: named there as *S2–S3*, **corrected to S4** with the file (D4-4); **built at T12, which gave it a row of its own** beside `RotatePagesDialog`'s and `RecoveryDialog`'s, the family row keeping a ⤴ breadcrumb | T12 |
| `core/render/FitPolicy.{h,cpp}` — which rectangle a fit measures, and the zoom that satisfies it (D4-14) | ⚠️ **no row when this plan was written; added with the file at T12** (§3.4's `core/render/` family). §4 named the backend accessor and the dialog and not the policy, because the plan expected the arithmetic to stay in `ui/viewer/PageLayoutEngine` — it could not: only `rawpage_render_tests` can open a corpus file and it links no widgets, so I-28's own observable was unassertable until the fit moved | T12 |
| `ui/shell/MainWindow`, `DocumentWindow`, `StatusBar` — pane hosting, the split, reading mode, the coordinate readout, logical page labels | §3.13's rows | T5 … T12 |
| `app/Application.{h,cpp}` — session capture on quit and restore on launch | §3.1's row: *"still owed: session save/restore on quit (`SessionState` is Stage 4)"* is paid | T13 |
| `tests/ui/tst_panes.cpp`, `tst_tools.cpp`, **`tst_rulers.cpp`** (T8; this row said `tst_snap.cpp`, which §5.6 correctly puts in `rawpage_tests` — corrected with the file), `tst_split.cpp`, `tst_presentation.cpp`, `tst_arranger.cpp` | §3.17's `tests/ui/` row | T5 … T11 |
| `tests/unit/tst_sessionstate.cpp`, `tst_outline.cpp`, `tst_metadata.cpp`, **`tst_snap.cpp`** | §3.17's `tests/unit/` row | T3, T4, T8 |
| `tests/composition/tst_restore.cpp` | §3.17's `tests/composition/` row | T13 |
| `tests/bench/bench_surface.cpp`, and `core/render/RenderScheduler`'s `setLatencyProbe` | §3.17's `tests/bench/` row and §3.4's scheduler row | **T14, and neither was foreseen here.** §8 said T14 would *re-read* T2's figure and assumed `bench_pipeline` could do it; it cannot — its Stage 4 mode issues the request pattern ITSELF, which is the one thing T14's subject forbids. Measuring the shipped widgets needs a composition bench of its own and a way to time an enqueue nothing in the harness makes |
| `ui/shell/MainWindow` — `paneLayoutState()`; `ui/shell/DocumentWindow` — `splitState()`; `core/settings/SessionState` — `defaultFilePath()` and `SessionDocument::splitState` | §3.13's and §3.10's rows | **T13, and none of the three was foreseen here.** The session stores a window's panes and a tab's split as opaque strings (`core/` is not taught a dock or a split-mode vocabulary), so each needs an encoder where its vocabulary lives; and `SessionState` had no `defaultFilePath()` because T3 had no product caller to resolve one for |

**Settings keys** — **nineteen** (D4-5, corrected at T13 from the eighteen its headline said while its list said nineteen, which had itself corrected fifteen at T8; the membership is listed here
rather than left to be re-derived), each a row in `core/settings/SettingsKeys.def` and therefore an
automatic Preferences editor (convention 17): `pageDisplay/autoScrollSpeed`; the **eleven**
`units/rulerUnit`, `units/showRulers`, `units/showGrid`, `units/showGuides`, `units/gridSpacing`,
`units/gridSubdivisions`, `units/gridColour`, **`units/gridOriginX`** and **`units/gridOriginY`**
(the origin is one fact in two rows — there is no point codec), `units/snapToGrid`,
`units/snapToGuides`; `fullScreen/transition`, `fullScreen/autoAdvanceSec`, `fullScreen/loop`,
`fullScreen/monitor`, `fullScreen/showCursor`; `general/rememberPaneLayout`,
`general/rememberWindowLayout`. The **Units & Guides** (T8) and **Full Screen** (T10) pages ship —
**eight of 26**. `general/restoreSession` gains its first reader and is not re-declared.

**Catalog families** — `i18n-lint`'s `keyFamilies` gains **`panes`**; the full-screen and units
strings live in the existing `shell.` and `prefs.` families, as Stage 3's recovery strings turned out
to (Stage 3 guide **I-16**: a family is added when a key's namespace is new, not when a feature is).

---

## 5. Component design

Only the pieces whose shape is not obvious from a one-line component entry.

### 5.1 The library graph after Stage 4

Unchanged. Stage 4 adds no library and no `platform/` interface: `rawpage_core` gains
`core/settings/SessionState` and three `core/doc/` readers, `rawpage_pdfium` gains the facade halves
that hydrate them, and everything else is `rawpage_ui`. **`rawpage_assert_headless()` is unchanged**,
and the `core/` half of this stage — `SessionState`, the snap resolver's arithmetic, the fingerprint —
builds and tests with no display, which is where most of Stage 4's provable behaviour lives.

### 5.2 Where Stage 4 state lives, and why that is the whole design

```
                        the FILE                    (pdf/ -- Stage 4 writes nothing)
  ────────────────────────────────────────────────────────────────────────────────
  outline, destinations, Info        read once at open, hydrated into core/doc/   (D4-3)
  ────────────────────────────────────────────────────────────────────────────────
                        the SESSION                 core/settings/SessionState    (D4-8, D4-9)
  open documents · tab order · window geometry per fingerprint · pane layout
  per-document: guides, layout mode, zoom intent, scroll position                 (D4-7)
  ────────────────────────────────────────────────────────────────────────────────
                        the PREFERENCES             core/settings/Settings        (D4-5)
  units, grid, snap, full-screen and presentation defaults
  ────────────────────────────────────────────────────────────────────────────────
                        the WIDGET                  ui/ -- dies with the window
  which tool is armed · the split's sizes · what is peeking · the laser pointer
```

A fact belongs one row lower than the place it is first needed, never one row higher. The two
mistakes this layout prevents are a guide written into the file (D4-7) and a zoom written into the
preferences, where it would become every document's zoom.

### 5.3 One navigation, as it will actually run

```
click a thumbnail ─▶ ThumbnailsPane::activated(pageIndex)
 └─ pane declares KeyContext::PageThumbnails            (ShortcutBinder::declareContext, D1-5)
     └─ it does NOT touch the Document: it asks the view to go there
         └─ DocumentWindow::goToPage(index, Reason::Pane)
             ├─ ViewportController::scrollToPage(index)      view history records the jump
             ├─ PageView repaints from tiles already cached where it can
             └─ StatusBar page box shows the LABEL, not index+1     (outline-labels.pdf)
                  └─ the pane's selection follows the view, and the view does not follow
                     it back -- one direction per gesture, or the two chase each other
```

### 5.4 The thumbnail request, and what bounds it

```
ThumbnailsPane scrolled ─▶ visible thumbnail slots + one screen of margin
 └─ for each slot: RenderRequest{ page, thumbnail scale, flags=None, rotationDelta } (D3-15)
     └─ scheduler.enqueue(request, RenderPriority::Speculative)          (D4-10)
         ├─ a visible-priority tile always goes first                    RenderScheduler.h
         ├─ the shared RenderCache answers a hit without a worker         O7's one cache
         └─ scrolled away ─▶ cancel the ticket, as TilePresenter does
```

**What must be measured rather than assumed** (T2, D4-16): whether 20–40 speculative requests in
flight move the *viewport's* p95 at all, and what the cache does when a 900-page pane's thumbnails
compete with a split view's two viewports for 256 MiB.

### 5.5 Session capture and restore

```
quit (clean) ─▶ Application::captureSession()
 ├─ per window: geometry, maximized state, tab order, active tab
 ├─ fingerprint = hash(sorted monitors: id, x, y, w, h, scale, primary)   (D4-8)
 └─ per document: path, DocumentId, layout mode, zoom intent, scroll, guides (D4-7)

launch ─▶ main.cpp
 ├─ 1. RecoveryPrompt::checkAndOffer(...)          crashed sessions first    (D3-11)
 ├─ 2. command-line files
 └─ 3. SessionState::restore(...) when general/restoreSession       (D4-9)
       ├─ skip any path already open (recovery or the command line)
       ├─ geometry for THIS fingerprint, else the most recent
       └─ clamp every window into the current virtual desktop
```

**A document that no longer exists is reported once and dropped from the session**, not offered — it
is not an error, it is a file that moved, and the next capture will not carry it.

### 5.6 Test targets after Stage 4

| Target | Links | Stage 4 suites | Runs where |
|---|---|---|---|
| `rawpage_tests` | unchanged | `tst_sessionstate`, `tst_outline`, `tst_metadata`, `tst_snap` (the resolver's arithmetic), `tst_fingerprint` | Windows + `portable-asan` |
| `rawpage_ui_tests` | unchanged | `tst_panes`, `tst_tools`, **`tst_rulers`** (T8 — a second Stage 4 suite this table did not name: `tst_snap` is headless by design and can hold no case about a ruler, a drag or a repaint), `tst_split`, `tst_presentation`, `tst_arranger`, plus the census edits in `tst_viewercommands` and `tst_context` | Windows, `offscreen` |
| `rawpage_render_tests` | unchanged | `tst_contentbox` (D4-14, the engine half), the thumbnail request's raster | Windows |
| `rawpage_composition_tests` | unchanged | `tst_restore` — quit, relaunch and restore through `CompositionRoot` with a scratch `SessionState` | Windows |
| `rawpage_platform_tests` | unchanged | the presentation's `IPowerSession` call and the topology join | Windows |
| `gates.` | no binary | unchanged in kind; the census cases move with the numbers | `gates` preset |

**A composition case must point `SessionState`'s file at a scratch directory**, for the reason
Stage 3 T11 learned about the recovery root: a suite that wrote the tester's session would reopen
their documents. The rule is already in `CLAUDE.md` → *Traps*; Stage 4 gives it a second subject.

---

## 6. Task breakdown

Ordering is risk-first. **T2 comes before any pane, split or presentation code**: a no-go on the
Stage 4 workload measurement reshapes the thumbnails pane, and learning it after T5 means rebuilding
the pane under a finished sidebar. Stages 2 and 3 put their go/no-go third and second for the same
reason.

| # | Task | Depends on | Content | Done when |
|---|---|---|---|---|
| **T1** | **The corpus this stage can be judged on** | — | `ordinary/cropbox-margins.pdf` (a `/CropBox` inset well inside the `/MediaBox`, with wide white margins **and** ink that stops short of the CropBox, so sheet, crop and content are three rectangles — D4-14); `ordinary/dests-named.pdf` (a `/Names /Dests` tree and an old-style `/Dests` dictionary, destinations of three kinds — `/XYZ`, `/Fit`, `/FitH` — and **one that names a page that does not exist**, as the control); `ordinary/transitions.pdf` (per-page `/Trans` of three kinds, a `/Dur`, and `/PageMode /FullScreen`); `PROVENANCE.md` rows; `golden/manifest.json` rows | `make-corpus.py --verify`, `--check-provenance` and `--self-test` green; **70 files regenerating byte-identically**; `golden-update --check-manifest` green with the three new files cases or excluded with a reason |
| **T2** | **The Stage 4 workload measurement, and the `IAcceleratedSurface` verdict** | T1 | `bench_pipeline`'s Stage 4 mode (D4-16): a viewport's requests plus a thumbnail stream at `Speculative` plus a second viewport, over `pages-900.pdf` and `mixed-page-sizes.pdf`, at the shipped cache cap and at a small one | The **viewport p95** is reported beside Stage 2 T18's figure, with the cache's hit rate, evictions and cross-document evictions; **the verdict is recorded** in `Tech-Stack-Plan.md` §3.3 and `WinAcceleratedSurface`'s row is answered. A no-go reshapes T5's design **before** T5 |
| **T3** | `core/settings/SessionState` | — | D4-8's fingerprint and clamping; D4-9's restore rules; the on-disk form and its schema version; guides (D4-7) | `tst_sessionstate` and `tst_fingerprint` green **headless**: a round trip; an unknown fingerprint falling back; every window clamped onto a desktop one third the size; a missing file dropped with one report; **the control** — a geometry that is already on-screen is not moved |
| **T4** | `core/doc/` read-only models | — | D4-3's `Outline`, `Destinations` and `Metadata` (Info only, D4-4); the three facade readers | `tst_outline`, `tst_metadata` green over `outline-labels.pdf` and `dests-named.pdf`: the two-level tree with its open/closed state; three destination kinds resolved to a page and a point; **the dangling destination reported and not resolved**; Info fields read, and a document with no `/Info` answering empty rather than failing |
| **T5** | `SidebarDock` panes + `ThumbnailsPane` | T2, T4 | D4-2's pane registry; D4-10's viewport-bounded `Speculative` stream; selection, sizes (`+`/`-`), `Enter`, arrows; `KeyContext::PageThumbnails`; `Ctrl+R` reaching Stage 3's `page.rotate` | `tst_panes` green: the pane declares its context and the chords resolve **from a key event**; the request census shows **only visible slots plus the margin** enqueued over `pages-900.pdf`, all at `Speculative`; clicking a thumbnail moves the view and the view moves the selection back **without a loop** |
| **T6** | `BookmarksPane`, `DestinationsPane` | T4, T5 | Navigation only (D4-2); the whole §8 Bookmarks table's **navigation** rows — `Enter`, `→`/`←`, `*`/`/`; the editing rows stay unregistered | Both panes navigate; expand/collapse-all over a two-level tree; `Ctrl+Shift+B` and the destinations pane open from a key event; **every editing chord in the Bookmarks table is still `NotImplemented` and leaves the key alone** (the control) |
| **T7** | `ToolController` and the four tools | T5 | D4-1; the sticky-tool mode, the temporary-hand modifier, `Esc` cancellation, cursors; `HandTool`, `SelectTool` (which still selects nothing — Stage 5), `MarqueeZoomTool`, `LoupeTool`; auto-scroll with `pageDisplay/autoScrollSpeed` | `tst_tools` green: `H`/`V`/`Z` arm from a key event; the modifier pans without disarming the current tool and restores it on release; `Esc` cancels a drag in progress and leaves the view where it was; **the control** — `Esc` with no drag in progress does not change the tool |
| **T8** | Rulers, grid, guides, snapping | T3, T7 | D4-6's resolver and threshold; D4-11's overlay painting; `RulerWidget` with the unit selection and cursor tracking; guide drag, move, lock, delete, clear; the `units/*` keys — **eleven as built, not eight: D4-5** — and the **Units & Guides** page | `tst_snap` green over synthetic geometry: the precedence, the threshold at 50 % and 400 % **giving the same screen tolerance**, and no snap outside it (the control); guides round-trip through `SessionState`; `Ctrl+U`, `Ctrl+'`, `Ctrl+;` dispatch from a key event |
| **T9** | `SplitViewContainer` | T7 | D4-13: horizontal, vertical, spreadsheet freeze, remove; synchronized scroll; one handle and one journal | `tst_split` green: two views over one document, one `DocumentHandle` asserted; a rotation through `Ctrl+R` in one view appears in both at the same revision; the frozen row and column stay while the body scrolls; closing a split leaves one view and no leaked tickets |
| **T10** | `FullScreenController` and presentation | T9 | D4-12's two levels; transitions, auto-advance, loop, blank, laser, cursor; the monitor choice; the five `fullScreen/*` keys and the **Full Screen** page; `KeyContext::FullScreen` | `tst_presentation` green: every §8 Full Screen chord dispatches; `setPresentationModeActive` is called **true on entry and false on every exit path**, including an exception and a topology change (the control asserts the false); a monitor that disappears moves the presentation rather than losing it |
| **T11** | `WindowArranger` | T9 | D4-17: tile, cascade, merge windows, the window list, sync scroll between windows; `window.new` as a second view of one document | `tst_arranger` green over synthetic screen geometry: tiles fill the work area and do not overlap; cascade steps by the title-bar height; merge re-docks every detached tab in order; **the control** — arranging with one window changes nothing |
| **T12** | Fit Visible, Document Properties, reading mode, page labels | T4, T8 | D4-14's content box and **I-28's closure**; `DocumentPropertiesDialog` (D4-4); `Ctrl+L` reading mode; logical page labels in the page box and *Go to Page* | `tst_contentbox` green: over `cropbox-margins.pdf` the three fits give **three different zooms**, and Fit Visible's is the content's; `Ctrl+3` asserted **on the zoom**, not the mode (I-28's lesson); a page with no objects falls back; the page box accepts `iv` and `A-12` on `outline-labels.pdf` |
| **T13 ✅ 2026-09-23** | Session restore in the shell | T3, T5, T10, T11 | D4-9's ordering against `RecoveryPrompt`; pane layout memory; `general/rememberPaneLayout`, `general/rememberWindowLayout` | `tst_restore` green **through `CompositionRoot`**: quit with three documents and a split, relaunch, get them back; a *Don't Save* document comes back **clean**; a document recovery already opened is **not opened twice**; `restoreSession` off restores nothing |
| **T14 ✅ 2026-09-23** | The re-measurement, the census, CI, lockstep | all | D4-16's second reading through the real panes; `--list-actions` re-census; §9; §10 | The T2 figure re-read with the shipped panes and **recorded as the product's**; `--list-actions` and the two census cases agree; `Implementation-Plan.md`'s Stage 4 status block reflects reality, deviations included |

---

## 7. Blocking preconditions — setup checklist

| # | Precondition | Why | State |
|---|---|---|---|
| **P-1** | **Two monitors at different scale factors**, at least one changeable while the app runs — owner: **you** | Stage 2's P-1 rig, unchanged: mixed-DPI window drags, the full-screen monitor choice, and the fingerprint. Guide cases M-01, M-02, M-05 | ✅ **Satisfied** — the rig Stage 2 measured on 2026-08-31: a 4K panel at **150 %** plus a 2560×1440 at **100 %** (`Stage2-Rendering-Plan.md` §7) |
| **P-2** | **A way to REMOVE a monitor while the product is running** — physically unplugged, disabled in Display Settings, or `Win`+`P` → *PC screen only* — owner: **you** | **D4-8's whole point.** A fingerprint that is only ever seen once proves nothing; the case that matters is a window restored onto an arrangement that no longer exists, and a presentation on a monitor that vanishes mid-slide (guide M-04). No headless run can reach it | ✅ **Satisfied — `Win`+`P` → *PC screen only*, chosen by the user 2026-09-19.** The cable stays connected, so **Windows keeps the display's identity**: the removed monitor leaves `monitors()` and the arrangement fingerprint changes by its **set**, while the restored arrangement returns with the same `id` and `scaleFactor` and is therefore a fingerprint D4-8 has already seen — which is what makes M-04 step 3 an *unknown*-fingerprint case and step 1 a *known*-fingerprint one. **What this method therefore does NOT reach** (convention 19, stated here so a later reader subtracts it rather than adds it): an arrangement in which a display's identity itself changed — a cable moved to another port, or a different panel on the same one — which moves a monitor's `id` rather than the set. That is the unplug case, and no Stage 4 case claims it |
| **P-3** | **Python 3** on every job that runs the corpus checks | T1's three new files. Already a verification dependency (`tests/gates/RequirePython.cmake`) | ✅ Python 3.13.9 on this machine; `gates.corpus.*` already require it on every job |
| **P-4** | **A pointing device with a wheel, and — if one is available — a pen or touch screen** — owner: **you** | Wheel-zoom and its modifier, auto-scroll, the temporary hand tool and the loupe are all input-device behaviour (guide M-12). **Touch is a bonus, not a requirement**: `general/touchMode` is Stage 1's declared-and-inert key and Stage 4 does not implement it, so a rig without a touch screen loses nothing this stage owes | ✅ **A wheel mouse, and no pen or touch screen** (confirmed 2026-09-19). Every wheel, modifier, auto-scroll, temporary-hand and loupe step of M-12 is walkable; **its pen/touch step is recorded *not run for want of hardware*** rather than left ambiguous, and loses nothing this stage owes — `general/touchMode` stays Stage 1's declared-and-inert key |

**All four preconditions are cleared, P-2 and P-4 on 2026-09-19** — the rig owes this stage nothing
further. P-2 is `Win`+`P` and P-4 is a wheel mouse with no pen or touch, and each row above records
what its answer reaches **and what it does not**, because a method chosen without its bound written
down reads later as broader coverage than it is.

---

## 8. Verdict ledger — what Stage 4 owes

| Verdict | Produced by | Recorded in |
|---|---|---|
| **A2 at Stage 4** — does the shipped software presentation path hold the frame budget once a thumbnails pane, a split view and a presentation share one scheduler, one worker and one 256 MiB cache? And therefore: **is `platform::IAcceleratedSurface` needed?** ✅ **ANSWERED: `go`, and NOT needed.** T2 said `go, with named custom work` (bench, 10.59–10.74 ms); **T14 measured the named work and the verdict became plain** (product, **7.03–8.56 ms**; the two-window column 18.91–19.33 → **4.01–4.75 ms**) | **T2** (the bench, before the panes) and **T14** (re-read through the shipped panes) — D4-16 | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), beside A2's probe and product figures, **labelled as Stage 4's workload**; and `Architecture-FileMap.md`'s `WinAcceleratedSurface` row, which stops deferring |

**Stage 4 owns no open decision in `Implementation-Plan.md` §8.** **O7** (the render cache's topology)
is *resolved* and its own row names **Stage 5** as the revisit; T2's measurement is evidence that row
will want, and is recorded as such rather than as a reopening.

**A stage that is green but has no verdict is not signed off.** Stage 4 owes one, and it is also the
last stage that can cheaply decide to build a GPU path, because every later stage adds surfaces that
would have to be ported onto it.

---

## 9. CI changes

| Job | Before | After |
|---|---|---|
| `gates` (ubuntu, no Qt) | 124 cases, four gates | Unchanged in kind. Plus the three new corpus files in `gates.corpus.*`. **`mutation-lint` must still say one command** (D4-1) |
| `windows` (self-hosted) | 923 cases | Plus every Stage 4 suite. The `ui.` suites stay offscreen; `tst_restore` joins `ui.composition.` with a scratch session file |
| `portable-asan` (ubuntu) | 560 cases | Plus `tst_sessionstate`, `tst_snap`, `tst_outline`, `tst_metadata` — the `core/` half of this stage, which is the half a sanitizer can reach. **`ui/` is still outside ASan's reach** (O10), and the guide says so rather than implying coverage |
| `portable-fuzz` (ubuntu, clang) | three targets | **Unchanged — Stage 4 adds no target and no seed.** The parsers this stage adds are the outline and destination readers, and they are PDFium's, behind the facade (D4-3) |
| `fuzz-soak.yml` | three targets, ten minutes | Unchanged |
| `coverage-report` | names what did not run | Gains the pane, split and presentation suites, which are Windows-only, so their absence from a Linux run is named rather than inferred |

---

## 10. Lockstep debt this stage creates

**Paid in the same change as this plan** — corrections to existing documents, made now because each
is a fact this plan establishes:

- **`Implementation-Plan.md` § Stage 4**: the status line; the settings-key list reduced to fifteen,
  with `pageDisplay/continuousScroll` removed and `units/snapToObjects` added to Stage 6's list
  (D4-5); `Ctrl+4` and `S` removed from the key bindings handled and **added to Stage 5's**
  (D4-15); `GoToPageDialog` removed from the components, having been folded at Stage 2 T14;
  `DestinationsPane` reconciled with its FileMap row (D4-3); the exit criteria given Fit Visible and
  the A2 verdict. §5's guide row and its prose count.
- **`Architecture-FileMap.md`**: `core/doc/Destinations` and `ui/panes/DestinationsPane` re-tagged
  ○ (S7) → ○ (S4 read, S7 edit) (D4-3); `core/doc/Metadata` re-tagged ○ (S4) → ○ (S4 Info read, S8
  XMP and editing) (D4-4); `ui/dialogs/*`'s family row moves `DocumentPropertiesDialog` from *S2–S3*
  to *S4*.
- **`CLAUDE.md`** and **`claude-plan/README.md`**: the *next action*, the document-set counts (five
  stage plans and five testing guides), and Stage 3's state in the README's own table.

**Owed with the code**:

- Every ⚠️ row in §4, with its file.
- ✅ **Flow C** (`Architecture-FileMap.md` §5) gained the pane at T5 and — **paid late, at T14** —
  the split and the background window's `Prefetch` lane, which T9 and T11 were the owners of and
  neither drew. Flow B needed nothing: opening a document is unchanged by a pane. **That two of the
  three arrived a task late is the argument for this list existing**, not against it: nothing else
  would have noticed.
- ✅ **`Tech-Stack-Plan.md` §3.3**'s A2 entry gained Stage 4's workload figure at T2 and **the
  product's own at T14**, kept in two clearly separated blocks because they are D2-1's pair one
  level up; `WinAcceleratedSurface`'s row stopped saying *"if A2 says it is needed"* at T2 and was
  **confirmed at T14**.
- ✅ **`Stage2-Rendering-Testing-Guide.md` §8's I-28** is closed at T12 **in that guide**, with the
  measurement, because an issue is closed where it was raised. **Paid 2026-09-23**, along with that
  guide's own *do not re-report this* row in its manual §7, whose expectation is now inverted rather
  than deleted.
- ✅ **`CLAUDE.md`'s *What works today* and *deliberately absent* paragraphs** were re-derived at
  T14, from the product rather than from the previous sentence: **373 declared / 94 registered / 279
  unregistered** (`--list-actions`), **nine** unconsumed Viewer chords and not thirteen — the four
  this stage registered are named — **694 catalog keys** resolving in both locales with none falling
  back to itself, **6 of 18** platform interfaces and **3 of 7** backend ones, **14** layer-lint
  escapes, **19** Stage 4 settings keys over **8 of 26** Preferences pages, and **302 / 291** objects
  from the clean builds. The sentence that says `Select` selects nothing until Stage 5 stays true.
- **`Features.md` §8 is not touched by this stage.** Recorded here so that D4-15's three deferrals
  are not mistaken for binding changes: the chords are right, and only their stage moved. **Still
  true as of T12** — but `Features.md` **§1.4** did gain a line there, and it is the routing table's
  answer rather than a scope change: *Go to Page* had to decide what a typed `1` means on a document
  whose front matter is roman, §1.4 owns capabilities, and a decision recorded only in a header
  comment would be invisible to the next person who reads the catalog.

---

## 11. Explicitly out of scope

| Not in Stage 4 | Arrives at |
|---|---|
| **Any document change at all** — no command, no `Commands.def` row (D4-1). `Ctrl+R` from the Thumbnails pane dispatches Stage 3's command and rotates the **current** page or all pages; **a thumbnail selection is not yet a page set** | Stage 7 |
| Page operations from the pane — reorder by drag, cut/copy/paste, delete, extract, duplicate. Their chords are bound in the Thumbnails table and stay unregistered | Stage 7 |
| **Bookmark and destination EDITING** — rename, re-parent, delete, new-from-view, set-destination, the generators (D4-2, D4-3) | Stage 7 |
| **Text selection.** `SelectTool` arms and selects nothing, exactly as it has since Stage 2 T14 | Stage 5 |
| **Reflow view (`Ctrl+4`) and Snapshot (`S`)** (D4-15); **the contextual properties bar (`Ctrl+E`)**, which Stage 4 never listed | Stage 5, Stage 5, Stage 6 |
| Comparison view and its `Ctrl+]`/`Ctrl+[` navigation | Stage 5 |
| **Snapping to object edges**, and `units/snapToObjects` (D4-5, D4-6) | Stage 6 |
| XMP, editing any metadata field, and `/ModDate` on save (D4-4) | Stage 8 |
| The eight panes whose models do not exist (D4-2) | Stages 5–13 |
| **Touch mode.** `general/touchMode` stays Stage 1's declared-and-inert key; enlarging hit targets is not this stage's | Stage 14 |
| **High-contrast deference and anything else withdrawn on 2026-08-23.** Colour inversion and a custom page background are `Features.md` §1.2 view transforms and are **kept**; the accessibility feature that deferred to the OS theme is gone and is not to be rebuilt here | — (removed) |
| Printing, print preview, and the `Grayscale` flag's UI | Stage 8 |
| A presenter view, notes on a second monitor, remote controls | Stage 16 |

---

## 12. Risks inside Stage 4

| Risk | Shape | Mitigation |
|---|---|---|
| **The thumbnails pane starves the page the user is reading** | 900 speculative requests behind one worker, and the viewport waits behind them | D4-10's priority and viewport bound; **T2 measures it before the pane exists**, and the measurement is the stage's verdict |
| **A view state reaches the document** | A guide, a zoom or a pane width finds its way into the file, or a Stage 4 gesture constructs a command | D4-1 makes it a `mutation-lint` number; D4-7 gives guides a home; `layer-lint` already denies `pdf/` to `ui/` |
| **A window restores where nobody can see it** | The fingerprint is wrong, or the arrangement changed between quit and launch | D4-8's clamp is a backstop that makes a wrong fingerprint cosmetic; the case restores onto a desktop one third the size |
| **Restore and recovery both bring the same document back** | Two mechanisms, one document, two windows — or worse, one window with the other's edits | D4-9's ordering and skip; the case opens a crashed session and a saved session naming the same path |
| **A case asserts an intent and the effect is wrong** | I-28 again, in a stage made of intents: a fit mode, a snap flag, a selection, a fingerprint | Every case names its observable; the guide's §1 makes it a written rule and T12 re-asserts `Ctrl+3` on the **zoom** |
| **A widget case passes because nothing was laid out** | Convention 12: a pane with no layout pass has no visible rows, and every "what is visible" assertion passes vacuously | Each pane case drives layout and asserts a **non-zero** visible count before asserting anything about it |
| **The presentation leaves the screen saver disabled** | `setPresentationModeActive(true)` on entry and a path out that does not pair with it | D4-12 requires the `false` on every exit; the control asserts it after an abnormal exit and after a topology change |
| **`ui/` grows a second rasterizer** | A pane, a loupe or a presentation transition that draws pages its own way because the scheduler is awkward | D4-10 and D4-11 say where pixels come from and what may be painted over them; a second `FPDF_` caller is a `layer-lint` failure by construction |

---

## 13. Definition of Done for Stage 4

[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions),
made concrete:

1. T1…T14 complete; the [exit criteria](./Implementation-Plan.md#stage-4--viewing-and-navigation-surface) met.
2. `cmake --build --preset x64-release --target checks` clean — four gates, and **`mutation-lint`
   still reporting one command** (D4-1).
3. `ctest` green on Windows across all six targets and the `gates` preset, every Stage 0, 1, 2 and 3
   case included and present **by name**.
4. `portable-asan` green including `SessionState`, the snap resolver and the three readers;
   `portable-fuzz` and the soak unchanged and clean on three targets.
5. **Golden suite green with its baselines unchanged** — Stage 4 changes no rasterization, and a
   moved baseline is a finding rather than a step.
6. **The A2-at-Stage-4 verdict is recorded**, probe and product, in `Tech-Stack-Plan.md` §3.3, and
   `WinAcceleratedSurface`'s row says what was measured rather than what would decide it.
7. **Stage 2's I-28 is closed in Stage 2's guide**, with the measurement, and `Ctrl+3` is asserted on
   a zoom.
8. [`Stage4-ViewingSurface-Testing-Guide.md`](./Stage4-ViewingSurface-Testing-Guide.md) — written
   from this plan before the code, its automated half run green by me with **actual** output, its
   manual half walked and signed off **by you**. I do not mark a manual case.
9. No open S1 or S2 in the guide's issue log.
10. `Architecture-FileMap.md` rows flipped and §10's debt paid, in the same changes as the code.
11. `Implementation-Plan.md`'s Stage 4 status block updated to reality, deviations included.
