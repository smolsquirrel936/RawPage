# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository currently is

**Stage 0 scaffolding plus the first thirteen tasks of Stage 1.** The product is **RawPage**, a
Windows-first desktop PDF editor modeled on Right PDF Professional. The design lives under
`claude-plan/`.

### Where the work is right now — read this first

**Stage 1 is IN PROGRESS. T1–T13 are complete; T14 is next.** Live status, task by task
and deviation by deviation, is the Stage 1 status block in `claude-plan/Implementation-Plan.md` —
that block is the authority, and this section is its summary.

| | |
|---|---|
| **Tests** | **297/297** on `x64-debug` and `x64-release` (66 at the end of Stage 0); the bare `gates` preset is 65/65 with no Qt |
| **Gates** | `layer-lint` clean at 125 files / 735 includes; `license-audit` clean; `i18n-lint` clean at 67 files / 2 catalogs, 0 waivers. **Three gates live**, not four — see the gate table below. `tools/ribbon-validate` is a build step, not a gate |
| **Verdicts recorded** | **A3 = `go, with named custom work`**; **`-municode` = not adopted**; A5, A6, A7 from Stage 0 |
| **Issue log** | I-01…I-25 in `Stage1-AppShell-Testing-Guide.md` §8. **None open.** I-04 closed at T8; I-12…I-16 at T8/T9; **I-17** by the first `portable-asan` run to reach Stage 1's code; **I-18…I-20** by T10's first run against a real, shown window; **I-21** and **I-22** at T11 and **I-23** at T12 — all three by running something new against the product; **I-24** and **I-25** at T13, and both of those were the *harness* rather than the product — a case that asserted the opposite of what it meant, and a gate that flagged a diagnostic string. *(Stage 1 now has its own I-15 and I-16; prose citing “I-15's lesson” or “I-20” means **Stage 0's** — the guide's §8 header says so.)* |
| **Manual pass** | **M-01 ✅ and M-02 🟡 signed off by the user 2026-08-23.** **M-03 (document tabs: tear off, re-dock) became walkable at T12, and M-12 (the stage's honest emptiness) fully at T13** — the keyboard works, so that walk can be taken as written. M-04…M-07, M-09 and M-10 are not walkable yet, and M-11 is walkable except for its Preferences step |

**Next action: T14** — `LogPane` and `PreferencesDialog` (the shell plus the General and Shortcuts
pages), every page built **from `SettingsSchema`** rather than hand-laid-out, with the Shortcuts page
editing `KeyBindingStore` and showing `shadows()`. T13 hands it two things it needs: a remap now takes
effect with nothing rebuilt, and `ShortcutBinder::qtKeyFor()` is the conversion a shortcut editor needs
to show a chord as a key and read one back. Nothing is carried forward: I-19 was paid off at T11, A-58
closed at T12 and A-53 at T13. Nothing is implemented ahead of the plan's task order.

### What now exists as code

- Stage 0's build system, six presets, and the toolchain pin (TA-1…TA-9) refusing any compiler but
  Qt's own GCC 13.1.0/msvcrt; the live gates plus the CTest fixtures proving they fail correctly.
- The four `core/` vocabulary headers (`Result.h`, `Log.h`, `Geometry.h`, `Units.h`).
- **`app/main.cpp` and `app/CrashHandler.{h,cpp}` (T1)** — per-monitor-v2 DPI awareness and the
  unhandled-exception filter both installed *before any Qt object exists*; minidumps to
  `%LOCALAPPDATA%\RawPage\crashes\` with a plain-text sidecar; `scripts/symbolize.ps1` resolves a
  dump to a function name and **refuses a binary whose build id does not match**.
- **`core/cmd/` (T3, T4)** — `ActionId.def` (**358** declared actions), `ActionId.h`, `KeyContext.h`,
  `ActionRegistry`, `Chord`, `KeyBindingDefaults.def` (**240** bindings), `KeyBindingStore`.
- **`core/settings/` (T5)** — `SettingsKeys.def`, `SettingsSchema`, `Settings`, `RecentFiles`. Typed
  keys only: there is no string overload anywhere, and three negative-compile fixtures prove a
  misspelled key, a string key and a wrong-typed value each fail to compile. D1-16's versioning is
  live in both directions — an older file migrates and is restamped, a **newer** file is read for
  what we understand and left byte-identical.
- **`tools/i18n-lint/` + `i18n/` (T6, extended T8/T9/T10/T11/T12)** — the third gate, and `rawpage_en.ts` /
  `rawpage_zh_TW.ts` at **463 keys each** (358 `action.*` + 34 `shell.*` + 61 `ribbon.*` + 10 `error.*`), declared in
  `i18n/locales.json` with `zh_CN` and `ja` marked `"shipped": false`. The `.ts` files are compiled
  to `.qm` **beside the executable** rather than into a Qt resource, so Stage 15 can ship them
  replaceable; `LinguistTools` is a build tool and is deliberately not registered with
  `rawpage_link_qt`, because `qt-modules.txt` is the LINK closure `license-audit` audits.
- **`platform/` (T7)** — `platform.h` declaring all **seventeen** OS interfaces (the figure was
  carried as fifteen until T7 measured it), `PlatformFactory`, and two implementations:
  `WinSingleInstanceBus` (D1-11 — `Local\` mutex, a named pipe whose name carries the session id,
  and `AllowSetForegroundWindow` from the secondary) and `WinShellIntegration` (per-user PDF
  association only). An unimplemented interface is a **null member**, never a silent stub.
  `tests/platform/` is a third test target, for the reason D1-4 gives about `ui/`.
- **`app/` (T8)** — `CommandLine` (a pure `QStringList` → intent parse, with **no `argc/argv`
  overload and a compile fixture proving it**), `Services.h`, `CompositionRoot` (the only caller of
  `platform::createPlatform()`; also owns the logging settings and the `Settings::sync()` on
  teardown), `Application` (the translator stack — `en` installed **underneath** the active locale,
  because the catalog key is the source string), and `SingleInstance` (policy only; scope is a hash
  of the executable's directory). `main.cpp` now runs flow A end to end.
- **`ui/shell/` (T9)** — `Catalog` + `ShellKeys.def`, `ThemeManager` (eleven closed colour tokens,
  completeness enforced by a `default`-less switch under `-Werror`), `StatusBar` (built and
  **disabled**, because there is no document), `SidebarDock` (Qt gives dock/float; **auto-hide is
  ours**), and `MainWindow`. `ui/UiModule.cpp` was deleted, as its own comment instructed.
- **The ribbon (T10)** — `res/ribbon.json` (9 tabs, 49 groups, **192 controls**, all of it data),
  `RibbonModel` (its one parser — widget-free, so `tools/ribbon-validate` can compile it in rather
  than reimplement the schema), `Ribbon` with the A3 verdict's three pieces of named custom work,
  `RibbonTabBuilder`, and `ActionFactory` + `RawPageAction` — **the only place a `QAction` is
  constructed**, enforced by a non-escapable `layer-lint` rule across four spellings and by a
  show-time self-check. `tools/spike-ribbon/` was **deleted**, exactly as D1-1 said it would be.
  There are **no icons**: a large button is a tall text button until there are assets to put on it.
- **The Quick Access Toolbar, the tool finder and the customization (T11)** — `QuickAccessToolbar`
  (add / remove / reorder / placement / reset, all five of them commands rather than context-menu
  lambdas), `ToolFinder` (whose *ranking* is a widget-free free function, so the result order is
  asserted without a window), and `UiCustomization` (versioned per D1-16, **carrying a newer build's
  unknown fields through untouched**, and holding action *string ids* so a rename cannot orphan a
  customization). The ribbon body now sits in a `QScrollArea`, which paid off **I-19**.
- **Document tabs and the stub they carry (T12)** — `DocumentTabBar` (reorder, middle-click close,
  tear-off, re-dock, overflow) and `DocumentWindow`, which under **D1-2** holds a path and nothing
  else. `openFiles()` opens a tab per accepted path, still checking only the name and the suffix and
  reading no bytes. **`rawpage.exe` opens PDFs into tabs** — as stubs that say what Stage 2 brings.
- **The keyboard (T13)** — `ui/shell/ShortcutBinder`, one per window, filtering `qApp` and answering
  only for its own window. Three separable parts: `activeContext()`, a pure walk up `parentWidget()`
  to the innermost widget that *declares* a `KeyContext`; `chordFor()`, the QtGui boundary
  `core/cmd/Chord` was created for, driven by **one table read both ways** so `unreachableBindings()`
  can say which §8 row names a key no keyboard can produce (all 248 round-trip); and `activate()`,
  which resolves and dispatches. **Only two of five outcomes consume the key** — a dispatch and an
  `Unavailable` refusal do, a `NotFound` does not. The focused widget gets **first refusal through
  Qt's own `ShortcutOverride` handshake**, so a `QLineEdit` keeps `Ctrl+A` with no list of text
  widgets to maintain. **Nothing in the Stage 1 shell declares a context**, so `activeContext()`
  answers `Global` throughout — which is the honest state, and is what every §8 Global default is
  resolved against.

**Still no document code**: nothing in `core/doc/`, `pdf/cos/`, `pdf/render/`, `pdf/ops/`, or
`ui/viewer/`. `platform/win/` holds two of seventeen interfaces. **`rawpage.exe` opens a window with a
working ribbon, a customizable toolbar, a tool finder, document tabs and — since T13 — a keyboard** —
9 tabs above a sidebar, a status bar and a live theme, and a tab per opened file whose surface names
what Stage 2 brings. Most ribbon buttons are **disabled and say why**: 358 commands are declared and
**seventeen** are registered, which is the honest state rather than a shortcut. Each of those
seventeen is reachable from the ribbon, the toolbar, the tool finder, a test, the future batch runner
**and now its §8 chord**; the other ~341 chords resolve, are refused as `NotFound`, and deliberately
leave the key unconsumed. Because an ordinary launch runs an
event loop, **the headless smoke test is `--dump-ui-strings`**: it composes every service, loads both
catalogs and exits 0 without creating a window.

### Conventions established mid-stage — these bind all later work

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
   issue **I-19** was found exactly this way.)
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
   and the reason `Ctrl+K` means two different things — and it would have put each user remap into 358
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

### Accessibility is OUT OF SCOPE (2026-08-23)

Removed from the product entirely on the user's instruction — **not deferred**. The single record is
[`Features.md` → *Removed from scope*](claude-plan/Features.md); every other document cross-references
it. Gone: screen-reader names/roles, high-contrast deference (**D1-15** withdrawn), Read Aloud with
`platform::ISpeech` (**the platform interface count is seventeen — see the T7 correction below**), the accessibility
checker, alt text, and **PDF/UA**. Criterion **R-8**, manual case **M-08** and precondition **P-2**
were withdrawn with it.

**Deliberately kept, and not to be removed as collateral:** full keyboard operability and focus
visibility (core architecture, and a power-user feature); `core/doc/TagTree` and
`core/text/LayoutAnalyzer` (Write mode's structure under decision **O2** — Stage 13 infrastructure
that also happened to serve accessibility, not the reverse); Reflow view; and the ISO 32000
accessibility *permission bit*, which any conforming reader must round-trip.

### What Stage 0's manual pass taught, and Stage 1 has now confirmed twice

Stage 0's three S1s all came from manual cases no automated run could reach. Stage 1 has repeated the
lesson from both directions: the user's **M-01** walk found that `Tab` visited the ribbon groups in
collapse-priority order rather than visual order (**I-05**) — invisible to a green offscreen run —
and that finding was then *automated*, moving half of a manual criterion into the self-test
(**I-06**). Write the manual half for what the harness structurally cannot see; when a manual finding
turns out to be measurable, move it.

`claude-plan/skills-specs-generation/` is a **vendored third-party skill set** (its own nested git
repo, MIT-ish LICENSE) that generated these docs. Don't edit it as part of product work.

## The document set — who owns which facts

Each document is the single source of truth for its own subject; the others cross-reference rather
than restate. **When something changes, fix the owning document; never re-decide a fact locally.**

| Document | Owns |
|---|---|
| `claude-plan/Features.md` | The capability catalog and the **authoritative key-binding tables** (§8). No stack choices. |
| `claude-plan/Tech-Stack-Plan.md` | Locked stack, license posture, directory layout, the inward-only dependency rule, mutation-through-commands, the first milestone. |
| `claude-plan/Architecture-FileMap.md` | What every planned file does, plus the call-trace flows A–K. |
| `claude-plan/Implementation-Plan.md` | Stage ordering, **live status**, shared conventions, testing strategy, Definition of Done, traceability. |
| `claude-plan/Stage<N>-<Area>-Plan.md` | *Optional per stage.* One stage's task-level execution order and its stage-local design choices. **Two exist:** `Stage0-Toolchain-Plan.md` (tasks T1–T15, decisions D0-1…D0-8, the §7 precondition checklist) and `Stage1-AppShell-Plan.md` (tasks T1–T15, decisions D1-1…D1-16, the §7 precondition checklist P-1…P-4). It owns no product facts and no live status — those stay in the four documents above. |
| `claude-plan/Stage<N>-<Area>-Testing-Guide.md` | How one stage is verified — **both** the automated pass I run and the manual pass you walk. **Two exist:** `Stage0-Toolchain-Testing-Guide.md` (run, signed off, closed) and `Stage1-AppShell-Testing-Guide.md`, written from Stage 1's plan before its code — **no case in it has been run**; its one issue, I-01, was raised and closed before any code. Each is written right after its stage's plan and before that stage's code, per the workflow section below. Do not write one for a stage that is not next; do not treat its absence as a gap. |

`claude-plan/README.md` is an index only and owns no facts.

**Both `Architecture-FileMap.md` and `Implementation-Plan.md` carry live status and must be updated
in the same change as the code they describe** — a file's ○ → ✅ row flip is part of that file's
definition of done. A folded-away file keeps its row as a ⤵ breadcrumb; rows are never deleted.

### Citing a label — always name the file it lives in

This document set identifies almost everything by a short label — `O7`, `D1-16`, `TA-2`, `A-31`,
`M-05`, `P-1`, `R1`, `I-01`. They are compact and unambiguous *inside* their own document and
completely opaque outside it.

**Whenever you write one of these labels in a reply, in a commit message, or in a document, give its
home on first use in that reply:** the file, and the section when the file is large.

> `O7` (`Implementation-Plan.md` §8) · `D1-16` (`Stage1-AppShell-Plan.md` §3) ·
> `A-31` (`Stage1-AppShell-Testing-Guide.md` §5.4) · `TA-2` (`cmake/ToolchainAssert.cmake`)

Repeating the label later in the same reply needs no repeat citation. A bare label with no home is a
defect in the reply, not something for the reader to go and look up.

| Label | Means | Lives in |
|---|---|---|
| `D1`…`D9` | Locked stack decisions | `Tech-Stack-Plan.md` §2 |
| `A1`…`A7` | Assumptions / go-no-go verdicts | `Tech-Stack-Plan.md` §3.3 |
| `R1`…`R8` | Open risks carried forward | `Tech-Stack-Plan.md` §10 |
| `O1`…`O10` | Open decisions carried forward | `Implementation-Plan.md` §8 |
| Flows `A`…`K` | Call traces | `Architecture-FileMap.md` §5 |
| `D0-n` / `D1-n` | Stage-local design decisions | that stage's `Stage<N>-*-Plan.md` §3 |
| `T1`…`T15` | Stage tasks | that stage's `Stage<N>-*-Plan.md` §6 |
| `V-n` (S0) / `P-n` (S1) | Blocking preconditions, yours | that stage's `Stage<N>-*-Plan.md` §7 |
| `R-1`…`R-9` | The A3 ribbon acceptance criteria | `Stage1-AppShell-Plan.md` §5.3 |
| `A-nn` | Automated test cases | that stage's `Stage<N>-*-Testing-Guide.md` §5 |
| `M-nn` | Manual test cases | that stage's `Stage<N>-*-Testing-Guide.md` §6 |
| `I-nn` | Issue-log entries | that stage's `Stage<N>-*-Testing-Guide.md` §8 |
| `S1`…`S4` | Severities | that stage's `Stage<N>-*-Testing-Guide.md` §1 |
| `TA-1`…`TA-9` | Toolchain pin assertions | `cmake/ToolchainAssert.cmake`; described in `Stage0-Toolchain-Plan.md` §5.1 |
| `WC-1`…`WC-6` | Wrong-compiler fixtures | `tests/gates/fixtures/wrong-compiler/` |

**Three namespaces genuinely collide and must always carry their scope, never just the file:**

- **`T1`…`T15` exist in *both* stage plans** and mean different tasks. Always "Stage 1 T2", never "T2".
- **`R-1`…`R-9`** (ribbon criteria) versus **`R1`…`R8`** (project risks) — one hyphen apart.
- **`A-nn`** (test cases) versus **`A1`…`A7`** (assumptions) — likewise. `A-31` and `A3` are unrelated.

The same applies to stage-scoped labels generally: `M-05`, `I-01` and `D1-2` only mean something once
the reader knows which stage's document they belong to.

## Locked decisions (expensive to reverse — do not relitigate in passing)

- **C++20 / MinGW-w64 GCC 13.1.0** — *Qt's own bundled compiler* at `C:\Qt\Tools\mingw1310_64`
  (`x86_64-w64-mingw32`, SEH, posix threads, **msvcrt** — not UCRT). `-std=c++20 -Wall -Wextra
  -Wpedantic -Wshadow -Wnon-virtual-dtor`, x64 first. `cmake/toolchain-mingw.cmake` asserts version,
  triple, and CRT and **fails configure on a mismatch**: an MSYS2 UCRT64 or newer MinGW GCC against
  this Qt is a libstdc++/CRT ABI skew, i.e. a week-long crash hunt. Upgrading Qt moves the compiler
  in the same change.
- **Qt 6.11.1 Widgets** (not QML), at `C:\Qt\6.11.1\mingw_64`, **dynamically linked under LGPL-3.0**,
  from the official binary install (not vcpkg). Never `-static` Qt; never anti-tamper the Qt DLLs —
  user-replaceability is an LGPL obligation that is release-gate tested. **Qt Charts is installed on
  this machine and is GPL-3.0-only: on disk is fine, linking it breaches D4.**
- **The Qt install is the build environment**: CMake 3.30.5, Ninja 1.12.1, `windeployqt`, and the
  binutils (`gendef`, `dlltool`, `objdump`, `gdb`, `addr2line`) all come from `C:\Qt\Tools`. A build
  box needs Qt and vcpkg, nothing else.
- **PDFium (BSD-3) for read/raster only**, linked as a **DLL through its `FPDF_*` C API** so the
  compiler that built it does not matter, behind our own facade, **plus a first-party COS layer for
  every write**. The rule in one line: *we read through PDFium and write through our own code.*
  Nothing but buffers and callbacks crosses the PDFium boundary — no C++ object, exception, or heap
  ownership.
- **No GPL or AGPL dependency may ever enter the tree** (rules out MuPDF, Poppler, Ghostscript,
  Xpdf, Qt Charts/DataVisualization, Xapian). RawPage's own source is proprietary. **One named
  exception:** the GCC runtime (`libstdc++`, `libgcc`) is `GPL-3.0-or-later WITH GCC-exception-3.1`,
  which permits proprietary distribution **only while we compile with stock GCC** — adding a
  non-free GCC plugin voids the exception and would relicense our own source. `license-audit` matches
  whole SPDX expressions, never a `GPL` substring, and ships the runtime DLLs dynamically.
- **CMake ≥ 3.25 + presets + vcpkg manifest mode (`x64-mingw-dynamic`) + Ninja**; Catch2 v3;
  MSI (WiX v4) + MSIX, shipping the GCC runtime DLLs beside the Qt ones.
- **Sanitizers and fuzzing do not run on Windows.** MinGW GCC has no ASan/UBSan runtime and no
  libFuzzer, so the `portable-asan` / `portable-fuzz` presets build `core/` + `pdf/cos/` — the two
  layers that are portable by rule — on Linux instead. A finding there is reproduced in `x64-debug`
  before it counts as fixed. Debug info is DWARF, not PDB: crash dumps symbolize against archived
  unstripped binaries.
- **Async tiled rendering** at physical DPI on a worker pool; the UI thread never parses or rasterizes.

## Architecture (once code exists)

Layers are separate static libraries (`rawpage_core`, `rawpage_pdf`, `rawpage_platform`, `rawpage_ui`) linked
by one `rawpage` executable, so the linker itself catches some violations. **Dependencies point inward
only:**

- `app/` — composition root. The **only** translation units naming `pdf::createBackend()` or
  `platform::createPlatform()`. No domain logic.
- `ui/` — Qt Widgets only. May never include `pdf/` or `platform/win/`, and may never mutate a
  `Document`; its entire mutation vocabulary is *construct a `Command`, hand it to `CommandJournal`*.
- `core/` — domain. std + `QtCore` (documented exception) + a short pure-computation allowlist (ICU,
  HarfBuzz, Hunspell) + its two interface headers (`core/backend.h`, `platform/platform.h`). Must
  build and pass its full test suite with **no display and no PDF engine linked**.
- `pdf/` — the only layer that may emit PDF bytes or call `FPDF_*`. `pdf/cos/` is first-party
  (lexer, object model, xref, filters, crypt, incremental writer); `pdf/render/` is the PDFium
  facade; `pdf/ops/` realizes each mutation.
- `platform/win/` — the only OS-specific code; implements the 17 `platform/platform.h` interfaces
  and makes no policy decisions.
- `i18n/`, `res/`, `corpus/`, `golden/` are **data, not code**.

### Mutation-through-commands — the load-bearing design

Every document change is a `Command` (`name/scope/apply/revert/serialize`) run through
`CommandJournal`. `apply()` expresses **intent** to a `MutationSink` interface (`setPageRotation(3, 90)`)
— it never touches bytes; `pdf/ops/` decides how intent becomes COS objects. Read flow D in
`Architecture-FileMap.md` §5 before writing any mutating code; every mutation in the product, from a
highlight to Write-mode typing, has exactly that shape.

Consequences that fall out of it: undo/redo, an fsync-per-command recovery journal
(`%LOCALAPPDATA%\RawPage\recovery\<docid>\`), precise `Scope`-keyed invalidation, and incremental save
(append a new xref section; Save-As is the full-rewrite compaction path).

### The action layer

Every user action is a named `ActionId`. Ribbon, QAT, context menus, chords, tool finder, CLI, and
batch all resolve to one and call `ActionRegistry::dispatch(ActionId, KeyContext)`. **Nothing is wired
directly to a widget slot.** `KeyContext` is computed from the focus chain by `ui/shell/ShortcutBinder`,
which is also the only path from a key press to a command — **no `QAction` carries a shortcut** (see
convention 15). This is what makes the same chord mean different things per context (twelve deliberate
Write-mode collisions are specified in `Features.md` §8) and what makes the headless batch runner and
CLI possible without a second implementation.

## Non-negotiable rules when writing code

These are machine-enforced by CI gates, not by review discipline. **Four are specified; THREE are
live.** `checks` runs the three that exist and says three — if it ever *claims* four, that is the bug.

| Gate | State | Fails on |
|---|---|---|
| `tools/layer-lint` | ✅ **live** | an outward include; `FPDF_` outside `pdf/`; a filesystem write outside `pdf/` and `core/settings/`; a path read out of `argv` outside `app/main.cpp`; **a `QAction` constructed outside `ui/shell/ActionFactory.cpp`** (D1-7, four spellings, non-escapable); **an OS header in `platform/platform.h`**, which is its own layer entry because that file is included by `core/`, `ui/` and `app/` |
| `tools/license-audit` | ✅ **live** | any dependency SPDX identifier outside the allowlist |
| `tools/i18n-lint` | ✅ **live** | a literal reaching a user-facing sink and not wrapped in `tr()` (escapable, `// i18n-lint: allow-literal(reason)`); a key missing from a shipped locale; an `ActionId` with no `action.*` catalog entry. `orphan-key` is S3 — reported, never blocking. Stage 1 ships `en` and `zh-TW` only; `zh-CN` and `ja` are declared `"shipped": false` (D1-14) |
| `tools/mutation-lint` | ○ **Stage 3** | a `Command` subclass missing from `CommandRegistry`, or lacking a serialize round-trip test or a revert test |

Two rules are escapable, both with an inline marker **on the same line as the match** — not the line
above — and both censused by that gate's `--explain`: `// layer-lint: allow-filesystem-write(reason)`
(**four** waivers exist — three in `app/CrashHandler.cpp`, one on the named-pipe `CreateFileW` in
`platform/win/WinSingleInstanceBus.cpp`) and `// i18n-lint: allow-literal(reason)` (none yet). `--explain` distinguishes a waiver that **silenced something** from one that did not, which is
Stage 0's I-20 written into the tool rather than remembered.

Plus the standing conventions:

- **A new `Command` ships with its inverse and its tests, or it does not ship.**
- **Every dialog collects parameters and constructs a `Command`.** Document logic in a dialog is a
  rejection, not a style note.
- **No feature that the headless batch path (flow K) cannot reach.** Reachable only from a widget
  means unfinished.
- Every settings key is declared once in `SettingsSchema`; an undeclared key is a compile error.
- `Result<T>` at boundaries — no exception crosses a layer.
- Warnings are errors in `core/` and `pdf/`; `ui/` has a grace list that may only shrink.
- Partial application is a bug: `apply()` returning false must leave the document untouched.
- A repaired (reconstructed-xref) document's first save is forced to Save-As.

## Commands

**`cmake`, `ninja` and `g++` are not on this machine's `PATH`.** Dot-source the environment first;
a command that works only *without* it is itself a finding.

```powershell
. .\scripts\env.ps1
cmake --preset x64-release
cmake --build --preset x64-release --target checks    # the live gates; CI runs this BEFORE compiling
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure        # 275/275 as of Stage 1 T12
```

Diagnostics the entry point answers (T1), all usable under `QT_QPA_PLATFORM=offscreen`:

```powershell
.\build\x64-release\bin\rawpage.exe --version              # name, build id, Qt version
.\build\x64-release\bin\rawpage.exe --print-dpi-awareness  # expect PerMonitorAwareV2
.\build\x64-release\bin\rawpage.exe --print-args <paths>   # the D1-3 argv-vs-Qt measurement
.\build\x64-debug\bin\rawpage.exe   --crash-test           # debug only; writes a minidump
.\scripts\symbolize.ps1 -Dump <path.dmp>                   # resolves the faulting frame
.\build\x64-release\bin\rawpage.exe --dump-ui-strings     # every shell key + action label, resolved
.\build\x64-release\bin\rawpage.exe --lang zh-TW --dump-ui-strings   # guide case A-72
.\build\x64-release\bin\rawpage.exe --help                # the parser's own vocabulary; exit 2 on a usage error
```

The three gates can also be run one at a time, and each has an `--explain` that prints its rules and
its live waivers — that census is how anyone notices waivers creeping from a handful to a hundred:

```powershell
.\build\x64-release\bin\layer-lint.exe    --root . --explain   # includes, rules, 4 live waivers
.\build\x64-release\bin\i18n-lint.exe     --root . --explain   # locales, shipped flags, unfinished count
.\build\x64-release\bin\license-audit.exe --help
.\build\x64-release\bin\ribbon-validate.exe --explain   # NOT a gate -- a build step (D1-8)
```

**Two notes on running `rawpage.exe` from PowerShell**, both of which cost time to rediscover.
PowerShell does **not wait** on a GUI-subsystem binary, so `$LASTEXITCODE` after `& .\rawpage.exe`
reads the *previous* command's — use `Start-Process -Wait -PassThru` and read `.ExitCode`. And an
ordinary launch now runs an event loop and does not return; `--dump-ui-strings` is the switch that
composes everything and exits.

Presets: `gates` (tools + gate fixtures only, **no Qt and no vcpkg** — what CI runs first),
`x64-debug`, `x64-release`, `arm64-release`, plus the two non-Windows ones — `portable-asan`,
`portable-fuzz`.

**Three test targets, and the split is load-bearing rather than organizational:**

| Target | Links | CTest prefix | Runs where |
|---|---|---|---|
| `rawpage_tests` | `rawpage_core`, `rawpage_pdf` | `unit.` | Windows + `portable-asan`. It also compiles `app/CommandLine.cpp` **in** — the parser is a pure `QStringList` → intent function, and compiling it rather than linking the executable is what keeps the assertion below true. `rawpage_assert_headless()` **fails configure** if this ever links `rawpage_ui` or `rawpage_platform`, directly or transitively — that assertion is the standing proof that `core/` needs no display and no engine |
| `rawpage_platform_tests` | `rawpage_core`, `rawpage_platform` | `platform.` | **Windows only** (S1 · T7) |
| `rawpage_ui_tests` | `rawpage_core`, `rawpage_ui` | `ui.` | **Windows only**, `QT_QPA_PLATFORM=offscreen` (D1-4). **Arrived at T9**, not the plan's T15, on the precedent `tests/platform` set at T7. It installs `rawpage_en.qm` so the widgets are exercised against real text rather than their own catalog keys |

Single test / suite — Catch2 v3 binaries registered with CTest, selected by name regex:

```
ctest --preset x64-debug   -R tst_settings --output-on-failure
ctest --preset x64-release -R "^platform\."      # the OS layer's suite (19 cases)
ctest --preset x64-release -R "^gates\."         # the gates against deliberately bad input
ctest --preset x64-release -R "unit.settings.compile"   # the negative-COMPILE fixtures (A-41)
ctest --preset x64-release -R reversibility     # apply+revert restores the structural hash (S3)
ctest --preset x64-release -R roundtrip         # incremental save keeps untouched bytes identical (S3)
```

Verification specifics live in `Tech-Stack-Plan.md` §9; the five-layer test strategy (unit, golden
image, corpus round-trip, fuzz, manual guides) plus `tests/bench/` is in `Implementation-Plan.md` §2.

## Per-stage workflow — plan → testing guide → code

**As soon as a stage's plan exists in `Implementation-Plan.md`, write that stage's
`Stage<N>-<Area>-Testing-Guide.md` — before any of the stage's code.** The plan says what gets built;
the guide says how we will know it works. Writing it first is what keeps verification from being
reverse-engineered out of whatever happened to get implemented. Use the vendored `testing-guide`
skill for the shape. One file per stage, holding **both** of these:

| Part | Who runs it | What goes in it |
|---|---|---|
| **Automated — I run it** | Claude, in-session | Everything I can execute and read the result of: the `checks` target, the stage's new `ctest` suites *and every prior stage's*, golden-image and round-trip suites, `portable-asan` / `portable-fuzz` where they apply, and `tests/bench` figures where the stage carries a measurement (A2). Each case names the exact command and its pass condition, so a run either passes or yields a specific failure — never "looks fine". |
| **Manual — you run it** | The user, on real hardware | Only what genuinely cannot run headless: window/ribbon behaviour, multi-monitor and mixed-DPI, input devices, shell integration, printing, scanning, physical tokens. Numbered cases with ID · Goal · Pre · Steps · Expect · If-it-fails, so a result is reproducible and a failure is reportable without a conversation. |

- **If it can be automated, it belongs in the automated part.** The manual part is the residue, and
  it should shrink as the harness grows; a manual case that could have been a test is a defect in the
  guide, not a fact about the product.
- I run the automated part and report **actual** output. A failing suite is reported as failing, with
  the output — never smoothed over, never declared fixed without a re-run.
- I hand you the manual part and **never mark a manual case passed on your behalf.** "Automated green,
  manual not yet walked" is a legitimate and useful state to report; "done" is not, until you say so.
- The guide is written from the plan and corrected in the same change as any later design shift, like
  every other live document here.

## Definition of Done for any stage

Code complete · `checks` clean · `ctest` green including all prior stages · golden and round-trip
suites green · the stage's testing guide **written from its plan before the code**, its automated
half run green by me and its manual half walked and signed off by you · no open S1/S2 issue ·
`Architecture-FileMap.md` rows flipped ○ → ✅ in the same change · `Implementation-Plan.md` status
block updated to reality, deviations included.

## Open risks and decisions to respect

**A3 is SETTLED** (2026-08-23, Stage 1 T2): **`go, with named custom work`** — Qt Widgets host a real
ribbon, and the fallback (a first-party ribbon widget, *not* a toolkit change) is not in play. The
named work T10 inherits is a collapse `QLayout` (~60 lines, because stock layouts distribute *space*
whereas collapse changes what a child *is*), a chevron tab overflow over `QTabBar` (~30), and an
explicit tab-order pass on every change of the collapsed set (~15, and the only one that was not
predicted). Recorded per criterion in `Tech-Stack-Plan.md` §3.3 — **including that the verdict rests
on eight criteria, not nine, because R-8 was withdrawn rather than cleared.**

Still open: **A2** (p95 per-tile render latency under ~16 ms, Stage 2) and **A4** (does a signature
survive a later unrelated incremental save, Stage 3). **A5**, **A6** and **A7** were settled in
Stage 0 — we build with Qt's own bundled GCC 13.1.0 and `toolchain-mingw.cmake` keeps it that way. Larger unknowns carried forward: the Write-mode reflow engine
(R1), a first-party full-text index since Xapian is GPL (R2), Office export fidelity (R3), the
sanitizer/PDB capability loss from the GCC toolchain (R7), and the MSVC-shaped corners of the Windows
integration surface — print driver, Office add-ins, arm64 (R8). Open decisions O1–O10 are listed in
`Implementation-Plan.md` §8 — if work forces one, record the resolution there rather than deciding it
inline.
