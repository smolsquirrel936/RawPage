# RawPage — Architecture & File Map

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
              ITextExtractor, ISigner, IOcrEngine,   IScanner, IFontResolver,
              IIndexer, MutationSink                 IAcceleratedSurface, … (15 total)
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

**Three rows are 🟡 and the ✅ rows are these** — the three gates (Stage 0 T7, T8 and **Stage 1 T6**)
with `tools/common/{Json.h,LineScan.h}`, the build-system and script rows of §3.18, the four `core/`
vocabulary headers (Stage 0 T10), the unit and platform harnesses (Stage 0 T11, **Stage 1 T7**), the
three `core/cmd/` action-layer rows (**Stage 1 T3**), the key-binding row (**Stage 1 T4**), the three
`core/settings/` rows (**Stage 1 T5**), the two `i18n/` data rows (**Stage 1 T6**), and
`platform/platform.h` with `PlatformFactory` and `WinSingleInstanceBus` (**Stage 1 T7**);
🟡: `platform/win/WinShellIntegration` (association done, the Stage 15 shell handlers owed), plus
`app/main.cpp` and
`app/CrashHandler.{h,cpp}`, both opened by **Stage 1 T1** and each carrying an explicit
*Real as of T1* / *Still owed* split in its Role cell. Everything else is ○ with its stage tag: no
*document* code exists yet — nothing in `core/doc/`, `pdf/` or `ui/` — only Stage 0's scaffolding,
the shared vocabulary every later layer is written in, Stage 1's entry point, its action and settings
layers, and its OS boundary. The stage tags are the contract between this map and the roadmap.

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
| `app/main.cpp` | ✅ (S1 T1, T8) | Entry point. Sets DPI awareness before any window exists, installs the crash handler, parses the command line, hands off to `CompositionRoot`, runs the Qt event loop. **Real as of T1:** per-monitor-v2 awareness and `CrashHandler::install()` both before the first Qt object, the `app/` log sink, and the four diagnostic switches the testing guide measures the entry point with (`--version`, `--print-args`, `--print-dpi-awareness`, `--crash-test`). **Real as of T8:** flow A end to end — parse → compose → claim → translators → window → `exec()`. A secondary instance forwards and exits 0 without ever creating a window; `--dump-ui-strings` composes every service, loads the catalogs and exits 0 without one either, which is what keeps a headless smoke test meaningful now that an ordinary launch runs an event loop. It is also the **only** translation unit permitted to touch `argv`, enforced by `layer-lint`'s non-escapable `argv-path-read` rule. **Still owed:** nothing for Stage 1; Stage 3 adds `RecoveryPrompt` and Stage 14 the batch verbs. | `app/CrashHandler`, `app/CommandLine`, `app/CompositionRoot`, `app/Application` |
| `app/Application.{h,cpp}` | 🟡 (S1 T8) | `QApplication` subclass: application lifetime, translator loading, last-window-closed policy. **Real as of T8:** the translator stack, resolving `--lang` → `language/uiLocale` → the system locale and loading `<application dir>/i18n/rawpage_<locale>.qm`. **TWO translators are installed, `en` first and the active locale on top of it** — because the catalog key IS the source string here (T6), an untranslated string falls back to its own key rather than to English, so the source catalog goes underneath as the fallback layer. **Still owed:** session save/restore on quit (`SessionState` is Stage 4). Theme application is delegated to `ui/shell/ThemeManager`, which owns it. | `core/settings/Settings`, `ui/shell/ThemeManager`, `core/settings/SessionState` |
| `app/CompositionRoot.{h,cpp}` | ✅ (S1 T8) | **The wiring hub.** Constructs `platform::createPlatform()` and `pdf::createBackend()`, the `Settings` store, the `ActionRegistry`, the `RenderScheduler` and its worker pool, then builds `MainWindow` injecting only interfaces. The only file that knows a concrete backend exists. **Real as of T8:** `Settings`, `RecentFiles`, `ActionRegistry`, `KeyBindingStore` (seeded from the defaults table, then the per-user overrides out of `settings.ini`, reporting how many were accepted and how many named an action that no longer exists), and `platform::createPlatform()`. It also owns the **logging settings**: `logging/level` sets the threshold and `logging/subsystems` becomes a filter wrapped around the sink `main.cpp` supplied, re-applied live on a settings change. Its destructor calls `Settings::sync()` — the write nothing had called before T8 (issue I-14). `RenderScheduler` and `createBackend()` arrive at Stage 2. | `pdf::createBackend()`, `platform::createPlatform()`, `core/settings/Settings`, `core/cmd/ActionRegistry`, `core/render/RenderScheduler`, `ui/shell/MainWindow`, `app/Services` |
| `app/Services.h` | ✅ (S1 T8) | The aggregate of injected interfaces passed by reference down the UI tree. Keeps constructors from growing twelve parameters without becoming a service locator. Stage 1 carries `Settings&`, `RecentFiles&`, `ActionRegistry&`, `KeyBindingStore&` and `Platform&`; Stage 2 adds `IDocumentBackend&` and `RenderScheduler&`, both of which are `core/` types and so keep this header inside what `ui/` may name. **REFERENCES, not pointers** — every service here is required for the window to exist at all, which is the opposite of `platform::Platform`, whose null members are the honest state of an unimplemented interface. **It is one of exactly two files `ui/` may look outward to**, and it has its own `layer-lint` layer entry constraining it to `core/` plus `platform/platform.h` — the same treatment `platform/platform.h` gets, for the same reason: without it, a `pdf/` include added here at Stage 2 would hand every widget a dependency on the engine through the one door that is open. | Consumed by `ui/shell/MainWindow`, `ui/shell/DocumentWindow` |
| `app/CommandLine.{h,cpp}` | 🟡 (S1 T8 basic, S14 full) | Parses every CLI verb from [`Features.md` §9.6](./Features.md#96-command-line) into an intent object; runs headless verbs without creating a window; documents exit codes. **Real as of T8:** open-with-file, `--lang`, `--dump-ui-strings`, `--help`, `--`, and the diagnostic vocabulary, with exit code 2 for a usage error. A **pure** function from a `QStringList` to an intent — no filesystem, no Qt GUI type, no side effect — which is why `tst_commandline` lives in the headless suite: `tests/unit` compiles this `.cpp` in rather than linking the executable, and `rawpage_assert_headless()` still resolves to `core/` + `pdf/`. **THERE IS NO argc/argv OVERLOAD AND NEVER WILL BE** — issue I-04 — and `tests/unit/compile/argv-overload.cpp` proves a caller who tries does not compile. **Still owed:** every verb beyond these, and the headless batch path, at Stage 14. | `core/batch/BatchRunner`, `core/cmd/ActionRegistry`, `app/CompositionRoot` |
| `app/CrashHandler.{h,cpp}` | 🟡 (S1 T1) | Unhandled-exception and `SIGABRT` capture, minidump write, "flush the recovery journal" last rites, opt-in report queue. The GCC toolchain emits **DWARF, not PDB**, so the dump is paired with a build ID and symbolized against the archived unstripped binary rather than by a symbol server — decide and build that path here, not at Stage 15. **Real as of T1:** `SetUnhandledExceptionFilter`, a lazily-loaded `dbghelp!MiniDumpWriteDump` writing to `%LOCALAPPDATA%\RawPage\crashes\`, a plain-text sidecar carrying the build id and the last-rites outcome, and the whole symbolization path end to end (`scripts/symbolize.ps1`, guide case A-65). **Still owed:** `SIGABRT` capture, the opt-in report queue (S15), and the real last-rites sink — the hook is installed and invoked, with a no-op body until `RecoveryJournal` arrives at S3. | `core/cmd/RecoveryJournal`, `core/Log`, `platform::IShellIntegration` (dump path) |
| `app/RecoveryPrompt.{h,cpp}` | ○ (S3) | On launch, discovers recovery journals whose source still matches, presents them, and drives replay; reports the command that failed if replay stops early. | `core/cmd/RecoveryJournal`, `core/cmd/CommandJournal`, `ui/dialogs/RecoveryDialog` |
| `app/SingleInstance.{h,cpp}` | ✅ (S1 T8) | Decides first-instance vs. secondary-instance at startup; a secondary instance forwards its file list and exits; the primary raises itself and opens the file. Thin on purpose — the hard parts are `platform::ISingleInstanceBus` (D1-11) and what is left here is **policy**: whether `general/singleInstance` wants the behaviour at all, and what the scope is. **The scope is a hash of the executable's own directory**, so a debug build and a release build on one desktop are two applications rather than one confusing one. A bundle with no bus is `Disabled`, not an error: every launch being its own instance is a usable product; a refusal to start is not. | `platform::ISingleInstanceBus`, `ui/shell/MainWindow` |

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
| `core/doc/TagTree.{h,cpp}` | ○ (S7) | The logical structure tree: tag types, logical order, language attributes, artifact marking. Pure → `tst_tags`. **Also the durable home of Write mode's structure (O2)** — `ReflowToFixedComposer` writes it on compose, and `FixedToReflowConverter` reads it back, **which is why this file survived the 2026-08-23 accessibility removal**: it is Stage 13 infrastructure that happened to also serve accessibility, not the other way round. *Alt text, table-header scope and the `ISpeech` consumer were removed.* | `ui/panes/TagsPane`, `core/text/LayoutAnalyzer`, `core/reflow/{ReflowToFixedComposer,FixedToReflowConverter}` |
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
| `core/cmd/ActionId.def`, `ActionId.h` | ✅ | **One declarative table, four artifacts** (D1-6): the `.def` is an X-macro list of `RAWPAGE_ACTION(Enumerator, "string.id", DefaultContext, "action.catalog.key")`, and the enum, the string↔id maps, the default-context map and the `action.*` key list are all generated from it — so a typo is a build error *on the row that contains it*, verified by negative control. **327 actions declared** across `Features.md` §1–§7 and §9; declaring is not registering. The string id is a permanent public name written to disk, and `RAWPAGE_ACTION_ALIAS` rows keep a renamed id resolving forever (D1-16) — none yet, and the mechanism is tested before its first real use. Ribbon buttons, menu items, shortcuts and CLI verbs all resolve to one of these — never to a slot directly. | `core/cmd/ActionRegistry`, `ui/shell/RibbonTabBuilder`, `app/CommandLine`, `tools/i18n-lint` (reads the `.def` as text) |
| `core/cmd/ActionRegistry.{h,cpp}` | ✅ | `ActionId` → handler, enablement predicate, checked-state, icon, and tooltip; the label comes from `ActionId.def`, not from the caller, so nobody can disagree with the table. `dispatch(ActionId, KeyContext)` is the single entry point for all input. **Two accessors arrived at T11 because a caller finally needed them:** `isToggle()`, the question `isChecked()` cannot answer since it conflates "not a toggle" with "a toggle that is off" — without it `ActionFactory` had never called `setCheckable` and its header's promise about toggles had gone untested for want of a toggle (I-21); and `tooltipKey()`, a `Registration` field that had been unreadable since T3. **Three pinned properties:** an unregistered id returns `NotFound` rather than asserting or silently no-opping; a registered-but-disabled id returns the *distinct* `Unavailable`, so a CLI can tell "does not exist" from "not available right now"; and registration is **not** idempotent — a second claim on an id is an error and the first survives. Not thread-safe by design: composition and dispatch are both main-thread. Pure → `tst_actionregistry`. | `ui/shell/ShortcutBinder`, `ui/shell/Ribbon`, `app/CommandLine`, `core/batch/BatchRunner` |
| `core/cmd/KeyContext.h` | ✅ | The input contexts of [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference): `Global, Common, Viewer, Write, AnnotationTools, PageThumbnails, Bookmarks, Comments, FormsDesign, FormsFill, FindSearch, FullScreen` — **twelve**, being §8's eleven contexts plus `Common`. This enum is why one chord can mean two things. `Common` is present even though focus is never *in* it: resolution is layered — innermost context, then `Common`, then `Global` — and the store must be able to hold a binding at the `Common` layer. `ShortcutBinder::activeContext()` never returns it. | `core/cmd/KeyBindingStore`, `ui/shell/ShortcutBinder` |
| `core/cmd/Chord.h`, `Chord.cpp`, `KeyBindingDefaults.def`, `KeyBindingStore.{h,cpp}` | ✅ | `(chord, KeyContext) → ActionId`, seeded from the §8 defaults, overridable per user, persisted in settings. Resolution is **innermost context, then `Common`, then `Global`** — that order *is* the model. Detects and **reports** (does not forbid) a remap that shadows a `Global` chord, because §8 ships six such collisions by design. Emits the **two-class collision census**: class 1 (a context takes a chord back from `Global`) = **6**, class 2 (a context overrides `Common`, which §8 explicitly permits) = **9**, and Write mode's **12** = 8 over Viewer + 4 over Global + 0 over Common. `shadows()` reports class 1 only. **`Chord` is first-party rather than `QKeySequence`**, which lives in QtGui and `core/` may not touch — `ui/shell/ShortcutBinder` converts at the boundary. Pure → `tst_keybindings`, which **parses `Features.md` §8 itself** and asserts the store matches it in both directions (D1-9), verified by negative control. | `ui/shell/ShortcutBinder`, `ui/dialogs/ShortcutsPage`, `core/settings/Settings` |
| `core/cmd/commands/PageCommands.{h,cpp}` | ○ (S3 rotate, S7 rest) | Rotate, insert, delete, move, duplicate, extract, replace, crop, resize, renumber — one `Command` subclass each. `RotatePageCommand` is the first-milestone mutation. | `core/doc/PageTree`, `MutationSink`, `ui/panes/ThumbnailsPane` |
| `core/cmd/commands/AnnotationCommands.{h,cpp}` | ○ (S6) | Add, edit, move, delete, reply, set-status, set-appearance, lock, import-merge — per annotation kind. | `core/doc/AnnotationStore`, `MutationSink` |
| `core/cmd/commands/FormCommands.{h,cpp}` | ○ (S9) | Create/delete/edit field, set value, set tab order, reset form, import data, flatten form. | `core/doc/FormModel`, `MutationSink` |
| `core/cmd/commands/BookmarkCommands.{h,cpp}` | ○ (S7) | Create, rename, delete, re-parent, reorder, set destination or action, bulk-generate. | `core/doc/Outline`, `MutationSink` |
| `core/cmd/commands/StructureCommands.{h,cpp}` | ○ (S7) | Layer and tag mutations: create/delete/merge/flatten layers, retype and move tags. *(`SetAltText` removed 2026-08-23.)* | `core/doc/LayerTree`, `core/doc/TagTree`, `MutationSink` |
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
| `core/text/LayoutAnalyzer.{h,cpp}` | ○ (S5) | Groups runs into lines, lines into blocks, blocks into a logical order; detects columns and table grids. Pure → `tst_layout`. **The quality of Write mode and Reflow view both rest on this one file** — which is why it outlived the 2026-08-23 accessibility removal; Read Aloud was only ever a third consumer. | `core/text/PageTextModel`, `core/doc/TagTree`, `core/reflow/FixedToReflowConverter` |
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
| `core/reflow/ReflowToFixedComposer.{h,cpp}` | ○ (S13) | The way back: lays a `ReflowDocument` out onto pages and emits the page-content **intent** through `MutationSink`. **Also emits a structure tree derived from the reflow model (decision O2)** — headings, paragraphs, lists, tables and reading order — so the semantic model persists in *standard* PDF rather than in a private stream, and a later re-conversion reads tags instead of re-inferring from glyphs. Also the *Prevent Further Edits* finalization path. | `core/reflow/LineBreaker`, `core/doc/TagTree` (**written**, not just read), `MutationSink` (realized by `pdf/cos/ContentStreamBuilder` on the far side) |
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
| `core/settings/Settings.{h,cpp}` | ✅ | The **one** typed settings store. **`isSet()` arrived at T11**: whether the FILE carries a key, which is a different question from whether its value equals the default — the Quick Access Toolbar ships three commands and the user may remove all three, and only one of those two empty lists means "give them the factory toolbar". Groups mirror the Preferences pages of [`Features.md` §9.1](./Features.md#91-preferences-groups). Typed get/set with defaults, a `changed` subscription, reset. **Loads once into memory and writes only on an explicit `sync()`**, because D1-16's downgrade rule — a file a NEWER build wrote is read for what we understand and left byte-identical — is not expressible through a live `QSettings`, which syncs on destruction. An out-of-range value is clamped and logged; an unreadable one falls back to its default; a `shortcuts/` key naming an action that no longer resolves is dropped, counted and removed. Pure → `tst_settings`. | Read by nearly everything; written by `ui/dialogs/PreferencesDialog` |
| `core/settings/SettingsKeys.def`, `SettingsSchema.{h,cpp}` | ✅ | **One declarative table, three artifacts**, the same shape as `ActionId.def` (D1-6): the constexpr `Key<T>` objects, the runtime descriptor table a Preferences page iterates, and the group census all come from the `.def`. Type, default, range, group, and whether policy may lock it. **There is no string overload anywhere**, so an undeclared key is a name that does not exist — three negative-compile fixtures prove a misspelled key, a string key and a wrong-typed value each fail to compile (D1-10). The `shortcuts/<context>/<action>` family is generated over `KeyContext` × `ActionId` rather than opened as a string API. Enums persist **by name**, so a reordered enum cannot change what an existing file means. Carries `kCurrentSchemaVersion` and the ordered migration chain (D1-16). | `core/settings/Settings`, `core/settings/PolicyOverlay` |
| `core/settings/PolicyOverlay.{h,cpp}` | ○ (S15) | Machine-wide administrative defaults and locks layered over user settings, with "who locked this" reporting. | `core/settings/Settings`, `ui/dialogs/PreferencesDialog` |
| `core/settings/ProfileStore.{h,cpp}` | ○ (S6) | Generic named-profile persistence reused by stamps, watermarks, headers/footers, security schemes, signature appearances, conversion profiles, and batch sequences. | `core/security/SecuritySchemeStore`, `core/batch/SequenceStore`, `ui/dialogs/*` |
| `core/settings/RecentFiles.{h,cpp}` | ✅ | Recent list with pinning and the clear action. A pinned entry is **not** subject to eviction and does not spend a `documents/recentCount` slot; `clear()` clears the history and keeps the pins, and `clearIncludingPins()` is the other one, called by name. Paths are canonicalized, so one document reached through `/`, `\` and `./` is one entry. **Keyed on the path, not a `DocumentId`** — that type arrives with `core/doc/` at Stage 2, and a recent list is a list of paths by nature; Stage 2 adds an overload, not a new key. Per-document view state is Stage 4's `SessionState`. | `core/doc/DocumentId` (S2 overload), `platform::IShellIntegration` (jump list) |
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
| `pdf/ops/ConformanceChecker.{h,cpp}` | ○ (S8) | Validates and reports PDF/A and PDF/X conformance per object, with page references. *(PDF/UA dropped 2026-08-23 with the accessibility removal.)* | `pdf/cos/*`, `ui/dialogs/ConformanceReportDialog` |
| `pdf/DocumentBackendImpl.{h,cpp}` | ○ (S2) | Implements `IDocumentBackend`: open, close, hydrate the model, hand out the rasterizer / extractor / sink / signer. | `pdf/render/*`, `pdf/ops/*`, `core/backend.h` |
| `pdf/BackendFactory.{h,cpp}` | ○ (S2) | `pdf::createBackend()` — the one exported symbol `app/` calls. Nothing else in the program names a `pdf/` type. | `app/CompositionRoot` |

### 3.12 `platform/` — the OS abstraction

| File | Status | Role | Interacts with |
|---|---|---|---|
| `platform/platform.h` | ✅ | **Interfaces only**: the **seventeen** of [`Tech-Stack-Plan.md` §4.3](./Tech-Stack-Plan.md#43-what-qt-gives-us-for-free-versus-what-needs-platform-code), plus the `Platform` bundle struct, the name census, and `createPlatform()`. No implementation and **no Win32 header — enforced, not asked for**: `tools/layer-lint/rules.json` declares this ONE FILE as its own layer with an OS-header deny list, with a fixture proving the rule fires and a control proving `platform/win/` may still include the OS freely. An unimplemented interface is a **null member**, never a stub that silently succeeds, and `unimplementedInterfaces()` names them so "not built yet" is printable rather than discovered by dereferencing. Every handle crosses as an opaque integer. | Implemented by `platform/win/*`; consumed by `core/`, `ui/`, `app/` |
| `platform/win/WinSingleInstanceBus.{h,cpp}` | ✅ | Named-object instance detection, file-list handoff, and raise-to-foreground (D1-11). A `Local\` mutex decides who is primary; a length-prefixed UTF-8 message over a named pipe carries the file list; the **secondary** calls `AllowSetForegroundWindow` with the primary's pid — read from the pipe rather than from the payload — before it exits, because Windows refuses foreground activation to a process that received no input. **The pipe name carries the session id explicitly**: `Local\` does not apply to pipes, so without it two users' correctly-separated mutexes would still meet on one machine-wide pipe. The handler is delivered by queued invocation onto the thread that called `listen()`. | `app/SingleInstance` |
| `platform/win/WinDisplayTopology.{h,cpp}` | ○ (S2) | Monitor enumeration with per-monitor DPI, and change notification on topology or scale changes. | `core/render/DpiPolicy`, `core/settings/SessionState` |
| `platform/win/WinAcceleratedSurface.{h,cpp}` | ○ (S2) | Accelerated presentation of rendered tiles, with a software fallback when no device is available. | `ui/viewer/TilePresenter` |
| `platform/win/WinFontResolver.{h,cpp}` | ○ (S2) | Installed-font enumeration and fallback selection for a missing embedded document font. | `pdf/render/FontResolverBridge`, `core/reflow/Shaper` |
| `platform/win/WinShellIntegration.{h,cpp}` | 🟡 (S1 assoc ✅, S15 rest) | **Real as of T1… T7:** per-user ProgID registration under `HKCU\Software\Classes`, the `.pdf` `OpenWithProgids` entry, association query, and `showDefaultAppsUi()`. That last one is **not a setter and cannot be**: since Windows 8 the association is protected by a hash the OS computes, so an app can register a ProgID but only the user can make it default — modelling it as `setDefaultHandler()` would be an API whose contract cannot be honoured. The registry root is injectable, so the write path is tested for real against a scratch key rather than against the association a person is using. **Still owed:** context-menu verbs, thumbnail provider, preview handler, index filter, jump list, taskbar progress — all Stage 15, each a separate COM server registration. | `core/settings/RecentFiles`, `ui/shell/MainWindow` |
| `platform/win/WinRichClipboard.{h,cpp}` | ○ (S5) | Bitmap, vector-metafile, and HTML clipboard flavours beyond Qt's text/image. | `core/text/TextSelection`, `ui/viewer/tools/SnapshotTool` |
| `platform/win/WinCertificateStore.{h,cpp}` | ○ (S10 recipient lookup, S11 full) | Enumerates digital IDs and root certificates from the OS store; builds chains; resolves certificate-encryption recipients. | `core/security/SignatureValidator`, `pdf/cos/PublicKeySecurityHandler`, `ui/dialogs/DigitalIdManager` |
| `platform/win/WinCryptoProvider.{h,cpp}` | ○ (S11) | Signs a hash through the OS crypto provider, including smart cards and HSMs; verifies; fetches OCSP and CRL. | `pdf/ops/SignatureOps`, `core/security/SignatureValidator` |
| `platform/win/WinSecretStore.{h,cpp}` | ○ (S11) | OS-key-protected storage for signing-ID passwords and service tokens. | `core/security/SignatureRequest`, `core/settings/Settings` |
| `platform/win/WinPrinting.{h,cpp}` | ○ (S8) | Printer enumeration and job submission with duplex, tray, and page-range control beyond `QPrinter`. | `ui/dialogs/PrintDialog` |
| `platform/win/WinPrinterDriverService.{h,cpp}` | ○ (S16) | Installs, services, and configures the **virtual PDF printer**, and receives its spooled jobs. | `core/batch/BatchRunner`, the installer |
| `platform/win/WinScanner.{h,cpp}` | ○ (S16) | Imaging-device enumeration and page acquisition with ADF and duplex control. | `ui/dialogs/ScanDialog` |
| ~~`platform/win/WinSpeech.{h,cpp}`~~ | ⤵ | **Withdrawn 2026-08-23** with Read Aloud and the rest of the accessibility surface — see [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23). Its interface `platform::ISpeech` goes with it, taking the platform count from sixteen to fifteen. | — |
| `platform/win/WinOfficeBridge.{h,cpp}` | ○ (S8 export, S12 import, S16 add-ins) | Drives the installed office suite for high-fidelity conversion in both directions; hosts the add-ins' entry points. Which direction uses it versus a first-party writer is open decision O1. | `pdf/ops/ExportOps`, `core/create/DocumentBuilder`, `core/batch/BatchRunner` |
| `platform/win/WinBrowserBridge.{h,cpp}` | ○ (S16) | Native-messaging host for the browser extensions. | `app/CommandLine` |
| `platform/win/WinRightsManagement.{h,cpp}` | ○ (S16) | Applies and reads enterprise information-protection labels; tracks and revokes access. | `core/security/SecurityScheme` |
| `platform/win/WinProcessSandbox.{h,cpp}` | ○ (S10) | Launches a restricted child process for protected-view rendering of untrusted files. | `core/security/TrustPolicy`, `app/CompositionRoot` |
| `platform/win/WinPowerSession.{h,cpp}` | ○ (S2) | Sleep/resume, battery-saver render throttling, and session lock/logoff notifications. | `core/render/RenderScheduler` |
| `platform/win/PlatformFactory.{h,cpp}` | ✅ | `platform::createPlatform()` — assembles the Windows bundle, and the only exported symbol from this layer. Two of seventeen slots are non-null at Stage 1; the count of what is *not* implemented is logged at assembly rather than left implicit. The executable path is passed IN rather than resolved with `GetModuleFileNameW`: `app/` already knows where it launched from, and a test must be able to assemble a bundle without registering anything. | `app/CompositionRoot` |

### 3.13 `ui/shell/` — the application frame

| File | Status | Role | Interacts with |
|---|---|---|---|
| `ui/shell/MainWindow.{h,cpp}` | 🟡 (S1 T12) | The frame: ribbon, QAT, document tab bar, sidebar dock, status bar, message bar. Owns the open-document set. Contains **no** document logic — it dispatches `ActionId`s. **Real as of T9:** the frame, the sidebar with its edge strips, the status bar, the theme, `openFiles()` under D1-2 (existence and `.pdf` suffix, **not one byte read** — case A-58), `raiseToForeground()` for flow B's handoff, and **four** registered actions at that point (`view.toggleSidebar`, `view.fullScreen`, `file.quit`, and `view.toggleRibbon` at T10) — what the window could genuinely service then, with every other id left declared and unregistered, which is the honest state at any count. Its **destructor disconnects the sidebar explicitly** (issue I-12): a lambda bound to a child's signal outlives the derived object, and `~QWidget` closing the window re-entered a `MainWindow` that no longer existed. **Real as of T10:** the ribbon, hosted through `setMenuWidget` so no docked pane can land above it, built from `res/ribbon.json` before the central surface and **after** `registerActions()` — the factory reads registration state as it builds each button, so the order is load-bearing rather than cosmetic. `showEvent` runs the D1-7(b) wiring self-check, asserting in debug and logging in release. **Real as of T11:** the menu-widget slot holds a COLUMN — the Quick Access Toolbar above or below the ribbon, both inside the one slot `QMainWindow` puts above the dock areas, so no docked pane can land between them — plus the tool finder in the tab strip's corner and the customization that is saved on every change rather than at shutdown. **Real as of T12:** the document tab strip over a two-page body (the placeholder, and the documents), `openFiles()` opening a stub tab per accepted path (D1-2), the window title and status bar following the current document, and **seventeen registered commands**. **And the rule T12 forced:** there can now be more than one of these — tearing a tab out makes a second one — while `ActionRegistry` is a single shared object whose registration is deliberately not idempotent. So **registration happens once per registry and no handler captures `this`**: each resolves `MainWindow::activeShell()` at dispatch time, which is what makes a Global command mean *the window I am looking at* and what stops a closed window's handler from being dispatched into. **Real as of T13:** the window owns a `ShortcutBinder`, created before the action factory because the factory asks it which context a dispatch happened in and released FIRST in the destructor because it filters the application's events — so `Ctrl+O`, `F4`, `F9`, `F11`, `Ctrl+W`, `Ctrl+Tab` and the rest of §8's Global table now reach the same commands the ribbon reaches. **Still owed:** the message bar (S2), and everything a real document brings at Stage 2. | `ui/shell/*`, `ui/shell/DocumentWindow`, `core/cmd/ActionRegistry`, `app/Services` |
| `ui/shell/Ribbon.{h,cpp}` | 🟡 (S1 T10, T11) | The ribbon widget: tabs, groups, sizing behaviour, collapse, contextual-tab show/hide, key tips. **Risk A3 was proven by the T2 spike and is productionized here**, and the file carries all three pieces of the named custom work the verdict rests on: `RibbonPageLayout`, a custom `QLayout`, because collapse changes what a child *is* rather than how much space it gets; a chevron overflow over `QTabBar`, whose own overflow is scroll arrows; and `restoreTabOrder()`, which re-chains `Tab` over the groups in **visual** order whenever the collapsed set changes (issue **I-05** — collapse re-parents, and re-parenting appends to the end of the focus chain). Hysteresis on the un-collapse direction only, so a slow drag across the threshold cannot oscillate. **Two width facts, both found by asking a real ribbon to be narrow (I-19):** the page layout's minimum is **one** collapsed group rather than the sum of them, and the tab bar's size policy is `Ignored` — together they were holding the whole main window open at 1044 px and making the chevron unreachable. **I-19 was closed at T11**: the body now sits in a `QScrollArea`, so `RibbonPageLayout::minimumSize()` can go back to the honest content minimum — the SUM of the collapsed groups — without that number reaching the main window. Collapse is still exhausted first and the row scrolls only below the width every group collapsed needs; the scrollbar's height is reserved at every width, because a ribbon whose height depends on its width is R-4's flicker wearing a different hat. **Still owed:** contextual tabs beyond the one that proves the mechanism (per feature, Stages 5–13). | `ui/shell/RibbonTabBuilder`, `ui/shell/ActionFactory`, `core/cmd/ActionRegistry` |
| `ui/shell/RibbonTabBuilder.{h,cpp}` | ✅ (S1 T10) | Builds the ribbon from a declarative layout resource plus the `ActionRegistry`, so adding a command never means writing widget code. **It makes no decision the resource could have expressed** — not the tab, not the group, not the button size, not the collapse order — which is the only thing that keeps D1-8's promise true: a command added in Stage 9 is a registry row plus a JSON line. Asserted from both directions by `tst_ribbonbuild`: everything in the resource is in the widget tree, **and no control exists that the resource did not declare**. | `res/ribbon.json`, `ui/shell/RibbonModel`, `core/cmd/ActionRegistry` |
| `ui/shell/RibbonModel.{h,cpp}` | ✅ (S1 T10) | The ribbon resource's schema and its **one** parser: `QByteArray` → tabs/groups/controls plus diagnostics, naming no widget at all. That last part is what makes it shareable — `tools/ribbon-validate` **compiles this translation unit in** rather than reimplementing the schema, so the build-time check (D1-8) and the runtime build cannot disagree about what the file means. A validator with its own parser can certify a file the runtime reads differently, which is worse than no validator. Same technique `tests/unit` uses for `app/CommandLine.cpp`. Every problem is collected rather than the first returned, and each carries a **line number** resolved from the raw bytes. | `core/cmd/ActionId.h`, `res/ribbon.json` |
| `ui/shell/QuickAccessToolbar.{h,cpp}` | ✅ (S1 T11) | Add/remove/reorder commands, above-or-below placement, reset — all five of `Features.md` §1.1's operations. **It holds `ActionId`s and rebuilds its buttons from that list**: the list is what gets serialized, restored and compared against the factory default, so making the LAYOUT the state would put the source of truth in the wrong place. Every operation is a command (`ui.qatAdd`, `ui.qatRemove`, `ui.qatPlacement`, `ui.resetCustomization`) rather than a context-menu lambda, which is what keeps them reachable from the Stage 14 batch runner. **`ui.qatAdd` and `ui.qatRemove` are the product's first PARAMETERIZED commands** and `ActionRegistry::dispatch` carries no argument — the surface that raised the menu records the target in `pendingTarget()` and the handler reads it. That is the Stage 1 answer and not a general one; open decision **O11**. | `ui/shell/ActionFactory`, `ui/shell/UiCustomization` |
| `ui/shell/UiCustomization.{h,cpp}` | 🟡 (S1 T11) | Serializes the QAT and shortcut customizations for export and team deployment. **Widget-free**, so the Stage 14 batch runner can read and write a customization without a window. **D1-16 in both directions:** the blob carries its own `version` because a file that leaves this machine arrives at a build with a different idea of the format, an older one is migrated and re-stamped, and a **newer** one is read for what we understand with its unknown members carried through verbatim (`extras`) — the downgrade half, which is the one that loses a user's layout during a rollback. Commands are stored as **action string ids**, resolved through the alias table, never as enum values. **Still owed:** the ribbon half (custom groups are Stage 14's customize dialog), and the file picker — this class converts to and from BYTES because `layer-lint` allows a filesystem write in `pdf/` and `core/settings/` only, and a `ui/` class that writes files is one that will eventually write a document. | `core/settings/Settings`, `core/cmd/KeyBindingStore` |
| `ui/shell/ShortcutBinder.{h,cpp}` | ✅ (S1 T13) | Watches focus to compute the active `KeyContext`, resolves a chord through `KeyBindingStore`, and dispatches. **The file that makes one chord mean two things** — and the file that keeps that possible, because Qt's own shortcut system cannot express §8's *innermost context, then `Common`, then `Global`* and would have made a user's remap a shadow copy inside 358 actions. Three separable parts: `activeContext()`, a pure walk up `parentWidget()` from the focus to the innermost widget that DECLARES a context (D1-5, `tst_context` over synthetic chains); `chordFor()`, the QtGui boundary `core/cmd/Chord`'s header promised — one table read both ways, so `unreachableBindings()` can answer *which §8 row names a key no keyboard can produce* from the same rows that produce them; and `activate()`, which resolves and dispatches. **Five outcomes, and only two consume the key**: a dispatch and an `Unavailable` refusal consume, while a `NotFound` (most of §8 at Stage 1, where seventeen of 358 ids are registered), a `WidgetNative` row and an unbound chord are left for whoever else wants them. **The focused widget gets first refusal through Qt's own `ShortcutOverride` handshake**, so a `QLineEdit` keeps `Ctrl+A` without this file maintaining a list of widgets that edit text. **One binder per window, filtering `qApp`** and answering only for its own window, because a second `MainWindow` would otherwise dispatch the same chord twice. **Nothing in Stage 1 declares a context**: the Viewer surface is Stage 2, the panes Stage 4, Write mode Stage 13 — so `activeContext()` answers `Global` throughout the shell today, which is the honest answer and is exactly what makes every §8 Global default work. `shortcutComplaints()` is T13's half of D1-7: an action carrying its own key sequence, or a free-standing `QShortcut`, is a chord that bypassed the context stack and the user's remap on its way in. | `core/cmd/KeyBindingStore`, `core/cmd/KeyContext`, `core/cmd/ActionRegistry` |
| `ui/shell/DocumentTabBar.{h,cpp}` | ✅ (S1 T12) | Tabs, reorder, tear-off to a new window, re-dock, middle-click close, overflow. **It reports gestures and moves nothing** — a tear-off has to choose between a new window and another window's strip, and that choice is not available to a widget that knows only its own tabs, so `MainWindow` owns it. **Unlike the ribbon it uses `QTabBar`'s own overflow**: a ribbon has a small fixed tab set and shows a chevron, a document strip has an unbounded user-driven one and every product with one shrinks and then scrolls. The gestures themselves are manual case **M-03**; everything they end in — reorder, release, adopt, detach, close — is asserted in `tests/ui/tst_documenttabs.cpp`. | `ui/shell/MainWindow`, `ui/shell/DocumentWindow` |
| `ui/shell/DocumentWindow.{h,cpp}` | 🟡 (S1 T12) | Per-document container: owns the `Document`, its `CommandJournal`, the viewer, the split container, and the pane set. The unit that a tab represents. **A STUB at Stage 1 by decision D1-2, and its emptiness is the feature:** it holds a path and the widgets that name it — no document, no journal, no page number, no zoom, no selection, no pane set, and no member Stage 2 would have to *remove* rather than fill in. Guide case **A-57** is the guard against it calcifying. It never opens the file: `MainWindow::openFiles` checks that a path exists and ends in `.pdf` and reads no bytes (A-58), because deciding whether bytes are a PDF is `IDocumentBackend`'s job. | `core/doc/Document`, `core/cmd/CommandJournal`, `ui/viewer/PageView`, `ui/panes/*` |
| `ui/shell/SidebarDock.{h,cpp}` | 🟡 (S1 T9) | Pane hosting: dock left/right, float, auto-hide tab strip, width memory, per-document open-pane state. **Real as of T9:** dock-left/right, float and re-dock come from `QDockWidget` and are not reimplemented; **auto-hide is ours**, because Qt has no notion of it — the pane collapses and the window shows an edge strip, and `peek()`/`unpeek()` reveal it *without* pinning it. Width memory is captured on **resize**, not on hide, because a collapsing splitter reports width 0 on the way out; a zero is never recorded. Pane chrome (pin, float, dock side) is deliberately **not** given `ActionId`s — the same call T4 made for its fifteen `WidgetNative` bindings — while the close button dispatches `view.toggleSidebar` through the registry. **Still owed:** real panes (S4+) and per-document open-pane state, which is `SessionState` at Stage 4; width memory is per session until then. | `ui/panes/*`, `core/settings/SessionState` |
| `ui/shell/StatusBar.{h,cpp}` | 🟡 (S1 T9) | Page-number box, page controls, zoom controls, view-mode buttons, cursor coordinates, background-task progress. **Real as of T9:** the message area, the page box, the page total, the zoom label and the task indicator — **all built and disabled**, because there is no document to be on page 3 of. That is the point rather than a shortcut: "1 of 1 at 100 %" with nothing open is a lie that becomes very hard to see once there IS a document to compare against. The task indicator is hidden when idle. **Real as of T12:** there IS a document now, so `setDocumentPresent(true)` runs for the first time and the page box, page total and zoom label come alive with the current tab — and the page total still reads the em dash, because a stub tab has no pages and "1 of 1" would be inventing one. **Still owed:** everything that needs a `ViewportController` and a `RenderScheduler`, at Stage 2. | `ui/viewer/ViewportController`, `core/render/RenderScheduler` |
| `ui/shell/Catalog.{h,cpp}` + `ShellKeys.def` | ✅ (S1 T9) | Resolving a catalog key to text — the one place `ui/` turns `shell.window.title` into words. **The context is the key's own namespace**, the part before the first dot, and NOT the enclosing class name `QObject::tr()` would use: the key is the source string in this project (T6), and a label built by `RibbonTabBuilder` has to resolve identically to the same label shown by `ToolFinder`. `ShellKeys.def` is the shell's key table — one file, two artifacts, the same shape as `ActionId.def` — so the constants the widgets use and the list `--dump-ui-strings` prints cannot drift, which is the only thing that makes guide case A-72 worth running. | `i18n/*.ts`, `core/cmd/ActionId.h` |
| `ui/shell/MessageBar.{h,cpp}` | ○ (S2) | The document-condition banner: form detected, signed, protected, repaired, in Write mode — each with its action button. | `core/doc/RepairReport`, `core/doc/Permissions`, `core/doc/FormModel` |
| `ui/shell/ToolFinder.{h,cpp}` | ✅ (S1 T11) | Fuzzy command search over the `ActionRegistry`, showing each match's chord and running it. **The ranking is a free function naming no widget** — `toolfinder::rank()` — which is what lets guide case **A-56** assert the ORDER (exact, then prefix, then word-start, then subsequence) without a window, the same split `RibbonModel` made at T10. **A declared-but-unregistered command ranks, appears, and is marked** rather than being hidden: the ribbon shows those commands greyed and a search that cannot find a button the user is looking at is a search they stop trusting. Each hit carries the chord that would actually fire, read from `KeyBindingStore` — defaults plus the user's remaps — and never from a list here. | `core/cmd/ActionRegistry`, `core/cmd/KeyBindingStore` |
| `ui/shell/ActionFactory.{h,cpp}` + `RawPageAction.h` | ✅ (S1 T10) | **The only place in RawPage that constructs a `QAction`** — decision D1-7. `layer-lint`'s non-escapable `bare-qaction` rule allows the construction here and in `RawPageAction.h` (the subclass definition) and nowhere else, across four spellings with two controls (case A-12). The factory resolves the label from `ActionId.def` through the catalog, builds a declared-but-unregistered id **disabled with a tooltip saying so** rather than omitting it, and makes exactly one `triggered` connection — into `ActionRegistry::dispatch`. `wiringComplaints()` is the runtime half (A-54): it names a bare `QAction` and a second `connect` **separately**, because the lint catches the first at the moment it is written and structurally cannot see the second. Two things the first real run taught it (**I-18**, **I-20**): Qt manufactures intrinsic actions of its own — `QMenu::menuAction()`, `QDockWidget::toggleViewAction()` — and `QToolButton::setDefaultAction` adds a receiver, so the invariant is *every receiver accounted for*, not *exactly one*. **T11 taught it three more things.** A `QLineEdit`'s clear button is a QAction that is NOT in `QWidget::actions()` and so cannot be recognised by identity at all — `adoptWidgetActions()` lets the widget's owner stamp it at the instant the property is turned on (**I-22**). The factory had never called `setCheckable`, so its own promise that a toggle is checkable had gone untested for want of a toggle (**I-21**); it now asks `ActionRegistry::isToggle()`, and reads the registration's `tooltipKey` as well. And `addToMenu()`/`addMenuSeparator()` exist because `addAction(` is in the lint's pattern list with no exception for the overload that takes a built action: a caller asks here, one line instead of two, and the rule stays absolute with a single allowed file. | `core/cmd/ActionRegistry`, `ui/shell/Catalog` |
| `ui/shell/ThemeManager.{h,cpp}` | ✅ (S1 T9) | Light, dark, follow-system; palette tokens; the UI font scale. *(OS high-contrast deference removed 2026-08-23 with decision D1-15.)* **Eleven closed colour ROLES**, resolved by one switch over the whole enum with no `default` — so under `ui/`'s `-Werror` a token added without a value in both themes does not compile, which is `ActionId.def`'s trick applied to colour. `FollowSystem` is a **request**, not a state: it resolves to Light or Dark through `QStyleHints::colorScheme()` and re-resolves live when the OS flips, which is what stops "the setting does nothing" being the first bug filed. The font scale is applied to a base point size captured once, so repeated changes do not compound. | `core/settings/Settings` |
| ~~`ui/shell/ReadAloudBar.{h,cpp}`~~ | ⤵ | **Withdrawn 2026-08-23** with Read Aloud — see [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23). | — |
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
| `ui/panes/TagsPane.{h,cpp}` | ○ (S7) | Tag tree editing and the logical-order overlay. *(Alt text and the accessibility-check report removed 2026-08-23.)* | `core/doc/TagTree` |
| `ui/panes/AttachmentsPane.{h,cpp}` | ○ (S6) | Embedded files: list, open, extract, add, delete, with the trust warning. | `core/doc/Attachments`, `core/security/TrustPolicy` |
| `ui/panes/SignaturesPane.{h,cpp}` | ○ (S11) | Signature list with status, details, certificate viewer, view-signed-version, and compare-versions. | `core/security/ValidationReport` |
| `ui/panes/SearchPane.{h,cpp}` | ○ (S5) | Advanced search: scope (document / folder / index / portfolio), all the toggles, the result list, sort, filter, and refine. `KeyContext::Search`. | `core/text/SearchEngine`, `core/text/SearchQuery` |
| `ui/panes/DestinationsPane.{h,cpp}` | ○ (S7) | Named destinations: list, sort, create, rename, delete, navigate. | `core/doc/Destinations` |
| `ui/panes/DocumentMapPane.{h,cpp}` | ○ (S13) | Heading-outline navigation for Write mode. | `core/reflow/ReflowDocument` |
| `ui/panes/StampPalettePane.{h,cpp}` | ○ (S6) | Stamp browsing by category, favourites, and custom-stamp creation. | `core/settings/ProfileStore`, `ui/viewer/tools/StampTool` |
| `ui/panes/DifferencesPane.{h,cpp}` | ○ (S5) | The comparison difference list, with navigation and the report export. | `core/text/DocumentComparer` |
| `ui/panes/LogPane.{h,cpp}` | ○ (S1) | Filterable in-app log viewer and the "collect diagnostics" action. | `core/Log.h` |
| `ui/dialogs/PreferencesDialog.{h,cpp}` | ○ (S1 shell, grows per stage) | The preference pages of [`Features.md` §9.1](./Features.md#91-preferences-groups), driven by `SettingsSchema` so a new key needs no new widget code; shows policy locks; includes the Shortcuts page. | `core/settings/Settings`, `core/settings/SettingsSchema`, `core/cmd/KeyBindingStore` |
| `ui/dialogs/*.{h,cpp}` | ○ (S2–S16) | The dialog family, one file each. *S2–S3:* `RepairReportDialog`, `RecoveryDialog`, `DocumentPropertiesDialog`. *S4:* `GoToPageDialog`. *S5:* `CompareDialog`. *S6:* `CommentSummaryDialog`, `MeasurementScaleDialog`, `CustomStampDialog`. *S7:* `PageOpsDialogs` (rotate/insert/delete/extract/replace/split/crop/renumber/resize), `MergeDialog`, `WatermarkDialog`, `HeaderFooterDialog`, `BatesDialog`, `ImpositionDialog`, `LinkPropertiesDialog`. *S8:* `ExportDialog`, `OptimizerDialog`, `ConformanceReportDialog`, `FlattenDialog`, `PrintDialog`. *S9:* `FieldPropertiesDialog`, `TabOrderDialog`, `CalcOrderDialog`, `FormDataDialog`, `DuplicateFieldDialog`. *S10:* `SecurityDialog`, `SecuritySchemeDialog`, `SanitizeDialog`, `RedactionSetupDialog`, `TrustManagerPage`. *S11:* `SignDialog`, `SignatureAppearanceEditor`, `DigitalIdManager`, `TrustedIdentitiesDialog`, `ValidationPreferencesDialog`, `CertificateViewer`, `EnvelopeDialog`. *S12:* `NewDocumentDialog`, `CreateFromFilesDialog`, `ConversionProfileDialog`. *S13:* `ParagraphDialog`, `TabStopsDialog`, `PageSetupDialog`, `ConversionFidelityDialog`, `TableDialog`, `TocDialog`, `CaptionDialog`, `FootnoteDialog`, `ChartDialog`. *S14:* `BatchEditorDialog`, `BatchReportDialog`, `DiagnosticsDialog`. *S15:* `ActivationDialog`, `UpdateDialog`, `AboutDialog`, `ThirdPartyNoticesDialog`. *S16:* `ScanDialog`, `OcrDialog`, `PortfolioDialog`. Each collects parameters and constructs a `Command` or a service call — **none contains document logic**. | `core/cmd/commands/*`, `core/security/*`, `core/batch/*`, `core/create/*` |
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
| `tools/i18n-lint/` | ✅ | The third gate. Four rules: `unwrapped-literal` (a literal reaching one of the **user-facing sinks** named in `rules.json` and not inside `tr()`; the one escapable rule), `missing-key` (a key absent from a locale marked `"shipped": true`), `action-label` (an `ActionId` with no `action.*` entry, read from `ActionId.def` **as text**), and `orphan-key` (**S3 — reported, never blocking**). Sink-driven rather than literal-driven on purpose: a rule that fires on object names, settings paths and format specifiers is a rule that gets waived everywhere and then switched off. Links nothing but the standard library (D0-1) — it reads Qt Linguist `.ts` files **without Qt**. It scans `.def` tables (T9, issue I-16) and, from T10, `res/ribbon.json`: a gate reads the files that **reference** a key, and whether one happens to be compiled is a fact about the build rather than about what the gate is for. | `i18n/*.ts`, `i18n/locales.json`, `core/cmd/ActionId.def`, `res/ribbon.json`, CI |
| `tools/license-audit/` | ✅ (S0 · T8) | Walks the resolved vcpkg manifest plus the Qt module list and the GCC runtime, emits `THIRD-PARTY.md`, and fails on any SPDX **expression** outside the allowlist. Matching is whole-expression, never a `GPL` substring: `GPL-3.0-or-later WITH GCC-exception-3.1` must pass while bare `GPL-3.0-or-later` must fail. Also asserts no non-free GCC plugin was loaded, which is the condition the exception rests on. | `vcpkg.json`, `cmake/toolchain-mingw.cmake`, `ui/dialogs/ThirdPartyNoticesDialog`, CI |
| `tools/golden-update/` | ○ (S2) | Regenerates `golden/` reference rasters, requiring an explicit reason so a rendering regression cannot be laundered into a new baseline. | `golden/`, `tests/golden/` |
| `tests/unit/` | ✅ (S0 · T11) | Catch2 suites over `core/` and `pdf/`, linked **without** `rawpage_ui` and `rawpage_platform` — the standing proof that the boundary holds. | `core/*`, `pdf/*` |
| `tests/platform/` | ✅ (S1 · T7) | The OS layer's own suite: the interface census, the `Local\` mutex name (case A-62), the session id in the pipe name, and a **real** claim → forward → deliver round trip. Single-process, because `CreateMutexW` returns `ERROR_ALREADY_EXISTS` for a name the same process holds, so the second bus object takes the genuine secondary branch. A **separate target** for the reason D1-4 gives about `ui/`: `tests/unit`'s headless assertion forbids linking `rawpage_platform`, and the first platform test is exactly the pressure that would otherwise weaken it. Windows only; never on `portable-asan`. | `platform/*` |
| `tests/ui/` | ✅ (S1 · T9, extended T10 … T12) | The widget suite (D1-4). **At T9:** theme tokens over both modes, the sidebar's auto-hide and width memory, the status bar's no-document state, `openFiles()` under D1-2, and the string dump's coverage of every shell key and every action label. **T10** added the ribbon build and the action-wiring self-check against planted violations; **T11** the Quick Access Toolbar's operations, its round trip through `Settings`, the customization blob's version handling, and the tool finder's ranking; **T12** the document tabs — reorder, close, step, release and adopt, tear-off, and the window set that makes a Global command act on the window that is active. Seven suites beside its own `main.cpp`, split by subject rather than by task. Links `rawpage_core` + `rawpage_ui` and nothing else; runs under `QT_QPA_PLATFORM=offscreen`, set both by CTest and by its own `main()` so that a developer running the binary by hand gets CI's conditions rather than real windows on their desktop. It installs `rawpage_en.qm`, so the widgets are exercised against real text rather than against their own catalog keys. **None of it can judge appearance** — that is the guide's manual half, and a case here claiming to have checked a *look* is a defect in the case. Windows only; never on `portable-asan`. **Arrived at T9 rather than the plan's T15**, on the precedent `tests/platform` set at T7. **Extended at T10** with `tst_ribbonbuild` and `tst_actionwiring`, which between them cover A-50, A-51's runtime half and A-54 — and which found three defects on their first run against the real shown window (**I-18**, **I-19**, **I-20**). The ribbon cases `show()` the widget and activate both layout levels explicitly: offscreen there is no event loop to do the second half, and a bare `resize()` yields stable, plausible, meaningless numbers. | `ui/*`, `core/*` |
| `tests/golden/` | ○ (S2) | Renders corpus pages at fixed resolutions and compares against `golden/` with a perceptual threshold; writes a diff image on failure. | `pdf/render/PdfiumRasterizer`, `golden/` |
| `tests/roundtrip/` | ○ (S3) | Open → command → incremental save → reopen: asserts the mutation landed, untouched objects are byte-identical, and pre-existing signatures still validate. | `pdf/cos/IncrementalWriter`, `corpus/` |
| `tests/fuzz/` | ○ (S3) | libFuzzer targets over `pdf/cos/Lexer` and `pdf/cos/Parser`, seeded from `corpus/malformed/`. Built by the `portable-fuzz` preset, **not** by the Windows GCC build, which has no libFuzzer; findings are reproduced in `x64-debug` before they count as fixed. | `pdf/cos/*` |
| `cmake/toolchain-mingw.cmake` | ✅ (S0 · T3) | The pinned toolchain: `C:\Qt\Tools\mingw1310_64` — GCC 13.1.0, `x86_64-w64-mingw32`, SEH, posix, msvcrt — as a cache-variable default, plus the shared warning/charset/subsystem flags. **Fails configure** on any other compiler, because that is Qt's own bundled GCC and a different libstdc++ or CRT against this Qt surfaces as an unexplained crash in Stage 6 rather than an error in Stage 0. | `CMakePresets.json`, `tools/license-audit`, CI |
| `cmake/ToolchainPreflight.cmake` | ✅ (S0 · M-03 fix) | The pin's roots and the three checks that must run **before `project()`**: `RAWPAGE_MINGW_ROOT` exists, `RAWPAGE_QT_PREFIX` exists, the vcpkg toolchain file is reachable through `VCPKG_ROOT`, and the pinned compiler can actually **compile** (`-fsyntax-only`, not `-dumpversion` — the driver answers that one itself while the toolchain is unusable). Before it, a `FATAL_ERROR` from the toolchain file landed mid-detection and CMake printed two red herrings after the real message (I-12), and a compiler that could not start surfaced as *"not able to compile a simple test program"* with no compiler output (I-10). `toolchain-mingw.cmake` includes it too and calls the two cheap checks, so the wording has one home and still fires without a preflight. | `cmake/toolchain-mingw.cmake`, `CMakeLists.txt`, `tests/gates/fixtures/wrong-compiler/` |
| `tools/ribbon-validate/` | ✅ (S1 · T10) | **Not a gate** — `checks` runs three and says three — but the build-time half of D1-8. It validates `res/ribbon.json` against `core/cmd/ActionId.def` and **fails the build**, naming the id and the line, on an unknown action, a duplicate placement, an empty group, a wrong schema version, a duplicate id or a title that is not a catalog key. Wired as a dependency of `rawpage_ui`, so the layer that consumes the resource is the layer whose build stops. It **compiles `ui/shell/RibbonModel.cpp` in** rather than reimplementing the schema — one parser, so a build-time verdict and a runtime one cannot disagree. Links QtCore only, and returns early for the gate configurations, so `tools/` still builds with no Qt at all. | `res/ribbon.json`, `ui/shell/RibbonModel`, `core/cmd/ActionId.def` |
| `tools/common/LineScan.h` | ✅ (S1 · T6) | Splitting a source line into code and trailing comment, and deciding whether a waiver marker is **applied** or merely **quoted**. Shared rather than copied because both of Stage 0's gate findings live in it — I-15 (a rule tested against one spelling catches one spelling) and I-20 (the tool counted its own documentation as three live waivers) — and a second gate re-deriving that logic would re-derive the bugs. Header-only, standard library only. | `tools/i18n-lint/`, `tools/layer-lint/` |
| `tools/common/Json.h` | ✅ (S0 · T7) | Minimal JSON reader shared by the gates. Exists because the gates link nothing but the standard library (D0-1), which rules out Qt's JSON and any vcpkg dependency; `layer-lint` reads `rules.json` with it and `license-audit` reads `vcpkg.json` and vcpkg's SPDX SBOM. A reader, not a library — no writing, no schema validation. | `tools/layer-lint/`, `tools/license-audit/` |
| `tests/gates/` | ✅ (S0 · T9 · extended S1 · T6, T8, T9) | The gates tested against deliberately bad input: **59 CTest cases** on the bare `gates` preset, of which 14 are `i18n-lint`'s and 4 arrived with T8 and T9 — the `argv-path-read` rule with its entry-point control, and `ui/`'s two permitted interface headers with a control for each direction. `fixtures/layer-lint/README.md` records which spellings of a violation are covered and which are knowingly not. `RunGateCase.cmake` asserts **both** exit code and diagnostic, because CTest's `WILL_FAIL` cannot tell a correct finding (exit 1) from a crashed gate (exit 2), and `PASS_REGULAR_EXPRESSION` ignores the exit code entirely. Includes the negative controls without which every positive case is satisfied by a gate that rejects everything. | `tools/*`, `cmake/ToolchainAssert.cmake`, CI |
| `tools/spike-pdfium/` | ✅ (S0 · T12) ⤵ | **Throwaway.** Answered risk A6 — a PDFium DLL links from `g++` and opens a document — and is deleted when `pdf/render/PdfiumInit` replaces it at Stage 2. The durable outputs are the recorded verdict and `scripts/fetch-pdfium.ps1`. | `third_party/pdfium/` |
| `tests/bench/` | ○ (S2) | Per-tile render latency at 96/144/192 DPI over the corpus — the measurement that settles risk A2. | `core/render/*`, `pdf/render/*` |

---

### 3.18 Build system, scripts, and CI

The map owed these rows: Stage 0 built a working build system, and a file map that describes every
`.cpp` but none of the machinery that compiles them is the first place this document starts drifting
from the tree.

| File | Status | Role | Interacts with |
|---|---|---|---|
| `CMakeLists.txt` | ✅ (S0 · T2) | Root build definition. Declares the four layer libraries plus `rawpage`, so the **linker itself** catches a class of layering violation before `layer-lint` runs. Also holds the three build-shape switches: `RAWPAGE_PORTABLE_SUBSET` (core/ + pdf/cos/ only, for the Linux sanitizer job), `RAWPAGE_GATES_ONLY` (tools/ + tests/gates/ with **no Qt and no vcpkg** — what makes the CI gates job possible), and `RAWPAGE_TARGET_ARCH` (arm64 fails here with an explanation rather than three assertions deep). | every layer, `cmake/*`, CI |
| `CMakePresets.json` | ✅ (S0 · T4) | Six configure presets. vcpkg is the outer toolchain with the pin **chainloaded** via `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` — not the reverse. `hostSystemName` conditions hide the Linux presets on Windows and vice versa. | `cmake/toolchain-mingw.cmake`, CI |
| `cmake/ToolchainAssert.cmake` | ✅ (S0 · T3) | TA-1…TA-9: compiler vendor, exact version, triple, SEH, posix, **msvcrt-not-UCRT**, compiler-inside-the-pinned-root, Qt version/prefix, and no non-free GCC plugin. Separate from the toolchain file because CMake re-reads that file for every `try_compile()`. Each failure names the offending value **and the fix**. | `CMakeLists.txt`, `tests/gates/fixtures/wrong-compiler/` |
| `cmake/RawPageWarnings.cmake` | ✅ (S0 · T2) | The one place flags are decided. `-Werror` for `core/` and `pdf/`; a shrinking grace list for `ui/`; `-g` in **every** configuration because DWARF in the binary is the only symbolization story this toolchain has. Records why `-fanalyzer` is absent — it false-positives on `std::string` with this GCC. | every target |
| `cmake/RawPageQt.cmake` | ✅ (S0 · T2) | `rawpage_link_qt()` — no target links Qt directly. Records the real link closure into `qt-modules.txt` for `license-audit`, and refuses `Qt6::Charts`/`DataVisualization`/`VirtualKeyboard` at configure time with a named GPL-3.0 diagnostic. | `license-audit`, every Qt-using target |
| `cmake/RawPageChecks.cmake` | ✅ (S0 · T9 · extended S1 · T6) | The `checks` target, now running **three** gates. Depends **only** on the gate executables, never the layer libraries — that is what lets CI run the gates before compiling anything. `i18n-lint`'s three inputs are passed explicitly, so an absent one is exit 2 rather than a silently skipped rule. | `tools/*`, CI |
| `vcpkg.json` | ✅ (S0 · T6) | Every §6.2 dependency, grouped into features so resolution can be **tested now and required later** (D0-3). Only `tests` is built today. | `vcpkg-configuration.json`, `license-audit` |
| `vcpkg-configuration.json` | ✅ (S0 · T5) | Pins the vcpkg baseline commit, so a `git pull` in the vcpkg clone changes nothing and a dependency update is an attributable diff. Registers `tools/ports/` as the overlay directory. | `vcpkg.json` |
| `tools/ports/` | ✅ (S0 · T5) | Empty, deliberately. Where a port goes when vcpkg lacks a working `x64-mingw-dynamic` build — **never** a reason to change the triplet. A5 found no such port. | `vcpkg-configuration.json` |
| `scripts/env.ps1` | ✅ (S0 · T1) | Dot-sourced; puts the pinned toolchain on `PATH` and sets `VCPKG_ROOT` itself, so the build never depends on machine state. Detects being *run* rather than dot-sourced and says so. | every documented command, CI |
| `scripts/bootstrap.ps1` | ✅ (S0 · T1) | Verifies prerequisites and reports what to fix. **Installs and downloads nothing** — a bootstrap that silently mutates a build box is not one to trust on a build box. | `scripts/env.ps1` |
| `scripts/fetch-pdfium.ps1` | ✅ (S0 · T12) | Fetches the pinned PDFium, verifies its **SHA-256** and refuses on mismatch, then generates the GNU import library upstream does not ship (`gendef` + `dlltool`). Idempotent. | `third_party/pdfium/` |
| `third_party/pdfium/` | ✅ (S0 · T12) | Fetched, never committed — only its `README.md` is. The reproducibility is in the pin, not in the bytes. | `scripts/fetch-pdfium.ps1` |
| `corpus/make-minimal.py` | ✅ (S0 · T12) | Generates `corpus/minimal.pdf`. A generator rather than a committed blob because a PDF xref stores **absolute byte offsets**: a one-character edit invalidates every one of them. Stage 2 owns the real corpus. | `corpus/`, `tools/spike-pdfium/` |
| `res/ribbon.json` | ✅ (S1 · T10) | **Data, not code — and it is the whole ribbon.** 9 tabs (one contextual), 49 groups, 192 controls, every id declared in `ActionId.def`. Tab and group titles are **catalog keys**, not text, because the catalog key is the source string in this project. `collapsePriority` is higher-collapses-first and is deliberately a different axis from visual order — conflating the two is I-05. Validated at build time (`tools/ribbon-validate`) and then **copied beside the executable**, next to `i18n/`, rather than compiled into a Qt resource: the same call T6 made for the `.qm` catalogs, so Stage 15 can ship it replaceable. The validation is what makes that safe, and the runtime parse is the *same* parser, so a hand-edited copy fails with the same diagnostics rather than yielding a quietly thinner ribbon. | `ui/shell/RibbonModel`, `ui/shell/RibbonTabBuilder`, `tools/ribbon-validate`, `tools/i18n-lint` |
| `i18n/locales.json` | ✅ (S1 · T6) | Which locales exist and which of them **ship** (D1-14). The `shipped` flag is what lets `missing-key` fail at all: `zh_CN` and `ja` are declared with `"shipped": false` and a reason, so their absence is *data the gate reads* rather than a case nobody wrote, and `i18n-lint --explain` names them. | `tools/i18n-lint/`, `app/Application` (S1 · T8) |
| `i18n/rawpage_en.ts`, `rawpage_zh_TW.ts` | ✅ (S1 · T6) | **Data, not code.** Qt Linguist catalogs at **463 keys each** — 358 `action.*`, **34 `shell.*`**, **61 `ribbon.*`** (58 tab and group titles from `res/ribbon.json`, plus 3 the ribbon's own code names), **10 `error.*`**. The `shell.*` group grew by 6 at T11 (the toolbar and the tool finder) and 6 more at T12 (the document tab, the title with a document in it, and the open dialog's own chrome). **The catalog key is the `<source>` and `en` is a real catalog rather than the identity**, because `core/` may hold no user-facing English (`core/Result.h`) and `ActionId.def` already carries one key per action; there is consequently no English in the sources for `lupdate` to extract, and these are hand-maintained with `missing-key` and `action-label` keeping them honest. Each group arrived **with the code that uses it** — `shell.*` at T9 and again at T11 and T12, `ribbon.*` at T10; `prefs.*` arrives the same way at T14 — seeding them earlier would be a hundred `orphan-key` notes and a catalog nobody could review. Compiled to `.qm` **beside the executable** by `i18n/CMakeLists.txt` so Stage 15 can ship them replaceable, and installed as **two** translators at runtime, `en` underneath the active locale: because the key is the source string, an untranslated string falls back to its own key rather than to English unless the source catalog is there to catch it. | `tools/i18n-lint/`, `ui/*` (S1 · T9+) |
| `THIRD-PARTY.md` | ✅ (S0 · T8) | **Generated** by `license-audit`, never hand-edited, and committed per release (§8.2). The About → Third-Party Notices dialog is built from the same data, so a dependency cannot ship without appearing in both. | `tools/license-audit/` |
| `.github/workflows/ci.yml` | ✅ (S0 · T13) | Three jobs. `windows` and `portable-asan` both `needs: gates`, so a layering or licence violation stops everything before a runner installs a toolchain. `windows` is **self-hosted**: aqtinstall cannot fetch Qt 6.11.1, and downgrading the pin to suit CI was rejected. | everything |
| `.gitattributes` | ✅ (S0 · T1) | Marks `corpus/**` and `golden/**` binary. Not cosmetic: Git's text heuristic misreads a minimal uncompressed PDF, and CRLF injection would shift every xref offset — silently turning a fixture into a different file than the test was written against. | `corpus/`, `golden/` |

---

## 4. Breadcrumb convention

When a planned file is folded into another, its row **stays** with status ⤵ and a Role of
"folded into `X` — see that row", because a reader chasing a reference from an older stage needs the
trail. The same applies to renames. Rows are never silently deleted.

*(No foldings yet. Two rows are nonetheless breadcrumbs rather than plans: `platform/win/WinSpeech`,
withdrawn 2026-08-23 with the accessibility surface, and — recorded here because a reader will look
for it — `platform/PlatformModule.cpp`, Stage 0's placeholder translation unit, **deleted at Stage 1
T7** once `platform/` had real content, exactly as its own header comment instructed. It never had a
row, because it never had a responsibility to fold anywhere.)*

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
      ├─ Settings(SettingsSchema, %APPDATA%\RawPage\settings.ini)
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
 └─ WinShellIntegration's registered verb launches rawpage.exe <path>
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
     └─ LineBreaker + Shaper ─▶ page layout
         ├─ sink.replacePageContent(...)  ─▶ ContentStreamBuilder ─▶ new content streams
         └─ sink.replaceStructureTree(...) ─▶ TagTree built FROM the reflow model      // O2
             // The reflow model is dropped, but its STRUCTURE survives as standard PDF
             // tags -- so the next Convert to Writable reads tags instead of re-guessing
             // from glyphs. (Until 2026-08-23 this also bought PDF/UA proximity for free;
             // accessibility is out of scope now, and the round-trip fidelity argument --
             // which was always the primary one -- is untouched.)
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
rawpage.exe --batch redact-and-secure.seq --input C:\in --output C:\out --quiet --json
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

- **Portability** — the fifteen `platform/` interfaces are wide but shallow, and *none* is in the
  document-editing path. A future `platform/mac/` implements the same header; `core/` and `pdf/`
  need no change. The list was derived from the "needs platform code" table in
  [`Tech-Stack-Plan.md` §4.3](./Tech-Stack-Plan.md#43-what-qt-gives-us-for-free-versus-what-needs-platform-code),
  not invented.
- **Testability** — `core/` links `QtCore` and nothing else. `tests/unit/` deliberately links
  *without* `rawpage_ui` and `rawpage_platform`, so the boundary is verified by the build on every run
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
