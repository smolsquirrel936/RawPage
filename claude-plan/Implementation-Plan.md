# RawPage — Implementation Roadmap (Staged Build Plan)

## Context

This is RawPage's **staged build plan** — the execution backbone and the living progress log. It is
derived from three upstream documents and adds only *ordering, verification, and status*:

- [`Features.md`](./Features.md) — **what** to build, and the authoritative key-binding tables that
  each stage below cross-references. No stage redefines a chord.
- [`Tech-Stack-Plan.md`](./Tech-Stack-Plan.md) — **how**: the locked stack, the layer contract, the
  mutation-through-commands design, and the risk-reducing first milestone that becomes Stages 1–3.
- [`Architecture-FileMap.md`](./Architecture-FileMap.md) — **where**: every *Components* entry below
  names a file that has a row in that map, with the same stage tag. *For per-file detail, read the
  map; this document does not repeat it.*

**Stack recap:** C++20 on **MinGW-w64 GCC 13.1.0, Qt's own bundled compiler** · Qt 6.11.1 Widgets,
dynamically linked (LGPL-3.0) · PDFium (BSD-3) as a C-API DLL for read/raster + a first-party COS
layer for writes · CMake 3.30.5 + Ninja 1.12.1 (both from the Qt install) + vcpkg
(`x64-mingw-dynamic`) · Catch2 v3 · MSI/MSIX.

**The promise.** Every stage carries a fixed template — *Status, Objectives, Components, Key
bindings handled, Data/settings keys, Cross-cutting additions, Algorithms & notes, Testing, Exit
criteria, Deferrals* — and **each stage is independently verifiable before the next begins**. A
stage is not done because it looks done; it is done when its exit criteria are met, its automated
suite is green, and its manual checklist has been walked.

---

## 1. Shared infrastructure & conventions

These apply to **every** stage. They are built in Stages 0–3 and then reused, never re-invented.

### 1.1 The action layer — one dispatch path for all input

Every user action is a named `ActionId`. The ribbon, the Quick Access Toolbar, context menus,
keyboard chords, the tool finder, the command line, and batch sequences **all** resolve to an
`ActionId` and call `ActionRegistry::dispatch(ActionId, KeyContext)`. Nothing is wired directly to a
widget slot.

This buys three things that would each otherwise be a project of its own: bindings are **remappable**
(the store is data, not code); the same chord **disambiguates by context** — which is the only way
`Ctrl+K` can be *Preferences* while reading and *Insert hyperlink* while writing, exactly as
[`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference) specifies; and every
feature is reachable **headlessly**, which is what makes Stage 14's batch runner and CLI possible
without a second implementation of anything.

`KeyContext` is computed from focus by `ui/shell/ShortcutBinder` and is one of: `Global`, `Viewer`,
`WriteMode`, `AnnotationTool`, `Thumbnails`, `Bookmarks`, `Comments`, `FormsDesign`, `FormsFill`,
`Search`, `FullScreen`.

### 1.2 One typed settings store

`core/settings/Settings` over `%APPDATA%\RawPage\settings.ini`, with `PolicyOverlay` layering
machine-wide administrative defaults and locks on top. Conventions:

- Every key is declared once in `SettingsSchema` with a type, default, range, group, and a
  policy-lockable flag. **An undeclared key is a compile-time error** — this is what stops settings
  sprawl across seventeen stages.
- Groups mirror the Preferences pages of
  [`Features.md` §9.1](./Features.md#91-preferences-groups): `general/`, `documents/`,
  `pageDisplay/`, `fullScreen/`, `commenting/`, `forms/`, `identity/`, `measuring/`, `reading/`,
  `search/`, `security/`, `signatures/`, `trust/`, `units/`, `spelling/`, `multimedia/`,
  `convertTo/`, `convertFrom/`, `batch/`, `javascript/`, `performance/`, `language/`, `shortcuts/`,
  `updates/`, `ai/`, `logging/`.
- Named-profile collections (stamps, watermarks, headers/footers, security schemes, signature
  appearances, conversion profiles, batch sequences) go through the one `ProfileStore`, never into
  ad-hoc files.

### 1.3 The cross-cutting gate — mutation-through-commands

Per [`Tech-Stack-Plan.md` §5](./Tech-Stack-Plan.md#5-the-headline-cross-cutting-concern-designed-end-to-end-mutation-through-commands):
**every document change is a `Command` run through `CommandJournal`, and no `ui/` or `core/` code
writes PDF bytes.** Four gates hold it, and every gate that exists runs in CI on every push.
**All four are live** — `layer-lint`, `license-audit` and `i18n-lint` from Stage 1 T6, and
`mutation-lint` from Stage 3 T6, with the stage that gave it something to check:

| Gate | Fails the build when… |
|---|---|
| `tools/layer-lint` | an include crosses a layer boundary outward; a PDFium call **or handle** is named outside **`pdf/render/`** (narrowed from `pdf/` at S2 · T2); a filesystem write appears outside `pdf/`, `core/settings/`, `tools/` and `tests/`; a path is read out of `argv` outside `app/main.cpp`; a `QAction` is constructed outside `ui/shell/ActionFactory.cpp`; an OS header reaches `platform/platform.h` or an engine/QtGui include reaches `core/backend.h`. `rules.json` is the executable form |
| `tools/mutation-lint` | a `Command` subclass under `core/cmd/commands/` has no `core/cmd/Commands.def` row, or no test case tagged `[serialize:Class]`, or none tagged `[revert:Class]`; or a table row's header does not declare its class (Stage 3 D3-12). It proves a test exists; a revert that lies is `tst_reversibility`'s |
| `tools/i18n-lint` | a user-facing literal is not wrapped in `tr()`; a shipped locale is missing a catalog key; an `ActionId` has no `action.*` entry; or — from S2 · T16, issue I-04 — **a source names a catalog key that no catalog defines** (`unknown-key`, anchored on the five declared families so a file name is not mistaken for a key). `orphan-key` is the one rule that reports rather than blocks |
| `tools/license-audit` | any dependency's SPDX identifier is outside the allowlist (this is what mechanically enforces the no-GPL rule) — and from S2 · T7 it audits the **resolved closure**, enumerating every installed port's SBOM rather than only what `vcpkg.json` declares (issue I-02) |

`cmake --build --target checks` runs **all four, and says four** — until Stage 3 T6 it ran and said
three, and a target naming a count that disagrees with what it runs is the failure these gates exist
to prevent, one level up — and **CI runs
`checks` before compiling**, so a layering violation fails in seconds rather than after a full build.

### 1.4 Standing policy across stages

- **Strings out of code** (`tr()` + `.ts` catalogs) from Stage 1, with `zh-TW` loaded in CI so a
  hard-coded string is visible immediately, not at translation time.
- **`Result<T>` at boundaries** — no exception crosses a layer.
- **Warnings are errors** (`-Werror` over `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor`) in
  `core/` and `pdf/`; `ui/` has a grace list that must only shrink. The toolchain pin is what keeps
  this from breaking the build on an unrelated GCC upgrade.
- **A new `Command` ships with its inverse and its tests, or it does not ship.**
- **Every dialog collects parameters and constructs a `Command`.** A dialog containing document
  logic is a review rejection, not a style preference.
- **No stage adds a feature that flow K (headless batch) cannot reach.** If it is only reachable
  from a widget, it is not finished.

---

### 1.5 Conventions discovered while building — these bind all later work

> **Every bare `Tn` in this subsection is a STAGE 1 task**, and every bare `I-nn` is a
> `Stage1-AppShell-Testing-Guide.md` §8 issue unless it says otherwise. These were written as
> Stage 1 discovered them; Stage 2 has a `T1`…`T18` and an `I-01`…`I-16` of its own, so an
> unqualified "T9" here means the shell task that built `MainWindow`, not the one that opened a
> document. Conventions found in a later stage are appended with their stage named inline.
>
> **These moved here from `CLAUDE.md` on 2026-09-06**, which had grown a second copy of §1's
> subject. This document owns shared conventions (see the document-set table in `CLAUDE.md`);
> that file now carries the one-line list and points here.

1. **Nothing may read a path out of `argv`.** Measured at T1: this machine's ANSI code page is
   **950**, and the CRT replaces anything it cannot represent with `?` — an emoji path came back as
   `report-?????.pdf` in `argv` while `QCoreApplication::arguments()` was byte-perfect. Parse
   `QCoreApplication::arguments()` and nothing else. **Closed at T8 by three mechanisms** (issue
   **I-04**): there is no `argc/argv` overload and a compile fixture proves it; `layer-lint` carries a
   **non-escapable `argv-path-read` rule** permitting `argv` only in `app/main.cpp`; and
   `tst_commandline` asserts an emoji path survives byte for byte. Proven end to end when a forwarded
   `測試-📄.pdf` crossed three process boundaries unchanged.
2. **Catch2 test names are ASCII — and contain no comma.** Catch2 splits a filter on commas, so a
   name with one cannot be selected by hand even though CTest escapes it when running the suite (T9).
   The ASCII half: `catch_discover_tests` hands the name back to CTest as a
   command-line filter and a `§` does not survive the round trip — the test then "fails" with a
   passing body. Section references go in comments. (Issue **I-07**.)
3. **`core/` may not name a Qt GUI type.** `QKeySequence` is QtGui, so `core/cmd/Chord` is
   first-party and `ui/shell/ShortcutBinder` converts at the boundary.
4. **`settings.ini` is read through our own codecs, never through `QVariant`'s conversions.**
   Measured at T5: QSettings' INI reader turns any comma-containing value into a `QStringList` and
   `QVariant::toString()` on one returns an *empty string*, so `logging/subsystems=render,parse` read
   back as unset; a one-element list comes back as a bare `QString`, so pinning exactly one file lost
   it on restart; and a section named `[general]` is QSettings' own reserved fallback section, so the
   hand edit the testing guide asks for was silently ignored. All three are handled and each has a
   test that fails if the handling is removed. (Issues **I-08** and **I-09**.)
5. **The catalog key is the `<source>`, and `en` is a real catalog rather than the identity.**
   `core/Result.h` forbids user-facing English in `core/` and `ActionId.def` carries one `action.*`
   key per action, so there is no English in the sources for `lupdate` to extract. Every locale,
   `en` included, supplies the text. Consequences: a key missing from `en` is a detectable defect
   rather than a button rendering `action.file.open`; the catalogs are hand-maintained data, not
   generated; and `ui/` resolves through the catalog rather than relying on a fallback. (T6.)
6. **An unimplemented `platform/` interface is a null member, never a stub.** A stub that silently
   succeeds lets a caller believe a signature was created or a scan happened, and that surfaces
   stages later as data nobody produced. `unimplementedInterfaces()` names the gaps so "not built
   yet" is printable. Equally: **`platform/platform.h` may not include an OS header** — it is its
   own `layer-lint` layer entry with a fixture and a control, because `core/`, `ui/` and `app/` all
   include it. (T7.)
7. **Every negative control must actually exercise the thing.** Two of T3's first probes appended
   rows *after* an `#undef` and proved nothing; a control that does not fail for the right reason is
   worse than none. T9 applied it to a crash: the `~MainWindow` disconnect was verified by commenting
   it out and watching exactly one case segfault.
8. **A gate reads the files the compiler reads, `.def` tables included.** Four X-macro tables are
   `#include`d straight into layers that have deny lists, and until T9 neither gate scanned them —
   so `i18n-lint` called 22 referenced keys orphans, and an `#include` written inside a table would
   have been invisible to `layer-lint`. Whether a file is *compiled* is a fact about the build; it
   says nothing about whether a gate should read it. (Issue **I-16**.)
9. **`ui/` looks outward to exactly two files, and each is constrained by its own layer entry.**
   `platform/platform.h` and `app/Services.h`. Both are interface headers naming only `core/` types;
   both have a `layer-lint` entry saying so, with a fixture and a control in each direction. Widening
   that list is an architectural decision, not a diff. (Issue **I-15**.)
10. **A widget that connects a child's signal to a lambda holding `this` must disconnect in its
   destructor.** `~QWidget` closes the window, which hides children, which emits — into an object
   whose derived part is gone. Qt drops the connection only when the *QObject* part goes, and that is
   last. (Issue **I-12**; found by `tests/ui` on its first run.)
11. **`core/` may not let the *host* decide a domain rule.** `RecentFiles` deduped paths through
   `QDir::cleanPath`, which converts *platform-native* separators — so `C:\x\a.pdf` collapsed onto
   `C:/x/a.pdf` on Windows and stayed a separate document on Linux. Every path this product sees is a
   Windows path; `core/` builds on Linux only so the sanitizers can run, and a rule that differs
   between the two builds means the sanitized build is not exercising the shipping build's logic —
   which makes *"reproduce it in `x64-debug` before it counts as fixed"* impossible to satisfy.
   Normalize by hand, not by platform. (Issue **I-17**; and note the corollary — a Windows-green
   `core/` test proves nothing about `portable-asan` until that job has actually run.)
12. **A widget test must drive layout itself, and `resize()` alone is not driving it.** Offscreen
   there is no event loop, so a bare `resize()` marks layouts dirty and returns; the geometry a real
   window gets on its next paint never arrives. A ribbon suite that trusted `resize()` reported six
   collapsed groups at every width from 1600 down to 400 — monotone, reproducible, and meaningless.
   `show()` the widget (offscreen is what the platform is for), then `activate()` **every** layout
   level involved. And when a case asserts a consequence of a size, assert the size it actually got
   first: `resize(240)` returning a 1044-pixel widget is a finding, not a failed assertion. (T10;
   issue **I-19** was found exactly this way.) **And assert what the LAYOUT gave the widget, not
   only what the widget contains**: eleven ribbon cases asked the tab bar for its page count, its
   current index, which tabs the chevron had taken — all correct, all answered from the bar's own
   contents — while the layout was handing that bar **zero pixels** and no tab was on screen for
   six days (**I-36**).
13. **A self-check meets its real subject once, and that is when it gets corrected.** Two of T10's
   three findings were the *check* being wrong, not the product: Qt manufactures actions nobody asked
   for (`QMenu::menuAction`, `QDockWidget::toggleViewAction`), and `QToolButton::setDefaultAction`
   adds a receiver, so "exactly one receiver on `triggered`" — the invariant the plan wrote — was
   false for all 192 ribbon buttons at once. Run a self-check against the *product*, not only against
   fixtures that prove it can fail. (Issues **I-18** and **I-20**.)

14. **A command handler may not capture the window that registered it.** `ActionRegistry` is one
   object shared by every `MainWindow`, and registration is deliberately not idempotent — so with a
   second window (T12 tears a tab out into one) the first window's handler would act for the whole
   application, including after that window was destroyed. Registration happens **once per
   registry**; every handler resolves `MainWindow::activeShell()` at **dispatch** time. That is also
   what makes a Global command mean *the window I am looking at*, which is what it always meant.
   Corollary measured under `offscreen`: `activateWindow()` raises no `ActivationChange` there, so
   `raiseToForeground()` sets the active shell itself rather than deriving it from an event that may
   not come.
15. **A chord never becomes a `QAction` shortcut or a `QShortcut`.** Qt's shortcut system cannot say
   *innermost context, then `Common`, then `Global`* — the resolution order that IS the binding model
   and the reason `Ctrl+K` means two different things — and it would have put each user remap into 360
   actions as a shadow copy of `KeyBindingStore`. Every chord goes through `ui/shell/ShortcutBinder`:
   focus chain → `KeyContext` → store → `ActionRegistry::dispatch`. A widget declares its context with
   `ShortcutBinder::declareContext()` and nothing else does. This is D1-7's rule in its keyboard form,
   and it is checked the same way — `MainWindow::showEvent` reports any action carrying a key sequence
   and any free-standing `QShortcut`, with one of each planted in `tests/ui/tst_context.cpp`. (T13.)

16. **A command that needs a user is DISABLED where there is none — it does not block.** `file.open`
   raises a modal file picker; under `QT_QPA_PLATFORM=offscreen` that dialog is shown to nobody and
   never returns, which stopped a whole `ctest` run dead with no failure and no output. Its
   enablement predicate is false when there is no one to ask, so a headless dispatch is refused with
   `Unavailable` — distinct from `NotFound`, which is exactly what those two codes are for. Every
   such command needs a parameterized twin that needs no user (`openFiles()` is `file.open`'s), and
   the general answer is **O11**. (Issue **I-23**.)

17. **A surface generated from a declarative table reads the TABLE, not a runtime description of
   it.** `ui/dialogs/SettingsPage` builds one row per key by `#include`ing `core/settings/SettingsKeys.def`
   with a macro that instantiates a template, so every editor holds the real `constexpr Key<T>` and calls
   the typed `get`/`set`. Iterating `SettingsSchema::allKeys()` would have been the obvious loop and is
   the trap: a descriptor carries a *path*, so a page built from descriptors needs a string-keyed
   `Settings::get`, and adding one for a dialog's convenience makes D1-10's *"an undeclared key does not
   compile"* decorative for the entire product. The runtime table still owns *which widget* and *what to
   call it*. (T14.)

18. **`core/Log.h` has ONE sink and `setLogSink` REPLACES it — so nothing chains it.** Anything that
   wants records is fanned out from the single sink `app/CompositionRoot` installs: the console, the
   rolling file, the in-memory buffer the log pane reads. A widget that captured the installed sink and
   installed a wrapper would be dropped the next time the logging settings changed — which is exactly
   what the Preferences page it shipped alongside does — and two windows doing it would nest in
   construction order and unwind in closing order. It is also I-12's bug in a new disguise: a lambda
   installed by a widget outlives the widget unless something removes it. (T14; the reasoning lives in
   `core/LogBuffer.h`.)

19. **A test that reaches a behaviour through a back door must say what it therefore does NOT
   cover.** `tst_documenttabs.cpp` asserted re-dock by calling `adoptDocument` directly, which proves
   the *destination* works and says nothing about whether the gesture can reach it. A comment recorded
   the gesture as manual-only, so the gap was known — and the case still read as coverage to everyone
   who came after, including the guide, the file map and this document, all four of which described
   re-dock as shipped. It was unreachable by hand for six days and green the whole time (**I-34**, the
   stage's first S2 in the product). The rule is not *"test the gestures"* — offscreen there is no
   pointer and that limit is real. It is that the back door has to be named at the assertion, so the
   next reader subtracts it instead of adding it. The corollary is convention 7's: a case that cannot
   fail for the reason it claims to test is worse than no case at all.

20. **A colour is judged in BOTH themes, or it is judged in neither.** `disabledText` was picked
   against a light mock at T9 and read 2.54 there and 3.05 in dark — the theme suite was green
   throughout, because it asked whether each token *resolves*, whether light and dark *differ*, and
   whether the background/text pair is the right way round. None of those is the question a person
   asks, which is *can I read it*. Two manual passes signed off the light theme without switching
   the OS, so the first person to try dark found it (**I-40**). Any new colour is a `ThemeToken`,
   and `tst_theme` measures it against the surface it is painted over **in both themes** as a
   contrast ratio against a floor — never as expected hex values, which would make the case a copy
   of the table it checks. The floor is a legibility bar, **not** an accessibility one: that left
   the product on 2026-08-23. The same case asserts disabled text stays under half the enabled
   ratio, because a floor raised until they match trades one defect for a worse one.

21. **A palette is not a theme: the native style paints from the COLOUR SCHEME, and it wins.**
   Qt's `windows11` style takes control text from the scheme it derives from Windows and ignores
   `QApplication::setPalette` while doing it — measured, an enabled `QToolButton` painting `#1f1f1f`
   on a `#1f1f1f` background while a `QLabel` beside it obeyed the palette exactly. So a themed
   shell has to call **`QStyleHints::setColorScheme()`** as well, and `Unknown` is the value that
   means *follow the OS* — set it **before** anything reads the scheme back, or "follow system"
   silently means "follow whatever I last chose". Offscreen the call is inert (no readback, no
   signal), so the widget suite can assert the mapping and nothing further; the proof that pixels
   changed is a rendered widget read back by hand. (**I-41**, found because the previous fix — a
   correct one to a real defect — changed nothing on screen.)

22. **A case proving a mechanism FIRES is half of proving it fires WHEN IT SHOULD — and the criterion
   will not ask for the other half.** Stage 2's guide gave T16 one sentence: *a missing embedded font
   substitutes through our policy, proven by a case that names the substitute*. That case passed. The
   case beside it — asking what happens to a font that is **not** missing — measured the product
   replacing **Helvetica with Segoe UI on every page of every document**, because PDFium consults its
   font hook for the base-14 faces too and four files in this project had written down that it would
   not (**S2 · I-13**). The defect was in the half nobody had specified, it would have changed how
   every existing document renders, and it would have been baked into the golden baselines one task
   later. This is convention 7 pointed at a different target: 7 says a control must fail for the right
   reason, and this says **a new mechanism needs a control at all**, written from *what must stay
   untouched* rather than from the criterion. The three T16 shipped are the shape — a base-14 font, an
   embedded font, and the hook uninstalled entirely.
   *(Stage 2 T16. The other half of the lesson is I-10's: the four files that said it could not happen
   were each repeating a claim written by somebody who had not built the thing.)*

23. **A button that shows a command is an `ActionToolButton`, never a stock `QToolButton` with a
   default action — and anything retained per document is measured as LIVE heap, never as private
   bytes.** Qt 6.11.1's `QToolButton` re-applies its default action on every `ActionChanged` and
   connects `changed()` again each time, with no check for the connection it already holds: a stock
   button's action gains one connection per change, for ever. Every open and close of a document
   changes the enabled state of every Viewer command, so the ribbon and QAT gained **56 connections
   per document opened** — found only by counting `QObject::receivers()` per signal, because a
   connection is not an object and no object count moved. `ui/shell/ActionToolButton.h` drops the
   button's own `changed()` connections before Qt re-adds its one; guide case **A-90** keeps a stock
   button as its control, so the day Qt stops re-connecting the case says the class can go. The
   second clause is how that and three other retentions were found at all: private bytes include
   freed memory a heap keeps, swung by **180 MB** between rounds while live heap moved kilobytes, and a
   first diagnosis read from private bytes pointed at the wrong layer. `tests/bench/HeapCensus` sums
   busy heap blocks; `rawpage_bench_openclose_ui` is the harness (**Stage 2 · I-35**, guide case **A-92**).
   *(Stage 2, found in manual case M-09 and fixed the same day.)*

---

## 2. Cross-cutting testing strategy

Five layers, because a PDF editor fails in five different ways.

| Layer | Covers | Where |
|---|---|---|
| **Unit (headless)** | All pure logic: page tree, outline, annotation store, form model and calculation graph, permissions, text layout analysis, selection, search, line breaking, table formulas, change tracking, signature *decisions* (mock `ICryptoProvider`), COS lexer/parser/filters/crypt, every command's inverse. Linked **without** `rawpage_ui` and `rawpage_platform`. | `tests/unit/` |
| **Golden-image render** | Rasterization fidelity. Corpus pages rendered at 96/144/192 DPI and compared to `golden/` with a perceptual threshold; a diff image is written on failure. Baselines are regenerated only through `tools/golden-update` with a stated reason — a rendering regression must never be laundered into a new baseline. | `tests/golden/` |
| **Corpus round-trip** | Save fidelity. Open → run a command → incremental save → reopen: the mutation is present, untouched objects are **byte-identical**, and a pre-existing signature still validates. Run over the whole corpus including encrypted, signed, tagged, XFA, and malformed files. | `tests/roundtrip/` |
| **Fuzzing + sanitizers** | Parser robustness — a security property, not a quality one. libFuzzer over `pdf/cos/Lexer` and `pdf/cos/Parser`, seeded from `corpus/malformed/`, plus ASan+UBSan over the `core/` and `pdf/cos/` unit suites. 10 minutes clean before every release; nightly long runs. **Neither builds under MinGW GCC** (no sanitizer runtime, no libFuzzer), so both run on the `portable-asan` / `portable-fuzz` non-Windows presets over exactly the two layers that are portable by rule — see [`Tech-Stack-Plan.md` §6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management) and risk R7. Anything they find is reproduced in the Windows `x64-debug` build before it counts as fixed. | `tests/fuzz/` |
| **Per-stage testing guide** | One guide per stage, carrying **both** halves of that stage's verification: an **automated** section — the exact commands over the four layers above, with pass conditions, run by the implementer — and a **manual** section for what a person must see on a real machine: window behaviour, multi-monitor and mixed-DPI, input and tools, shell integration, printing, scanning, signing with a physical token. **Written immediately after the stage's plan and before the stage's code** — see §5. | `Stage<N>-<Area>-Testing-Guide.md` |

Plus **`tests/bench/`**, which is not pass/fail but gates a decision: per-tile render latency at
each DPI over the corpus, settling risk A2 — and, since Stage 2 issue I-35, memory retained per document
opened (guide case A-92).

### 2.1 The build's test targets — the split is load-bearing, not organizational

*Moved here from `CLAUDE.md` on 2026-09-06, which had grown a second copy of this document's
subject. The live counts stay in the stage status blocks in §4; what this table owns is **why each
target is separate**, which is a design fact rather than a status one. `CLAUDE.md` carries the
one-line summary and points here.*

Counted 2026-09-02, re-counted at S2 · T17, which added the fifth, and at **S3 · T11, which added a sixth — `rawpage_composition_tests`**: this heading read *"three"*
from Stage 1 T9 until 2026-09-02, having missed `rawpage_render_tests` (S2 · T2) and
`rawpage_bench_render` (S2 · T3) even though the table below already listed the first — so the count
is re-measured with the target rather than remembered afterwards. A sixth prefix, `gates.`, has **no binary at all** — its cases drive the
gate executables and CMake itself through `cmake -P`, which is what lets the `gates` preset run it
with no Qt and no vcpkg. **Split of the 741, re-measured 2026-09-14 after Stage 2 I-39 and I-40** (`ctest -N`): `unit.` 346, `ui.` 207,
`gates.` 94, `render.` 55, `platform.` 36, `bench.` 3 — `render.` includes T17's ten `render.golden.*`, which are a
separate BINARY inside the same PREFIX, deliberately: the prefix is what CI's non-empty check reads,
**and as of T18 that check finally reads `render` at all**. The stage status blocks in §4 remain the place for a count at a named moment. **One of the 741 is deliberately SKIPPED rather than green** —
`platform.powersession reports the machine's power source`, on a desktop with no battery, which is
guide case A-62's own *"report not applicable explicitly rather than pass vacuously"* honoured.

| Target | Links | CTest prefix | Runs where |
|---|---|---|---|
| `rawpage_tests` | `rawpage_core`, `rawpage_cos`, **`rawpage_ops`** (S3 · T3, portable, no engine — `gates.headless_assert.ops_*`) | `unit.` | Windows + `portable-asan`. It also compiles **three** `app/` translation units **in** rather than linking them — `CommandLine.cpp` (a pure `QStringList` → intent parse), `LogFile.cpp` (QtCore and nothing else) and `SingleInstance.cpp` (T15; names only `core/` and the interface header `platform/platform.h`) — and compiling rather than linking is what keeps the assertion below true. `rawpage_assert_headless()` — in `cmake/RawPageHeadless.cmake` since Stage 2 T1 — **fails configure** if this ever links `rawpage_ui`, `rawpage_platform` or **`rawpage_pdfium`**, directly or transitively; that assertion is the standing proof that `core/` needs no display and no engine, and `tests/gates/fixtures/headless-assert/` is the five-case fixture proving it fires — with the control proving it does not fire on a clean closure. **From S2 · T5 it also holds the COS reader's suite**: `rawpage_cos` contains `pdf/cos/Lexer` and, from T6, `CosObject` + `Parser`, and `tst_cos_lexer` / `tst_cos_parser` reach `corpus/` through a `RAWPAGE_CORPUS_DIR` compile definition — the same mechanism as `RAWPAGE_FEATURES_MD`, and for the same reason: a test that guessed a relative path would pass or fail on whatever working directory CTest happened to use |
| `rawpage_platform_tests` | `rawpage_core`, `rawpage_platform` | `platform.` | **Windows only** (S1 · T7). **Since 2026-09-14 it also depends on the `rawpage` target** and is handed its path: `tst_manifest.cpp` (guide case A-93, Stage 2 issue I-35) reads the executable's embedded segment-heap manifest and starts it, because reading resources is a Win32 question and this is the Windows-only suite |
| `rawpage_ui_tests` | `rawpage_core`, `rawpage_ui` | `ui.` | **Windows only**, `QT_QPA_PLATFORM=offscreen` (D1-4). **Arrived at T9**, not the plan's T15, on the precedent `tests/platform` set at T7. It installs `rawpage_en.qm` so the widgets are exercised against real text rather than their own catalog keys. **From S2 · T13 every fixture comes from `tests/ui/ShellFixture.h`** — seven copies of the same eight fields became a seven-way edit the moment `app::Services` grew a render pipeline — and that header supplies the FAKE rasterizer the viewer suite runs on, since this target may not link an engine. Its tile colour is chosen so that *a page is on screen* is distinguishable from *a white sheet was drawn*, and it pads its rows so the `bytesPerLine` assumption is exercised somewhere |
| `rawpage_render_tests` | `rawpage_core`, `rawpage_cos`, **`rawpage_ops`** (S3 · T3, for the page walk against the engine), `rawpage_pdfium`, **`rawpage_platform`** (S2 · T16) | `render.` | **Windows only** (S2 · T2). **From S2 · T13 it also holds the only case that drives the REAL engine through the REAL scheduler** (`tst_renderpipeline.cpp`, guide A-79): every other suite fakes one end of that path — `tests/unit` and `tests/ui` may not link an engine, and the rest of this target calls the rasterizer with no scheduler — so the assembly the product actually runs had no case at all until then (issue **I-09**), which is why `app::BackendRasterizer` is a header rather than a class private to `CompositionRoot.cpp`. It links the ENGINE, which is the whole reason it is a separate target: the raster suite is the pressure that would otherwise put PDFium into the headless closure and destroy what `rawpage_assert_headless()` proves. It needs **no display** — there is no Qt GUI object in it — which is how "initializing PDFium does not require a display" is established rather than assumed. **From S2 · T9 it also holds the SHIPPED BACKEND's census**, which `tests/unit/tst_backend.cpp` used to assert: `pdf::createBackend()` moved into `rawpage_pdfium` when it started returning something that opens documents, and the headless suite may not link that. The portable half of A-46 stays in `tests/unit` over a test backend defined in that file. **`rawpage_platform` joined the link line at T16, and it is I-09's finding a second time**: the font chain is PDFium → `FontResolverBridge` → `WinFontResolver` → `core/render/FontSubstitution`, three libraries deep, and no existing target could hold more than one link of it — `tests/unit` may link neither engine nor OS layer, `tests/platform` may not link the engine. Adding the OS layer to a Windows-only, engine-linking target costs nothing of what this target's separateness protects, which is that **`tests/unit`** links no engine and no display |
| `rawpage_golden_tests` | `rawpage_core`, `rawpage_cos`, `rawpage_pdfium`, `rawpage_platform`, **QtGui** | `render.golden.` | **Windows only** (S2 · T17). **The fifth target, and QtGui is why it is one**: it reads and writes the 198 PNG baselines in `golden/`, and `tests/render`'s separateness rests on that target linking no Qt GUI library at all — which is how *the engine needs no display* is established rather than assumed. Folding a codec into it would have retired that property with every test still passing, which is the same pressure `tests/unit` met at Stage 1 T7 and T9 and this target meets from a fourth side. It needs no display either: `QImage` is a raster class and Qt's PNG handler is built into QtGui rather than being a plugin, so no `QGuiApplication` exists here. **Its CTest prefix is inside `render.`** on purpose — a sixth prefix would have to be remembered separately by CI's non-empty check. It compiles `tests/golden/GoldenSuite.cpp`, which `tools/golden-update` also compiles in, so the producer and the consumer of `golden/` cannot disagree about what a render is, how it is encoded, or what *the same* means |
| `rawpage_composition_tests` | `rawpage_core`, `rawpage_cos`, `rawpage_ops`, `rawpage_pdfium`, `rawpage_platform`, `rawpage_ui` — and `app/CompositionRoot.cpp`, `CrashHandler.cpp`, `LogFile.cpp` and `RecoveryPrompt.cpp` **compiled in** | `ui.composition.` | **Windows only**, `QT_QPA_PLATFORM=offscreen` (S3 · T11). **The sixth target, and it is separate because it is the only one that may link all three of the engine, the widgets and the OS bundle**: its subject is the product's own composition — guide case A-74 replays a journal `rawpage_killprobe` left behind through `CompositionRoot`, `MainWindow` and `RecoveryPrompt` — and each target above proves that one of those is NOT needed by another, a property this link line would retire wherever it went. **Its prefix sits inside `ui.`**, as `render.golden.` sits inside `render.`, so CI's non-empty check reads it without being told. It depends on `rawpage_killprobe`, `rawpage_translations` and `rawpage_resources` |
| `rawpage_bench_ribbon` | `rawpage_core`, `rawpage_ui` | `bench.` | **Windows only**, offscreen (T15). Not a test target and the difference is load-bearing: a test asserts and fails, **a benchmark reports, and its exit code is about whether the measurement was *possible*** — never about whether the machine was fast, or a loaded runner turns a green build red for a reason unrelated to the change. It is registered with CTest anyway, because a benchmark nobody runs is one that has stopped compiling by the time its number matters |
| `rawpage_bench_pipeline` | `rawpage_core`, `rawpage_cos`, `rawpage_pdfium` — and deliberately **NOT** raw `pdfium` | `bench.` | **Windows only** (S2 · T18). **A2's PRODUCT figure** (D2-1, second half) and the numbers that answered **O7** and **D2-14**. It drives the assembly the product runs — backend → `app::BackendRasterizer` → `RenderScheduler` → `RenderCache`, with `TileGrid` choosing tiles and `DpiPolicy` the resolution — and the absent `pdfium` link line is the structural half of that claim: it **cannot name an `FPDF_` symbol even by accident**, so a "measurement of the product's render path" cannot quietly become a measurement of something below the facade. It creates no Qt GUI object. **A second binary rather than a rewrite of `bench_render`**, whose own CMakeLists predicted the rewrite: the probe's figure has to stay re-takeable (D2-1 keeps both in `Tech-Stack-Plan.md` §3.3) and `--unsafe-parallel` is I-05's reproducer, so deleting that program would have made a recorded finding folklore |
| `rawpage_bench_render` | `rawpage_core`, `rawpage_cos`, `rawpage_pdfium` | `bench.` | **Windows only** (S2 · T3), and it creates **no Qt GUI object at all** — which is how "the engine needs no display" is established rather than assumed. A2's probe figure, and the run that found **I-05**. It takes an optional path, so any corpus file can be put through the real engine by hand; `8 --unsafe-parallel` reproduces the heap corruption on demand, because a finding that needs a source edit to reproduce is one that gets re-litigated |
| `rawpage_bench_openclose`, `rawpage_bench_openclose_ui` | the first: `rawpage_core`, `rawpage_cos`, `rawpage_pdfium`, `rawpage_platform`; the second adds `rawpage_ui` and compiles `app/`'s composition in | *none — deliberately not CTest cases* | **Windows only** (Stage 2 issue I-35, 2026-09-13). **Memory per document opened**, below `ui/` and through the product's own `CompositionRoot` and `MainWindow`; guide case A-92 is the second's reading. They report a trend over dozens of rounds, for which no threshold is decided, which is why they are not registered — and both embed the product's segment-heap manifest (`app/rawpage.rc`) so their figures describe the product's allocator |
| `rawpage_bench_openclose_ui_classicheap` | as `rawpage_bench_openclose_ui` | *none* | **Windows only**. **The control**: the same harness built WITHOUT the manifest, so the classic heap's fragmentation (I-35) stays reproducible without editing a build file |

---

### Definition of Done — every stage, no exceptions

1. Code complete for the stage's Objectives.
2. `cmake --build --target checks` clean — every gate that exists at that stage. `checks` reports
   how many it ran; if it ever *claims* four before Stage 3, that is the bug.
3. `ctest` green: the stage's new unit tests **and** every prior stage's.
4. Golden-image and round-trip suites green (from Stage 2 and Stage 3 respectively).
5. The stage's `Stage<N>-<Area>-Testing-Guide.md` was **written from the stage plan before the code**
   (§5), its **automated** section runs green, and its **manual** section has been walked and signed
   off by a person. Automated-green with the manual pass outstanding is a reportable state, not a
   done one.
6. No open S1 or S2 issue against the stage.
7. `Architecture-FileMap.md` rows for the stage's files updated from ○ to ✅ **in the same change**.
8. This document's Status block updated to reality, including any deviation from the plan.

---

## 3. Stage dependency order

```
0 ─▶ 1 ─▶ 2 ─▶ 3 ─▶ 4 ─▶ 5 ─▶ ┬─▶ 6 ─┬─▶ 8 ─▶ ┬─▶  9 ─┬─▶ 11 ─▶ 12 ─▶ 13 ─▶ 14 ─▶ 15 ─▶ 16
                              └─▶ 7 ─┘         └─▶ 10 ─┘
```

- **0 → 1 → 2 → 3** is strictly sequential and is [the first milestone](./Tech-Stack-Plan.md#7-recommended-first-milestone-foundation-vertical-slice). Nothing parallelizes here; each proves a decision the next one builds on.
- **6 and 7** may run in parallel — annotations and page assembly touch disjoint parts of the model.
- **9 and 10** may run in parallel after 8.
- **11** needs 10 (encryption re-keying shares `pdf/cos` crypt code) and 8 (incremental save of a signature dictionary).
- **13** is the largest stage and depends on 5's `LayoutAnalyzer`; it may be split 13a/13b and its two halves parallelized after 13a's model lands.
- **16** is post-v1 and its items are independent of each other.

---

## 4. Stages

### Stage 0 — Toolchain, skeleton, and the four gates

**Status:** 🟢 **DONE, 2026-08-19.** All 15 tasks complete; **both halves of [`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md) are green** — A-01…A-73 run by me, M-01…M-07 walked and signed off by the user on 2026-08-18/19. CI run `32207889494` is green on all three jobs: `gates` 25, `windows` 66, `portable-asan` 52. Verdicts recorded: **A5** and **A6**.

> **What the manual pass cost, and bought.** Seven cases produced **22 issue-log entries** and took the suite from 50 tests to 66/52. Three were **S1**, and all three came from manual cases with none reachable by the automated half: **I-01** (Qt 6.11.1 is behind the installer's Archive filter, so the README led to the wrong Qt), **I-04** (a stock Windows account runs no scripts, and no script can diagnose that — the diagnosing script is itself blocked), and **I-15** (`layer-lint` missed outward includes spelled `../`, `<>`, or with different case, and Qt writes entirely). The first two made a clean-machine build unreproducible from the documentation; the third meant the gate the next sixteen stages lean on did not fail on real violations. One **S2** followed later: **I-22**, the licence register implying no attribution was owed for eleven BSD/MIT/zlib rows. None of these was visible from a green automated run.

Both planning artifacts were written first, in the §5 order:
[`Stage0-Toolchain-Plan.md`](./Stage0-Toolchain-Plan.md) (execution plan — tasks T1–T15, the
Stage-0-local decisions D0-1…D0-8, and the §7 precondition checklist) and
[`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md) (written **from** that plan,
**before** any code). **No guide case has been formally run. The A5 verdict is recorded (T6); A6 is still open.** Preconditions V-1…V-4 verified 2026-08-17 — vcpkg at `C:\Users\squirrel\vcpkg`, baseline
`94a5411977…`, community triplet `x64-mingw-dynamic` present.

| Task | State | Outcome and deviations |
|---|---|---|
| **T1** repository skeleton | ✅ 2026-08-17 | `.gitignore`, `.gitattributes`, `scripts/env.ps1`, `scripts/bootstrap.ps1`, root `README.md`. `env.ps1` yields cmake 3.30.5 · ninja 1.12.1 · g++ 13.1.0 (`x86_64-w64-mingw32`), resolves `g++` to the pin, and sets `VCPKG_ROOT` in-process — retiring the "setx does not reach a running process" problem. **Deviation:** `claude-plan/skills-specs-generation` had been committed as a gitlink with no `.gitmodules` — an accidental submodule that would have failed a fresh clone and so **case M-01**; untracked and ignored instead |
| **T2** target graph | ✅ 2026-08-17 | Root + five layer `CMakeLists.txt`, `RawPageWarnings.cmake`, `RawPageQt.cmake`, four placeholder TUs, `app/main.cpp`. Five TUs compile clean under `-Werror`; four static libs and `rawpage.exe` link; the exe **runs, exits 0**, subsystem **Windows GUI**, imports **`msvcrt.dll`, never `ucrtbase.dll`**. `rawpage_link_qt()` **fails configure on `Qt6::Charts`** with the GPL-3.0 diagnostic. **Deviations:** (a) `-municode` deferred to Stage 1 — measured, `-mwindows -municode` with a plain `main()` fails to link (*undefined reference to `wWinMain`*), and inventing a stub to satisfy a flag is worse than waiting for the real entry point; (b) `RawPageQt.cmake` pulled forward from T7, because the wrapper must exist before the *first* Qt link; (c) guide case **A-41** corrected — see the note there |
| **T3** toolchain pin | ✅ 2026-08-18 | `cmake/toolchain-mingw.cmake` + `cmake/ToolchainAssert.cmake`, TA-1…TA-9. Clean configure reports *"Toolchain pin verified: GCC 13.1.0 x86_64-w64-mingw32, SEH/posix/msvcrt, no GCC plugin"*; `compiler-plugins.txt` written empty. **Six negative tests pass** (`tests/gates/fixtures/wrong-compiler/`, WC-1…WC-6, pulled forward from T9 and run by hand until T9 wraps them in CTest): wrong version (**TA-2**), `_UCRT` defined (**TA-6**), `__MSVCRT__` undefined (**TA-6** cannot pass vacuously), bad `RAWPAGE_MINGW_ROOT`, injected `-fplugin`, and `-static-libstdc++` — each failing configure with its own diagnostic. **Deviations:** (a) `CMAKE_SYSTEM_NAME` is **not** set, against the plan's §5.1 sketch — setting it in a toolchain file switches CMake into cross-compiling mode, disabling `try_run()` and altering `find_package()`, for no benefit on a native build; (b) the `-fplugin` scan is **duplicated** into the toolchain file, because a plugin that fails to load breaks GCC's own compiler probe *before* `project()`, and CMake then reports "the compiler is broken" instead of the licence consequence; (c) **TA-6's probe now compiles with `CMAKE_CXX_FLAGS`** — writing WC-2 exposed that the original bare-compiler probe let `-DCMAKE_CXX_FLAGS=-D_UCRT` configure cleanly and report "msvcrt" while the whole build compiled as UCRT, i.e. the assertion was interrogating a compilation the project never performs. **Still untested:** TA-1, TA-3, TA-4, TA-5, TA-7 (each needs a differently *built* GCC) and TA-8 (a second Qt) — recorded in the fixture README, and covered by manual case **M-02** |
| **T4** presets | ✅ 2026-08-18 | `CMakePresets.json`, version 6: `x64-debug`, `x64-release`, `arm64-release`, `portable-asan`, `portable-fuzz`, plus build and test presets. vcpkg is outermost with the pin chainloaded via `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` — verified working before `vcpkg.json` exists (manifest mode simply stays off until T6). `hostSystemName` conditions hide the Linux presets on Windows and vice versa. **Windows presets fully verified**: configure, build, and `rawpage.exe` exit 0; `arm64-release` exits 1 with the O6 explanation. **Deviations:** (a) the toolchain pin and TA-8 are **skipped** when `RAWPAGE_PORTABLE_SUBSET=ON` — a Linux gate build compiles the same sources with a different compiler on a different OS, which is the entire point, so asserting "exactly GCC 13.1.0, SEH, msvcrt" there would reject the configuration we are trying to prove; (b) `arm64-release` **fails at configure** rather than configuring, and guide case **A-03** was tightened from "succeeds or fails explicitly" to one expected outcome. **Untested:** `portable-asan` and `portable-fuzz` are JSON-schema-valid (`--list-presets` parses the whole file) but **have never been run** — there is no Linux here. They are first exercised by CI at T13, and that is where a mistake in them will surface |
| **T5** vcpkg baseline | ✅ 2026-08-18 | `vcpkg-configuration.json` pins baseline `94a541197763a4f449a1b91478df48c0584a6256` and registers `tools/ports/` as an overlay directory (created, empty, with the rule written down: a port that builds only under MSVC is vendored there, **never** worked around by changing the triplet) |
| **T6** manifest + **A5** | ✅ 2026-08-18 | `vcpkg.json` declares every §6.2 vcpkg dependency across five features. **A5 verdict recorded** in [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified): all five groups **resolve** on `x64-mingw-dynamic`; no MSVC-only port; no overlay needed. The default set was **really built** — catch2 3.15.3 → `libCatch2.a`, installed and consumed through the preset. **Deviations:** (a) D0-3 is applied more broadly than written — *everything* except the test harness sits behind a feature, because making OpenSSL a default dependency would couple Stage 0's build to a Stage 10 need and let a Stage 10 port failure block Stage 1; (b) `VCPKG_HOST_TRIPLET=x64-mingw-dynamic` added to the presets, since the default `x64-windows` host triplet resolves MSVC — present here, absent by design in **M-01**. **Not closed:** resolution ≠ compilation. `codecs`, `text`, `crypto`, `ocr` have never been compiled; ICU and OpenSSL are the known MinGW risks, and each group closes when its stage switches the feature on |
| **T7** layer-lint | ✅ 2026-08-18 | `tools/layer-lint/` (RuleSet · IncludeGraph · LayerLint · main) plus `tools/common/Json.h`, all standard-library-only per D0-1. **Clean on the real tree** (13 files, 38 includes) and **six fixtures verified by hand** in `tests/gates/fixtures/layer-lint/`: `core/`→`pdf/`, `ui/`→`pdf/`, `FPDF_` outside `pdf/`, a write outside the allowed layers, `<QtWidgets/QWidget>` in `core/` — each exit 1 with `file:line: rule: detail` — plus **`write-escaped`, a negative control proving the inline waiver works** (exit 0). `--explain` prints the graph, the rules, the exclusions, and every live escape; a missing rules file exits **2**, not 1. **Deviations:** (a) external includes are checked by **deny-list, not allowlist** — an allowlist would have to enumerate the whole standard library and would fail closed on every unlisted header, which teaches people to edit the rules file to get work done; the linker enforces the remainder, since `core/` links only `Qt6::Core`; (b) `tools/common/Json.h` is a new shared file the plan did not name — the gates cannot use Qt's JSON (D0-1) and T8 must read `vcpkg.json`; (c) `core/settings/` is its own layer entry with longest-prefix-first matching, since it is the one part of `core/` permitted to write; (d) fixtures pulled forward from T9, which now inherits working cases. **`tools/layer-lint/` flipped ○ → ✅ in `Architecture-FileMap.md`** — the first row in the project to do so |
| **T8** license-audit | ✅ 2026-08-18 | `tools/license-audit/` (LicenseAudit · Inventory · main) + `spdx-map.json`. Reads **vcpkg's own SPDX SBOM** (`vcpkg.spdx.json`) for exact versions and concluded licences, falling back to the map; runs with **no installed tree at all**, because the CI gates job has no vcpkg. `THIRD-PARTY.md` generates. The allowlist appears in exactly one place. **Deviations:** (a) the plan's signature `audit(const VcpkgManifest&, const QtModuleList&)` is preserved, so the compiler-plugin re-check is a separate `checkCompilerPlugins()` called from `main`; (b) **findings are blocking or warning** — see the note below |
| **T9** checks + gate tests | ✅ 2026-08-18 | `cmake/RawPageChecks.cmake` and `tests/gates/`. `--target checks` runs both gates and **depends only on the two gate executables, never the layer libraries** — the property that makes "gates before compile" real. **16 CTest cases, all passing**, via `RunGateCase.cmake`, which asserts **both** exit code and diagnostic: CTest's `WILL_FAIL` cannot tell exit 1 from exit 2 (a correct finding from a crashed gate), and `PASS_REGULAR_EXPRESSION` ignores the exit code entirely. Includes four **negative controls** — the escaped write, `GPL-3.0-or-later WITH GCC-exception-3.1`, `LGPL-3.0`, and an allowed Qt module set — without which every positive case is equally satisfied by a gate that rejects everything. **Extended after a completeness audit: 23 cases**, and to **25** when I-08 added the two `THIRD-PARTY.md` emission cases with their `EXPECT_FILE`/`EXPECT_ABSENT` assertions. The six `wrong_compiler` cases (TA-2, TA-6 both directions, bad root, `-fplugin`, `-static-libstdc++`) had been written and passing by hand at T3 but never wired, while the testing guide already named `ctest -R gates.wrong_compiler` as their command. They now run against a **minimal fixture project** rather than the real one: each case is a full CMake configure, and the real project pulls in vcpkg and Qt at ~200 s apiece, which would have put twenty minutes into a suite that otherwise runs in 3.5 s |
| **T10** core vocabulary | ✅ 2026-08-18 | `core/Result.h`, `Log.h`, `Geometry.h`, `Units.h` — header-only, QtCore-only, no OS and no engine. `Result<T>`/`Result<void>` with `RAWPAGE_TRY`; `Error` carries a **catalog key, not a sentence**, since `core/` has no `tr()` context and must hold no user-facing English. `Log.h` enforces *"never write document text or file paths above debug level"* **by type**, via `Redacted<T>` — a code-review rule is enforced by whoever happens to review; a type is enforced by the compiler. `Geometry.h` keeps PDF space (y-up, points) and device space (y-down, pixels) as types with **no conversion between them at all** except through `PageTransform`, which must be handed zoom, rotation and physical DPI |
| **T11** unit harness | ✅ 2026-08-18 | `tests/unit/` on Catch2 3.15.3, registered with `catch_discover_tests`. **50 tests pass** (16 gate + 7 toolchain + 27 unit); **66** since I-08, I-10, I-11, I-13, I-15, I-17 and I-20 (26 gate + 13 toolchain + 27 unit); Linux runs 50, one fewer gate case than Windows minus the toolchain pin, because the case-mix spelling cannot occur on a case-sensitive filesystem (I-19). The **headless boundary is asserted at configure time, over the resolved link closure** — verified by adding `rawpage_ui` and watching configure refuse. `DL_PATHS` points CTest at the mingw runtime, or every test fails to *launch* with `0xC0000135`, which looks nothing like a test failure. **Deviation:** `rawpage_link_qt()` changed from `PRIVATE` to `PUBLIC` — `core/`'s public headers include `<QString>`, so Qt's include path is part of core's *interface*; with `PRIVATE`, every consumer fails to find QString and the tempting repair is to re-link Qt in each one, scattering the dependency the layer contract keeps in one place |
| **T12** PDFium spike — **A6** | ✅ 2026-08-18 | **A6 verdict recorded** in [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified). Pinned **PDFium 153.0.8009.0** (`chromium/8009`, non-V8, SHA-256 verified); `gendef` read 472 exports and `dlltool` produced the GNU import library upstream does not ship; the spike links from `g++`, opens `corpus/minimal.pdf`, and reports `pages=1`, `595.276 x 841.890 pt`. Imports are `msvcrt.dll`, **never `ucrtbase.dll`**. Durable outputs: `scripts/fetch-pdfium.ps1` (hash-pinned, idempotent) and `third_party/pdfium/README.md`; the spike source stays until Stage 2's facade replaces it. **C-1 done**: `corpus/minimal.pdf`, generated by `corpus/make-minimal.py` rather than hand-typed, because a PDF xref stores absolute byte offsets and a one-character edit invalidates every one of them. **Two findings:** (a) `pdfium.dll` imports **no C runtime at all** — it statically links its own — so there is no shared-CRT surface in either direction, a *stronger* result than A6 assumed; the consequence is that **memory allocated by PDFium must be freed by PDFium**, now a rule rather than a style note; (b) the prebuilt archive's `LICENSE` is the **packager's MIT**, not PDFium's BSD-3 text or its third-party notices, which §8.1 requires us to ship — carried to Stage 15 |
| **T13** CI | 🟡 2026-08-18 — **`gates` green; `windows` blocked on infrastructure** | `.github/workflows/ci.yml`, three jobs, `windows` and `portable-asan` both `needs: gates`. **First run: `gates` PASSED** — the enforcement gates run on a bare Linux runner with no Qt and no vcpkg, in seconds, before anything else starts. That validates D0-1 and the ordering guarantee. Writing the job also exposed a **false claim**: the root `CMakeLists.txt` called `find_package(Qt6 REQUIRED)` unconditionally, so the no-Qt job could not have existed; fixed with `RAWPAGE_GATES_ONLY` and a `gates` preset, verified locally with Qt and vcpkg deliberately hidden (3.8 s, 23/23). **`portable-asan` failed and is fixed**: `ubuntu-latest` exports vcpkg as `VCPKG_INSTALLATION_ROOT`, not `VCPKG_ROOT`, so the preset's `$env{VCPKG_ROOT}` expanded to empty — `windows-latest` sets both, which is why it only failed on Linux. **`windows` failed for a cause outside our control** — see below |
| **T14** portable-asan proof | ✅ 2026-08-18 | Ran in CI on `ubuntu-latest`: **43 tests clean under ASan+UBSan** (16 gate + 27 unit); **45** since I-08, pending the next CI run. This is the layer contract proven mechanically rather than asserted — `core/` compiled by a *different compiler* on a *different OS*, with no display and no PDF engine present. `-fno-sanitize-recover=all` is set, so a finding aborts rather than printing and continuing. The 43-vs-50 gap is the seven toolchain-pin cases, correctly absent where there is no toolchain to pin |
| **T15** verdicts and lockstep | ✅ 2026-08-18 | `Architecture-FileMap.md` gained **§3.18** (build system, scripts, CI) and every Stage 0 row is ✅ — the map previously described every planned `.cpp` and none of the machinery that compiles them. Testing guide reconciled against what actually ran. `Tech-Stack-Plan.md` carries the A5 and A6 verdicts, the corrected Hunspell licence, and the `-fanalyzer` finding |

> **⚠️ `aqtinstall` cannot install Qt 6.11.1 — CI's Windows job needs a self-hosted runner.**
> Investigated 2026-08-18 rather than assumed. Qt 6.11.1 *is* published and `tools_mingw1310`
> *is* listed; `Updates.xml` and its `.sha256` both return HTTP 200. With hash checking
> disabled, aqt reveals the real error: it requests `…/qt6_6111/qt6_6111/Updates.xml` → **404**,
> while Qt 6.11 splits its repository per architecture as `…/qt6_6111/qt6_6111_mingw/` → 200.
> aqtinstall **3.3.0 (latest) and 3.2.1 fail identically**, and a trusted-mirror config does not
> help — the "checksum" error was masking the 404.
>
> The choice was: downgrade the Qt pin to a version aqt can fetch, or run the job where the
> pinned toolchain already lives. **Downgrading was rejected** — TA-8 asserts 6.11.1, and
> testing against a different Qt than we ship is exactly the ABI question A7 exists to prevent.
> [`Stage0-Toolchain-Plan.md` §9](./Stage0-Toolchain-Plan.md#9-ci-design-d0-7) already named the
> self-hosted runner as the fallback and said the pin is not negotiable to make CI easier; that
> prediction came true on the first run.
>
> The job is now `runs-on: [self-hosted, windows]`, gated on repository variable
> `RAWPAGE_HAS_WINDOWS_RUNNER` so the workflow stays honest until a runner is registered — a
> queued-forever job, or one marked `continue-on-error`, would claim coverage that does not
> exist. **Until then, Windows build/test coverage is local only**, and Stage 0's Definition of
> Done cannot be met on that item.

> **Two bugs the gate found in itself on first run, both fixed.** `$`-prefixed comment keys in
> `spdx-map.json` were being read as components with no licence — JSON has no comment syntax, so the
> convention vcpkg uses was turned against us. And the audit initially **failed** on dependencies this
> build does not ship: `vcpkg.json` declares every future dependency so that A5 could be answered early
> (D0-3), which meant Stage 0 could not go green until every future licence was reconciled — and the
> tempting way to make that go away is to widen the allowlist, the precise failure this gate exists to
> prevent. Findings are now **blocking** for components in the active feature set and **warnings** for
> declared-but-unbuilt ones, with `--feature` to promote them. The warnings are printed on every run,
> so nothing is lost, only deferred to the stage that owns it.
>
> ✅ **Finding raised and resolved, 2026-08-18.** The gate reported Hunspell's `MPL-2.0` as not on the
> §8.2 allowlist. Checking upstream showed the owning document was wrong in a way nobody had noticed:
> Hunspell's grant is **`MPL-1.1 OR LGPL-2.1-or-later OR GPL-2.0-or-later`** — it does not offer
> MPL-2.0 at all, so §6.2 was electing a licence that was never granted. Resolution (yours): elect the
> **LGPL** branch, taken as **LGPL-3.0** since the grant reads *"or later"*. That identifier is already
> on the allowlist and is exactly Qt's licence, so **the allowlist did not have to be widened** — the
> gate was satisfied by fixing the fact, not by loosening the rule. `Tech-Stack-Plan.md` §6.2 corrected
> in the same change. Consequence carried forward: Hunspell now owes a **corresponding-source offer**
> like Qt's, currently a `TODO-BEFORE-RELEASE` in `spdx-map.json` and owned by Stage 15's release gate.
> The audit's LGPL check was generalized from the exact string `LGPL-3.0` to any LGPL expression, so a
> future `LGPL-2.1-or-later` component cannot ship without a source offer.

This stage exists because the gates in §1.3 are worthless if added
later — by then the violations are already in the tree.

**Objectives.** A repository that builds an empty executable on a clean machine, with all four
enforcement gates already failing correctly on deliberately bad input, every third-party dependency
resolved and license-audited, and **the two remaining toolchain unknowns (A5, A6) settled before
either can cost a later stage a week**. A7 is already settled — the build uses Qt's own bundled
GCC 13.1.0, so there is no second toolchain to reconcile
([`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified)).

**Components & responsibilities.**
- `CMakeLists.txt`, `CMakePresets.json` — four layer targets (`rawpage_core`, `rawpage_pdf`,
  `rawpage_platform`, `rawpage_ui`) plus the `rawpage` executable; presets `x64-debug`, `x64-release`,
  `arm64-release`, `portable-asan`, `portable-fuzz`; a `checks` target.
- `cmake/toolchain-mingw.cmake` — the **pinned** toolchain: `C:\Qt\Tools\mingw1310_64` (GCC 13.1.0,
  `x86_64-w64-mingw32`, SEH, posix, msvcrt) as a cache-variable default, plus the standard
  warning/charset/subsystem flags from
  [`Tech-Stack-Plan.md` §6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management). **Configure
  fails, loudly, on any other compiler** — keeping A7 settled rather than merely documented, since
  the next developer's MSYS2 install is the realistic way this regresses. Also asserts no non-free
  GCC plugin is loaded (the Runtime Library Exception condition, `Tech-Stack-Plan.md` §8.1).
- `vcpkg.json` — manifest with a pinned baseline, resolved on the **`x64-mingw-dynamic`** triplet;
  covers every dependency in
  [`Tech-Stack-Plan.md` §6.2](./Tech-Stack-Plan.md#62-third-party-libraries). Settles risk A5, whose
  answer is now triplet-specific: a port that builds only under MSVC is found here or not at all.
- A **PDFium link spike** — the smallest program that calls `FPDF_InitLibrary` and
  `FPDF_LoadDocument` against a PDFium **DLL** and opens one corpus file, with the `.dll.a` import
  library generated if upstream ships none. Settles A6. It is throwaway code; what it produces is a
  recorded answer and the documented acquisition step for `pdfium.dll`.
- `tools/layer-lint/` — `int run(const IncludeGraph&, const RuleSet&)`; `--explain` prints the graph.
- `tools/license-audit/` — `Report audit(const VcpkgManifest&, const QtModuleList&)`; emits
  `THIRD-PARTY.md`; fails on any SPDX identifier outside the allowlist.
- `core/Result.h`, `core/Log.h`, `core/Geometry.h`, `core/Units.h` — the shared vocabulary.
  `Geometry.h` gives PDF-space and device-space point/rect types that **do not implicitly convert**.
- `tests/unit/` — Catch2 harness, linked without `rawpage_ui` and `rawpage_platform`.
- CI pipeline — `checks` → build → `ctest`, on every push.

**Key bindings handled.** None.

**Data / settings keys.** None (the schema arrives in Stage 1).

**Cross-cutting additions.** None — the mutation gate turns on in Stage 3, but `layer-lint` and
`license-audit` are live from this stage.

**Algorithms & notes.** `Geometry.h`'s non-converting coordinate types are worth the friction: a
silent PDF-point/device-pixel mix-up is the single most common class of bug in this domain, and it
manifests as "slightly wrong at 150% scaling" — the hardest kind to find later.

**Testing.**
*Automated:* the gates are tested against fixtures — a file with an outward include, a file with a
stray `FPDF_` call, a manifest with a GPL dependency (**and one carrying bare `GPL-3.0-or-later`, to
prove the GCC-runtime allowance is an exact-expression match and not a `GPL` substring hole**) —
each must fail — **including a build that links `Qt6::Charts`, which is installed on this machine and
is GPL-3.0-only**. `Result` and unit conversions have unit tests. A clean-machine CI job proves the
build works from a bare checkout, and a second CI job configures `portable-asan` on Linux to prove
`core/` really does build without a display or a PDF engine from day one.
*Manual:* → **written**: [`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md)
(see §5), seven cases M-01…M-07 — a short manual section, because almost everything here is
automatable. In brief: clone into an empty directory on a machine with a stock `C:\Qt\6.11.1` install
and nothing else; run the documented bootstrap; confirm it builds without a single manual step. Then
deliberately point the configure at a different GCC — an MSYS2 UCRT64 one is the realistic case — and
confirm it refuses. **Refined by writing the guide:** the wrong-compiler check splits in two — a stub
compiler reporting the wrong *version* is automated (case A-04), while only a **real** UCRT64 install
exercises the CRT assertion, so M-02 is the residue a stub cannot fake.

**Exit criteria.** Clean checkout → documented command → empty `rawpage.exe` builds; `checks` passes
on the real tree and fails on each bad fixture, Qt Charts included; `THIRD-PARTY.md` is generated,
contains no non-allowlisted license, and lists Qt 6.11.1 and the GCC runtime with its exception;
**A5 and A6 have recorded verdicts** — the vcpkg manifest resolves on `x64-mingw-dynamic` and the
PDFium DLL spike opens a file — and **A7 stays settled**: a mismatched compiler fails configure.

**Deferrals.** `mutation-lint` (needs `Command`, Stage 3); `i18n-lint` (needs `tr()` sites, Stage 1 — **delivered at T6**);
arm64 is configured but not validated on hardware, and per R8 may need LLVM-MinGW rather than GCC —
that is O6's problem, not Stage 0's. `portable-fuzz` is wired but has no targets until Stage 2 gives
it a lexer.

---

### Stage 1 — App shell, action layer, settings, single instance

*Part 1 of the [first milestone](./Tech-Stack-Plan.md#7-recommended-first-milestone-foundation-vertical-slice).*

**Status:** ✅ **SIGNED OFF 2026-08-31**, when M-12, the last manual case, was walked and signed off by the user (status flipped here 2026-09-14; the date is the one `README.md` records). **T1–T15 all complete as of 2026-08-25, and the automated half is green in CI. Until 2026-08-31 the
stage was open on the manual walk and nothing else.** The A3 verdict is recorded, the §8 collision census is asserted, `checks` runs three gates, the OS boundary is declared in full, flow A runs end to end, and `rawpage.exe` opens a window whose ribbon — 9 tabs, 49 groups, 192 controls — is built entirely from `res/ribbon.json`, with a customizable Quick Access Toolbar, a tool finder, document tabs that open a stub per file, a keyboard in which every one of §8's Global chords resolves through `KeyBindingStore` and dispatches through the same `ActionRegistry` call the ribbon makes, and — since T14 — **a Preferences dialog whose every page is generated from `SettingsSchema`, a Shortcuts page that remaps a chord with nothing rebuilt, and a log pane over a rolling log file.** `ctest` is **334/334** on both presets and the bare `gates` preset is **66/66**. **Flow J is real**: changing the theme repaints the window behind the dialog.

**What T15 leaves open, precisely.** **Every automated guide case is green, A-80 included.** **CI is green on all four jobs** — run 32785493072, and **the `windows` job ran for the first time in the project's history**: 334/334 there, `gates` 52/52, `portable-asan` 196/196.
That job's long absence was never the missing runner every document had assumed: one had been online
since 2026-08-17, and the block was a variable **name** mismatch (**I-30**, S2). So three quarters of
the product's test surface — the widget suite, the OS layer, the toolchain pin, and the gates run
against the *real* configuration where `qt-modules.txt` and `compiler-plugins.txt` actually exist —
had never once been exercised by CI. It has now, and it was green on the first attempt. A fourth job,
`coverage-report`, states in every run summary which jobs ran and what went unexercised when one did
not, so a skipped job can never again be misread as coverage.

**The manual walk has started finding things the harness could not.** The user's M-03 walk on
2026-08-30 produced **I-33** (S3) and **I-34** (S2) — the stage's first defects **in the product**
rather than in a test, a gate, a document or the CI. I-34 is the one to remember: re-dock was
documented as shipped in four places, asserted green by a case that called `adoptDocument` directly,
and impossible for a person to perform for as long as it existed, because the gesture reported at the
40 px threshold crossing instead of at the release. Both were fixed the same day — three cases, three
negative controls, `ctest` **337/337** on both presets. Fixing I-34 then exposed **I-35** (S3) in the
same sitting — a window emptied by a re-dock stayed open, a state nothing could reach while re-dock
itself was unreachable — closed the same day with two more cases and two more controls, `ctest`
**339/339**. **M-03 was then re-walked in full and signed off by the user on 2026-08-30** — the
sign-off is on the repaired product rather than on the repair, which is the only reading of a fixed
case that means anything. Note the order: I-35 was unreachable until I-34 was fixed, so a case
interrupted by a defect is finished when it is walked again, not when the defect is closed.

**And then M-04 stopped at its own first step.** Its opening move is *View tab → Panes → Log*,
and on 2026-08-30 there was no View tab to click: the ribbon's `QTabBar` had been laid out **zero
pixels wide at every window size since T10** (**I-36**, S2), so the strip showed the tool finder
and nothing else, and only the page that happened to be current — Home — was reachable at all.
`QSizePolicy::Ignored` tells `QBoxLayout` that the widget's width preference is not a number, so a
**stretch factor** is the only thing that can state its share; beside a separate `addStretch(1)`
the spacer took the strip. Fixed the same day, `ctest` **340/340**, with the control convention 7
requires. The instructive part is why eleven green ribbon cases missed it: every one asked the tab
bar what it *contained* — page count, current index, which tabs the chevron had taken — and
`tabRect()` measures the bar's own contents, which stay perfectly correct inside a widget the
layout gave no width. Nothing asked what the **layout handed the widget**, and that is now part of
shared convention 12 in `CLAUDE.md`.

**M-04 was then re-walked from step 1 and signed off by the user on 2026-08-30**, with two more
results neither of which was a broken pane. **I-37** (S3): an auto-hidden sidebar answered a click
on its edge strip and not a hover, where the case's *Expect* asks for a hover — both gestures are
defensible, so this was closed by a **decision** rather than by default, and the user chose to move
the product to the guide; the strip now peeks on `QEvent::Enter` and still answers a click.
**I-38** is not a defect at all: `general/touchMode` is declared, stored, offered on the General
page and **read by nothing**, and the user's call was **planned absence, Stage 14** — it is now a
row in `Stage1-AppShell-Plan.md` §11 and in the guide's §1, and M-04's steps no longer ask for it.
**And re-walking that one gesture found I-39** (S3): the edge strip stayed on screen beside the pane
it had just summoned — behaviour unchanged since T9, and unseeable until I-37 made a peek something a
person could perform at all, since every earlier case reached `peek()` in a fixture where visibility
was not the subject. Fixed the same day and asserted in both directions inside I-37's own case.
Re-walked once more and **signed off by the user the same day**. `ctest` **341/341**.

**The same walk's tool-finder step then produced I-44 (S2), and it is the worst defect this stage
has found**: a query that matched nothing **froze the application**. The result list is a
`Qt::Popup` window and therefore holds the mouse grab for the whole process; the *nothing matched*
message is itself a row, so an empty result set still showed the list; and `Esc`, a click outside
and `Enter` on the unselectable row all failed to put it away. Every click anywhere went to a
popup that ignored it, and force-closing the process was the only way out. Found by making a
typo — which is how anyone would find it, and nobody had, since T11. Three exits added, each
independently missing, and the case asserts each one separately.

**And the fix for it shipped a regression, I-45 (S2), which the user found by re-walking the same
step within the hour.** Dismissing the list on any `FocusOut` closed it in the same turn it opened,
because **showing a `Qt::Popup` is itself a focus change** — the search box receives a focus-out
with reason `PopupFocusReason` exactly when the results appear — so no search showed anything at
all. The case added with I-44 could not see it: offscreen there is no such focus change, so the
assertion passed while the desktop did the opposite. Losing focus **to the popup** is not losing
focus; the rule is now asserted in both directions with the bug as its control. Two fixes for one
defect in one afternoon is the cost of a `Qt::Popup`, and it is written into the file map row.

**And answering a question about the QAT's context menu during that same sitting found I-43 (S3)**:
reordering the toolbar could not be done by a person. `moveCommand` had been public since T11 and
the order persisted since T11; `QuickAccessToolbar.h`, `CLAUDE.md` and `Architecture-FileMap.md`
all called reordering shipped; and there was no menu item, no drag and no `ActionId` — the only
caller in the tree was a test calling the method. **That is I-34's shape and the third instance in
this stage**, which is why the new case asserts the ROUTE as well as the destination — a
dispatch-only case would have passed against the build that shipped. **The route then changed
inside the same walk**: the user asked for drag-to-reorder, `buildMenu` dropped the two items and
gained `ui.qatLock`, and the case now asserts the commands stay registered and are **absent** from
the menu — the same route assertion pointed the other way (see the M-11 paragraph below).
`ui.qatMoveLeft`, `ui.qatMoveRight` and `ui.qatLock` are **the only ids Stage 1 has added to the
plan's 358**, which now stands at 361 declared and 23 registered. Logged against **M-11**, whose
steps say *customize the toolbar*.

**M-06 was signed off on 2026-08-31 after producing four defects, and it opened with I-42 (S2)**: pressing `Alt` did nothing,
in every locale. The ribbon has *drawn* key tips since T10 — two levels, `Esc` backs out — and
nothing ever asked it to: `Alt` is bound to `AppRibbonKeyTips`, `ShortcutBinder` forms the chord
and a case asserts that it does, and the id was never registered, so `dispatch` answered
`NotFound` and the key fell through. Every link was tested; the chain was not. And
`Tech-Stack-Plan.md` §3.3 had recorded **R-5 as met since 2026-08-23** — judged in M-01 against
the T2 spike, whose handler the shipped ribbon never inherited, which is I-36's shape exactly.
Registered as the **twentieth** of 358 ids, and the tip letters moved out of the tab titles into a
`ribbon.keytip.*` catalog family: the first-character scheme gave `zh-TW` 常 建 編 註 組 表 保 檢,
none of them typable, with 表 claimed by both 表單 and 表格工具. The new case asserts the letters
are ASCII and distinct **in both shipped catalogs**, because the half that was broken was broken
only in Chinese and M-01 walked English. `ctest` **345/345**. The A3 verdict stands — R-5 asked
whether Qt Widgets *can* host key tips and the spike answered it — but the row describing the
product was false for eleven days, and is now corrected the way T15 corrected R-9.

**M-05 then found I-40 (S3) at its first step**: switching Windows to dark left most of the shell's
text unreadable — not the theme failing to apply, but the **disabled** colour, which is what most
of this ribbon is drawn in because 358 commands are declared and 19 registered. Measured, it was
3.05 against the window and 2.62 against a pane; the light theme, signed off in two earlier passes,
measured **2.54** and was the worse of the two — M-01 and M-02 had walked light only, because until
M-05 nothing had a reason to switch the OS. Both raised, and the new case measures every foreground
token against the background it is actually painted over, in both themes, against a floor rather
than a target — and asserts in the same breath that disabled text stays under half the enabled
ratio, since a floor raised until the two match would trade the defect for a worse one. `ctest`
**342/342**. It is not an accessibility criterion: that left the product on 2026-08-23.

**And the I-40 fix did not work, which is how I-41 (S2) was found on 2026-08-31.** With the palette
corrected the text was still unreadable, and that is the fact that pointed away from the palette:
Qt's native `windows11` style paints control text from the colour scheme **it** derives from
Windows and ignores the application palette entirely. Rendering a widget and reading the pixels
back settled it — on a light-mode OS with the dark palette installed, an enabled `QToolButton`'s
glyph was `#1f1f1f` on a `#1f1f1f` background and a disabled one `#181b1f`, while a `QLabel` in the
same window honoured the palette exactly. So the theme reached every label and no button: the
ribbon, the toolbar, the tool finder. `apply()` now calls `QStyleHints::setColorScheme()` as well
as installing the palette, with **`Unknown` for follow-system** so the OS keeps the decision — and
that call is ordered before `systemTheme()` reads the scheme back, or "follow system" would read
our own last answer and report it as the system's. `ctest` **343/343**. The lesson is I-40's, one
level up: measuring the colour table confirmed the colour table and said nothing about what
reached the screen. **M-05 was then re-walked on the repaired build — both halves, follow-system
across a live OS flip and an explicit Dark held against a light-mode OS — and signed off by the
user on 2026-08-31.**

**M-04 took three passes and produced four findings, and that sequence is its real result**: a tab
strip nobody could click hid a pane gesture nobody could perform, which hid a strip nobody could
watch disappear. Not one of the three defects was a regression — two had shipped at T9 and T10 and
were green in CI throughout. **All nine walkable manual cases are now signed off — M-01 … M-07, M-11 and M-12**, the last of them on 2026-08-31, which closes the manual half of this stage. M-08 was withdrawn with accessibility; M-09 and M-10 test code that does not exist.

**Preparing M-11 found I-47 (S2) before the walk began**: `Settings::sync()` was called from one
place — `CompositionRoot`'s teardown — so every preference lived in memory until a clean quit and a
kill lost the lot. The case had said *settings are written when changed, not at exit* since it was
written and the code had said the opposite since T5; **the two had never been compared, because
M-11 had never been walked**, and the suite could not compare them either, since every settings
case called `sync()` itself. Flushed on change now, teardown kept as a backstop, `ctest` **348/348**.
**I-48** struck two of that case's steps as testing things Stage 1 never had — a recent-files
surface, the fifth built-but-unreachable capability of the stage, and a window-geometry restore
that belongs to `SessionState` at Stage 4.

**And walking M-11 found I-49 (S2) in its first step**: every Quick Access Toolbar customization
made the toolbar disappear. A widget constructed with a parent that is already on screen starts
hidden, and a hidden widget is an empty layout item — so add, remove, reorder, reset and the
empty-state label all produced invisible widgets and a row collapsed to 4 pixels of margin. The
constructor's rebuild was the only one that had ever worked. `ui.resetCustomization` — the
documented way back — rebuilt it invisibly too, so **restarting the application was the only
recovery**, and the user reported it as having accidentally closed the toolbar. Nine `[qat]` cases
missed it because every one of them asks the command LIST, which stays correct for a row nobody
can see: the sixth instance of this stage's recurring shape. `ctest` **349/349**.

**M-11 then produced three more, and one feature.** **I-50** (S3): only the 22 enabled commands
could be added to the toolbar, because Qt delivers no input event to a disabled widget and every
ribbon control carried its own `CustomContextMenu` — so right-clicking any of the other 338 did
nothing, and the toolbar's own empty space offered an `Add` that could never be enabled. **I-51**
(S3): the startup line printed `importOverrides`'s DROPPED count as the applied one, so a session
that applied the user's remap perfectly logged `0 of 1 user overrides applied` — and it cost real
time, because a wrong diagnostic sends its reader hunting a defect that is not there. **I-52**
(S3) is the one to remember: a defect in the drag feature two hours after it shipped, where a
slot among the buttons was used as an index into the layout, so a drop past the last button
landed on the trailing stretch — **and the case written with the feature missed it**, because it
asserted the command list, which reads identically either way. The invariant that catches it is
positional, and that is the third time in two days the same blind spot has been paid for.

**The feature itself was the user's request, made during the walk**: the Quick Access Toolbar now
reorders by **dragging** a button, with a **lock** toggle guarding against doing it by accident
(`ui/qatLocked`, unlocked by default). The two move commands stay REGISTERED although the context
menu no longer offers them, because a drag reaches nothing but a pointer and this project's
standing rule is that no feature may exist which flow K cannot reach — the menu was simplified as
asked, the reachability was not traded away. `ctest` **354/354**; 361 ids declared, 23 registered.

**The remaining Definition-of-Done item is that walk, and it is the user's alone**: M-01 and
M-02 are signed off — **M-02 re-walked in full on 2026-08-30**, all four steps against the shipped
shell rather than T2's spike, with no findings, which retires the last scoping note A3's R-6 carried;
M-03 and **M-04** are signed off too, both on 2026-08-30, and **M-05 and M-06 on 2026-08-31** —
M-05 a case this guide had listed as unwalkable, wrongly, since both halves of it had existed since
T14, and M-06 the longest walk of the stage: every surface in `zh-TW` plus the editorial review of
the 29 unfinished catalog entries, which no automated case can do. **M-07 was walked and signed off on 2026-08-31** — flow B end to end, all five steps, one process
throughout and both awkward filenames intact, which is the one thing no automated case can judge:
D1-11's `AllowSetForegroundWindow` grant is refused to a process that received no input, so *raises*
and *flashes in the taskbar* look identical to a harness. It became walkable the same day, once two
preconditions its own text had never stated were dealt with (**I-46**): the Qt runtime deployed beside the executable, since Explorer does not
inherit the dev shell's `PATH`, and a hand-made Windows association, since **no route in the product
can register one** — implemented, tested, unreachable, and declared a planned absence for Stage 14/15.
**M-11 was walked and signed off on 2026-08-31** — the case that produced the most of any in this
stage, and the only one that changed the product's feature set while being walked. **M-12 was walked and signed off on 2026-08-31 and was the last one**, which closes the manual
half and with it Stage 1's Definition of Done. It produced **I-53**, disposed of as a planned
absence: plan §5.1's *registered-but-disabled* tooltip was never built, and the user's call was
that at Stage 1 the second kind of disabled is nearly theoretical — 338 of 361 ids are
unregistered, so almost every disabled control a person meets is the explained kind, and the few
that are not are refused for reasons visible on screen. It becomes real at Stage 2. M-09 and M-10
test code that does not exist yet.
Risk **A3** —
can Qt Widgets host a real ribbon, tear-off tabs, and auto-hiding panes without a commercial control
library — is **settled: `go, with named custom work`**. The fallback (a first-party ribbon widget,
*not* a change of toolkit) is not needed.

Both planning artifacts were written first, in the §5 order:
[`Stage1-AppShell-Plan.md`](./Stage1-AppShell-Plan.md) (tasks T1–T15, decisions D1-1…D1-16, the §7
precondition checklist P-1…P-4) and
[`Stage1-AppShell-Testing-Guide.md`](./Stage1-AppShell-Testing-Guide.md) (written **from** that plan,
**before** any code). Preconditions: **P-3 and P-4 satisfied**; **P-1 satisfied** when M-02 was walked
on 2026-08-23, and **exercised to its full extent on 2026-08-30** — the mid-session scale change and
the re-login at a different primary scale, which are the parts of the rig a static two-monitor setup
does not provide. **P-2 (Narrator) was withdrawn** with the accessibility removal — see
[`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23).

| Task | State | Outcome and deviations |
|---|---|---|
| **T1** entry point, DPI, crash handler, `-municode` verdict | ✅ 2026-08-23 | `app/main.cpp` rewritten; `app/CrashHandler.{h,cpp}`; `-Wl,--build-id` on every target via `rawpage_common_flags`; `scripts/symbolize.ps1`. Both presets build clean, `ctest` **66/66 on `x64-release` and `x64-debug`**, `checks` clean at **29 files / 109 includes / 3 escapes**. Guide cases run: **A-03 ✅** (`--version` under `offscreen`, exit 0, no window), **A-04 ✅** (`msvcrt.dll` present, `ucrtbase.dll` absent), **A-05 ✅** (see the verdict below), **A-06 ✅** (`PerMonitorAwareV2`, set before the first Qt object), **A-07 ✅ with a correction** (see I-02), **A-08 ✅**, **A-65 ✅** (forced crash → dump → `symbolize.ps1` resolved `rawpage::app::CrashHandler::forceCrashForTesting()` at `app/CrashHandler.cpp:290`, with the build ids of dump and binary agreeing), **A-66 ✅** (the last-rites hook is invoked and its no-op state is written to the dump sidecar *and* logged at install). **Deviations:** (a) three `layer-lint` `filesystem-write` waivers were added in `app/CrashHandler.cpp` — the crash directory, the minidump, and the sidecar — each with a named reason and visible in `--explain`; this is the first use of the escape hatch in product code and takes the standing census from 0 to 3. (b) The `--print-args` diagnostic originally probed `_UNICODE` and reported `municode=yes` with `-municode` nowhere in the build: Qt6::Core defines `UNICODE`/`_UNICODE` in its INTERFACE compile definitions, so the probe was measuring Qt. It now reads `RAWPAGE_MUNICODE`, set in `cmake/RawPageWarnings.cmake` beside the link option it describes, so the diagnostic cannot drift from the flag. (c) The last-rites hook is deliberately **not** wrapped in `__try`/`__except` — GCC on Windows x64 does not implement SEH in C++, so the wrapper would have been a comment pretending to be a guard. |
| **T2** A3 ribbon spike | ✅ 2026-08-23 — **A3 verdict recorded: `go, with named custom work`** | `tools/spike-ribbon/` (throwaway, deleted at T10): a ribbon of plain `QWidget` composition plus **one custom `QLayout`**, with a `--self-test` mode. **Measured: R-2 ✅, R-4 ✅ (height), R-7 ✅, R-9 ✅ at p95 0.29 ms against a 16 ms budget.** **Walked and signed off by the user: M-01 ✅ and M-02 🟡** *(M-02 ✅ since its 2026-08-30 re-walk — see deviation (b))*, carrying R-1, R-3, R-4's flicker, R-5, R-6 and R-7. **R-8 was withdrawn** the same day when accessibility left the product, so the verdict rests on **eight criteria, not nine** — recorded plainly in `Tech-Stack-Plan.md` §3.3 rather than smoothed over. **Named custom work:** a collapse layout (~60 lines), a chevron overflow (~30), and a tab-order pass (~15). **Your M-01 walk produced I-05 and I-06:** `Tab` visited the ribbon groups in collapse-priority order rather than visual order, because collapse is implemented by re-parenting and re-parenting appends to the end of the focus chain — a durable constraint on T10, and retained as **keyboard operability** after the accessibility removal. **Deviations:** (a) the plan's 3-working-day timebox is a calendar budget that does not map onto how this was executed; the criteria, not the clock, bounded it. (b) M-02 was 🟡: its non-ribbon half names a sidebar, a floating pane and Preferences, none of which exist before T9/T14, so it was re-walked then — **done 2026-08-30, in full and with no findings, and M-02 is now ✅**. The deviation is kept rather than deleted because it is the record of a debt that was named at the time it was incurred and paid seven days later. |
| **T3** `ActionId.def`, `ActionRegistry`, `KeyContext` | ✅ 2026-08-23 | `core/cmd/{ActionId.def,ActionId.h,KeyContext.h,ActionRegistry.h,ActionRegistry.cpp}` and `tests/unit/tst_actionregistry.cpp`. **`ctest` 88/88 on both presets** (was 66); `checks` clean at **37 files / 171 includes**. All three §5.1 properties asserted: an unregistered id returns `NotFound`, a disabled one returns the **distinct** `Unavailable`, and a second registration is an error with the first handler surviving. **Two negative controls run, both inside the table region after the first pair mistakenly landed after the `#undef` and proved nothing:** a duplicate enumerator fails the build with *`ActionId.def:52: error: redeclaration of 'FileOpen'`* — the typo's own line, which is D1-6's whole claim — and a catalog key that disagrees with its string id fails `tst_actionregistry`. **Deviations:** (a) **327 ids declared, not the plan's ~200.** The id space was derived section by section from `Features.md` §1–§7 and §9 rather than estimated; under-declaring would be worse, since every string id is a permanent public name that owes an alias row if it ever changes. Guide case **A-25**'s "~200" should read ~327. (b) **`ErrorCode::Unavailable` was added to `core/Result.h`** — a Stage 0 vocabulary header. A-22 requires unregistered and disabled to be *distinct codes*, and none of the existing nine expressed "exists, but not applicable right now": `PermissionDenied` means a document or OS refusal, which a disabled action usually is not. (c) A test name containing `§` failed — see the deviation note below. |
| **T4** `KeyBindingStore` + §8 defaults + census | ✅ 2026-08-23 | `core/cmd/{Chord.h,Chord.cpp,KeyBindingDefaults.def,KeyBindingStore.h,KeyBindingStore.cpp}` and `tests/unit/tst_keybindings.cpp`. **`ctest` 110/110 on both presets** (was 88); `checks` clean at **42 files / 199 includes**. **All three census numbers assert separately and all three hold: 12 Write-mode (8 over Viewer + 4 over Global + 0 over Common), 6 class 1, 9 class 2** — computed from the live document before a line of C++ was written, and unchanged by the accessibility removal. `shadows()` reports class 1 only. **D1-9 verified by negative control in both directions:** adding a §8 row the store lacks fails with *"context Global, chord Ctrl+Alt+Y, documented as: Some new command nobody implemented"*, and deleting a row the store has fails with *"stored binding Global / Ctrl+Shift+O is not in the document"* — each naming the row, which is what A-30 demands so that "the store is wrong" and "the parser is wrong" can be told apart. **Deviations:** (a) **`Chord` is a first-party type, not `QKeySequence`.** This document's Stage 1 component list sketches `lookup(QKeySequence, KeyContext)`, but `QKeySequence` lives in **QtGui**, which `core/` may not include — `layer-lint` denies it and `core/` links only `Qt6::Core`. `ui/shell/ShortcutBinder` will convert at the boundary. (b) **A second binding kind, `WidgetNative`.** Fifteen §8 rows (`↑ ↓ ← →` in the item panes, `Tab` in the Forms contexts) move focus or selection inside the focused widget; giving them `ActionId`s would mint permanent public names for behaviours nothing dispatches. They are *recorded* rather than dropped, so "the store matches §8 row for row" stays true of every row and not just the convenient ones. Remapping **onto** such a chord is the one refusal the store makes. (c) **31 further `ActionId`s** were needed to cover §8 — presentation controls, form-fill verbs, find toggles — taking the declared total from 327 to **358**. |
| **T5** `SettingsSchema`, `Settings`, `RecentFiles` | ✅ 2026-08-23 | `core/settings/{SettingsKeys.def,SettingsSchema.h,SettingsSchema.cpp,Settings.h,Settings.cpp,RecentFiles.h,RecentFiles.cpp}`, `tests/unit/{tst_settings.cpp,tst_recentfiles.cpp}`, and four compile fixtures under `tests/unit/compile/`. **`ctest` 159/159 on both presets** (was 110); `checks` clean at 54 files / 265 includes, waiver census still **3**. All eleven scalar Stage 1 keys plus the `shortcuts/` family; D1-16 live in both directions. **A-41 is asserted three ways plus a control** (`tests/unit/RunCompileCase.cmake`): a misspelled key, a key named by string, and a right key with a wrong-typed value each fail to compile, and `declared-key.cpp` must still compile — without that control the case is equally satisfied by a header nobody can include. **Deviations:** (a) **`SettingsKeys.def`, an X-macro table, was added** — the plan's §4 manifest lists only `SettingsSchema.{h,cpp}`, but the constexpr `Key<T>` objects and the runtime descriptor table a Preferences page iterates are two lists that must agree forever, and D1-6's lesson is one table. (b) **Two keys beyond the plan's twelve are declared, both in a new `Internal` group**: `meta/schemaVersion` (which the guide already treats as internal state) and `documents/recentList`. The MRU list has to live somewhere, and "an undeclared key does not compile" is only true if there is no undeclared key — declaring it is what keeps the guarantee rather than what weakens it. (c) **`RecentFiles` is keyed on the path, not `DocumentId`** — that type arrives with `core/doc/` at Stage 2, and a recent list is a list of paths by nature. (d) **`changed` is a subscription list, not a `Signal<Key>`** — there is no `Signal` type in `core/` and inventing a core-wide one for a single user would be speculative infrastructure. (e) **Two QSettings dialect defects were found by running the tests, not by reading** — see I-08 and I-09; both are fixed, and each has a test that fails if the fix is removed. |
| **T6** `tools/i18n-lint` + catalogs + the third gate | ✅ 2026-08-23 | `tools/i18n-lint/{main.cpp,I18nLint.{h,cpp},I18nRules.{h,cpp},Catalog.{h,cpp},SourceScan.{h,cpp},rules.json}`, `tools/common/LineScan.h`, `i18n/{locales.json,rawpage_en.ts,rawpage_zh_TW.ts}`, and 14 gate cases over 10 fixture trees. **`checks` runs three gates and says three.** **`ctest` 173/173 on both presets** (was 159); the bare `gates` preset — no Qt, no vcpkg — is **53/53**, which is A-11. Guide cases run: **A-10 ✅**, **A-11 ✅**, **A-13 ✅** (five spellings in one tree, and the case asserts the *count*, so finding four of five would not pass), **A-14 ✅**, **A-15 ✅**, **A-16 ✅**, **A-17 ✅** (the applied waiver waives; a quoted marker, an unknown kind, and a marker on a codeless line each waive nothing), **A-70 ✅**, **A-71 ✅**. **A-72 is not yet runnable** — it needs `--lang` and a UI, which arrive at T8 and T9. **Deviations:** (a) **The catalog key is the `<source>` and `en` is a real catalog, not the identity.** Qt's convention makes the English sentence the key; `core/Result.h` forbids user-facing English in `core/` and `ActionId.def` already carries one `action.*` key per action, so there is no English in the sources for `lupdate` to extract. The consequence is that a missing key in `en` is a detectable defect rather than a button rendering `action.file.open`. (b) **The `unwrapped-literal` rule is sink-driven, not literal-driven.** "Every literal" would fire on object names, settings paths, catalog keys and format specifiers, and this stage's own risk register says a gate people waive everywhere is a gate that gets switched off. The sink list is data in `rules.json` and grows with `ui/`; the honest cost is a false negative for a literal handed to a user through a helper it does not name. (c) **`shell.*`, `ribbon.*` and `prefs.*` are NOT seeded.** The plan lists them as Stage 1 catalog groups; they arrive with the code that references them at T9 and T14, because adding them now would be a hundred `orphan-key` notes and a catalog nobody could review. The three `error.*` keys `core/settings/` actually references *are* present. (d) **28 of the 361 zh-TW entries are marked `type="unfinished"`** — the domain-heavy terms (Bates numbering, imposition, redaction, tag artifacts, rights management) where the Taiwanese convention is genuinely uncertain. `--explain` counts them, `missing-key` does not fail on them, and **M-06 is the review** (precondition P-3). Reporting them as finished would be claiming a review that has not happened. (e) **`tools/common/LineScan.h` was extracted** so `layer-lint` and `i18n-lint` share one copy of the code/comment split and the applied-versus-quoted waiver test — both of Stage 0's gate findings, I-15 and I-20, live in that logic, and a second gate re-deriving it would re-derive them. `layer-lint` keeps its own copy for now; the extraction is the shared home, not yet the single one. |
| **T7** `platform/platform.h` + Windows factory | ✅ 2026-08-23 | `platform/platform.h`, `platform/win/{PlatformFactory,WinSingleInstanceBus,WinShellIntegration}.{h,cpp}`, and a new `tests/platform/` target. **`ctest` 194/194 on both presets** (was 173); `checks` clean at **72 files / 363 includes** (the Stage 0 placeholder TU `platform/PlatformModule.cpp` was deleted, as its own comment instructed once the layer had real content); the bare `gates` preset 55/55. **All seventeen interfaces declared**, two implemented, and `unimplementedInterfaces()` names the other fifteen rather than leaving a caller to dereference null. **Deviations:** (a) **The interface count is seventeen, not the plan's fifteen** — see I-10. The figure was a count of §4.3's table ROWS, one of which declares two interfaces, and `IPrinting` had never been written down at all even though `platform/win/WinPrinting` has had a FileMap row since the map was written. `Tech-Stack-Plan.md` §4.3 owns the corrected figure and now carries the `IPrinting` row; `tests/platform` asserts the count against the header. (b) **A third test target, `tests/platform/`.** `tests/unit`'s `rawpage_assert_headless()` forbids linking `rawpage_platform`, deliberately — the first platform test is exactly the pressure D1-4 met from `ui/` by adding a target rather than weakening the existing one. It is Windows-only and never runs on `portable-asan`. The handoff round trip runs **single-process**, which is a real handoff: `CreateMutexW` returns `ERROR_ALREADY_EXISTS` for a name the same process holds, so the second bus takes the genuine secondary branch. What it cannot cover is cross-process foreground activation — that stays manual case M-07. (c) **`platform.h`'s freedom from OS headers is now a gate rule, not a convention** — a `layer-lint` layer entry for that one file, with a fixture proving it fires and a control proving `platform/win/` may still include the OS. A header comment saying "no Win32 here" is review discipline; this is the gate. (d) **The pipe name carries the session id**, because `Local\` scopes mutexes and NOT pipes — the two halves of D1-11 look symmetric and are not. (e) **The fourth `filesystem-write` waiver**, on the named-pipe `CreateFileW` in the bus, taking the standing census from 3 to 4. Waived rather than narrowing the pattern: `CreateFileW` really is how a file gets written on Windows. (f) **A real race was found and fixed by the tests** — see I-11. **A-60, A-61 and M-07 remain outstanding**: they are end-to-end through `rawpage.exe`, which needs T8. |
| **T8** `app/` composition root | ✅ 2026-08-23 — **I-04 closed** | `app/{Services.h,CommandLine,CompositionRoot,Application,SingleInstance}.{h,cpp}`, `app/main.cpp` rewritten to flow A, `i18n/CMakeLists.txt` (the `.ts` → `.qm` step), `tests/unit/tst_commandline.cpp`, and two negative-compile fixtures. **`ctest` 226/226 on both presets** (was 194); the bare `gates` preset **59/59** (was 55); `checks` clean at **100 files / 509 includes**, i18n-lint **45 files / 2 catalogs / 0 waivers**. Flow A verified end to end against the real executable: a second launch forwarded `測試-📄.pdf` to the first over the named pipe and the primary logged the path **byte for byte**, which is I-04's rule proven across three process boundaries rather than asserted. Guide cases run: **A-60 ✅**, **A-63 ✅**, **A-64 ✅**, **A-72 ✅** (380 strings dumped per locale, 377 differ, **0 render a raw key**; the 3 identical are the product name twice and the em dash). **I-04 is closed by three mechanisms, not one:** there is no `argc/argv` overload to call and a compile fixture proves it; `tools/layer-lint` gained a **non-escapable `argv-path-read` rule** allowed only in `app/main.cpp`, with a fixture and an entry-point control; and `tst_commandline` asserts an emoji path survives the parser byte for byte. **Deviations:** (a) **`SingleInstance::claim()` runs AFTER `CompositionRoot::build()`, not before it as flow A draws.** The claim is governed by `general/singleInstance`, so `Settings` has to exist first. A secondary therefore constructs `Settings` before exiting, which reads the file and never writes it. (b) **Two `layer-lint` gaps were found by needing them** — see I-15 and I-16. `ui/` had no permitted path to `platform/platform.h`, whose own header comment has always said it is included by `core/`, `ui/` and `app/`; and no gate scanned `.def` files, though four X-macro tables are `#include`d straight into layers that have deny lists. Both fixed with fixtures and controls. (c) **`app/Services.h` is now `ui/`'s second permitted look outward**, with its own layer entry constraining it to `core/` plus `platform/platform.h` — the `platform.h` treatment, for the reason that a `pdf/` include added there at Stage 2 would otherwise reach every widget. (d) **Two real defects found by running the flow** rather than by reading it: settings were never written to disk (I-14), and they were being written to `%APPDATA%\RawPage\RawPage\` (I-13). (e) The `.qm` catalogs are built beside the executable rather than into a Qt resource, so Stage 15 can ship them replaceable; `LinguistTools` is used as a build tool and deliberately not registered with `rawpage_link_qt`, because `qt-modules.txt` is the LINK closure `license-audit` audits. |
| **T9** `MainWindow`, `StatusBar`, `SidebarDock`, `ThemeManager` | ✅ 2026-08-23 | `ui/shell/{Catalog,ThemeManager,StatusBar,SidebarDock,MainWindow}` plus `ui/shell/ShellKeys.def`, 22 new `shell.*` catalog entries in both shipped locales, and a new `tests/ui/` target. **`rawpage.exe` opens a window.** `ctest` 226/226 on both presets; `checks` clean; **`--dump-ui-strings` under `offscreen` composes every service, loads both catalogs and exits 0 without a window**, which is what keeps a headless smoke test meaningful now that an ordinary launch runs an event loop. **Deviations:** (a) **`tests/ui/` arrives here rather than at the plan's T15**, on the precedent T7 set with `tests/platform`: four widget files shipping with no suite until six tasks later would make "code complete AND `ctest` green" vacuous for all six. T15 still owns adding it to the CI `windows` job. (b) **Auto-hide is first-party.** §4.3 lists docking among what Qt gives us and is right — dock, float and re-dock are `QDockWidget` and are not reimplemented — but Qt has no notion of a pane collapsing to an edge strip, so that part is ours. It is a second piece of named custom work in the same family as A3's collapse layout. (c) **A crash found by `tests/ui` on its first run** (I-12), of exactly the shape T7's pipe race had: a lambda bound to a child's signal outliving the derived object. Fixed, and **verified by negative control** — with the `disconnect` commented out, that one case segfaults and no other. (d) **Chord input is not wired**, by plan: `ShortcutBinder` is T13. The three registered actions are reachable through the registry — and therefore through a test and the batch runner — and through the pane chrome, but not yet from the keyboard. (e) **Three actions registered, not the plan's 60–80.** That figure is Stage 1's total across T9–T14; registering a stub handler for an id this window cannot service would make every one of them look available, which is precisely the state §5.1 says to avoid. |
| **T10** `Ribbon`, `RibbonTabBuilder`, `ActionFactory`, `res/ribbon.json` | ✅ 2026-08-24 | `ui/shell/{RibbonModel,RawPageAction,ActionFactory,Ribbon,RibbonTabBuilder}`, `res/ribbon.json` with `res/CMakeLists.txt`, the new `tools/ribbon-validate/`, `layer-lint`'s non-escapable `bare-qaction` rule with **six** gate cases, `tests/ui/{tst_ribbonbuild,tst_actionwiring}.cpp`, 61 new `ribbon.*` catalog entries in both shipped locales, and **`tools/spike-ribbon/` deleted** exactly as D1-1 said it would be. `ctest` **250/250** on both presets (was 226); the bare `gates` preset **65/65** (was 59); `checks` clean at **109 files / 569 includes**, i18n-lint **55 files / 0 waivers**. The ribbon is 9 tabs, 49 groups and **192 controls**, all of it from the resource: `RibbonTabBuilder` makes no decision the JSON could have expressed, and `tst_ribbonbuild` asserts both directions — everything declared is present **and no control exists that was not declared**, which is the half that actually protects D1-8. Guide cases run: **A-12 ✅**, **A-50 ✅**, **A-51 ✅**, **A-54 ✅**. A-51 was verified by planting `"file.saevAs"` and building: the build stops with `res/ribbon.json:44: unknown action id` and the resource is **not** copied, so no stale valid file can mask it. **Deviations:** (a) **`ui/shell/RibbonModel.{h,cpp}` is a file the plan's §4 manifest does not list.** D1-8 needs the resource validated before the link and built at startup, which is two consumers of one schema — and a validator with its own parser can certify a file the runtime reads differently, which is worse than no validator. So the parser is widget-free and `tools/ribbon-validate` **compiles that translation unit in**, the technique `tests/unit` already uses for `app/CommandLine.cpp`. (b) **`tools/ribbon-validate/` is a new tool and is deliberately NOT a fourth gate.** `checks` runs three and says three; this is a build step `rawpage_ui` depends on. Wiring it into `checks` would have been the smaller CMake diff, would have made the gate count a lie, and would have put a Qt dependency into the job whose entire value is not having one. (c) **No icons, and no `res/rawpage.qrc`.** The manifest lists both at T10. There are no icon assets and inventing 358 of them is not work this task can do honestly, so a large button is a tall text button and `ActionRegistry`'s `iconName` field stays unread. Recorded rather than faked. (d) **`res/ribbon.json` ships beside the executable rather than in a Qt resource**, the same call T6 made for the `.qm` catalogs and for the same reason — Stage 15 may ship it replaceable. The build-time validation is what makes that safe, and the runtime parse is the same parser, so an edited copy fails with the same diagnostics rather than yielding a quietly thinner ribbon. (e) **One extra construction site inside the allowed file.** A `QMenu`'s items are `QAction`s and Qt gives no choice; the overflow chevron needs some. `ActionFactory::addNonCommandMenuItem` builds them where every other action is built and stamps them, rather than carving `Ribbon.cpp` out of the lint — the rule keeps exactly one allowed path and stays non-escapable. (f) **A fourth registered action**, `view.toggleRibbon`, so R-7's behaviour is reachable from the ribbon, from a test and from the Stage 14 batch runner before T13 wires `F9`. (g) **Three defects found by the new suite's first run against a real, shown window** — I-18, I-19 and I-20 — and **two of the three were the check being wrong rather than the product**. I-19 is the one that mattered: the ribbon was holding the whole main window open at 1044 px, and behind that, the chevron overflow the A3 verdict counts as named custom work was **unreachable in the shipped configuration** while its code looked correct. |
| **T11** `QuickAccessToolbar`, `UiCustomization`, `ToolFinder` | ✅ 2026-08-24 — **I-19 closed** | `ui/shell/{UiCustomization,QuickAccessToolbar,ToolFinder}.{h,cpp}`, two new settings keys (`ui/qatCommands`, `ui/qatBelowRibbon`), 6 new `shell.*` keys and one `error.*` key in both shipped locales, and `tests/ui/{tst_qat,tst_toolfinder}.cpp`. `ctest` **264/264** on both presets (was 250); the bare `gates` preset unchanged at 65/65; `checks` clean at **117 files / 659 includes**, i18n-lint 61 files / 0 waivers. Guide cases run: **A-55 ✅**, **A-56 ✅**. **I-19's open half is paid rather than carried again:** the ribbon body sits in a `QScrollArea`, so `RibbonPageLayout::minimumSize()` returns the honest content minimum — the sum of the collapsed groups — without that number reaching the main window. Collapse is still exhausted first; the row scrolls only below the width every group collapsed needs. **Deviations:** (a) **The QAT and the tool finder are hosted by a header COLUMN in the menu-widget slot**, not by the ribbon. The toolbar has to be above or below the ribbon and inside the one slot `QMainWindow` puts above the dock areas, or a docked pane can land between them; `Ribbon` gained only a corner-widget slot and a `controlContextMenuRequested` signal, and knows nothing about either. (b) **`ui.qatAdd` and `ui.qatRemove` are the product's first parameterized commands, and `ActionRegistry::dispatch` has no argument.** The Stage 1 answer is `QuickAccessToolbar::pendingTarget()` — the surface that raised the menu records what was under the cursor and the handler reads it, which keeps both reachable from a test and from the batch runner. It is not a general answer; recorded as **O11** in §8. (c) **`ActionFactory` had never called `setCheckable`**, so its header's promise that "a toggle is checkable and only a toggle is" had gone untested for want of a toggle. The QAT's placement item is the first one; fixed, and `ActionRegistry` gained `isToggle()` and `tooltipKey()` because the factory needed both (**I-21**). (d) **`ActionFactory::made_` became `QPointer`s.** The toolbar rebuilds its buttons on every add, remove and reorder and the actions are parented to the buttons, so entries in that list are destroyed as a matter of routine — raw pointers made `refresh()` a use-after-free waiting for the first reorder. (e) **`Settings::isSet()` was added**: a toolbar the user emptied and a profile that never customized one both read back as an empty list, and only one of them means "restore the factory defaults". (f) **Two catalog additions were needed for one command**, not one: `ui.qatPlacement`'s label is a noun and cannot say which way the toggle points, so the check mark says it and `shell.qat.placementHint` says what the check mark means. (g) **A third kind of Qt-manufactured action** was found by the wiring self-check on its first run against the tool finder: `QLineEdit::setClearButtonEnabled(true)` builds one, and unlike `QMenu::menuAction()` it is NOT in `QWidget::actions()`, so it cannot be recognised by identity. `ActionFactory::adoptWidgetActions()` lets the widget's owner stamp it at the moment the property is turned on (**I-22**). |
| **T12** `DocumentTabBar`, stub `DocumentWindow` | ✅ 2026-08-24 — **A-58 closed** | `ui/shell/{DocumentWindow,DocumentTabBar}.{h,cpp}`, `MainWindow`'s central column rewritten around a tab strip and a two-page body, 6 more `shell.*` keys in both locales, and `tests/ui/tst_documenttabs.cpp`. `ctest` **275/275** on both presets (was 264); the bare `gates` preset 65/65; `checks` clean at **122 files / 697 includes**, i18n-lint 65 files / 0 waivers. Guide cases run: **A-57 ✅**, **A-58 ✅** (its second half was not runnable before this task). **Seventeen commands are now registered**, up from nine: `file.open` (a dialog), `file.close`, `file.closeAll`, `window.{nextTab,prevTab,next,prev,new}`. **Deviations:** (a) **The command handlers stopped capturing `this`, and registration became once-per-registry.** Tearing a tab out makes a second `MainWindow` while `ActionRegistry` stays one shared object whose registration is deliberately not idempotent — so a second window would have collected one duplicate-registration error per command, and the first window's handlers would have gone on acting for every window in the application, including after that window was destroyed. Each handler now resolves `MainWindow::activeShell()` at dispatch time. This is a structural change T12 forced rather than a refactor T12 chose. (b) **`raiseToForeground()` sets the active shell directly** rather than waiting for the `ActivationChange` that `activateWindow()` normally causes: measured under `offscreen`, that event never arrives, and a Global command dispatched into the window nobody had raised. (c) **`file.open` is registered with an enablement predicate that is false where there is no user.** A modal file picker under `offscreen` is shown to nobody and never returns — a ten-minute CI hang the first time a test dispatched it (**I-23**). As a disabled command it is refused with `Unavailable`, which a headless caller can tell apart from `NotFound`; the parameterized form of "open this file" is `openFiles()`, which flow B and the CLI already use. (d) **The document strip uses `QTabBar`'s own overflow** where the ribbon replaced it with a chevron: a ribbon has a small fixed tab set, a document strip has an unbounded user-driven one, and copying the ribbon here would be consistency with the wrong thing. (e) **`DocumentWindow` holds a path and nothing else**, and A-57's "no member Stage 2 would have to remove" is a claim about the header — the test asserts what it can (the meta-object gains no property or method, and everything reported is derived from the path) and says so rather than implying more. **(f) Two defects shipped in this task and were found six days later by the first person to walk M-03 — `I-33` and `I-34`, fixed 2026-08-30.** Recorded against T12 rather than against the fix date, because that is where they were introduced. I-34 is the instructive one: the drop-onto-another-window branch was correct, and `tst_documenttabs.cpp` asserted the destination by calling `adoptDocument` directly while a comment recorded the gesture as manual-only — so a branch no test and no person had ever driven read as covered. It could not be reached by hand at all. The lesson is narrower than *"test the gestures"*: **a case that reaches a behaviour through a back door should say what it therefore does not cover**, because the next reader counts it as coverage. |
| **T13** `ShortcutBinder` + full chord wiring | ✅ 2026-08-24 — **A-53 closed** | `ui/shell/ShortcutBinder.{h,cpp}` and `tests/ui/tst_context.cpp`. `ctest` **297/297** on both presets (was 275); the bare `gates` preset 65/65; `checks` clean at **125 files / 735 includes**, i18n-lint 67 files / 0 waivers. **No new catalog keys and no new settings keys** — T13 adds a way to reach commands, not commands. Guide case **A-53 ✅**, and the T13 exit criterion is asserted over the table rather than over a handful of chords: every dispatched row in §8's Global table is registered with a recording handler, pressed, and checked to have reached its own action; a second sweep does the same for every context's own rows, which is the resolution ORDER asserted rather than described. **Deviations, and each is a decision the plan left open rather than a departure from it:** (a) **Chords are not `QAction` shortcuts, and that is the whole design.** Qt's shortcut system has no way to say *innermost context, then `Common`, then `Global`* — the order `KeyBindingStore`'s header calls the model — and it would have put the user's remap into 358 actions as a shadow copy of the store. So the binder filters key events and resolves through the store, which is also what makes a remap take effect with no widget rebuilt (asserted). (b) **The focused widget gets first refusal through Qt's own `ShortcutOverride` handshake** rather than through a list of widgets that edit text. `Ctrl+A` is `edit.selectAll` in §8's Common table AND what a `QLineEdit` does with its own text; a list would be wrong the first time somebody added a widget, and Qt already asks this question of every widget in its own shortcut path. (c) **Only two of five outcomes consume the key.** A dispatch and an `Unavailable` refusal consume; a `NotFound` does not, because Stage 1 registers seventeen of 358 ids and eating the other chords would make the shell swallow `Ctrl+S` for twelve stages. (d) **One binder per window, installed on `qApp` and answering only for its own window.** A key event reaches the focus widget first, so a filter on the window would never see a chord typed into a text field; but two windows filtering the application would dispatch one chord twice, and the fix is the window test rather than a single global binder, because the context must be computed per window. (e) **Nothing in the Stage 1 shell declares a `KeyContext`.** The Viewer surface is Stage 2, the panes Stage 4, Write mode Stage 13, so `activeContext()` answers `Global` throughout — the honest answer, and the one the T13 exit criterion is written against. Declaring `Viewer` on D1-2's stub to make the mechanism look exercised would have been a stub claiming to be a page surface. (f) **`unreachableBindings()` is a census the plan did not ask for**, and it is the case that could have found a dead table: a chord whose key the conversion cannot name looks identical to a working one everywhere else — the store holds it, the census counts it, the tool finder prints it, and only the keyboard disagrees. All 248 rows round-trip. (g) **A second self-check, in the shape of D1-7(b).** An action carrying its own key sequence, or a free-standing `QShortcut`, is a chord that bypassed the context stack and the remap on its way in; `MainWindow::showEvent` now runs that check beside the wiring one, asserting in debug and logging in release, and both kinds are planted in the suite. |
| **T14** `LogPane`, `PreferencesDialog` (shell + General + Shortcuts) | ✅ 2026-08-25 — **I-26, I-27, I-28** | `ui/dialogs/{PreferencesDialog,SettingsPage,ShortcutsPage}.{h,cpp}`, `ui/panes/LogPane.{h,cpp}`, `core/LogBuffer.{h,cpp}`, `app/LogFile.{h,cpp}`, 62 new `prefs.*` catalog entries in both shipped locales, and `tests/{unit/tst_logbuffer,ui/tst_preferences,ui/tst_logpane}.cpp`. `ctest` **328/328** on both presets (was 297); the bare `gates` preset **66/66** (was 65); `checks` clean at **140 files / 911 includes**, i18n-lint 79 files / 2 catalogs / 0 waivers, **6** live filesystem-write waivers (was 4). Guide cases run: **A-90 ✅**, **A-91 ✅**, **A-92 ✅**, **A-93 ✅**, **A-94 ✅** — a new §5.10 in the guide. `--dump-ui-strings` now emits **515** keys per locale (was 380), **0** of which render as their own key, and 510 differ between `en` and `zh-TW`. **Nineteen of 358 commands are registered** (was seventeen): `file.preferences` and `pane.log`. **Deviations, and each is a decision the plan left open rather than a departure from it:** **(a) The pages are generated by `#include`ing `SettingsKeys.def` into `SettingsPage.cpp`, not by walking `SettingsSchema::allKeys()`.** The runtime descriptor table would have been the obvious iteration and it cannot be: `core::Settings` has no string-keyed `get`/`set` by D1-10, and adding one for the Preferences dialog's convenience would make *“an undeclared key does not compile”* decorative for the whole product. So each row instantiates a template holding the real `constexpr Key<T>`. `SettingsSchema` still owns *which widget* and *what to call it*, through three artifacts added here — an editor kind, a choice set, and a label key derived from the path — all derived from the row's type and path rather than added as columns anybody maintains. **(b) A third page ships, Logging, and it reconciles two things this plan says.** The T14 row names two pages; §11 says *“three pages here”*. Logging is the third because T14 also ships the log pane, `logging/level` and `logging/subsystems` decide what that pane can ever show, and `logging/retainFiles` had been a declared key with no surface since T5. A generated page costs a row in `shippedPages()`. **(c) The dialog is MODELESS and applies immediately — Close and Restore Defaults, no OK/Cancel/Apply.** A `Cancel` that must un-apply needs a transaction `core::Settings` deliberately does not have (it loads once and writes on `sync()`, because D1-16's downgrade rule is not expressible through a live `QSettings`), and a `Cancel` that only stopped future writes while the theme already changed is a button that lies. Modeless is also what makes `file.preferences` need **no** enablement guard — a better answer than I-23's `canPromptTheUser`, available here only because the dialog had no reason to be modal. **(d) `core/LogBuffer` and `app/LogFile` are two files the §4 manifest does not list.** `core/Log.h` has ONE sink and `setLogSink` **replaces** it, so a pane that chained it would be dropped the next time `applyLoggingSettings()` ran — which is what T14's own Logging page does. `CompositionRoot` now installs one sink that **fans out** to the console, the file and the buffer. The file is `app/`'s because `core/` may not write to disk, and `main.cpp` has promised it to T14 since T1. **(e) `log.openFolder` is declared and NOT registered.** The folder belongs to `app/LogFile` and `ui/` looks outward to exactly two files; including a third to open a directory would widen that list for a convenience, and widening it is an architectural decision rather than a diff (I-15). **(f) Three defects, and all three were found by running something new against the product** — **I-26** (a remap *added* a chord instead of replacing it, found by the first caller that ever remapped for a user), **I-27** (`orphan-key` about to report forty generated keys nobody could act on) and **I-28** (a log file that could not name the binary that wrote it, found by reading the file the executable actually wrote). **(g) Convention 2 caught a new test name in this session**: a case named with a `§` registered with CTest and then matched nothing, exactly as I-07 says. Renamed; no new issue, because the convention did its job. A second self-inflicted one is recorded in A-92's result: a case asserted the shadow census as **6** and got **7**, because six is a count of *chords* and seven of *entries* — the same conflation as I-01, now asserted both ways. |
| **T15** test target, CI, verdicts, doc lockstep | ✅ 2026-08-25 — **I-29, I-30**; **A-80 closed, and every automated case with it** | `tests/bench/` (new, one benchmark), `tests/unit/tst_singleinstance.cpp`, `scripts/check-single-instance.ps1`, four `.github/workflows/ci.yml` changes, and the §4 manifest's last `Architecture-FileMap.md` rows. `ctest` **334/334** on both presets (was 328); the bare `gates` preset 66/66; `checks` clean at **141 files / 930 includes**, i18n-lint 78 files / 2 catalogs / 0 waivers. **No new catalog keys, no new settings keys and no new registered commands** — T15 builds no product surface, and that is what the task is. Guide cases run: **A-61 ✅**, **A-80 ✅**, **A-81 ✅**, **A-82 ✅**, **A-83 ✅**, **A-84 ✅**, **A-85 ✅** — **every automated case in the guide**. **CI is green on all four jobs** — run 32785493072, and **the `windows` job ran for the first time in the project's history**: 334/334 there, `gates` 52/52, `portable-asan` 196/196. A-85's figure on the runner (TOTAL p95 **0.476 ms**) is within 1 % of this machine's 0.471 ms, which is what a stable measurement across two boxes looks like. **`core/CoreModule.cpp` deleted**, as its own comment instructed and as §10 had carried since T7. **Deviations, and each is a decision the plan left open rather than a departure from it:** **(a) `tests/bench/` arrives a stage early, and A-85 is why.** `Architecture-FileMap.md` schedules the directory for Stage 2's per-tile render latency (risk A2), but R-9 — a tab switch inside one 60 Hz frame — is the only one of A3's eight criteria that is a number rather than a judgement, and `Stage1-AppShell-Plan.md` §5.3 names `tests/bench` as how it is judged. Stage 1 cannot record its A3 verdict complete without it. A bench target's exit code is deliberately about whether the measurement was *possible*, never about whether the machine was fast: a loaded runner must not turn a green build red for a reason unrelated to the change. **The figure is p95 = 0.471 ms against 16.667 ms**, reported in three phases because one opaque total cannot be acted on. **(b) A-61 is split, and the split was forced by a measurement.** Qt resolves `QStandardPaths::AppDataLocation` through `SHGetKnownFolderPath`, so overriding `APPDATA` for a child process does **not** redirect the settings file — verified rather than assumed — and `main.cpp` exposes no switch for `CompositionRoot::Options::settingsPath`. Adding a CLI verb at T15 would extend the vocabulary `Features.md` §9.6 owns for Stage 14. So the *policy* half is a CTest case and the *two-process* half is `scripts/check-single-instance.ps1`, which edits the real settings file and restores it in a `finally` — precisely why it must not be a suite anybody runs casually. Both halves ship with a control, because a `false` case with no `true` case passes against a build whose single-instance code was deleted outright. **(c) `app/SingleInstance.cpp` became the third `app/` translation unit compiled into the headless suite**, not linked. It names `platform/platform.h`, and the tempting repair was one `target_link_libraries` line — which is the exact pressure D1-4 exists to resist, applied for real rather than described. `platform.h` is an interface header that includes no OS header (its own `layer-lint` entry), so `rawpage_assert_headless()` still resolves to `core/` + `pdf/` and `portable-asan` runs the new cases where neither forbidden library exists. **(d) "`tests/ui/` into the CI `windows` job" turned out to be an assertion rather than a step.** `ctest --preset x64-release` already ran the widget suite; what no job could do was notice its **absence** — a target that failed to build subtracts cases from a run that still reports *"100% tests passed"*. All three jobs now count what they registered, in both directions. The first version of the absence check returned 0 against a listing containing 124 such cases and would have passed on a Linux runner with the whole suite leaked onto it; it was shown failing against real positive input before it was trusted (convention 7). **(e) Two findings, and both are the harness and its infrastructure rather than the product** — which is now the fourth consecutive task where that is true. **I-29**: every figure the new benchmark printed on its first run was 0.000 ms, because three percentile calls were arguments to one `printf` and argument evaluation order is unspecified. Caught by disbelieving the result, not by anything failing. **I-30** is the S2: **the `windows` CI job had never run, and it was never a missing runner.** One has been online since 2026-08-17. The repository variable was `RECTO_HAS_WINDOWS_RUNNER`; the workflow reads `RAWPAGE_HAS_WINDOWS_RUNNER`. Two artefacts in different systems had to agree and nothing checked that they did, so for a week three-quarters of the product's test surface had no CI while the run page said nothing was wrong. The variable was set with the user's authorization and the next push produced the first `windows` run the project has ever had — green, at 334/334. The structural fix is a fourth job, `coverage-report`, which states in every run summary which jobs ran and — when the Windows one did not — enumerates exactly what went unexercised. It does not fail, because a job that goes red for missing infrastructure teaches everyone to ignore a red build. |

> **A convention T9 adds to it: a Catch2 test name may not contain a comma either.** Catch2 splits a filter on commas, so a case named *"…round trip, and peeking…"* cannot be selected by hand even though CTest escapes it correctly when it runs the suite. The same family as I-07 and I-04 — a name crossing a narrow, punctuation-sensitive process boundary — and the same remedy: keep the name plain and put the detail in a comment.
>
> **A convention T3 established the hard way: Catch2 test names are ASCII.** The `KeyContext` case was
> first named *"…matches Features.md §8"* and failed — not in its body, but because
> `catch_discover_tests` registers the name with CTest, CTest passes it back as a command-line filter,
> and `§` does not survive that round trip (it returned as `Features.md ??8`). Catch2 then matched no
> test and CTest reported a failure that looked like a broken assertion. This is **I-04's root cause in
> a second place** — a narrow, code-page-bound process boundary — and the machine's ACP of 950 is again
> what made it visible. Section references belong in comments, not in test names.

#### Verdict: `-municode` — **not adopted** (resolves Stage 0 deviation (a), decision **D1-3**)

Measured 2026-08-23 with `rawpage.exe --print-args`, from PowerShell **and** `cmd.exe`, against a path
containing CJK characters and one containing an emoji. The machine's **system ANSI code page is 950**
(Big5), which is what makes the result decisive rather than vacuous — a UTF-8 ACP would have hidden it:

| List | CJK path | Emoji path |
|---|---|---|
| `argv` (narrow, from the CRT) | survives — Big5 can represent it | **`report-?????.pdf`** — lost, lossily and irreversibly |
| `QCoreApplication::arguments()` | byte-for-byte correct | **byte-for-byte correct** |

Both shells give the same answer. Qt sources `arguments()` from `GetCommandLineW()`, not from `argv`,
so the problem `-municode` exists to solve is already solved for our only consumer, and the flag is
**not adopted** — no `wWinMain`, no `Qt6::EntryPointPrivate`, a plain `int main(int, char**)`.

**The standing rule this creates, which is the real deliverable:** *nothing in RawPage may read a path
out of `argv`.* `main()` hands `argv` to `QApplication` (which ignores it for `arguments()`) and reads
nothing else from it. `app/CommandLine` at **T8** must parse `QCoreApplication::arguments()` and
nothing else — the emoji row above is what a violation costs, and it is silent.

#### Verdict: **A3** — ✅ `go, with named custom work`

**Recorded 2026-08-23.** Qt Widgets host a real ribbon. The middle rung of the
`Stage1-AppShell-Plan.md` §5.3 scale is a **pass**, and the fallback — a first-party ribbon widget —
is not in play. The named work is a collapse `QLayout` (~60 lines), a chevron tab overflow (~30), and
a tab-order pass (~15); all three are ordinary Qt.

**Stated plainly because it would be easy to hide:** the verdict rests on **eight of the nine
criteria**. R-8 was **withdrawn, not cleared**, when accessibility was removed from the product the
same day — so the ninth rung was deleted rather than met. Full per-criterion table in
[`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified).

**Objectives.** A window with a working ribbon, document tabs, a sidebar, and a status bar, where
every command already flows through the action layer, one typed settings store exists, the UI is
translatable, and a second launch hands its file to the first instance.

**Components & responsibilities.**
- `app/main.cpp` — DPI awareness before any window; crash handler; CLI parse; hand off.
- `app/CompositionRoot` — `Services build(Settings&, Platform&, IDocumentBackend&)`.
- `app/Application` — `void loadTranslators(const QLocale&)`, `void restoreSession()`.
- `app/CommandLine` — `Result<Intent> parse(QStringList)` (open-with-file only this stage).
- `app/CrashHandler` — `void install()`, `void writeMinidump(EXCEPTION_POINTERS*)`.
- `app/SingleInstance` — `bool claim()`, `void forwardTo(primary, QStringList files)`.
- `core/cmd/ActionId.h`, `core/cmd/ActionRegistry` — `void register(ActionId, Handler, Enable, Label)`,
  `bool dispatch(ActionId, KeyContext)`, `bool isEnabled(ActionId) const`.
- `core/cmd/KeyContext.h`, `core/cmd/KeyBindingStore` —
  `std::optional<ActionId> lookup(const Chord&, KeyContext) const`,
  `Result<void> rebind(const Chord&, KeyContext, ActionId)`, `std::vector<Shadow> shadows() const`.
  **Corrected at T4:** this list originally said `QKeySequence`, which lives in QtGui and `core/`
  may not include. `core/cmd/Chord` is the first-party equivalent; `ui/shell/ShortcutBinder`
  converts at the boundary.
- `core/settings/Settings`, `SettingsSchema` — `template<class T> T get(Key) const`,
  `void set(Key, T)`, `Signal<Key> changed`.
- `core/settings/RecentFiles` — `void note(DocumentId)`, `void pin(DocumentId)`, `void clear()`.
- `ui/shell/MainWindow`, `Ribbon`, `RibbonTabBuilder` (`void build(Ribbon&, const QJsonDocument&, ActionRegistry&)`),
  `QuickAccessToolbar`, `UiCustomization`, `ShortcutBinder` (`KeyContext activeContext() const`),
  `DocumentTabBar`, `DocumentWindow`, `SidebarDock`, `StatusBar`, `ToolFinder`, `ThemeManager`.
- `ui/panes/LogPane`, `ui/dialogs/PreferencesDialog` (shell + General + Shortcuts pages).
- `platform/platform.h` (all **seventeen** interfaces **declared** — the count was written as
  fifteen and corrected by measurement at T7), `platform/win/PlatformFactory`,
  `WinSingleInstanceBus`, `WinShellIntegration` (file association only).
- `tools/i18n-lint/`, `i18n/rawpage_en.ts`, `rawpage_zh_TW.ts`.

**Key bindings handled** — from [`Features.md` §8 → Global](./Features.md#global-active-in-every-context)
(`KeyContext::Global`): `Ctrl+O`, `Ctrl+W`, `Ctrl+Q`, `Ctrl+Tab` / `Ctrl+Shift+Tab`,
`Ctrl+F6` / `Ctrl+Shift+F6`, `F4`, `F9`, `Alt` (key tips), `Ctrl+Shift+U`, `Ctrl+K`, `F1`.
Also `Ctrl+Z` / `Ctrl+Y` from [Common](./Features.md#common-all-contexts-unless-a-context-above-overrides-the-chord)
— **registered and dispatching, but with an empty journal until Stage 3.**

**Data / settings keys.** `general/theme`, `general/uiFontScale`, `general/touchMode`,
`general/restoreSession`, `general/singleInstance`, `language/uiLocale`,
`shortcuts/<context>/<action>`, `logging/level`, `logging/subsystems`, `logging/retainFiles`,
`documents/recentCount`, `documents/recentPinned`.
**Four more are declared than this list names, and each is recorded where it was added:** `meta/schemaVersion`
and `documents/recentList` at T5 (deviation (b) — internal state, not preferences), and `ui/qatCommands`
and `ui/qatBelowRibbon` at T11. All four are in the `Internal` group and get no Preferences row: they
are state and customization rather than settings a user goes looking for in a dialog.

**Cross-cutting additions.** No commands yet. **i18n catalog groups introduced:** `shell.*`,
`ribbon.*`, `error.*`, `prefs.*`, `action.*` (one label per `ActionId`). **Live at T14: 537 keys per
shipped locale**, of which 62 are `prefs.*`. Three of those families are **generated rather than
written down** — `prefs.setting.<group>.<key>` from `SettingsKeys.def`, `prefs.choice.<enum>.<member>`
from the enum's own name table, and `prefs.context.<Name>` from `KeyContext` — so the whole key
appears in no source and only its namespace does. `tools/i18n-lint` learned to read a reference
ending in a dot as a namespace at T14 (**I-27**), which is what keeps forty unactionable `orphan-key`
notes off every run, and `Catalog::dumpStrings()` walks those tables so A-72 still covers all of them.

**Algorithms & notes.**
- `ShortcutBinder::activeContext()` is the keystone of the whole binding model: it walks the focus
  chain, takes the innermost widget that declares a context, and falls back to `Global`. It is a
  **pure function of the focus chain** and is unit-tested with synthetic chains — no window needed.
- `RibbonTabBuilder` reading a declarative resource means adding a command in a later stage is a
  registry entry plus a JSON line, never widget code. This is what keeps sixteen more stages from
  each growing their own toolbar plumbing.
- `KeyBindingStore` **reports** rather than forbids a remap that shadows a Global chord in one
  context, because [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference) already
  ships twelve deliberate collisions — forbidding shadowing would make the specified defaults
  illegal.

**Testing.**
*Automated:* `tst_actionregistry` (register, dispatch, enablement, unknown id);
`tst_keybindings` (§8 defaults load exactly; the twelve documented collisions each resolve to the
right `ActionId` per context; remap and persist; shadow detection);
`tst_settings` (typed round-trip, defaults, undeclared-key rejection, `changed` signal);
`tst_context` (`activeContext()` over synthetic focus chains); `tst_recentfiles`;
`tst_commandline`. `i18n-lint` green with `zh-TW` complete.
*Manual:* → queued **`Stage1-AppShell-Testing-Guide.md`**, written before this stage's code (§5). In brief:
ribbon renders and collapses; a QAT customization survives restart; tear a tab out and re-dock it;
panes dock, float, and auto-hide; the theme follows the system; `--lang zh-TW` translates the UI;
double-clicking a second PDF reuses the running instance; the tool finder finds and runs a command.

**Exit criteria.** The window opens with a functional ribbon; **every** ribbon button, menu item,
and chord dispatches through `ActionRegistry` (verified by a debug assertion that no `QAction`
carries a direct connection); the §8 Global defaults all resolve correctly; settings round-trip;
`--lang zh-TW` shows a translated UI; a second launch reuses the first instance; risk **A3** has a
recorded verdict.

**Deferrals.** No document can be opened yet — `Ctrl+O` shows a dialog and reports "not yet
implemented". Contextual ribbon tabs, per-document pane state, the full Preferences page set, and
`WinShellIntegration`'s thumbnail/preview/index handlers all come later.

---

### Stage 2 — PDF engine boundary and tiled rendering

*Part 2 of the first milestone.*

**Status:** ✅ **SIGNED OFF BY THE USER, 2026-09-14.** **CODE COMPLETE — ALL EIGHTEEN TASKS DONE (T1–T3 2026-09-01, T4 and T5 2026-09-02, T6 2026-09-03, T7 … T15 2026-09-04, T16 2026-09-05, T17 and T18 2026-09-06). THE STAGE WAS SIGNED OFF ON 2026-09-14, when **the manual walk was COMPLETE — all ten cases signed off by the user, the last (M-09) on 2026-09-14** — and **every §9 item in the guide is now met**, and the user signed the stage off the same day. Item 1 was met on 2026-09-14 when A-69, its last half-met case, was met. Item 5 was met the same day: the new `.github/workflows/fuzz-soak.yml` ran both fuzz targets for 601 s each in run **34781013867**, 2,271,896 and 875,611 inputs, with no crash, leak, timeout or sanitizer line (A-25, A-26). A-71 has since been met, and A-72 and A-73 were completed on 2026-09-14. M-09 ran under Application Verifier on 2026-09-13, which was on this machine all along and came back clean, and its growth clause found **I-35** (`Stage2-Rendering-Testing-Guide.md` §8), partly fixed the same day — four causes, each with a case run red first, which halved the running product's growth — and downgraded from S2 to **S3 by the user** once the residual was measured as heap fragmentation, which the segment heap (`app/rawpage.manifest`) then fixed on 2026-09-14 — against a control build, private bytes 72.5 → 80.1 MB over 40 rounds where the classic heap reached 339.6. **I-35 CLOSED 2026-09-14**: the running product, measured without Application Verifier — whose full page heap had been left on for `rawpage.exe` through every earlier driven run, so none of those figures describe the product — grows +0.29 MB a round, the log pane's bounded fill.** **I-36 and I-37 (S3, both closed 2026-09-14)** are CI run 34772420519's two failures: A-84's and A-87's `PATH` test property was split on its semicolons, so a clean build could not start `rawpage.exe` (A-87 reported that as a skip), and `RenderScheduler::waitForIdle()` could return before the worker called the wake handler, which the ASan job landed in. Both were reproduced, fixed and verified on this machine, each with a case or a control run red first; CI run 34773735428 then passed on them, meeting A-71. **I-38 (S2, closed 2026-09-14)** was found in that green run's log: `fuzz_cos_parser` had reported a signed overflow in `Parser::parseObjectStream` on every run since T18 and failed none, because `portable-fuzz` lacked `-fno-sanitize-recover=all`. The offset is now range-checked before it is added, the preset aborts on a finding, and a unit case was run red under a local `-fsanitize-undefined-trap-on-error` build. **CI run 34775047037 confirmed all three**: every job green, 729/729 on Windows, and the first fuzz run under the abort flag with no sanitizer line in its log. **I-39 (S2) and I-40 (S3), both raised and closed 2026-09-14**, came from completing guide cases A-72 and A-73. A shortcut saved under a RETIRED action spelling survived a rename but could not be reset or cleanly rebound, because the settings loader kept the old key while every write built the new one. `Settings` now rewrites a retired spelling at load, in `shortcuts/` and in `ui/qatCommands`, and eleven cases through the synthetic alias table drive the real Shortcuts page and toolbar restore, each run against a control. Separately, `UiCustomization::applyToSettings` reported kept shortcuts as dropped, Stage 1 I-51's misreading in a second place. `--list-actions` gained its effective-chord column. 741/741 on this machine, one skipped. **I-41 (S3), raised and closed 2026-09-14**, came from completing guide case A-69. `rawpage_bench_pipeline` gained a latency control, phase 0: a rasterizer with a known 2.0 ms cost, run through the same instrument, which exits 1 before any figure if the instrument is dead, quantized or not counting the queue. Its first run showed the benchmark's pump delivered tiles only on a ~15.6 ms timer tick, so every latency it had produced was an upper bound. Through a yielding pump, A2's viewport p95 is **13.03–13.20 ms**, about 1.27× headroom rather than T18's 1.18×. The verdict is unchanged, and T18's *p50 gets worse with eight workers* is withdrawn. Both negative controls were run red. `Tech-Stack-Plan.md` §3.3 carries the corrected tables. A2 IS SETTLED ON BOTH MEASUREMENTS — `go` — AND **O7, O10 AND D2-14 ARE ANSWERED BY NUMBERS RATHER THAN BY PREFERENCE.** A PAGE IS ON SCREEN, THE KEYBOARD MOVES IT, THE PREFERENCES CHANGE IT, A MISSING FONT SUBSTITUTES THROUGH OUR OWN POLICY, AND 198 COMMITTED BASELINES NOW SAY A PAGE RENDERS THE SAME WAY TWICE. A2 has its probe verdict: `go`, the STAGE 2 EXIT CRITERION (A-21) is met on Windows, the product OPENS A DOCUMENT AND DRAWS A PAGE, and `core/render/` now holds the whole policy layer — resolution, tiling, layout, cache and scheduler. I-05 is CLOSED at T11 with the arrangement D2-9 left open: ONE render thread with a queue. **NO S1 OR S2 IS OPEN — I-35 was raised at S2 on 2026-09-13 in M-09 and downgraded to S3 by the user the same day** (2026-09-13, M-09) — the product kept about 4 MB of private memory per round of ten documents. Four causes were found by measurement rather than by reading: the log pane kept every line ever logged, every ribbon and QAT button gained a signal connection each time its command changed (a Qt 6.11.1 `QToolButton` behaviour — convention 23), the font substitution log was append-only, and finished tiles of a closed document were cached. The first diagnosis, read from private bytes, had pointed at the wrong layer. Through the product's own composition on the real platform, live heap is now flat (guide case A-92: 34.91 → 34.97 MB over 30 rounds) — **but the running product, re-measured the same day under M-09's driver, still grows +2.2 MB of private bytes a round against +4.3 before** — and a bounded follow-up measured that remainder as heap fragmentation (committed-but-free memory rising while live heap stays flat through the real single-instance pipe), not a leak of live objects. **Before it, no S1 or S2 remained** — **I-17 … I-21 were all raised and closed on 2026-09-06**, and **three of the five came from the manual walk**, which had walked three cases; **I-23 and I-24 followed on 2026-09-12 from M-04's**, raised and closed the same day. **I-21 is the one that says most about the harness**: every form in the product rendered as its labels and nothing else, because PDFium draws every annotation except `/Subtype /Widget` from `FPDF_RenderPageBitmap` and the facade never created the form-fill environment a widget's `/AP` needs — **and the golden baseline recorded that empty page as correct**, since a comparison with oneself cannot see an absence. `corpus/ordinary/annots-mixed.pdf` had been rendering its nine non-widget annotations correctly under the identical flag since T4; the two files are the two halves of one experiment and nothing compared them until a person did. **I-20 is the next**: `Actual Size` was wrong by the monitor's scale factor on every scaled display, because `ui/viewer/` joined `QScreen::name()` to `MonitorInfo::id` — two identifiers that can never be equal, so T12's measured physical DPI never reached a pixel — and the fallback it silently used instead was Qt's DEVICE-INDEPENDENT dpi read as a physical one. Neither suite could see it: the join exists only on a real desktop, `tests/ui` runs offscreen, and A-60 measures the topology with no screen beside it. **M-01 and M-02 passed against it**, because both judge crispness and continuity and the render was crisp and continuous at two thirds of its size; it took the first case that put the product beside another renderer. **I-19 is the other one**: a cache hit woke nothing, so a mixed-DPI drag blanked the page and left it blank, and the suite stayed green throughout because every case that could have caught it drained the completion queue through a back door the product does not have. **The manual half paid for itself: all ten cases signed off, and FOURTEEN product defects found by the user walking or preparing them (re-counted from the guide's §8 on 2026-09-14) — every one invisible to a green suite, and two of them, **I-28** and **I-34**, still open at S3.** **A seventh, I-27, was found by the WALK WITHOUT BEING IN IT**: the suite went red on A-84 because the walker still had `rtl-binding.pdf` open, and the cause was that `--dump-ui-strings`, `--print-screen-metrics` and `--list-actions` claimed the single-instance bus before answering — so each forwarded, returned 0 and printed nothing whenever a window was open. Three documented diagnostics, one of them I-20's own remedy, had been silently answering nothing for anyone with the product running, and a green `ctest` was conditional on an unrelated window. `Intent::composesAndExits()` now names the property and `main.cpp` skips the claim; guide case **A-87** holds the bus while it asserts, and skips rather than passing if its control cannot prove the bus was claimed. **M-01 and M-02** — the DPI pair — walked and signed off 2026-09-06; M-01 needed a product fix and a second walk to get there, M-02 passed first time on all four steps including the cold start at a changed primary scale. **M-03 is signed off too, on its third walk and after two product fixes** — its first stopped on **I-20** at the first file, its second on **I-21** at the sixth, and the third compared all six files against Acrobat at 100 % and 400 % with the anti-aliasing toggles. **Recorded in that case's own required wording**: *consistent with the reference over the generated corpus*, never *rendering is correct* — the corpus is ours, so a construct our generator never emits is outside this evidence entirely (D2-11). **M-04 is signed off as of 2026-09-12** — all five steps on the generated 900-page file, at `renderWorkers` of 8, 1 and 16, relaunching between each. It is **A2's third answer and the only one that is not a number**: a p95 under budget with a juddering scroll would have been a finding about the scheduler, and it did not happen. **That walk cost two more product fixes** — **I-23**, `Ctrl+Shift+T` could enter two-up and never leave it because the chord was registered through the helper that SELECTS a fixed mode while `checked` made it look like a toggle to everything that asked; and **I-24**, a paged mode trapped the reader on the page, since the layout holds one page and neither the wheel nor the arrow keys could cross its edge. **Neither is about rendering.** Both are gestures around the scroll, both were covered by cases that drove the COMMAND where a reader uses the GESTURE, and I-23's case had pressed its chord exactly once — convention 22, a mechanism proved to FIRE and never proved to fire WHEN IT SHOULD. **M-05's walk began 2026-09-13 and found I-25 in its first minutes** — a paged Two-Up could not leave its spread, because the page step landed on the page already beside it and the dominant-page snap then put it back, so pages 4-7 of the odd-count corpus file were unreachable by any gesture or command. **It is the third in that family and the sharpest of them**: the step it needed, `PagePresentation::spreadFor()`, had existed and been unit-tested since T13 under a header comment saying `ui/viewer/` called it — and nothing did. A present-tense claim nothing re-derives, wearing ten green `CHECK`s as evidence. The three paged-turn cases written for I-24 the day before each select `Single`, the one paged mode where the defect cannot appear. **M-05 is signed off as of 2026-09-13, on its second walk** — all four modes paged through in full, the `Ctrl+Shift+H`/`Ctrl+Shift+T` cycle closing back where it started, pairing and cover and RTL matching Acrobat on both fixtures, and the scroll half walked at Fit Width and at Fit Page including the fast-spin gesture no synthetic wheel event can produce. **Its bound is recorded with it**: two A4 fixtures of our own, no mixed-size spread, and an RTL file that carries `/Lang (ar)` and no Arabic text at all — RTL binding is a layout question and RTL text is Stage 5's. **M-06 is signed off as of 2026-09-13, on its first walk** — both zoom extremes with no hang, no out-of-memory and no black page, the preset ladder in both directions, and Fit Page, Actual Size and Fit Width — **but not Fit Visible's own clause, which the user signed through as I-28** (S3, open): `Ctrl+3` fits the CropBox where `Features.md` §1.2 specifies the content bounding box, so on every corpus file it is `Ctrl+0` under another name, and the one case asserting it checked the fit MODE and never the zoom. **M-07 is signed off as of 2026-09-13, on its second walk and after four product fixes** — **I-29** (S3, the message bar was painted the desk's own grey and could not be found), **I-30** (S2, an encrypted file's double-click left its password prompt behind the window in front), **I-31** (S1, a PDF with no readable page opened as a silent empty tab) and **I-32** (S2, a refusal said only in the status bar, and called four damaged PDFs *not a PDF*); the user held the first walk over the last two rather than sign it off against the case's own Expect line. **Its bound is recorded with it**: the generated corpus only, each malformed file damaged one way, and nine damaged files that open with no banner because `RepairReport` covers the xref layer alone — judged by eye to render sensibly, with content-level reporting an unasked scope question. **M-08 is signed off as of 2026-09-13, on its first walk and with no new findings** — its double-click step was added for I-30 the same day. **Its bound**: the corpus's five generated handlers, ASCII passwords only, no certificate handler, no permission enforcement (Stage 10), and `user` and `owner` indistinguishable in the product today; no automated case asserts that `owner` opens a file, which was measured by hand. **I-17 and I-18 came earlier the same day**, when the first CI run to execute Stage 2 code found the build broken in two places at once: `portable-asan` and `portable-fuzz` had not configured since T2 (a missing guard in `pdf/CMakeLists.txt`, under a comment asserting the guard was elsewhere), and the `windows` job had never fetched PDFium. Both were in the BUILD rather than the product, both invisible because no CI run happened between Stage 1 sign-off and T18, and the earlier claim that no S2 remained **was unqualified when written and was not yet earned**: I-02 closed at T16, and **I-13** — this stage's only S2 found in the PRODUCT, the font bridge substituting Segoe UI for Helvetica on every page of every document — was raised and closed inside the same run, by a control case nothing in the criterion had asked for. **T17's own two findings, I-14 and I-15, are both S3 and both in the harness it built.** What the golden suite's green means is bounded and the bound is stated wherever it is quoted: **consistency against our own generated corpus, never fidelity to real-world PDFs** (D2-11, §13.6).** Its execution plan
([`Stage2-Rendering-Plan.md`](./Stage2-Rendering-Plan.md), decisions **D2-1 … D2-14**, tasks
**T1 … T18**) and its testing guide
([`Stage2-Rendering-Testing-Guide.md`](./Stage2-Rendering-Testing-Guide.md), **74 automated** as written and
**10 manual** cases) were both written on 2026-08-31, **before any Stage 2 code**, per §5. The guide
holds **93** automated cases as of 2026-09-14 (re-counted from its `**A-nn ·` headings) — **A-83 … A-87** from the manual walks and **A-88 … A-93** from I-35 are the latest; the rest of this paragraph is the history of how the first 82 arrived, and its own running totals are that history: **A-75** was added on 2026-08-31 with I-02, **A-76 … A-78** at
T4 because the task's exit criterion said the corpus's reproducibility was *"asserted rather than
assumed"* and the guide had no case that asserted it, **A-79** at T13 (issue **I-09**) because the
one arrangement the shipped product actually runs — the real engine on the real scheduler through the
shipped adapter — was the one arrangement no suite exercised, **A-80** at T14, because the view
rotation this document lists among Stage 2's key bindings had no case in a guide written before
anyone noticed that `RenderRequest` had nowhere to carry it, and **A-81** at T15 — the measurement
behind **I-11**, and **A-82** at T17 — I-13's tripwire, because the guide was written before the
font bridge existed and nothing in it stopped that finding from being re-made silently. That is
**82**. **T16 added no case and that is deliberate**: A-66 and A-67 were already written
for it, and what T16 added instead was a *control* beside A-66 — the case asking whether the font
bridge runs when it should NOT — which is what found **I-13**. A control lives with its case; a
guide that numbered every one of them would number half the suite. **T17 added one for the opposite
reason**: A-82 is not a control for a case in the guide, it is a standing check that T16's *fix*
stays in place, and the thing it protects — 198 committed baselines — did not exist when the
choice about what to number was last made. **A-83 was added on 2026-09-06 by the MANUAL WALK, which is
the first case in this stage a suite did not ask for: M-01's very first minutes found I-19, and the
back door that hid it — every scheduler case draining the completion queue by calling `deliverReady()`
itself — was a convention 19 debt owed at A-38's assertion since T11. **A-84 came from the walk as well**, later the same day: M-03's first file exposed **I-20**, and the invariant it now asserts — that the dots per inch of glass do not move when only the OS scale factor moves — is a property `DpiPolicy`'s header had stated in prose since T10 with nothing checking it. **A-85 followed within the hour**, when M-03's own form-heavy file turned out to have been rendering as an empty page since T9 (**I-21**) — and it is written as a property with no reference image in it, because the golden baseline was the thing that had been agreeing with the defect. That is **85**, and **three of the last four cases in this stage were written because a person looked at the screen.**

| Task | State | Outcome and deviations |
|---|---|---|
| **T1** the engine boundary and the library split | ✅ 2026-09-01 | `core/backend.h` (all seven interfaces, D2-7), `pdf/BackendFactory.{h,cpp}`, `cmake/RawPageHeadless.cmake`, `tests/unit/tst_backend.cpp`, two `layer-lint` fixtures and a five-case gate fixture for the headless assertion; `pdf/PdfModule.cpp` **deleted**; `rawpage_pdf` renamed **`rawpage_cos`**. `ctest` **367/367** on `x64-debug` and `x64-release` (was 354: +6 unit, +5 `gates.headless_assert.*`, +2 `gates.layer_lint.*`); the bare `gates` preset **73/73** (was 66); `checks` clean at **144 files / 999 includes**, `i18n-lint` clean at 79 files / 2 catalogs / 0 waivers, and the catalogs are **538 keys each** (the new one is `error.backend.notImplemented`). **Deviations, and every one of them is a decision the plan left open rather than a departure from it:** **(a) `IPageRasterizer` returns a first-party `RasterImage`, not a `QImage`** — see **I-03** in the guide's §8. This document's own component list writes `Result<QImage> raster(...)` and three `core/render/` signatures beside it; `core/` may not name a QtGui type, links `Qt6::Core` only, and `layer-lint`'s `core` layer has denied `QImage` since Stage 0. The gate and the layer contract win, so the boundary carries width/height/stride/format/bytes and `ui/` wraps them at the far side — which is also what D2-9's *buffers and callbacks only* already said. T10 and T11 inherit the same correction for `RenderCache` and `RenderScheduler`. **(b) `rawpage_pdfium` is not declared as a target yet.** It has no sources until T2 writes `pdf/render/PdfiumInit`, and a static library kept alive by a placeholder TU is exactly what this task just deleted. The guarantee does not wait for the target: `rawpage_assert_headless()` rejects the forbidden **name** before resolving it to anything, which is what makes the rule hold on the portable presets where that target will never exist at all. **(c) The assertion moved to `cmake/RawPageHeadless.cmake`** so the gate fixture could exercise the real function rather than a copy of it (convention 7). **(d) It was reading half the graph, and the fixture found it on its first run**: an INTERFACE or IMPORTED target — `Qt6::Core` among them — keeps its edges in `INTERFACE_LINK_LIBRARIES` and leaves `LINK_LIBRARIES` unset, so a walk reading only the latter stops at the first edge of the wrong kind and reports a clean closure it never finished. Both are read now. **(e) The backend is wired into `app/CompositionRoot` and deliberately NOT into `Services`.** `pdf::createBackend()` has its one real caller and logs `backend: assembled -- 0 of 7 interfaces implemented` at startup; the service reaches `ui/` at T9, with the first widget that reads it, because a capability handed downward ahead of its consumer is the shape Stage 1 found five times (I-34, I-42, I-43, I-46, I-47). **(f) Two figures in this document set were measured rather than carried forward and were wrong**: the catalogs (`Architecture-FileMap.md` §3.18 said 463 keys; 538) and `platform/platform.h`'s interface count in the FileMap's own §1 diagram (said 15; 17 since Stage 1 T7). Both corrected. **A-04 (`portable-asan` still configures) is green by construction and unrun here** — it is a Linux job, and CI is what runs it. |
| **T2** PDFium made permanent | ✅ 2026-09-01 | `cmake/PdfiumAssert.cmake` (**PA-1 … PA-5**), `pdf/render/PdfiumInit.{h,cpp}`, the new **`rawpage_pdfium`** library, a new test target **`rawpage_render_tests`** (`render.` prefix) with `tst_pdfiuminit.cpp`, five `layer-lint` fixtures and seven `gates.pdfium_pin.*` cases; **`tools/spike-pdfium/` deleted**. `ctest` **383/383** on both presets (was 367); the bare `gates` preset **85/85** (was 73); `checks` clean at **147 files / 1,024 includes**, `i18n-lint` at **90 files** (was 79 — see the deviation below). Guide cases **A-05 … A-13 all ✅**. **Deviations:** **(a) The `FPDF_` rule narrowed as D2-9 asked, and the companion handle rule turned out not to be redundant** — the raw typedefs all begin with `FPDF_` and were already caught, but PDFium ships `include/cpp/`, whose `ScopedFPDFDocument`, `ScopedFPDFPage` and the `FPDFDocumentDeleter` family contain **no underscore after `FPDF`** and are invisible to a substring search for it. A `core/` struct holding a `ScopedFPDFDocument` is this boundary's exact failure mode spelled the one way the other rule misses; it has its own fixture. **(b) `gates.layer_lint.fpdf_outside_pdf` had never had a control.** It has proved since Stage 0 that naming PDFium outside `pdf/` fails, and nothing had proved that naming it inside succeeds — so it was equally satisfied by a gate rejecting PDFium everywhere, which is not a stricter rule but one that forbids the product. Added. **(c) `i18n-lint` had never scanned `pdf/` or `platform/`**, and the first key the engine layer named was reported as an orphan by the gate while being referenced by the file that had just been written. Paths added; the deeper half — that **no rule compares a referenced key against the catalogs**, leaving **eighteen** referenced keys with no entry, four of them in `core/` — is **I-04**, S3, owned by T14. **(d) The imported PDFium target is declared in `pdf/CMakeLists.txt` rather than through the packager's `PDFiumConfig.cmake`**, which points `IMPORTED_IMPLIB` at the MSVC-format `pdfium.dll.lib`; GNU ld cannot read it, and what we link is the `libpdfium.dll.a` that `fetch-pdfium.ps1` generates. **(e) `--version` reports the build-time pin and says so**, because the `FPDF_` C API exposes no version call. **(f) `rawpage_pdfium` links PDFium PRIVATE**, so the facade's consumers do not acquire `fpdfview.h`; the A2 probe links the engine directly and its CMake comment says why it is the one program in the tree deliberately below the facade rather than behind it. |
| **T3** A2 early probe — the go/no-go | ✅ 2026-09-01 — **A2 verdict recorded: `go`**; **I-05** | `corpus/make-a2-text-page.py` + `corpus/a2-text-page.pdf` (62 text runs, **6,650 glyphs**, regenerates byte-identically), `tests/bench/bench_render.cpp` and `bench.render.a2_probe`. `ctest` **384/384** on both presets. **The figure:** raw `FPDF_RenderPageBitmap` at **4.00 device px/pt** (200 % zoom on the 150 %-scaled monitor), A4 → **2381 × 3368 px**, 35 tiles of 512 × 512, 20 timed passes after 3 warm-up. One worker **p50 0.433 / p95 0.688 / max 0.911 ms**; eight workers behind an engine lock **p50 0.405 / p95 0.532 / max 0.885 ms** — **25–31× headroom** against the 16.667 ms frame budget, so D10's tiling policy stands and the stage continues. Recorded in `Tech-Stack-Plan.md` §3.3 with the machine it was measured on, and risk **R5** struck through in the same change. **Deviations, and the first is the reason this task exists at T3 rather than at T13:** **(a) The probe found that PDFium cannot be called from two threads at once — `I-05`, S2, open, owned by T11.** Eight workers each with their **own `FPDF_DOCUMENT` and own `FPDF_PAGE`** — exactly the confinement D2-9 specifies — crash; measured by walking the count, **1 exits 0 and 2, 4 and 8 all die with `STATUS_HEAP_CORRUPTION`**. What PDFium shares is process-global (font mapper, glyph caches), not per-document. **A2 is unaffected**: it is a throughput ceiling, not a latency one — eight locked workers retire **1.05×** one worker's tiles per second. D10's actual promise, *the UI thread never rasterizes*, survives either arrangement (one render thread, or N workers on one engine lock) and choosing between them is T11's. **(b) The benchmark therefore ships with a global engine lock and an `--unsafe-parallel` switch** that reproduces the crash on demand, because a finding that needs a source edit to reproduce is one that gets re-litigated. **(c) The reported per-tile figure is SERVICE TIME, measured inside the lock** — what the engine costs, not what a worker waited — so the scaling question is answered with **throughput** instead: a p95 ratio would read *below* 1.0 and look like better-than-perfect scaling. **(d) `stdout` is unbuffered**, added after the first crashing run printed nothing at all under redirection and made *"how far did it get"* unanswerable. **(e) The probe's own limits are recorded with its figure**: one page, one page shape, a **non-embedded** base-14 font (so it measures PDFium's system-font path, not an embedded subset), no images, shading, transparency or annotations. The corpus-wide figure through the shipped pipeline is T18's. |
| **T4** the corpus — entirely generated | ✅ 2026-09-02 | **60 files**, every one produced by a script we own (D2-11), every one with a `corpus/PROVENANCE.md` row, every one regenerating **byte-identically**. Nine new modules under `corpus/`: `pdfbuild.py` (the shared assembler — classic xref, xref streams, object streams, incremental revisions, the filter helpers), four codecs written from the specifications because the standard library has none and a pasted blob would be a borrowed file in disguise (`deflate.py`, `jpegenc.py`, `ccitt.py`, `jpxenc.py`), `fontenc.py`, `cryptenc.py`, and the three generators plus the `make-corpus.py` driver. **29 `ordinary/`** (DCT RGB and grey, JPX, CCITT G3 and G4, predicted Flate, indexed/stencil/soft-masked and filter-chained images; axial, radial and function-based shadings; coloured and uncoloured tiling patterns; transparency groups, blend modes and a luminosity soft mask; hairlines and both winding rules; embedded CJK and Latin subsets; nine form widget types and nine annotation types, each with a real `/AP`; a tagged document whose drawing order is not its reading order; outline and page labels; **900 pages**, an odd count with a cover, an RTL binding, object-stream structure, and **three incremental generations**), **24 `malformed/`** with one named fault each, **5 `encrypted/`** across R2/R3/R4/R6. `ctest` **387/387** on both presets (was 384: `gates.corpus.reproducible`, `.provenance`, `.self_test`); the bare `gates` preset **88/88**. **Deviations, all four of them about making the exit criterion real rather than about the data:** **(a)** The criterion says reproducibility is *"asserted rather than assumed"* and no case in the guide asserted it, so **A-76 … A-78** were added — the third being the negative control, because a checker that returns success without looking passes the other two; it drives all five detections in memory and requires each to fire. **(b) `zlib.compress` could not be used.** Its output depends on the zlib build the interpreter links, so the same script would emit different bytes here and on the Ubuntu runner and the gate would go red for a reason unrelated to this project — hence a DEFLATE encoder we own, emitting stored, fixed **and** dynamic Huffman blocks so `Flate` (T7) meets all three decode paths. The same argument produced `jpegenc.py`'s hardcoded DCT cosine table instead of `math.cos`, whose last bit is the platform's libm. **(c) The JPX stream carries one coding pass** — the EBCOT cleanup pass of the MSB plane — so a 0/255 source reconstructs to roughly 0/128. It is a *truncated*, not a malformed, codestream; it exercises the `/JPXDecode` path and is explicitly not a claim about JPEG 2000 fidelity, and that limit is stated in the module, in `PROVENANCE.md` and in the guide's §7. **(d) `.gitattributes` was narrowed**: `corpus/** binary` was marking the new generators binary too, so `git diff` showed nothing for several thousand lines of Python. **Verified beyond the gate:** all 29 `ordinary/` files plus both seeds open and rasterize through the real PDFium (`rawpage_bench_render`), and **all 24 malformed files were run through it with no crash and no hang** — 19 recovered, 5 cleanly refused, which is the calibration those fixtures want. |
| **T5** `pdf/cos/Lexer` | ✅ 2026-09-02 | `pdf/cos/Lexer.{h,cpp}` in `rawpage_cos`, `tests/unit/tst_cos_lexer.cpp` (**24 cases**, guide **A-15**), `tests/fuzz/{LexerFuzzBody.h,fuzz_cos_lexer.cpp,CMakeLists.txt}` and a compile fixture. `ctest` **412/412** on both presets (was 387); `gates` **88/88**; `layer-lint` clean at **153 files / 1,047 includes**. **The first token of PDF this project has ever read for itself.** *Tolerant by design* is implemented as three named guarantees rather than a disposition — **it never fails** (no `Result`; a malformed construct yields a token carrying a `LexNote` and lexing continues, because a reader that stops at the first bad byte cannot reconstruct anything and `XrefRepair` at T8 is what needs it to keep going), **it always makes progress** (every `next()` below the end advances ≥ 1 byte), and **it never reads out of bounds**, including inside the one-byte-from-the-end escapes `#`, `\` and `<`. Each tolerance is a specified behaviour with a corpus file or a named real-world producer behind it: `--5` reads as −5 (Acrobat does too, and a lexer that stopped at the second sign would silently turn one operand into two), `1.2.3` stays one token, `/A#GG` keeps its `#` (`malformed/bad-name-hex-escape.pdf`), `<41A>` pads per ISO 32000-1 7.3.4.3, a raw CR/CRLF inside a literal string normalizes to LF, and an out-of-range integer **clamps rather than wraps** — a wrapped length is negative and a negative length is one `size_t` cast from an allocation the size of the address space. **Deviations, both about a target this machine cannot build:** **(a)** the fuzz body was factored into `tests/fuzz/LexerFuzzBody.h` so `tst_cos_lexer` replays all 60 corpus files and a table of adversarial inputs through the **same** function `LLVMFuzzerTestOneInput` wraps — 38 of the 60 produce at least one note — and **(b)** the entry point is compile-checked by the Windows toolchain every run (`unit.cos.compile.fuzz_entrypoint`), because a fuzz TU only clang builds is one that rots between Linux runs; that is Stage 1's I-30 shape. Neither substitutes for **A-25**, whose ten clean minutes remain **unrun** — there is no clang here — and which is now recorded as a Linux job alongside A-04 rather than as pending. **A-27 changed in kind**: counting fuzz targets by hand once is right on the day and silently wrong after, so `tests/fuzz/CMakeLists.txt` **fails configure** when the count is not `RAWPAGE_EXPECTED_FUZZ_TARGETS` (1 now, 2 at T6), and the root's Stage-0 message *"NO FUZZ TARGETS EXIST YET"* went with it. **Two negative controls were run rather than reasoned about**: removing the `)` advance produced *"no progress at 0"* and *"no progress at 585"* — the second from a real corpus file, which also proves the replay loop does work — and removing the `<<` lookahead reddened two cases. |
| **T6** `CosObject` + `Parser` | ✅ 2026-09-03 | `pdf/cos/{CosObject,Parser}.{h,cpp}` in `rawpage_cos`, `tests/unit/tst_cos_parser.cpp` (**30 cases**, guide **A-16** and **A-17**), `tests/fuzz/{ParserFuzzBody.h,fuzz_cos_parser.cpp}` and a second compile fixture. `ctest` **443/443** on both presets (was 412); `gates` **88/88**; `checks` clean at **161 files / 1,086 includes**, i18n-lint at **96 files**. **The first PDF OBJECT this project has read for itself** — `pdf::createBackend()` still refuses everything by name and no document has been opened. **Identity-preserving round-trip is carried by a SPAN on every parsed object**, and it is asserted the strong way: every object in every one of the 29 `corpus/ordinary/` files is re-parsed FROM ITS OWN BYTE RANGE and must compare equal — 200-plus objects, 20-plus of them streams. That is what Stage 3's byte-identical incremental save rests on, and the corollary is enforced rather than documented: a constructed or mutated object has no span, so Stage 3 must serialize it properly rather than read the absence as nothing-to-write. **The `/Length` rule is: trust it only when it lands on `endstream`.** `corpus/malformed/length-lies-short.pdf` (12 short), `length-lies-long.pdf` (400 long — which still FITS inside the file, so a parser that only checked `declared > available` would accept it silently), `huge-length-claim.pdf` (4,294,967,295 in a 10 KB file) and `truncated-mid-stream.pdf` all recover to the right bytes and say so, and **A-17's control** — `ordinary/stream-length-control.pdf`, three bytes different — parses with the recovery path **not** taken, without which the case would pass against a parser that always scans. **Deviations, and each is a boundary the plan left to the task:** **(a) the depth bound is `kMaxDepth = 128` and it is a REFUSAL, not a correctness claim** — a legal PDF has no depth limit, but `deep-nesting.pdf`'s 4,000 levels are a stack overflow in an unguarded recursive-descent parser and A-21 counts that as a crash; past the limit the construct is consumed ITERATIVELY, so the parse resumes correctly instead of reading 3,872 stray brackets as top-level junk. **(b) An indirect `/Length 9 0 R` resolves through a CALLBACK rather than through an `XrefTable` member**, because T7 is built on this file and a member would be a cycle — which is also what made the recovery path testable before any xref exists. **(c) `parseObjectStream` takes the DECODED payload rather than the stream**, because every object stream in the corpus is `/FlateDecode`d and Flate is T7's; the suite asserts that gap rather than describing it, so it closes when T7 closes it. **(d) Two new catalog keys**, `error.pdf.badObjectHeader` and `error.pdf.badObjectStream`, in both shipped locales — 541 per catalog, was 539. **(e) Token lookahead is a two-slot PUSHBACK rather than a lexer rewind**, so a noted token is counted once rather than once per re-lex; without it the census says a file is worse than it is. **Four negative controls were run rather than reasoned about:** trusting `/Length` blindly reddened exactly the two recovery cases; raising `kMaxDepth` past 4,000 turned two cases into a **SegFault**, which is the crash the bound exists to prevent; replacing the pushback with a rewind turned a note count of 2 into 3; and truncating the stream span reddened the corpus round-trip case. **A-27 is now fully closed** — `RAWPAGE_EXPECTED_FUZZ_TARGETS` went 1 → 2 in this commit, as T5's own comment said it would. **A-26 is unrun and is recorded as a Linux job beside A-04 and A-25**, not as pending. |
| **T7** `XrefTable` + the nine filters | ✅ 2026-09-04 — **I-06 raised**; **I-02's inventory half closed** | `pdf/cos/XrefTable.{h,cpp}`, `pdf/cos/filters/` (ten files: the registry plus nine filters), `tests/unit/{tst_cos_filters,tst_xreftable}.cpp` (**27 cases**, guide **A-18**, **A-19**, **A-20**), the `Inventory.cpp` closure change with two new gate fixtures, and vcpkg's `codecs` feature switched on for the Windows **and portable** presets. `ctest` **472/472** on both presets (was 443); `gates` **90/90** (was 88); `checks` clean at **185 files / 1,178 includes**. **zlib is not an image dependency here** — every PDF 1.5-or-later file keeps its cross-reference table and its object streams Flate-compressed, so `/FlateDecode` is what lets the reader find a single object in a modern document. That is why the filters had to land with, not after, the xref. **P-3 stopped being a dry-run**: `vcpkg install --x-feature=codecs` on `x64-mingw-dynamic` completed in **1.2 minutes**, all five ports. **Deviations, and the first two are findings:** **(a) `/JBIG2Decode` is DECLARED AND DECODES NOTHING — issue I-06.** Three facts forced it and each was established rather than assumed: the only usable library is Ghostscript's `jbig2dec`, which is **AGPL-3.0** and therefore a build failure under D4 rather than a trade-off; **the corpus contains no JBIG2 file at all**, and D2-11 says a construct the corpus never contains is one nothing here ever renders; and nothing in Stage 2 would call it, since rendering goes through PDFium. That is D2-4's own argument for `CryptFilter`. It refuses under its own catalog key rather than returning an empty buffer, which would reach the image pipeline as a blank page. `/CCITTFaxDecode` with `K > 0` is the smaller half of the same issue: implemented, no fixture. **(b) A-18's criterion as written is unsatisfiable for three of the nine** — `encode`→`decode` returning the input exactly is not a property JPEG or JPEG 2000 *has*, and CCITT and JBIG2 are decode-only at a stage D2-4 makes a reader. Corrected structurally rather than narrowed: `isLossless(FilterKind)` carries the distinction, the round-trip case iterates it, and a companion case asserts the other four refuse to encode **by name**. **(c) LZW's `EarlyChange` boundary was MEASURED, not derived** — the decoder runs one table entry behind the encoder, so its condition is `>=` where the encoder's is `>`, and the two look like different rules while being one. A decoder using the encoder's form fails on the corpus stream at the first boundary crossing; that was run. **(d) CCITT is first-party** because libtiff's fax decoder is reachable only through a `TIFF*` handle, so fabricating a TIFF header around every PDF image stream is more code than the decoder. **(e) `Qt`'s `emit` macro** silently turned a lambda named `emit` into `= [...]`; `core/Result.h` pulls QtCore in through `QString`, so nothing in `pdf/cos/` may use that identifier. **Four negative controls run:** breaking LZW's boundary reddened the corpus chain and the round-trip; reversing the xref's newest-wins rule reddened A-20 and nothing else; inverting `/BlackIs1` reddened CCITT **only after** an exact bitmap comparison was added, which is what proved the G3-versus-G4 cross-check had a blind spot; truncating a stream span reddened the round-trip case. |
| **T8** `XrefRepair`, `RepairReport`, `Revision`, `Scope` | ✅ 2026-09-04 — **the Stage 2 exit criterion, on Windows** | `pdf/cos/XrefRepair.{h,cpp}`, `core/doc/{Revision.h,Scope.{h,cpp},RepairReport.{h,cpp}}` — the first three files in `core/doc/` — and `tests/unit/{tst_xrefrepair,tst_docvocabulary}.cpp` (**20 cases**, guide **A-21**, **A-22**, **A-23**). `ctest` **492/492** on both presets (was 472); `gates` **90/90**; `checks` clean at **194 files / 1,218 includes**, i18n-lint **125 files**, `license-audit` **20 components**. **A-21 is met**: all 24 `corpus/malformed/` files yield a table and a report or a named refusal — no crash, no hang, no unbounded allocation. **Its ASan half (A-24) is a Linux job and is unrun**, and that is a real gap rather than a formality: a Windows run proves these files terminate and proves nothing about a read one byte past a buffer. **Deviations:** **(a) The repairer repairs as LITTLE as possible.** The obvious *rebuild everything when the xref is broken* would throw away the good half of a file whose damage is confined to its tail — which is most damaged files — and would make A-22's ratio 1.0 for every document ever opened. So it tries the file's own structures, verifies them, and replaces only entries that do not hold the object they name. **(b) `openWithRepair()` is deliberately the ONLY entry point**, so T9 opens a document through one code path whether or not it is damaged; two paths is how the repaired case ends up untested. **(c) A-23 moved one layer up.** Its steps say *through `pdf/cos/Parser`*, and the parser sees objects rather than trailers — `/Encrypt` is a trailer key, so the decline lives on the table, before the repair path, and returns **`Unsupported` rather than `ParseFailure`** because *this file is fine and this build cannot read it* must not send a caller down the damaged-file road. **(d) `RepairReport::requiresSaveAs()` is not `!isEmpty()`** — an `Info` finding such as an exactly-recovered `/Length` is the commonest thing in any real PDF, and forcing the prompt on it would train users to ignore it. **(e) `Revision` and `Scope` have no guide case of their own** and are tested anyway: their consumers are T11's and Stage 3's, and a vocabulary type with no test is one whose semantics get decided by its first accidental caller. **One finding, fixed in the same change:** `root-points-at-nothing.pdf` came back marked repaired with an **empty report**, because the code asked *is there a `/Root`* rather than *does it resolve*. Repaired-with-nothing-to-show is the one combination A-21 forbids. **Negative control run:** forcing the repairer past its fast path reddened the un-repaired control at once. |
| **T9** a document opens | ✅ 2026-09-04 — **A-46 closed; A-58 replaced by A-47** | `pdf/render/{PdfiumDocument,PdfiumRasterizer}.{h,cpp}`, `pdf/DocumentBackendImpl.{h,cpp}`, `core/doc/{Page,PageTree}.{h,cpp}`, `core/render/RenderRequest.h`, `tests/render/{tst_pdfiumdoc,tst_rasterizer}.cpp` (**22 cases**, guide **A-42 … A-47**), `tests/unit/tst_pagetree.cpp` (**11 cases**) and `tests/ui/FakeBackend.h`. `ctest` **526/526** on both presets (was 492); `gates` **90/90**; `checks` clean at **209 files / 1,320 includes**, i18n-lint **136 files**. **`rawpage.exe` opens a PDF and `PdfiumRasterizer` draws a page** — the backend census now logs `2 of 7 interfaces implemented` where it read 0 from T1. **Deviations, and the first two are structural rather than cosmetic:** **(a) `pdf::createBackend()` MOVED from `rawpage_cos` to `rawpage_pdfium`.** A backend that opens documents IS the engine, and `tests/unit` links `rawpage_cos` under `rawpage_assert_headless()`, which forbids `rawpage_pdfium` by name (D2-2). `tst_backend.cpp` split in consequence: the portable half — the interface declarations, `notImplementedError`, the census helper, `RasterImage`'s stride check — stays in the headless suite over a **test backend defined in that file**, and the shipped backend's census moved to `tests/render/`. **What is lost is stated rather than absorbed: the concrete backend is no longer asserted under ASan.** **(b) `core/render/RenderRequest.h` came forward from T10**, because `IPageRasterizer::raster()` takes one, `core/backend.h` has carried a forward declaration since T1, and T9's exit criterion is a rasterized page. `DpiPolicy`, `TileGrid` and `PagePresentation` are still T10's and are built ON it. **(c) `Services` gained `core::IDocumentBackend&`** — the consumer `app/Services.h` has been saying since T1 it was waiting for, which is why it was not added earlier (I-34's shape). **(d) The file is read by US and handed to PDFium as BYTES**, not as a path: what encoding `FPDF_LoadDocument`'s `const char*` is depends on the platform, and convention 1 exists because this machine's code page 950 turned an emoji filename into `report-?????.pdf`. The cost — a 400 MB document occupies 400 MB — is recorded rather than absorbed. **(e) D2-9's per-worker confinement is not what shipped**: the engine lock lives in `PdfiumInit`, held across `LoadPage`, bitmap creation, render and destroy, because I-05 measured the alternative false. **Two findings, both fixed in the same change.** **`FPDF_GetPageCount` returns `/Count` VERBATIM**, so `corpus/malformed/pages-count-wrong.pdf` opened claiming **97 pages over three real kids** — the count reaches the status bar and the scrollbar before anything walks the document, so believing it gives a 3-page file a 97-page scrollbar. The trailing run of unloadable pages is now dropped, and only the trailing run, because removing an interior page would renumber every page after it. And **the backend census log line was briefly LOST** — it lived in the null backend's constructor — and was noticed by a smoke run of `--dump-ui-strings` showing the platform bundle's census with nothing beside it. **One test was wrong and the code was right**: the render-flags case rendered `text-a4.pdf`, which is black type on white paper and therefore ALREADY greyscale, so a `Grayscale` render of it is byte-identical by construction. Moved to `image-dct-rgb.pdf`, with a `REQUIRE` that the colour render actually contains colour so the comparison cannot pass vacuously. **Three negative controls run:** replacing the negative tile origin with `(0,0)` reddened the region and scale cases; restoring the trust in `/Count` reddened the page-count case alone; ignoring the backend's refusal reddened both halves of A-47's UI case. |\n| **T10 … T18** | ⚪ not started | — |
| **T10** `core/render/` policy | ✅ 2026-09-04 | `core/render/{DpiPolicy,TileGrid,PagePresentation}.{h,cpp}` and `tests/unit/{tst_dpipolicy,tst_tilegrid,tst_presentation}.cpp` (**18 cases**, guide **A-28 … A-34**). `ctest` **544/544** on both presets (was 526); `gates` **90/90**; `checks` clean at **220 files / 1,384 includes**. **No new catalog key, settings key or registered command** — T10 builds arithmetic, and all of it runs headless. **Deviations, and each is a decision the plan left open rather than a departure from it:** **(a) `PagePresentation::layout()` takes a `Resolution` and a CURRENT PAGE, and returns `DeviceRect`s** where this document's component list writes `QVector<QRectF> layout(const PageTree&, LayoutMode, Zoom) const`. A zoom alone does not determine a pixel — the monitor's physical DPI does too, and `DpiPolicy` is the file that combines them, so taking a zoom here would make this the second place that arithmetic happens. Without a current page, **Single and Two-Up are indistinguishable from their continuous twins** and the four modes are two. And `core/Geometry.h` exists precisely so a rectangle says which space it is in; a `QRectF` here is the PDF-point/device-pixel mix-up that file was written to prevent. **(b) `Resolution` carries three numbers, not one.** The raster DPI is the monitor's PHYSICAL DPI times the zoom — Actual Size is 100 % on the glass — and the OS scale factor is carried separately as a device-pixel ratio. The consequence is a real property with a case: **moving a window between two monitors of equal glass DPI and different scale factors is a resample, not a re-render**. The third number is `upscale`, because capping the resolution without a compensating factor silently shrinks the page, and the cap then reads as a zoom bug. **(c) `performance/prefetchPages` counts PAGES and `TileGrid` sees one page**, so within a page the knob is a ring of tiles (`Config::prefetchRings`) and the pages half belongs to `ui/viewer/PageLayoutEngine` at T13. Both halves are real and they are not the same number. **(d) The defaults for `pageDisplay/maxRenderResolution` live in `DpiPolicy::Limits` until T15 adds the row**, which the header says in as many words — `SettingsKeys.def` rule 2 is that a default has exactly one home, and the row must carry that value when it lands. **One finding, and it is a language trap rather than a defect:** `PageLayout::slots()` does not compile, because **Qt defines `slots` as an empty macro** — the diagnostic is `expected unqualified-id before ')' token` on the declaring line. Renamed `pageSlots()`, with the reason recorded at the declaration; this is the `emit` collision T7 hit, in a second spelling. **Four negative controls run:** letting the OS scale factor into the raster DPI reddened the physical-DPI case and its control; replacing floor division with truncation reddened exactly the two lines written for it — and it reddened NOTHING on the first attempt, because the case as first written could not tell the two apart, which is convention 7 catching a case rather than a defect; removing the RTL column swap reddened four assertions in A-34 and nothing else; and putting the cover page in the verso column reddened A-33's geometry half. |
| **T11** `RenderScheduler` + `RenderCache` | ✅ 2026-09-04 — **I-05 closed; D2-9 corrected** | `core/render/{RenderCache,DiskTileStore,RenderScheduler}.{h,cpp}` and `tests/unit/{tst_rendercache,tst_renderscheduler}.cpp` (**17 cases**, guide **A-36 … A-41**). `ctest` **561/561** on both presets (was 544); `gates` **90/90**; `checks` clean at **226 files / 1,423 includes**, `license-audit` **20 components**, i18n-lint **148 files / 2 catalogs / 0 waivers**. **The waiver census went 6 → 8**, both in `DiskTileStore.cpp` and both silencing something real. **I-05 IS CLOSED, AND THE ARRANGEMENT IS ONE RENDER THREAD WITH A QUEUE.** D2-9 named two ways to keep D10's promise and left the choice to this task; it was made on T3's measurement rather than on taste — eight workers behind the engine lock retire **1.05×** the tiles per second of one, so the parallelism buys 5 %, and per-tile p95 is **0.532 ms against a 16.667 ms budget**, so the engine is not the bottleneck. `Config::workerCount` remains and defaults to 1, each worker taking its own rasterizer from a factory, because T18 must be able to walk the count without a rebuild. D2-9's row in `Stage2-Rendering-Plan.md` was corrected in this change, as that row said it would be. **Deviations:** **(a) `RenderCache::invalidate()` takes a `DocumentHandle`**, which this document's `invalidate(Scope, Revision)` omits. A `Scope` names pages and objects and says nothing about which document they belong to, and **O7 leaves open whether one cache serves several** — `RenderRequest` carries a handle precisely so two documents can share one. Without it, invalidating one document's page 3 would drop page 3 of every open document. **(b) Invalidation RE-KEYS what it does not drop**, and without that half `Scope` buys nothing at all: the revision inside every key would have moved, so a one-page rotation would miss on all 900 pages of a catalogue — a full re-render with no symptom on screen and nothing failing. The guide's *"a lookup for an older `Revision` misses"* then holds structurally rather than by a second check. **(c) `core/render/DiskTileStore.{h,cpp}` is a file the plan's §4 manifest does not list**, added with a FileMap row. D2-14 requires the disk tier behind the same interface as the memory tier; a tier whose I/O lived in `RenderCache.cpp` could not be built against a temporary directory by a test, and would have put write waivers in the pure half. It is **off by default** and **does not claim to survive a restart** — its index is in memory and the directory is purged on construction; a cross-session cache needs an on-disk index and a validity check with a security dimension, which belongs with O7 at T18. **(d) Delivery is asserted more strongly than "on the UI thread"**, because offscreen there is no UI thread: **no subscriber is ever entered from a worker**, checked by recording the thread id at every callback, with the data-free wake handler as the control proving a worker thread existed at all. That is also what lets `RenderCache` be lock-free — only the pumping thread touches it. **(e) A cache hit is delivered through the same path as a render** rather than returned from `enqueue()`: a subscriber called from inside `enqueue()` would re-enter a paint handler that has not finished asking for its tiles. **Five negative controls run, and two of them found holes in the SUITE rather than in the code.** Removing the re-key reddened A-41; letting `cancel` mark instead of dequeue reddened A-36 and A-37; delivering from the worker reddened four cases. But ignoring the document filter and keeping a superseded result both passed — the first because the control was aimed at a line that was not the filter, the **second because the suite could not reach the branch at all**: every case either had not started or had already finished, and a job cancelled or superseded WHILE RUNNING takes a third path. A latch in the fake rasterizer now holds a tile inside the engine so a case can act mid-flight, and both branches have a case and a control. |
| **T12** `WinDisplayTopology`, `WinPowerSession` | ✅ 2026-09-04 — **I-07 raised and closed** | `platform/win/{WinDisplayTopology,WinPowerSession,WinMessageWindow}.{h,cpp}` and `tests/platform/{tst_displaytopology,tst_powersession}.cpp` (**9 cases**, guide **A-60**, and the automated halves of **A-61** and **A-62**). The Windows bundle goes from **two of seventeen** interfaces to **four**. **A-60 is green against the real P-1 rig and agrees with the guide §2 rig table for the first time through the product's own query** — `\\.\DISPLAY1` 3840×2160 at 150 % with a **163 DPI** panel, `\\.\DISPLAY2` 2560×1440 at 100 % with **108** — and the physical-DPI column is the point: `96 * scale` says 144 and 96, which is what every `DpiPolicy` caller passed before this task. The precondition is asserted rather than assumed: `processIsPerMonitorAware()` is a `REQUIRE`, and `tests/platform/main.cpp` sets per-monitor-v2 awareness before `QCoreApplication` exists, without which Windows hands a DPI-unaware suite virtualized figures that are self-consistent and wrong. **Deviations, all recorded rather than absorbed: (a)** `WinMessageWindow` is a fourth file the plan's §4 manifest did not list — both interfaces need the same hidden top-level window because on Windows a notification IS a window message, and it carries the load-bearing note that a message-only window (the obvious choice) does not receive broadcasts and would silently never fire. Added with its own FileMap row, on T11's `DiskTileStore` precedent. **(b)** `changed` fires on a real DIFFERENCE rather than per message, because one scale change produces several notifications; the suppression counter is what makes that assertable. **(c) `subscribe()` has no product caller yet and that is a decision**: `PageView` re-reads the topology on Qt's own screen-change events, and `IDisplayTopology` has no `unsubscribe`, so a per-window subscription would dangle when a torn-off window closes. Its real consumer is `SessionState` at Stage 4. **(d) the battery THROTTLE is T15's**, not this task's: it needs `performance/throttleOnBattery`, and `setActiveWorkerLimit()` clamps to `[1, 1]` today. **(e)** the desktop has no battery, so `platform.powersession reports the machine's power source` **SKIPS by name** — A-62's own *"not applicable, explicitly"* requirement honoured rather than passed vacuously. **I-07:** `platform/platform.h` documented `MonitorInfo`'s geometry as *device-independent* pixels and it is physical pixels — the DIP reading is what a DPI-unaware process is handed, so the comment described the failure mode as the specification. Corrected in the same change. |
| **T13** `ui/viewer/` | ✅ 2026-09-04 — **A PAGE IS ON SCREEN**; **I-08 and I-09 raised and closed** | `ui/viewer/{ViewportController,PageLayoutEngine,TilePresenter,PageView}.{h,cpp}`, `app/BackendRasterizer.h`, and `tests/ui/{ShellFixture.h,tst_viewport,tst_pageview}.cpp` plus `tests/render/tst_renderpipeline.cpp` (**28 new cases**, guide **A-52 … A-56**, **A-79**, and **A-39's widget half**). `DocumentWindow` stops being D1-2's stub: it owns the `DocumentHandle`, the `Revision` and a `PageView`, and it is what CLOSES the handle — cancel this document's jobs, drain the scheduler, drop its tiles, then close, because a worker inside PDFium on a document about to be destroyed is a use-after-free no mutex can prevent. `CompositionRoot` builds the one `RenderCache` and `RenderScheduler` and owns the scheduler's **one** wake handler (convention 18: `setWakeHandler` replaces, so a `PageView` installing its own would silence every other view the moment a second tab opened). `Services` gains `RenderScheduler&` **and** `RenderCache&`. **D2-8 is honoured: `PageView` is the first widget in the product to declare a `KeyContext`**, so `activeContext()` answers something other than `Global` for the first time in the product's history. **Deviations: (a)** `app/BackendRasterizer.h` is a file the manifest did not list — see I-09. **(b)** the seven `tests/ui` fixtures became one `ShellFixture.h`, because `Services` growing two members turned seven copies of the same eight fields into a seven-way edit. **(c)** guide case **A-57**, *"the stub holds a path and nothing else"*, was **rewritten rather than deleted** — the emptiness was D1-2's feature and filling it is this task, so a case still guarding it would assert that T13 did not happen. **(d)** the `shell.document.placeholderBody` catalog key is **deleted** in both shipped locales: its text promised that "the page surface arrives with Stage 2", and a key apologizing for something that now exists is worse than no key. Five `error.*` keys were added, taking the catalogs from 561 to **565**. **(e) `platform::IAcceleratedSurface` is NOT wired** — software presentation only, T16 — and the interface census says so rather than a stub pretending. **Six negative controls run; one of them refused to fail and the CASE was fixed**: the eviction claim was bounded by a tile COUNT loose enough that a presenter which never evicted still passed, so `TilePresenter::heldPages()` exists and the case now asserts the first page's tiles are GONE. **(f) I-24, found by the user 2026-09-12 in the M-04 walk and fixed the same day:** a paged layout holds one page, so the scroll range was that page and neither the wheel nor the arrow keys could cross a boundary — a reader in `Single` was stuck. `PageView::scrollOrTurnPage` turns the page when the scroll is already at the limit and the mode is paged. The suite had driven the COMMANDS (`Page Down` becomes a page step in a paged mode, with a case saying so) and never the GESTURE. `Features.md` §1.2 gains the capability line, because the wheel half was specified nowhere. **(g) I-25, found by the user 2026-09-13 in the M-05 walk and fixed the same day:** the page step was `currentPage ± 1` in every mode, which in a two-up lands on the page already on screen — the layout does not change and `refresh()`'s dominant-page snap returns the current page to where it started, so a paged Two-Up could never leave its spread. `PageLayoutEngine::spreadStep()` now answers the step from `PagePresentation::spreadFor()` — the function whose T13 header comment had claimed `ui/viewer/` was already calling it, corrected in place — and `PageView::nextPage`, `prevPage` and `scrollOrTurnPage` all route through one `pageStep()`. Guide case **A-86**, three cases, asserts the PAGES ON SCREEN rather than the page index. `Features.md` §1.2 gains the spread-step line. |
| **T14** the Viewer commands, the status bar, the message bar | ✅ 2026-09-04 — **I-53 CLOSED**; **I-10 raised and closed**; **I-23 raised and closed 2026-09-12** (S2, found in the M-04 walk) | `ui/shell/MessageBar.{h,cpp}`, `ui/dialogs/PasswordDialog.{h,cpp}`, `MainWindow::registerViewerActions()`, and `tests/ui/{tst_viewercommands,tst_documentchrome}.cpp` (**25 new cases**, guide **A-55**'s second half, **A-57**, **A-58**, **A-59** and the new **A-80**). **Registration goes from 23 of 361 to 48** — the §8 Viewer subset `Implementation-Plan.md` § Stage 2 lists and not one id beyond it, with the absence of the rest asserted rather than assumed. The status bar shows real page numbers and a real zoom and its page box navigates; `MessageBar` raises the repair report and the protected banner; the password prompt is a modal that refuses to raise itself where there is nobody to ask. **Deviations, recorded rather than absorbed: (a) THE VIEW ROTATION NEEDED PLUMBING NOBODY HAD BUILT.** `Ctrl+Shift+=` is in this document's own Stage 2 binding list, and `RenderRequest` had no field for a rotation, `TileGrid` no way to swap a page's bounds, `PagePresentation` no parameter, and `PdfiumRasterizer` passed a hard-coded `rotate=0`. All four were extended, `viewRotation` joined `RenderRequest`'s equality AND its hash in the same change (D2-12), and `core/Geometry.h` gained `rotateBy`/`combine`/`swapsAxes` — a page's own `/Rotate` and the session's rotation are two rotations that must be added, and 270 + 180 is 450, which is not a `PageRotation`. **(b)** `ui/dialogs/PasswordDialog` is a file the plan's §4 manifest did not list. **(c) `H`/`V`/`Z` are registered and `ui/viewer/ToolController` is NOT built** — this document lists the three bindings for Stage 2 and the FileMap tags the tool state machine and its seventeen tool classes ○ (S4), so `PageView` holds which tool is armed, its cursor and what a drag does, and nothing more. **(d)** `repairReport()` moved onto `core::IDocumentBackend`, at its first consumer, exactly as T13 said it would. **Six negative controls run, and one refused to fail: the case asserting that rotating the view "re-keyed the tiles" was really asserting that new tiles were requested** — true anyway, because a rotated page is a different shape with different tile rectangles. `TilePresenter::heldRequests()` exists because of it and the case now reads the keys. **I-10:** Stage 1's I-53 note said the mechanism for *"not available for this document"* already existed as `Registration::tooltipKey`, and it does not: that field is the tooltip an ENABLED control shows, and reusing it put "needs a document to be open" on every enabled Viewer button. `unavailableKey` is a second field. **I-23, eight days later:** `Ctrl+Shift+T` was registered through the `registerMode` helper, which SELECTS a fixed `LayoutMode` — so the chord `Features.md` §8 calls *Toggle Two-Up* set `TwoUpContinuous` on every press and could never leave it, while `checked` made it look like a toggle to `ActionFactory` and to the ribbon. `ViewTwoUp` now has its own registration, the mirror of `ViewTogglePageMode`: each chord owns one axis of `LayoutMode` and preserves the other. **The case that covered it pressed the chord once** — convention 22 — and the two replacing it press twice from each of the four modes and were run red against the unfixed handler first. Registration stays at **48 of 361**: this moved an id between helpers rather than adding one. |
| **T15** the sixteen settings keys | ✅ 2026-09-04 — **I-11 raised** (S3, open by decision) | Sixteen rows in `core/settings/SettingsKeys.def`, two entries in `PreferencesDialog::shippedPages()`, and **no widget code at all** — which is convention 17 doing what it was built for, and the whole of A-64. `tests/unit/tst_stage2settings.cpp` and `tests/ui/tst_viewsettings.cpp` (**14 new cases**, guide **A-63 … A-65** and the new **A-81**), plus three in `tests/render`. **Five of 26 Preferences groups now ship.** **Deviations, recorded rather than absorbed: (a) `FitMode` MOVED from `ui/viewer/` into `core/render/DpiPolicy.h`.** `pageDisplay/defaultZoom` is a settings key and `SettingsKeys.def` may only name `core/` types; the alternative was a second enum in `core/` listing four of `FitMode`'s five values, which is the two-lists shape that file's own rule 2 exists to prevent. It belongs beside `Zoom` in any case. **(b) FOUR DEFAULTS CHANGED OWNER**, each from a struct that had said in its own comment that the row would take over — and a case now asserts the two still agree. **(c) `pageDisplay/lineWeight` is DECLARED AND INERT (I-11)**: `Features.md` §1.2 defines it as *"draw hairlines at their true width instead of a minimum of one pixel"*, and PDFium always enforces the one-pixel minimum with no `FPDF_*` flag over it. Measured rather than asserted — `tests/render`'s A-81 walks every flag `RenderFlags` can express and shows none of them moves the solid ink coverage of `corpus/ordinary/vector-hairlines.pdf` by more than 1.2 %. Same shape as I-06. **(d) A-65's criterion is CORRECTED rather than narrowed**, as A-18's was at T7: three of its five toggles change pixels and are measured over the real engine (51,279 / 9,048 / 23,928), `transparencyGrid` changes no pixel of any raster because it is what is BEHIND the page, and `lineWeight` changes nothing anywhere. **(e) `performance/renderWorkers` takes effect at the next launch**, because a worker pool's threads are made once; the cache caps are live. **Five negative controls run across nine cases.** **And the image measurement is worth the reader's time**: two straightforward ways of writing it measured ZERO and would have been reported as a finding about PDFium — the flag only bites where a source pixel is resampled hard, and which fixture at which scale does that is a property of the corpus. Sweeping every image fixture at three scales found 23,928. |
| **T16** fonts and glyphs | ✅ 2026-09-05 — **I-13 raised and closed (S2, in the product)**; **I-02, I-04 and I-12 closed** | `pdf/render/FontResolverBridge` (the `FPDF_SYSFONTINFO` hook, version 2), `pdf/render/GlyphCache`, `platform/win/WinFontResolver` — **the fifth of seventeen OS interfaces** — and two files this stage's plan did not list: **`core/render/FontSubstitution`** and a **thirtieth corpus file**. **+35 cases** (642 → **677**, one still skipped): 17 `unit.` over the policy, 7 `platform.` over the resolver, 7 `render.` over the chain, and 4 gate cases. **Guide A-66 green and it NAMES the substitutes** — `RawPageAbsentSerif` → Times New Roman, `RawPageAbsentSans` → Segoe UI. **Deviations, recorded rather than absorbed: (a) THE SUBSTITUTION POLICY IS IN `core/`, not in `platform/win/`.** `platform/` makes no policy decisions and choosing a family is almost nothing but policy — so the ordered candidate list, the `/BaseFont` normalization and the base-14 predicate are a `core/render/` file, and A-66's *our policy rather than the engine's* is asserted by `tests/unit` with no display, no Windows and no engine linked. **(b) `pdf/render/` MAY NOW INCLUDE `platform/platform.h`**, through a new `pdf-render` `layer-lint` entry rather than by widening `pdf`. The font hook is where the engine boundary and the OS boundary physically meet; the narrowing is the point, because `pdf/cos/` is portable BY RULE and must not inherit it. Two gate cases, and the control was verified by widening the wrong layer and watching it fail. **(c) `rawpage_render_tests` LINKS `rawpage_platform`** — issue **I-09**'s shape again: the chain is three libraries, no existing target could hold more than one link of it, and the arrangement the product runs would otherwise have had no case. **(d) `WinAcceleratedSurface` was NOT built and its row is re-tagged ○ (S2) → ○ (S4)**, on the Stage 2 guide's own terms: accelerated presentation is Stage 4's *if A2 says it is needed*, and A2's probe is 0.532 ms against a 16.667 ms budget. **(e) vcpkg's `text` feature was SPLIT rather than turned on (I-12)** — into `fonts`, `shaping` and `spelling`, one per owning stage — because T16's own `objdump -p` clause measured that Stage 2 links none of it: PDFium bundles its own FreeType and substitution reaches the OS through DirectWrite. That also moves the GPL `iconv.exe` out of the tree until Stage 13, which closes **I-02**. **(f) `i18n-lint` gained an `unknown-key` rule (I-04)** and the eighteen keys it found were written in both locales. **AND THE FINDING THAT MATTERS IS I-13**: the first run of the CONTROL case — not the case the task was written around — measured the bridge resolving **Helvetica to Segoe UI**, because PDFium asks its font hook about the base-14 faces too and four files in this project had said it would not. Unfixed, T16 would have changed how every page of every existing document renders, and invalidated T17's baselines before they were captured. **Two negative controls planted and run**, and the second answered a question the cases could not answer by inspection: making `isStandardFontName()` answer false reddens three cases — two `unit.` and the base-14 control in `tests/render` — so the guard against I-13 recurring is a check rather than a comment; and disabling `GlyphCache`'s hit path reddens the **second-document** case and **not** the second-tile one, because a second tile performs no font mapping and therefore never asks the cache. That means the second-tile case's cache assertion is *implied by* its mapping assertion rather than independent of it, which is recorded at the case (conventions 7 and 19). |
| **T17** golden images | ✅ 2026-09-06 — **I-14 and I-15 raised and closed (both S3, both in T17's own harness)** | `tests/golden/` (`GoldenSuite.{h,cpp}` + `tst_golden.cpp`, a **fifth test target**), `tools/golden-update/`, and `golden/` — **198 reference rasters over 56 corpus files and 66 pages at 96 / 144 / 192 DPI**, with `manifest.json` (hand-authored), `captured.json` (tool-written), `README.md` and an append-only `UPDATES.md`. **+10 cases** (677 → **687**, one still skipped), green on `x64-debug` and `x64-release`; `checks` clean at **270 files / 1,865 includes**. **Guide A-48, A-49, A-50 and A-51 all ✅, and A-82 is new.** The margin worth recording: a re-render of the unchanged corpus differs from its baselines in **zero pixels** at all three resolutions, so the whole of both thresholds is headroom rather than a budget already spent. **Deviations, recorded rather than absorbed: (a) A FIFTH TEST TARGET**, `rawpage_golden_tests`, rather than three more files in `tests/render`. The golden suite needs QtGui for the PNG codec, and `tests/render`'s separateness rests on it linking no Qt GUI library at all — that is how *the engine needs no display* is established rather than assumed, and folding a codec in would have retired it silently. Its CTest prefix is `render.golden.`, inside the `render.` namespace, so CI's non-empty-prefix loop covers it the moment **T18** adds `render` to that list. **(b) The render, the codec and the comparison live in `tests/golden/GoldenSuite.cpp` and `tools/golden-update` COMPILES THAT FILE IN.** One renderer, one encoder, one definition of *the same*: two copies would produce a tool writing files the suite could not accept, and the failure would read as a rendering regression rather than as two programs disagreeing — `tools/ribbon-validate`'s one-parser argument in a second subject. The direction is the one `Architecture-FileMap.md` §3.17 already declared. **(c) THE MANIFEST IS DATA AND IS HAND-AUTHORED**, with `--survey` to measure the corpus and print a skeleton, and `--check-manifest` to assert every corpus file is a baseline case or an excluded-with-a-reason row in both directions. A generated manifest would be a second copy of the corpus with no reviewer. Five files are excluded, each because the engine will not hand us a page: four it refuses and one that opens with zero pages. **(d) 8-BIT GREY WHERE IT IS LOSSLESS.** 171 of the 198 baselines have `R == G == B` in every pixel; detected exactly and expanded back on read, which took the committed tree from 30.5 MB to **13.1 MB**. `QImageWriter::setCompression(9)` was tried and REVERTED — Qt reads that value on its own scale for PNG and it took the tree to **516 MB**, measured rather than reasoned about. **(e) THE PNG ENCODER IS DELIBERATELY NOT PINNED**, unlike `corpus/`'s byte-identical rule: the comparison is over decoded PIXELS and never over file bytes, so a Qt upgrade can produce a `git` diff but cannot turn a green suite red. **(f) A-51 gained a RUNTIME half.** Configure already refuses a mismatched pin (`cmake/PdfiumAssert.cmake`); `captured.json` records the engine and a case compares it with `pdf::render::version()`, which covers the one thing configure cannot see — a `pdfium.dll` swapped beside the executable after the build, which would otherwise appear as pixel drift across all 198 baselines at once. **(g) A-82 is a new numbered case** rather than a control: if T16's base-14 decline is ever removed, every baseline moves and the failure reads as a renderer regression, so the capture log is asserted to contain only engine-internal font outcomes except on cases the manifest declares `machineDependent`. **THE TWO FINDINGS ARE BOTH IN THE HARNESS, AND THEY FAILED IN OPPOSITE DIRECTIONS. I-15 went red on its first run**: the perturbation control greys a render to prove the comparison can fail, and it was aimed at `ordinary/text-a4.pdf` — black ink on white paper, where greying is the identity. Repointed at a colour page: 165,413 differing pixels of 891,662. **I-14 stayed green and recorded nothing**: the capture marks the font-bridge log, renders, and keeps the rows in between, and the mark was taken *after* `open()` — but PDFium resolves some fonts while opening, so the one corpus file whose purpose is to have substitutions recorded none, and the machine-dependence guard was never armed. Found by reading the tool's output rather than by anything going red, which is Stage 1's I-29 exactly. **And fixing it surfaced a fact nobody had predicted**: `malformed/bad-name-hex-escape.pdf` substitutes too — its broken `#`-escape truncates `/Helvetica` to `/Helve`, which is not a standard-14 name — so it is the **second** `machineDependent` case, declared because the capture said so and not because anyone foresaw it. **Nine negative controls run**, each verified to fail for its own reason: a moved baseline, a corpus file with no row (through both the suite and the tool), a manifest row naming no file, an exclusion with no reason, a capture log from another engine, an undeclared machine-resolved face, and the skip path — which must **skip by name** rather than fail, with its own control proving the same case still compares something when the log is untouched. |
| **T18** A2 re-measured, the carry-overs, CI, doc lockstep | ✅ 2026-09-06 — **A2 SETTLED (`go`); O7, O10 and D2-14 all ANSWERED BY MEASUREMENT; Stage 1's A-25 and A-26 finally run** | `tests/bench/bench_pipeline.cpp` (a third bench), `tests/unit/tst_actionalias.cpp` + `tests/unit/fixtures/ActionAliasFixture.def`, `--list-actions` across `app/CommandLine` and `app/main.cpp`, a table-parameterized lookup in `core/cmd/ActionId.h`, and four `.github/workflows/ci.yml` changes. **+9 cases** (687 → **696**, one still skipped): 7 `unit.` over the alias algorithm, 1 `unit.` over the new switch's parse, 1 `bench.`. `checks` clean; `gates` preset 94/94. **THE HEADLINE IS THE MARGIN. A2 = `go` — p95 14.18 ms against 16.667 ms, which is 1.18× headroom where the probe reported 31×.** *(An upper bound: corrected 2026-09-14 by Stage 2 I-41 to 13.03–13.20 ms, about 1.27×.)* Nothing regressed: the probe timed **service time** inside the engine lock, this times **queued latency** from `enqueue()` to delivery over a real 1920×1080 viewport served by one worker. The comparable pair is the probe's 0.532 ms p95 and this run's **0.356 ms cost per tile**, and those agree. **That gap is exactly what D2-1 existed to expose**, and it is Stage 1's R-9 lesson paying off a second time. **Deviations, recorded rather than absorbed: (a) A SECOND BENCH BINARY, NOT A REWRITE OF `bench_render`** — whose own CMakeLists said *“T18 rewrites this file”*. Rewriting it would have made the probe's committed figure un-re-takeable and deleted `--unsafe-parallel`, which is **I-05's on-demand reproducer** and exists because a finding needing a source edit to reproduce gets re-litigated. `bench_pipeline` also **does not link raw `pdfium`**, so it cannot reach past the facade even by accident. **(b) THE FIRST VERSION OF THE BENCHMARK MEASURED THE WRONG QUANTITY AND SAID SO IN 42 ms.** It enqueued the WHOLE PAGE — 140 tiles at 288 dpi — and called the result a latency, which reads as a failed A2 and is not one: it was measuring how long a page takes, reported per tile. A user waits on a viewport, so the subject is a 1920×1080 viewport and `--whole-page` reports the other number (43.9 ms, three frames, which is why tiles stream rather than pages). **(c) `core/cmd/ActionId.h` gained a table-parameterized lookup**, `actionFromStringIdIn(descriptors, aliases, id)`, with the shipped functions as one-line wrappers. Alias rows could not be added to `ActionId.def` — they say *this name used to mean that action* and are written to users' settings files, so inventing three would be fiction in a shipped table. **(d) `--list-actions` NEEDS THE WINDOW**, not just the services: registration happens in `MainWindow`'s constructor (convention 14), so a dump taken earlier would report 361 declared and 0 registered — confidently wrong. **(e) HALF OF A-25's CRITERION IS CORRECTED RATHER THAN DROPPED**, the call T7 made about A-18 and T16 about A-67: *declared-with-owning-stage* is not printed, because a stage column on 361 `.def` rows is a second copy of the roadmap that no reader of this document would think to update. **(f) A FOURTH CI JOB, `portable-fuzz`, which no debt list had named.** A-25 and A-26 were recorded as *Linux jobs* since T5 and T6 and **there was no Linux fuzz job** — a deferral whose owner did not exist, I-30's shape. Both targets now build, are asserted to have LINKED before being run, and run seeded on every push; the ten-minute soak is deliberately not per-push and is Stage 3's. *(Corrected 2026-09-14: the soak was added in Stage 2 as `.github/workflows/fuzz-soak.yml` and ran clean in run 34781013867.)* **THE FINDING THAT WOULD OTHERWISE HAVE GONE UNNOTICED: the two alias cases in `tst_actionregistry.cpp` assert nothing.** Every assertion in both sits inside `for (const ActionAlias& alias : kActionAliases)`, a loop over an empty array — they passed, they were counted, and their bodies never ran. Its neighbour *“a live id always wins over an alias of the same spelling”* walked 361 live ids and found each of them, which is true whatever the alias path does. **Three controls run**, two of which fire as COMPILE ERRORS because the new assertions are `STATIC_REQUIRE`: reversing the resolution order gives `static assertion failed: resolve("file.open") == ActionId::FileOpen` — an assertion that could not have existed before the fixture supplied a colliding row — and emptying the fixture reddens the row-count check. The third found that the wrapper-agreement case is **latent** today and says so in its own file. |

Carries risk **A2** as a hard **go/no-go**: p95 per-tile latency
must be under ~16 ms at the target DPI on the reference machine. If it is not, the tiling policy in
[`Tech-Stack-Plan.md` D10](./Tech-Stack-Plan.md#2-decisions-confirmed) is revisited **before**
anything is built on top of it. **The plan measures A2 twice** (D2-1): a throwaway probe over raw
`FPDF_RenderPageBitmap` at **T3**, early enough that a no-go reshapes the architecture rather than
replacing it, and a final measurement through the shipped `RenderScheduler` at **T18** — which is
Stage 1 T15's lesson, where the ribbon's R-9 figure had to be re-measured against the shipped ribbon
because a spike's ribbon is as small as its author made it. Stage 2 also owns **O7** and **O10** (§8).

**Two corrections were made to this document set while the plan was written**, both recorded as
**I-01** in the Stage 2 guide's §8. `Architecture-FileMap.md` tagged the six `pdf/cos/` **reader**
rows (fifteen files) ○ (S3) while the component list below claims them and Stage 3's does not; and it
tagged `Revision.h`, `Scope` and `Page` ○ (S3) while their own *Interacts with* column names Stage 2
consumers. This document owns stage ordering, so the FileMap was corrected — including `PageTree`'s
new split tag **○ (S2 read, S3 edit)**, which follows `Outline`'s existing precedent.

**The corpus is entirely generated — decided by the user 2026-08-31 (plan D2-11), which withdrew
precondition P-2 outright.** Nothing in `corpus/` is borrowed or downloaded, so no committed file
needs licence clearance — a gap no gate closes, since `license-audit` governs linked dependencies
and says nothing about data — and every file regenerates byte-identically, which matters because a
PDF xref stores absolute byte offsets. **It also bounds the exit criterion below**: *“every corpus
file opens and renders correctly”* is a claim about **consistency against our own output**, not
about **fidelity to real-world PDFs**. Real-producer constructs — scanner CCITT quirks, InDesign
transparency, unusual colour spaces — are outside Stage 2's evidence. That limit is a
Definition-of-Done item (plan §13.6) rather than a footnote, and the trigger for revisiting it is
the first real-world rendering bug the corpus cannot reproduce.

**Objectives.** Open a PDF and look at it: scroll, zoom, all four page-layout modes, correct
rasterization at each monitor's physical DPI, and a window that survives being dragged between
monitors at different scaling.

**Components & responsibilities.**
- `core/backend.h` — `IDocumentBackend`, `IPageRasterizer`, `ITextExtractor`, `MutationSink`,
  `ISigner`, `IOcrEngine`, `IIndexer` all **declared** now, even where unimplemented, so later
  stages add no boundary.
- `core/render/DpiPolicy` — `Resolution resolve(Zoom, MonitorScale, PhysicalDpi) const`,
  `bool requiresRerender(Resolution, Resolution) const`. **Built at T10.** `Resolution` turned out to carry three
  numbers rather than one: the raster DPI (the monitor's *physical* DPI times the zoom, because Actual Size is
  100 % on the glass), the device-pixel ratio for presentation, and an `upscale` that compensates for the
  `maxRenderResolution` cap — capping without it silently shrinks the page and reads as a zoom bug.
- `core/render/TileGrid` — `TileSet tilesFor(const Page&, Resolution, TileRect viewport, held) const`.
  **Built at T10**, with the viewport in integral device pixels rather than a `QRectF`: a tile is a literal
  buffer, and two adjacent tiles that rounded a shared edge independently disagree by a pixel. `held` is what the
  caller already has, and is what makes the evict set computable by a class with no memory of the last frame.
- `core/render/RenderRequest.h` — the value type whose equality **is** the cache key.
- `core/render/RenderScheduler` — `RenderTicket enqueue(RenderRequest, RenderPriority)`,
  `void cancel(RenderTicket)`, a token-based `subscribeReady` and a `deliverReady()` PUMP. **Built at T11**, and
  the two departures are the design: there is no `Signal` type in `core/` (Stage 1 T5 recorded the same thing for
  `Settings`), and delivery happens on the thread that calls `deliverReady()` so that **no subscriber is ever
  entered from a worker** — which is what lets `RenderCache` be lock-free. A ticket is one caller's interest in
  one tile, so two viewports over one tile coalesce to one rasterization and either can cancel without taking the
  other's pixels. **T11 closed I-05 by choosing ONE render thread with a queue**; see the task row above.
- `core/render/RenderCache` — `std::optional<RasterImage> lookup(const RenderRequest&) const`,
  `void put(...)`, **`void invalidate(DocumentHandle, Scope, Revision)`**. **Built at T11**, and the handle is
  not optional: a `Scope` names pages and objects and says nothing about which document they belong to, while O7
  leaves open whether one cache serves several. Invalidation also **re-keys** what the scope does not touch to the
  new revision — without that, the revision inside every key would have moved and `Scope` would buy nothing.
- `core/render/PagePresentation` — `PageLayout layout(const PageTree&, LayoutMode, Resolution, int currentPage)
  const`, over `core::DeviceRect`. **Built at T10**, with three corrections to the line this list used to carry:
  a `Zoom` alone does not determine a pixel (the monitor's physical DPI does too, and `DpiPolicy` is where those
  combine); Single and Two-Up show ONE page or ONE spread, so without a current page they are indistinguishable
  from their continuous twins and the four modes are two; and `QRectF` says nothing about which coordinate space
  it is in, which is the mix-up `core/Geometry.h` exists to prevent.
- `core/doc/RepairReport`, and **four vocabulary types brought forward from Stage 3** because the three
  signatures above already name them: `Revision.h` and `Scope` (`RenderCache::invalidate`, and the
  `Revision` `RenderRequest` carries), `Page` (`TileGrid::tilesFor`), and a **read-only** `PageTree`
  (`PagePresentation::layout`) — construct, index, page size, rotation and label here; insert, move,
  remove and duplicate at Stage 3. See `Stage2-Rendering-Plan.md` **D2-5**.
- `pdf/cos/` first half — `CosObject`, `Lexer`, `Parser`, `XrefTable`, `XrefRepair`,
  `filters/{Flate,LZW,Ascii85,AsciiHex,RunLength,Dct,Jpx,Ccitt,Jbig2}` — **nine**; `CryptFilter` goes to Stage 10 with the `StandardSecurityHandler` whose key it needs (D2-4).
- `pdf/render/PdfiumInit`, `PdfiumDocument` (`Result<Handle> open(path, PasswordCb)`),
  `PdfiumRasterizer` (`Result<RasterImage> raster(const RenderRequest&)`), `GlyphCache`,
  `FontResolverBridge` — **plus `core/render/FontSubstitution`, which this list did not name.**
  *Which* face substitutes is policy and `platform/win/` makes none, so the candidate list is a
  `core/` file; that is also what makes it assertable with no display and no engine (T16).
  **The three signatures above said `QImage` until 2026-09-01 and could not have been built as
  written** — `QImage` is QtGui, `core/` links `Qt6::Core` only and `layer-lint` has denied it there
  since Stage 0, so an interface declared in `core/backend.h` cannot name one. `core::RasterImage`
  is the first-party buffer that replaces it (width, height, stride, pixel format, bytes), which is
  also what plan **D2-9**'s *buffers and callbacks only* already required; `ui/` wraps those bytes in
  a `QImage` at the far side, where QtGui is allowed. Recorded as **I-03** in the Stage 2 guide's §8
  and fixed at **T1**, in the header that declares the interface.
- `pdf/DocumentBackendImpl`, `pdf/BackendFactory` (`std::unique_ptr<IDocumentBackend> createBackend()`).
- `ui/viewer/PageView`, `TilePresenter`, `PageLayoutEngine`, `ViewportController`.
- `ui/shell/MessageBar`.
- `platform/win/WinDisplayTopology` (`QVector<MonitorInfo> monitors() const`, `Signal<> changed`),
  `WinFontResolver`, `WinPowerSession` — **three, not four.** `WinAcceleratedSurface` was moved to
  **Stage 4** at T16 rather than built, on the Stage 2 guide's own terms: accelerated presentation
  is Stage 4's *if A2 says it is needed*, and A2's probe is p95 0.532 ms against a 16.667 ms budget.
  Its `platform::Platform` member stays null, which is the vocabulary `platform.h` defines for *not
  built*, and the interface census asserts it rather than letting a stub pretend.
- `tests/golden/`, `tests/bench/`, `corpus/` (including `corpus/malformed/`), `tools/golden-update/`.

**Key bindings handled** — from [`Features.md` §8 → Viewer](./Features.md#viewer-review-mode)
(`KeyContext::Viewer`): `Ctrl+O` now functional; `Ctrl+=` / `Ctrl+-`, `Ctrl+0`, `Ctrl+1`, `Ctrl+2`,
`Ctrl+3`, `Ctrl+M`, `Ctrl+Shift+N`, arrow keys, `Page Down`/`Page Up`, `Space`/`Shift+Space`,
`Ctrl+Page Down`/`Ctrl+Page Up`, `Home`/`End`, `Alt+←`/`Alt+→`, `Ctrl+Shift+=`/`Ctrl+Shift+-`
(rotate **view**), `H`, `V`, `Z`, `Ctrl+Shift+H`, `Ctrl+Shift+T`.

**Data / settings keys.** `pageDisplay/layoutMode`, `pageDisplay/defaultZoom`,
`pageDisplay/showCoverInTwoUp`, `pageDisplay/rtlBinding`, `pageDisplay/lineWeight`,
`pageDisplay/antialiasText`, `pageDisplay/antialiasImages`, `pageDisplay/antialiasLineArt`,
`pageDisplay/transparencyGrid`, `pageDisplay/maxRenderResolution`, `pageDisplay/wheelZooms`,
`performance/renderWorkers`, `performance/tileCacheMB`, `performance/diskCacheMB`,
`performance/prefetchPages`, `performance/throttleOnBattery`.

**Cross-cutting additions.** None (no mutation yet — deliberately: read and write are proven
separately so a failure is unambiguous).

**Algorithms & notes.**
- **Tile invalidation is `Revision`-and-`Scope`-keyed, not time-keyed.** `RenderRequest` carries the
  `Revision`, so a stale tile can never be shown after a mutation lands in Stage 3.
- **Cancellation on scroll matters more than raw throughput.** A tile that scrolled out of view must
  be dropped from the queue, not rendered and discarded; the perceived-smoothness win is larger than
  any per-tile optimization.
- **`XrefRepair` runs a full-file object scan** when the xref is unusable, then rebuilds the table
  and reports every reconstruction. A file we repaired is flagged so Stage 3 can force its first
  save to Save-As — appending to a file whose structure we guessed is how originals get destroyed.
- **`DpiPolicy` distinguishes a scale change needing re-rasterization from one needing a resample.**
  Getting this wrong yields either blurry text or a stutter on every monitor move.
- PDFium is **thread-confined per worker**; `PdfiumInit` is the only place its global state is
  touched.

**Testing.**
*Automated:* `tst_dpipolicy`, `tst_tilegrid`, `tst_rendercache` (LRU, cap, scope invalidation),
`tst_presentation` (all four layout modes, cover page, RTL), `tst_cos_lexer`, `tst_cos_parser`,
`tst_cos_filters` (each filter round-trips), `tst_xrefrepair` (every `corpus/malformed/` file yields
a report, never a crash). **`tests/fuzz/` — `fuzz_cos_lexer` and `fuzz_cos_parser`, the first targets
the `portable-fuzz` preset has ever had.** `tests/golden/` establishes the baseline at three DPIs. `tests/bench/`
reports p50/p95 — **the A2 gate**. `portable-asan` runs the COS parse of the whole corpus clean
under ASan+UBSan; the raster path, which cannot leave Windows because PDFium is in it, is checked
with Dr. Memory / Application Verifier over the same corpus instead (R7).
*Manual:* → queued **`Stage2-Rendering-Testing-Guide.md`**, written before this stage's code (§5). In brief:
open every corpus file; scroll a 900-page document; all four layout modes; zoom to 6400% and to 8%;
drag the window between a 100 %- and a 150 %-scaled monitor and confirm crisp text with no layout
jump; change monitor scaling while the window is open; open an encrypted file and a repaired file.

**Exit criteria.** Every corpus file opens and renders correctly, `corpus/malformed/` included, with
no crash, no ASan finding on the portable COS run and no Dr. Memory finding on the raster run;
golden baselines committed; scrolling a large document stays
responsive; mixed-DPI dragging is correct; **A2 has a recorded verdict with numbers**.

**Deferrals.** No text selection, no search, no annotations, no editing, and no save — the document
is strictly read-only this stage. Split views, rulers, grid, guides, and full-screen come in Stage 4.

---

### Stage 3 — Document model, command journal, incremental save

*Part 3 of the first milestone — the stage that closes the risk slice.*

**Status:** 🟡 **In progress — T1 and T2 complete (2026-09-14). RISK A4's PROBE VERDICT IS `go`**, graded
from the user's Acrobat walk (guide M-03's probe walk, signed off by the user 2026-09-14) and OpenSSL —
after guide issue I-06 moved Acrobat's grading to a permitted edit. **T3 and T4 complete (2026-09-14); T5 to T10 complete (2026-09-15); T11 and T12 complete (2026-09-16); T13 CODE COMPLETE 2026-09-17** — CI run 35132580941 read (three targets linked and run 60 s, `portable-asan` 557/557, no sanitizer line), the first soak over three (run 35134357858) found guide issue **I-13** in `fuzz_cos_roundtrip`, fixed; a clean re-soak waits on a push, and M-03's product walk on the user. **A4's PRODUCT verdict is recorded as PARTIAL**: integrity through the product's own Save is met (OpenSSL, 4 of 4, 8 of 8 controls), its Acrobat rotation control awaits M-03's product walk, and a permitted edit through the product is Stage 6's — **both A4 figures hold over RawPage's own generated signatures only.** T13's fuzz target found guide issues **I-12** (before its first push) and **I-13** (its first soak), both fixed. Its execution plan ([`Stage3-Mutation-Plan.md`](./Stage3-Mutation-Plan.md), decisions
**D3-1 … D3-18**, tasks **T1 … T13**, preconditions **P-1 … P-5**) and its testing guide
([`Stage3-Mutation-Testing-Guide.md`](./Stage3-Mutation-Testing-Guide.md), **83 automated** and
**8 manual** cases) were both written on 2026-09-14, **before any Stage 3 code**, per §5.

| Task | State | What landed, what was measured, and what deviated |
|---|---|---|
| **T1** The writer's corpus | ✅ 2026-09-14 | **P-2's validator first** (`tests/gates/ResolveOpenSSL.cmake`, guide A-06's three `gates.validators.*` cases; P-2 ✅ in CI run 34826889323, whose Windows job was 744/744 with the one named skip — `Stage3-Mutation-Plan.md` §7 owns the detail). **Then the corpus:** `corpus/cmsenc.py` (DER, a deterministic RSA-2048 key derived from a public seed, a self-signed X.509 v3 certificate, a detached RFC 5652 `SignedData`) and `corpus/make-signed.py`, producing **`corpus/signed/`'s four files** — approval over a classic table, approval over an xref stream, a `/DocMDP /P 1` certification (the policy control), and approval-then-unsigned-update; plus **`ordinary/rotate-inherited.pdf`** and **`ordinary/hybrid-xrefstm.pdf`**. **67 corpus files**, all regenerating byte-identically, one `PROVENANCE.md` row each. The instrument: `tests/gates/check-signatures.py` reads signatures as bytes and asks OpenSSL, **flipped-byte controls first** — `gates.corpus.signed` gives *8 of 8 controls rejected, 4 of 4 signatures verify*, and two controls on the script itself prove it stops on a validator that accepts everything and fails on one that rejects everything; `--shapes` checks A-16 with seven detections, each driven to fire. **24 new golden baselines** (222 in all) in one filtered capture, no existing PNG touched. **`ctest` 751/751 on `x64-release` and on `x64-debug`**, one named skip (was 744: +5 `gates.corpus.signed*`, +1 `unit.`, +1 `render.`); **`gates` preset 102/102**; `checks` clean. Guide cases **A-14 … A-18 ✅**, with evidence at each. **Deviations, each recorded rather than absorbed: (a) `pdfbuild.Pdf.serialize` gained `keep_loose`**, because under `objstm=True` it packed every non-stream object — the signature dictionary included, and a packed signature dictionary has no file offsets for `/ByteRange` to name; the default changes no existing byte (`--verify`). **(b) `golden-update --filter` is repeatable**, so one decision to add six files' baselines is one run and one `UPDATES.md` entry, as A-18 asks. **(c) The shape check and its self-test are a mode of the signature script**, not a second tool, so A-15 and A-16 read signatures with one extractor. **(d) CI gains an *A-15* log step in both jobs** that runs `tests/gates/CheckSignatures.cmake` directly — `ctest --verbose` would not do, because `RunGateCase.cmake` prints a passing case's output nowhere; the wrapper finds Python as configure does, since the Windows runner's is reached through the registry. **Not yet read: a CI run carrying those steps** (Ubuntu's OpenSSL 3.0.13 has not judged these files). **Finding: Stage 3 guide I-03 (S3)** — `pdf/cos/XrefTable`'s hybrid `/XRefStm` path had **never run under test**, while `tst_xreftable.cpp`'s header and the plan's §2 both said a hand-built buffer exercised it; the new fixture's case and its in-place control are its first coverage, and the free-entry hybrid arrangement stays uncovered and named. **Signing is Stage 11**: two sentences that said Stage 10 were corrected in place. |
| **T2** `ObjectAllocator`, `IncrementalWriter`, and the A4 probe | ✅ 2026-09-14 — **A4 PROBE VERDICT: `go`** (M-03's probe walk signed off by the user) | `pdf/cos/{ObjectAllocator,IncrementalWriter}.{h,cpp}` in `rawpage_cos`, `tests/unit/{tst_objectallocator,tst_incrementalwriter}.cpp`, `tests/unit/RunA4Probe.cmake` and the `unit.a4_probe.openssl` case, eight `error.pdf.write*` keys in both catalogs. **+19 cases** (751 → **770**, on `x64-release` and on `x64-debug`, one named skip); `gates` 102/102; `checks` clean at **290 files / 2,100 includes**, i18n-lint **180 files**. Guide **A-19 … A-25, A-27 ✅** and **A-28's automated half ✅**. **A-26 ✅, read from the log of CI run 34834009577** (commit `17099e0`): `portable-asan` 454/454 with every writer case and the probe run, and zero sanitizer lines; `gates` on Ubuntu judged the signed corpus with OpenSSL 3.0.13 and agreed; Windows 770/770. That run's one T2-caused compiler warning — GCC 13's `-Wmaybe-uninitialized` inside `<regex>`, used by the probe case — was removed by reading `/ByteRange` with `pdf/cos/Parser`, and the coverage job's stale *"the 198 baselines"* became *"every golden baseline"*, a count nothing re-derived. **THE PROBE'S INTEGRITY HALF:** the writer alone wrote a page-1 rotation into all four `corpus/signed/` files; read back from disk, every original is the unchanged prefix of its output; OpenSSL rejected 8 of 8 flipped-byte copies and then verified **4 of 4** signatures. That is guide §3's criteria (1) and (2); (3) and (4) are Acrobat's, over `build\x64-release\a4-probe\`, and `Tech-Stack-Plan.md` §3.3 records the probe as **not yet graded**. **Deviations: (a) `append()` RETURNS THE UPDATE rather than writing a `QIODevice`** — the original is taken read-only, so the prefix property is structural; `pdf/cos/` stays std-only for `portable-asan` and `fuzz_cos_roundtrip`; and the device, its flush and its replace are D3-4's `SaveCoordinator` and `IDurableFiles`. The component entry below is corrected in place. **(b) `ObjectDelta` lives in `IncrementalWriter.h`** with its allocator, and **`serializeObject()` with it** — both named in no plan row; `FullWriter` (S8) is where sharing them becomes a question. **(c) The serializer ignores spans** and writes a changed object from its values: byte identity's subject is the objects NOT in the update, and a span inside a packed object was not trustworthy (I-04). **(d) An empty delta is refused for an ordinary update and allowed for a complete one**, whose table is itself the point of a repaired file's Save-As. **(e) `truncated-mid-stream.pdf` is refused by `appendComplete` (`writeNoRoot`)** — its repair finds no catalog — so one `requiresSaveAs()` file cannot be written at all; what Save-As does then is T8's. **Two findings, both S2, both in the READER and both fixed with a case run red first:** **I-04** — a packed object's nested values kept spans into a dead decompression buffer while the comment said they were cleared (108 of 132 assertions red on the old code); **I-05** — the lexer's reals were not correctly rounded, so the writer turned `0.3` into `0.30000000000000004` on every object it rewrote (4 of 10 red; found by the serializer's corpus round-trip over 2,229 objects). **Controls run:** A-22 with a planted second change failed in every file; the probe's instrument is A-15's, whose controls on itself were run at T1. **Not measured:** how PDFium reads a complete section written over `junk-before-header.pdf`'s physical offsets — T8's `tst_saveengine`. **THE FIRST ACROBAT WALK, and I-06 (S2):** Acrobat called the rotated probe INVALID while OpenSSL verified it; shown known answers — the untouched original (*not modified*), our writer changing `/Info` (*not modified*), our writer adding a comment (*signed revision unaltered, subsequent changes*), and a different writer's identical rotation (*INVALID*) — the cause is Acrobat's policy for these signatures, which permits only fill-in, signing and commenting, not the writer. **Decided by the user:** the probe now writes a comment (graded) and a rotation (a control) into each file — OpenSSL 16 of 16 controls rejected, 8 of 8 verified — guide §3 and M-03 corrected, D3-1 amended, and the product's permitted-edit Acrobat half carried to Stage 6's exit criteria. **THE RE-WALK AND THE VERDICT:** all three approval signatures, after the comment and a **Validate Signature**, read *"the revision … covered by this signature has not been altered; however, there have been subsequent changes"*; the certified file after the comment and all four rotations read INVALID, *altered or corrupted*. Guide §3's four criteria all hold: **`go`** — the append model stands over RawPage's generated signatures, and `Tech-Stack-Plan.md` §3.3 records it as the probe's. *View Signed Version*, walked afterwards with its control, shows each approval file's comment in the current file and absent from the signed version, and Acrobat lists that comment as the only subsequent change. |
| **T3** `rawpage_ops`: `PageOps`, `MutationSinkImpl` | ✅ 2026-09-14 | `pdf/ops/{PageOps,MutationSinkImpl}.{h,cpp}` and `pdf/ops/CMakeLists.txt` (**`rawpage_ops`**, portable, linking `rawpage_cos` and no engine, added before `pdf/`'s portable-subset return — D3-3); a **`pdf-cos`** layer entry in `tools/layer-lint/rules.json` with two positive fixtures and a control; two `gates.headless_assert.ops_*` cases; `tests/unit/{tst_pageops,tst_mutationsink}.cpp`; `tests/render/tst_pageopsengine.cpp`; four `error.pdf.*` keys in both catalogs. **+21 cases** (770 → **791** on `x64-release` and `x64-debug`, one named skip): 6 + 9 `unit.`, 1 `render.`, 5 `gates.`; `gates` preset **107/107**; `checks` clean at **297 files / 2,145 includes**, i18n-lint **184 files**. **x64-debug 791/791; x64-release 791/791** — in the full run 789, because the A4 probe's writing case and `unit.a4_probe.openssl` failed while the three `a4-probe\comment\` files were still **open in Acrobat** from the M-03 walk and Windows refused the overwrite (confirmed: exactly those three files were locked; the debug tree's copies, which nobody had open, passed). With Acrobat closed both passed — 16 of 16 controls rejected, 8 of 8 signatures verified — and the writing case now names the file and the likely cause when that happens. Guide **A-03, A-05, A-29 … A-33 ✅**. **The measurement that matters:** over **55 corpus files and 1,016 pages** both readers open, the walk and PDFium agree on the page count and, index by index, on the effective rotation and the media box size — with one exception, **I-07** (S3): `malformed/pages-loop.pdf` is 2 pages to PDFium (its single leaf shown twice through a cycle) and 1 to the walk, asserted exactly and left to D3-7's refusal at T4. **Controls run:** both `pdf-cos` positives came back clean against rules without the entry; a walk planted to drop inheritance through an intermediate node failed the engine comparison on exactly `rotate-inherited.pdf` and two headless cases. **Deviations: (a) D3-5's inherited-`/Rotate` rule refined** — a `/Rotate` equal to the inherited value is dropped whatever the on-disk dictionary held, not only when it held none, so an undo after a save writes the page's original dictionary back (A-32) rather than an explicit `/Rotate`; recorded in the plan's D3-5 row and `PageOps.h`. **(b) A-29's engine half is in `rawpage_render_tests`**, which gains `rawpage_ops`, because the headless suite may not link PDFium; the plan's §5.6 row is corrected. **(c) `MutationSinkImpl` owns its `XrefTable` and not the bytes**, opens through `openWithRepair()` so a sink sees the same objects as the rest of the reader, and exposes `adopt()` for the save path; nothing in `core/` holds it yet (T4). **(d) The walk passes inherited `/Rotate` and `/MediaBox` DOWN rather than following `/Parent` up**, and stops at `kMaxDepth` 256; `/Count` is never read. |
| **T4** `core/doc/`: `Document`, `DocumentId`, `Permissions`, rotation | ✅ 2026-09-14 | `core/doc/{Document,Permissions}.{h,cpp}`, `core/doc/DocumentId.h`, `PageTree::setRotation`; `core/backend.h`'s `IDocumentBackend` gains **`permissions()` and `documentId()`**; `pdf/DocumentBackendImpl` builds a `pdf/ops/MutationSinkImpl` per open document, assesses permissions and takes the identity in `open()`, and hands out the sink only for an editable document (`rawpage_pdfium` links `rawpage_ops`); `tests/unit/{tst_document,tst_permissions}.cpp`, `tests/render/tst_backendedit.cpp`; six `error.edit.*` keys in both catalogs. **+16 cases** (791 → **807** on `x64-release` and `x64-debug`, one named skip); `gates` 107/107; `checks` clean at **305 files / 2,195 includes**, i18n-lint **189 files**. Guide **A-34 … A-37 ✅**. **The census reads 3 of 7** in the product's startup log. **What the pre-flight does over the corpus:** all five encrypted files open in the engine and are refused `error.edit.encrypted` with no sink; every ordinary and signed file is editable; `malformed/pages-loop.pdf` is refused `error.edit.pageCountDisagrees` (`2/1`) — I-07's disagreement, refused as D3-7 intended. **Control run:** with the rotation comparison removed from `Permissions::assess`, the planted rotation case failed. **Deviations: (a) `IDocumentBackend` gained two accessors** the plan did not list. D3-7 puts the assessment in `core/` and the two readings live only in the backend, so the backend calls `Permissions::assess()` once at open and exposes the result; D3-16's identity is *"taken at open"*, which is only possible where the bytes are read. Both are nullable like `pages()`, and the two test backends return null. **(b) Five refusal kinds, not three**: encrypted, no page map, and disagreements on page count, rotation and media box size — the size standing in for page ORDER, since neither reader exposes an object number the other shares; plus `error.edit.notAssessed` for a document no backend has spoken for. **(c) `PageTree::setRotation(index, degrees)`**, flow D's name, rather than the component list's `rotate(PageIndex, int)`; absolute degrees, normalized as `Page` normalizes. **(d) A-34's engine half moved forward** from `tst_saveengine` (T8) into `tst_backendedit`, since the shipped backend could already be hydrated from. **(e) The backend runs the first-party reader twice per open** — once in `PdfiumDocument` for the repair report, once in the sink — a cost T8's save measurement will see rather than one T4 optimized away. **CI for T3 (run 34858266515) failed on infrastructure**: the Windows job's PDFium download and the fuzz job's CMake download both returned HTTP 504 from GitHub before any build; the failed jobs were re-run, and the ASan job of that run passed 474/474 with every `rawpage_ops` suite named and no sanitizer line. |
| **T5** `Command`, `Commands.def`, `CommandRegistry`, `CommandJournal`, `RotatePageCommand` | ✅ 2026-09-15 | `core/cmd/{Command.h,Commands.def,CommandRegistry.{h,cpp},CommandJournal.{h,cpp}}`, `core/cmd/commands/PageCommands.{h,cpp}`, `Document::modelHash()`; `tests/unit/{tst_journal,tst_command_serialize,tst_reversibility}.cpp` and `tests/unit/CosBackend.h`; `command.page.rotate` and four `error.command.*` keys in both catalogs (**654** `<source>` entries per catalog, was 649). **+16 cases** (807 → **823** on `x64-release` and `x64-debug`, one named skip); `gates` 107/107; `checks` clean at **317 files / 2,277 includes**, i18n-lint **197 files**. Guide **A-38 … A-44 ✅, A-45's Windows half ✅** — its `portable-asan` half waits for a CI log. **Reversibility over the corpus:** *270 combinations over 59 editable corpus files, 0 failures*; the 61 candidates are every PDF outside `encrypted/` minus `malformed/pages-loop.pdf` (refused by the shipped backend, guide I-07), and the first-party writer did not open two, `malformed/objstm-corrupt.pdf` and `truncated-mid-stream.pdf`. The harness is a function returning its failures, and **A-44 runs it over four planted liars** — a revert that restores nothing, one that restores only the model, one that restores only the sink, and a command that does nothing — and it names each. **Nine controls run, each red with its plant and green without:** a revert turning the same way; a revert forgetting the sink; dirty computed from the revision; `run()` without the pre-flight; `run()` recording a failed apply; no partial-application check; groups not gathering; 45° accepted by `deserialize`; the zh-TW `command.page.rotate` key renamed. **The first pass of those controls was wrong and was re-run:** the script restored each file by moving a backup back, which gave it an mtime older than the planted object, so ninja kept the planted build — four controls ran with an earlier plant still compiled in, and one of them failed a case its own plant could not reach. The script now touches the restored file and proves each tag green on the clean build. **Deviations: (a) `Command` has a sixth method, `operation()`**, beyond `Tech-Stack-Plan.md` §5.3's `name/scope/apply/revert/serialize`: `Permissions::may(Operation)` has to be told which edit is attempted, and the journal asks before `apply()` runs. **(b) `CommandRegistry` is generated from `Commands.def` and static**, not the `add<T>()` this plan sketched — D3-12's table is what `mutation-lint` reads, and a registry filled by calls has a call to forget. **(c) `run()` returns `Status`**, not `bool`, so a refusal's catalog key reaches `ui/`; `undo()`/`redo()` stay `bool`. **(d) `markSaved()` takes no argument** — the saved position is the journal's own. **(e) `RotatePageCommand` is a RELATIVE quarter turn** (flow D's `{page, +90}`), not a target angle, so its inverse is exact in any state; −90 is normalized to 270, and a serialized page index above ISO 32000-1 Annex C's object limit is refused. **(f) i18n-lint's `command` key family joined at T5** rather than with the undo menu at T10, because `name()` returns `command.*` keys from the first command on (`tools/i18n-lint/rules.json` says so). **(g) T4's in-file test backend became `tests/unit/CosBackend.h`**, with `plantReading()`, so three suites share one real sink. **(h) `modelHash()` is encoded by hand, not through `QDataStream`**: layer-lint's filesystem-write rule matched the stream's `QIODevice` open mode on an in-memory buffer, and a waiver would have put a false entry in the census. **(i) The harness requires every `apply()` to change the hash or the delta**, a criterion the guide did not state, because without it a command that does nothing passes. **What T5 does not do:** write the recovery journal (T9) — so A-40's *journal lines* are not claimed — or save (T8); `markSaved()` is what the save path will call. |
| **T6** `tools/mutation-lint` | ✅ 2026-09-15 | `tools/mutation-lint/{main.cpp,MutationLint.{h,cpp},CMakeLists.txt}`; `cmake/RawPageChecks.cmake` runs it; `tests/gates/fixtures/mutation-lint/` (seven trees) and nine `gates.mutation_lint.*` cases. **+9 cases** (823 → **832** on `x64-release` and `x64-debug`, one named skip); `gates` preset **116/116** (was 107); `checks` **names and runs four** — *"Running enforcement gates (layer-lint, license-audit, i18n-lint, mutation-lint)"* — clean at **320 files / 2,297 includes**, i18n-lint **197 files**, mutation-lint *"1 command(s): RotatePageCommand; 1 table row(s); 95 test file(s) scanned"*. Guide **A-08, A-10, A-11 ✅**. **How the precision was shown rather than asserted:** every failing tree holds three commands and one defect, and its case checks that no sibling class and no other rule is reported; each defective tag also appears where a test does not — a comment, an `INFO()` string, a case-misspelling, a file under `tests/gates/` — and must not count. **Eight controls, each red:** five planted in the gate (a tag counted anywhere in a file; `tests/gates/` scanned; comments not stripped; only a same-line base recognized; a base merely starting with `Command` counted), and three on the real tree with no rebuild (the revert tag renamed; a `CropPageCommand` planted under `core/cmd/commands/`; the table's header column misspelled). **Deviations: (a) a fourth rule, `row-without-class`**, beyond D3-12's three: the table's header column is read by nothing the compiler runs — `CommandRegistry.cpp` lists its includes by hand — so a wrong one was found nowhere. **(b) Exit 2 for an unreadable row**, not a finding: a row the gate cannot parse is a row it has not checked. **(c) `tests/gates/` is excluded from the tag scan**, because the gate's own fixtures carry tags naming fixture classes. **(d) A tag counts only inside a `TEST_CASE`/`TEST_CASE_METHOD`/`SCENARIO`/`TEMPLATE_TEST_CASE`'s arguments**, a precision the plan did not specify. **What it cannot see**, printed by `--explain`: a class deriving from `Command` through an intermediate base, a macro-declared class, and whether a tagged test tests anything — a lying revert is `tst_reversibility`'s (A-44), the second of D3-12's two mechanisms. The stale *"three gates"* claims in `CLAUDE.md`, §1.3 above, `Tech-Stack-Plan.md` §5.6, `tools/CMakeLists.txt`, the root `CMakeLists.txt` and two tool headers were corrected in the same change; the Stage 1 and Stage 2 guides' *three* are results from named runs and stay. **CI run 34873994971 (T6's push) FAILED on the Linux `portable-asan` job**: GCC 13 with ASan at `-O2` raised `-Wmaybe-uninitialized` inside libstdc++'s `<regex>`, which `-Werror` made fatal — the other three jobs passed, and no build on this machine can raise that warning (Stage 3 guide **I-08**). **Fixed** by replacing the gate's regexes with a hand-written scanner, so it includes no `<regex>`; the nine cases and five gate controls were re-run on the rewrite, and **CI run 34876524852 passed all five jobs** — `portable-asan` 509/509 with no sanitizer line, `Gates` and Windows each naming four gates. The same day's reading of T5's run 34870534757 met **A-45's sanitizer half** (500/500 under ASan+UBSan, no sanitizer line). |
| **T7** `platform::IDurableFiles`, `WinDurableFiles` | ✅ 2026-09-15 | `platform/platform.h` gains **`IDurableFiles`, the eighteenth interface** (`syncToDisk`, `replaceFile`), `platform/win/WinDurableFiles.{h,cpp}`, wired in `PlatformFactory`; `tests/platform/tst_durablefiles.cpp` (eight cases) and the census case renamed *"census: the Windows bundle implements six of eighteen interfaces"*; `ReplaceFileW(`, `MoveFileExW(` and `DeleteFileW(` in `layer-lint`'s `filesystem-write` patterns with a fixture each and a comment control; seven `error.file.*` keys in both catalogs (**661** per catalog, was 654). **+12 cases** (832 → **844** on `x64-release` and `x64-debug`, one named skip); `gates` preset **120/120** (was 116); `checks` clean at **323 files / 2,314 includes**, i18n-lint **199 files**. Guide **A-09, A-46 … A-49 ✅**. **The startup log** (`rawpage.exe --dump-ui-strings`, offscreen): *"platform: Windows bundle assembled -- 6 of 18 interfaces implemented"*. **The waiver census is 12**, re-derived from `layer-lint --explain`: the eight before, plus four in `WinDurableFiles.cpp` — one per call spelling (`CreateFileW` for the flush's handle, `ReplaceFileW`, `MoveFileExW` through one helper for both moves, `DeleteFileW`). **Measured on this machine, not assumed:** a target open without `FILE_SHARE_DELETE` fails `ReplaceFileW` with `ERROR_SHARING_VIOLATION` (32, `error.file.inUse`); a reader that shares delete does NOT block the replace and keeps reading the content it opened; and `ReplaceFileW` refuses a read-only target by itself with `ERROR_ACCESS_DENIED` — so the up-front read-only check exists to give the refusal its own key, not for safety. **Controls, each red:** a waiver removed from the real file (`layer-lint` names `ReplaceFileW(` at its line); the three patterns removed (all three fixtures pass clean); `replaceFile` planted as a rename over the target, the way `QSaveFile` saves (three cases red: the stream and creation time lost, a held target reported as access denied, a replace past a share-delete reader refused); and the read-only pre-check removed (the key becomes `error.file.accessDenied`, both files still intact). A-47's own control, a plain `MoveFileExW` rename, loses the stream. **Deviations: (a) `ReplaceFileW` is called WITH a backup name**, which D3-4 did not say: without one, Microsoft documents `ERROR_UNABLE_TO_MOVE_REPLACEMENT` as leaving the replaced file deleted, and with one every documented failure leaves both files findable; `ERROR_UNABLE_TO_MOVE_REPLACEMENT_2` moves the target back, or reports `error.file.originalMoved` naming where it is. **(b) `DeleteFileW(` joined the write patterns** beside the plan's two, because the same file deletes that backup. **(c) A read-only target is refused by its own key** before the OS is asked. **(d) The path of a new target uses `MoveFileExW(MOVEFILE_WRITE_THROUGH)` without `MOVEFILE_REPLACE_EXISTING` or `MOVEFILE_COPY_ALLOWED`**, so a file appearing in between is not overwritten and a cross-volume move is refused. **Not covered, and each test file says so:** `ERROR_UNABLE_TO_MOVE_REPLACEMENT_2` and an undeletable backup (they need a filesystem failing between two steps of one call), the security descriptor, durability across a power loss, a volume without streams, and a real application holding the file (manual case M-08). |
| **T8** The save path | ✅ 2026-09-15 | **`core/doc/SaveCoordinator.{h,cpp}`** (D3-4, D3-6, D3-16); **`pdf/ops/RevisionWriter.{h,cpp}`** — compose, write, re-read FROM THE PATH, verify, adopt — shared by both backends; `IDocumentBackend` gains **`writeRevision`** and **`adoptRevision`**, implemented by `pdf/DocumentBackendImpl`; `Document::refreshFileState` and `CommandJournal::adoptSavedFile`; `IDurableFiles` gains **`removeFile`** and **`processExists`**; `RenderRequest::pageRotationDelta`, drawn by `PdfiumRasterizer` (D3-15); `tests/unit/{tst_savecoordinator,tst_mutationrender}.cpp`, `tests/unit/{SaveSession,FakeDurableFiles}.h`, **`tests/roundtrip/tst_roundtrip.cpp`** (compiled into `rawpage_tests`, D3-18), `tests/render/tst_saveengine.cpp` and `RunA4Product.cmake`, `tests/bench/bench_save.cpp`; ten catalog keys (**670** per catalog, was 661). **+22 cases** (844 → **866/866** on `x64-release`, **866/866** on `x64-debug`, one named skip); `gates` 120/120; `checks` clean at **334 files / 2,431 includes**, i18n-lint **203 files**, mutation-lint *"1 command(s): RotatePageCommand"*; waiver census still **12**. Guide **A-50 … A-61 ✅, A-56 🟡** (its recovery `meta.json` clause is T9's). **The round trip over the corpus:** *"47 saved in place, 12 repaired files saved as a new file"* — each saved file read back from its path, the opened bytes its prefix, the update holding only the rotated page. **Through PDFium:** *"54 files saved and re-read"*, rotations matching the model. **A4 through the product, automated half:** OpenSSL rejected all 8 flipped copies of the four product-saved signed files and verified all 4 signatures (`render.a4_product.openssl`). **Save cost (A-61), this machine, five runs, medians:** `pages-900.pdf` (0.1 MB) — hash at open 0.37 ms, save after one rotation 14.2 ms; **a generated 200 MB file — hash at open 606 ms, save 1,560 ms**, which includes D3-16's second hash of the file on disk. D3-4's *Revisit* question is now a number. **Eleven controls, each red:** in `SaveCoordinator` the identity check removed, the identity reduced to size and time, the repaired check removed, a failed step leaving its temporary file, the stray cleanup ignoring liveness, and `adoptRevision` skipped; in `RevisionWriter` the prefix check and the rotation read-back removed; in the shipped backend a repaired document given an ordinary update; the rasterizer ignoring the delta; and the request's key ignoring it. **Issue I-09** (Stage 3 guide §8): A-58's *pixel-identical* is **within one level in 255** — a rotation passed to `FPDF_RenderPageBitmap` and the same rotation read from `/Rotate` anti-alias curves one level apart (`vector-curves.pdf`, 9 of 125,756 bytes) — and without the font bridge `bad-name-hex-escape.pdf` renders differently on every open, which the product's assembly does not reach. **On the way:** a failed Catch2 `CHECK` on two `QByteArray`s crashes the reporter, so the save suites compare hashes. **Deviations: (a) `IDurableFiles` gained `removeFile` and `processExists`** after T7 — `SaveCoordinator` in `core/` may not delete a temporary file itself (layer-lint's write rule), and a stray file is removed only when its writer is gone (A-60), which Qt cannot answer. **(b) `save(journal, path)` takes the path** rather than reading it from the document: `DocumentId` holds a normalized, case-folded path, which is an identity and not a name to write to. **(c) The write and the re-read live in `pdf/ops/RevisionWriter`**, not in the backend, so the headless `CosBackend` and the shipped backend run the same code and the round trip runs under ASan. **(d) A Save-As onto the document's own file goes through every Save check**, so a repaired document cannot be "saved as" itself. **(e) A Save-As of an unchanged, unrepaired document is a byte copy**; `IncrementalWriter` refuses an empty delta. **(f) A failure after the replace keeps the journal dirty** and leaves the backend's identity on the previous file, so the next Save asks for Save-As — stated in `SaveCoordinator.h`. **(g) `RevisionWriter::setComposeFaultForTesting`** is a named seam, the only way to obtain an update its reader rejects from a correct writer (A-54). **(h) A-58's tolerance** — I-09. **CI run 34913473783 (T8's push) failed ONE case on the Linux `portable-asan` job**, and the defect was the test's: A-50's same-size, same-time section set the modification time on the handle that had written, and on Linux `QFile` flushed its buffered write at close, after the time was set. Fixed by closing the write first and setting the time through a second open. The same run's ASan job passed every other save case — the round trip over the corpus included, with no sanitizer line — and its Windows job ran 866/866 with `render.a4_product.openssl` reporting *"8 of 8 controls rejected, 4 of 4 signatures verify"*. **Not done here, and each owner is named:** the recovery `meta.json` after a save (T9); `DocumentWindow` subscribing the scheduler to the journal and computing the delta from the model (T10); `Ctrl+S` (T10). |
| **T9** `RecoveryJournal` and the kill harness | ✅ 2026-09-15 | **`core/cmd/RecoveryJournal.{h,cpp}`** and **`core/cmd/OperationRecorder.h`**; `CommandJournal` gains `setRecorder`, `undoStep`/`redoStep` and **`abandonOpenGroup`**, and `adoptSavedFile` takes the path; `SaveCoordinator::setPhaseObserverForTesting`; `tests/unit/tst_recovery.cpp`; **`tests/platform/killprobe/` (`rawpage_killprobe`)** and `tests/platform/tst_recoverykill.cpp`, the platform target now linking `rawpage_cos` and `rawpage_ops`; thirteen `error.recovery.*` keys (**683** per catalog, was 670). **+14 cases** (866 → **880/880** on `x64-release`, **880/880** on `x64-debug`, one named skip); `gates` 120/120; `checks` clean at **340 files / 2,512 includes**, i18n-lint **206 files**; waiver census **14** (was 12). Guide **A-62 … A-70 ✅** — A-70's ASan half from CI run 34917155088 (539/539 under ASan+UBSan, no sanitizer line; its Windows job ran both kill cases green) — and A-09's and A-56's T9 halves. **The kills, measured:** during commands, twenty `TerminateProcess` calls landed *"between apply and the flush: 4; inside the flush: 16"* and every acknowledged command was recovered, never more than one extra; during saves, all twenty landed *"between writing the temporary file and the replace"* and the target was always a whole revision with its `Zone.Identifier` stream. **Nine controls, each red:** the journal not reporting a run; a failed group not abandoned; an unterminated group kept; a torn line replayed as malformed; a pre-save undo re-run instead of reverted; discovery ignoring the lock; degradation warning more than once; a save not truncating; and a line left in Qt's buffer — which lost acknowledged commands in all twenty kill runs. **Two controls did not exercise their subject on the first attempt and were rewritten** (convention 7): removing only the warn-once guard left one warning, because the append already stops after the first degrade; and one plant did not compile under `-Werror`. **Deviations: (a) undo and redo lines carry the WHOLE STEP** — `"cmds"` and the step's name — where plan §5.5 wrote one `"cmd"`: a group is one undo step and a line naming one of its commands could not replay it. **(b) An undo of a step from before the last save replays as the commands' `revert()` run as a new step**, so the document is right and the undo menu holds that inverse rather than the original step. **(c) `CommandJournal::abandonOpenGroup`**, a method the plan did not list, so a group that fails part-way leaves no trace. **(d) Replay runs with no recorder attached, and returns the applied lines to seed the next journal** — the inverse step is never serialized. **(e) A command whose `serialize()` is empty ends recording** with `error.recovery.notRecoverable` (Tech-Stack-Plan.md §5.5), rather than the journal refusing it. **(f) The directory is made with `QDir::mkpath`, removed with `QDir::rmdir`, and locked with `QLockFile`** — none of which layer-lint's write patterns name; the header says it is the gate's blind spot and not a licence. **(g) The kill harness's windows are held open by test-only phase observers that sleep 15 ms**, stated in the probe's header; without them the windows are microseconds wide. **Stated, not closed:** a kill between `SaveCoordinator`'s replace and the `meta.json` rewrite leaves a journal whose meta names the pre-save file, which discovery then offers as *recover to a new file* — the edits replayed onto a file that already has them, into an untitled document; the original is never touched. **Not done here:** the prompt, the untitled document and the report dialog (T11), `documents/recoveryDir` (T12), and `DocumentWindow` attaching a journal (T10). |
| **T10** The UI | ✅ 2026-09-15 — **I-10 raised and closed** | **`ui/dialogs/{RotatePagesDialog,UnsavedChangesDialog}.{h,cpp}`** (D3-13, D3-14); `ui/shell/DocumentWindow` owns the document's **`CommandJournal`, `RecoveryJournal` and `SaveCoordinator`**; `MainWindow::registerMutationActions()` registers **`edit.undo`, `edit.redo`, `page.rotate`, `file.save` and `file.saveAs`**, with `rotatePages`, `saveDocument`/`saveDocumentAs`, `closeDocument(index, disposition)` returning a `Status`, a `closeEvent`, and `updateDocumentTitles()`; `MessageBar` gains `SaveFailed`, `SaveAsRequired` and `RecoveryUnavailable`; `ActionRegistry::Registration` gains `tooltipText`/`unavailableText`; `PageView::modelChanged` and the engine tree beside the model's, so `TilePresenter` sets `pageRotationDelta` (D3-15); `catalog::translateText`; `app::Services::recoveryRoot`; `tests/ui/tst_mutationcommands.cpp`, `FakeBackend`'s editable mode, and `ShellFixture` with `FakeDurableFiles` and a temporary recovery root. **30 catalog keys per catalog** (**713**, was 683): 25 `shell.*` in `ShellKeys.def`, `command.page.rotateAll`, `error.save.cancelled` and three `error.close.*`. **+10 cases** (880 → **890/890** on `x64-release` and on `x64-debug`, one named skip); `gates` **120/120**; `checks` clean at **345 files / 2,593 includes**, i18n-lint **210 files**, mutation-lint *"1 command(s): RotatePageCommand"*; waiver census **14**. **CI run 34922017047** (commit `fcc78ef`, T10's push): all five jobs green — Windows **890/890**, `portable-asan` **539/539** with no `runtime error`, `AddressSanitizer` or `LeakSanitizer` line, the Linux `gates` job 106/106, and the fuzz and coverage jobs. **`rawpage.exe --list-actions`:** *"361 declared 53 registered 308 unregistered"*. Guide **A-71 … A-73, A-75, A-76, A-78 … A-81 ✅, A-77 🟡** (its `documents/warnOnRepairedSave` half is T12's); A-74 is T11's. **What the cases send:** key events to a shown window's focused viewer for all six chords; a rotated page's slot read **before** a tile is pumped, then its pixels, then the delta its tiles were requested with; every close route — the tab, the window, a torn-off window, `file.quit` over two documents — with Cancel, Discard and Save, and a headless close refused `Unavailable`. **Nine controls, each red:** a dirty close that never asks (both close cases); no dirty marker; `modelChanged` without `refresh()`; no rotation delta; every mutation re-raising the document's banners (the dismissed recovery banner came back); Save-As with no explanation; rotate enabled without the permissions; all pages planned without a group; a failed save raising nothing. **A tenth planted bug stayed GREEN** — `engine_.setDocument` removed from `modelChanged` — because that call only cleared a layout `refresh()` rebuilds anyway; it was removed from the product and the control rewritten (convention 7). **Deviations: (a) `ActionRegistry` gained two `Registration` fields** the plan did not list: a key cannot say *Undo: Rotate page 1 by 90 degrees* or the document's own refusal, so a function written in `ui/` returns resolved text and `core/` produces no sentence. **(b) *File changed on disk* is not a banner kind of its own:** it and *repaired* both say why a Save became a Save-As, so they share `SaveAsRequired` with the reason's own text; the three kinds A-80 names are recovery, save failed and Save-As required. **(c) `closeDocument` returns a `Status`**, and `closeEvent` asks only about DIRTY documents — a clean document stays with the window as it did. **(d) A mutation updates the titles and the actions, not `updateDocumentChrome()`**, which clears and re-raises every document banner — a dismissed banner would otherwise return with the next command. **(e) `StatusBar` is unchanged**: the plan's file list named it, and the dirty marker lives in the tab and the title (D3-14's surfaces). **(f) `app::Services` gained a value, `recoveryRoot`**, empty meaning the default, so the widget suite writes into its own temporary directory; T12's setting is what the product reads. **(g) A-81 moved four more cases** than the two it names: `tst_toolfinder` (two), `tst_qat` and `tst_shell` used `file.save` or `file.saveAs` as their example of an unregistered command and now use `file.print`; `tst_shell` and `tst_documenttabs` read 53. **Three findings on the way.** **I-10** (guide §8): `command.*` entries sat in the catalogs' `error` context, where `translate` never looks, so Undo's tooltip printed the raw key — moved to a `command` context. **A closed document outlived its fixture:** `closeDocument` deletes the tab with `deleteLater`, and a case that ended before the event loop ran it had the destructor close its handle on the NEXT case's backend, whose fresh document then lost its sink — the case's `Editor` now flushes deferred deletes before its fixture goes. **And a race in the case's own read:** the delta was reset after the command, and under `-j 16` a worker had already recorded the command's request, so the case read -1; the reset is before the command now, and four stressed runs of `ui.` and `render.` were clean. **One unexplained red, recorded rather than dropped:** one full `x64-debug` run failed T7's *"platform.durable replace control: a plain rename over the target loses the stream"* with its output not captured; nothing in T10 touches that file or `WinDurableFiles`, and it passed alone, in the next two full `x64-debug` runs (890/890 each) and in CI run 34922017047's Windows job. Not reproduced; if it recurs, the output is what to keep. |
| **T11** `RecoveryPrompt`, `RecoveryDialog` | ✅ 2026-09-16 — **I-11 raised and closed** | **`app/RecoveryPrompt.{h,cpp}`** — `checkAndOffer(MainWindow&, const Services&)`, called by `app/main.cpp` after the window is shown and before the command line's files open; **`ui/dialogs/RecoveryDialog.{h,cpp}`** — one row per journal, Recover (or *Recover to a new file*) / Discard / Decide later, and a parameterized twin; **`MainWindow::recoverDocument(RecoveryCandidate)`**; `DocumentWindow` gains a **recovery constructor** that replays before any recorder or subscription and seeds its new journal with the applied lines, **untitled documents** (`sourcePath()`, `isUntitled()`, `suggestedSavePath()`, a Save that is `SaveAsRequired` by `error.save.untitled`), and `recoveryReport()`/`recoveryFailure()`; `MessageBar::Kind::Recovered`; `RecoveryJournal`'s `"untitled"` meta flag and a directory name never reused; `CompositionRoot::Options::recoveryRoot`; `app/CrashHandler`'s three sentences corrected to D3-11. **Tests:** `tests/ui/tst_recoverydialog.cpp` (eight cases, compiling `app/RecoveryPrompt.cpp` in), two `unit.recovery` cases, and **`tests/composition/`** — `rawpage_composition_tests`, a sixth test binary (deviation (a)) with A-74. **23 catalog keys per catalog** (**736**, was 713): 20 `shell.*` in `ShellKeys.def`, `error.save.untitled`, `error.recovery.sourceMissing`, `error.recovery.alreadyOpen`. **+11 cases** (890 → **901/901** on `x64-release` and on `x64-debug`, one named skip — `unit.` 435, `ui.` 226 of which `ui.composition.` 1, `platform.` 48, `render.` 68, `bench.` 4, `gates.` 120, by `ctest -N`); `gates` preset **120/120**; `checks` clean at **352 files / 2,681 includes**, i18n-lint **214 files**, mutation-lint *"1 command(s): RotatePageCommand"*, 110 test files; waiver census **14**, unchanged; `--list-actions` *361 declared, 53 registered, 308 unregistered*, unchanged. Guide **A-67 ✅** (its untitled half), **A-74 ✅**, **A-82 🟡** (the prompt and the sentences; the keys and the Documents page are T12's). **A-74, measured:** `rawpage_killprobe` rotating a copy of `text-a4.pdf`, killed after its third acknowledgement, and the product's composition — the real engine, `WinDurableFiles`, `MainWindow`, `RecoveryPrompt` — offering and replaying it: three runs read *"the probe acknowledged 3 rotation(s)"* and *"replayed 3 step(s); torn trailing line 0"*, the document dirty with its history, the killed session's directory gone. **The startup log** (`--dump-ui-strings`, offscreen) now reads *"CrashHandler installed; buildId … ; lastRites none (D3-11: the recovery journal is on disk before each edit returns)"*. **Thirteen planted bugs, every one red in the end — twelve on their first run:** the untitled flag ignored when choosing into-file; an empty journal offered; a tab closed by its path; no step-*k* line; a directory name reused; a save leaving the untitled flag; the new journal not seeded; a refused recovery raising no banner; Save As proposing the source; nobody-to-ask ignored; the crashed directory kept; replay run with a recorder attached. **Two need saying: C7** — an untitled document's Save handed to `SaveCoordinator` — **stayed GREEN on its first run**, because the coordinator's identity check also turns a save to the empty path into a Save As, with the wrong reason (*"changed or moved by something else"*); the case now requires the explanation to be `error.save.untitled`'s and went red (convention 7). **C4** — the prompt's nobody-to-ask check removed — is red only on the `offered` count, because `RecoveryDialog::ask` refuses on its own when nobody can answer: the prompt's check is the second of two guards. **One red full run, and it was the harness:** the first full `x64-release` run failed A-74 alone, because after C1 was restored only `rawpage_ui_tests` had been rebuilt, and `rawpage_composition_tests` — which links the same `rawpage_ui` — still carried the plant; after a full build the same suite was 901/901. **Deviations: (a) A SIXTH TEST BINARY, `tests/composition/`** (`rawpage_composition_tests`), which `Stage3-Mutation-Plan.md` §5.6 did not list: A-74 needs the engine, the widgets and the OS bundle in one process, and every existing target exists to keep one of those out (§2.1); its cases sit inside the `ui.` prefix, as `render.golden.` sits inside `render.`, so CI's non-empty check is unchanged. **(b) The report of step *k* is the recovered document's `Recovered` banner, not the dialog's**: the dialog collects a disposition and closes before any replay starts, and a dialog that replayed would hold document logic. **(c) The plan's `recovery` catalog family (§4) is `shell.recovery.*`**, as T10 filed the Rotate Pages dialog under `shell.rotate.*` — every key `ShellKeys.def` declares is `shell`'s context (guide I-10). **(d) `meta.json` gains `"untitled":true`**, beyond §5.5's shape: a document recovered into a new file has its source's bytes, and without the flag a second crash would offer it as a replay under the source's path, where a Save writes into the file the user chose not to touch; absent for a titled document, so T9's meta is byte-identical. **(e) A journal with no operations is removed without asking**, and **(f) a Recover that cannot happen** — the file already open here, a source gone, an open refused, a journal that replays nothing — opens nothing, keeps the directory, and raises a window-scoped `OpenFailed` line. **(g) `MainWindow::closeDocument` finds the tab by its widget**, not its path: two untitled documents both answer to the empty path, and by path the first would be closed — latent, since no untitled document existed before T11, and a case plants it. **(h) After a replay the crashed session's directory is removed even if it stopped at step *k***: steps *k*… cannot be applied to this source, and keeping the directory would offer the same failure at every launch; the banner says what was not recovered. **Stated, not closed:** with `general/singleInstance` off, two instances launched together after a crash can both offer the same journal — discovery checks the lock, and nothing holds it while a dialog is open. **Not done here:** `documents/recoveryDir` and `documents/warnOnRepairedSave` (T12); M-04 is walkable and **not walked**. **CI run 35029614272** (commit `bd5543d`, T11's push): all five jobs green. |
| **T12** Settings keys and the Documents page | ✅ 2026-09-16 | **Two rows in `core/settings/SettingsKeys.def`** (D3-9): `documents/recoveryDir` (`QString`, empty) and `documents/warnOnRepairedSave` (`bool`, `true`), both in the `Documents` group; **`PreferencesDialog::shippedPages()` gains `Documents`**, second — six of 26 groups — and the page is generated (convention 17) with four rows, beside Stage 1's `recentCount` and `recentPinned`, which had been declared there with no page since Stage 1; **`RecoveryJournal::resolveRoot(configured, fallback)`**, read by `DocumentWindow` when a document opens and by `RecoveryPrompt` at launch; `MainWindow::saveDocument` skips a **repaired** document's explanation when the key is off, and goes to Save As either way; three `prefs.*` catalog keys per catalog (**739**, was 736). **Tests:** `tests/unit/tst_stage3settings.cpp` (four cases), a second A-77 case in `tst_mutationcommands`, a redirect case in `tst_recoverydialog`, and `tst_viewsettings`' page count corrected 5 → 6 in place. **+6 cases** (901 → **907/907** on `x64-release` and on `x64-debug`, one named skip — `unit.` 439, `ui.` 228 of which `ui.composition.` 1, `platform.` 48, `render.` 68, `bench.` 4, `gates.` 120, by `ctest -N`); `checks` clean at **353 files / 2,696 includes**, i18n-lint **214 files**, mutation-lint 111 test files; waiver census **14**, unchanged; `--list-actions` unchanged at *361 / 53 / 308*. Guide **A-77 ✅** (its setting half) and **A-82 ✅** (its keys and page). `--lang zh-TW --dump-ui-strings` resolves the page as *文件* and both rows' labels in Chinese. **Seven planted bugs, each red:** the explanation shown whatever the key says; the key off also skipping Save As; the key silencing every reason; `DocumentWindow` ignoring the key; the prompt ignoring it; a relative folder accepted; the Documents page not shipped. **Deviations: (a) Precedence is `documents/recoveryDir`, then `Services::recoveryRoot`, then the default** — the plan names only the key; T11's `CompositionRoot::Options::recoveryRoot` is how a test composition keeps out of the user's folder, and the setting must still redirect inside such a composition for A-82 to be measurable. **(b) Only an ABSOLUTE folder counts**: a relative one would resolve against the directory the process started in, so it is ignored and logged. **(c) The key gates the REPAIRED reason only**: D3-6 names the repaired document's explanation, and a file changed on disk or an untitled document goes on saying why — a case plants the broader reading. **(d) The folder is a line edit that applies on `editingFinished`**, which is what the generated page builds for a `QString` row (convention 17); a folder picker would be widget code per setting. **(e) `PreferencesDialog.h`'s *"THREE PAGES, AND WHICH THREE"*** had been stale since Stage 2 T15 and was corrected in place. **Stated, not closed:** the key takes effect for documents opened after it changes — an open document keeps its journal — and **journals under a folder the key no longer names are not offered** until it names it again; the prompt looks where the key says, which is also what keeps a test composition from being offered the user's own journals. **Not done here:** M-05 and M-06 are walkable and **not walked**. |
| **T13** A4 through the product, `fuzz_cos_roundtrip`, CI, lockstep | 🟡 2026-09-17 — **code complete; A-83(b)'s re-soak and M-03's product walk outstanding** | **`tests/fuzz/{RoundtripFuzzBody.h,fuzz_cos_roundtrip.cpp}`** (D3-17): parse any input; when the first-party reader and sink can edit it, rotate page 1, compose through `RevisionWriter::compose` with the product's choice of section, and require every input byte in front, a re-read through the output's own table with nothing forcing Save-As, page 1 at the new rotation and every other page unmoved, and **`RevisionWriter::verify` agreeing** — the last two beyond D3-17's three, because the product refuses a save that fails them; `tests/fuzz/CMakeLists.txt` declares **3**, the assertion moved to **`cmake/RawPageFuzzTargets.cmake`** with **`tests/gates/fixtures/fuzz-count/`** and four `gates.fuzz_count.*` cases (A-83(a)); **`tests/fuzz/seeds/`** — `rawpage_fuzz_seeds`, a helper built on every preset but `gates`, keeping the revisions the body composes (D3-17's *"seeds gain the writer's own outputs"*), CTest `unit.fuzz.roundtrip_seeds`; `tst_roundtrip` replays the body over the corpus with three planted compose faults; `unit.cos.compile.fuzz_entrypoint_roundtrip`. **CI (§9):** `portable-fuzz` checks three linked targets, writes the seeds and runs the round trip 60 s; `fuzz-soak.yml` runs three, the round trip seeded from `fuzz-seeds/roundtrip`, `corpus/malformed`, `corpus/ordinary` and `corpus/signed`; `coverage-report` names the kill harness as Windows-only; `portable-asan` needed no change — the round trip has been in `rawpage_tests` since T8 (D3-18). **I-12 (S2), found before the push by the body under a temporary in-process mutation loop — 88 failures in 1,000,000 inputs, all on repaired files' complete sections, all refused by `verify` — and fixed here by the user's decision of 2026-09-17:** `IncrementalWriter::protectOriginal` (re-writes into the update every object whose parse runs to the end of the file, and raises the allocator past the largest referenced number via `ObjectAllocator::reserveThrough`), called by `RevisionWriter::compose` before any append; `PageOps::rotatedPage` keeps a loose stream page a stream; `openWithRepair` never recovers object 0. Four `roundtrip I-12 …` cases, **each red with its fix removed**; afterwards the same seed and an independent one, 1,000,000 inputs each, nothing. **The fix's cost**, `rawpage_bench_save` with and without the walk, same machine: `pages-900.pdf` median 15.47 against 14.54 ms, 200 MB 1,632 against 1,623 ms. **A4 through the product (D3-1's second half):** `render.a4_product.openssl` re-run after I-12's change — OpenSSL 3.5.6, *"8 of 8 controls rejected, 4 of 4 signatures verify"* — and **the product verdict recorded in `Tech-Stack-Plan.md` §3.3 as PARTIAL**. **+12 cases** (907 → **919/919** on `x64-release` and on `x64-debug`, one named skip — `unit.` 447, `ui.` 228 of which `ui.composition.` 1, `platform.` 48, `render.` 68, `bench.` 4, `gates.` 124, by `ctest -N`); the `gates` preset **124/124**; `checks` clean at **357 files / 2,721 includes**, i18n-lint 214 files, mutation-lint 115 test files. **By name (A-83(c)):** 742 test names in `tests/` at Stage 2's sign-off `40dfaa7`, 739 present today and 3 census cases renamed when their counts changed (named in the guide). **§10's debt:** every item checked against its document and marked where it was paid. **Deviations: (a)** A-83(a)'s fixture is a gate case over a LANGUAGES NONE project, not a `portable-fuzz` configure — so the assertion is proven to fire where it could never be seen before. **(b)** The seed writer is a helper in `tests/fuzz/seeds/`, added from the root CMakeLists on every preset, because `tests/fuzz/` is read by `portable-fuzz` only and its target count must not include a helper. **(c)** The body checks more than D3-17 names (own table, `verify`'s agreement). **(d)** I-12 was fixed inside T13 rather than logged for later (the user's decision, 2026-09-17), which made the writer change part of what A4's product figure measures. **(e)** A-83(c)'s exceptions are three renames, not the two A-81 moved, which kept their names. **After the push (`b8e1a72`, CI run 35132580941, all five jobs green), A-83(b) from the logs:** three targets configured, linked and run 60 s — the round trip *"Done 15997 runs"* — with no sanitizer line; `portable-asan` 557/557 with the I-12 and fuzz-body cases by name. **The soak, run 35134357858:** lexer 1,824,169 and parser 886,538 inputs clean; **`fuzz_cos_roundtrip` stopped at 5,244 on UBSan** — *"signed integer overflow: 9223372036854775807 + 1"* at `XrefRepair.cpp:379` — **guide issue I-13 (S2)**: an object number from an object stream's pair table, a classic subsection or an xref stream's `/Index` placed with no bound, and `start + index` computed before any check. **Fixed:** `cos::kMaxObjectNumber` (8,388,607, ISO 32000-1 Annex C) in `pdf/cos/XrefTable.h`, never exceeded by a placed number and checked before each addition, with a backstop in `mergeEntry`; `readField` accumulates unsigned (an 8-byte `/W` field, the same class); the allocator's limit `static_assert`ed equal. Three `tst_xrefrepair` cases, each red on the unfixed reader and red again with only its own guard removed. **+3 cases: 922/922** on `x64-release` and `x64-debug`, one named skip; `checks` clean (layer-lint 357 files / 2,722 includes). **Not done here:** the push of the I-13 fix and the re-soak that meets A-83(b); M-03's product walk and every other unwalked manual case; the stage sign-off. |

Carries risk **A4** as a **go/no-go**: can an incremental save
preserve the original bytes closely enough that a signature applied *before* an unrelated later edit
still validates? `pdf/cos/IncrementalWriter` is the riskiest first-party code in the project. **The
plan measures A4 twice** (D3-1): a probe over the writer alone at **T2**, before any model, journal or
UI code, and the product through the shipped save path at **T13** — Stage 2's D2-1 method, whose two
A2 figures differed by the whole product. **A4's evidence is two validators that share no code with
RawPage** (D3-2, decided by the user 2026-09-14): the OpenSSL command-line tool for the integrity of
the signed range, automated, and Acrobat for modification policy, manual, over signed files generated
like the rest of the corpus. The verdict is therefore bounded to **RawPage's own generated
signatures**, and the sign-off says so.

**Four scope and design decisions were made by the user on 2026-09-14 while the plan was written**,
and this entry was corrected in the same change: **Save writes a complete new file beside the target
and atomically replaces it** (D3-4) rather than appending in place; `Outline` and `Metadata` move to
Stage 4, where their first consumers are (D3-8); `documents/saveMode` moves to Stage 8 and
`documents/autosaveIntervalSec` to Stage 13 (D3-9); and A4's evidence, above.

**Objectives.** Make one real change to a document, reversibly, durably, and recoverably: rotate a
page, undo it, redo it, save it incrementally, reopen it, and recover it after a kill.

**Components & responsibilities.**
- `core/doc/Document` — `Result<void> hydrate(IDocumentBackend&)`, `Revision revision() const`.
- `core/doc/DocumentId`, and `PageTree`'s **rotation** (`void rotate(PageIndex, int)`). ~~`move(Range,
  PageIndex)`, `Outline` (read-only), `Metadata`~~ — **moved 2026-09-14** (`Stage3-Mutation-Plan.md`
  **D3-8**): nothing in Stage 3 calls a page move, and the first readers of an outline and of metadata
  are Stage 4's `BookmarksPane` and `DocumentPropertiesDialog`. `Page`, `Revision.h` and `Scope`
  arrived at Stage 2 (D2-5), and a page label is `Page::label()` since Stage 2 T9.
- `core/doc/Permissions` — **the pre-flight half, brought forward from Stage 10** (D3-7): `may(Operation)`
  refuses, by name, a document this build cannot write — encrypted, or read differently by the COS
  reader and the engine. The permission bits stay Stage 10's.
- `core/doc/SaveCoordinator` — **not in the original list** (D3-4): whether to save, where, and what a
  failure leaves, over the backend and `platform::IDurableFiles`, so the batch path reaches it.
- `core/cmd/Command.h` — the five-method contract. **`name()` returns a catalog key and arguments**,
  not a translated string — `core/` has no `tr()` (D3-10; guide issue I-01).
- `core/cmd/CommandJournal` — `bool run(std::unique_ptr<Command>)`, `bool undo()`, `bool redo()`,
  `void beginGroup(QString)`, `void endGroup()`, `Revision revision() const`,
  `void markSaved(Revision)`, and **subscriptions** for `mutated(Scope)` and `dirtyChanged(bool)` —
  the pattern `core/settings/Settings` uses, since `core/` has no `Signal` type. **Dirty is a position
  in the history, not a revision** (D3-10).
- `core/cmd/CommandRegistry` — `void add<T>()`, `std::unique_ptr<Command> deserialize(const QJsonObject&)`.
- `core/cmd/RecoveryJournal` — `void append(const QJsonObject&)`, `QVector<Found> discover()`,
  `ReplayResult replay(CommandJournal&)`, `void truncate()`.
- `core/cmd/commands/PageCommands` — `RotatePageCommand` only this stage.
- `pdf/cos/IncrementalWriter` — ~~`Result<void> append(QIODevice&, const ObjectDelta&)`~~ **as built
  at T2:** `Result<WrittenUpdate> append(std::string_view original, const XrefTable&, const
  ObjectDelta&)`, returning the bytes to write after the original rather than writing a device, plus
  `appendComplete()` for D3-6. Why is in the T2 row of the status table above.
- `pdf/cos/ObjectAllocator`.
- `pdf/ops/` as a **portable** library, `rawpage_ops`, so the writer and the sink run under
  `portable-asan` and `portable-fuzz` (D3-3).
- `pdf/ops/MutationSinkImpl` — `Status setPageRotation(int pageIndex, int degrees)` (the signature
  `core/backend.h` has declared since Stage 2 T1) + the queue/flush pair, holding **desired state** so
  an undo before Save writes nothing (D3-5).
- `platform::IDurableFiles` and `platform/win/WinDurableFiles` — **the eighteenth platform interface**
  (D3-4): a flush to disk and a replace that keeps the target's streams, neither of which Qt provides.
- `pdf/ops/PageOps` — `void applyRotation(CosObject& pageDict, int angle)`. **The type is
  `CosObject`**, the single variant Stage 2 T6 built; this line said `CosDict` while the object
  model was still a sketch.
- `app/RecoveryPrompt`, `ui/dialogs/RecoveryDialog`. ~~`ui/dialogs/RepairReportDialog`~~ — folded into
  `ui/shell/MessageBar` at Stage 2 T14. **Added by the plan:** `ui/dialogs/RotatePagesDialog` — the
  rotate half of Stage 7's `PageOpsDialogs`, because `Features.md` §8 says `Ctrl+R` *opens the dialog*
  (D3-13) — and `ui/dialogs/UnsavedChangesDialog`, because this is the first stage in which closing a
  document can lose work (D3-14).
- `tools/mutation-lint/`, `tests/roundtrip/` (compiled into `rawpage_tests`, not a sixth target —
  D3-18). ~~`tests/fuzz/`~~ — **moved to Stage 2**, which is where
  `pdf/cos/Lexer` and `Parser` are written and therefore the earliest a fuzz target can exist;
  `CMakePresets.json` has said so since Stage 0. ~~Stage 3 adds seeds, not targets.~~ **Stage 3 adds
  seeds and ONE target**, `fuzz_cos_roundtrip` — parse, rotate, append, re-parse — because the writer
  consumes whatever the tolerant reader produced from a malformed file (D3-17, corrected 2026-09-14).

**Key bindings handled** — `Ctrl+S` ([Global](./Features.md#global-active-in-every-context)),
`Ctrl+Z` / `Ctrl+Y` ([Common](./Features.md#common-all-contexts-unless-a-context-above-overrides-the-chord),
now backed by a real journal), and `Ctrl+R` in **both**
[`Viewer`](./Features.md#viewer-review-mode) and
[`Thumbnails`](./Features.md#page-thumbnails-pane) contexts — the first chord to demonstrate context
dispatch against a real mutation.

**Data / settings keys.** `documents/recoveryDir`, `documents/warnOnRepairedSave` — which controls
the explanation before a repaired document's forced Save-As and **never** the forcing (D3-6).
~~`documents/autosaveIntervalSec`~~ → Stage 13 and ~~`documents/saveMode` (incremental | full)~~ →
Stage 8, **moved by the user 2026-09-14** (D3-9): a journal flushed per command has no interval, and
`full` has no writer until `FullWriter`. The Documents Preferences page ships.

**Cross-cutting additions — the gate turns on here.** First registered command:
`RotatePageCommand`. From this stage forward `mutation-lint` blocks any command lacking registration,
a serialize round-trip test, and a revert test. i18n groups: `command.*` (undo-menu labels),
`recovery.*`.

**Algorithms & notes.**
- **Permission pre-flight happens before construction, not after.** `CommandJournal::run` asks
  `Permissions::may()` first; a refused operation must leave the document bit-identical.
- **Partial application is a bug, not a state.** `apply()` returning false must leave the document
  untouched; a debug build asserts it, and the reversibility property test proves it over the corpus.
- **The recovery journal is `fsync`ed per command.** Slower than buffering, and correct — the whole
  point is surviving a kill at an arbitrary instant.
- **Replay is per-command resumable.** A failure at command *k* keeps 1..*k-1* and reports *k*. An
  all-or-nothing recovery would throw away good work to avoid reporting a bad line. **Refined by
  D3-11: the unit is the undo step** — a group is one step, so a failure inside a group keeps the steps
  before it and none of the group, and an unterminated trailing group is dropped and reported.
- **A repaired document's first save is forced to Save-As.** Appending an incremental update to a
  file whose structure we reconstructed risks destroying the original.
- **Compound operations group.** "Insert 12 pages" is one `Ctrl+Z`, via `beginGroup`/`endGroup`.

**Testing.**
*Automated:* `tst_journal` (run/undo/redo/group, dirty tracking, revision monotonicity, permission
refusal leaves no trace); `tst_pagetree`; `tst_command_serialize` (round-trip per command);
`tst_reversibility` — **the property test**: for every registered command over the corpus,
`apply`+`revert` restores a structural hash exactly; `tst_roundtrip` — open → rotate → incremental
save → reopen: rotation present, untouched objects **byte-identical**, and `corpus/signed-*.pdf`
still validates (**the A4 gate**); `tst_recovery` (append/discover/replay, mid-file corruption stops
cleanly, read-only directory degrades to memory with one warning); `tests/fuzz` targets live on the
`portable-fuzz` preset.
*Manual:* → [`Stage3-Mutation-Testing-Guide.md`](./Stage3-Mutation-Testing-Guide.md) §6, **written
2026-09-14** before this stage's code (§5) — eight cases: the first mutation by hand; save and reopen
in three readers that are not PDFium; **A4 in Acrobat, walked twice** (probe and product) with a
certified document as the control; killing the real product and recovering; a read-only recovery
directory; a repaired file's forced Save-As; closing with unsaved changes; saving while another
application holds the file. **Two items this line used to queue became automated**, as the guide
explains: comparing the file bytes before and after (against bytes read back from disk), and killing
mid-edit (`TerminateProcess` at instants the harness proves it reached). *Rotate from Thumbnails* is a
binding a case resolves and a gesture Stage 4 owns — there is no pane until then (D3-13).

**Exit criteria.** Rotate → undo → redo → save → reopen works end to end; the original bytes are
provably preserved; a pre-signed file's signature survives an unrelated incremental edit
(**A4 verdict recorded**); killing the process and relaunching offers and correctly replays the
change; `mutation-lint` is live and fails on a test command with no inverse.

**Deferrals.** Every other page operation (Stage 7). No annotations, forms, security, or export. No
full-rewrite Save-As yet — that is Stage 8; this stage's forced-Save-As path writes through the
incremental writer to a fresh file.

> **🏁 First milestone complete.** All five locked decisions are now proven against real code:
> the toolkit (D2), the render half of the engine (D3/D10), our own write half (D3), the mutation
> design (D5), and the platform boundary. Everything after this point is *work*, not *risk*.

---

### Stage 4 — Viewing and navigation surface

**Status:** ⚪ **Not started.**

**Objectives.** Everything in [`Features.md` §1](./Features.md#1-view--find-) that is not text or
search: the sidebar panes, split views, rulers/grid/guides, full-screen and presentation, window
arrangement, and session restore.

**Components & responsibilities.**
- `ui/panes/ThumbnailsPane` — `void setPageSize(int)`, `QVector<PageIndex> selection() const`.
- `ui/panes/BookmarksPane` (navigation only), `ui/panes/DestinationsPane` (read-only).
- `ui/viewer/SplitViewContainer` — `void split(Orientation)`, `void freeze(int row, int col)`.
- `ui/viewer/RulerWidget`, `ui/viewer/GridGuideLayer` — `QPointF snap(QPointF, SnapFlags) const`.
- `ui/viewer/FullScreenController` — `void enter(ScreenIndex)`, `void setTransition(Transition)`,
  `void setAutoAdvance(int sec)`, `void blank(BlankMode)`.
- `ui/viewer/ToolController` + `tools/{HandTool,SelectTool,MarqueeZoomTool,LoupeTool}`.
- `ui/shell/WindowArranger` — `void tile(Orientation)`, `void cascade()`, `void mergeWindows()`,
  `void syncScroll(bool)`.
- `core/settings/SessionState` — `void capture(const MainWindow&)`, `void restore(MainWindow&)`.
- `ui/dialogs/GoToPageDialog`, `ui/dialogs/DocumentPropertiesDialog`.
- `core/doc/Outline` (read — `BookmarksPane` is its first reader) and `core/doc/Metadata`
  (`DocumentPropertiesDialog` is its first reader), **moved here from Stage 3 on 2026-09-14** by the
  user (`Stage3-Mutation-Plan.md` D3-8): a model type arrives with its first caller.

**Key bindings handled** — [Viewer](./Features.md#viewer-review-mode): `Ctrl+4`, `Ctrl+U`, `Ctrl+'`,
`Ctrl+;`, `Ctrl+L` (reading mode), `S` (snapshot). [Global](./Features.md#global-active-in-every-context):
`F11`, `Ctrl+Shift+P`, `Ctrl+Shift+B`. Full [Thumbnails](./Features.md#page-thumbnails-pane) context:
`Enter`, arrows, `+`/`-`. [Bookmarks](./Features.md#bookmarks-pane): `Enter`, `→`/`←`, `*`/`/`.
Full [Full Screen & Presentation](./Features.md#full-screen--presentation) table.

**Data / settings keys.** `pageDisplay/continuousScroll`, `pageDisplay/autoScrollSpeed`,
`units/rulerUnit`, `units/gridSpacing`, `units/gridSubdivisions`, `units/gridColour`,
`units/gridOffset`, `units/snapToGrid`, `units/snapToGuides`, `units/snapToObjects`,
`fullScreen/transition`, `fullScreen/autoAdvanceSec`, `fullScreen/loop`, `fullScreen/monitor`,
`fullScreen/showCursor`, `general/rememberPaneLayout`, `general/rememberWindowLayout`.

**Cross-cutting additions.** No new commands (this stage is all view state, no document mutation —
a useful check that view and document changes stay separate). i18n: `panes.*`, `fullscreen.*`,
`units.*`.

**Algorithms & notes.** Snapping resolves in a fixed precedence — object edges, then guides, then
grid — with one threshold in device pixels so it feels the same at every zoom. Session restore must
survive a monitor that no longer exists: geometry is stored per monitor-arrangement fingerprint and
clamped to the current desktop on restore. Thumbnails share the Stage 2 render pipeline at a low
resolution rather than owning a second one.

**Testing.**
*Automated:* `tst_snap` (precedence and threshold), `tst_sessionstate` (round-trip; restore onto a
smaller desktop clamps), `tst_presentation_modes`, `tst_thumbnail_selection`.
*Manual:* → queued **`Stage4-ViewingSurface-Testing-Guide.md`** (see §5). Emphasis: two monitors at
different scaling, full-screen on the secondary monitor, unplugging a monitor while a window is on
it, and spreadsheet-split freezing.

**Exit criteria.** All four layout modes plus split views work; rulers, grid, guides, and snapping
behave identically at 50% and 400% zoom; full-screen and presentation work on a chosen monitor;
window arrangement and session restore survive a monitor topology change.

**Deferrals.** Comparison view (Stage 5, needs text). Pane *editing* — bookmark edit is Stage 7,
comments Stage 6, fields Stage 9, layers/tags Stage 7. AI assistant (Stage 16).

---

### Stage 5 — Text layer: extraction, selection, find, search, comparison

**Status:** ⚪ **Not started.** `core/text/LayoutAnalyzer` is on the critical path for Stage 13's
reflow engine, so its quality bar here is higher than this stage alone needs.

**Objectives.** Text becomes first-class: extract it, select it, copy it, find it, search across
files, and diff two documents.

**Components & responsibilities.**
- `core/text/PageTextModel` — `Result<void> hydrate(ITextExtractor&, PageIndex)`.
- `core/text/LayoutAnalyzer` — `Blocks analyze(const PageTextModel&, const TagTree*) const`.
- `core/text/TextSelection` — `Range hitTest(QPointF) const`, `Range expandTo(Granularity) const`,
  `QString asPlainText() const`, `QString asFormatted() const`, `Table asTable() const`.
- `core/text/SearchEngine` — `Hits find(const Document&, const SearchQuery&)`,
  `Hits findAcross(const QStringList& paths, const SearchQuery&)`.
- `core/text/SearchQuery.h`, `core/text/DocumentIndex`.
- `core/text/DocumentComparer` — `DiffSet compare(const Document&, const Document&, CompareOptions)`.
- `pdf/render/PdfiumTextExtractor`, `pdf/cos/ContentStreamParser`.
- `ui/viewer/SelectionOverlay`, `ui/viewer/ComparisonView`, `ui/panes/SearchPane`,
  `ui/panes/DifferencesPane`, `ui/write/FindReplaceBar` (find half only),
  `ui/viewer/tools/SnapshotTool`, `ui/dialogs/CompareDialog`.
- `platform/win/WinRichClipboard`.

**Key bindings handled** — the whole [Find / Search](./Features.md#find--search) table
(`KeyContext::Search`): `Ctrl+F`, `F3`/`Shift+F3`, `Ctrl+G`/`Ctrl+Shift+G`, `Ctrl+Shift+F`, `Enter`,
`↑`/`↓`, `Alt+R`, `Alt+W`, `Alt+C`, `Esc`. [Viewer](./Features.md#viewer-review-mode):
`Ctrl+Alt+C` (compare), `Ctrl+]`/`Ctrl+[` (next/previous difference).
[Common](./Features.md#common-all-contexts-unless-a-context-above-overrides-the-chord): `Ctrl+C`,
`Ctrl+A` in the Viewer context.

**Data / settings keys.** `search/wholeWords`, `search/caseSensitive`, `search/ignoreDiacritics`,
`search/useRegex`, `search/includeComments`, `search/includeBookmarks`, `search/includeFields`,
`search/includeAttachments`, `search/maxResults`, `search/indexCacheMB`,
`documents/compareSensitivity`, `documents/compareIgnoreHeaderFooter`,
`documents/compareIgnoreWhitespace`.

**Cross-cutting additions.** No document mutation. i18n: `search.*`, `compare.*`.

**Algorithms & notes.**
- `LayoutAnalyzer` uses the tag tree where present and falls back to geometry: baseline clustering
  into lines, gap analysis into blocks, whitespace-column detection, and ruling-line-plus-alignment
  detection for tables. **Its accuracy is measured, not asserted** — a labelled corpus subset with a
  precision/recall target, so Stage 13 inherits a known quantity rather than a hope.
- The document index is built lazily on first search and reused, so a second search of a 900-page
  file is instant.
- Comparison aligns text with a Myers diff over normalized run tokens, then confirms
  page-appearance differences with a raster compare — text-only diffing misses moved images, and
  raster-only diffing reports every reflowed line as a change.

**Testing.**
*Automated:* `tst_selection` (hit-test, word/line/block expansion, column selection, RTL, vertical
text), `tst_layout` (precision/recall against the labelled subset), `tst_search` (all flags, regex,
each content class, CJK, combining marks), `tst_index`, `tst_compare` (known before/after pairs).
*Manual:* → queued **`Stage5-TextSearch-Testing-Guide.md`** (see §5), whose manual section is brief:
selection across columns and page boundaries; copy into Word and confirm formatting;
snapshot into an image editor; search a CJK document; compare two real revisions.

**Exit criteria.** Selection is accurate on multi-column, RTL, and CJK pages; every §1.8 search
toggle works and is honoured across all five content classes; cross-file search returns correct
snippets; comparison finds text, appearance, and page-level changes on real revision pairs.

**Deferrals.** Replace (needs Write mode, Stage 13). Catalog index (Stage 16). OCR'd-layer search
works automatically once Stage 16 adds OCR — no change needed here.

---

### Stage 6 — Annotations and review

**Status:** ⚪ **Not started.** May run in parallel with Stage 7.

**Objectives.** The full comment and markup toolset of
[`Features.md` §4](./Features.md#4-review--annotate-), the Comments pane with management, measurement
tools, and FDF/XFDF exchange.

**Components & responsibilities.**
- `core/doc/Annotation`, `AnnotationStore` — `void add(Annotation)`, `Query query(Filter) const`,
  `void reply(AnnotationId, Annotation)`, `void setStatus(AnnotationId, Status, QString author)`,
  `MergeResult importFdf(const Fdf&)`, `Fdf exportFdf(Filter) const`.
- `core/doc/Attachments` — `void add(EmbeddedFile)`, `Result<QByteArray> extract(FileId) const`.
- `core/cmd/commands/AnnotationCommands` — one subclass per operation.
- `core/cmd/commands/ContentCommands` — typewriter text only this stage.
- `core/settings/ProfileStore` (first user: stamps).
- `pdf/ops/AnnotationOps`, `pdf/ops/AppearanceStreamBuilder` —
  `CosStream build(const Annotation&, AppearanceState)`.
- `pdf/cos/ContentStreamBuilder`.
- `ui/viewer/AnnotationOverlay`, `ui/viewer/tools/{MarkupTool,NoteTool,TypewriterTool,DrawingTool,InkTool,StampTool,MeasureTool}`.
- `ui/panes/CommentsPane`, `ui/panes/StampPalettePane`, `ui/panes/AttachmentsPane`.
- `ui/dialogs/{CommentSummaryDialog,MeasurementScaleDialog,CustomStampDialog}`.

**Key bindings handled** — the whole
[Annotation tools](./Features.md#annotation-tools) table (`KeyContext::AnnotationTool`):
`Ctrl+Alt+{M,H,U,X,T,L,R,O,N,P,S,D,A,K}`, `K`, `Shift`+drag, `Alt`+drag, `Enter`, `Esc`, `Delete`,
`Ctrl+E`, `Ctrl+Alt+Enter`, arrows/`Ctrl+`arrows. The whole
[Comments pane](./Features.md#comments-pane) table (`KeyContext::Comments`): `↑`/`↓`, `Enter`, `F2`,
`Ctrl+Enter`, `Delete`, `Space` (checkmark — note the deliberate three-way collision with Viewer
scroll and Forms-Fill toggle), `Ctrl+Shift+K`, `Ctrl+F`, `Ctrl+.`/`Ctrl+,`. [Global](./Features.md#global-active-in-every-context):
`Ctrl+Shift+C`, `Ctrl+Shift+A`.

**Data / settings keys.** `identity/author`, `identity/email`, `identity/organization`,
`commenting/defaultColour.<tool>`, `commenting/defaultOpacity.<tool>`,
`commenting/defaultThickness.<tool>`, `commenting/stickyTool`, `commenting/popupBehaviour`,
`commenting/showPopupsOnHover`, `commenting/printComments`, `commenting/summaryLayout`,
`commenting/spellCheckComments`, `measuring/scaleRatio`, `measuring/displayUnit`,
`measuring/precision`, `measuring/snapToPaths`, `measuring/writeToComment`.

**Cross-cutting additions.** Commands registered: `AddAnnotation`, `EditAnnotation`, `MoveAnnotation`,
`DeleteAnnotation`, `ReplyToAnnotation`, `SetAnnotationStatus`, `SetAnnotationAppearance`,
`LockAnnotation`, `ImportAnnotations`, `AddAttachment`, `DeleteAttachment`, `AddTypewriterText` —
each with its inverse and both tests. i18n: `annot.*`, `comments.*`, `measure.*`, `stamps.*`.

**Algorithms & notes.**
- **We generate every `/AP` appearance stream ourselves** rather than relying on the viewer, so a
  RawPage highlight looks identical in Acrobat, Chrome, and a phone. This is the single biggest
  interoperability decision in the stage and it is why `AppearanceStreamBuilder` gets golden tests.
- Comment migration to a revised document matches on text content first, then position, and
  **reports** what it could not place — a comment silently moved to the wrong paragraph is worse than
  one reported as unplaceable.
- Ink smoothing fits a Catmull-Rom spline to sampled points, keeping the raw samples so the eraser
  can split a stroke.
- Measurement snapping reuses Stage 4's snap precedence, extended with path endpoints, midpoints,
  and intersections.

**Testing.**
*Automated:* `tst_annotation`, `tst_annotstore` (filter/sort/group, threading, status history),
`tst_fdf` (import/export round-trip, and against files produced by other readers),
`tst_appearance` (golden raster per annotation type — this is what protects interoperability),
`tst_measure` (scale, units, precision), plus reversibility and round-trip for all twelve commands.
*Manual:* → queued **`Stage6-Annotations-Testing-Guide.md`**. Every tool on a real page; open the
result in another PDF reader and confirm identical appearance; import comments exported by another
reader; record an audio comment; measure with a calibrated scale.

**Exit criteria.** Every §4.1 tool creates a correct, correctly-appearing annotation; the Comments
pane groups, filters, sorts, searches, replies, and sets status; FDF/XFDF round-trips with at least
one other reader; comment summaries generate in all four layouts; annotations undo cleanly. **Carried
from Stage 3 (guide issue I-06, decided by the user 2026-09-14): risk A4's Acrobat half through the
product** — each of the four `corpus/signed/` files opened in RawPage, a comment added with the first
annotation command, saved through `SaveCoordinator`, and Acrobat reporting every approval signature's
signed revision unaltered with subsequent changes (and the certified control flagged), recorded beside
Stage 3's figures in `Tech-Stack-Plan.md` §3.3. Stage 3's product walk could only rotate, which Acrobat
rejects from any writer.

**Deferrals.** Redaction *marks* exist; **applying** them is Stage 10. Shared review, review
tracking, and collaborative sessions are Stage 16. Audio recording needs `QtMultimedia` — if it
slips, it slips alone.

---

### Stage 7 — Document assembly

**Status:** ⚪ **Not started.** May run in parallel with Stage 6.

**Objectives.** All of [`Features.md` §5](./Features.md#5-assemble) except batch: page operations,
merge, bookmark editing, tags, layers, headers/footers/Bates, watermarks, imposition, and links.

**Components & responsibilities.**
- `core/doc/PageTree` (full mutation set), `Outline` (full editing + generators),
  `Destinations`, `LayerTree`, `TagTree`.
- `core/cmd/commands/PageCommands` (all: insert, delete, move, duplicate, extract, replace, crop,
  resize, renumber), `BookmarkCommands`, `StructureCommands`, `DecorationCommands`.
- `pdf/ops/PageOps` (full), `DecorationOps` — `void applyWatermark(const WatermarkSpec&, Range)`,
  `void applyHeaderFooter(const HfSpec&, Range)`, `void applyBates(const BatesSpec&, Range)`;
  `ImpositionOps` — `Result<Document> impose(const Document&, const ImpositionSpec&)`.
- `ui/panes/{BookmarksPane (editing),LayersPane,TagsPane,DestinationsPane}`.
- `ui/viewer/tools/{CropTool,LinkTool}`.
- `ui/dialogs/{PageOpsDialogs,MergeDialog,WatermarkDialog,HeaderFooterDialog,BatesDialog,ImpositionDialog,LinkPropertiesDialog}`.

**Key bindings handled** — the rest of [Page Thumbnails](./Features.md#page-thumbnails-pane):
`Delete`, `Ctrl+X`/`Ctrl+C`/`Ctrl+V`, `Ctrl+A`, `Alt+↑`/`Alt+↓`, `Ctrl+Shift+X`, `Ctrl+Shift+D`.
The rest of [Bookmarks](./Features.md#bookmarks-pane): `F2`, `Delete`, `Ctrl+B`, `Tab`/`Shift+Tab`,
`Alt+↑`/`Alt+↓`, `Ctrl+Shift+D`. [Viewer](./Features.md#viewer-review-mode): `Ctrl+B` (new bookmark).
[Global](./Features.md#global-active-in-every-context): `Ctrl+Shift+L`, `Ctrl+Shift+E`.

**Data / settings keys.** `documents/defaultPageSize`, `documents/cropRemoveWhiteMargins`,
`documents/splitMode`, `documents/splitValue`, `documents/mergeBookmarkPerFile`,
`documents/renameCollidingFields`, `documents/watermarkProfile`, `documents/headerFooterProfile`,
`documents/batesStart`, `documents/batesDigits`, `documents/batesPrefix`, `documents/batesSuffix`,
`documents/impositionProfile`, `documents/autoDetectUrls`, `documents/linkAppearance`.

**Cross-cutting additions.** Commands: `InsertPages`, `DeletePages`, `MovePages`, `DuplicatePages`,
`ExtractPages`, `ReplacePages`, `CropPages`, `ResizePages`, `RenumberPages`, `MergeDocuments`,
`CreateBookmark`, `RenameBookmark`, `DeleteBookmark`, `ReparentBookmark`, `SetBookmarkTarget`,
`GenerateBookmarks`, `CreateLayer`, `DeleteLayer`, `MergeLayers`, `FlattenLayers`, `SetLayerUsage`,
`RetagElement`, `AutoTag`, `ApplyWatermark`, `RemoveWatermark`, `ApplyHeaderFooter`,
`RemoveHeaderFooter`, `ApplyBates`, `RemoveBates`, `CreateLink`, `EditLink`, `DeleteLink`,
`CreateDestination`, `RenameDestination`, `DeleteDestination`. i18n: `pages.*`, `bookmarks.*`,
`layers.*`, `tags.*`, `decorate.*`, `links.*`.

**Algorithms & notes.**
- **Reordering pages must repair every reference to them:** links, bookmark destinations, named
  destinations, article threads, and structure-tree page references. A "move pages" that breaks
  bookmarks is the classic bug of this feature area, so `tst_reference_repair` runs after every page
  command, not only after `MovePages`.
- Watermarks, headers/footers, and Bates numbers are written into **marked content sections tagged
  with a RawPage key**, which is what makes "remove the watermark I added" possible without guessing.
- Split-by-size does a binary search on the page count per output, since compression makes size
  non-linear in pages.
- Bates numbering across a document set is one logical run: the allocator is shared and the report
  states the first and last number per document.
- `MergeDocuments` is the only page command that can hit form-field name collisions; the policy
  (rename versus deliberately merge) is a setting, and the result is reported either way.

**Testing.**
*Automated:* `tst_pagetree` (full mutation set), `tst_reference_repair` (links, bookmarks,
destinations, tags survive every reordering), `tst_outline` (hierarchy ops and all three generators),
`tst_layers`, `tst_tags`, `tst_decoration` (apply → remove → byte-compare against the original),
`tst_imposition` (n-up and booklet page order for 2/4/8/16 and odd counts), `tst_merge` (bookmarks,
links, fields, layers preserved; collisions reported); reversibility and round-trip for all 36
commands.
*Manual:* → queued **`Stage7-Assembly-Testing-Guide.md`**. Emphasis: drag pages between two open
documents; a 30-way merge; booklet imposition printed duplex on paper and physically folded.

**Exit criteria.** Every §5.1 page operation works and repairs all references; merge preserves
bookmarks/links/fields/layers; bookmark hierarchy editing works by drag and by chord; watermarks and
headers/footers apply, update, and remove cleanly; imposition output is correct on paper; the tag
tree can be viewed, retyped and reordered.

**Deferrals.** Batch (Stage 14). Scanner-sourced page insert (Stage 16). Flatten (Stage 8).
Portfolios (Stage 16).

---

### Stage 8 — Save, export, and optimize

**Status:** ⚪ **Not started.** This is the stage that makes the product's output *shippable* — up to
here every path has been incremental-save only.

**Objectives.** All of [`Features.md` §2.5](./Features.md#25-save-and-export): full-rewrite Save-As,
version and conformance targets, export to every listed format, content reuse, optimization, and
flattening.

**Components & responsibilities.**
- `pdf/cos/FullWriter` — `Result<void> write(QIODevice&, const Document&, WriteOptions)`.
- `pdf/cos/Linearizer`.
- `pdf/ops/OptimizeOps` — `Result<Savings> optimize(Document&, const OptimizeProfile&)`,
  `SpaceAudit audit(const Document&) const`.
- `pdf/ops/FlattenOps` — `void flatten(FlattenTargets, Range)`.
- `pdf/ops/ExportOps` — `Result<void> exportPages(const ExportSpec&)`,
  `Result<void> exportOffice(Format, const QString& path)`.
- `pdf/ops/ConformanceChecker` — `Report check(const Document&, Conformance) const`.
- `platform/win/WinPrinting`, `platform/win/WinOfficeBridge` (export path).
- `ui/dialogs/{ExportDialog,OptimizerDialog,ConformanceReportDialog,PrintDialog,FlattenDialog}`.

**Key bindings handled** — [Global](./Features.md#global-active-in-every-context): `Ctrl+Shift+S`,
`Ctrl+P`, `Ctrl+D`.

**Data / settings keys.** `convertFrom/exportFormat`, `convertFrom/exportDpi`,
`convertFrom/exportColourSpace`, `convertFrom/textLayoutPreserving`,
`convertFrom/officeEngine` (firstParty | officeBridge), `documents/optimizeProfile`,
`documents/targetPdfVersion`, `documents/conformanceTarget`, `documents/linearizeOnSave`,
`documents/flattenTargets`, `documents/saveMode` (incremental | full) — **moved here from Stage 3**
(`Stage3-Mutation-Plan.md` D3-9), because `full` is `FullWriter`'s. **`Ctrl+Shift+S` is registered
from Stage 3**, where Save-As copies and appends (D3-16); this stage changes what it writes, not
whether it exists.

**Cross-cutting additions.** Commands: `OptimizeDocument`, `FlattenContent`,
`ConvertPdfVersion` — all document mutations and therefore commands. Export and print are **not**
commands (they do not change the document) — a distinction `mutation-lint` does not police, so it is
stated here. i18n: `export.*`, `optimize.*`, `conformance.*`, `print.*`.

**Algorithms & notes.**
- Save-As renumbers objects, drops unreachable ones, packs object streams, and is the **only** path
  that discards revision history — which is exactly why "Save" and "Save As" must never be silently
  interchanged.
- Office export quality is a product-defining bar and settles risk **R3** here on measured fidelity:
  a first-party writer versus round-tripping through `IOfficeBridge`, chosen per format, not
  globally. The decision and its measurements are recorded in this stage's status block.
- Conformance checking reports **per object with a page reference**; "this file is not PDF/A" without
  a location is not actionable.
- The space audit attributes every byte to a category, which is what makes size reduction
  predictable rather than a lottery.

**Testing.**
*Automated:* `tst_fullwriter` (renumber, compact, unreachable drop; reopen equals the source
semantically), `tst_linearize` (structure validated by an external checker), `tst_optimize` (each
profile reduces size without visual change — verified by golden compare), `tst_flatten` (flattened
output renders identically to the unflattened original), `tst_export_raster` (DPI and colour space),
`tst_conformance` (known-good and known-bad PDF/A and PDF/X files), `tst_export_office` (fidelity
score against a labelled corpus, tracked as a number over time).
*Manual:* → queued **`Stage8-SaveExport-Testing-Guide.md`**. Emphasis: export to Word and open in
real Word; print to a physical printer; validate PDF/A output in an external validator; optimize a
200 MB scanned file.

**Exit criteria.** Save-As produces a smaller, semantically identical file; every export format
opens correctly in its native application; PDF/A output validates externally; flattening changes
nothing visually; the space audit's numbers match reality; **R3 has a recorded decision with
fidelity measurements**.

**Deferrals.** PDF/X output profiles beyond X-1a and X-4. EPUB. Batch export (Stage 14).

---

### Stage 9 — Forms

**Status:** ⚪ **Not started.** May run in parallel with Stage 10.

**Objectives.** All of [`Features.md` §6](./Features.md#6-forms): field creation and properties for
all eight types, tab order, format/validate/calculate, filling, and data exchange.

**Components & responsibilities.**
- `core/doc/FormField`, `FormModel` — `Result<void> setValue(FieldName, Value)`,
  `CalcOrder calculationOrder() const`, `void setCalculationOrder(CalcOrder)`,
  `QVector<FieldName> missingRequired() const`, `TabOrder tabOrder(PageIndex) const`.
- `core/forms/FieldScript` — the sandboxed Acrobat-compatible JavaScript subset:
  `Result<Value> evaluate(const Script&, ScriptContext&)`.
- `core/cmd/commands/FormCommands`.
- `pdf/ops/FormFieldOps` — `void createField(const FieldSpec&)`, `void normalizeStaticXfa()`.
- `pdf/render/PdfiumFormFill`.
- `ui/viewer/FormOverlay`, `ui/viewer/tools/FieldTool`, `ui/panes/FieldsPane`.
- `ui/dialogs/{FieldPropertiesDialog,TabOrderDialog,CalcOrderDialog,FormDataDialog,DuplicateFieldDialog}`.

**Key bindings handled** — the whole [Forms — Design](./Features.md#forms--design) table
(`KeyContext::FormsDesign`): `Ctrl+Alt+F`, `T`/`K`/`B`/`D`/`L`/`U`/`G`/`X`, `Tab`/`Shift+Tab`,
`Enter`, `Delete`, `Ctrl+A`, `Ctrl+D`, arrows, `Ctrl+Shift+`arrows, `Ctrl+Shift+T`, `Ctrl+Alt+G`.
The whole [Forms — Fill](./Features.md#forms--fill) table (`KeyContext::FormsFill`): `Tab`/`Shift+Tab`,
`Space`, `↑`/`↓`, `Enter`, `Ctrl+Enter`, `Esc`, `F2`, `Ctrl+Alt+G`, `Ctrl+Shift+K`.
[Global](./Features.md#global-active-in-every-context): `Ctrl+Shift+I`.

**Data / settings keys.** `forms/highlightFields`, `forms/highlightColour`,
`forms/requiredFieldColour`, `forms/autoComplete`, `forms/autoCompleteHistory`,
`forms/showFieldTooltips`, `forms/warnCalcOrder`, `forms/showFocusRect`,
`javascript/enableFormScripts`, `javascript/warnBlockedScript`.

**Cross-cutting additions.** Commands: `CreateField`, `DeleteField`, `EditFieldProperties`,
`MoveField`, `ResizeField`, `DuplicateFieldAcrossPages`, `SetFieldValue`, `SetTabOrder`,
`SetCalculationOrder`, `ResetForm`, `ImportFormData`, `FlattenForm`, `NormalizeStaticXfa`.
i18n: `forms.*`, `fields.<type>.*`.

**Algorithms & notes.**
- Calculation order is a **topological sort** of the field dependency graph; a cycle is reported with
  the participating field names rather than silently iterated to a fixed point.
- `SetFieldValue` is a command like any other, so form filling is undoable and recoverable — which
  falls out of Stage 3 for free and is a differentiator many editors lack.
- The JavaScript subset is **sandboxed and enumerated**: no filesystem, no network, no `app.launchURL`
  without a trust check. A script calling outside the subset is blocked and *reported*, never
  silently ignored — a silently-failing form is worse than one that says why.
- Auto-size text fields resolve their font size by binary search on the fitted extent, cached per
  field geometry.

**Testing.**
*Automated:* `tst_formfield` (all eight types, every option flag), `tst_formmodel` (name resolution,
tab order by row/column/structure, required validation), `tst_calc` (sum/product/average/min/max,
simplified field notation, topological order, cycle reporting), `tst_fieldscript` (the subset;
blocked calls are reported), `tst_formdata` (FDF/XFDF/XML/CSV round-trip, and merging many filled
copies into one spreadsheet), `tst_xfa_normalize` (static normalizes; dynamic reports honestly);
`tst_appearance` extended to widget appearance states.
*Manual:* → queued **`Stage9-Forms-Testing-Guide.md`**. Emphasis: fill a real government form; a
calculated invoice with a deliberate cycle; tab through 200 fields; open a RawPage-authored form in
another reader and fill it there.

**Exit criteria.** All eight field types are creatable with full properties; auto-detection produces
usable fields on a static PDF; calculations and validation work with a reported order; tab order
works all three ways; data round-trips in all four formats; a RawPage-authored form fills correctly in
another reader; field edits undo.

**Deferrals.** Dynamic XFA beyond honest reporting (risk R4). Barcode *decoding* verification is
generation-only this stage. Reader-extension rights (a licensing question, not a technical one).

---

### Stage 10 — Security, permissions, redaction, sanitize

**Status:** ⚪ **Not started.** May run in parallel with Stage 9. `pdf/ops/RedactionOps` is the one
operation in the product whose correctness is a **security** property — a redaction that leaves the
text recoverable is a data breach, not a bug.

**Objectives.** [`Features.md` §7.1–§7.5 and §7.9](./Features.md#7-secure--sign-): passwords,
certificate encryption, security schemes, redaction, hidden-content removal, and the trust manager.

**Components & responsibilities.**
- `core/doc/Permissions` — `bool may(Operation) const`.
- `core/security/{EncryptionPolicy,SecurityScheme,SecuritySchemeStore,RedactionPlan,HiddenContentScanner,TrustPolicy}`.
- `core/cmd/commands/SecurityCommands`.
- `pdf/cos/{StandardSecurityHandler,PublicKeySecurityHandler}`.
- `pdf/ops/{EncryptionOps,RedactionOps,SanitizeOps}` —
  `RedactionOps::Result apply(const RedactionPlan&)` returns what it removed, per page.
- `platform/win/WinProcessSandbox`, `platform/win/WinCertificateStore` (recipient lookup).
- `ui/viewer/tools/RedactTool`, `ui/dialogs/{SecurityDialog,SanitizeDialog,RedactionSetupDialog,SecuritySchemeDialog,TrustManagerPage}`.

**Key bindings handled** — [Annotation tools](./Features.md#annotation-tools): `Ctrl+Alt+K` (mark
for redaction; the *apply* action is menu-only by design — it is irreversible in effect even though
it is undoable in session).

**Data / settings keys.** `security/defaultEncryption`, `security/minPasswordLength`,
`security/warnWeakPassword`, `security/encryptMetadata`, `security/defaultPermissions`,
`security/redactionOverlayColour`, `security/redactionCodeSet`, `security/redactAlsoMetadata`,
`trust/allowNetwork`, `trust/allowedHosts`, `trust/blockedHosts`,
`trust/blockedAttachmentTypes`, `trust/privilegedLocations`, `trust/protectedViewMode`,
`javascript/enableDocumentScripts`.

**Cross-cutting additions.** Commands: `ApplyPasswordSecurity`, `ApplyCertificateSecurity`,
`RemoveSecurity`, `ApplySecurityScheme`, `ApplyRedactions`, `SanitizeDocument`. i18n: `security.*`,
`redact.*`, `sanitize.*`, `trust.*`.

**Algorithms & notes.**
- `Permissions::may()` is asked by `CommandJournal::run` **before** any command's `apply()`, so
  permission enforcement is one predicate in one place rather than a check scattered through fifty
  commands. This was designed in Stage 3 precisely so this stage is small.
- **Redaction rewrites content streams.** Covered text-showing operators are removed and the
  surrounding graphics state is preserved; covered image regions are re-encoded with the area
  destroyed. Drawing a black rectangle over text — which some tools do — is not redaction, and
  `tst_redaction` asserts extraction returns nothing for redacted regions.
- Re-encryption must re-key **every** stream and string, including ones we never parsed. A missed
  string is a plaintext leak in an encrypted file.
- Sanitize **verifies after removing** and reports per category, because "we tried" is not a
  security guarantee.
- Applying redactions to a signed document warns first: it will break the signature, by design.

**Testing.**
*Automated:* `tst_crypt` (RC4 40/128, AESV2, AESV3 against published vectors), `tst_permissions`
(every operation × every permission combination), `tst_encryptionops` (apply → open with the password
→ remove → byte-compare; every string and stream re-keyed), `tst_redaction` — **the critical suite**:
after applying, text extraction over the region returns nothing, the content stream contains no
covered glyph, the image bytes changed, and the same holds after Save-As and after optimization;
`tst_sanitize` (each category found, removed, and verified gone), `tst_trust`.
*Manual:* → queued **`Stage10-Security-Testing-Guide.md`**. Emphasis: redact, save, then attack the
output with an independent extraction tool and with a hex editor; certificate encryption for a real
recipient; protected view on a hostile file.

**Exit criteria.** All four encryption algorithms round-trip; permissions are enforced through the
single predicate for every command; **redacted content is unrecoverable by an independent tool, in a
hex editor, and after Save-As and optimization**; sanitize finds, removes, and verifies every
category; the trust manager blocks what it claims to.

**Deferrals.** AIP/rights management (Stage 16). Electronic envelopes and secure delivery need
Stage 11's signing. The security audit log lands with Stage 14's logging work.

---

### Stage 11 — Digital IDs and signatures

**Status:** ⚪ **Not started.** Needs Stage 10 (shared `pdf/cos` crypt code) and Stage 8.

**Objectives.** [`Features.md` §7.5–§7.8](./Features.md#75-digital-ids-and-trust): digital ID
management, trusted identities, signing, appearances, long-term validation, validation, and secure
delivery.

**Components & responsibilities.**
- `core/security/{SignatureRequest,SignatureValidator,ValidationReport}` —
  `SignatureValidator::validate(const Signature&, ValidationOptions) const`.
- `pdf/ops/SignatureOps` — `Result<void> sign(const SignatureRequest&, ICryptoProvider&)`,
  `QVector<Signature> readSignatures(const Document&) const`.
- `platform/win/{WinCertificateStore,WinCryptoProvider,WinSecretStore}`.
- `ui/panes/SignaturesPane`, `ui/dialogs/{SignDialog,SignatureAppearanceEditor,DigitalIdManager,TrustedIdentitiesDialog,ValidationPreferencesDialog,CertificateViewer,EnvelopeDialog}`.

**Key bindings handled** — [Global](./Features.md#global-active-in-every-context): `Ctrl+Shift+G`
(Signatures pane). Signing itself is deliberately menu- and ribbon-only: a one-chord irreversible
cryptographic operation is a footgun.

**Data / settings keys.** `signatures/defaultId`, `signatures/idPasswordTimeoutMin`,
`signatures/defaultAppearance`, `signatures/timestampServer`, `signatures/timestampCredentials`,
`signatures/embedRevocation`, `signatures/padesProfile`, `signatures/verifyAsOf` (signing | now),
`signatures/requireRevocationCheck`, `signatures/allowExpiredTimestamps`,
`signatures/trustOsRootStore`, `signatures/ldapServer`.

**Cross-cutting additions.** Commands: `SignDocument`, `CertifyDocument`, `CreateSignatureField`,
`ClearSignature`. i18n: `sign.*`, `validate.*`, `digitalId.*`, `trustedId.*`.

**Algorithms & notes.**
- Signing reserves the `/Contents` byte range, hashes **exactly** the bytes outside it, then embeds
  the CMS blob without shifting a single byte. An off-by-one here produces a signature that validates
  locally and fails everywhere else — so `tst_signing` cross-validates with an independent verifier.
- Every crypto primitive comes from `ICryptoProvider`, so all validation *logic* is unit-tested
  headlessly against a mock: chain building, trust decisions, revocation, and the
  timestamp-versus-signer-clock distinction.
- The permitted-versus-actual change comparison after a certifying signature reuses Stage 5's
  `DocumentComparer` — the same code that diffs two files answers "was this modification allowed?"
- **Offline validation reports honestly.** "Cannot be validated without network access" is a
  distinct, non-alarming state; showing a red cross for an unreachable OCSP responder trains users
  to ignore real warnings.

**Testing.**
*Automated:* `tst_validation` (valid, tampered, expired, revoked, untrusted, offline — all against a
mock provider), `tst_signing_bytes` (byte ranges exact; the document is byte-identical outside
`/Contents`), `tst_pades` (B-B, B-T, B-LT, B-LTA structure), `tst_signed_version` (view-as-signed and
compare-versions), plus round-trip: sign → add an annotation incrementally → the signature still
validates and reports the later change as permitted or not.
*Manual:* → queued **`Stage11-Signatures-Testing-Guide.md`**. Emphasis: **a physical smart card or
USB token**; sign in RawPage and validate in Acrobat, and the reverse; a real timestamp authority; a
certifying signature followed by a permitted and a forbidden change.

**Exit criteria.** Signing works with a self-signed ID, a PKCS#12 file, an OS-store ID, and a
physical token; RawPage-signed documents validate in at least one other reader and vice versa;
timestamps and revocation embed and verify; view-signed-version and compare-versions are correct;
the offline state reports honestly.

**Deferrals.** AIP labels (Stage 16). Reader-extension rights. Batch signing lands with Stage 14.

---

### Stage 12 — Create and import

**Status:** ⚪ **Not started.**

**Objectives.** The v1 subset of [`Features.md` §2.1](./Features.md#21-create-a-pdf): blank PDF,
from one or many files, from the clipboard, from a screen-region capture, and from an open document's
page range.

**Components & responsibilities.**
- `core/create/DocumentBuilder` — `Result<Document> blank(const PageSpec&)`,
  `Result<Document> fromFiles(const QStringList&, const ConversionProfile&)`,
  `Result<Document> fromClipboard()`, `Result<Document> fromRegion(const QImage&)`.
- `core/create/ConversionProfile` + its `ProfileStore` collection.
- `pdf/ops/ContentImport` — image, text, HTML, and Markdown to page content.
- `platform/win/WinOfficeBridge` (import path), `platform/win/WinRichClipboard` (paste path).
- `ui/dialogs/{NewDocumentDialog,CreateFromFilesDialog,ConversionProfileDialog}`,
  `ui/viewer/tools/RegionCaptureTool`.

**Key bindings handled** — [Global](./Features.md#global-active-in-every-context): `Ctrl+N` (now
functional, opening in Write mode once Stage 13 lands; until then it creates a blank fixed page).
[Common](./Features.md#common-all-contexts-unless-a-context-above-overrides-the-chord): `Ctrl+V`
into the Thumbnails context to paste pages.

**Data / settings keys.** `convertTo/defaultProfile`, `convertTo/pageSize`,
`convertTo/orientation`, `convertTo/margins`, `convertTo/imageCompression`,
`convertTo/imageQuality`, `convertTo/fontEmbedding`, `convertTo/pdfVersion`,
`convertTo/conformance`, `convertTo/bookmarksFromHeadings`, `convertTo/preserveLinks`,
`convertTo/convertComments`, `convertTo/officeEngine`.

**Cross-cutting additions.** Commands: `InsertImportedPages`, `PastePages`. Creating a *new* document
is not a mutation of an existing one, so `DocumentBuilder` is a service, not a command — stated here
because the distinction is easy to get wrong. i18n: `create.*`, `convertTo.*`.

**Algorithms & notes.** Office import quality follows the Stage 8 / risk R3 decision — the same
engine choice applies in both directions, and the profile carries it. Markdown and HTML import go
through a first-party layout pass rather than a browser engine (`QtWebEngine` is excluded by D2's
module list). Image import chooses a page size from the image's physical DPI when it declares one,
and falls back to the configured default when it does not.

**Testing.**
*Automated:* `tst_builder` (blank pages at every preset size, correct boxes),
`tst_fromfiles` (each source format produces a valid, openable PDF), `tst_conversion_profile`
(every knob has a measurable effect), `tst_clipboard_import` (text, image, and vector flavours),
`tst_import_fidelity` (a labelled corpus scored as a tracked number).
*Manual:* → queued **`Stage12-Creation-Testing-Guide.md`** (see §5), whose manual section is brief:
create from a 200-page Word document with headings and cross-references, and check
bookmarks and links; drag twelve mixed files onto the window; paste a spreadsheet region.

**Exit criteria.** Blank creation works at every preset size; each listed source format converts to
a valid PDF; conversion profiles round-trip and every setting demonstrably matters; Word headings
become bookmarks and cross-references become links.

**Deferrals.** Scanner, OCR, web capture, the virtual printer, Office add-ins, and portfolios are
all Stage 16.

---

### Stage 13 — Write mode (Convert to Writable)

**Status:** ⚪ **Not started.** **The largest and least certain stage in the plan** — risk **R1**.
It is split so that failure is contained: **13a** delivers the reflow model, conversion, and text
and paragraph editing; **13b** adds tables, illustrations, document structure, and revision
tracking. 13b may parallelize once 13a's model is stable. **A spike precedes 13a** and its finding
is recorded here before the stage is committed to.

**Objectives.** [`Features.md` §3](./Features.md#3-edit-): the dual-mode model, *Convert to
Writable*, word-processor editing, and *Prevent Further Edits* — plus Review-mode object editing.

**Components & responsibilities.**
*13a:* `core/reflow/{ReflowDocument,StyleSheet,LineBreaker,Shaper,FixedToReflowConverter,ReflowToFixedComposer,FidelityReport,SpellChecker}`;
`core/cmd/commands/ReflowCommands`; `core/cmd/commands/ContentCommands` (full Review-mode object
editing); `pdf/cos/ContentStreamBuilder` (page composition); `ui/write/{WriteCanvas,StyleGallery,SpellCheckPane,FindReplaceBar}` (replace half);
`ui/viewer/tools/ObjectEditTool`; `ui/dialogs/{ParagraphDialog,TabStopsDialog,PageSetupDialog,ConversionFidelityDialog}`.
*13b:* `core/reflow/{TableModel,ChangeTracker}`; `ui/write/{TableTools,TrackChangesBar}`;
`ui/panes/DocumentMapPane`; `ui/dialogs/{TableDialog,TocDialog,CaptionDialog,FootnoteDialog,ChartDialog}`.

Key signatures: `FixedToReflowConverter::convert(const Document&) -> Result<ReflowDocument, FidelityReport>`;
`ReflowToFixedComposer::compose(const ReflowDocument&, MutationSink&)` — **emits both page content and a structure tree (O2)**;
`LineBreaker::breakParagraph(const Paragraph&, const Constraints&) -> Lines`;
`TableModel::evaluate(CellRef) -> Value`; `ChangeTracker::accept(RevisionId)`.

**Key bindings handled** — the **whole** [Write mode](./Features.md#write-mode) table
(`KeyContext::WriteMode`), which is where the twelve deliberate collisions become real and testable
— **eight overriding Viewer and four overriding Global**, per
[`Features.md` §8](./Features.md#what-the-twelve-deliberate-collisions-counts): `Ctrl+B`, `Ctrl+I`, `Ctrl+U`, `Ctrl+D`, `Ctrl+E`, `Ctrl+K`, `Ctrl+L`,
`Ctrl+R`, `Ctrl+J`, `Ctrl+=`, `Ctrl+Shift+A`, `Ctrl+Shift+L`, `Ctrl+Shift+N`, `Ctrl+Shift+>`/`<`,
`Ctrl+Shift+=`, `Ctrl+Space`, `Ctrl+Alt+1`/`2`/`3`, `Ctrl+Shift+7`, `Tab`/`Shift+Tab`, `Ctrl+H`,
`Ctrl+Enter`, `Ctrl+Shift+Enter`, `Ctrl+Shift+Space`, `Ctrl+Shift+8`, `Ctrl+Shift+R`, `Ctrl+Alt+M`,
`Alt+=`, `F7`, `Shift+F7`, `Ctrl+Alt+F`, `Ctrl+Shift+W`. [Global](./Features.md#global-active-in-every-context):
`Ctrl+Shift+O` (Document Map).

**Data / settings keys.** `spelling/checkAsYouType`, `spelling/language`,
`spelling/customDictionaries`, `spelling/ignoreUppercase`, `spelling/ignoreUrls`,
`documents/convertWarnOnFeatureLoss`, `documents/keepFixedRenditionUntilFinalized`,
`documents/trackChangesOnByDefault`, `documents/reviewerColour`, `documents/showFormattingMarks`,
`documents/defaultStyleSet`, `documents/autosaveIntervalSec` — **moved here from Stage 3**
(`Stage3-Mutation-Plan.md` D3-9): the recovery journal is flushed per command, and an interval first
means something when a burst of typing is coalesced into one journal line.

**Cross-cutting additions.** Commands: `EditTextRun`, `SetParagraphFormat`, `ApplyStyle`,
`InsertPageBreak`, `InsertTable`, `EditTable`, `InsertObject`, `SetTextWrap`, `InsertToc`,
`UpdateToc`, `InsertFootnote`, `InsertCrossReference`, `AcceptRevision`, `RejectRevision`,
`ConvertToWritable`, `PreventFurtherEdits`, plus the Review-mode set `EditTextInPlace`,
`ReplaceImage`, `MoveObject`, `ResizeObject`, `SetObjectZOrder`, `GroupObjects`, `EditArticleThread`.
Typing commands **coalesce**, so a burst of keystrokes is one undo step. i18n: `write.*`,
`styles.*`, `table.*`, `toc.*`, `revisions.*`, `spelling.*`.

**Algorithms & notes.**
- **Conversion is the whole stage's risk.** It consumes Stage 5's `LayoutAnalyzer` output plus the
  tag tree, and its accuracy on untagged documents is bounded by that analyzer's measured
  precision/recall — which is *why* Stage 5 measured it.
- **The fidelity report is a feature, not an apology.** Users tolerate an imperfect conversion they
  were warned about; they do not tolerate silent corruption. The report is shown *before* the user
  commits.
- **The original fixed rendition is retained** until *Prevent Further Edits*, so the conversion is
  reversible. That is why `Ctrl+Alt+F` confirms first — it is the one genuinely one-way action in
  the product.
- Line breaking is Knuth–Plass-style with penalties for widows, orphans, and keep-with-next.
- Table formulas share the Stage 9 dependency-graph and topological-sort code; there is no reason
  for two expression evaluators in one product.

**Testing.**
*Automated:* `tst_styles`, `tst_linebreak` (golden line-break positions per paragraph fixture),
`tst_table` (structure ops, sizing, sort, formulas), `tst_changetracking` (accept/reject one, all,
by reviewer), `tst_spell`, `tst_convert_fidelity` (a labelled corpus scored on paragraph, column, and
table detection — **tracked as a number over time, not a pass/fail**), `tst_roundtrip_reflow`
(fixed → reflow → fixed renders within the golden threshold of the original);
**`tst_reflow_convergence`** — **the measurement decision O2 rests on**: convert → compose → convert
*again with no edits in between*, and diff model₂ against model₁ across the corpus. Since the model
is **not** persisted, a converter that does not converge means a user's document silently restructures
every time they reopen it; and because compose now emits a structure tree, round 2 reads those tags
rather than re-inferring from glyphs, which is precisely what this case has to prove works. Plus
reversibility for all 23 commands including coalesced typing.
*Manual:* → queued **`Stage13-WriteMode-Testing-Guide.md`**. Emphasis: convert real-world documents
of every kind — a tagged Word export, an untagged scan-derived file, a multi-column journal article,
a spreadsheet-heavy report — and score each conversion by hand; the twelve chord collisions verified
by switching modes with the same key; IME input for CJK.

**Exit criteria.** Convert to Writable produces an editable document with an honest fidelity report;
**compose emits a structure tree derived from the reflow model, and `tst_reflow_convergence` is green
over the corpus (O2)**;
text, paragraph, style, and page-layout editing all work; tables including formulas work; TOC
generates and updates; change tracking records and accepts/rejects; Prevent Further Edits produces a
fixed PDF that renders within the golden threshold of the composed layout; **every one of the twelve
documented chord collisions resolves correctly on mode switch**; all Write-mode edits undo.

**Deferrals.** Convert to Writable Excel (Stage 16). Equations, SmartArt-style diagrams, and index
generation. Merging revisions from two writable documents.

---

### Stage 14 — Batch, CLI, and preferences completion

**Status:** ⚪ **Not started.**

**Objectives.** [`Features.md` §5.11, §9.5–§9.7](./Features.md#511-batch-processing): batch
sequences, the full command line, logging and diagnostics, and the complete Preferences set.

> ⤵ **Read Aloud and the accessibility pass were removed from this stage on 2026-08-23** — see
> [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23).
> **Full keyboard operability stays here**: it is retained product behaviour, and it is the one part
> of the old accessibility bullet that was never an accessibility feature.

**Components & responsibilities.**
- `core/batch/{Action,ActionSequence,SequenceStore,BatchRunner,BatchReport,WatchedFolder}` —
  `BatchRunner::run(const ActionSequence&, const Inputs&, ProgressSink&) -> BatchReport`.
- `app/CommandLine` (full verb set, JSON report, exit codes).
- `ui/dialogs/{BatchEditorDialog,BatchReportDialog,DiagnosticsDialog}`, all remaining
  Preferences pages, `ui/panes/LogPane` (complete).
- Keyboard pass across `ui/`: full keyboard operability and visible focus on every control.

**Key bindings handled** — none. **Every batch action reuses an existing `ActionId`**, which is the
point. *(`Ctrl+Shift+Y` for Read Aloud was removed 2026-08-23.)*

**Data / settings keys.** `batch/defaultSequence`, `batch/continueOnError`, `batch/outputPattern`,
`batch/collisionPolicy`, `batch/watchedFolders`, `batch/maxParallel`,
`reading/voice`, `reading/rate`, `reading/volume`, `reading/highlightSpoken`,
`reading/readingOrder` (tags | heuristic), `reading/reduceMotion`, `reading/highContrast`,
`logging/collectDiagnostics`, `general/safeMode`.

**Cross-cutting additions.** No new commands — **and that is the stage's most important property.**
If batch needed a new command for anything, it would mean a feature had been implemented in a widget
rather than behind an `ActionId`, and Stage 1's discipline had failed. `tst_batch_coverage` asserts
every mutating `ActionId` is reachable from a sequence. i18n: `batch.*`, `cli.*`, `reading.*`,
`diagnostics.*`.

**Algorithms & notes.** The batch runner drives the **same** `ActionRegistry` and `CommandJournal`
as the UI, with no window constructed — flow K in the file map. Watched folders debounce on file
settling (size stable across two polls) to avoid grabbing a half-copied file. Dry-run runs every
command's `apply` against a scratch copy and reports the diff without saving.

**Testing.**
*Automated:* `tst_sequence` (serialize/deserialize, parameter prompting), `tst_batchrunner`
(continue-on-error, dry-run, cancellation, parallelism), `tst_batch_coverage` (**every mutating
`ActionId` is reachable from a sequence — the standing proof that the action layer is real**),
`tst_cli` (every verb, exit codes, JSON schema), `tst_watchedfolder` (debounce), and a keyboard audit
test asserting every interactive widget is reachable by `Tab` in visual order.
*Manual:* → queued **`Stage14-BatchCli-Testing-Guide.md`**. Emphasis: a full keyboard-only pass
through the entire application; a 1,000-file batch run; a watched folder fed by a slow network copy.

**Exit criteria.** Sequences build, save, share, and run over files, folders, trees, watched folders,
and open documents; **every mutating action is reachable from both a sequence and the CLI**; the
whole application is operable by keyboard alone; the diagnostics bundle contains what support needs
and no document content.

**Deferrals.** Scheduled sequences (needs a service; Stage 15's installer work).

---

### Stage 15 — Packaging, licensing, deployment, release gate

**Status:** ⚪ **Not started.** Delivers **v1.0**.

**Objectives.** [`Features.md` §9.4, §9.8, §9.9](./Features.md#98-licensing-activation-and-updates):
installers, shell integration completion, activation, updates, administrative policy, telemetry, and
the [compliance release gate](./Tech-Stack-Plan.md#83-release-gate-must-all-be-ticked).

**Components & responsibilities.**
- `installer/` — WiX v4 MSI with per-feature selection, plus a per-user MSIX; `windeployqt
  --compiler-runtime` integration keeping the Qt **and** GCC runtime DLLs beside the executable and
  replaceable. WiX packages a file tree and is indifferent to which compiler produced it.
- The **symbol archive** for this release: DWARF is not PDB, so the unstripped binaries are archived
  by build ID and the crash reports symbolized against them
  ([`Tech-Stack-Plan.md` §6.3](./Tech-Stack-Plan.md#63-build-and-dependency-management)).
- `platform/win/WinShellIntegration` (complete: thumbnail provider, preview handler, index filter,
  jump list, taskbar progress, context-menu verbs).
- `core/licensing/{Entitlement,ActivationClient,OfflineActivation}`.
- `core/update/{UpdateChecker,DeltaUpdater}`.
- `core/settings/PolicyOverlay` (complete) + an ADMX-style policy template.
- `core/telemetry/{EventSink,EventCatalog}` — opt-in, off by default.
- `ui/dialogs/{ActivationDialog,UpdateDialog,AboutDialog,ThirdPartyNoticesDialog}`.

**Key bindings handled** — none new.

**Data / settings keys.** `updates/channel`, `updates/checkIntervalHours`, `updates/autoDownload`,
`updates/disabled` (policy-lockable), `general/telemetryOptIn`, `general/portableMode`,
`licensing/entitlement`, `licensing/seat`.

**Cross-cutting additions.** No new commands. i18n: **every** catalog completed and reviewed for
`en`, `zh-TW`, `zh-CN`, `ja`; `i18n-lint` requires 100% coverage for a release build, which is when
D6 stops being a convention and becomes a ship blocker.

**Algorithms & notes.** Delta updates diff against the installed version and fall back to a full
download on mismatch. Offline activation is a signed request/response file pair with no network at
any point. Portable mode redirects every user-data path to a folder beside the executable, which is
tested by running from a read-only location and confirming a clear diagnosis rather than a crash.

**Testing.**
*Automated:* installer smoke tests (install, launch, uninstall leaves no orphan registration, and
per-feature selection works); `tst_activation` (online, offline, deactivate, expiry, trial);
`tst_update` (delta apply, fallback, signature verification); `tst_policy` (a locked key cannot be
changed and reports which policy locked it); `tst_telemetry` (**off by default; no document content,
file name, or path ever appears in a payload**).
*Manual:* → queued **`Stage15-Packaging-Testing-Guide.md`**. Emphasis: clean-VM install; upgrade
over a previous version; silent enterprise deployment with a policy template; **the compliance
release gate walked item by item, including physically replacing a Qt DLL with an independently
built one and confirming the app still launches**.

**Exit criteria.** MSI and MSIX install, upgrade, and uninstall cleanly on a clean VM; shell
integration works in Explorer; activation works online and offline; updates apply; the policy
template locks settings; telemetry is off by default and provably content-free; **every item in the
Tech-Stack-Plan §8.3 release gate is ticked**; all four locales are 100% translated.

**Deferrals.** Everything in Stage 16. macOS and Linux packaging (the `platform/` boundary makes
this a future project, not a rewrite).

---

### Stage 16 — Post-v1 subsystems

**Status:** ⚪ **Not started.** Post-v1. The items are **independent of each other** and can be
scheduled by demand rather than by dependency. Each is sized as its own mini-stage with its own
exit criteria; they are grouped here because none of them reshapes the core.

| Item | Feature source | Principal components | Notes |
|---|---|---|---|
| **OCR** | [§2.1](./Features.md#21-create-a-pdf) | `core/ocr/OcrPipeline`, Tesseract behind `IOcrEngine` | Searchable-image and editable-text modes. Search picks up the OCR layer with no change to Stage 5. |
| **Scanner** | [§2.1](./Features.md#21-create-a-pdf) | `platform/win/WinScanner`, `ui/dialogs/ScanDialog` | ADF, duplex, deskew, despeckle, blank-page drop. Needs physical hardware to verify. |
| **Virtual printer** | [§2.4](./Features.md#24-office-and-printer-integration) | `platform/win/WinPrinterDriverService`, a print monitor | The heaviest OS integration in the product; ships as a separate installer feature. **The likeliest place risk R8 bites**: the WDK's print-driver build rules are MSVC-shaped, so this component may have to be built with a second toolchain and talk to us across the platform layer's existing C/COM boundary. Decide when it is scheduled, on a spike, not now. |
| **Office add-ins** | [§2.4](./Features.md#24-office-and-printer-integration) | `platform/win/WinOfficeBridge`, five add-in projects | Word/Excel/PowerPoint/Visio/Outlook ribbons. COM automation without ATL/`#import` means hand-written interface plumbing under GCC — more code, no architectural change (R8). |
| **Browser extensions + web capture** | [§2.1](./Features.md#21-create-a-pdf) | `platform/win/WinBrowserBridge`, three extension projects | Native-messaging host plus link-depth crawling. |
| **PDF Portfolios** | [§2.2](./Features.md#22-pdf-portfolios) | `core/portfolio/*`, `ui/dialogs/PortfolioDialog` | A container format with its own navigation model. |
| **Catalog full-text index** | [§1.8](./Features.md#18-search-and-find-) | `core/text/CatalogIndex` implementing `IIndexer` | First-party inverted index — **risk R2**; Xapian is excluded by license. |
| **Multimedia & 3D** | [§3.4](./Features.md#34-rich-media-and-3d) | `core/media/*`, `core/three/*`, U3D/PRC loaders | The largest post-v1 item; 3D is effectively its own renderer. |
| **Shared review & collaboration** | [§4.3](./Features.md#43-sharing-and-collaboration) | `core/review/*`, a sync transport | Needs a server-side decision that does not exist yet. |
| **AIP / rights management** | [§7.8](./Features.md#78-secure-delivery) | `platform/win/WinRightsManagement` | Enterprise labels, tracking, revocation. |
| **AI assistant** | [§1.7](./Features.md#17-ai-assistant) | `core/ai/*` behind a provider interface | Must be disableable with a *provable* no-transmission guarantee and a per-document opt-out. |
| **Convert to Writable Excel** | [§3.1](./Features.md#31-the-dual-mode-model-) | `core/reflow/TableExtractor` | Depends on Stage 13's converter quality. |
| **PDF/X profiles, EPUB export** | various | — | Small, independent. |

**Testing.** Each item carries its own unit suite and, where it touches hardware or the OS, its own
manual guide. The scanner, virtual printer, and Office add-ins **cannot** be verified without real
hardware and real installed Office; that constraint is stated up front rather than discovered.

**Exit criteria.** Per item, defined when it is scheduled.

---

## 5. Per-stage testing guides

**Every stage gets one guide, written immediately after that stage's plan and before its first line
of code.** Sequence, per stage: plan (§4, optionally expanded into a `Stage<N>-<Area>-Plan.md` — see
below) → guide → implement → run the guide. Writing the guide from
the plan is what keeps verification independent of the implementation; written afterwards it only
ever confirms what was built, including the parts that were built wrong.

Each guide carries **two sections**, per §2:

- **Automated** — the exact commands, with pass conditions, that the implementer runs: `checks`, the
  stage's `ctest` suites plus all prior ones, golden and round-trip where they apply,
  `portable-asan` / `portable-fuzz`, and any `tests/bench` figure the stage owes.
- **Manual** — numbered cases (ID · Goal · Pre · Steps · Expect · If-it-fails) for what cannot run
  headless, walked by a person on real hardware and signed off. Anything automatable belongs in the
  section above; this one is the residue and should shrink over the project's life.

Stages whose surface is entirely headless still get a guide — its manual section is short or empty,
and that is a finding worth recording rather than a reason to skip the file. **Stages 0, 1, 2 and 3 have theirs**, each
written from its plan before its code; every other row below is a schedule, not a backlog of missing
documents.

**A stage's §4 entry may also be expanded into a `Stage<N>-<Area>-Plan.md` execution document** when
its task-level detail would swamp this file — [`Stage0-Toolchain-Plan.md`](./Stage0-Toolchain-Plan.md),
[`Stage1-AppShell-Plan.md`](./Stage1-AppShell-Plan.md),
[`Stage2-Rendering-Plan.md`](./Stage2-Rendering-Plan.md) and
[`Stage3-Mutation-Plan.md`](./Stage3-Mutation-Plan.md) are the four that exist. Such a document
owns **only** that stage's
task ordering and its stage-local design choices; the objectives, components, and exit criteria stay
here, and the stage's live status stays in its §4 status block. It sits between plan and guide in the
sequence: **§4 entry → `Stage<N>-<Area>-Plan.md` (optional) → testing guide → implement → run the
guide.**

| Stage | Guide | State |
|---|---|---|
| 0 — Toolchain & gates | [`Stage0-Toolchain-Testing-Guide.md`](./Stage0-Toolchain-Testing-Guide.md) | ✅ **Written 2026-08-17** from [`Stage0-Toolchain-Plan.md`](./Stage0-Toolchain-Plan.md), before any Stage 0 code · **not yet run** — no case has a result. 40 automated cases (A-01…A-73), 7 manual (M-01…M-07): the clean-machine bootstrap and the **real** UCRT64 wrong-compiler configure. Carries the **A5/A6** verdicts and keeps **A7** settled |
| 1 — App shell | [`Stage1-AppShell-Testing-Guide.md`](./Stage1-AppShell-Testing-Guide.md) | ✅ **Written 2026-08-19** from [`Stage1-AppShell-Plan.md`](./Stage1-AppShell-Plan.md), before any Stage 1 code · 🟡 **partly run — T1…T13 verified 2026-08-24**. 68 automated (A-01…A-85, plus **A-67** added at T7), 12 manual (M-01…M-12); the manual half is large by design, since A3 is judged on a real screen. Carries the **A3** ribbon go/no-go, **recorded**. Twenty-five issues, **none open** (I-04 closed at T8; I-12…I-16 at T8/T9; **I-17** by the first `portable-asan` run to reach Stage 1's code; **I-18…I-20** by T10's first run against a real, shown window; **I-24 and I-25 at T13, both of them the harness rather than the product** — **CI now green at `gates` 45/45 and `portable-asan` 177/177**, run 32649932954, with the `windows` job still skipped for want of a registered runner, which is why **A-80 stays open**); **fifteen of the twenty were found by running something rather than by reading it**, including both S1s |
| 2 — Rendering | [`Stage2-Rendering-Testing-Guide.md`](./Stage2-Rendering-Testing-Guide.md) | ✅ **Written 2026-08-31** from [`Stage2-Rendering-Plan.md`](./Stage2-Rendering-Plan.md), before any Stage 2 code · 🟡 **partly run — T1–T9's cases have results, the stage's EXIT CRITERION (A-21) is met on Windows, and A-46 is closed.** **78 automated** (A-01…A-78: A-75 added 2026-08-31 with I-02, A-76…A-78 at T4), **10 manual** (M-01…M-10). **Four automated cases are Linux jobs this machine cannot run — A-04, A-24, A-25 and A-26** — and are recorded as such rather than as pending. Six issues: I-01 and I-03 closed, **I-02 half-closed at T7**, I-04, I-05 and **I-06** (T7's JBIG2 scope finding) open. The manual half is **smaller than Stage 1's**, on purpose: most of this stage is pure arithmetic and is automated, so what is left is the irreducible residue — two monitors at different scaling, perceived smoothness while scrolling, fidelity judged beside a reference, and a memory checker the CI runner does not have. **Needs two monitors at different scaling** (satisfied — the rig matches A2's specification exactly) and carries the **A2** verdict, plus **O7** and **O10**. One issue, **I-01**, raised by reading rather than running and closed the same day |
| 3 — Mutation & save | [`Stage3-Mutation-Testing-Guide.md`](./Stage3-Mutation-Testing-Guide.md) | ✅ **Written 2026-09-14** from [`Stage3-Mutation-Plan.md`](./Stage3-Mutation-Plan.md), before any Stage 3 code · **T1's and T2's cases run** (A-14 … A-25, A-27, and A-28's automated half, 2026-09-14), the rest not yet; M-03's probe walk, which grades A4's probe, not yet walked. **83 automated** (A-01…A-83), **8 manual** (M-01…M-08). Carries the **A4** verdict, measured twice and judged by OpenSSL (integrity) and Acrobat (policy). ~~Needs a hex editor~~ — the byte comparison is automated against bytes read back from disk; **killing mid-edit is automated too**, with `TerminateProcess` at instants the harness must prove it reached, and the manual residue is the real product killed by hand. Five issues, all closed: **I-01** and **I-02** raised by reading and closed in the same change, **I-03** found at T1, **I-04** and **I-05** at T2 |
| 4 — Viewing surface | `Stage4-ViewingSurface-Testing-Guide.md` | ⚪ Queued |
| 5 — Text, selection, search | `Stage5-TextSearch-Testing-Guide.md` | ⚪ Queued — near-entirely automated; the manual section covers selection feel and RTL/CJK rendering only |
| 6 — Annotations | `Stage6-Annotations-Testing-Guide.md` | ⚪ Queued |
| 7 — Assembly | `Stage7-Assembly-Testing-Guide.md` | ⚪ Queued |
| 8 — Save & export | `Stage8-SaveExport-Testing-Guide.md` | ⚪ Queued |
| 9 — Forms | `Stage9-Forms-Testing-Guide.md` | ⚪ Queued |
| 10 — Security | `Stage10-Security-Testing-Guide.md` | ⚪ Queued — **needs an independent extraction tool and a hex editor in the rig** |
| 11 — Signatures | `Stage11-Signatures-Testing-Guide.md` | ⚪ Queued — **needs a physical smart card or USB token** |
| 12 — Creation & conversion | `Stage12-Creation-Testing-Guide.md` | ⚪ Queued — near-entirely automated; the manual section is fidelity spot-checks against source documents |
| 13 — Write mode | `Stage13-WriteMode-Testing-Guide.md` | ⚪ Queued — the largest guide; hand-scored conversion fidelity |
| 14 — Batch, CLI | `Stage14-BatchCli-Testing-Guide.md` | ⚪ Queued — a full keyboard-only pass over the whole application |
| 15 — Packaging | `Stage15-Packaging-Testing-Guide.md` | ⚪ Queued — **needs a clean VM**; walks the compliance gate |
| 16 | per item | One per item, written when that item is scheduled |

---

## 6. Milestone map

| Milestone | Stages | Outcome |
|---|---|---|
| **M0 — Foundation** | 0–3 | Shell, ribbon, action layer, settings, i18n; open and render a PDF at correct DPI; one reversible, journalled, incrementally-saved, crash-recoverable mutation. **Every locked decision proven.** |
| **M1 — Reader** | 4–5 | A genuinely usable PDF reader: all view modes, panes, split, full-screen, selection, copy, find, cross-file search, comparison. *Internal alpha.* |
| **M2 — Reviewer** | 6–8 | Annotate, assemble, and get output back out: full markup and comment management, all page and document assembly, Save-As, export, optimize. *Private beta.* |
| **M3 — Professional** | 9–11 | Forms, encryption, permissions, redaction, sanitize, digital signatures. *Feature-complete for the professional use case.* |
| **M4 — Author** | 12–13 | Create PDFs from other sources; dual-mode Write/Review editing. *The signature capability lands.* |
| **M5 — Release** | 14–15 | Batch, CLI, installers, activation, updates, policy, compliance gate. **→ v1.0** |
| **M6 — Beyond** | 16 | OCR, scanner, virtual printer, Office add-ins, browser extensions, portfolios, catalog index, multimedia and 3D, collaboration, AIP, AI assistant. |

---

## 7. Traceability — every catalog section to its stages

| `Features.md` section | Stages |
|---|---|
| §1.1 Workspace | 1 (ribbon, QAT, tabs, sidebar, tool finder, themes), 4 (panes, window arrangement, session restore) |
| §1.2 Page display | 2 (layout modes, zoom, fidelity controls), 4 (scroll modes, split, reading/full-screen/presentation) |
| §1.3 Rulers, grid, guides | 4 |
| §1.4 Navigation | 2 (page controls, zoom, view history), 4 (thumbnails, bookmarks, destinations), 5 (comment/field/result/difference navigation) |
| §1.5 Document comparison | 5 |
| ~~§1.6 Read Aloud~~ | ⤵ **withdrawn 2026-08-23** — see [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23) |
| §1.7 AI assistant | 16 |
| §1.8 Search and find | 5 (find, advanced search, per-document index), 16 (catalog index) |
| §2.1 Create a PDF | 12 (blank, files, clipboard, region), 16 (scanner, OCR, web capture, virtual printer) |
| §2.2 PDF Portfolios | 16 |
| §2.3 Batch conversion | 12 (profiles), 14 (batch engine, CLI, watched folders) |
| §2.4 Office & printer integration | 16 |
| §2.5 Save and export | 3 (incremental save), 8 (Save-As, export, optimize, flatten, conformance) |
| §3.1 Dual-mode model | 13 |
| §3.2 Review-mode editing | 6 (typewriter), 13 (text, objects, article threads), 3 (metadata) |
| §3.3 Write-mode editing | 13a (text, paragraph, styles, page layout, spelling), 13b (tables, illustrations, TOC, revisions) |
| §3.4 Rich media and 3D | 16 |
| §4.1 Annotation tools | 6 (all), 10 (redaction marks' apply path) |
| §4.2 Comment management | 6 |
| §4.3 Sharing and collaboration | 16 |
| §5.1–5.2 Pages & thumbnails | 3 (rotate), 4 (thumbnail navigation), 7 (everything else) |
| §5.3 Merge | 7 |
| §5.4 Bookmarks | 4 (navigate), 7 (edit, generate, TOC page) |
| §5.5 Tags & document structure | 7 (tag tree, autotag), 13 (Write mode's structure, decision O2). *Accessibility removed 2026-08-23; the structure it sat on is retained for Write mode.* |
| §5.6 Layers | 7 |
| §5.7 Headers, footers, Bates | 7 |
| §5.8 Watermarks | 7 |
| §5.9 Imposition | 7 |
| §5.10 Links and destinations | 7 |
| §5.11 Batch processing | 14 |
| §6 Forms | 9 |
| §7.1–7.4 Protection & schemes | 10 |
| §7.5 Digital IDs and trust | 11 |
| §7.6–7.7 Signing and validation | 11 |
| §7.8 Secure delivery | 11 (envelopes, password-and-email), 16 (AIP) |
| §7.9 Removing sensitive content | 10 |
| §8 Key bindings | 1 (Global, the binding store, context dispatch), then each stage wires its own context's table |
| §9.1 Preferences | 1 (shell + General + Shortcuts), each stage adds its page, 14 (completion) |
| §9.2 Persistence | 1 (settings, recent), 3 (recovery journal), 4 (session state), 6 (profile store) |
| §9.3 File formats | 2 (read + repair), 8 (write + conformance), 12 (import) |
| §9.4 OS integration | 1 (association, single instance), 8 (printing), 15 (shell handlers, jump list), 16 (printer driver, Office, browsers, scanner) |
| §9.5 Logging & diagnostics | 0 (`Log.h`), 1 (log pane), 14 (diagnostics bundle, safe mode) |
| §9.6 Command line | 1 (open-with-file), 14 (full verb set) |
| §9.7 Display & input | 2 (DPI, monitors), 4 (touch/pen), 14 (keyboard operability, focus visibility) |
| §9.8 Licensing & updates | 15 |
| §9.9 Telemetry | 15 |
| §10 Portability note | 1 (all **seventeen** interfaces declared — the figure was carried as fifteen until Stage 1 T7 measured it; **eighteen** since Stage 3 T7 added `IDurableFiles`), then each stage implements the Windows backend it needs |

---

## 8. Open decisions carried forward

| # | Decision | Owner stage | Why it is not decided now |
|---|---|---|---|
| O1 | Office export/import engine: first-party writers, `IOfficeBridge` through the installed suite, or a commercial component for that subsystem only. (Risk R3.) | 8, confirmed in 12 | Should be decided on **measured** fidelity against a labelled corpus, not on preference. |
| ~~O2~~ | ~~Whether the Write-mode reflow model is persisted between sessions.~~ **Resolved: reconstruct on each *Convert to Writable*, and make `ReflowToFixedComposer` emit a structure tree** so the model's *structure* persists as **standard PDF tags** rather than as a private stream. | ✅ **Resolved 2026-08-20** | Persisting the model **inside** the PDF was rejected on security grounds: it would carry deleted text and reviewer identity in a container that Stage 10's sanitize/redaction path — which ships **three stages earlier** — knows nothing about, so *redact → save* would leave the redacted text recoverable. A **local cache** was rejected because the same file would then reconstruct differently depending on whether an invisible cache survived, and inconsistent results are harder to support than consistent slowness. Tag emission recovers most of the benefit interoperably: [flow H](./Architecture-FileMap.md#h-convert-to-writable--crossing-into-the-second-document-model) already consults `TagTree` for *"much better fidelity"*, so a second conversion reads our tags instead of re-guessing from glyphs. *(It also used to advance PDF/UA; that benefit lapsed with the 2026-08-23 accessibility removal, and the round-trip fidelity argument — always the primary one — is unaffected.)* **Accepted cost:** tracked changes have no PDF representation, so they cannot survive a close or be shared. If shareable Write-mode review is ever required, that is a **new** decision, not a reopening of O2. **13a still owes the convergence measurement** — see its status block. |
| O3 | Whether dynamic XFA gets more than honest reporting. (Risk R4.) | post-13 | Effectively a second document format; needs demand evidence before the investment. |
| O4 | AI assistant provider and whether an on-device model is offered. | 16 | Product and privacy decision; the interface keeps it open. |
| O5 | Collaboration transport (server product, third-party sync, or shared-folder only). | 16 | Needs a business decision that does not exist yet. |
| O6 | arm64 as a shipped target versus build-only. | 15 | Depends on PDFium and dependency availability, and on demand. **MinGW-w64 GCC's aarch64 support is immature (R8)**, so this is also a question of whether arm64 means switching that target to LLVM-MinGW. |
| ~~O7~~ | ~~Whether the render cache's disk tier is shared between documents or per-document.~~ **Resolved: ONE SHARED CACHE, and the disk tier does not ship** — so the question's literal subject does not exist in the product this stage delivers. | ✅ **Resolved 2026-09-06, Stage 2 T18** | **Answered by the measurement D2-14 required, not by preference.** `tests/bench/bench_pipeline` opens eight corpus documents at once and renders each twice at 144 dpi. At the **shipped 256 MiB cap**: 400 lookups, **50 % hit rate, 0 evictions, 0 cross-document evictions** — sharing costs a document nothing at all, because nothing is competing. At a **24 MiB cap**: 0 % hits and **297 of 297 evictions cross-document** — sharing costs a document everything. **The number that answers O7 is that second column, and its shape is the answer**: a shared cache is free until the cap is too small, at which point it is catastrophic in a way a per-document cache would merely have made slow. **The right lever is therefore the cap, not the topology** — `performance/tileCacheBytes` is a settings row (T15) and a user with eight large documents can raise it, whereas a per-document split would have made the 256 MiB case *worse* by fragmenting a budget that currently has room to spare. `RenderCache::crossDocumentEvictions()` exists for exactly this and was built with the cache at T11 rather than bolted on here, which is the only reason the question could be answered with a number. **Revisit at Stage 5**, per this row's original owner column: text extraction and search add a second consumer of the same cache and change the workload this was measured over. |
| O8 | Sequenced-run scheduling: an in-app timer versus an OS-level service. | 14/15 | A service changes the installer's privilege story. |
| ~~O9~~ | ~~Product name.~~ **Resolved: the product is RawPage.** | ✅ **Resolved 2026-08-20** | Settled by you, not deferred to Stage 15. The rename was carried out across the tree in the same change: **907 occurrences in 78 files**, three CMake modules renamed, `corpus/minimal.pdf` regenerated (its xref holds absolute byte offsets, so it could not be edited), and the GitHub repo and working folder renamed. Verified afterwards at 39/39 gate cases and **66/66 tests**, `rawpage.exe` exit 0, imports `msvcrt.dll` and never `ucrtbase.dll`. **The old estimate that a rename "touches only `res/`, `i18n/`, and the installer" was wrong** — it reached every layer, the build system, the scripts and CI. |
| O11 | **How a parameterized command reaches the action layer.** `ActionRegistry::dispatch(ActionId, KeyContext)` carries no argument, and T11 produced the first two commands that need one — `ui.qatAdd` and `ui.qatRemove`, which mean nothing without *which command*. Stage 1's answer is that the surface which raised the menu records the target (`QuickAccessToolbar::pendingTarget()`) and the handler reads it: enough to keep both reachable from a test and from a headless caller, and not enough for a batch script that never clicked anything. | **14** | Flow K is where it stops being avoidable — a batch step naming `pages.rotate` has to say *which pages, and by how much*, and there is no clicking involved. Deciding it now would be designing the batch runner's argument model a stage at a time from inside a toolbar. Related and deliberately not conflated: `file.open` raises a dialog and so is refused headlessly (I-23) — the parameterized form of that one already exists as `MainWindow::openFiles`. |
| ~~O10~~ | ~~How sanitizer coverage is obtained given that MinGW GCC has none.~~ **Resolved: the PORTABLE ROUTE, both presets, on Linux CI — and NO second Windows toolchain.** | ✅ **Resolved 2026-09-06, Stage 2 T18** | **What tipped it was that the portable route stopped being a plan and started being a job.** `portable-asan` has run since Stage 0; T18 added **`portable-fuzz`** to CI, so both fuzz targets now build and run seeded on every push — which is what finally gave guide cases **A-25** and **A-26** results after three tasks of *"the target exists and has never been linked"*. Against that, an `x64-clang-asan` preset over LLVM-MinGW would buy `platform/win/` and `ui/` coverage and cost a **second toolchain pin** — which is precisely what `cmake/toolchain-mingw.cmake`'s TA-1…TA-9 exist to prevent, because a second libstdc++/CRT against this Qt is the week-long crash hunt Stage 0 decided never to have. **The residual gap is named rather than closed:** ASan cannot see the Win32 layer or the widgets, and **manual case M-09** (Dr. Memory / Application Verifier over the raster path) is the coverage for it — which is why that case was written as manual. It was called unwalkable here until 2026-09-13, when Application Verifier turned out to have been installed all along. It was run that day: the verifier was clean, and the growth clause found **I-35** (`Stage2-Rendering-Testing-Guide.md` §8), which was **weighed against the trigger below and does not meet it as written** — it is a retention rather than a memory-safety defect, which no sanitizer route catches either; its causes turned out to be in `ui/` and `pdf/render/`, and all four were found with a live-heap census rather than a sanitizer. **The trigger for reopening is a memory defect found in `platform/win/` or `ui/` that the portable jobs structurally could not have seen**, not a preference for more tooling. |

**A resolved decision keeps its row, struck through and dated, rather than being deleted** — the
same convention `Architecture-FileMap.md` uses for folded-away files. What was decided, when, and
what it actually cost is more useful to a later reader than a shorter table.
