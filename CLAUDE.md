# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository currently is

**Stage 0 scaffolding plus all fifteen tasks of Stage 1.** The product is **RawPage**, a
Windows-first desktop PDF editor modeled on Right PDF Professional. The design lives under
`claude-plan/`.

### Where the work is right now — read this first

**Stage 1's CODE IS COMPLETE — T1–T15 all done, and the automated half is green in CI.** What is left
is **your manual walk**, and nothing else. Live status, task by task and deviation by deviation,
is the Stage 1 status block in `claude-plan/Implementation-Plan.md` — that block is the authority,
and this section is its summary.

| | |
|---|---|
| **Tests** | **354/354** on `x64-debug` and `x64-release` (66 at the end of Stage 0; 334 at T15, +5 from the M-03 fixes, +2 from the M-04 fixes, +2 from M-05's, +4 from M-06's, +6 from M-11's I-47, I-49, I-50, I-52 and the toolbar lock, +1 from M-12's I-54); the bare `gates` preset is **66/66** with no Qt. Plus **one benchmark**, `bench.ribbon.tab_switch` — a bench reports a figure and its exit code is about whether the measurement was *possible*, never whether the machine was fast |
| **Gates** | `layer-lint` clean at **141 files / 972 includes**, **6** live filesystem-write waivers; `license-audit` clean; `i18n-lint` clean at **78 files / 2 catalogs, 0 waivers**. **Three gates live**, not four — see the gate table below. `tools/ribbon-validate` is a build step, not a gate |
| **Verdicts recorded** | **A3 = `go, with named custom work`**. T15 **re-measured R-9 against the shipped ribbon** rather than against T2's throwaway spike — p95 **0.471 ms** against a 16.667 ms budget, over 192 real controls, with a control proving the instrument was live. Both figures stand in `Tech-Stack-Plan.md` §3.3; the second is the one that is a claim about the product. **`-municode` = not adopted**; A5, A6, A7 from Stage 0 |
| **Issue log** | I-01…I-55 in `Stage1-AppShell-Testing-Guide.md` §8. **None open.** **I-54 (S2)**: the tool finder took only the **first character** of a query **and lost its blinking caret** — one report, two mechanisms, because opening a `Qt::Popup` takes both the keyboard and the window activation away from the widget behind it. Keys: a popup is a WINDOW, so once it opened Qt delivered every further keystroke to it rather than to the search box, and a `QListWidget` spent them on its own keyboard search; Backspace went the same way, so a mistyped query could not be corrected. Caret: a `QLineEdit` in a **deactivated** window stops painting its cursor whether or not it still holds focus. **Fixed in two rounds** — the first addressed the keys only, and the user found the caret still gone by using the repaired build, I-45's lesson for the third time. `setFocusPolicy(Qt::NoFocus)` had carried the comment *“typing stays in the box”* since T11 and could never have made it true: focus policy picks a widget **within** a window, and this is about which **window** the keyboard talks to. `eventFilter` now keeps Up/Down/PageUp/PageDown/Enter and forwards the rest to the line edit, and the list carries `WA_ShowWithoutActivating` and a focus proxy back to the box — with `Qt::NoFocus`, **all three of the things `QCompleter` does with its own popup**, which this widget is in all but name. **The third defect this one popup has produced** (I-44, I-45), and nine green `[toolfinder]` cases missed all of it because every one types at `searchBox()` — the one address a real keystroke does not reach while the list is up: convention 19's back door, I-34's and I-43's shape. **Raised by the user after M-12 was already signed off**, which re-opened and re-closed the no-open-S2 box. **I-53** is not a defect: plan §5.1's *registered-but-disabled* tooltip was never built — an unregistered control says it is not implemented, one that is merely unavailable says nothing — and the user's call was **planned absence, Stage 2**, since 338 of 361 ids are unregistered and the few registered-and-disabled ones are refused for reasons visible on screen. **I-50 (S3)**: only the 22 *enabled* commands could be added to the toolbar — Qt sends no input event to a disabled widget, so right-clicking any of the other 338 did nothing — and the toolbar's own empty space offered an `Add` that could never be enabled. `Ribbon::contextMenuEvent` now resolves the position with `childAt()`, which does return disabled children, and the menu is keyed on what is under the cursor. **I-51 (S3)**: the startup line printed the DROPPED override count as the applied one, so a session that applied a remap perfectly logged `0 of 1 user overrides applied` — a wrong diagnostic that sent its reader hunting a defect that was not there. **I-52 (S3)**: a defect in the drag feature two hours old — a slot among the buttons is not an index into the layout, so a drop past the last button landed on the stretch and the button jumped to the far right edge. **I-49 (S2)**: **every** Quick Access Toolbar customization made the toolbar vanish — a widget built with a parent that is already on screen starts hidden, and a hidden widget is an empty layout item, so add/remove/reorder/reset each produced invisible buttons and a 4-pixel strip. `ui.resetCustomization` rebuilt it invisibly too, so restarting was the only way back. Nine `[qat]` cases missed it because every one asks the command LIST, which stays correct for a row nobody can see — the sixth time this stage has been caught by that (I-34, I-42, I-43, I-46, I-47). **I-47 (S2)**: every preference lived only in memory until a **clean** shutdown — `Settings::sync()` was called from one place, `CompositionRoot`'s teardown — so a crash or a kill lost settings the user had watched take effect, which the no-OK-button Preferences dialog makes worse rather than better. Now flushed on every change, with teardown as a backstop. Missed by the suite because every settings case called `sync()` itself: convention 19's back door again. **I-48** is not a defect: two of M-11's steps tested things that do not exist — a recent-files surface (declared, unregistered, no UI: the **fifth** built-but-unreachable capability after I-34, I-42, I-43, I-46) and a window-geometry restore, which is `SessionState` at Stage 4. **I-46** is not a defect but is worth knowing: **nothing in the product can register the PDF file association** — all four calls are implemented and tested, and the only callers are the tests, which is this stage's recurring shape for the fourth time (I-34, I-42, I-43). The user's call was **planned absence, Stage 14/15**; M-07 walks against a hand-made Windows association, and its *Pre* now also names the `windeployqt` step it had always needed. **I-45 (S2)** is the one to remember: it was a **regression in I-44's own fix** — dismissing the list on any `FocusOut` closed it in the same turn it opened, because showing a `Qt::Popup` *is* a focus change, so no search showed anything. Found by the user re-walking the repaired build within the hour, and invisible to the case added with I-44 because offscreen that focus change does not happen. **I-44 (S2, and the worst defect this stage has produced)**: a tool-finder query that matched nothing **froze the application** — the result list is a `Qt::Popup` window holding the mouse grab, the *"nothing matched"* message is itself a row so the list still showed, and `Esc`, an outside click and `Enter` all failed to put it away. Force-close was the only exit. Three exits added, each independently missing, plus `FocusOut`/`WindowDeactivate`. **I-43 (S3)**: reordering the Quick Access Toolbar could not be done by a person — `moveCommand` public since T11, the order persisted since T11, **three documents calling it shipped**, and no menu item, no drag and no `ActionId` to reach it; the only caller was a test that called the method. I-34's shape, and the third instance this stage. Fixed with `ui.qatMoveLeft` / `ui.qatMoveRight`, each disabled at the end it cannot move towards, and the case asserted the **route** (the real context menu offers them) as well as the dispatch, with a control for each. **Superseded hours later in the same walk**: the user asked for drag-to-reorder, so `buildMenu` dropped both items and `ui.qatLock` joined them — the two commands stay **registered and unlisted** (the tool finder and flow K still reach them), and the case's route half was *inverted rather than deleted*, now asserting they are registered **and absent from the menu**. Those three ids are **the only ones Stage 1 has added to the plan's 358**, giving 361 declared and 23 registered. **I-42 (S2)**: pressing `Alt` did nothing in any locale — the ribbon has *drawn* key tips since T10 and nothing ever asked it to, because `AppRibbonKeyTips` was bound to `Alt` and never registered. **R-5 had been recorded as met against the T2 spike**, whose handler the shipped ribbon never inherited — I-36's shape again. Registered (the **twentieth** id), and the tip letters moved out of the tab titles into a `ribbon.keytip.*` catalog family, because the first-character scheme gave `zh-TW` 常 建 編 註 組 表 保 檢 — untypable, with 表 claimed twice. **I-41 (S2)**: Qt's native `windows11` style paints control text from the colour scheme **it** derives from Windows and ignores the application palette, so the dark theme was in force for every `QLabel` and for **no** `QToolButton` — the ribbon, the toolbar and the tool finder. Measured on a light OS with the dark palette installed: an enabled button's glyph was `#1f1f1f` on a `#1f1f1f` background, a disabled one `#181b1f`. `apply()` now calls `QStyleHints::setColorScheme()` as well, `Unknown` for follow-system so the OS keeps the decision. Found only because the I-40 fix below did not work — the token table was the one place anyone had looked. **I-40 (S3)**: in dark mode the shell's **disabled** text could not be read — and disabled text is most of this shell, since 358 commands are declared and 19 registered. Measured 3.05 dark and **2.54 light**, so the light theme two manual passes had already signed off was the worse of the two; both raised (4.77 / 3.87 against the window), with a new case measuring every text token against the surface it is painted on, in both themes, and asserting disabled stays under half the enabled ratio so it still reads as disabled. **Not an accessibility criterion** — that left the product on 2026-08-23 — but whether the ribbon can be read. **I-39 (S3)**: the auto-hide edge strip stayed on screen beside the pane it had just summoned — true since T9, and invisible until I-37 made a peek something a person could perform, because every earlier case reached `peek()` in a fixture where visibility was not the subject. **I-37 (S3)**: an auto-hidden sidebar could be revealed only by *clicking* the edge strip, never by reaching for it — closed by the user's decision to move the product to the guide rather than the guide to the product, so the strip now peeks on `QEvent::Enter` and still answers a click. **I-38** is not a defect: `general/touchMode` is stored and read by nothing, and the user's call was **planned absence, Stage 14** — it is now a row in the guide's §1 and in `Stage1-AppShell-Plan.md` §11 rather than an open issue, and M-04's steps no longer ask for it. **I-36 (S2)**: opening M-04 found that **no ribbon tab was ever drawn** — `QSizePolicy::Ignored` tells QBoxLayout the tab bar's width preference is not a number, and next to a separate `addStretch(1)` the spacer took the whole strip, so the `QTabBar` was laid out **0 px wide at every window size** from T10 onward. Eleven green ribbon cases missed it because each asked the bar what it *contained* and none asked what the **layout gave it**; fixed, with a case and its control. **I-33, I-34 and I-35 were raised by the user's M-03 walk on 2026-08-30 and are Stage 1's first findings in the PRODUCT rather than in the harness** — every earlier one was a test, a gate, a document or the CI. **I-34 (S2)**: re-docking a tab into another window could not be performed by a person, because `DocumentTabBar` reported the gesture at the instant the 40 px threshold was crossed rather than at the release, giving the drag one sample to land on a ~25 px strip; the branch it fed was correct and had never been driven by test or by hand. **I-33 (S3)**: tearing a window's ONLY tab out left the original with no tabs, because `detachDocument` guarded the index but never the count. **I-35 (S3)**: a window emptied by re-docking its last tab away stayed open — reachable only *after* I-34 was fixed, which is why a repaired case is re-walked rather than signed off on the strength of the repair. All fixed the same day — five cases, five negative controls, 339/339. I-04 closed at T8; I-12…I-16 at T8/T9; **I-17** by the first `portable-asan` run to reach Stage 1's code; **I-18…I-20** by T10's first run against a real, shown window; **I-21** and **I-22** at T11 and **I-23** at T12 — all three by running something new against the product; **I-24** and **I-25** at T13, and both of those were the *harness* rather than the product — a case that asserted the opposite of what it meant, and a gate that flagged a diagnostic string; **I-26…I-28** at T14 — a remap that *added* a chord instead of replacing one (found by the first caller that ever remapped for a user), a gate about to report forty notes nobody could act on, and a log file that could not name the binary that wrote it; and **I-29 and I-30** at T15, again the harness and its infrastructure rather than the product — a benchmark whose every printed figure was its own argument-evaluation bug, and (the S2) **a CI job that had never run because two systems had to agree on a variable name and nothing checked that they did**. *(Stage 1 now has its own I-15 and I-16; prose citing “I-15's lesson” or “I-20” means **Stage 0's** — the guide's §8 header says so.)* |
| **Manual pass** | **M-01 ✅ signed off 2026-08-23; M-02 ✅** — signed off 2026-08-23 for the ribbon, then **re-walked in full on 2026-08-30** against the shipped shell: all four steps including the mid-session scale change and the re-login, **no findings**, which retired R-6's last scoping note and satisfied precondition P-1 outright. **M-03 ✅ walked and signed off 2026-08-30**, in full — and it found **I-33, I-34 and I-35**, the stage's first three defects in the product, all fixed and the case re-walked before sign-off. **M-03 became walkable at T12 and M-12 at T13. T14 made M-11 fully walkable and unblocked M-02's non-ribbon half** — its steps name the sidebar, a floating pane *and Preferences*, and T14 made all three real, which is what let that re-walk happen as written. **M-04 ✅ walked and signed off 2026-08-30**, after **three passes and four findings**, each repair making the next defect reachable: it stopped at step 1 because no View tab existed to click (**I-36**); the second pass, on the repaired build, produced **I-37** and **I-38**; fixing I-37 created a gesture that had never existed, and walking *that* found **I-39**. None was a regression — two had shipped since T9 and T10, green in CI throughout. **M-05 ✅ walked and signed off 2026-08-31** — mis-listed as unwalkable until 2026-08-30, then it found **I-40** and **I-41**, the second only because the first fix changed nothing a person could see. **M-06 ✅ walked and signed off 2026-08-31**, the longest walk of the stage: every surface in `zh-TW`, the editorial review of 29 unfinished entries, and four defects — **I-42** (`Alt` raised no key tips in any locale), **I-44** (a query matching nothing froze the application), **I-45** (the fix for it closed the list in the same turn it opened) and **I-43**, which surfaced here and belongs to M-11. **M-07 ✅ walked and signed off 2026-08-31** — flow B end to end by hand, all five steps, one process throughout, both awkward filenames correct. **M-11 ✅ signed off 2026-08-31**, the case that found the most: **I-47**, **I-49**, **I-50**, **I-51**, **I-52** — the last a defect in a fix made hours earlier during the same walk — plus the drag-to-reorder and lock feature the user asked for mid-case. **M-12 ✅ walked and signed off 2026-08-31 — the last case of the stage.** No S1: nothing claimed to have saved, printed or undone anything, the stub tab did not pretend to be a document, and **no chord was intercepted by the window manager, the IME or the platform before Qt saw it**, which is the one thing in that case no synthetic key event could have established. Its one finding, **I-53**, was disposed of as a planned absence. **All nine walkable cases are now ✅** — M-01 … M-07, M-11 and M-12; M-08 was withdrawn with accessibility and M-09 and M-10 are not walkable |

**STAGE 1 IS COMPLETE. Every Definition-of-Done item is closed as of 2026-08-31**, the manual walk
included — it was the last one, and it was never code. M-01 … M-04 were signed off on 2026-08-30,
**M-05, M-06, M-07, M-11 and finally M-12 on 2026-08-31**. M-08 was withdrawn with accessibility;
M-09 and M-10 are not walkable, their subject being code no stage before Stage 2 writes. **I do not
mark a manual case** — every one of these was walked and signed off by the user.

**Next action: Stage 2 T1.** Stage 2 is where documents start being opened, parsed and drawn — and
where **I-53** stops being theoretical (see below). **Its two planning documents are written and its
code is not started**, which is the workflow's intended state: `Stage2-Rendering-Plan.md` (decisions
**D2-1 … D2-14**, tasks **T1 … T18**, preconditions **P-1 … P-4**) and
`Stage2-Rendering-Testing-Guide.md` (**74 automated**, **10 manual** cases, none run), both dated
2026-08-31. **Stage 2's `T1`…`T18` are a different namespace from Stage 0's and Stage 1's `T1`…`T15`**
— always say which stage. Three things that plan settles are worth knowing before touching the code:
**`rawpage_pdf` must split** into `rawpage_cos` (portable, first-party) and `rawpage_pdfium` (Windows,
links the DLL), because `tests/unit`'s `rawpage_assert_headless()` claims *“no display and no PDF
engine”* while forbidding only `rawpage_ui` and `rawpage_platform` — the moment `PdfiumInit.cpp` joins
`rawpage_pdf`, that claim is false and `portable-asan` stops configuring (D2-2); **A2 is measured
twice**, a throwaway probe at T3 and the shipped pipeline at T18, which is Stage 1 T15's R-9 lesson
written down in advance (D2-1); and **four `core/doc/` vocabulary types come forward from Stage 3** —
`Revision.h`, `Scope`, `Page` and a read-only `PageTree` — because three Stage 2 signatures already
name them (D2-5). All three are recorded in `Stage2-Rendering-Plan.md` §3.

**M-12 produced one finding, disposed of as a planned absence rather than fixed.** **I-53**: plan
§5.1 (`Stage1-AppShell-Plan.md`) requires a disabled control to say *which kind* of disabled it is —
*“arrives in Stage 7”* for declared-and-unregistered, *“not available for this document”* for
registered-but-currently-refused — and **only the first was ever built**. The second says nothing at
all: no marker, no tooltip, on the ribbon, the toolbar and the tool finder alike. The distinction is
alive everywhere except the surface — `dispatch` separates `Unavailable` from `NotFound`, the log
prints them under different keys, and `ShortcutBinder` consumes the key for one and not the other —
so this is a surface that does not say what the product knows. Deferred on the user's call because
at Stage 1 the second kind is nearly theoretical: **338 of 361 ids are unregistered**, so almost
every disabled control a person meets *is* the explained kind, and the few that are not are refused
for reasons visible on screen (no document, one tab, one window). The mechanism to fill it in exists
and is unused — a `tooltipKey` on the registration. **Stage 2 is when it becomes real.**

**Two routes named in M-12's own steps did not exist**, found while preparing the walk: there is no
File menu — `file.close` and `file.closeAll` appear in **none** of the 192 ribbon controls — and
**the ribbon can never show the registered-but-disabled case**, because only nine of the 23
registered ids have a ribbon control and all nine are unconditionally enabled. Reaching that case
takes the tool finder or a hand-edited `ui/qatCommands`. The guide's §7 caveat row now says so.

**CI is green on all four jobs** (run 32785493072), and **the `windows` job ran for the first time in
the project's history** — 334/334 there, `gates` 52/52, `portable-asan` 196/196. Its absence was never
the missing runner every document had assumed: one had been online since 2026-08-17, and the block was
a variable **name** mismatch (`RECTO_HAS_WINDOWS_RUNNER` set, `RAWPAGE_HAS_WINDOWS_RUNNER` read —
**I-30**). Three quarters of the test surface had therefore never been exercised by CI at all; it has
now, and it was green first time. A fourth job, `coverage-report`, names in every run summary what
went unexercised when the Windows job does not run, so a skipped job cannot again be misread as
coverage.

**Nothing is carried forward**: I-19 was paid off at T11, A-58 closed at T12, A-53 at T13, I-26…I-28
each in the same change as the code that found them, and I-29 and I-30 the same way at T15.
`core/CoreModule.cpp` — the last Stage 0 placeholder in `core/` — was deleted at T15, and **every file
in the tree now has an `Architecture-FileMap.md` row**. Nothing is implemented ahead of the plan's task
order.

### What now exists as code

- Stage 0's build system, six presets, and the toolchain pin (TA-1…TA-9) refusing any compiler but
  Qt's own GCC 13.1.0/msvcrt; the live gates plus the CTest fixtures proving they fail correctly.
- The four `core/` vocabulary headers (`Result.h`, `Log.h`, `Geometry.h`, `Units.h`).
- **`app/main.cpp` and `app/CrashHandler.{h,cpp}` (T1)** — per-monitor-v2 DPI awareness and the
  unhandled-exception filter both installed *before any Qt object exists*; minidumps to
  `%LOCALAPPDATA%\RawPage\crashes\` with a plain-text sidecar; `scripts/symbolize.ps1` resolves a
  dump to a function name and **refuses a binary whose build id does not match**.
- **`core/cmd/` (T3, T4)** — `ActionId.def` (**361** declared actions — 358 through T15, plus `ui.qatMoveLeft` and `ui.qatMoveRight` at I-43 and `ui.qatLock` with drag-to-reorder), `ActionId.h`, `KeyContext.h`,
  `ActionRegistry`, `Chord`, `KeyBindingDefaults.def` (**240** bindings), `KeyBindingStore`.
- **`core/settings/` (T5)** — `SettingsKeys.def`, `SettingsSchema`, `Settings`, `RecentFiles`. Typed
  keys only: there is no string overload anywhere, and three negative-compile fixtures prove a
  misspelled key, a string key and a wrong-typed value each fail to compile. D1-16's versioning is
  live in both directions — an older file migrates and is restamped, a **newer** file is read for
  what we understand and left byte-identical.
- **`tools/i18n-lint/` + `i18n/` (T6, extended T8/T9/T10/T11/T12)** — the third gate, and `rawpage_en.ts` /
  `rawpage_zh_TW.ts` at **537 keys each** (361 `action.*` + 58 `shell.*` + 70 `ribbon.*`, nine of them the 2026-08-31 `ribbon.keytip.*` access keys + 10 `error.*`
  + 38 generated `prefs.*`), declared in
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
  (add / remove / reorder / placement / lock / reset — commands rather than context-menu lambdas, and
  since 2026-08-31 **reordering is a drag** with the two move commands kept registered but unlisted,
  because a gesture reaches nothing but a pointer and flow K must reach everything), `ToolFinder` (whose *ranking* is a widget-free free function, so the result order is
  asserted without a window), and `UiCustomization` (versioned per D1-16, **carrying a newer build's
  unknown fields through untouched**, and holding action *string ids* so a rename cannot orphan a
  customization). The ribbon body now sits in a `QScrollArea`, which paid off **I-19**.
- **Document tabs and the stub they carry (T12)** — `DocumentTabBar` (reorder, middle-click close,
  tear-off, re-dock, overflow) and `DocumentWindow`, which under **D1-2** holds a path and nothing
  else. **A tear-off is reported on the release, never at the 40 px threshold crossing** (I-34), and
  `detachDocument` refuses to detach a window's only document into a new window while still allowing
  it to be dropped onto another window's strip (I-33) — the guard sits *after* that branch, because a
  torn-off window holds exactly one document by construction. A window emptied by a **move** closes
  itself (I-35), and one emptied by a **close** does not; only a window that owns itself can go, since
  the primary window is a stack object in `app/main.cpp` whose address `main()` holds. `openFiles()` opens a tab per accepted path, still checking only the name and the suffix and
  reading no bytes. **`rawpage.exe` opens PDFs into tabs** — as stubs that say what Stage 2 brings.
- **The keyboard (T13)** — `ui/shell/ShortcutBinder`, one per window, filtering `qApp` and answering
  only for its own window. Three separable parts: `activeContext()`, a pure walk up `parentWidget()`
  to the innermost widget that *declares* a `KeyContext`; `chordFor()`, the QtGui boundary
  `core/cmd/Chord` was created for, driven by **one table read both ways** so `unreachableBindings()`
  can say which §8 row names a key no keyboard can produce (all **240** round-trip — the count is
  `KeyBindingDefaults.def`'s rows, which is what the store holds and the census walks; the figure
  read 248 until I-55 measured it); and `activate()`,
  which resolves and dispatches. **Only two of five outcomes consume the key** — a dispatch and an
  `Unavailable` refusal do, a `NotFound` does not. The focused widget gets **first refusal through
  Qt's own `ShortcutOverride` handshake**, so a `QLineEdit` keeps `Ctrl+A` with no list of text
  widgets to maintain. **Nothing in the Stage 1 shell declares a context**, so `activeContext()`
  answers `Global` throughout — which is the honest state, and is what every §8 Global default is
  resolved against.

- **Preferences, the log, and the rolling log file (T14)** — `ui/dialogs/PreferencesDialog`
  (**modeless**, applying immediately, Close and Restore Defaults and no OK/Cancel: a `Cancel` that
  must un-apply needs a transaction `Settings` deliberately does not have), `ui/dialogs/SettingsPage`
  — which **`#include`s `SettingsKeys.def` itself** and builds one typed editor per row, so adding a
  preference in a later stage is a `.def` row and no widget code — `ui/dialogs/ShortcutsPage`, which
  writes a remap to **both** `KeyBindingStore` and `Settings` and lists the class-1 shadows, and
  `ui/panes/LogPane` over the new `core/LogBuffer` and `app/LogFile`. Three of the 26 preference
  groups ship: **General, Logging, Shortcuts**. `SettingsSchema` gained a fourth artifact — an editor
  kind, a choice set and a `prefs.setting.*` label key, all **derived** from each row's type and path.

- **The measurement, the two-process check, and the CI that runs them (T15)** — `tests/bench/`
  (`bench_ribbon`, A3's R-9: **p95 0.471 ms** against a 16.667 ms frame budget, reported in three
  phases and carrying **its own control**, because the `layout` phase reads 0.000 ms and a zero has
  two explanations — a cheap switch, or a dead instrument. Twenty resizes across the collapse
  threshold answer it at 0.714 ms); `tests/unit/tst_singleinstance.cpp` plus
  `scripts/check-single-instance.ps1` for A-61's two halves; and four CI changes that turn coverage
  claims into assertions. **T15 builds no product surface** — no new catalog key, settings key or
  registered command — and that is what the task is. `core/CoreModule.cpp` was deleted, as its own
  comment had asked since Stage 0.

**Still no document code**: nothing in `core/doc/`, `pdf/cos/`, `pdf/render/`, `pdf/ops/`, or
`ui/viewer/`. `platform/win/` holds two of seventeen interfaces. **`rawpage.exe` opens a window with a
working ribbon, a customizable toolbar, a tool finder, document tabs, a keyboard, a Preferences dialog
and a log pane** —
9 tabs above a sidebar, a status bar and a live theme, and a tab per opened file whose surface names
what Stage 2 brings. Most ribbon buttons are **disabled and say why**: 361 commands are declared and
**twenty-three** are registered, which is the honest state rather than a shortcut. Each of those
twenty-three is reachable by a person and by flow K — through the tool finder, a test, the future
batch runner and its §8 chord — and **nine of them additionally have a ribbon button**
(`file.open`, `file.preferences`, `view.toggleSidebar`, `view.fullScreen`, `view.toggleRibbon`,
`window.new`, `pane.log`, `app.toolFinder`, `ui.resetCustomization`); the rest are toolbar,
tab-strip or context-menu commands. **Not every registered id is on the ribbon**, and the corollary
matters for M-12: no ribbon button is ever the *registered-but-disabled* case; the other ~338 chords resolve, are refused as `NotFound`, and deliberately
leave the key unconsumed. `log.openFolder` is deliberately **not** registered: the folder belongs to
`app/LogFile`, and `ui/` looks outward to exactly two files (convention 9). Because an ordinary launch runs an
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
| `claude-plan/Stage<N>-<Area>-Plan.md` | *Optional per stage.* One stage's task-level execution order and its stage-local design choices. **Three exist:** `Stage0-Toolchain-Plan.md` (tasks T1–T15, decisions D0-1…D0-8, the §7 precondition checklist), `Stage1-AppShell-Plan.md` (tasks T1–T15, decisions D1-1…D1-16, the §7 precondition checklist P-1…P-4) and `Stage2-Rendering-Plan.md` (tasks **T1–T18**, decisions D2-1…D2-14, preconditions P-1…P-4 of which P-2…P-4 are open), written 2026-08-31 before any Stage 2 code. It owns no product facts and no live status — those stay in the four documents above. |
| `claude-plan/Stage<N>-<Area>-Testing-Guide.md` | How one stage is verified — **both** the automated pass I run and the manual pass you walk. **Three exist:** `Stage0-Toolchain-Testing-Guide.md` (run, signed off, closed); `Stage1-AppShell-Testing-Guide.md` (written before Stage 1's code, **fully run and fully walked** — 73 automated cases, 12 manual of which 9 were walkable and all 9 signed off, issues I-01…I-55, none open); and `Stage2-Rendering-Testing-Guide.md`, written from Stage 2's plan before its code — **74 automated (A-01…A-74) and 10 manual (M-01…M-10), and no case in it has been run**. Its one issue, **I-01**, was raised and closed before any code, by *reading* the document set rather than running it: `Architecture-FileMap.md` tagged the `pdf/cos/` reader and three `core/doc/` vocabulary types Stage 3 while `Implementation-Plan.md` — which owns stage ordering — claims them for Stage 2. **Stage 2's guide has its own `A-nn`/`M-nn`/`I-nn` namespace**; a bare `I-34` means Stage 1's. Each is written right after its stage's plan and before that stage's code, per the workflow section below. Do not write one for a stage that is not next; do not treat its absence as a gap. |

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
| `tools/i18n-lint` | ✅ **live** | a literal reaching a user-facing sink and not wrapped in `tr()` (escapable, `// i18n-lint: allow-literal(reason)`); a key missing from a shipped locale; an `ActionId` with no `action.*` catalog entry. `orphan-key` is S3 — reported, never blocking, and a quoted key reference **ending in a dot** is a namespace reference covering every key beneath it, because several catalog families are built from a table at runtime and appear whole in no source (T14, issue I-27). Stage 1 ships `en` and `zh-TW` only; `zh-CN` and `ja` are declared `"shipped": false` (D1-14) |
| `tools/mutation-lint` | ○ **Stage 3** | a `Command` subclass missing from `CommandRegistry`, or lacking a serialize round-trip test or a revert test |

Two rules are escapable, both with an inline marker **on the same line as the match** — not the line
above — and both censused by that gate's `--explain`: `// layer-lint: allow-filesystem-write(reason)`
(**six** waivers exist — three in `app/CrashHandler.cpp`, two in `app/LogFile.cpp` for the rolling log file and its retention, one on the named-pipe `CreateFileW` in
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
ctest --preset x64-release --output-on-failure        # 354/354 as of I-54
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

Two checks that are neither a `ctest` case nor a gate, and each has a recorded reason for not being one:

```powershell
.\build\x64-release\bin\rawpage_bench_ribbon.exe   # A-85 / R-9, the one A3 criterion that is a number
.\scripts\check-single-instance.ps1                 # A-61's two-process half, with its `true` control
```

`check-single-instance.ps1` **edits `%APPDATA%\RawPage\settings.ini` and restores it in a `finally`**,
which is exactly why it is not a CTest case: a suite that rewrote the settings of whoever ran it would
be a worse defect than the one it checks. It has to, because Qt resolves `AppDataLocation` through
`SHGetKnownFolderPath` — measured at T15 — so overriding `APPDATA` for a child process does *not*
redirect the file.

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
| `rawpage_tests` | `rawpage_core`, `rawpage_pdf` | `unit.` | Windows + `portable-asan`. It also compiles **three** `app/` translation units **in** rather than linking them — `CommandLine.cpp` (a pure `QStringList` → intent parse), `LogFile.cpp` (QtCore and nothing else) and `SingleInstance.cpp` (T15; names only `core/` and the interface header `platform/platform.h`) — and compiling rather than linking is what keeps the assertion below true. `rawpage_assert_headless()` **fails configure** if this ever links `rawpage_ui` or `rawpage_platform`, directly or transitively — that assertion is the standing proof that `core/` needs no display and no engine |
| `rawpage_platform_tests` | `rawpage_core`, `rawpage_platform` | `platform.` | **Windows only** (S1 · T7) |
| `rawpage_ui_tests` | `rawpage_core`, `rawpage_ui` | `ui.` | **Windows only**, `QT_QPA_PLATFORM=offscreen` (D1-4). **Arrived at T9**, not the plan's T15, on the precedent `tests/platform` set at T7. It installs `rawpage_en.qm` so the widgets are exercised against real text rather than their own catalog keys |
| `rawpage_bench_ribbon` | `rawpage_core`, `rawpage_ui` | `bench.` | **Windows only**, offscreen (T15). Not a test target and the difference is load-bearing: a test asserts and fails, **a benchmark reports, and its exit code is about whether the measurement was *possible*** — never about whether the machine was fast, or a loaded runner turns a green build red for a reason unrelated to the change. It is registered with CTest anyway, because a benchmark nobody runs is one that has stopped compiling by the time its number matters |

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
