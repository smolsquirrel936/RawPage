# Recto — Architecture & File Map

## Context

This document says **what every file does and what it interacts with**. It makes the layered
architecture from [`Tech-Stack-Plan.md`](./Tech-Stack-Plan.md) concrete at the file level, and it
traces the **primary call flows** as ASCII chains so a new contributor can follow a real event from
a keystroke down to a byte on disk.

It is the *spatial* view of the codebase. It deliberately does **not** say *when* each file is built
— that is [`Implementation-Plan.md`](./Implementation-Plan.md), whose per-stage **Components** lists
name exactly the rows below. It does not say *what features exist* — that is
[`Features.md`](./Features.md), whose capabilities the flows in §5 exercise.

This document **rots faster than any other in the set**. A file's row here is part of that file's
definition of done: when a file lands, changes responsibility, splits, or is folded into another,
its row is updated in the same change.

---

## 1. Dependency rule — read this first

Dependencies point **inward only**. Nothing in an inner layer may name anything in an outer one.

```
                          app/
             (composition root — the ONLY layer that names a concrete)
                            │
                            │ constructs every concrete, injects interfaces
              ┌─────────────┴─────────────────┐
              ▼                               ▼
            ui/ ─────────uses────────▶      core/
        (Qt Widgets only)                (domain: no Qt Widgets,
                                          no PDFium, no Win32)
                                              │
                                     uses (interfaces only)
                            ┌─────────────────┴─────────────────┐
                            ▼                                   ▼
                    core/backend.h                    platform/platform.h
              IDocumentBackend, IPageRasterizer,     IShellIntegration, ICryptoProvider,
              ITextExtractor, ISigner, IOcrEngine,   IScanner, ISpeech, IFontResolver,
              IIndexer, MutationSink                 IAcceleratedSurface, … (16 total)
                            ▲                                   ▲
                 implements │                        implements │
                            │                                   │
                          pdf/                            platform/win/
              (the ONLY place FPDF_* or a PDF               (the ONLY OS-specific code)
               byte write may appear)
```

### The layer contract

| Layer | May include | May **never** include | Extra rule |
|---|---|---|---|
| `app/` | everything | — | The only translation units that name `pdf::createBackend()` or `platform::createPlatform()`. Contains no domain logic. |
| `ui/` | `core/`, `core/backend.h`, `platform/platform.h`, Qt | `pdf/`, `platform/win/` | May **never** mutate a `Document`. It constructs a `Command` and hands it to `CommandJournal::run()`. That is its only mutation vocabulary. |
| `core/` | std, `QtCore` (documented exception for `QString`/containers/signals), the portable pure-computation allowlist (**ICU, HarfBuzz, Hunspell**), `core/backend.h`, `platform/platform.h` | `ui/`, `pdf/`, `platform/win/`, `QtWidgets`, `QtGui`, PDFium, any OS header | Builds and passes its whole test suite with **no display and no PDF engine linked**. If it can't, the boundary has leaked. |
| `pdf/` | std, `QtCore`, `core/backend.h`, `core/doc/` value types, PDFium, codec libs | `ui/`, `platform/win/`, `core/` behaviour classes (`CommandJournal`, `Settings`) | The only layer permitted to emit PDF bytes or call `FPDF_*`. |
| `platform/win/` | `platform/platform.h`, Win32/COM | `ui/`, `core/`, `pdf/` | Implements interfaces and nothing else. No policy decisions. |

**`i18n/`, `res/`, `corpus/`, `golden/` are data, not code.** Nothing links them; they are loaded at
runtime or read by tests.

**Reading the "Interacts with" column.** A cell names collaborators in **both** directions, and the
wording says which: *"used by / read by / consumed by / driven by / populated by"* marks an **inbound**
collaborator in an outer layer (legitimate — `core/Settings` is read by everything), while a bare name
or *"calls"* marks an **outbound** dependency, which must always point inward. A `core/` row may
therefore name a `pdf/` or `ui/` file only as an inbound consumer, never as something it calls.

**Enforcement is mechanical, not cultural.** `tools/layer-lint` fails CI on any include crossing a
boundary outward, on `FPDF_` outside `pdf/`, and on a filesystem write outside `pdf/` and
`core/settings/`. See [`Tech-Stack-Plan.md` §5.6](./Tech-Stack-Plan.md#56-how-it-stays-enforced).

---

## 2. Status legend

| Marker | Meaning |
|---|---|
| ✅ | Exists now, and its role below is what it actually does. |
| 🟡 | Partially implemented — the row's Role describes the target; the roadmap stage's status block says what is real. |
| ○ | Planned. The parenthesized **S*n*** is the [roadmap](./Implementation-Plan.md) stage that builds it. |
| ⤵ | Folded into another file — the row stays as a breadcrumb, pointing at where the responsibility went. |

**Six rows are ✅** — the two gates (T7, T8), the four `core/` vocabulary headers (T10), and the unit
harness (T11). Everything else is ○ with its stage tag: no *document* code exists yet, only Stage 0's
scaffolding and the shared vocabulary every later layer is written in. The stage tags are
the contract between this map and the roadmap.

Stage 0 also adds files this map does not yet describe — the build definition, the CMake modules, the
gate fixtures, the CI workflow, and `scripts/`. Rows for those are owed by **task T15**, recorded in
[`Stage0-Toolchain-Plan.md` §10](./Stage0-Toolchain-Plan.md#10-lockstep-debt-this-stage-creates) so
the gap is a scheduled item rather than drift.

A row whose "File" cell names a folder with a `/*` suffix covers a **tight family** of same-shaped
files (one per tool, one per filter) that share a base class and a single responsibility; the Role
cell names the members. Everything else is one row per file, with `{.h,.cpp}` counting as one file.

---

## 3. File map by layer

### 3.1 `app/` — composition root

| File | Status | Role | Interacts with |
|---|---|---|---|
| `app/main.cpp` | ○ (S1) | Entry point. Sets DPI awareness before any window exists, installs the crash handler, parses the command line, hands off to `CompositionRoot`, runs the Qt event loop. | `app/CrashHandler`, `app/CommandLine`, `app/CompositionRoot`, `app/Application` |
| `app/Application.{h,cpp}` | ○ (S1) | `QApplication` subclass: application lifetime, theme application, translator loading, session save/restore on quit, last-window-closed policy. | `core/settings/Settings`, `ui/shell/ThemeManager`, `core/settings/SessionState` |
| `app/CompositionRoot.{h,cpp}` | ○ (S1) | **The wiring hub.** Constructs `platform::createPlatform()` and `pdf::createBackend()`, the `Settings` store, the `ActionRegistry`, the `RenderScheduler` and its worker pool, then builds `MainWindow` injecting only interfaces. The only file that knows a concrete backend exists. | `pdf::createBackend()`, `platform::createPlatform()`, `core/settings/Settings`, `core/cmd/ActionRegistry`, `core/render/RenderScheduler`, `ui/shell/MainWindow`, `app/Services` |
| `app/Services.h` | ○ (S1) | The aggregate of injected interfaces (`IDocumentBackend*`, `Platform*`, `Settings*`, `ActionRegistry*`, `RenderScheduler*`) passed by reference down the UI tree. Keeps constructors from growing twelve parameters without becoming a service locator. | Consumed by `ui/shell/MainWindow`, `ui/shell/DocumentWindow` |
| `app/CommandLine.{h,cpp}` | ○ (S1 basic, S14 full) | Parses every CLI verb from [`Features.md` §9.6](./Features.md#96-command-line) into an intent object; runs headless verbs without creating a window; documents exit codes. | `core/batch/BatchRunner`, `core/cmd/ActionRegistry`, `app/CompositionRoot` |
| `app/CrashHandler.{h,cpp}` | ○ (S1) | Unhandled-exception and `SIGABRT` capture, minidump write, "flush the recovery journal" last rites, opt-in report queue. The GCC toolchain emits **DWARF, not PDB**, so the dump is paired with a build ID and symbolized against the archived unstripped binary rather than by a symbol server — decide and build that path here, not at Stage 15. | `core/cmd/RecoveryJournal`, `core/Log`, `platform::IShellIntegration` (dump path) |
| `app/RecoveryPrompt.{h,cpp}` | ○ (S3) | On launch, discovers recovery journals whose source still matches, presents them, and drives replay; reports the command that failed if replay stops early. | `core/cmd/RecoveryJournal`, `core/cmd/CommandJournal`, `ui/dialogs/RecoveryDialog` |
| `app/SingleInstance.{h,cpp}` | ○ (S1) | Decides first-instance vs. secondary-instance at startup; a secondary instance forwards its file list and exits; the primary raises itself and opens the file. | `platform::ISingleInstanceBus`, `ui/shell/MainWindow` |

### 3.2 `core/doc/` — the document model

| File | Status | Role | Interacts with |
|---|---|---|---|
| `core/doc/Document.{h,cpp}` | ○ (S3) | The open document's aggregate root: `PageTree`, `AnnotationStore`, `FormModel`, `Outline`, `LayerTree`, `TagTree`, `Attachments`, `Metadata`, `Permissions`, `RepairReport`, current `Revision`. Owns **no** file handle and knows nothing about PDF syntax. | `core/doc/*`, mutated only by `core/cmd/Command` subclasses; hydrated by `IDocumentBackend` |
| `core/doc/DocumentId.h` | ○ (S3) | Stable identity for a document across sessions (source path + size + mtime + content hash). Keys the recovery journal and the per-document view state. | `core/cmd/RecoveryJournal`, `core/settings/SessionState` |
| `core/doc/Page.{h,cpp}` | ○ (S3) | One page: MediaBox/CropBox/TrimBox/BleedBox/ArtBox, rotation, label, resource summary, per-page annotation and field indices. Pure → `tst_page`. | Held by `core/doc/PageTree`; read by `core/render/TileGrid` |
| `core/doc/PageTree.{h,cpp}` | ○ (S3) | Ordered page collection with insert/move/remove/duplicate as **model** operations (no I/O), plus page-label resolution and index↔label mapping. Pure → `tst_pagetree`. | Mutated by `core/cmd/commands/PageCommands`; read by `ui/panes/ThumbnailsPane` |
| `core/doc/Annotation.{h,cpp}` | ○ (S6) | One annotation: subtype, rect, quadpoints, appearance properties, author, dates, subject, status history, reply parent, lock flags. Covers every markup type in [`Features.md` §4.1](./Features.md#41-annotation-tools). Pure → `tst_annotation`. | Held by `AnnotationStore`; **read by** `pdf/ops/AppearanceStreamBuilder` to generate `/AP` |
| `core/doc/AnnotationStore.{h,cpp}` | ○ (S6) | All annotations with per-page and per-author indices, reply threading, filter/sort/group queries backing the Comments pane, and FDF/XFDF import-merge semantics. Pure → `tst_annotstore`. | `core/doc/Annotation`, `ui/panes/CommentsPane`, `core/cmd/commands/AnnotationCommands` |
| `core/doc/FormField.{h,cpp}` | ○ (S9) | One field: type, name, value, default, options, flags, format/validate/calculate specs, appearance, tab index. Pure → `tst_formfield`. | Held by `FormModel` |
| `core/doc/FormModel.{h,cpp}` | ○ (S9) | Field hierarchy, name resolution, calculation dependency graph and order, validation dispatch, tab-order computation, and the import/export data mapping. Pure → `tst_formmodel`. | `core/doc/FormField`, `ui/panes/FieldsPane`, `ui/viewer/FormOverlay` |
| `core/doc/Outline.{h,cpp}` | ○ (S3 read, S7 edit) | The bookmark tree: nodes with title, style, destination-or-action, and children; promote/demote/reorder as model ops; generators from headings, font size, or a text pattern. Pure → `tst_outline`. | `core/cmd/commands/BookmarkCommands`, `ui/panes/BookmarksPane` |
| `core/doc/Destinations.{h,cpp}` | ○ (S7) | Named destinations: create, rename, delete, list, and repair after pages move. | `core/doc/PageTree`, `ui/panes/DestinationsPane` |
| `core/doc/LayerTree.{h,cpp}` | ○ (S7) | Optional-content groups: nesting, visibility state per usage (view/print/export), radio groups, lock flags. Pure → `tst_layers`. | `core/cmd/commands/LayerCommands`, `ui/panes/LayersPane`, `core/render/RenderRequest` |
| `core/doc/TagTree.{h,cpp}` | ○ (S7) | The logical structure tree: tag types, reading order, alt text, table header scope, language attributes, artifact marking. Pure → `tst_tags`. | `ui/panes/TagsPane`, `core/text/LayoutAnalyzer`, `platform::ISpeech` consumers |
| `core/doc/Attachments.{h,cpp}` | ○ (S6) | Document-level and annotation-level embedded files: add, extract, delete, and the dangerous-type policy hook. | `core/security/TrustPolicy`, `ui/panes/AttachmentsPane` |
| `core/doc/Metadata.{h,cpp}` | ○ (S3) | Info dictionary and XMP as one editable model, with round-trip preservation of XMP we do not understand. | `core/cmd/commands/MetadataCommands`, `ui/dialogs/DocumentPropertiesDialog` |
| `core/doc/Permissions.{h,cpp}` | ○ (S10) | The decoded permission set and the **single** `may(Operation)` predicate every command asks before it is allowed to run. Pure → `tst_permissions`. | `core/cmd/CommandJournal` (pre-flight), `core/security/EncryptionPolicy` |
| `core/doc/RepairReport.{h,cpp}` | ○ (S2) | What the parser had to reconstruct on open, per object, with severity — surfaced in the message bar and forcing first-save-as. | **Populated by** `pdf/cos/XrefRepair`; **read by** `ui/shell/MessageBar`, `ui/dialogs/RepairReportDialog` |
| `core/doc/Revision.h` | ○ (S3) | Monotonic document version. Render caches, panes, and the dirty flag all key off it, so nothing needs deep comparison to know it is stale. | `core/cmd/CommandJournal`, `core/render/RenderCache` |
| `core/doc/Scope.{h,cpp}` | ○ (S3) | What a mutation touched (page set, object set, "whole document"), so invalidation repaints one page instead of everything. | Emitted by `CommandJournal::mutated`, consumed by `ui/viewer/PageView` and every pane |

### 3.3 `core/cmd/` — the action layer and the mutation journal

| File | Status | Role | Interacts with |
|---|---|---|---|
| `core/cmd/Command.h` | ○ (S3) | The mutation contract: `name/scope/apply/revert/serialize`. **The only vocabulary for changing a document.** | Implemented by every file in `core/cmd/commands/`; run by `CommandJournal` |
| `core/cmd/CommandJournal.{h,cpp}` | ○ (S3) | Runs commands; maintains undo and redo stacks; groups compound operations into one undo step; pre-flights `Permissions::may()`; bumps `Revision`; emits `mutated(Scope)` and `dirtyChanged`; appends to the `RecoveryJournal`. Pure → `tst_journal`. | `core/doc/Document`, `core/cmd/RecoveryJournal`, `MutationSink`, everything in `ui/` |
| `core/cmd/CommandRegistry.{h,cpp}` | ○ (S3) | Type-id ↔ factory table for every command, so the recovery journal and batch runner can deserialize one. `tools/mutation-lint` asserts every subclass is registered. | `core/cmd/commands/*`, `core/cmd/RecoveryJournal`, `core/batch/BatchRunner` |
| `core/cmd/RecoveryJournal.{h,cpp}` | ○ (S3) | Append-and-fsync `journal.ndjson` plus `meta.json` per open document; discovery, source-match validation, and ordered replay with a stop-at-failure report. Degrades to memory-only if the directory is unwritable. | `core/cmd/CommandRegistry`, `core/doc/DocumentId`, `app/RecoveryPrompt` |
| `core/cmd/ActionId.h` | ○ (S1) | The enumeration of every named user action in the product. Ribbon buttons, menu items, shortcuts, and CLI verbs all resolve to one of these — never to a slot directly. | `core/cmd/ActionRegistry`, `ui/shell/RibbonTabBuilder`, `app/CommandLine` |
| `core/cmd/ActionRegistry.{h,cpp}` | ○ (S1) | `ActionId` → handler, enablement predicate, checked-state, label, icon, and tooltip. `dispatch(ActionId, KeyContext)` is the single entry point for all input. Pure → `tst_actionregistry`. | `ui/shell/ShortcutBinder`, `ui/shell/Ribbon`, `app/CommandLine`, `core/batch/BatchRunner` |
| `core/cmd/KeyContext.h` | ○ (S1) | The input contexts of [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference): `Global, Viewer, WriteMode, AnnotationTool, Thumbnails, Bookmarks, Comments, FormsDesign, FormsFill, Search, FullScreen`. This enum is why one chord can mean two things. | `core/cmd/KeyBindingStore`, `ui/shell/ShortcutBinder` |
| `core/cmd/KeyBindingStore.{h,cpp}` | ○ (S1) | `(chord, KeyContext) → ActionId`, seeded from the §8 defaults, overridable per user, persisted in settings. Detects and **reports** (does not forbid) a remap that shadows a Global chord in one context. Pure → `tst_keybindings`. | `core/settings/Settings`, `ui/dialogs/ShortcutsPage` |
| `core/cmd/commands/PageCommands.{h,cpp}` | ○ (S3 rotate, S7 rest) | Rotate, insert, delete, move, duplicate, extract, replace, crop, resize, renumber — one `Command` subclass each. `RotatePageCommand` is the first-milestone mutation. | `core/doc/PageTree`, `MutationSink`, `ui/panes/ThumbnailsPane` |
| `core/cmd/commands/AnnotationCommands.{h,cpp}` | ○ (S6) | Add, edit, move, delete, reply, set-status, set-appearance, lock, import-merge — per annotation kind. | `core/doc/AnnotationStore`, `MutationSink` |
| `core/cmd/commands/FormCommands.{h,cpp}` | ○ (S9) | Create/delete/edit field, set value, set tab order, reset form, import data, flatten form. | `core/doc/FormModel`, `MutationSink` |
| `core/cmd/commands/BookmarkCommands.{h,cpp}` | ○ (S7) | Create, rename, delete, re-parent, reorder, set destination or action, bulk-generate. | `core/doc/Outline`, `MutationSink` |
| `core/cmd/commands/StructureCommands.{h,cpp}` | ○ (S7) | Layer and tag mutations: create/delete/merge/flatten layers, retype and move tags, set alt text. | `core/doc/LayerTree`, `core/doc/TagTree`, `MutationSink` |
| `core/cmd/commands/SecurityCommands.{h,cpp}` | ○ (S10) | Apply/remove password or certificate security, apply a scheme, apply redactions, sanitize. Every one is a document mutation and therefore a `Command`. | `core/security/*`, `MutationSink` |
| `core/cmd/commands/ContentCommands.{h,cpp}` | ○ (S6 typewriter, S13 full) | Review-mode content edits: edit a text run, move/scale/delete an object, insert an image, change z-order. | `core/text/PageTextModel`, `MutationSink` |
| `core/cmd/commands/ReflowCommands.{h,cpp}` | ○ (S13) | Write-mode edits against `core/reflow/ReflowDocument`, coalesced so a burst of typing is one undo step. | `core/reflow/*`, `MutationSink` |
| `core/cmd/commands/DecorationCommands.{h,cpp}` | ○ (S7) | Watermark, header/footer, Bates, imposition, flatten, and their remove/update inverses. Expresses intent only — the realization lives in `pdf/ops/DecorationOps`. | `MutationSink`, `core/settings/ProfileStore` (the saved specs) |

### 3.4 `core/render/` — rendering policy (no rasterization here)

| File | Status | Role | Interacts with |
|---|---|---|---|
| `core/render/DpiPolicy.{h,cpp}` | ○ (S2) | Turns (zoom, monitor scale, monitor physical DPI) into the *device* resolution a tile must be rasterized at, and decides when a scale change requires re-rasterization versus a cheap resample. Pure → `tst_dpipolicy`. **The file most likely to be the root cause of a blurry-text bug.** | `platform::IDisplayTopology`, `core/render/TileGrid` |
| `core/render/TileGrid.{h,cpp}` | ○ (S2) | Splits a page at a given device resolution into tiles; computes the visible, prefetch, and evict sets from a viewport rect. Pure → `tst_tilegrid`. | `core/render/RenderScheduler`, `ui/viewer/TilePresenter` |
| `core/render/RenderRequest.h` | ○ (S2) | An immutable rasterization request: page, tile rect, device resolution, `Revision`, layer visibility, render flags (anti-alias, line-weight, transparency grid), priority. Its equality *is* the cache key. | `core/render/RenderCache`, `IPageRasterizer` |
| `core/render/RenderScheduler.{h,cpp}` | ○ (S2) | Priority queue over requests, coalescing, cancellation on scroll, worker-pool dispatch to `IPageRasterizer`, and delivery back on the UI thread. **The UI thread never rasterizes.** | `IPageRasterizer`, `core/render/RenderCache`, `ui/viewer/TilePresenter`, `platform::IPowerSession` (throttle) |
| `core/render/RenderCache.{h,cpp}` | ○ (S2) | Memory-and-disk tile cache keyed by `RenderRequest`, size-capped, LRU, invalidated by `Scope` and `Revision`. Pure → `tst_rendercache`. | `core/doc/Revision`, `core/doc/Scope`, `core/settings/Settings` |
| `core/render/PagePresentation.{h,cpp}` | ○ (S2) | Page-arrangement geometry for single / continuous / two-up / two-up-continuous / RTL, cover-page handling, and page-rect ↔ document-point mapping. Pure → `tst_presentation`. | `ui/viewer/PageLayoutEngine`, `ui/viewer/ViewportController` |

### 3.5 `core/text/` — text, selection, search, indexing

| File | Status | Role | Interacts with |
|---|---|---|---|
| `core/text/PageTextModel.{h,cpp}` | ○ (S5) | A page's text as runs with glyph rects, writing direction, and font identity, hydrated from `ITextExtractor`. The substrate for selection, find, copy, and the reflow converter. | `ITextExtractor`, `core/text/TextSelection`, `core/reflow/FixedToReflowConverter` |
| `core/text/LayoutAnalyzer.{h,cpp}` | ○ (S5) | Groups runs into lines, lines into blocks, blocks into a reading order; detects columns and table grids. Pure → `tst_layout`. **The quality of Write mode, Reflow view, and Read Aloud all rest on this one file.** | `core/text/PageTextModel`, `core/doc/TagTree`, `core/reflow/FixedToReflowConverter` |
| `core/text/TextSelection.{h,cpp}` | ○ (S5) | Selection as a run-offset range: hit-testing, word/line/block expansion, column (marquee) selection, and extraction as plain, formatted, or tabular text. Pure → `tst_selection`. | `ui/viewer/SelectionOverlay`, `platform::IRichClipboard` |
| `core/text/SearchEngine.{h,cpp}` | ○ (S5) | Executes a `SearchQuery` over one document or many: literal, whole-word, case/diacritic folding, and regex, across page text, comments, bookmarks, field values, and attachments. Pure → `tst_search`. | `core/text/PageTextModel`, `core/doc/AnnotationStore`, `core/text/DocumentIndex` |
| `core/text/SearchQuery.h` | ○ (S5) | The query value type: pattern, flags, and which content classes to include — exactly the toggles in [`Features.md` §1.8](./Features.md#18-search-and-find-). | `core/text/SearchEngine`, `ui/panes/SearchPane` |
| `core/text/DocumentIndex.{h,cpp}` | ○ (S5) | Per-document inverted index built lazily on first search, so a second search of a 900-page file is instant. | `core/text/SearchEngine`, `core/render/RenderCache` (shares the cache directory policy) |
| `core/text/CatalogIndex.{h,cpp}` | ○ (S16) | First-party inverted index over a folder tree: build, incremental update, purge, stop words, metadata fields. Implements `IIndexer`. See risk R2. | `IIndexer`, `core/batch/BatchRunner`, `ui/panes/SearchPane` |
| `core/text/DocumentComparer.{h,cpp}` | ○ (S5) | Diffs two documents: text insert/delete/replace/move via an alignment over `PageTextModel`, page-appearance differences via a raster compare, and page-level insert/delete/reorder detection; honours the ignore-lists and emits a navigable difference set plus the report model. Pure → `tst_compare`. Reused in S11 to answer "was this post-signature change permitted?" | `core/text/PageTextModel`, `IPageRasterizer`, ICU; **read by** `ui/viewer/ComparisonView`, `ui/panes/DifferencesPane`, `core/security/SignatureValidator` |

### 3.6 `core/reflow/` — the Write-mode model (see risk R1)

| File | Status | Role | Interacts with |
|---|---|---|---|
| `core/reflow/ReflowDocument.{h,cpp}` | ○ (S13) | The **second document model**: sections → paragraphs → runs, plus tables, anchored objects, styles, headers/footers, footnotes, fields, and a TOC model. Deliberately *not* a mode flag on `core/doc/Document`. | `core/cmd/commands/ReflowCommands`, `ui/write/WriteCanvas` |
| `core/reflow/StyleSheet.{h,cpp}` | ○ (S13) | Named paragraph and character styles, inheritance, and the resolved-format computation for a run. Pure → `tst_styles`. | `core/reflow/ReflowDocument`, `ui/write/StyleGallery` |
| `core/reflow/TableModel.{h,cpp}` | ○ (S13) | Rows, columns, spans, per-cell format, repeat-header, sort, and the formula/AutoSum evaluator with cell references. Pure → `tst_table`. | `core/reflow/ReflowDocument`, `ui/write/TableTools` |
| `core/reflow/LineBreaker.{h,cpp}` | ○ (S13) | Line breaking, justification, hyphenation, widow/orphan and keep-with-next, and column/page break placement. Pure → `tst_linebreak`. | HarfBuzz + ICU via `core/reflow/Shaper`, `core/reflow/ReflowToFixedComposer` |
| `core/reflow/Shaper.{h,cpp}` | ○ (S13) | Complex-script shaping and font fallback for document text, resolving missing fonts through `IFontResolver`. | HarfBuzz, `platform::IFontResolver` |
| `core/reflow/FixedToReflowConverter.{h,cpp}` | ○ (S13) | *Convert to Writable*: turns positioned glyphs plus the tag tree into a semantic `ReflowDocument`, and records every element it could not reconstruct. | `core/text/LayoutAnalyzer`, `core/doc/TagTree`, `core/reflow/FidelityReport` |
| `core/reflow/ReflowToFixedComposer.{h,cpp}` | ○ (S13) | The way back: lays a `ReflowDocument` out onto pages and emits the page-content **intent** through `MutationSink`. Also the *Prevent Further Edits* finalization path. | `core/reflow/LineBreaker`, `MutationSink` (realized by `pdf/cos/ContentStreamBuilder` on the far side) |
| `core/reflow/FidelityReport.{h,cpp}` | ○ (S13) | Per-element list of what the conversion lost or approximated, with page references — shown before the user commits to Write mode. | `ui/dialogs/ConversionFidelityDialog` |
| `core/reflow/ChangeTracker.{h,cpp}` | ○ (S13) | Tracked insertions, deletions, format changes, and moves with per-reviewer identity; accept/reject one, all, or all-by-reviewer. Pure → `tst_changetracking`. | `core/reflow/ReflowDocument`, `ui/write/TrackChangesBar` |
| `core/reflow/SpellChecker.{h,cpp}` | ○ (S13) | Per-language dictionaries, as-you-type checking, custom dictionaries, and the suggestion ranking. Pure → `tst_spell`. | Hunspell, ICU (word boundaries), `core/reflow/ReflowDocument`, `core/doc/AnnotationStore` (comment spell check); **driven by** `ui/write/SpellCheckPane` |

### 3.7 `core/security/` — protection logic (no crypto primitives here)

| File | Status | Role | Interacts with |
|---|---|---|---|
| `core/security/EncryptionPolicy.{h,cpp}` | ○ (S10) | Algorithm choice, what is covered (all / all-but-metadata / attachments-only), password strength rules, and the permission bits to write. Pure → `tst_encryptionpolicy`. | `core/doc/Permissions`; **consumed by** `pdf/cos/StandardSecurityHandler` via `MutationSink` |
| `core/security/SecurityScheme.{h,cpp}` | ○ (S10) | A named, saveable protection configuration (password, certificate, or label) with an apply audit record. | `core/security/SecuritySchemeStore`, `core/cmd/commands/SecurityCommands` |
| `core/security/SecuritySchemeStore.{h,cpp}` | ○ (S10) | Persist, list, favourite, import, and export schemes; surfaces which are locked by administrative policy. | `core/settings/ProfileStore`, `core/settings/PolicyOverlay` |
| `core/security/RedactionPlan.{h,cpp}` | ○ (S10) | The set of marked areas plus overlay appearance and code set; search-driven mark generation; the applied-redaction report. Pure → `tst_redaction`. | `core/text/SearchEngine`, `core/doc/AnnotationStore`; **consumed by** `pdf/ops/RedactionOps` via `MutationSink` |
| `core/security/HiddenContentScanner.{h,cpp}` | ○ (S10) | Enumerates every hidden-data category in [`Features.md` §7.9](./Features.md#79-removing-sensitive-content), reports what was found, and drives selective or full sanitize. Asks the engine what exists through `IDocumentBackend`; removal is expressed through `MutationSink`. | `core/doc/*`, `IDocumentBackend`, `MutationSink`; **read by** `ui/dialogs/SanitizeDialog` |
| `core/security/SignatureRequest.{h,cpp}` | ○ (S11) | Everything a signature needs before crypto: field, appearance, reason/location/contact, permitted-changes level, lock list, timestamp and revocation requirements, PAdES target. | `ISigner`, `ui/dialogs/SignDialog` |
| `core/security/SignatureValidator.{h,cpp}` | ○ (S11) | Validation orchestration: byte-range integrity, chain building, trust decision, revocation, timestamp-versus-signer-clock, and the permitted-versus-actual change comparison. Logic only — every primitive comes from `ICryptoProvider`. Pure → `tst_validation` with a mock provider. | `platform::ICryptoProvider`, `core/security/ValidationReport`, `ui/panes/SignaturesPane` |
| `core/security/ValidationReport.{h,cpp}` | ○ (S11) | The per-signature verdict, its evidence, and the human-readable reason — including the honest "cannot be validated offline" state. | `ui/panes/SignaturesPane`, `core/batch/BatchReport` |
| `core/security/TrustPolicy.{h,cpp}` | ○ (S10) | Whether this document may reach the network, run JavaScript, open a file, or submit a form; privileged locations; the attachment-type block list; the protected-view decision. Pure → `tst_trust`. | `core/doc/Attachments`, `app/CompositionRoot`, `platform::IProcessSandbox` |

### 3.8 `core/batch/`, `core/settings/`, and shared headers

| File | Status | Role | Interacts with |
|---|---|---|---|
| `core/batch/ActionSequence.{h,cpp}` | ○ (S14) | An ordered list of parameterized actions, each either fixed or prompt-at-run-time; serialize/deserialize for sharing. | `core/cmd/ActionRegistry`, `core/batch/SequenceStore` |
| `core/batch/BatchRunner.{h,cpp}` | ○ (S14) | Runs a sequence over a file list, folder, tree, watched folder, or the open documents; continue-or-stop on error; dry-run mode; progress and cancellation. **Runs with no UI attached** — the proof that the action layer is real. | `core/cmd/CommandJournal`, `IDocumentBackend`, `core/batch/BatchReport`, `app/CommandLine` |
| `core/batch/BatchReport.{h,cpp}` | ○ (S14) | Per-file outcome, timing, and reason for skip or failure; renders as text, CSV, or JSON for the CLI. | `ui/dialogs/BatchEditorDialog`, `app/CommandLine` |
| `core/batch/SequenceStore.{h,cpp}` | ○ (S14) | Save, rename, duplicate, delete, import, and export sequences. | `core/settings/ProfileStore` |
| `core/batch/WatchedFolder.{h,cpp}` | ○ (S14) | Watches a folder, debounces file-settling, and enqueues a sequence run per arrival. | `core/batch/BatchRunner`, `QtCore` file watching |
| `core/settings/Settings.{h,cpp}` | ○ (S1) | The **one** typed settings store. Groups mirror the Preferences pages of [`Features.md` §9.1](./Features.md#91-preferences-groups). Typed get/set with defaults, `changed(key)` signal, export/import, reset. Pure → `tst_settings`. | Read by nearly everything; written by `ui/dialogs/PreferencesDialog` |
| `core/settings/SettingsSchema.{h,cpp}` | ○ (S1) | Declares every key once — type, default, range, group, and whether policy may lock it. An undeclared key is a build-time error, which is what stops settings sprawl. | `core/settings/Settings`, `core/settings/PolicyOverlay` |
| `core/settings/PolicyOverlay.{h,cpp}` | ○ (S15) | Machine-wide administrative defaults and locks layered over user settings, with "who locked this" reporting. | `core/settings/Settings`, `ui/dialogs/PreferencesDialog` |
| `core/settings/ProfileStore.{h,cpp}` | ○ (S6) | Generic named-profile persistence reused by stamps, watermarks, headers/footers, security schemes, signature appearances, conversion profiles, and batch sequences. | `core/security/SecuritySchemeStore`, `core/batch/SequenceStore`, `ui/dialogs/*` |
| `core/settings/RecentFiles.{h,cpp}` | ○ (S1) | Recent list with pinning, per-document view state, and the clear action. | `core/doc/DocumentId`, `platform::IShellIntegration` (jump list) |
| `core/settings/SessionState.{h,cpp}` | ○ (S4) | The open-document set, tab order, window geometry per monitor arrangement, and pane layout, for session restore. | `app/Application`, `ui/shell/MainWindow`, `platform::IDisplayTopology` |
| `core/backend.h` | ○ (S2) | **The engine contract.** Declares `IDocumentBackend`, `IPageRasterizer`, `ITextExtractor`, `MutationSink`, `ISigner`, `IOcrEngine`, `IIndexer`. The narrowest and most important header in the project: it is what makes `core/` testable and the engine replaceable. | Implemented by `pdf/`; consumed by `core/*` and (read-only interfaces) by `ui/` |
| `core/Result.h` | ✅ (S0 · T10) | `Result<T>` / `Error` with a code, a message key for `tr()`, and context — so no layer signals failure by exception across a boundary. | Everything |
| `core/Log.h` | ✅ (S0 · T10) | Subsystem-tagged, level-filtered logging macros. Enforces the rule that document text and file paths never appear above debug level. | Everything; `ui/panes/LogPane` |
| `core/Geometry.h`, `core/Units.h` | ✅ (S0 · T10) | PDF-space vs device-space point/rect types that do not implicitly convert, and the unit conversions for rulers and measurement. Keeping these distinct is what prevents a whole class of coordinate bugs. | `core/render/*`, `ui/viewer/*`, `core/doc/Page` |

### 3.9 `pdf/cos/` — first-party PDF object layer (we own this outright)

| File | Status | Role | Interacts with |
|---|---|---|---|
| `pdf/cos/CosObject.{h,cpp}` | ○ (S3) | The object variant: null, bool, number, string, name, array, dict, stream, reference — with identity-preserving round-trip of anything we do not interpret. | All of `pdf/cos/`, `pdf/ops/` |
| `pdf/cos/Lexer.{h,cpp}` | ○ (S3) | Tokenizer over the byte stream: delimiters, literal and hex strings, numbers, names, comments. Tolerant by design. Pure → `tst_cos_lexer`, and a fuzz target. | `pdf/cos/Parser` |
| `pdf/cos/Parser.{h,cpp}` | ○ (S3) | Objects, indirect objects, streams with length-recovery when `/Length` lies, and object streams. Pure → `tst_cos_parser`, and the **primary fuzz target**. | `pdf/cos/Lexer`, `pdf/cos/XrefTable` |
| `pdf/cos/XrefTable.{h,cpp}` | ○ (S3) | Classic cross-reference tables and cross-reference streams, the `/Prev` chain across incremental updates, and the trailer. | `pdf/cos/Parser`, `pdf/cos/IncrementalWriter` |
| `pdf/cos/XrefRepair.{h,cpp}` | ○ (S3) | Full-file object scan when the xref is broken, rebuilding the table and emitting a `RepairReport`. This file is why damaged files open instead of failing. | `core/doc/RepairReport`, `pdf/cos/Parser` |
| `pdf/cos/filters/*.{h,cpp}` | ○ (S3) | The stream-filter family, one file each: `FlateFilter`, `LZWFilter`, `Ascii85Filter`, `AsciiHexFilter`, `RunLengthFilter`, `DctFilter`, `JpxFilter`, `CcittFilter`, `Jbig2Filter`, `CryptFilter`. Each is a pure `decode`/`encode` pair → `tst_cos_filters`. | zlib, libjpeg-turbo, OpenJPEG; used by `pdf/cos/Parser` and the writers |
| `pdf/cos/StandardSecurityHandler.{h,cpp}` | ○ (S10) | Password-based encryption: RC4 40/128, AESV2, AESV3; key derivation, permission bits, and the metadata/attachment coverage modes. Pure → `tst_crypt` against known vectors. | OpenSSL, `core/security/EncryptionPolicy` |
| `pdf/cos/PublicKeySecurityHandler.{h,cpp}` | ○ (S10) | Certificate-based encryption: per-recipient key envelopes and per-recipient permissions. | OpenSSL, `platform::ICertificateStore` |
| `pdf/cos/IncrementalWriter.{h,cpp}` | ○ (S3) | **Appends** changed objects, a new xref section, and a `/Prev`-chained trailer, leaving the original bytes untouched. The reason a signature can survive a later edit. → `tst_roundtrip`. | `pdf/cos/XrefTable`, `pdf/ops/MutationSinkImpl` |
| `pdf/cos/FullWriter.{h,cpp}` | ○ (S8) | Save-As: renumber, drop unreachable objects, compact, choose object-stream packing, and target a PDF version. | `pdf/cos/ObjectAllocator`, `pdf/ops/OptimizeOps` |
| `pdf/cos/Linearizer.{h,cpp}` | ○ (S8) | Fast-web-view output: first-page object ordering, hint streams. | `pdf/cos/FullWriter` |
| `pdf/cos/ObjectAllocator.{h,cpp}` | ○ (S3) | Object-number allocation and the free list, shared by both writers. | `pdf/cos/IncrementalWriter`, `pdf/cos/FullWriter` |
| `pdf/cos/ContentStreamParser.{h,cpp}` | ○ (S5) | Content-stream operators into a token stream with graphics-state tracking — the substrate for text extraction, redaction, and object-level editing. Pure → `tst_contentstream`. | `pdf/render/PdfiumTextExtractor` (cross-check), `pdf/ops/RedactionOps` |
| `pdf/cos/ContentStreamBuilder.{h,cpp}` | ○ (S6) | Emits well-formed content streams for new appearances, watermarks, header/footer text, and composed reflow pages. | `pdf/ops/AppearanceStreamBuilder`, `core/reflow/ReflowToFixedComposer` |

### 3.10 `pdf/render/` — the PDFium facade (read and raster only)

PDFium is linked as a **DLL through its `FPDF_*` C API**, which is what makes it consumable from our
GCC build regardless of how PDFium itself was compiled ([`Tech-Stack-Plan.md` A6](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified)).
Every file below therefore obeys one extra rule on top of the layering ones: **no C++ object, no
exception, no `FILE*`, and no heap-ownership transfer crosses the PDFium boundary** — buffers and
callbacks only. That was already the facade's design; it is now also an ABI requirement.

| File | Status | Role | Interacts with |
|---|---|---|---|
| `pdf/render/PdfiumInit.{h,cpp}` | ○ (S2) | Library init/teardown, the memory and font-hook configuration, and the one place PDFium's global state is touched. | PDFium |
| `pdf/render/PdfiumDocument.{h,cpp}` | ○ (S2) | Opens a document (including password-protected), exposes the page count and page tree, and hydrates `core/doc/Document` from it. | PDFium, `core/doc/Document`, `pdf/DocumentBackendImpl` |
| `pdf/render/PdfiumRasterizer.{h,cpp}` | ○ (S2) | Implements `IPageRasterizer`: renders one `RenderRequest` into a bitmap at the requested device resolution, honouring layer visibility and the render flags. Thread-confined per worker. | PDFium, `core/render/RenderRequest` |
| `pdf/render/PdfiumTextExtractor.{h,cpp}` | ○ (S5) | Implements `ITextExtractor`: characters with rects, direction, and font identity, feeding `PageTextModel`. | PDFium, `core/text/PageTextModel` |
| `pdf/render/PdfiumFormFill.{h,cpp}` | ○ (S9) | PDFium's form-fill environment for rendering live field appearances and widget states. | PDFium, `core/doc/FormModel`, `ui/viewer/FormOverlay` |
| `pdf/render/FontResolverBridge.{h,cpp}` | ○ (S2) | Routes PDFium's missing-font callbacks to `platform::IFontResolver`, so substitution policy lives on our side of the boundary. | PDFium, `platform::IFontResolver` |
| `pdf/render/GlyphCache.{h,cpp}` | ○ (S2) | Rasterized-glyph cache shared across tiles and pages, keyed by font + size + device resolution. | FreeType, `pdf/render/PdfiumRasterizer` |

### 3.11 `pdf/ops/` — mutation realization (the engine side of every command)

| File | Status | Role | Interacts with |
|---|---|---|---|
| `pdf/ops/MutationSinkImpl.{h,cpp}` | ○ (S3) | Implements `MutationSink`: receives **intent** from a command and turns it into COS object edits queued for the next save. The single funnel every mutation passes through on the engine side. | `core/backend.h`, all of `pdf/ops/*`, `pdf/cos/IncrementalWriter` |
| `pdf/ops/PageOps.{h,cpp}` | ○ (S3 rotate, S7 rest) | Page-tree object edits: rotation, insert, delete, move, duplicate, extract, box changes, scaling. | `pdf/cos/*`, `MutationSinkImpl` |
| `pdf/ops/AnnotationOps.{h,cpp}` | ○ (S6) | Annotation dictionary creation and edits, and per-page `/Annots` maintenance. | `pdf/ops/AppearanceStreamBuilder`, `pdf/cos/*` |
| `pdf/ops/AppearanceStreamBuilder.{h,cpp}` | ○ (S6) | Generates `/AP` normal, rollover, and down appearance streams for every annotation and widget kind — so annotations look identical in other viewers. | `pdf/cos/ContentStreamBuilder`, `core/doc/Annotation` |
| `pdf/ops/FormFieldOps.{h,cpp}` | ○ (S9) | AcroForm dictionary, field hierarchy, widget annotations, `/DA` strings, and static-XFA normalization. | `pdf/cos/*`, `core/doc/FormModel` |
| `pdf/ops/RedactionOps.{h,cpp}` | ○ (S10) | **Destructive** removal: rewrites content streams to delete covered text and image regions, strips the objects, and writes the overlay. The one operation whose correctness is a security property, not a quality one. | `pdf/cos/ContentStreamParser`, `pdf/cos/ContentStreamBuilder`, `core/security/RedactionPlan` |
| `pdf/ops/SanitizeOps.{h,cpp}` | ○ (S10) | Removes each hidden-content category and verifies afterwards that it is gone. | `core/security/HiddenContentScanner`, `pdf/cos/*` |
| `pdf/ops/EncryptionOps.{h,cpp}` | ○ (S10) | Applies, changes, and removes encryption, re-keying every stream and string in the file. | `pdf/cos/StandardSecurityHandler`, `pdf/cos/PublicKeySecurityHandler` |
| `pdf/ops/SignatureOps.{h,cpp}` | ○ (S11) | Implements `ISigner`: reserves the `/Contents` byte range, hashes the exact bytes, calls `ICryptoProvider` for the CMS blob, embeds timestamp and revocation data, and reads signatures back for validation. | `platform::ICryptoProvider`, `core/security/SignatureRequest`, `pdf/cos/IncrementalWriter` |
| `pdf/ops/DecorationOps.{h,cpp}` | ○ (S7) | Watermark, header/footer, and Bates content generation, each tagged so it can be found and removed again later. | `pdf/cos/ContentStreamBuilder`, `core/cmd/commands/DecorationCommands` |
| `pdf/ops/ImpositionOps.{h,cpp}` | ○ (S7) | N-up and booklet composition into a new document, with marks, gutters, and creep. | `pdf/cos/*`, `pdf/ops/PageOps` |
| `pdf/ops/FlattenOps.{h,cpp}` | ○ (S8) | Bakes annotations, fields, layers, and transparency into page content. | `pdf/ops/AppearanceStreamBuilder`, `pdf/cos/ContentStreamBuilder` |
| `pdf/ops/OptimizeOps.{h,cpp}` | ○ (S8) | Image recompression, font subsetting, object discard, cleanup, and the audit-space-usage report. | `pdf/cos/filters/*`, `pdf/cos/FullWriter` |
| `pdf/ops/ExportOps.{h,cpp}` | ○ (S8) | Page raster export, embedded-image export, and text/HTML export. Office export dispatches to `IOfficeBridge` or a first-party writer per risk R3. | `pdf/render/PdfiumRasterizer`, `platform::IOfficeBridge` |
| `pdf/ops/ConformanceChecker.{h,cpp}` | ○ (S8) | Validates and reports PDF/A, PDF/X, and PDF/UA conformance per object, with page references. | `pdf/cos/*`, `core/doc/TagTree`, `ui/dialogs/ConformanceReportDialog` |
| `pdf/DocumentBackendImpl.{h,cpp}` | ○ (S2) | Implements `IDocumentBackend`: open, close, hydrate the model, hand out the rasterizer / extractor / sink / signer. | `pdf/render/*`, `pdf/ops/*`, `core/backend.h` |
| `pdf/BackendFactory.{h,cpp}` | ○ (S2) | `pdf::createBackend()` — the one exported symbol `app/` calls. Nothing else in the program names a `pdf/` type. | `app/CompositionRoot` |

### 3.12 `platform/` — the OS abstraction

| File | Status | Role | Interacts with |
|---|---|---|---|
| `platform/platform.h` | ○ (S1) | **Interfaces only**: the sixteen from [`Tech-Stack-Plan.md` §4.3](./Tech-Stack-Plan.md#43-what-qt-gives-us-for-free-versus-what-needs-platform-code), plus the `Platform` bundle struct and `createPlatform()`. No implementation, no Win32 header. | Implemented by `platform/win/*`; consumed by `core/`, `ui/`, `app/` |
| `platform/win/WinSingleInstanceBus.{h,cpp}` | ○ (S1) | Named-object instance detection, file-list handoff, and raise-to-foreground. | `app/SingleInstance` |
| `platform/win/WinDisplayTopology.{h,cpp}` | ○ (S2) | Monitor enumeration with per-monitor DPI, and change notification on topology or scale changes. | `core/render/DpiPolicy`, `core/settings/SessionState` |
| `platform/win/WinAcceleratedSurface.{h,cpp}` | ○ (S2) | Accelerated presentation of rendered tiles, with a software fallback when no device is available. | `ui/viewer/TilePresenter` |
| `platform/win/WinFontResolver.{h,cpp}` | ○ (S2) | Installed-font enumeration and fallback selection for a missing embedded document font. | `pdf/render/FontResolverBridge`, `core/reflow/Shaper` |
| `platform/win/WinShellIntegration.{h,cpp}` | ○ (S1 assoc, S15 rest) | File-type registration and default-handler status, context-menu verbs, thumbnail provider, preview handler, index filter, jump list, taskbar progress. | `core/settings/RecentFiles`, `ui/shell/MainWindow` |
| `platform/win/WinRichClipboard.{h,cpp}` | ○ (S5) | Bitmap, vector-metafile, and HTML clipboard flavours beyond Qt's text/image. | `core/text/TextSelection`, `ui/viewer/tools/SnapshotTool` |
| `platform/win/WinCertificateStore.{h,cpp}` | ○ (S10 recipient lookup, S11 full) | Enumerates digital IDs and root certificates from the OS store; builds chains; resolves certificate-encryption recipients. | `core/security/SignatureValidator`, `pdf/cos/PublicKeySecurityHandler`, `ui/dialogs/DigitalIdManager` |
| `platform/win/WinCryptoProvider.{h,cpp}` | ○ (S11) | Signs a hash through the OS crypto provider, including smart cards and HSMs; verifies; fetches OCSP and CRL. | `pdf/ops/SignatureOps`, `core/security/SignatureValidator` |
| `platform/win/WinSecretStore.{h,cpp}` | ○ (S11) | OS-key-protected storage for signing-ID passwords and service tokens. | `core/security/SignatureRequest`, `core/settings/Settings` |
| `platform/win/WinPrinting.{h,cpp}` | ○ (S8) | Printer enumeration and job submission with duplex, tray, and page-range control beyond `QPrinter`. | `ui/dialogs/PrintDialog` |
| `platform/win/WinPrinterDriverService.{h,cpp}` | ○ (S16) | Installs, services, and configures the **virtual PDF printer**, and receives its spooled jobs. | `core/batch/BatchRunner`, the installer |
| `platform/win/WinScanner.{h,cpp}` | ○ (S16) | Imaging-device enumeration and page acquisition with ADF and duplex control. | `ui/dialogs/ScanDialog` |
| `platform/win/WinSpeech.{h,cpp}` | ○ (S14) | Voice enumeration and speech with rate/volume control and word-boundary events for highlighting. | `core/doc/TagTree`, `ui/shell/ReadAloudBar` |
| `platform/win/WinOfficeBridge.{h,cpp}` | ○ (S8 export, S12 import, S16 add-ins) | Drives the installed office suite for high-fidelity conversion in both directions; hosts the add-ins' entry points. Which direction uses it versus a first-party writer is open decision O1. | `pdf/ops/ExportOps`, `core/create/DocumentBuilder`, `core/batch/BatchRunner` |
| `platform/win/WinBrowserBridge.{h,cpp}` | ○ (S16) | Native-messaging host for the browser extensions. | `app/CommandLine` |
| `platform/win/WinRightsManagement.{h,cpp}` | ○ (S16) | Applies and reads enterprise information-protection labels; tracks and revokes access. | `core/security/SecurityScheme` |
| `platform/win/WinProcessSandbox.{h,cpp}` | ○ (S10) | Launches a restricted child process for protected-view rendering of untrusted files. | `core/security/TrustPolicy`, `app/CompositionRoot` |
| `platform/win/WinPowerSession.{h,cpp}` | ○ (S2) | Sleep/resume, battery-saver render throttling, and session lock/logoff notifications. | `core/render/RenderScheduler` |
| `platform/win/PlatformFactory.{h,cpp}` | ○ (S1) | `platform::createPlatform()` — assembles the Windows bundle. The only exported symbol from this layer. | `app/CompositionRoot` |

### 3.13 `ui/shell/` — the application frame

| File | Status | Role | Interacts with |
|---|---|---|---|
| `ui/shell/MainWindow.{h,cpp}` | ○ (S1) | The frame: ribbon, QAT, document tab bar, sidebar dock, status bar, message bar. Owns the open-document set. Contains **no** document logic — it dispatches `ActionId`s. | `ui/shell/*`, `ui/shell/DocumentWindow`, `core/cmd/ActionRegistry`, `app/Services` |
| `ui/shell/Ribbon.{h,cpp}` | ○ (S1) | The ribbon widget: tabs, groups, sizing behaviour, collapse, contextual-tab show/hide, key tips. Risk A3 is proven or disproven here. | `ui/shell/RibbonTabBuilder`, `core/cmd/ActionRegistry` |
| `ui/shell/RibbonTabBuilder.{h,cpp}` | ○ (S1) | Builds the ribbon from a declarative layout resource plus the `ActionRegistry`, so adding a command never means writing widget code. | `res/ribbon.json`, `core/cmd/ActionRegistry` |
| `ui/shell/QuickAccessToolbar.{h,cpp}` | ○ (S1) | Add/remove/reorder commands, above-or-below placement, reset. | `core/cmd/ActionRegistry`, `ui/shell/UiCustomization` |
| `ui/shell/UiCustomization.{h,cpp}` | ○ (S1) | Serializes the ribbon, QAT, and shortcut customizations for export and team deployment. | `core/settings/Settings`, `core/cmd/KeyBindingStore` |
| `ui/shell/ShortcutBinder.{h,cpp}` | ○ (S1) | Watches focus to compute the active `KeyContext`, resolves a chord through `KeyBindingStore`, and dispatches. **The file that makes one chord mean two things.** | `core/cmd/KeyBindingStore`, `core/cmd/KeyContext`, `core/cmd/ActionRegistry` |
| `ui/shell/DocumentTabBar.{h,cpp}` | ○ (S1) | Tabs, reorder, tear-off to a new window, re-dock, middle-click close, overflow. | `ui/shell/MainWindow`, `ui/shell/DocumentWindow` |
| `ui/shell/DocumentWindow.{h,cpp}` | ○ (S1) | Per-document container: owns the `Document`, its `CommandJournal`, the viewer, the split container, and the pane set. The unit that a tab represents. | `core/doc/Document`, `core/cmd/CommandJournal`, `ui/viewer/PageView`, `ui/panes/*` |
| `ui/shell/SidebarDock.{h,cpp}` | ○ (S1) | Pane hosting: dock left/right, float, auto-hide tab strip, width memory, per-document open-pane state. | `ui/panes/*`, `core/settings/SessionState` |
| `ui/shell/StatusBar.{h,cpp}` | ○ (S1) | Page-number box, page controls, zoom controls, view-mode buttons, cursor coordinates, background-task progress. | `ui/viewer/ViewportController`, `core/render/RenderScheduler` |
| `ui/shell/MessageBar.{h,cpp}` | ○ (S2) | The document-condition banner: form detected, signed, protected, repaired, in Write mode — each with its action button. | `core/doc/RepairReport`, `core/doc/Permissions`, `core/doc/FormModel` |
| `ui/shell/ToolFinder.{h,cpp}` | ○ (S1) | Fuzzy command search over the `ActionRegistry`, showing each match's chord and running it. | `core/cmd/ActionRegistry`, `core/cmd/KeyBindingStore` |
| `ui/shell/ThemeManager.{h,cpp}` | ○ (S1) | Light, dark, follow-system, and OS high-contrast; palette tokens; the UI font scale. | `core/settings/Settings` |
| `ui/shell/ReadAloudBar.{h,cpp}` | ○ (S14) | Read Aloud transport, voice/rate/volume, and the spoken-word highlight. | `platform::ISpeech`, `core/doc/TagTree` |
| `ui/shell/WindowArranger.{h,cpp}` | ○ (S4) | Tile horizontally/vertically, cascade, merge windows, the window list, and synchronized scrolling between views. | `ui/shell/MainWindow`, `core/settings/SessionState` |

### 3.14 `ui/viewer/` — the page surface

| File | Status | Role | Interacts with |
|---|---|---|---|
| `ui/viewer/PageView.{h,cpp}` | ○ (S2) | The scrolling page surface: paints tiles, hosts overlays, routes input to the active tool, and owns nothing about parsing. | `ui/viewer/TilePresenter`, `ui/viewer/ToolController`, `core/render/PagePresentation` |
| `ui/viewer/TilePresenter.{h,cpp}` | ○ (S2) | Asks the scheduler for the visible and prefetch tiles, paints what has arrived, shows a scaled placeholder for what has not, and cancels what scrolled away. | `core/render/RenderScheduler`, `core/render/TileGrid`, `platform::IAcceleratedSurface` |
| `ui/viewer/PageLayoutEngine.{h,cpp}` | ○ (S2) | Realizes `PagePresentation` geometry into widget coordinates for the current layout mode and zoom. | `core/render/PagePresentation`, `ui/viewer/ViewportController` |
| `ui/viewer/ViewportController.{h,cpp}` | ○ (S2) | Zoom (steps, arbitrary, fit-page/width/height/visible, actual size), scroll, page snapping, auto-scroll, and the view history behind `Alt+←`/`Alt+→`. | `core/render/DpiPolicy`, `ui/shell/StatusBar` |
| `ui/viewer/ToolController.{h,cpp}` | ○ (S4) | The active-tool state machine: selection, sticky-tool mode, cursor, `Esc` cancellation, and the temporary-hand-tool modifier. | `ui/viewer/tools/*`, `core/cmd/ActionRegistry` |
| `ui/viewer/tools/*.{h,cpp}` | ○ (S4–S10) | The tool family, one file each over a common `Tool` base: `HandTool`, `SelectTool`, `MarqueeZoomTool`, `LoupeTool`, `SnapshotTool`, `MarkupTool` (highlight/underline/strikeout/squiggly), `TypewriterTool`, `NoteTool`, `DrawingTool` (line/arrow/rect/oval/polygon/polyline/cloud), `InkTool`, `StampTool`, `MeasureTool`, `RedactTool`, `LinkTool`, `CropTool`, `FieldTool`, `ObjectEditTool`. Each translates gestures into a `Command` — **never** into a document edit. | `core/cmd/commands/*`, `ui/viewer/PageView`, `core/settings/Settings` (tool defaults) |
| `ui/viewer/SelectionOverlay.{h,cpp}` | ○ (S5) | Draws text and marquee selection, drag handles, and the selection context menu. | `core/text/TextSelection` |
| `ui/viewer/AnnotationOverlay.{h,cpp}` | ○ (S6) | Interactive annotation layer: hit-testing, drag/resize handles, pop-ups, and z-ordering above the raster. | `core/doc/AnnotationStore`, `ui/viewer/tools/*` |
| `ui/viewer/FormOverlay.{h,cpp}` | ○ (S9) | Live field widgets over the page: focus, tab order, highlight, validation messages, and the calculation cascade. | `core/doc/FormModel`, `pdf/render/PdfiumFormFill` |
| `ui/viewer/RulerWidget.{h,cpp}` | ○ (S4) | Horizontal and vertical rulers with the unit selection, cursor tracking, and guide dragging. | `core/Units.h`, `ui/viewer/GuideLayer` |
| `ui/viewer/GridGuideLayer.{h,cpp}` | ○ (S4) | Grid rendering, guide storage, and the snap computation for grid, guides, and object edges. | `core/settings/Settings`, `ui/viewer/tools/*` |
| `ui/viewer/SplitViewContainer.{h,cpp}` | ○ (S4) | Horizontal, vertical, and spreadsheet (frozen row/column) splits over one document. | `ui/viewer/PageView` |
| `ui/viewer/FullScreenController.{h,cpp}` | ○ (S4) | Full-screen and presentation mode: transitions, auto-advance, blank-to-black/white, the laser pointer, and monitor choice. | `platform::IDisplayTopology`, `core/settings/Settings` |
| `ui/viewer/ComparisonView.{h,cpp}` | ○ (S5) | Side-by-side and overlay comparison with synchronized scrolling and difference navigation. | `core/text/DocumentComparer`, `ui/panes/DifferencesPane` |

### 3.15 `ui/panes/`, `ui/dialogs/`, `ui/write/`

| File | Status | Role | Interacts with |
|---|---|---|---|
| `ui/panes/ThumbnailsPane.{h,cpp}` | ○ (S4) | Page thumbnails: navigation, multi-select, drag-reorder, cross-document drag, size control, and page-op context actions. Its `KeyContext` is `Thumbnails`. | `core/doc/PageTree`, `core/render/RenderScheduler`, `core/cmd/commands/PageCommands` |
| `ui/panes/BookmarksPane.{h,cpp}` | ○ (S4 view, S7 edit) | Bookmark tree: navigate, rename, re-parent by drag, expand/collapse, and the generators. `KeyContext::Bookmarks`. | `core/doc/Outline`, `core/cmd/commands/BookmarkCommands` |
| `ui/panes/CommentsPane.{h,cpp}` | ○ (S6) | The comment list: group, sort, filter, search, reply, set status and checkmark, and the summary launcher. `KeyContext::Comments`. | `core/doc/AnnotationStore`, `core/cmd/commands/AnnotationCommands` |
| `ui/panes/FieldsPane.{h,cpp}` | ○ (S9) | Field tree, tab-order editing, calculation-order editing, and field selection. `KeyContext::FormsDesign`. | `core/doc/FormModel` |
| `ui/panes/LayersPane.{h,cpp}` | ○ (S7) | Layer tree: visibility, lock, rename, merge, flatten, and per-usage visibility. | `core/doc/LayerTree` |
| `ui/panes/TagsPane.{h,cpp}` | ○ (S7) | Tag tree editing, reading-order overlay, alt text, and the accessibility-check report. | `core/doc/TagTree` |
| `ui/panes/AttachmentsPane.{h,cpp}` | ○ (S6) | Embedded files: list, open, extract, add, delete, with the trust warning. | `core/doc/Attachments`, `core/security/TrustPolicy` |
| `ui/panes/SignaturesPane.{h,cpp}` | ○ (S11) | Signature list with status, details, certificate viewer, view-signed-version, and compare-versions. | `core/security/ValidationReport` |
| `ui/panes/SearchPane.{h,cpp}` | ○ (S5) | Advanced search: scope (document / folder / index / portfolio), all the toggles, the result list, sort, filter, and refine. `KeyContext::Search`. | `core/text/SearchEngine`, `core/text/SearchQuery` |
| `ui/panes/DestinationsPane.{h,cpp}` | ○ (S7) | Named destinations: list, sort, create, rename, delete, navigate. | `core/doc/Destinations` |
| `ui/panes/DocumentMapPane.{h,cpp}` | ○ (S13) | Heading-outline navigation for Write mode. | `core/reflow/ReflowDocument` |
| `ui/panes/StampPalettePane.{h,cpp}` | ○ (S6) | Stamp browsing by category, favourites, and custom-stamp creation. | `core/settings/ProfileStore`, `ui/viewer/tools/StampTool` |
| `ui/panes/DifferencesPane.{h,cpp}` | ○ (S5) | The comparison difference list, with navigation and the report export. | `core/text/DocumentComparer` |
| `ui/panes/LogPane.{h,cpp}` | ○ (S1) | Filterable in-app log viewer and the "collect diagnostics" action. | `core/Log.h` |
| `ui/dialogs/PreferencesDialog.{h,cpp}` | ○ (S1 shell, grows per stage) | The preference pages of [`Features.md` §9.1](./Features.md#91-preferences-groups), driven by `SettingsSchema` so a new key needs no new widget code; shows policy locks; includes the Shortcuts page. | `core/settings/Settings`, `core/settings/SettingsSchema`, `core/cmd/KeyBindingStore` |
| `ui/dialogs/*.{h,cpp}` | ○ (S2–S16) | The dialog family, one file each. *S2–S3:* `RepairReportDialog`, `RecoveryDialog`, `DocumentPropertiesDialog`. *S4:* `GoToPageDialog`. *S5:* `CompareDialog`. *S6:* `CommentSummaryDialog`, `MeasurementScaleDialog`, `CustomStampDialog`. *S7:* `PageOpsDialogs` (rotate/insert/delete/extract/replace/split/crop/renumber/resize), `MergeDialog`, `WatermarkDialog`, `HeaderFooterDialog`, `BatesDialog`, `ImpositionDialog`, `LinkPropertiesDialog`, `AccessibilityReportDialog`. *S8:* `ExportDialog`, `OptimizerDialog`, `ConformanceReportDialog`, `FlattenDialog`, `PrintDialog`. *S9:* `FieldPropertiesDialog`, `TabOrderDialog`, `CalcOrderDialog`, `FormDataDialog`, `DuplicateFieldDialog`. *S10:* `SecurityDialog`, `SecuritySchemeDialog`, `SanitizeDialog`, `RedactionSetupDialog`, `TrustManagerPage`. *S11:* `SignDialog`, `SignatureAppearanceEditor`, `DigitalIdManager`, `TrustedIdentitiesDialog`, `ValidationPreferencesDialog`, `CertificateViewer`, `EnvelopeDialog`. *S12:* `NewDocumentDialog`, `CreateFromFilesDialog`, `ConversionProfileDialog`. *S13:* `ParagraphDialog`, `TabStopsDialog`, `PageSetupDialog`, `ConversionFidelityDialog`, `TableDialog`, `TocDialog`, `CaptionDialog`, `FootnoteDialog`, `ChartDialog`. *S14:* `BatchEditorDialog`, `BatchReportDialog`, `DiagnosticsDialog`. *S15:* `ActivationDialog`, `UpdateDialog`, `AboutDialog`, `ThirdPartyNoticesDialog`. *S16:* `ScanDialog`, `OcrDialog`, `PortfolioDialog`. Each collects parameters and constructs a `Command` or a service call — **none contains document logic**. | `core/cmd/commands/*`, `core/security/*`, `core/batch/*`, `core/create/*` |
| `ui/write/WriteCanvas.{h,cpp}` | ○ (S13) | The Write-mode editing surface: caret, selection, typing, IME, and drag-and-drop over a `ReflowDocument`. `KeyContext::WriteMode`. | `core/reflow/ReflowDocument`, `core/cmd/commands/ReflowCommands` |
| `ui/write/StyleGallery.{h,cpp}` | ○ (S13) | Style browsing, application, and modification. | `core/reflow/StyleSheet` |
| `ui/write/TableTools.{h,cpp}` | ○ (S13) | Contextual table editing: rows, columns, merge/split, sizing, borders, shading, sort, AutoSum. | `core/reflow/TableModel` |
| `ui/write/TrackChangesBar.{h,cpp}` | ○ (S13) | Change-tracking transport: record, show markup, navigate, accept/reject, and reviewer filtering. | `core/reflow/ChangeTracker` |
| `ui/write/FindReplaceBar.{h,cpp}` | ○ (S5 find, S13 replace) | The find toolbar for both modes, with replace and wildcards available only in Write mode. | `core/text/SearchEngine`, `core/reflow/ReflowDocument` |
| `ui/write/SpellCheckPane.{h,cpp}` | ○ (S13) | Full-pass spell check with suggestions and dictionary management. | `core/reflow/SpellChecker` |

### 3.16 Later-arriving subsystems

These land in Stages 9–16. They have rows now because the roadmap's *Components* lists name them, and
because declaring their interface early is what keeps them from reshaping the core when they arrive.

| File | Status | Role | Interacts with |
|---|---|---|---|
| `core/forms/FieldScript.{h,cpp}` | ○ (S9) | The sandboxed Acrobat-compatible form-JavaScript subset: `this.getField`, `event.value`, `AFNumber_Format`, `AFDate_Format`, `app.alert`. **Enumerated and sandboxed** — no filesystem, no network, no process launch. A call outside the subset is blocked and *reported*. Pure → `tst_fieldscript`. | `core/doc/FormModel`, `core/security/TrustPolicy` |
| `core/create/DocumentBuilder.{h,cpp}` | ○ (S12) | Builds a new `Document` from nothing, from source files, from the clipboard, or from a captured region. A **service, not a command** — it creates a document rather than mutating one. | `core/create/ConversionProfile`, `IDocumentBackend`, `platform::IOfficeBridge`, `platform::IRichClipboard` |
| `core/create/ConversionProfile.{h,cpp}` | ○ (S12) | The named bundle of every conversion setting (page setup, image and font handling, PDF version, conformance, bookmark and link generation). Persisted through `ProfileStore`. | `core/settings/ProfileStore`, `pdf/ops/ContentImport` |
| `pdf/ops/ContentImport.{h,cpp}` | ○ (S12) | Turns imported image, text, HTML, and Markdown content into page content streams. First-party layout — no browser engine (excluded by D2's module list). | `pdf/cos/ContentStreamBuilder`, HarfBuzz via `core/reflow/Shaper` |
| `ui/viewer/tools/RegionCaptureTool.{h,cpp}` | ○ (S12) | Drags a screen region and hands the bitmap to `DocumentBuilder`. | `core/create/DocumentBuilder`, `platform::IDisplayTopology` |
| `core/licensing/{Entitlement,ActivationClient,OfflineActivation}.{h,cpp}` | ○ (S15) | Entitlement state (perpetual / subscription / trial), online activation and deactivation, and the offline signed request/response file exchange. | `platform::ISecretStore`, `QtNetwork`, `ui/dialogs/ActivationDialog` |
| `core/update/{UpdateChecker,DeltaUpdater}.{h,cpp}` | ○ (S15) | Scheduled and on-demand update checks, channel selection, background download, signature verification, delta apply with a full-download fallback. | `QtNetwork`, `core/settings/PolicyOverlay`, `ui/dialogs/UpdateDialog` |
| `core/telemetry/{EventSink,EventCatalog}.{h,cpp}` | ○ (S15) | Opt-in, off by default. `EventCatalog` is the **enumerated, inspectable** list of sendable events; `EventSink` refuses anything not in it, which is what makes "no document content, ever" a mechanical guarantee rather than a promise. | `core/settings/Settings`, `core/settings/PolicyOverlay` |
| `installer/` | ○ (S15) | WiX v4 MSI with per-feature selection (printer driver and shell extensions are separately installable), plus a per-user MSIX. Keeps the Qt DLLs replaceable per the LGPL obligation, and ships the GCC runtime DLLs (`libstdc++-6`, `libgcc_s_seh-1`, `libwinpthread-1`) beside them — never statically linked. WiX packages a file tree and does not care which compiler produced it. | `tools/license-audit` output, `platform/win/WinShellIntegration` |
| `core/ocr/OcrPipeline.{h,cpp}` | ○ (S16) | Implements `IOcrEngine`: page image preprocessing, recognition, confidence reporting, and the searchable-image versus editable-text output modes. | Tesseract, Leptonica, `IPageRasterizer`, `MutationSink` |
| `core/portfolio/*.{h,cpp}` | ○ (S16) | Portfolio container model, component-file operations, cover sheet, and portfolio-wide search and security. | `core/doc/Attachments`, `core/text/SearchEngine` |
| `core/media/*.{h,cpp}` | ○ (S16) | Screen-annotation and rendition models, play areas, activation triggers, and alternate renditions. | `QtMultimedia`, `core/security/TrustPolicy`, `MutationSink` |
| `core/three/*.{h,cpp}` | ○ (S16) | 3D annotation model, U3D/PRC loading, part tree, named views, cross-sections, and 3D measurement. Effectively its own renderer — the largest post-v1 item. | `platform::IAcceleratedSurface`, `MutationSink` |
| `core/review/*.{h,cpp}` | ○ (S16) | Shared-review lifecycle, reviewer tracking, comment merging with conflict resolution, and live collaborative sessions. Blocked on open decision O5. | `core/doc/AnnotationStore`, `QtNetwork` |
| `core/ai/*.{h,cpp}` | ○ (S16) | Summarize, ask-with-citations, translate, TOC proposal, and structured extraction, behind a provider interface. Must be disableable with a **provable** no-transmission guarantee and a per-document opt-out. | `core/text/PageTextModel`, `core/settings/Settings`, `QtNetwork` |
| `core/reflow/TableExtractor.{h,cpp}` | ○ (S16) | *Convert to Writable Excel*: reconstructs a spreadsheet model from a table-heavy PDF and composes back. Depends on S13's converter quality. | `core/text/LayoutAnalyzer`, `core/reflow/TableModel`, `platform::IOfficeBridge` |

### 3.17 `tools/` and `tests/` — the gates

| File | Status | Role | Interacts with |
|---|---|---|---|
| `tools/layer-lint/` | ✅ (S0 · T7) | Parses every `#include` and fails on any outward edge, any `FPDF_` outside `pdf/`, and any filesystem write outside `pdf/` and `core/settings/`. `--explain` prints the include graph, the active rules, and every live escape. Rules live in `rules.json` (data, not code) so widening a layer's permissions is a reviewable diff. Exit codes: 0 clean · 1 violations · **2 could-not-run**, distinct so CI never reads a broken gate as a passing one. Standard library only — no Qt, no vcpkg (D0-1). | CI, the `checks` CMake target |
| `tools/mutation-lint/` | ○ (S3) | Asserts every `Command` subclass is in `CommandRegistry` and has a serialize round-trip test and a revert test. A new command without tests fails the build. | `core/cmd/CommandRegistry`, CI |
| `tools/i18n-lint/` | ○ (S1) | Fails on any user-facing literal not wrapped in `tr()`, and on catalog keys missing from a shipped locale. | `i18n/*.ts`, CI |
| `tools/license-audit/` | ✅ (S0 · T8) | Walks the resolved vcpkg manifest plus the Qt module list and the GCC runtime, emits `THIRD-PARTY.md`, and fails on any SPDX **expression** outside the allowlist. Matching is whole-expression, never a `GPL` substring: `GPL-3.0-or-later WITH GCC-exception-3.1` must pass while bare `GPL-3.0-or-later` must fail. Also asserts no non-free GCC plugin was loaded, which is the condition the exception rests on. | `vcpkg.json`, `cmake/toolchain-mingw.cmake`, `ui/dialogs/ThirdPartyNoticesDialog`, CI |
| `tools/golden-update/` | ○ (S2) | Regenerates `golden/` reference rasters, requiring an explicit reason so a rendering regression cannot be laundered into a new baseline. | `golden/`, `tests/golden/` |
| `tests/unit/` | ✅ (S0 · T11) | Catch2 suites over `core/` and `pdf/`, linked **without** `recto_ui` and `recto_platform` — the standing proof that the boundary holds. | `core/*`, `pdf/*` |
| `tests/golden/` | ○ (S2) | Renders corpus pages at fixed resolutions and compares against `golden/` with a perceptual threshold; writes a diff image on failure. | `pdf/render/PdfiumRasterizer`, `golden/` |
| `tests/roundtrip/` | ○ (S3) | Open → command → incremental save → reopen: asserts the mutation landed, untouched objects are byte-identical, and pre-existing signatures still validate. | `pdf/cos/IncrementalWriter`, `corpus/` |
| `tests/fuzz/` | ○ (S3) | libFuzzer targets over `pdf/cos/Lexer` and `pdf/cos/Parser`, seeded from `corpus/malformed/`. Built by the `portable-fuzz` preset, **not** by the Windows GCC build, which has no libFuzzer; findings are reproduced in `x64-debug` before they count as fixed. | `pdf/cos/*` |
| `cmake/toolchain-mingw.cmake` | ✅ (S0 · T3) | The pinned toolchain: `C:\Qt\Tools\mingw1310_64` — GCC 13.1.0, `x86_64-w64-mingw32`, SEH, posix, msvcrt — as a cache-variable default, plus the shared warning/charset/subsystem flags. **Fails configure** on any other compiler, because that is Qt's own bundled GCC and a different libstdc++ or CRT against this Qt surfaces as an unexplained crash in Stage 6 rather than an error in Stage 0. | `CMakePresets.json`, `tools/license-audit`, CI |
| `cmake/ToolchainPreflight.cmake` | ✅ (S0 · M-03 fix) | The pin's roots and the three checks that must run **before `project()`**: `RECTO_MINGW_ROOT` exists, `RECTO_QT_PREFIX` exists, the vcpkg toolchain file is reachable through `VCPKG_ROOT`, and the pinned compiler can actually **compile** (`-fsyntax-only`, not `-dumpversion` — the driver answers that one itself while the toolchain is unusable). Before it, a `FATAL_ERROR` from the toolchain file landed mid-detection and CMake printed two red herrings after the real message (I-12), and a compiler that could not start surfaced as *"not able to compile a simple test program"* with no compiler output (I-10). `toolchain-mingw.cmake` includes it too and calls the two cheap checks, so the wording has one home and still fires without a preflight. | `cmake/toolchain-mingw.cmake`, `CMakeLists.txt`, `tests/gates/fixtures/wrong-compiler/` |
| `tools/common/Json.h` | ✅ (S0 · T7) | Minimal JSON reader shared by the gates. Exists because the gates link nothing but the standard library (D0-1), which rules out Qt's JSON and any vcpkg dependency; `layer-lint` reads `rules.json` with it and `license-audit` reads `vcpkg.json` and vcpkg's SPDX SBOM. A reader, not a library — no writing, no schema validation. | `tools/layer-lint/`, `tools/license-audit/` |
| `tests/gates/` | ✅ (S0 · T9) | The gates tested against deliberately bad input: 39 CTest cases on Windows, 25 on Linux. `fixtures/layer-lint/README.md` records which spellings of a violation are covered and which are knowingly not. `RunGateCase.cmake` asserts **both** exit code and diagnostic, because CTest's `WILL_FAIL` cannot tell a correct finding (exit 1) from a crashed gate (exit 2), and `PASS_REGULAR_EXPRESSION` ignores the exit code entirely. Includes the negative controls without which every positive case is satisfied by a gate that rejects everything. | `tools/*`, `cmake/ToolchainAssert.cmake`, CI |
| `tools/spike-pdfium/` | ✅ (S0 · T12) ⤵ | **Throwaway.** Answered risk A6 — a PDFium DLL links from `g++` and opens a document — and is deleted when `pdf/render/PdfiumInit` replaces it at Stage 2. The durable outputs are the recorded verdict and `scripts/fetch-pdfium.ps1`. | `third_party/pdfium/` |
| `tests/bench/` | ○ (S2) | Per-tile render latency at 96/144/192 DPI over the corpus — the measurement that settles risk A2. | `core/render/*`, `pdf/render/*` |

---

### 3.18 Build system, scripts, and CI

The map owed these rows: Stage 0 built a working build system, and a file map that describes every
`.cpp` but none of the machinery that compiles them is the first place this document starts drifting
from the tree.

| File | Status | Role | Interacts with |
|---|---|---|---|
| `CMakeLists.txt` | ✅ (S0 · T2) | Root build definition. Declares the four layer libraries plus `recto`, so the **linker itself** catches a class of layering violation before `layer-lint` runs. Also holds the three build-shape switches: `RECTO_PORTABLE_SUBSET` (core/ + pdf/cos/ only, for the Linux sanitizer job), `RECTO_GATES_ONLY` (tools/ + tests/gates/ with **no Qt and no vcpkg** — what makes the CI gates job possible), and `RECTO_TARGET_ARCH` (arm64 fails here with an explanation rather than three assertions deep). | every layer, `cmake/*`, CI |
| `CMakePresets.json` | ✅ (S0 · T4) | Six configure presets. vcpkg is the outer toolchain with the pin **chainloaded** via `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` — not the reverse. `hostSystemName` conditions hide the Linux presets on Windows and vice versa. | `cmake/toolchain-mingw.cmake`, CI |
| `cmake/ToolchainAssert.cmake` | ✅ (S0 · T3) | TA-1…TA-9: compiler vendor, exact version, triple, SEH, posix, **msvcrt-not-UCRT**, compiler-inside-the-pinned-root, Qt version/prefix, and no non-free GCC plugin. Separate from the toolchain file because CMake re-reads that file for every `try_compile()`. Each failure names the offending value **and the fix**. | `CMakeLists.txt`, `tests/gates/fixtures/wrong-compiler/` |
| `cmake/RectoWarnings.cmake` | ✅ (S0 · T2) | The one place flags are decided. `-Werror` for `core/` and `pdf/`; a shrinking grace list for `ui/`; `-g` in **every** configuration because DWARF in the binary is the only symbolization story this toolchain has. Records why `-fanalyzer` is absent — it false-positives on `std::string` with this GCC. | every target |
| `cmake/RectoQt.cmake` | ✅ (S0 · T2) | `recto_link_qt()` — no target links Qt directly. Records the real link closure into `qt-modules.txt` for `license-audit`, and refuses `Qt6::Charts`/`DataVisualization`/`VirtualKeyboard` at configure time with a named GPL-3.0 diagnostic. | `license-audit`, every Qt-using target |
| `cmake/RectoChecks.cmake` | ✅ (S0 · T9) | The `checks` target. Depends **only** on the two gate executables, never the layer libraries — that is what lets CI run the gates before compiling anything. | `tools/*`, CI |
| `vcpkg.json` | ✅ (S0 · T6) | Every §6.2 dependency, grouped into features so resolution can be **tested now and required later** (D0-3). Only `tests` is built today. | `vcpkg-configuration.json`, `license-audit` |
| `vcpkg-configuration.json` | ✅ (S0 · T5) | Pins the vcpkg baseline commit, so a `git pull` in the vcpkg clone changes nothing and a dependency update is an attributable diff. Registers `tools/ports/` as the overlay directory. | `vcpkg.json` |
| `tools/ports/` | ✅ (S0 · T5) | Empty, deliberately. Where a port goes when vcpkg lacks a working `x64-mingw-dynamic` build — **never** a reason to change the triplet. A5 found no such port. | `vcpkg-configuration.json` |
| `scripts/env.ps1` | ✅ (S0 · T1) | Dot-sourced; puts the pinned toolchain on `PATH` and sets `VCPKG_ROOT` itself, so the build never depends on machine state. Detects being *run* rather than dot-sourced and says so. | every documented command, CI |
| `scripts/bootstrap.ps1` | ✅ (S0 · T1) | Verifies prerequisites and reports what to fix. **Installs and downloads nothing** — a bootstrap that silently mutates a build box is not one to trust on a build box. | `scripts/env.ps1` |
| `scripts/fetch-pdfium.ps1` | ✅ (S0 · T12) | Fetches the pinned PDFium, verifies its **SHA-256** and refuses on mismatch, then generates the GNU import library upstream does not ship (`gendef` + `dlltool`). Idempotent. | `third_party/pdfium/` |
| `third_party/pdfium/` | ✅ (S0 · T12) | Fetched, never committed — only its `README.md` is. The reproducibility is in the pin, not in the bytes. | `scripts/fetch-pdfium.ps1` |
| `corpus/make-minimal.py` | ✅ (S0 · T12) | Generates `corpus/minimal.pdf`. A generator rather than a committed blob because a PDF xref stores **absolute byte offsets**: a one-character edit invalidates every one of them. Stage 2 owns the real corpus. | `corpus/`, `tools/spike-pdfium/` |
| `THIRD-PARTY.md` | ✅ (S0 · T8) | **Generated** by `license-audit`, never hand-edited, and committed per release (§8.2). The About → Third-Party Notices dialog is built from the same data, so a dependency cannot ship without appearing in both. | `tools/license-audit/` |
| `.github/workflows/ci.yml` | ✅ (S0 · T13) | Three jobs. `windows` and `portable-asan` both `needs: gates`, so a layering or licence violation stops everything before a runner installs a toolchain. `windows` is **self-hosted**: aqtinstall cannot fetch Qt 6.11.1, and downgrading the pin to suit CI was rejected. | everything |
| `.gitattributes` | ✅ (S0 · T1) | Marks `corpus/**` and `golden/**` binary. Not cosmetic: Git's text heuristic misreads a minimal uncompressed PDF, and CRLF injection would shift every xref offset — silently turning a fixture into a different file than the test was written against. | `corpus/`, `golden/` |

---

## 4. Breadcrumb convention

When a planned file is folded into another, its row **stays** with status ⤵ and a Role of
"folded into `X` — see that row", because a reader chasing a reference from an older stage needs the
trail. The same applies to renames. Rows are never silently deleted.

*(No foldings yet — nothing has been built.)*

---

## 5. Primary flows

### A. Startup and wiring — `app/` is the only integrator

```
main()
 ├─ set per-monitor DPI awareness            // BEFORE any window exists
 ├─ CrashHandler::install()
 ├─ CommandLine::parse(argv)
 │    └─ if a headless verb → BatchRunner ─▶ exit(code)   // no window ever created
 ├─ SingleInstance::claim()
 │    └─ if secondary → ISingleInstanceBus.forward(files) ─▶ exit(0)
 └─ CompositionRoot::build()
      ├─ Settings(SettingsSchema, %APPDATA%\Recto\settings.ini)
      ├─ PolicyOverlay(machine policy)          ─▶ layered under Settings
      ├─ platform = platform::createPlatform()  // ← the ONLY createPlatform() call
      ├─ backend  = pdf::createBackend()        // ← the ONLY createBackend() call
      ├─ ActionRegistry + KeyBindingStore(§8 defaults + user overrides)
      ├─ RenderScheduler(worker pool, RenderCache, platform.power)
      ├─ Services{ backend, platform, settings, actions, scheduler }
      └─ MainWindow(Services&)                  // receives INTERFACES, not concretes
           └─ RecoveryPrompt::checkAndOffer()
```
**Direction shown:** `app/` names both concretes; `MainWindow` receives only interfaces and could
be constructed in a test against mocks with no PDFium and no Win32.

### B. Opening a document — Explorer double-click into an already-running instance

```
Explorer double-click a .pdf
 └─ WinShellIntegration's registered verb launches recto.exe <path>
     └─ SingleInstance::claim() → already running
         └─ WinSingleInstanceBus.forward([path])        // platform/win
             └─▶ (primary process) MainWindow::openFiles([path])
                  └─ IDocumentBackend.open(path, PasswordCallback)
                      ├─ PdfiumDocument opens; on /Encrypt → prompt via callback
                      ├─ CosParser + XrefTable read the object graph
                      │    └─ on a broken xref → XrefRepair ─▶ RepairReport
                      └─▶ hydrate core/doc/Document {PageTree, Outline, Permissions, …}
                  ├─ DocumentWindow(Document, CommandJournal) added as a tab
                  ├─ MessageBar shows: repaired / protected / signed / has-form
                  └─ PageView requests the first visible tiles      → flow C
```
**Direction shown:** `ui/` asked an *interface* to open a file. It never learned that PDFium exists,
and `platform/win` was reached only through `ISingleInstanceBus`.

### C. Tiled asynchronous rendering at the right DPI

```
PageView scrolled / zoomed
 └─ ViewportController → PagePresentation → visible page rects
     └─ DpiPolicy(zoom, IDisplayTopology.scaleFor(window)) ─▶ device resolution
         └─ TileGrid(page, resolution, viewport) ─▶ {visible, prefetch, evict}
             └─ TilePresenter
                  ├─ RenderCache.lookup(RenderRequest)  → hit  ─▶ paint immediately
                  └─ miss ─▶ RenderScheduler.enqueue(RenderRequest, priority)
                              └─ worker thread: IPageRasterizer.raster(request)
                                   └─ PdfiumRasterizer + GlyphCache          // pdf/render
                                        └─ FontResolverBridge → IFontResolver // missing font
                              ◀─ deliver to UI thread ─▶ RenderCache.put ─▶ repaint tile
   (a tile scrolled out of view is cancelled, not rendered)
```
**Direction shown:** the UI thread never parses or rasterizes; `core/render/` decides *policy* and
holds no PDFium type; the only PDFium call sits in `pdf/render/`.

### D. A mutation, end to end — the cross-cutting live path *(annotated)*

Rotating page 3 by 90°, triggered from the Thumbnails pane by `Ctrl+R`
([`Features.md` §8 → Page Thumbnails pane](./Features.md#page-thumbnails-pane)):

```
key press  Ctrl+R  while the Thumbnails pane has focus
 └─ ShortcutBinder: activeContext() == KeyContext::Thumbnails
     └─ KeyBindingStore.lookup(Ctrl+R, Thumbnails) ─▶ ActionId::RotatePages
         └─ ActionRegistry.dispatch(RotatePages, Thumbnails)
             └─ ui/dialogs/PageOpsDialogs::rotate() collects {pages:[3], angle:90}
                 └─ journal.run(make_unique<RotatePageCommand>({3}, 90))     ◀── ui/ STOPS HERE
                     │                                                           it never touches
                     ├─ Permissions::may(Assemble)?  no ─▶ refuse, nothing changed   the document
                     ├─ Command::apply(Document&, MutationSink&)
                     │    ├─ PageTree.page(3).setRotation(90)      // core model only
                     │    └─ sink.setPageRotation(3, 90)           // INTENT, not bytes
                     │         └─ MutationSinkImpl → PageOps → CosDict /Rotate  // pdf/ops
                     ├─ RecoveryJournal.append(cmd.serialize()) + fsync
                     ├─ Revision++ ; dirtyChanged(true)
                     └─ mutated(Scope{page 3})
                          ├─ RenderCache invalidates only page 3's tiles
                          ├─ PageView repaints page 3                 (not the document)
                          └─ ThumbnailsPane re-renders thumbnail 3
```
**Direction shown, and why this trace is the most important one in the document:** `ui/`'s entire
mutation vocabulary is *construct a `Command`, hand it to the journal*. `core/` expresses **intent**
to a `MutationSink` interface and never learns how a rotation is stored. `pdf/ops/` is the only code
that touches a COS object. Undo is free — `Ctrl+Z` calls `revert()` down the same chain. Crash
recovery is free — the serialized command is already on disk. Invalidation is precise — `Scope`
said one page. **Every** mutation in the product, from a highlight to applying redactions to a
keystroke in Write mode, follows exactly this shape.

### E. Adding a highlight — a feature flow

`Ctrl+Alt+H`, then drag across text ([`Features.md` §4.1](./Features.md#41-annotation-tools)):

```
Ctrl+Alt+H ─▶ KeyContext::Viewer ─▶ ActionId::HighlightTool
 └─ ToolController.activate(MarkupTool{Highlight})
     └─ drag over text
         ├─ TextSelection.hitTest → run-offset range     // core/text
         │    └─ over PageTextModel hydrated by ITextExtractor
         └─ release ─▶ AddAnnotationCommand{Highlight, quadPoints, colour from tool defaults}
             └─ journal.run(...)                          // ← identical shape to flow D
                 ├─ AnnotationStore.add(Annotation)       // core model
                 └─ sink.addAnnotation(...)
                      └─ AnnotationOps + AppearanceStreamBuilder ─▶ /Annots + /AP stream
             ─▶ mutated(Scope{page}) ─▶ AnnotationOverlay + CommentsPane refresh
```
The `/AP` stream is generated by us, not left to the viewer, so the highlight looks the same in
Acrobat and in a browser.

### F. Save and crash recovery

```
Ctrl+S ─▶ ActionId::Save
 └─ IDocumentBackend.save(Incremental)
     └─ MutationSinkImpl.flush() ─▶ IncrementalWriter
          ├─ append only the changed objects
          ├─ append a new xref section; trailer /Prev → the old one
          └─ original bytes untouched  ⇒ a pre-existing signature still validates
     └─ journal.markSaved(revision) ─▶ dirtyChanged(false) ─▶ RecoveryJournal.truncate()

… process killed before saving …

next launch
 └─ RecoveryPrompt: RecoveryJournal.discover()
     └─ meta.json's source hash still matches?
         ├─ yes ─▶ offer replay ─▶ for each line: CommandRegistry.deserialize → journal.run()
         │           └─ a failure at line k keeps 1..k-1 and reports k     // never all-or-nothing
         └─ no  ─▶ offer "recover to a new file" instead of replaying onto changed content
```

### G. Find, then advanced search

```
Ctrl+F ─▶ KeyContext::Search ─▶ FindReplaceBar
 └─ SearchQuery{pattern, flags, include:{text,comments,bookmarks,fields,attachments}}
     └─ SearchEngine.find(Document&, query)
          ├─ PageTextModel per page (lazily, via ITextExtractor)
          ├─ DocumentIndex built on the first search ⇒ the second is instant
          └─ AnnotationStore / Outline / FormModel for the non-page classes
     ─▶ hits ─▶ SelectionOverlay highlights ; SearchPane lists page + snippet
Ctrl+Shift+F ─▶ SearchPane scope = folder or catalog index
 └─ IIndexer (CatalogIndex) ─▶ cross-file hits, ranked
```

### H. Convert to Writable — crossing into the second document model

```
ActionId::ConvertToWritable
 └─ FixedToReflowConverter.convert(Document&)
     ├─ PageTextModel + LayoutAnalyzer ─▶ lines → blocks → reading order → columns/tables
     ├─ TagTree consulted where the document is tagged (much better fidelity)
     └─▶ ReflowDocument{sections, paragraphs, runs, tables, styles}   + FidelityReport
 ├─ ConversionFidelityDialog shows what could not be reconstructed ─▶ user confirms
 └─ DocumentWindow swaps the viewer for ui/write/WriteCanvas ; KeyContext → WriteMode
      └─ typing ─▶ ReflowCommands (coalesced) ─▶ journal.run()   // same shape as flow D

ActionId::PreventFurtherEdits
 └─ ReflowToFixedComposer.compose(ReflowDocument&)
     └─ LineBreaker + Shaper ─▶ page layout ─▶ sink.replacePageContent(...)
         └─ ContentStreamBuilder ─▶ new content streams ; the reflow model is dropped
```
**Direction shown:** the same journal, the same sink, the same `Ctrl+Z`. Write mode is a second
*model*, not a second mutation mechanism — which is exactly why it does not double the architecture.

### I. Sign, then validate — logic in `core/`, crypto in `platform/`

```
ActionId::SignDocument
 └─ SignDialog ─▶ SignatureRequest{field, appearance, reason, permittedChanges, PAdES:B-LT}
     └─ ISigner.sign(request)                                  // core/backend.h
         └─ SignatureOps                                        // pdf/ops
              ├─ reserve /Contents; compute the byte ranges
              ├─ hash exactly those bytes
              ├─ ICryptoProvider.signHash(...)  ─▶ WinCryptoProvider  // smart card / HSM / store
              ├─ fetch a timestamp + OCSP; embed for LTV
              └─ IncrementalWriter appends the signature dictionary

open a signed file
 └─ SignatureValidator.validate(sig)                            // core/security — pure logic
      ├─ byte-range integrity check
      ├─ ICertificateStore.buildChain(...) ─▶ WinCertificateStore
      ├─ ICryptoProvider.verify(...) + revocation + timestamp-vs-signer-clock
      └─▶ ValidationReport ─▶ SignaturesPane
```
**Direction shown:** every signature *decision* is testable headlessly against a mock
`ICryptoProvider`; no `core/` file includes a Win32 crypto header.

### J. A settings change propagating

```
PreferencesDialog (built from SettingsSchema) ─▶ Settings.set("pageDisplay/lineWeight", true)
 └─ Settings.changed("pageDisplay/lineWeight")
     ├─ RenderCache invalidates every tile whose RenderRequest carried the old flag
     ├─ RenderScheduler re-enqueues the visible set
     └─ StatusBar updates its indicator
   (a key locked by PolicyOverlay is shown disabled, with which policy locked it)
```

### K. A batch run with no UI at all

```
recto.exe --batch redact-and-secure.seq --input C:\in --output C:\out --quiet --json
 └─ CommandLine::parse ─▶ headless verb ─▶ NO MainWindow is ever constructed
     └─ BatchRunner.run(sequence, inputs)
          └─ per file: backend.open ─▶ for each action: ActionRegistry.dispatch(ActionId)
                        └─ journal.run(Command)      // the SAME path as flow D
             ─▶ backend.save ─▶ BatchReport row
     └─▶ BatchReport as JSON on stdout ; documented exit code
```
**Direction shown:** this flow is the standing proof that the action layer is real. If any feature
were implemented in a widget's slot instead of behind an `ActionId` and a `Command`, it would be
unreachable here — and that is exactly the regression this flow's test catches.

---

## 6. Why this shape

- **Portability** — the sixteen `platform/` interfaces are wide but shallow, and *none* is in the
  document-editing path. A future `platform/mac/` implements the same header; `core/` and `pdf/`
  need no change. The list was derived from the "needs platform code" table in
  [`Tech-Stack-Plan.md` §4.3](./Tech-Stack-Plan.md#43-what-qt-gives-us-for-free-versus-what-needs-platform-code),
  not invented.
- **Testability** — `core/` links `QtCore` and nothing else. `tests/unit/` deliberately links
  *without* `recto_ui` and `recto_platform`, so the boundary is verified by the build on every run
  rather than asserted in a document. Every page operation, form calculation, search, signature
  decision, reflow line break, and command inverse is a headless unit test.
- **The mutation invariant holds by construction** — `ui/` has no vocabulary for changing a
  document other than constructing a `Command`. Undo, redo, crash recovery, precise invalidation,
  batch replay, and signature-preserving incremental save are therefore not five features; they are
  five consequences of flow D, and `tools/mutation-lint` makes a sixth impossible to add without
  tests.
- **The engine is replaceable in halves** — reading goes through `IPageRasterizer` and
  `ITextExtractor`; writing goes through `MutationSink`. Swapping the read half (if risk A2 fails)
  or the write half touches `pdf/` and nothing else.
- **One dispatch bus** — `ActionId` + `KeyContext` is what lets `Ctrl+K` mean *Preferences* while
  reading and *Insert hyperlink* while writing, and simultaneously lets flow K reach every feature
  with no window on screen. Those two properties come from the same file.
- **Two document models, one mutation mechanism** — `core/doc/Document` (fixed) and
  `core/reflow/ReflowDocument` (reflowable) are honestly separate, because the reference product's
  dual-mode behaviour tells us they *are* separate. Sharing the journal instead of the model is what
  keeps that honesty from doubling the architecture.
